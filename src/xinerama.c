#include <X11/Xlib.h>

#include "debug.h"
#include "xinerama.h"

#ifdef _ST_WITH_XINERAMA
#include <X11/extensions/Xinerama.h>

#include "tray.h"
#include "settings.h"
#endif

void xinerama_init(Display *dpy)
{
#ifdef _ST_WITH_XINERAMA
    if (!XineramaIsActive(dpy)) {
        LOG_TRACE(("Xinerama is not active, returning\n"));
        return;
    }

    LOG_TRACE(("Xinerama is active\n"));

    tray_data.xinerama_active = True;
    tray_data.monitors = XineramaQueryScreens(dpy, &tray_data.n_monitors);

    LOG_TRACE(("Xinerama reports %d monitors\n", tray_data.n_monitors));
#else
    (void) dpy; /* unused */
#endif
}

void xinerama_update_geometry(void)
{
#ifdef _ST_WITH_XINERAMA
    XineramaScreenInfo chosen_monitor;
    unsigned int dummy;
    int x, y;

    if (!tray_data.xinerama_active)
        return;

    LOG_TRACE(("Updating geometry based on chosen Xinerama monitor\n"));

    XParseGeometry(settings.geometry_str, &x, &y, &dummy, &dummy);
    chosen_monitor = tray_data.monitors[settings.monitor];

    LOG_TRACE(("Chosen monitor %d: %dx%d+%d+%d\n", settings.monitor,
        chosen_monitor.width, chosen_monitor.height, chosen_monitor.x_org,
        chosen_monitor.y_org));

    if (x < 0)
        tray_data.xsh.x = chosen_monitor.x_org + chosen_monitor.width + x - tray_data.xsh.width;
    else
        tray_data.xsh.x = chosen_monitor.x_org + x;

    if (y < 0)
        tray_data.xsh.y = chosen_monitor.y_org + chosen_monitor.height + y - tray_data.xsh.height;
    else
        tray_data.xsh.y = chosen_monitor.y_org + y;

    LOG_TRACE(("New tray position (x,y): %d,%d\n", tray_data.xsh.x, tray_data.xsh.y));
#else
    return;
#endif
}
