#include <X11/Xlib.h>

#include "debug.h"
#include "xinerama.h"

#if defined(_ST_WITH_XINERAMA) || defined(_ST_WITH_XRANDR)
#include "tray.h"
#endif

#ifdef _ST_WITH_XINERAMA
#include <X11/extensions/Xinerama.h>

#include "settings.h"
#endif

#ifdef _ST_WITH_XRANDR
#include <X11/extensions/Xrandr.h>
#endif

#ifdef _ST_WITH_XINERAMA
static void xinerama_query_monitors(Display *dpy)
{
    if (tray_data.monitors != NULL) {
        XFree(tray_data.monitors);
        tray_data.monitors = NULL;
    }
    tray_data.n_monitors = 0;

    if (!XineramaIsActive(dpy)) {
        tray_data.xinerama_active = False;
        LOG_TRACE(("Xinerama is not active\n"));
        return;
    }

    tray_data.xinerama_active = True;
    tray_data.monitors = XineramaQueryScreens(dpy, &tray_data.n_monitors);

    LOG_TRACE(("Xinerama reports %d monitors\n", tray_data.n_monitors));
}
#endif

void xinerama_init(Display *dpy)
{
#if !defined(_ST_WITH_XINERAMA) && !defined(_ST_WITH_XRANDR)
    (void) dpy; /* unused */
#endif
#ifdef _ST_WITH_XINERAMA
    xinerama_query_monitors(dpy);
#endif
#ifdef _ST_WITH_XRANDR
    {
        int error_base;
        if (XRRQueryExtension(dpy, &tray_data.randr_event_base, &error_base)) {
            XRRSelectInput(
                dpy, DefaultRootWindow(dpy), RRScreenChangeNotifyMask);
            LOG_TRACE(("RandR active, event base %d\n",
                tray_data.randr_event_base));
        } else {
            tray_data.randr_event_base = -1;
            LOG_TRACE(("RandR is not available\n"));
        }
    }
#endif
}

void xinerama_update_geometry(void)
{
#ifdef _ST_WITH_XINERAMA
    XineramaScreenInfo chosen_monitor;
    unsigned int dummy;
    int x = 0, y = 0, flags, monitor;

    if (!tray_data.xinerama_active)
        return;

    LOG_TRACE(("Updating geometry based on chosen Xinerama monitor\n"));

    flags = XParseGeometry(settings.geometry_str, &x, &y, &dummy, &dummy);

    /* The configured monitor may no longer exist (e.g. it was unplugged).
     * Clamp to the available range without mutating settings.monitor, so the
     * tray returns to the requested monitor once it comes back. */
    monitor = settings.monitor;
    if (monitor >= tray_data.n_monitors)
        monitor = tray_data.n_monitors - 1;
    if (monitor < 0)
        monitor = 0;
    chosen_monitor = tray_data.monitors[monitor];

    LOG_TRACE(("Chosen monitor %d: %dx%d+%d+%d\n", monitor,
        chosen_monitor.width, chosen_monitor.height, chosen_monitor.x_org,
        chosen_monitor.y_org));

    if (flags & XValue && flags & XNegative)
        x += chosen_monitor.width - tray_data.xsh.width;
    if (flags & YValue && flags & YNegative)
        y += chosen_monitor.height - tray_data.xsh.height;

    tray_data.xsh.x = chosen_monitor.x_org + x;
    tray_data.xsh.y = chosen_monitor.y_org + y;

    LOG_TRACE(("New tray position (x,y): %d,%d\n", tray_data.xsh.x, tray_data.xsh.y));
#else
    return;
#endif
}

void xinerama_handle_event(XEvent ev)
{
#if defined(_ST_WITH_XINERAMA) && defined(_ST_WITH_XRANDR)
    XWindowAttributes root_wa;

    if (tray_data.randr_event_base < 0
        || ev.type != tray_data.randr_event_base + RRScreenChangeNotify)
        return;

    LOG_TRACE(("RRScreenChangeNotify received; reconfiguring monitors\n"));

    XRRUpdateConfiguration(&ev);
    xinerama_query_monitors(tray_data.dpy);

    if (!tray_data.xinerama_active)
        return;

    /* Refresh the cached root window size; the screen bounding box changes
     * when monitors are added or removed, and the strut autodetection relies
     * on it. */
    if (XGetWindowAttributes(
            tray_data.dpy, DefaultRootWindow(tray_data.dpy), &root_wa)) {
        tray_data.root_wnd.width = root_wa.width;
        tray_data.root_wnd.height = root_wa.height;
    }

    /* Recompute the position as on startup and move the tray. The resulting
     * ConfigureNotify on the tray window drives icon repositioning through the
     * normal path; update the strut here too in case the move is a no-op. */
    xinerama_update_geometry();
    XMoveWindow(
        tray_data.dpy, tray_data.tray, tray_data.xsh.x, tray_data.xsh.y);
    tray_update_window_strut();
#else
    (void) ev; /* unused */
#endif
}
