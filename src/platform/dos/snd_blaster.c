/*
 * snd_blaster.c - Sound Blaster-class DAC output for the DOS backend.
 *
 * Detects a card from the BLASTER environment variable (parsed by the portable,
 * host-tested snd_parse_blaster) plus a DSP reset handshake, then renders the
 * shell's tone cues as 8-bit unsigned PCM played through 8237 single-cycle DMA.
 * snd_dos.c prefers this when present and falls back to the PC speaker
 * otherwise -- sound stays strictly optional.
 *
 * This is faithful Sound Blaster spec I/O (DSP ports base+6/A/C/E, DMA
 * controller ports, DSP commands 0x40/0x14/0xD1/0xD3). COMPILED ONLY under
 * CASTALIA_DOS; like vesa.c it cannot run in the host build and is validated on
 * emulator/hardware, not in CI. Every poll loop is bounded so a missing or
 * misbehaving card degrades to silence, never a hang.
 */
#ifdef CASTALIA_DOS

#include "snd_blaster.h"
#include "dos_dpmi.h"
#include "castalia/plat.h"
#include "castalia/sys.h"

#include <conio.h>   /* inp / outp                 */
#include <stdlib.h>  /* getenv                     */
#include <string.h>

#define SB_RATE       11025          /* DAC sample rate (Hz)              */
#define SB_BUFBYTES   8192           /* low-memory DMA buffer size        */
#define SB_BUFPARA    (SB_BUFBYTES / 16)

/* DSP register offsets from the I/O base. */
#define SB_RESET(base)   ((base) + 0x06)
#define SB_READ(base)    ((base) + 0x0A)
#define SB_WRITE(base)   ((base) + 0x0C)  /* bit7=0 -> ready to accept    */
#define SB_RDSTAT(base)  ((base) + 0x0E)  /* bit7=1 -> read data ready    */

/* 8-bit DMA controller port tables, indexed by channel (0..3). */
static const int DMA_ADDR[4]  = { 0x00, 0x02, 0x04, 0x06 };
static const int DMA_COUNT[4] = { 0x01, 0x03, 0x05, 0x07 };
static const int DMA_PAGE[4]  = { 0x87, 0x83, 0x81, 0x82 };

static SndBlaster g_cfg;
static cbool      g_ready = CFALSE;
static cu16       g_buf_seg = 0, g_buf_sel = 0;
static cu32       g_buf_phys = 0;
static long       g_buf_usable = 0;   /* bytes before the 64K DMA page edge */
static char       g_desc[32];
static unsigned char g_pcm[SB_BUFBYTES];

/* A crude few-microsecond delay: reads to the unused POST port 0x80 take
 * roughly one ISA bus cycle each. Bounded and side-effect-free. */
static void io_delay(int n)
{
    int i;
    for (i = 0; i < n; i++) { (void)inp(0x80); }
}

/* ---- DSP primitives, all bounded ------------------------------------- */

static void dsp_write(int base, unsigned char v)
{
    int spins = 0;
    while ((inp(SB_WRITE(base)) & 0x80) != 0 && spins < 10000) { spins++; }
    outp(SB_WRITE(base), v);
}

static int dsp_read(int base)
{
    int spins = 0;
    while ((inp(SB_RDSTAT(base)) & 0x80) == 0 && spins < 10000) { spins++; }
    return inp(SB_READ(base));
}

/* Reset the DSP and confirm the 0xAA ready byte. Returns CTRUE on success. */
static cbool dsp_reset(int base)
{
    int spins;
    outp(SB_RESET(base), 1);
    io_delay(30);                 /* hold >= 3us */
    outp(SB_RESET(base), 0);
    for (spins = 0; spins < 200; spins++) {
        if ((inp(SB_RDSTAT(base)) & 0x80) != 0) {
            return (inp(SB_READ(base)) == 0xAA) ? CTRUE : CFALSE;
        }
        io_delay(4);
    }
    return CFALSE;
}

/* ---- Detection / teardown -------------------------------------------- */

cbool blaster_detect(SndBlaster *cfg_out)
{
    const char *env = getenv("BLASTER");
    g_ready = CFALSE;
    g_desc[0] = '\0';

    if (!snd_parse_blaster(env, &g_cfg)) {
        SYS_LOGI("snd", "no BLASTER variable; using PC speaker");
        return CFALSE;
    }
    /* Only 8-bit DMA channels (0..3) are usable by our single-cycle path. */
    if (g_cfg.dma < 0 || g_cfg.dma > 3) {
        SYS_LOGW("snd", "BLASTER DMA %d out of 8-bit range; using speaker",
                 g_cfg.dma);
        return CFALSE;
    }
    if (!dsp_reset(g_cfg.base)) {
        SYS_LOGW("snd", "no DSP at base %03Xh; using PC speaker", g_cfg.base);
        return CFALSE;
    }

    /* Low-memory, page-aligned-enough DMA buffer. */
    if (dpmi_alloc_dos(SB_BUFPARA, &g_buf_seg, &g_buf_sel) != CE_OK) {
        SYS_LOGW("snd", "no low DMA buffer; using PC speaker");
        return CFALSE;
    }
    g_buf_phys = ((cu32)g_buf_seg) << 4;
    {
        cu32 off = g_buf_phys & 0xFFFFUL;
        long room = (long)(0x10000UL - off);
        g_buf_usable = (room < SB_BUFBYTES) ? room : SB_BUFBYTES;
    }

    dsp_write(g_cfg.base, 0xD1);  /* turn the DAC speaker on */

    sys_snprintf(g_desc, sizeof(g_desc), "A%03X I%d D%d",
                 g_cfg.base, g_cfg.irq, g_cfg.dma);
    SYS_LOGI("snd", "Sound Blaster DAC up (%s, %d Hz)", g_desc, SB_RATE);
    g_ready = CTRUE;
    if (cfg_out) { *cfg_out = g_cfg; }
    return CTRUE;
}

void blaster_shutdown(void)
{
    if (g_ready) {
        dsp_write(g_cfg.base, 0xD3);   /* DAC speaker off */
    }
    if (g_buf_sel != 0) {
        dpmi_free_dos(g_buf_sel);
        g_buf_seg = g_buf_sel = 0;
    }
    g_ready = CFALSE;
}

const char *blaster_desc(void) { return g_desc; }

/* ---- Playback -------------------------------------------------------- */

/* Program the 8237 for a single-cycle read (memory -> DAC) of 'len' bytes. */
static void dma_setup(int ch, cu32 phys, long len)
{
    unsigned page = (unsigned)((phys >> 16) & 0xFF);
    unsigned off  = (unsigned)(phys & 0xFFFF);
    unsigned cnt  = (unsigned)(len - 1);

    outp(0x0A, 0x04 | ch);              /* mask channel                     */
    outp(0x0C, 0x00);                   /* clear byte flip-flop             */
    outp(0x0B, 0x48 | ch);              /* single mode, read transfer       */
    outp(DMA_ADDR[ch],  off & 0xFF);
    outp(DMA_ADDR[ch],  (off >> 8) & 0xFF);
    outp(DMA_PAGE[ch],  page);
    outp(DMA_COUNT[ch], cnt & 0xFF);
    outp(DMA_COUNT[ch], (cnt >> 8) & 0xFF);
    outp(0x0A, ch);                     /* unmask channel                   */
}

void blaster_play_tone(int freq_hz, int ms)
{
    long len, i;
    long half;
    int  base;
    unsigned tc;

    if (!g_ready) { return; }
    if (freq_hz <= 0 || ms <= 0) { return; }
    base = g_cfg.base;

    len = (long)SB_RATE * (long)ms / 1000L;
    if (len < 1)             { len = 1; }
    if (len > g_buf_usable)  { len = g_buf_usable; }

    /* Synthesize an 8-bit unsigned square wave (0x40/0xC0 avoids DC pops). */
    if (freq_hz > SB_RATE / 2) { freq_hz = SB_RATE / 2; }
    half = (long)SB_RATE / (long)freq_hz / 2L;
    if (half < 1) { half = 1; }
    for (i = 0; i < len; i++) {
        g_pcm[i] = ((i / half) & 1L) ? 0xC0 : 0x40;
    }
    dpmi_copy_to_dos(g_buf_seg, 0, g_pcm, (cu32)len);

    /* Sample rate via time constant, then start an 8-bit single-cycle block. */
    tc = 256U - (unsigned)(1000000L / SB_RATE);
    dsp_write(base, 0x40);
    dsp_write(base, (unsigned char)tc);

    dma_setup(g_cfg.dma, g_buf_phys, len);

    dsp_write(base, 0x14);                       /* 8-bit single-cycle out   */
    dsp_write(base, (unsigned char)((len - 1) & 0xFF));
    dsp_write(base, (unsigned char)(((len - 1) >> 8) & 0xFF));

    /* Block for the tone, then acknowledge the completion IRQ at the DSP. */
    plat_sleep_ms((cu32)ms);
    (void)inp(SB_RDSTAT(base));                  /* ack 8-bit DMA-done       */
}

#endif /* CASTALIA_DOS */
