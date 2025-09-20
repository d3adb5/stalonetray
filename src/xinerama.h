#ifndef _STALONETRAY_XINERAMA_H_
#define _STALONETRAY_XINERAMA_H_

#include <X11/Xlib.h>

void xinerama_init(Display *dpy);
void xinerama_update_geometry(void);

#endif
