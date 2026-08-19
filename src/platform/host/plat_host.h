/*
 * plat_host.h - Host-backend-only hooks for the headless front-end.
 *
 * These are NOT part of the portable platform contract (plat.h). Only the
 * host driver (main.c under CASTALIA_HOST) and host tests use them to
 * synthesize input without a real display.
 */
#ifndef CASTALIA_PLAT_HOST_H
#define CASTALIA_PLAT_HOST_H

#include "castalia/plat.h"

/* Enqueue a synthetic input event (mouse/keyboard) for plat_poll_event. */
void plat_host_push_event(const PlatEvent *ev);
/* Set the reported mouse position/buttons directly. */
void plat_host_set_mouse(int x, int y, int buttons);

/* Turn the simulated network wire on (see net_host.c). Off by default, so the
 * host build reports "no device" unless a demo explicitly asks for it. Must be
 * called before net_init(). */
void plat_host_set_loopback(cbool on);
long plat_host_net_tx(void);
long plat_host_net_rx(void);

/*
 * The last typematic rate the platform was ASKED for, whether or not it could
 * do anything about it (a host cannot). Nothing above the platform layer is
 * in the key-repeat loop, so on a host there is no behaviour to observe and
 * this is the only way a scene can tell the setting travelled from the panel
 * rather than stopping in the settings struct. Both -1 until asked.
 */
void plat_host_last_key_repeat(int *delay_ms, int *cps);

#endif /* CASTALIA_PLAT_HOST_H */
