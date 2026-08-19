/*
 * app_net.c - CastaliaOS Network: adapter status, address configuration,
 * the live ARP table, and a ping tool.
 *
 * The window is a thin skin over net_stack.c (the ARP/IPv4/ICMP/UDP layer)
 * and net.h (the raw-frame seam). It is also a real, if minimal, host on the
 * wire: while it is open it answers ARP requests for its address and replies
 * to incoming echo requests, because a machine that only talks and never
 * answers is not on the network, it is shouting at it.
 *
 * A ping is driven from the frame timer rather than a blocking loop -- the
 * shell is cooperative and single-threaded, so the app sends one request, then
 * polls for frames each frame until the reply arrives or the deadline passes.
 * The timer only runs while a ping is in flight; an idle window costs nothing.
 */
#include "apps.h"
#include "castalia/wm.h"
#include "castalia/ui.h"
#include "castalia/gfx.h"
#include "castalia/plat.h"
#include "castalia/sys.h"
#include "castalia/net.h"
#include "castalia/net_stack.h"

#define NA_PAD      8
#define NA_ROW      14
#define NA_LOG      12          /* lines of ping output kept                */
#define NA_LOG_TEXT 48
#define NA_TIMEOUT  1200        /* ms before a request is given up on       */
#define NA_COUNT    4           /* echo requests per ping run               */

enum { NA_IDLE = 0, NA_RESOLVING, NA_WAITING };

typedef struct {
    NetStack ns;
    NetDeviceInfo dev;
    cbool  up;
    int    state;
    cu32   target;              /* address being pinged                     */
    cu32   hop;                 /* who we actually send it to               */
    cu16   seq;                 /* sequence of the request in flight        */
    cu32   sent_at;             /* plat_ticks_ms when it went out           */
    int    left;                /* echo requests still to send              */
    int    ok, lost;
    long   rtt_sum;
    char   log[NA_LOG][NA_LOG_TEXT];
    int    log_n;
    char   target_text[24];
    char   status[64];
    UiHot hot;    /* which of the four buttons the pointer is on */
} NetApp;

typedef struct {
    CRect adapter, config, arp, ping;
    CRect target, btn_ping, btn_stop, btn_ip, btn_gw;
/* The four buttons, in one order, so hit-testing and painting agree. */
#define NAB_PING 0
#define NAB_STOP 1
#define NAB_IP   2
#define NAB_GW   3
#define NAB_N    4
    CRect arp_list, log_list;
} NaLayout;

/* ---- layout ------------------------------------------------------------ */
static void na_layout(WmWindow *win, NaLayout *L)
{
    CRect c = wm_client_rect(win);
    int cw = crect_w(&c), ch = crect_h(&c);
    int y = NA_PAD, half;

    /*
     * Tall enough for the three rows it actually contains.
     *
     * `3 * NA_ROW + 16` was 58, and the rows do not fit in 58: they start at
     * +16, step by NA_ROW + 2, and the last box is NA_ROW tall, so the content
     * ends at 16 + 2*(NA_ROW+2) + NA_ROW = 62. The bottom field overflowed the
     * group frame by four pixels -- visible as the Status box sitting across
     * the group's own bottom border, in both of these groups, because they
     * share the formula.
     */
#define NA_GROUP_H (16 + 2 * (NA_ROW + 2) + NA_ROW + 6)
    L->adapter = crect_make(NA_PAD, y, cw - 2 * NA_PAD, NA_GROUP_H);
    y = L->adapter.y1 + 8;

    L->config = crect_make(NA_PAD, y, cw - 2 * NA_PAD, NA_GROUP_H);
    L->btn_ip = crect_make(L->config.x1 - 96, L->config.y0 + 16, 88, 18);
    L->btn_gw = crect_make(L->config.x1 - 96, L->config.y0 + 38, 88, 18);
    y = L->config.y1 + 8;

    half = (cw - 2 * NA_PAD - 8) / 2;
    L->arp = crect_make(NA_PAD, y, half, ch - y - NA_PAD);
    L->arp_list = crect_make(L->arp.x0 + 6, L->arp.y0 + 16,
                             crect_w(&L->arp) - 12, crect_h(&L->arp) - 22);

    L->ping = crect_make(NA_PAD + half + 8, y, cw - 2 * NA_PAD - half - 8,
                         ch - y - NA_PAD);
    L->target = crect_make(L->ping.x0 + 6, L->ping.y0 + 18,
                           crect_w(&L->ping) - 12 - 96, 18);
    L->btn_ping = crect_make(L->ping.x1 - 90, L->ping.y0 + 18, 40, 18);
    L->btn_stop = crect_make(L->ping.x1 - 46, L->ping.y0 + 18, 40, 18);
    /* The log stops short of the foot so the summary line has its own room. */
    L->log_list = crect_make(L->ping.x0 + 6, L->ping.y0 + 42,
                             crect_w(&L->ping) - 12, crect_h(&L->ping) - 62);
}

/* ---- log --------------------------------------------------------------- */
static void na_log(NetApp *a, const char *text)
{
    int i;
    if (a->log_n < NA_LOG) {
        sys_strlcpy(a->log[a->log_n], text, sizeof a->log[0]);
        a->log_n++;
        return;
    }
    for (i = 0; i < NA_LOG - 1; i++) {
        sys_strlcpy(a->log[i], a->log[i + 1], sizeof a->log[0]);
    }
    sys_strlcpy(a->log[NA_LOG - 1], text, sizeof a->log[0]);
}

/* ---- the wire ---------------------------------------------------------- */
/* Send one frame; CTRUE when the backend took it. */
static cbool na_send(const cu8 *frame, cu32 len)
{
    return (len > 0 && net_send_frame(frame, len) == CE_OK) ? CTRUE : CFALSE;
}

/* Drain everything the backend has for us. Answers what a host must answer,
 * and reports whether the reply we are waiting for arrived. */
static void na_pump(NetApp *a, WmWindow *win)
{
    static cu8 frame[NET_MTU];
    static cu8 out[NET_MTU];
    NsRx rx;
    int got;
    char line[NA_LOG_TEXT], ip[24];

    while ((got = net_poll_frame(frame, (cu32)sizeof frame)) > 0) {
        switch (ns_receive(&a->ns, frame, (cu32)got, &rx)) {
        case NS_RX_ARP_REQUEST:
            na_send(out, ns_build_arp_reply(&a->ns, rx.src_ip, rx.src_mac,
                                            out, (cu32)sizeof out));
            break;
        case NS_RX_PING_REQUEST:
            na_send(out, ns_build_ping_reply(&a->ns, &rx, out,
                                             (cu32)sizeof out));
            net_ipv4_format(rx.src_ip, ip, sizeof ip);
            sys_snprintf(line, sizeof line, "Echo from %s answered", ip);
            na_log(a, line);
            break;
        case NS_RX_PING_REPLY:
            if (a->state == NA_WAITING && rx.seq == a->seq) {
                long ms = (long)(plat_ticks_ms() - a->sent_at);
                net_ipv4_format(rx.src_ip, ip, sizeof ip);
                sys_snprintf(line, sizeof line, "Reply %s: seq=%d ttl=%d %ldms",
                             ip, (int)rx.seq, rx.ttl, ms);
                na_log(a, line);
                a->ok++;
                a->rtt_sum += ms;
                a->state = NA_IDLE;         /* the next one starts on tick  */
            }
            break;
        case NS_RX_MALFORMED:
            na_log(a, "Malformed frame dropped");
            break;
        default:
            break;
        }
        wm_invalidate(win, NULL);
    }
}

/* Send the next echo request of the run, resolving the hop first if needed. */
static void na_step(NetApp *a, WmWindow *win)
{
    static cu8 out[NET_MTU];
    cu8 mac[NET_MAC_LEN];
    char line[NA_LOG_TEXT], ip[24];

    if (a->left <= 0) {
        if (a->state == NA_IDLE && a->target != 0) {
            long avg = (a->ok > 0) ? a->rtt_sum / a->ok : 0;
            sys_snprintf(a->status, sizeof a->status,
                         "%d received, %d lost, average %ldms",
                         a->ok, a->lost, avg);
            a->target = 0;
            wm_set_animated(win, CFALSE);   /* idle again: stop the timer   */
            wm_invalidate(win, NULL);
        }
        return;
    }
    if (!ns_arp_get(&a->ns, a->hop, mac)) {
        if (a->state != NA_RESOLVING) {
            na_send(out, ns_build_arp_request(&a->ns, a->hop, out,
                                              (cu32)sizeof out));
            a->state = NA_RESOLVING;
            a->sent_at = plat_ticks_ms();
        } else if (plat_ticks_ms() - a->sent_at > NA_TIMEOUT) {
            net_ipv4_format(a->hop, ip, sizeof ip);
            sys_snprintf(line, sizeof line, "No route to %s", ip);
            na_log(a, line);
            a->lost += a->left;
            a->left = 0;
            a->state = NA_IDLE;
        }
        return;
    }
    if (a->state == NA_WAITING) {
        if (plat_ticks_ms() - a->sent_at <= NA_TIMEOUT) { return; }
        na_log(a, "Request timed out");
        a->lost++;
        a->left--;
        a->state = NA_IDLE;
        return;
    }
    if (na_send(out, ns_build_ping(&a->ns, a->target, mac, 32, &a->seq,
                                   out, (cu32)sizeof out))) {
        a->sent_at = plat_ticks_ms();
        a->state = NA_WAITING;
        a->left--;
    } else {
        na_log(a, "Send failed");
        a->left = 0;
        a->state = NA_IDLE;
    }
}

static void na_start_ping(NetApp *a, WmWindow *win, const char *text)
{
    cu32 ip = 0;
    char line[NA_LOG_TEXT], buf[24];
    if (!a->up) {
        sys_strlcpy(a->status, "No network adapter", sizeof a->status);
        return;
    }
    if (!net_ipv4_parse(text, &ip) || ip == 0) {
        sys_snprintf(a->status, sizeof a->status, "'%s' is not an address",
                     text);
        return;
    }
    sys_strlcpy(a->target_text, text, sizeof a->target_text);
    a->target = ip;
    a->hop = ns_next_hop(&a->ns, ip);
    a->left = NA_COUNT;
    a->ok = a->lost = 0;
    a->rtt_sum = 0;
    a->state = NA_IDLE;
    net_ipv4_format(ip, buf, sizeof buf);
    sys_snprintf(line, sizeof line, "Pinging %s...", buf);
    na_log(a, line);
    sys_snprintf(a->status, sizeof a->status, "Pinging %s", buf);
    wm_set_animated(win, CTRUE);           /* run the timer while in flight */
    wm_invalidate(win, NULL);
}

/* ---- painting ---------------------------------------------------------- */
/* A titled group box; returns the rect its contents may use. */
static void na_group(GfxSurface *s, const CRect *r, const char *title)
{
    const UiPalette *p = ui_palette();
    CRect box = *r;
    box.y0 += 5;
    gfx_bevel(s, &box, GFX_BEVEL_ETCHED, p->light, p->dark, GFX_NO_FILL);
    {
        int tw = gfx_text_width(GFX_FONT_BOLD, title) + 6;
        CRect gap = crect_make(r->x0 + 8, r->y0, tw, 10);
        gfx_fill_rect(s, &gap, p->face);
        gfx_draw_text(s, GFX_FONT_BOLD, r->x0 + 11, r->y0, title, p->text);
    }
}

static void na_field(GfxSurface *s, int x, int y, int w,
                     const char *label, const char *value)
{
    const UiPalette *p = ui_palette();
    CRect box;
    gfx_draw_text(s, GFX_FONT_SYSTEM, x, y + 3, label, p->text);
    box = crect_make(x + 84, y, w, NA_ROW);
    gfx_bevel(s, &box, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark,
              GFX_RGB(0xFF, 0xFF, 0xFF));
    gfx_draw_text(s, GFX_FONT_SYSTEM, box.x0 + 4, box.y0 + 3, value, p->text);
}

cbool app_net_ping_button(struct WmWindow *win, CRect *out)
{
    NaLayout L;
    if (win == NULL || out == NULL) { return CFALSE; }
    if (wm_user(win) == NULL) { return CFALSE; }
    na_layout(win, &L);
    *out = L.btn_ping;
    return CTRUE;
}

static void na_paint(WmWindow *win, GfxSurface *s)
{
    NetApp *a = (NetApp *)wm_user(win);
    const UiPalette *p = ui_palette();
    CPoint o = wm_client_origin(win);
    NaLayout L;
    CRect c, r, well;
    char buf[48], v[24];
    int i, x, y, fw;

    if (a == NULL) { return; }
    na_layout(win, &L);
    c = wm_client_rect(win);
    gfx_fill_rect(s, &c, p->face);

    /* Adapter. */
    r = crect_offset(&L.adapter, o.x, o.y);
    na_group(s, &r, "Adapter");
    x = r.x0 + 10;
    y = r.y0 + 16;
    /*
     * One width for all three, because they are one column and a column with
     * ragged right edges reads as a mistake. Device was `crect_w(&r) - 104`
     * while Address and Status were a fixed 140, so the group had a full-width
     * box above two short ones -- which is what it looked like.
     *
     * The TCP/IP group below already does this: three fields, all 130.
     */
    fw = crect_w(&r) - 104;
    na_field(s, x, y, fw, "Device", a->dev.name);
    net_mac_format(a->dev.mac, v, sizeof v);
    na_field(s, x, y + NA_ROW + 2, fw, "Address",
             a->up ? v : "--:--:--:--:--:--");
    na_field(s, x, y + 2 * (NA_ROW + 2), fw, "Status",
             a->up ? "Connected" : "No packet driver");

    /* Configuration. */
    r = crect_offset(&L.config, o.x, o.y);
    na_group(s, &r, "TCP/IP");
    x = r.x0 + 10;
    y = r.y0 + 16;
    net_ipv4_format(a->ns.ip, v, sizeof v);
    na_field(s, x, y, 130, "IP address", v);
    net_ipv4_format(a->ns.mask, v, sizeof v);
    na_field(s, x, y + NA_ROW + 2, 130, "Subnet mask", v);
    net_ipv4_format(a->ns.gateway, v, sizeof v);
    na_field(s, x, y + 2 * (NA_ROW + 2), 130, "Gateway", v);
    r = crect_offset(&L.btn_ip, o.x, o.y);
    ui_draw_button(s, &r, "Address...",
                   ui_hot_state(&a->hot, NAB_IP, UI_BTN_NORMAL));
    r = crect_offset(&L.btn_gw, o.x, o.y);
    ui_draw_button(s, &r, "Gateway...",
                   ui_hot_state(&a->hot, NAB_GW, UI_BTN_NORMAL));

    /* ARP table. */
    r = crect_offset(&L.arp, o.x, o.y);
    na_group(s, &r, "ARP table");
    well = crect_offset(&L.arp_list, o.x, o.y);
    gfx_bevel(s, &well, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark,
              GFX_RGB(0xFF, 0xFF, 0xFF));
    {
        CRect saved;
        CRect in = crect_inset(&well, 2);
        int rows = crect_h(&in) / 11;
        cu32 ip;
        cu8 mac[NET_MAC_LEN];
        saved = gfx_clip_narrow(s, &in);
        if (ns_arp_count(&a->ns) == 0) {
            gfx_draw_text_rect(s, GFX_FONT_SYSTEM, &in, "(empty)",
                               p->text_disabled,
                               GFX_ALIGN_HCENTER | GFX_ALIGN_VCENTER);
        }
        for (i = 0; i < rows && ns_arp_entry(&a->ns, i, &ip, mac); i++) {
            char m[24];
            net_ipv4_format(ip, v, sizeof v);
            net_mac_format(mac, m, sizeof m);
            sys_snprintf(buf, sizeof buf, "%-15s %s", v, m);
            gfx_draw_text(s, GFX_FONT_SYSTEM, in.x0 + 3, in.y0 + 2 + i * 11,
                          buf, p->text);
        }
        gfx_set_clip(s, &saved);
    }

    /* Ping. */
    r = crect_offset(&L.ping, o.x, o.y);
    na_group(s, &r, "Ping");
    well = crect_offset(&L.target, o.x, o.y);
    gfx_bevel(s, &well, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark,
              GFX_RGB(0xFF, 0xFF, 0xFF));
    gfx_draw_text(s, GFX_FONT_SYSTEM, well.x0 + 4, well.y0 + 5,
                  a->target_text, p->text);
    r = crect_offset(&L.btn_ping, o.x, o.y);
    ui_draw_button(s, &r, "Ping",
                   ui_hot_state(&a->hot, NAB_PING,
                                a->up ? UI_BTN_NORMAL : UI_BTN_DISABLED));
    r = crect_offset(&L.btn_stop, o.x, o.y);
    ui_draw_button(s, &r, "Stop",
                   ui_hot_state(&a->hot, NAB_STOP,
                                (a->target != 0) ? UI_BTN_NORMAL
                                                 : UI_BTN_DISABLED));

    well = crect_offset(&L.log_list, o.x, o.y);
    gfx_bevel(s, &well, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark,
              GFX_RGB(0x00, 0x00, 0x00));
    {
        CRect saved;
        CRect in = crect_inset(&well, 2);
        int rows = crect_h(&in) / 11;
        int first = (a->log_n > rows) ? a->log_n - rows : 0;
        saved = gfx_clip_narrow(s, &in);
        for (i = first; i < a->log_n; i++) {
            gfx_draw_text(s, GFX_FONT_SYSTEM, in.x0 + 3,
                          in.y0 + 2 + (i - first) * 11, a->log[i],
                          GFX_RGB(0x46, 0xF0, 0x74));
        }
        gfx_set_clip(s, &saved);
    }

    /* Summary line along the foot of the ping box. */
    r = crect_offset(&L.ping, o.x, o.y);
    gfx_draw_text(s, GFX_FONT_SYSTEM, r.x0 + 6, r.y1 - 13, a->status,
                  p->text);
}

/* ---- input ------------------------------------------------------------- */
static void na_on_ping(cbool ok, const char *text, void *user)
{
    WmWindow *win = (WmWindow *)user;
    NetApp *a = (win != NULL) ? (NetApp *)wm_user(win) : NULL;
    if (!ok || a == NULL || text == NULL) { return; }
    na_start_ping(a, win, text);
}

static void na_on_address(cbool ok, const char *text, void *user)
{
    WmWindow *win = (WmWindow *)user;
    NetApp *a = (win != NULL) ? (NetApp *)wm_user(win) : NULL;
    cu32 ip = 0;
    if (!ok || a == NULL || text == NULL) { return; }
    if (net_ipv4_parse(text, &ip)) {
        a->ns.ip = ip;
        sys_strlcpy(a->status, "Address changed", sizeof a->status);
    } else {
        sys_strlcpy(a->status, "Not a valid address", sizeof a->status);
    }
    wm_invalidate(win, NULL);
}

static void na_on_gateway(cbool ok, const char *text, void *user)
{
    WmWindow *win = (WmWindow *)user;
    NetApp *a = (win != NULL) ? (NetApp *)wm_user(win) : NULL;
    cu32 ip = 0;
    if (!ok || a == NULL || text == NULL) { return; }
    if (net_ipv4_parse(text, &ip)) {
        a->ns.gateway = ip;
        sys_strlcpy(a->status, "Gateway changed", sizeof a->status);
    } else {
        sys_strlcpy(a->status, "Not a valid address", sizeof a->status);
    }
    wm_invalidate(win, NULL);
}

/* Which of the four buttons is at (px,py), or -1. */
static int na_btn_at(const NaLayout *L, int px, int py)
{
    if (crect_contains(&L->btn_ping, px, py)) { return NAB_PING; }
    if (crect_contains(&L->btn_stop, px, py)) { return NAB_STOP; }
    if (crect_contains(&L->btn_ip, px, py))   { return NAB_IP; }
    if (crect_contains(&L->btn_gw, px, py))   { return NAB_GW; }
    return -1;
}

static cbool na_click(WmWindow *win, NetApp *a, int px, int py)
{
    NaLayout L;
    char v[24];
    na_layout(win, &L);
    (void)ui_hot_press(&a->hot, na_btn_at(&L, px, py));

    if (crect_contains(&L.btn_ping, px, py) ||
        crect_contains(&L.target, px, py)) {
        if (!a->up) {
            sys_strlcpy(a->status, "No network adapter", sizeof a->status);
            wm_invalidate(win, NULL);
            return CTRUE;
        }
        ui_prompt("Ping", "Address to ping:", a->target_text, na_on_ping, win);
        return CTRUE;
    }
    if (crect_contains(&L.btn_stop, px, py)) {
        if (a->target != 0) {
            a->left = 0;
            a->target = 0;
            a->state = NA_IDLE;
            wm_set_animated(win, CFALSE);
            na_log(a, "Stopped");
            sys_strlcpy(a->status, "Stopped", sizeof a->status);
            wm_invalidate(win, NULL);
        }
        return CTRUE;
    }
    if (crect_contains(&L.btn_ip, px, py)) {
        net_ipv4_format(a->ns.ip, v, sizeof v);
        ui_prompt("IP Address", "This machine's address:", v,
                  na_on_address, win);
        return CTRUE;
    }
    if (crect_contains(&L.btn_gw, px, py)) {
        net_ipv4_format(a->ns.gateway, v, sizeof v);
        ui_prompt("Gateway", "Default gateway:", v, na_on_gateway, win);
        return CTRUE;
    }
    return CFALSE;
}

static cbool net_proc(WmWindow *win, WmMessage msg, long a, long b, void *param)
{
    NetApp *app = (NetApp *)wm_user(win);
    switch (msg) {
    case WM_MSG_PAINT:       na_paint(win, (GfxSurface *)param); return CTRUE;
    case WM_MSG_LBUTTONDOWN: return na_click(win, app, (int)a, (int)b);
    case WM_MSG_MOUSEMOVE: {
        NaLayout L;
        if (app == NULL) { return CFALSE; }
        na_layout(win, &L);
        if (ui_hot_move(&app->hot, na_btn_at(&L, (int)a, (int)b))) {
            CRect br[NAB_N];
            br[NAB_PING] = L.btn_ping; br[NAB_STOP] = L.btn_stop;
            br[NAB_IP]   = L.btn_ip;   br[NAB_GW]   = L.btn_gw;
            ui_hot_repaint(win, &app->hot, br, NAB_N);
        }
        return CTRUE;
    }
    case WM_MSG_LBUTTONUP:
    case WM_MSG_MOUSELEAVE: {
        cbool redraw;
        if (app == NULL) { return CFALSE; }
        redraw = ui_hot_release(&app->hot);
        if (msg == WM_MSG_MOUSELEAVE && ui_hot_move(&app->hot, -1)) {
            redraw = CTRUE;
        }
        if (redraw) {
            NaLayout L;
            CRect br[NAB_N];
            na_layout(win, &L);
            br[NAB_PING] = L.btn_ping; br[NAB_STOP] = L.btn_stop;
            br[NAB_IP]   = L.btn_ip;   br[NAB_GW]   = L.btn_gw;
            ui_hot_repaint(win, &app->hot, br, NAB_N);
        }
        return CTRUE;
    }
    case WM_MSG_TIMER:
        if (app != NULL && app->up) {
            na_pump(app, win);
            na_step(app, win);
        }
        return CTRUE;
    case WM_MSG_KEYDOWN:
        if ((int)a == PLAT_KEY_ENTER && app != NULL) {
            ui_prompt("Ping", "Address to ping:", app->target_text,
                      na_on_ping, win);
            return CTRUE;
        }
        return CFALSE;
    case WM_MSG_DESTROY:
        if (app != NULL) { sys_free(app, (cu32)sizeof(NetApp)); }
        return CTRUE;
    default:
        (void)b;
        return CFALSE;
    }
}

void app_net_open(void)
{
    NetApp *a;
    WmWindow *w;
    PlatVideoInfo vi;
    CRect frame;
    int fw = 470, fh = 340, fx, fy;

    a = (NetApp *)sys_calloc(1, (cu32)sizeof(NetApp));
    if (a == NULL) { SYS_LOGE("app", "net: OOM"); return; }
    net_device_info(&a->dev);
    a->up = net_available();
    /* A plausible static configuration to start from; the buttons change it.
     * 10.0.2.x is what the emulators hand out, so it is the least surprising
     * default on the machines this is most often run on. */
    ns_init(&a->ns, a->dev.mac, 0x0A00020FUL, 0xFFFFFF00UL, 0x0A000202UL);
    sys_strlcpy(a->target_text, "10.0.2.2", sizeof a->target_text);
    sys_strlcpy(a->status, a->up ? "Ready" : "No adapter -- running offline",
                sizeof a->status);
    na_log(a, a->up ? "Adapter ready" : "No packet driver found");

    plat_video_info(&vi);
    fx = (vi.width - fw) / 2;
    fy = (vi.height - fh) / 2;
    if (fx < 0) { fx = 0; }
    if (fy < 0) { fy = 0; }
    frame = crect_make(fx, fy, fw, fh);

    w = wm_create("Network", &frame,
                  WM_STYLE_TITLE | WM_STYLE_CLOSE | WM_STYLE_MINIMIZE |
                  WM_STYLE_BORDER, net_proc, a);
    if (w == NULL) { sys_free(a, (cu32)sizeof(NetApp)); return; }
    wm_show(w, CTRUE);
    wm_invalidate(w, NULL);
    SYS_LOGI("app", "opened Network (adapter %s)", a->dev.name);
}
