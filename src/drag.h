/* -------------------------------
 * vim:tabstop=4:shiftwidth=4
 * drag.h
 * -------------------------------
 * Drag-to-reorder tray icons.
 * -------------------------------*/

#ifndef _DRAG_H_
#define _DRAG_H_

#include <X11/Xlib.h>

#include "icons.h"

/* One-time setup. Safe to call without an active drag in flight. */
void drag_init(void);

/* Arm the passive Button1+modifier grab on ti's mid-parent. Called from
 * embedder_embed once mid_parent exists. No-op when drag_reorder is off
 * or no modifier is configured. */
void drag_install_grab(struct TrayIcon *ti);

/* Dispatch button/motion events that belong to the drag interaction.
 * Plugs into the main event loop next to scrollbars_handle_event. */
void drag_handle_event(XEvent ev);

/* True while ti is the icon currently following the cursor. The embedder
 * consults this to avoid overwriting the cursor-tracking position with the
 * grid-derived one during a drag. */
int drag_icon_is_floating(struct TrayIcon *ti);

/* Forget any in-flight drag involving ti. Called when ti is about to be
 * freed so we don't dereference it later. */
void drag_forget_icon(struct TrayIcon *ti);

#endif
