/*
 * net_pkt.c - Packet-driver networking client for the DOS backend.
 *
 * Locates an installed FTP-Software-style packet driver (the same interface
 * mTCP uses), reads its class/version and station (MAC) address, and sends raw
 * Ethernet frames through it. This is the link-layer foundation an IP/UDP/TCP
 * stack can be built on later.
 *
 * Faithful to the Packet Driver Specification:
 *   - detect: scan software-interrupt vectors 0x60..0x80 for the "PKT DRVR"
 *     signature 3 bytes into the handler;
 *   - driver_info (AH=1) for class/version/number;
 *   - get_address (AH=6) for the station address;
 *   - send_pkt (AH=4) to transmit a frame from a low-memory buffer.
 * The RECEIVE path (access_type + a real-mode receiver callback the driver
 * invokes) is a documented next step; net_poll_frame returns 0 for now.
 *
 * COMPILED ONLY under CASTALIA_DOS; like vesa.c/snd_blaster.c it cannot run in
 * the host build and is validated on emulator/hardware, not CI. The address and
 * checksum helpers it complements (net_util.c) are portable and host-tested.
 */
#ifdef CASTALIA_DOS

#include "castalia/net.h"
#include "castalia/sys.h"
#include "dos_dpmi.h"

#include <string.h>

#define PKT_BUF_PARA ((NET_MTU + 15) / 16 + 4)   /* low DMA/frame buffer     */

static cbool         g_up = CFALSE;
static int           g_int = 0;
static int           g_class = 0;
static int           g_version = 0;
static cu8           g_mac[NET_MAC_LEN];
static char          g_name[40];
static cu16          g_buf_seg = 0, g_buf_sel = 0;

/* Scan the software-interrupt vectors for the packet-driver signature. */
static int find_driver(void)
{
    int v;
    for (v = 0x60; v <= 0x80; v++) {
        cu16 seg, off;
        const char *sig;
        if (dpmi_get_real_vector(v, &seg, &off) != CE_OK) { continue; }
        if (seg == 0 && off == 0) { continue; }
        /* Low memory is identity-mapped; "PKT DRVR" sits 3 bytes into the
         * handler entry point. */
        sig = (const char *)(((cu32)seg << 4) + (cu32)off + 3);
        if (memcmp(sig, "PKT DRVR", 8) == 0) { return v; }
    }
    return -1;
}

/* driver_info (AH=1, AL=0xFF): fill class/version. Returns CTRUE on success. */
static cbool driver_info(int vec)
{
    DpmiRegs r;
    memset(&r, 0, sizeof(r));
    r.eax = 0x01FF;                 /* AH=1 driver_info, AL=0xFF             */
    if (dpmi_int(vec, &r) != CE_OK) { return CFALSE; }
    if (r.flags & 0x01) { return CFALSE; }   /* carry set -> error          */
    g_version = (int)(r.ebx & 0xFFFF);
    g_class   = (int)((r.ecx >> 8) & 0xFF);  /* CH = class                  */
    return CTRUE;
}

/* get_address (AH=6): read the station (MAC) address into g_mac. */
static cbool get_address(int vec)
{
    DpmiRegs r;
    memset(&r, 0, sizeof(r));
    r.eax = 0x0600;                 /* AH=6 get_address                      */
    r.ebx = 0;                      /* interface / handle 0                  */
    r.es  = g_buf_seg;
    r.edi = 0;
    r.ecx = NET_MAC_LEN;
    if (dpmi_int(vec, &r) != CE_OK) { return CFALSE; }
    if (r.flags & 0x01) { return CFALSE; }
    dpmi_copy_from_dos(g_mac, g_buf_seg, 0, NET_MAC_LEN);
    return CTRUE;
}

CResult net_init(void)
{
    int vec;
    g_up = CFALSE;
    memset(g_mac, 0, sizeof(g_mac));
    sys_strlcpy(g_name, "None (no network)", sizeof(g_name));

    vec = find_driver();
    if (vec < 0) {
        SYS_LOGI("net", "no packet driver found (vectors 60h..80h)");
        return CE_UNSUPPORTED;
    }
    g_int = vec;

    if (dpmi_alloc_dos(PKT_BUF_PARA, &g_buf_seg, &g_buf_sel) != CE_OK) {
        SYS_LOGW("net", "no low buffer for packet driver");
        return CE_NOMEM;
    }

    (void)driver_info(vec);   /* best-effort: class/version for reporting */
    (void)get_address(vec);   /* best-effort: station address            */

    sys_snprintf(g_name, sizeof(g_name),
                 "Packet driver @ INT %02Xh (class %d)", vec, g_class);
    SYS_LOGI("net", "%s ver %d, MAC %02X:%02X:%02X:%02X:%02X:%02X",
             g_name, g_version, g_mac[0], g_mac[1], g_mac[2],
             g_mac[3], g_mac[4], g_mac[5]);
    g_up = CTRUE;
    return CE_OK;
}

void net_shutdown(void)
{
    if (g_buf_sel != 0) {
        dpmi_free_dos(g_buf_sel);
        g_buf_seg = g_buf_sel = 0;
    }
    g_up = CFALSE;
}

cbool net_available(void) { return g_up; }

void net_device_info(NetDeviceInfo *out)
{
    if (out == NULL) { return; }
    memset(out, 0, sizeof(*out));
    out->available = g_up;
    out->int_no    = g_int;
    out->class_id  = g_class;
    memcpy(out->mac, g_mac, NET_MAC_LEN);
    sys_strlcpy(out->name, g_name, sizeof(out->name));
}

CResult net_send_frame(const void *frame, cu32 len)
{
    DpmiRegs r;
    if (!g_up) { return CE_UNSUPPORTED; }
    if (frame == NULL || len == 0 || len > NET_MTU) { return CE_INVALID; }

    dpmi_copy_to_dos(g_buf_seg, 0, frame, len);
    memset(&r, 0, sizeof(r));
    r.eax = 0x0400;                 /* AH=4 send_pkt                         */
    r.ds  = g_buf_seg;
    r.esi = 0;
    r.ecx = len;
    if (dpmi_int(g_int, &r) != CE_OK) { return CE_FAIL; }
    if (r.flags & 0x01) { return CE_FAIL; }
    return CE_OK;
}

int net_poll_frame(void *buf, cu32 cap)
{
    CASTALIA_UNUSED(buf);
    CASTALIA_UNUSED(cap);
    /* Receiving needs an access_type receiver callback the driver invokes in
     * real mode; that is the next step. No frames are surfaced yet. */
    return 0;
}

#endif /* CASTALIA_DOS */
