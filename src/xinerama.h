#ifndef _STALONETRAY_XINERAMA_H_
#define _STALONETRAY_XINERAMA_H_

#include <X11/Xlib.h>

void xinerama_init(Display *dpy);
void xinerama_update_geometry(void);
/* React to a RandR screen change event by re-querying monitors and
 * repositioning the tray as if it had just started up. No-op for events that
 * are not RandR screen changes. */
void xinerama_handle_event(XEvent ev);

#endif
