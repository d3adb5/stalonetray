/* -------------------------------
 * vim:tabstop=4:shiftwidth=4
 * order.h
 * -------------------------------
 * Persist icon order across tray restarts.
 *
 * On startup we read a snapshot of the previous layout (keyed by WM_CLASS,
 * with _NET_WM_PID breaking ties between icons of the same class, gated by an
 * X-session token) and use it to restore the order of icons as they redock --
 * window ids are not stable across a tray restart, since apps recreate their
 * dock window, so they are not used for matching. Once every snapshot entry
 * has been
 * reclaimed -- or a timeout elapses -- the snapshot is dropped and from then
 * on the file simply tracks the live order.
 * -------------------------------*/

#ifndef _ORDER_H_
#define _ORDER_H_

#include "icons.h"

/* Load the snapshot and establish the session token. Called once at startup,
 * before any icon docks. No-op when remember_icon_order is off. */
void order_init(void);

/* During the startup window, position a freshly docked icon according to the
 * snapshot (matched by WM_CLASS). Outside that window, or with no matching
 * entry, the icon keeps its gravity-assigned slot. */
void order_place_icon(struct TrayIcon *ti);

/* Persist the current visual order. Deferred (no-op) while the startup window
 * is still open; writes on every order change once settled. */
void order_save(void);

/* Periodic hook: closes the startup window once the timeout elapses. */
void order_periodic(void);

/* True while the startup restore window is still open. The main loop polls
 * instead of blocking during this window so the timeout can fire when idle. */
int order_startup_pending(void);

#endif
