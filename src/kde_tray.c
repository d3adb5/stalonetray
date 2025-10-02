/* -------------------------------
 * vim:tabstop=4:shiftwidth=4
 * kde_tray.c
 * Sun, 19 Sep 2004 12:31:10 +0700
 * -------------------------------
 * kde tray related routines
 * -------------------------------*/

#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include "debug.h"
#include "kde_tray.h"
#include "xutils.h"

/* This list holds "old" KDE icons, e.g. the icons that are (likely) to be
 * already embedded into some system tray and, therefore, are to be ignored.
 * The list is empty initially */
Window *old_kde_icons = NULL;
unsigned long n_old_kde_icons = -1;

int kde_tray_update_fallback_mode(Display *dpy)
{
    /* Get the contents of KDE_NET_SYSTEM_TRAY_WINDOWS root window property.
     * All windows that are listed there are considered to be "old" KDE icons,
     * i.e. icons that are to be ignored on the tray startup.
     * If the property does not exist, fall back to old mode */
    if (tray_data.xa_kde_net_system_tray_windows == None
        || !x11_get_root_winlist_prop(dpy,
            tray_data.xa_kde_net_system_tray_windows,
            (unsigned char **)&old_kde_icons, &n_old_kde_icons)) {
        LOG_INFO(("WM does not support KDE_NET_SYSTEM_TRAY_WINDOWS, will use "
                  "legacy scheme\n"));
        x11_extend_root_event_mask(tray_data.dpy, SubstructureNotifyMask);
        tray_data.kde_tray_old_mode = 1;
    } else {
        tray_data.kde_tray_old_mode = 0;
    }
    return tray_data.kde_tray_old_mode;
}

void kde_tray_init(Display *dpy)
{
    static int initialized = False;

    unsigned long n_client_windows, i;
    Window *client_windows;
    Atom xa_net_client_list;

    if (!kde_tray_update_fallback_mode(dpy)) return;

    /* do nothing if this function was already called */
    if (initialized) return;

    /* 1. If theres no previous tray selection owner, try to embed all
     * available KDE icons and, therefore, leave the list of old KDE icons
     * empty */
    if (tray_data.old_selection_owner == None) {
        n_old_kde_icons = 0;
        initialized = True;
        return;
    }
    /* 2.Next, we are going to remove some entries from old_kde_icons list */
    /* 2.a. First, we remove all icons that are listed in _NET_CLIENT_LIST
     * property, since this means that they are not embedded in any kind of
     * tray */
    xa_net_client_list = XInternAtom(dpy, "_NET_CLIENT_LIST", True);
    if (x11_get_root_winlist_prop(dpy, xa_net_client_list,
            (unsigned char **)&client_windows, &n_client_windows)) {
        for (i = 0; i < n_client_windows; i++)
            kde_tray_old_icons_remove(client_windows[i]);
    }
    /* 2.b. Second, we remove all windows that have root window as their
     * parent,
     * since this also means that they are not embedded in any kind of tray */
    for (i = 0; i < n_old_kde_icons; i++) {
        Window root, parent, *children;
        unsigned int nchildren;
        int rc;
        nchildren = 0;
        children = NULL;
        if ((rc = XQueryTree(dpy, old_kde_icons[i], &root, &parent, &children,
                 &nchildren))) {
            if (root == parent) old_kde_icons[i] = None;
            if (nchildren > 0) XFree(children);
        }
        if (!x11_ok() || !rc) old_kde_icons[i] = None;
    }
#ifdef DEBUG
    /* Some diagnostic output */
    for (i = 0; i < n_old_kde_icons; i++)
        if (old_kde_icons[i] != None)
            LOG_TRACE(
                ("0x%lx is marked as an old KDE icon\n", old_kde_icons[i]));
#endif

    initialized = True;
}

int kde_tray_update_old_icons(Display *dpy)
{
    unsigned int i, rc;
    XWindowAttributes xwa;
    /* Remove dead entries from old kde icons list.
     * We use XGetWindowAttributes to see if the
     * window is still alive */
    for (i = 0; i < n_old_kde_icons; i++) {
        rc = XGetWindowAttributes(dpy, old_kde_icons[i], &xwa);
        if (!x11_ok() || !rc) old_kde_icons[i] = None;
    }
    return SUCCESS;
}

int kde_tray_is_old_icon(Window w)
{
    unsigned int i;
    for (i = 0; i < n_old_kde_icons; i++)
        if (old_kde_icons[i] == w) return True;
    return False;
}

void kde_tray_old_icons_remove(Window w)
{
    unsigned int i;
    for (i = 0; i < n_old_kde_icons; i++)
        if (old_kde_icons[i] == w) {
            LOG_TRACE(("0x%lx unmarked as an old kde icon\n", w));
            old_kde_icons[i] = None;
        }
}

int kde_tray_check_for_icon(Display *dpy, Window w)
{
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    static Atom xa_kde_net_wm_system_tray_window_for = None;
    unsigned char *data = NULL;
    /* Check if the window has a property named _KDE_NET_WM_SYSTEM_TRAY_WINDOW
     * FOR */
    if (xa_kde_net_wm_system_tray_window_for == None)
        xa_kde_net_wm_system_tray_window_for =
            XInternAtom(dpy, "_KDE_NET_WM_SYSTEM_TRAY_WINDOW_FOR", True);
    /* If this atom does not exist, we have nothing to check for */
    if (xa_kde_net_wm_system_tray_window_for == None) return False;
    /* TODO: use x11_ call */
    XGetWindowProperty(dpy, w, xa_kde_net_wm_system_tray_window_for, 0L, 1L,
        False, XA_WINDOW, &actual_type, &actual_format, &nitems, &bytes_after,
        &data);
    XFree(data);
    if (x11_ok() && actual_type == XA_WINDOW && nitems == 1)
        return SUCCESS;
    else
        return FAILURE;
}

Window kde_tray_find_icon(Display *dpy, Window w)
{
    Window root, parent, *children = NULL;
    unsigned int nchildren, i;
    Window r = None;
    if (kde_tray_check_for_icon(dpy, w)) return w;
    XQueryTree(dpy, w, &root, &parent, &children, &nchildren);
    if (!x11_ok()) goto bailout;
    for (i = 0; i < nchildren; i++)
        if ((r = kde_tray_find_icon(dpy, children[i])) != None) goto bailout;
bailout:
    if (children != NULL && nchildren > 0) XFree(children);
    return r;
}
