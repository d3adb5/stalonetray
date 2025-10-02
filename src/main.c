/* -------------------------------
 * vim:tabstop=4:shiftwidth=4
 * main.c
 * Tue, 24 Aug 2004 12:19:48 +0700
 * -------------------------------
 * main is main
 * -------------------------------*/

#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xmd.h>
#include <X11/Xutil.h>

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "common.h"
#include "debug.h"
#include "embed.h"
#include "icons.h"
#include "layout.h"
#include "wmh.h"
#include "xembed.h"
#include "xutils.h"

#ifdef _ST_WITH_NATIVE_KDE
#include "kde_tray.h"
#endif

#include "scrollbars.h"
#include "settings.h"
#include "tray.h"
#include "xinerama.h"

struct TrayData tray_data;
static int tray_status_requested = 0;
#ifdef _ST_EXIT_GRACEFULLY
static Display *async_dpy;
#endif

void my_usleep(useconds_t usec)
{
    struct timeval timeout;
    fd_set rfds;
    FD_ZERO(&rfds);
    timeout.tv_sec = 0;
    timeout.tv_usec = usec;
    select(1, &rfds, NULL, NULL, &timeout);
}

/****************************
 * Signal handlers, cleanup
 ***************************/
void request_tray_status_on_signal(int)
{
    tray_status_requested = 1;
}

#ifdef _ST_EXIT_GRACEFULLY
void exit_on_signal(int sig)
{
    if (sig == SIGPIPE) {
        debug_disable_output();
    } else {
        psignal(sig, "");
        /* This is UGLY and is, probably, to be submitted to
         * Daily WTF, but it is the only way I found not to
         * use usleep in main event loop. */
        LOG_TRACE(("Sending fake WM_DELETE_WINDOW message\n"));
    }
    x11_send_client_msg32(async_dpy, tray_data.tray, tray_data.tray,
        tray_data.xa_wm_protocols, tray_data.xa_wm_delete_window, 0, 0, 0, 0);
    XSync(async_dpy, False);
}
#endif

void cleanup()
{
    static int clean = 0;
    static int cleanup_in_progress = 0;
    if (!clean && cleanup_in_progress) {
        LOG_ERROR(("forced to die\n"));
        abort();
    }
    if (clean) return;
    cleanup_in_progress = 1;
    if (tray_data.dpy != NULL && x11_connection_status()) {
        LOG_TRACE(("being nice to the icons\n"));
        /* Clean the list unembedding icons one by one */
        icon_list_clean_callback(&embedder_unembed);
        /* Give away the selection */
        if (tray_data.is_active)
            XSetSelectionOwner(
                tray_data.dpy, tray_data.xa_tray_selection, None, CurrentTime);
        /* Sync in order to wait until all icons finish their reparenting
         * process */
        XSync(tray_data.dpy, False);
        XCloseDisplay(tray_data.dpy);
        tray_data.dpy = NULL;
    }
    cleanup_in_progress = 0;
    clean = 1;
}

/**************************************
 * Helper functions
 **************************************/

/* Print tray status */
void dump_tray_status()
{
    int grid_w, grid_h;
    tray_status_requested = 0;
    layout_get_size(&grid_w, &grid_h);
    LOG_INFO(("----------- tray status -----------\n"));
    LOG_INFO(("active: %s\n", tray_data.is_active ? "yes" : "no"));
    LOG_INFO(("geometry: %dx%d+%d+%d\n", tray_data.xsh.width,
        tray_data.xsh.height, tray_data.xsh.x, tray_data.xsh.y));
    if (tray_data.xembed_data.current)
        LOG_INFO(("XEMBED focus: 0x%lx\n", tray_data.xembed_data.current->wid));
    else
        LOG_INFO(("XEMBED focus: none\n"));
    LOG_INFO(("currently managed icons:\n"));
    icon_list_forall(&print_icon_data);
    LOG_INFO(("-----------------------------------\n"));
}

/* Checks whether a given window class should be ignored */
int is_ignored_class(const char *name) {
    struct WindowClass *haystack = NULL;

    for (haystack = settings.ignored_classes; haystack; haystack = haystack->next) {
        if (!strcmp(name, haystack->name))
            return 1;
    }

    return 0;
}

/**************************************
 * (Un)embedding cycle implementation
 **************************************/

/* Add icon to the tray */
void add_icon(Window w, int cmode)
{
    struct TrayIcon *ti;
    const char *classname = NULL;
    /* Aviod adding duplicates */
    if ((ti = icon_list_find(w)) != NULL) {
        LOG_TRACE(("ignoring second request to embed 0x%lx"
                   " (requested cmode=%d, current cmode=%d)\n",
            w, cmode, ti->cmode));
        return;
    }
    /* Dear Edsger W. Dijkstra, I see you behind my back =( */
    if ((ti = icon_list_new(w, cmode)) == NULL) goto icon_allocation_failed;
    LOG_TRACE(("starting embedding for icon 0x%lx, cmode=%d\n", w, cmode));
    x11_dump_win_info(tray_data.dpy, w);

    classname = x11_get_window_class(tray_data.dpy, w);
    if (classname == NULL) {
        LOG_TRACE(("Ignoring icon, x11_get_window_class() failed: 0x%lx"
                   " (requested cmode=%d)\n",
            w, cmode));
        goto embedding_failed;
    }

    if (is_ignored_class(classname)) {
        LOG_INFO(("Ignoring icon because its class is ignored: %s\n", classname));
        goto done;
    }

    /* Start embedding cycle */
    if (!xembed_check_support(ti)) goto embedding_failed;
    if (ti->is_xembed_supported)
        ti->is_visible = xembed_get_mapped_state(ti);
    else
        ti->is_visible = True;
    if (ti->is_visible) {
        if (!embedder_reset_size(ti)) goto embedding_failed;
        if (!layout_add(ti)) goto embedding_failed;
    }
    if (!xembed_embed(ti)) goto embedding_failed_after_layout;
    if (!embedder_embed(ti)) goto embedding_failed_after_layout;
    embedder_update_positions(False);
    tray_update_window_props();
    /* Report success */
    LOG_INFO(("added icon %s (wid 0x%lx) as %s\n",
        x11_get_window_name(tray_data.dpy, ti->wid, "<unknown>"), ti->wid,
        ti->is_visible ? "visible" : "hidden"));
    goto done;

embedding_failed_after_layout:
    layout_remove(ti);

embedding_failed:
    icon_list_free(ti);

icon_allocation_failed:
    LOG_INFO(("failed to add icon %s (wid 0x%lx)\n",
        x11_get_window_name(tray_data.dpy, ti->wid, "<unknown>"), ti->wid));

done:
    if (classname != NULL)
        free((void *) classname);

    if (settings.log_level >= LOG_LEVEL_TRACE) dump_tray_status();
    return;
}

/* Remove icon from the tray */
void remove_icon(Window w)
{
    struct TrayIcon *ti;
    /* Ignore false alarms */
    if ((ti = icon_list_find(w)) == NULL) return;
    dump_tray_status();
    embedder_unembed(ti);
    xembed_unembed(ti);
    layout_remove(ti);
    icon_list_free(ti);
    LOG_INFO(("removed icon %s (wid 0x%lx)\n",
        x11_get_window_name(tray_data.dpy, ti->wid, "<unknown>"), w));
    /* no need to call embedde_update_positions(), as
     * scrollbars_click(SB_WND_MAX) will call it */
    /* XXX: maybe we need a different name for this
     * routine instad of passing cryptinc constant? */
    scrollbars_click(SB_WND_MAX);
    tray_update_window_props();
    dump_tray_status();
}

/* Track icon visibility state changes */
void icon_track_visibility_changes(Window w)
{
    struct TrayIcon *ti;
    int mapped;
    /* Ignore false alarms */
    if ((ti = icon_list_find(w)) == NULL || !ti->is_xembed_supported) return;
    mapped = xembed_get_mapped_state(ti);
    LOG_TRACE(("xembed_is_mapped(0x%lx) = %u\n", w, mapped));
    LOG_TRACE(("is_visible = %u\n", ti->is_visible));
#ifdef DEBUG
    x11_dump_win_info(tray_data.dpy, ti->wid);
#endif
    /* Nothing has changed */
    if (mapped == ti->is_visible) return;
    ti->is_visible = mapped;
    LOG_INFO(("%s icon 0x%lx\n", mapped ? "showing" : "hiding", w));
    if (mapped) { /* Icon has become mapped and is listed as hidden. Show this
                     icon. */
        embedder_reset_size(ti);
        if (!layout_add(ti)) {
            xembed_set_mapped_state(ti, False);
            return;
        }
        embedder_show(ti);
    } else { /* Icon has become unmapped and is listed as visible. Hide this
                icon. */
        layout_remove(ti);
        embedder_hide(ti);
    }
    embedder_update_positions(False);
    tray_update_window_props();
}

/* helper to identify invalid icons */
int find_invalid_icons(struct TrayIcon *ti)
{
    return ti->is_invalid;
}

#ifdef _ST_WITH_NATIVE_KDE
/* Find newly available KDE icons and add them as necessary */
/* TODO: move to kde_tray.c */
void kde_icons_update()
{
    unsigned long list_len, i;
    Window *kde_tray_icons;
    if (tray_data.kde_tray_old_mode
        || !x11_get_root_winlist_prop(tray_data.dpy,
            tray_data.xa_kde_net_system_tray_windows,
            (unsigned char **)&kde_tray_icons, &list_len)) {
        return;
    }
    for (i = 0; i < list_len; i++)
        /* If the icon is not None and is non old, try to add it
         * (if the icon is already there, nothing is gonna happen). */
        if (kde_tray_icons[i] != None
            && !kde_tray_is_old_icon(kde_tray_icons[i])) {
            LOG_TRACE(("found (possibly unembedded) KDE icon %s (wid 0x%lx)\n",
                x11_get_window_name(
                    tray_data.dpy, kde_tray_icons[i], "<unknown>"),
                kde_tray_icons[i]));
            add_icon(kde_tray_icons[i], CM_KDE);
        }
    XFree(kde_tray_icons);
}
#endif

void find_unmanaged_chromium_icons()
{
    unsigned int n;
    Window *topwins, dummy;
    XQueryTree(tray_data.dpy, DefaultRootWindow(tray_data.dpy), &dummy, &dummy,
        &topwins, &n);
    if (topwins == NULL) return;

    // Find toplevel windows (unmanaged) that have:
    //_NET_WM_WINDOW_TYPE(ATOM) == _NET_WM_WINDOW_TYPE_NOTIFICATION
    // CHROMIUM_COMPOSITE_WINDOW(CARDINAL) == 1
    Atom win_type = XInternAtom(tray_data.dpy, "_NET_WM_WINDOW_TYPE", False);
    Atom win_type_notif =
        XInternAtom(tray_data.dpy, "_NET_WM_WINDOW_TYPE_NOTIFICATION", False);
    Atom chrom_composite =
        XInternAtom(tray_data.dpy, "CHROMIUM_COMPOSITE_WINDOW", False);
    for (unsigned int i = 0; i < n; i++) {
        Atom *aitem;
        unsigned int *citem;
        unsigned long nitems;
        int rc = False;
        Bool ok = True;
        LOG_TRACE(("Chromium hack - checking window 0x%lx\n", topwins[i]));

        rc = x11_get_window_prop32(tray_data.dpy, topwins[i], win_type,
            XA_ATOM, (unsigned char **)&aitem, &nitems);
        LOG_TRACE(("Chromium hack, ret: %x %x=%x %lx\n", x11_ok(), rc, Success,
            nitems));
        if (!(x11_ok() && rc == SUCCESS && nitems == 1)) continue;
        LOG_TRACE(("Chromium hack, comp: %lx=%lx\n", aitem[0], win_type_notif));
        ok = aitem[0] == win_type_notif;
        XFree(aitem);
        if (!ok) continue;
        LOG_TRACE(("Found toplevel notification window 0x%lx\n", topwins[i]));

        rc = x11_get_window_prop32(tray_data.dpy, topwins[i], chrom_composite,
            XA_CARDINAL, (unsigned char **)&citem, &nitems);
        LOG_TRACE(("Chromium hack, ret: %x %x=%x %lx\n", x11_ok(), rc, Success,
            nitems));
        if (!(x11_ok() && rc == SUCCESS && nitems == 1)) continue;
        LOG_TRACE(("Chromium hack, comp: %lx=%lx\n", aitem[0], win_type_notif));
        ok = citem[0] == 1;
        XFree(citem);
        if (!ok) continue;
        LOG_TRACE(("Found chromium composite window 0x%ld\n", topwins[i]));

        add_icon(topwins[i], CM_FDO);
    }

    XFree(topwins);
}

#define PT_MASK_SB (1L << 0)
#define PT_MASK_ALL PT_MASK_SB

/* Perform several periodic tasks */
void perform_periodic_tasks(int mask)
{
    struct TrayIcon *ti;
    /* 1. Remove all invalid icons */
    while ((ti = icon_list_forall(&find_invalid_icons)) != NULL) {
        LOG_TRACE(("icon 0x%lx is invalid; removing\n", ti->wid));
        remove_icon(ti->wid);
    }
    /* 2. Print tray status if asked to */
    if (tray_status_requested) dump_tray_status();
    /* 3. KLUDGE to fix window size on (buggy?) WMs */
    if (settings.kludge_flags & KLUDGE_FIX_WND_SIZE) {
        /* KLUDGE TODO: resolve */
        XWindowAttributes xwa;
        XGetWindowAttributes(tray_data.dpy, tray_data.tray, &xwa);
        if (!tray_data.is_reparented
            && (xwa.width != tray_data.xsh.width
                || xwa.height != tray_data.xsh.height)) {
            LOG_TRACE(("KLUDGE: fixing tray window size (current: %dx%d, "
                       "required: %dx%d)\n",
                xwa.width, xwa.height, tray_data.xsh.width,
                tray_data.xsh.height));
            tray_update_window_props();
        }
    }
    /* 4. run scrollbars periodic tasks */
    if (mask & PT_MASK_SB) scrollbars_periodic_tasks();
}

/**********************
 * Event handlers
 **********************/

void expose(XExposeEvent ev)
{
    if (ev.window == tray_data.tray && settings.parent_bg && ev.count == 0)
        tray_refresh_window(False);
}

void visibility_notify(XVisibilityEvent) {}

void property_notify(XPropertyEvent ev)
{
#define TRACE_PROPS
#if defined(DEBUG) && defined(TRACE_PROPS)
    char *atom_name;
    atom_name = XGetAtomName(tray_data.dpy, ev.atom);
    LOG_TRACE(("atom = %s\n", atom_name));
    XFree(atom_name);
#endif
    /* React on wallpaper change */
    if (ev.atom == tray_data.xa_xrootpmap_id
        || ev.atom == tray_data.xa_xsetroot_id) {
        if (settings.transparent) tray_update_bg(True);
        if (settings.parent_bg || settings.transparent || settings.fuzzy_edges)
            tray_refresh_window(True);
    }
#ifdef _ST_WITH_NATIVE_KDE
    /* React on change of list of KDE icons */
    if (ev.atom == tray_data.xa_kde_net_system_tray_windows) {
        if (tray_data.is_active)
            kde_icons_update();
        else
            LOG_TRACE(("not updating KDE icons list: tray is not active\n"));
        kde_tray_update_old_icons(tray_data.dpy);
    }
#endif
    /* React on WM (re)starts */
    if (ev.atom
        == XInternAtom(tray_data.dpy, _NET_SUPPORTING_WM_CHECK, False)) {
#ifdef DEBUG
        ewmh_list_supported_atoms(tray_data.dpy);
#endif
        tray_set_wm_hints();
#ifdef _ST_WITH_NATIVE_KDE
        kde_tray_update_fallback_mode(tray_data.dpy);
#endif
    }
    /* React on _XEMBED_INFO changes of embedded icons
     * (currently used to track icon visibility status) */
    if (ev.atom == tray_data.xembed_data.xa_xembed_info) {
        icon_track_visibility_changes(ev.window);
    }
    if (ev.atom == tray_data.xa_net_client_list) {
        Window *windows;
        unsigned long nwindows, rc, i;
        rc = x11_get_root_winlist_prop(tray_data.dpy,
            tray_data.xa_net_client_list, (unsigned char **)&windows,
            &nwindows);
        if (x11_ok() && rc) {
            tray_data.is_reparented = True;
            for (i = 0; i < nwindows; i++)
                if (windows[i] == tray_data.tray) {
                    tray_data.is_reparented = False;
                    break;
                }
        }
        if (nwindows) XFree(windows);
        LOG_TRACE((
            "tray was %sreparented\n", tray_data.is_reparented ? "" : "not "));
    }
}

void reparent_notify(XReparentEvent ev)
{
    struct TrayIcon *ti;
    ti = icon_list_find(ev.window);
    if (ti == NULL) return;
    /* Reparenting out of the tray is one of non-destructive
     * ways to end XEMBED protocol (see spec) */
    if (ti->is_embedded && ti->mid_parent != ev.parent) {
        LOG_TRACE(("will now unembed 0x%lx\n", ti->wid));
#ifdef DEBUG
        print_icon_data(ti);
        x11_dump_win_info(tray_data.dpy, ev.parent);
#endif
        remove_icon(ev.window);
    }
}

void client_message(XClientMessageEvent ev)
{
    int cmode = CM_FDO;
    struct TrayIcon *ti;
#ifdef DEBUG
    /* Print neat message(s) about this event to aid debugging */
    char *msg_type_name;
    msg_type_name = XGetAtomName(tray_data.dpy, ev.message_type);
    if (msg_type_name != NULL) {
        LOG_TRACE(("message \"%s\"\n", msg_type_name));
        XFree(msg_type_name);
    }
    if (ev.message_type == tray_data.xa_wm_protocols) {
        msg_type_name = XGetAtomName(tray_data.dpy, ev.data.l[0]);
        if (msg_type_name != NULL) {
            LOG_TRACE(("WM_PROTOCOLS message type: %s\n", msg_type_name));
            XFree(msg_type_name);
        }
    }
#endif
    /* Graceful exit */
    if (ev.message_type == tray_data.xa_wm_protocols
        && (unsigned long) ev.data.l[0] == tray_data.xa_wm_delete_window
        && ev.window == tray_data.tray) {
        LOG_TRACE(("got WM_DELETE message, will now exit\n"));
        exit(0); // atexit will call cleanup()
    }
    /* Handle _NET_WM_PING */
    if (ev.message_type == tray_data.xa_wm_protocols
        && (unsigned long) ev.data.l[0] == tray_data.xa_net_wm_ping
        && ev.window == tray_data.tray) {
        LOG_TRACE(("got WM_PING message, sending it back\n"));
        XEvent reply;
        reply.xclient = ev;
        reply.xclient.window = DefaultRootWindow(tray_data.dpy);
        XSendEvent(tray_data.dpy, DefaultRootWindow(tray_data.dpy), False,
            (SubstructureNotifyMask | SubstructureRedirectMask), &reply);
    }
    /* Handle _NET_SYSTEM_TRAY_* messages */
    if (ev.message_type == tray_data.xa_tray_opcode && tray_data.is_active) {
        LOG_TRACE(("this is the _NET_SYSTEM_TRAY_OPCODE(%lu) message\n",
            ev.data.l[1]));
        switch (ev.data.l[1]) {
        /* This is the starting point of NET SYSTEM TRAY protocol */
        case SYSTEM_TRAY_REQUEST_DOCK:
            LOG_TRACE(
                ("dockin' requested by window 0x%lx, serving in a moment\n",
                    ev.data.l[2]));
#ifdef _ST_WITH_NATIVE_KDE
            if (kde_tray_check_for_icon(tray_data.dpy, ev.data.l[2]))
                cmode = CM_KDE;
            if (kde_tray_is_old_icon(ev.data.l[2]))
                kde_tray_old_icons_remove(ev.data.l[2]);
#endif
            add_icon(ev.data.l[2], cmode);
            break;
        /* We ignore these messages, since we do not show
         * any baloons anyways */
        case SYSTEM_TRAY_BEGIN_MESSAGE:
        case SYSTEM_TRAY_CANCEL_MESSAGE: break;
        /* Below are special cases added by this implementation */
        /* STALONETRAY_TRAY_DOCK_CONFIRMED is sent by stalonetray
         * to itself. (see embed.c) */
        case STALONE_TRAY_DOCK_CONFIRMED:
            ti = icon_list_find(ev.data.l[2]);
            if (ti != NULL && !ti->is_embedded) {
                ti->is_embedded = True;
                LOG_TRACE(("embedding confirmed for icon 0x%lx\n", ti->wid));
#ifdef DEBUG
                dump_tray_status();
#endif
            }
            tray_update_window_props();
            break;
        /* Dump tray status on request */
        case STALONE_TRAY_STATUS_REQUESTED: dump_tray_status(); break;
        /* Find icon and scroll to it if necessary */
        case STALONE_TRAY_REMOTE_CONTROL:
            ti = icon_list_find(ev.window);
            if (ti == NULL) break;
            scrollbars_scroll_to(ti);
#if 0
				/* Quick hack */
				{
					Window icon = ev.window;
					int rc;
					int x = ev.data.l[3], y = ev.data.l[4], depth = 0, idummy, i;
					int btn = ev.data.l[2];
					Window win, root;
					unsigned int udummy, w, h;
					XGetGeometry(tray_data.dpy, icon, &root,
							&idummy, &idummy,
							&w, &h, &udummy, &udummy);
					LOG_TRACE(("wid=0x%x w=%d h=%d\n", icon, w, h));
					x = (x == REMOTE_CLICK_POS_DEFAULT) ? w / 2 : x;
					y = (y == REMOTE_CLICK_POS_DEFAULT) ? h / 2 : y;
					/* 3.2. Find subwindow to execute click on */
					win = x11_find_subwindow_at(tray_data.dpy, icon, &x, &y, depth);
					/* 3.3. Send mouse click(s) to target */
					LOG_TRACE(("wid=0x%x btn=%d x=%d y=%d\n",
								win, btn, x, y));
#define SEND_BTN_EVENT(press, time) \
    do { \
        x11_send_button(tray_data.dpy, /* dispslay */ \
            press, /* event type */ \
            win, /* target window */ \
            root, /* root window */ \
            time, /* time */ \
            btn, /* button */ \
            Button1Mask << (btn - 1), /* state mask */ \
            x, /* x coord (relative) */ \
            y); /* y coord (relative) */ \
    } while (0)
					for (i = 0; i < ev.data.l[0]; i++) {
						SEND_BTN_EVENT(1, x11_get_server_timestamp(tray_data.dpy, tray_data.tray));
						my_usleep(250);
						SEND_BTN_EVENT(0, x11_get_server_timestamp(tray_data.dpy, tray_data.tray));
					}
#undef SEND_BTN_EVENT
				}
#endif
            break;
        default: break;
        }
    }
#ifdef DEBUG
    if (ev.message_type == tray_data.xa_tray_opcode && !tray_data.is_active)
        LOG_TRACE(("ignoring _NET_SYSTEM_TRAY_OPCODE(%d) message because "
                   "tray is not active\n",
            tray_data.is_active));
#endif
}

void destroy_notify(XDestroyWindowEvent ev)
{
    if (!tray_data.is_active && ev.window == tray_data.old_selection_owner) {
        /* Old tray selection owner was destroyed. Take over selection
         * ownership. */
        tray_acquire_selection();
    } else if (ev.window != tray_data.tray) {
        /* Try to remove icon from the tray */
        remove_icon(ev.window);
#ifdef _ST_WITH_NATIVE_KDE
    } else if (kde_tray_is_old_icon(ev.window)) {
        /* Since X Server may reuse window ids, remove ev.window
         * from the list of old KDE icons */
        kde_tray_old_icons_remove(ev.window);
#endif
    }
}

void configure_notify(XConfigureEvent ev)
{
    struct TrayIcon *ti;
    struct Point sz;
    XWindowAttributes xwa;
    if (ev.window == tray_data.tray) {
        /* Tray window was resized */
        /* TODO: distinguish between synthetic and real configure notifies */
        /* TODO: catch rejected configure requests */
        /* XXX: Geometry stuff is a mess. Geometry
         * is specified in slots, but almost always is
         * stored in pixels... */
        LOG_TRACE(("tray window geometry from event: %ux%u+%d+%d\n", ev.width,
            ev.height, ev.x, ev.y));
        /* Sometimes, configure notifies come too late, so we fetch real
         * geometry ourselves */
        XGetWindowAttributes(tray_data.dpy, tray_data.tray, &xwa);
        x11_get_window_abs_coords(
            tray_data.dpy, tray_data.tray, &tray_data.xsh.x, &tray_data.xsh.y);
        LOG_TRACE(("tray window geometry from X11 calls: %dx%d+%d+%d\n",
            xwa.width, xwa.height, tray_data.xsh.x, tray_data.xsh.y));
        tray_data.xsh.width = xwa.width;
        tray_data.xsh.height = xwa.height;
        /* Update icons positions */
        /* XXX: internal API is bad. example below */
        icon_list_forall(&layout_translate_to_window);
        embedder_update_positions(True);
        /* Adjust window background if necessary */
        tray_update_bg(False);
        tray_refresh_window(True);
        tray_update_window_strut();
        scrollbars_update();
    } else if ((ti = icon_list_find(ev.window))
        != NULL) { /* Some icon has resized its window */
        /* KDE icons are not allowed to change their size. Reset icon size. */
        if (ti->cmode == CM_KDE
            || settings.kludge_flags & KLUDGE_FORCE_ICONS_SIZE) {
            embedder_reset_size(ti);
            return;
        }
        if (settings.kludge_flags & KLUDGE_FORCE_ICONS_SIZE) return;
        /* Get new window size */
        if (!x11_get_window_size(tray_data.dpy, ti->wid, &sz.x, &sz.y)) {
            embedder_unembed(ti);
            return;
        }
        LOG_TRACE(("icon 0x%lx was resized, new size: %ux%u, old size: %ux%u\n",
            ev.window, sz.x, sz.y, ti->l.wnd_sz.x, ti->l.wnd_sz.y));
        /* Check if the size has really changed */
        if (sz.x == ti->l.wnd_sz.x && sz.y == ti->l.wnd_sz.y) return;
        ti->l.wnd_sz = sz;
        ti->is_resized = True;
        /* Do the job */
        layout_handle_icon_resize(ti);
        embedder_refresh(ti);
#ifdef DEBUG
        print_icon_data(ti);
#endif
        embedder_update_positions(False);
        tray_update_window_props();
#ifdef DEBUG
        dump_tray_status();
#endif
    }
}

void selection_clear(XSelectionClearEvent ev)
{
    /* Is it _NET_SYSTEM_TRAY selection? */
    if (ev.selection == tray_data.xa_tray_selection) {
        /* Is it us who has lost the selection */
        if (ev.window == tray_data.tray) {
            LOG_INFO(("another tray detected; deactivating\n"));
            tray_data.is_active = False;
            tray_data.old_selection_owner =
                XGetSelectionOwner(tray_data.dpy, tray_data.xa_tray_selection);
            if (!x11_ok()) {
                LOG_INFO(("could not find proper new tray; reactivating\n"));
                tray_acquire_selection();
            };
            LOG_TRACE(("new selection owner is 0x%lx\n",
                tray_data.old_selection_owner));
            XSelectInput(tray_data.dpy, tray_data.old_selection_owner,
                StructureNotifyMask);
            return;
        } else if (!tray_data.is_active) {
            /* Someone else has lost selection and tray is not active --- take
             * over the selection */
            LOG_INFO(("another tray exited; reactivating\n"));
            tray_acquire_selection();
        } else {
            /* Just in case */
            LOG_TRACE(("WEIRD: tray is active and someone else has lost tray "
                       "selection\n"));
        }
    }
}

void map_notify(XMapEvent ev)
{
#ifdef _ST_WITH_NATIVE_KDE
    /* Legacy scheme to handle KDE icons */
    if (tray_data.kde_tray_old_mode) {
        struct TrayIcon *ti = icon_list_find_ex(ev.window);
        if (ti == NULL) {
            Window w = kde_tray_find_icon(tray_data.dpy, ev.window);
            if (w != None) {
                LOG_TRACE(("Legacy scheme for KDE icons: detected KDE icon "
                           "0x%lx. Adding.\n",
                    w));
                add_icon(w, CM_KDE);
                /* TODO: remove some properties to trick ion3 so that it no
                 * longer thinks that w is a toplevel. Candidates for removal:
                 * 	- WM_STATE */
            }
        }
    }
#else
    (void) ev; /* unused */
#endif
}

void unmap_notify(XUnmapEvent ev)
{
    struct TrayIcon *ti;
    ti = icon_list_find(ev.window);
    if (ti != NULL && !ti->is_invalid) {
        /* KLUDGE! sometimes icons occasionally
         * unmap their windows, but do _not_ destroy
         * them. We map those windows back */
        /* XXX: not root caused */
        LOG_TRACE(("Unmap icons KLUDGE executed for 0x%lx\n", ti->wid));
        XMapRaised(tray_data.dpy, ti->wid);
        if (!x11_ok()) ti->is_invalid = True;
    }
}

/*********************************************************/
/* main() for usual operation */
int tray_main(int argc, char **argv)
{
    XEvent ev;
    /* Interpret those settings that need an open display */
    interpret_settings();
#ifdef DEBUG
    ewmh_list_supported_atoms(tray_data.dpy);
#endif
    xinerama_init(tray_data.dpy);
    xinerama_update_geometry();
    /* Create and show tray window */
    tray_create_window(argc, argv);
    tray_acquire_selection();
    tray_show_window();
#ifdef _ST_WITH_NATIVE_KDE
    kde_tray_init(tray_data.dpy);
#endif
    xembed_init();
#ifdef _ST_WITH_NATIVE_KDE
    kde_icons_update();
#endif
    find_unmanaged_chromium_icons();
    /* Main event loop */
    while ("my guitar gently wheeps") {
        /* This is ugly and extra dependency. But who cares?
         * Rationale: we want to block unless absolutely needed.
         * This way we ensure that stalonetray does not show up
         * in powertop (i.e. does not eat unnecessary power and
         * CPU cycles)
         * Drawback: handling of signals is very limited. XNextEvent()
         * does not if signal occurs. This means that graceful
         * exit on e.g. Ctrl-C cannot be implemented without hacks. */
        while (XPending(tray_data.dpy)
            || tray_data.scrollbars_data.scrollbar_down == -1) {
            XNextEvent(tray_data.dpy, &ev);
            xembed_handle_event(ev);
            scrollbars_handle_event(ev);
            switch (ev.type) {
            case VisibilityNotify:
                LOG_TRACE(("VisibilityNotify (0x%lx, state=%d)\n",
                    ev.xvisibility.window, ev.xvisibility.state));
                visibility_notify(ev.xvisibility);
                break;
            case Expose:
                LOG_TRACE(("Expose (0x%lx)\n", ev.xexpose.window));
                expose(ev.xexpose);
                break;
            case PropertyNotify:
                LOG_TRACE(("PropertyNotify(0x%lx)\n", ev.xproperty.window));
                property_notify(ev.xproperty);
                break;
            case DestroyNotify:
                LOG_TRACE(("DestroyNotify(0x%lx)\n", ev.xdestroywindow.window));
                destroy_notify(ev.xdestroywindow);
                break;
            case ClientMessage:
                LOG_TRACE(("ClientMessage(from 0x%lx?)\n", ev.xclient.window));
                client_message(ev.xclient);
                break;
            case ConfigureNotify:
                LOG_TRACE(("ConfigureNotify(0x%lx)\n", ev.xconfigure.window));
                configure_notify(ev.xconfigure);
                break;
            case MapNotify:
                LOG_TRACE(("MapNotify(0x%lx)\n", ev.xmap.window));
                map_notify(ev.xmap);
                break;
            case ReparentNotify:
                LOG_TRACE(("ReparentNotify(0x%lx to 0x%lx)\n",
                    ev.xreparent.window, ev.xreparent.parent));
                reparent_notify(ev.xreparent);
                break;
            case SelectionClear:
                LOG_TRACE(("SelectionClear (0x%lx has lost selection)\n",
                    ev.xselectionclear.window));
                selection_clear(ev.xselectionclear);
                break;
            case SelectionNotify: LOG_TRACE(("SelectionNotify\n")); break;
            case SelectionRequest:
                LOG_TRACE(("SelectionRequest (from 0x%lx to 0x%lx)\n",
                    ev.xselectionrequest.requestor,
                    ev.xselectionrequest.owner));
                break;
            case UnmapNotify:
                LOG_TRACE(("UnmapNotify(0x%lx)\n", ev.xunmap.window));
                unmap_notify(ev.xunmap);
                break;
            default:
#if defined(DEBUG) && defined(_ST_TRACE_EVENTS)
                LOG_TRACE(("Unhandled event: %s, serial: %ld, window: 0x%lx\n",
                    x11_event_names[ev.type], ev.xany.serial, ev.xany.window));
#endif
                break;
            }
            if (tray_data.terminated) goto bailout;
            /* Perform all periodic tasks but for scrollbars */
            perform_periodic_tasks(PT_MASK_ALL & (~PT_MASK_SB));
        }
        perform_periodic_tasks(PT_MASK_ALL);
        my_usleep(500000L);
    }
bailout:
    LOG_TRACE(("Clean exit\n"));
    return 0;
}

/* main() for controlling stalonetray remotely */
int remote_main(int, char **)
{
    Window tray, icon = None;
    int rc;
    int x, y, depth = 0, idummy, i;
    Window win, root;
    unsigned int udummy, w, h;
    tray_init_selection_atoms();
    tray_create_phony_window();
    LOG_TRACE(
        ("name=\"%s\" btn=%d cnt=%d x=%d y=%d\n", settings.remote_click_name,
            settings.remote_click_btn, settings.remote_click_cnt,
            settings.remote_click_pos.x, settings.remote_click_pos.y));
    tray = XGetSelectionOwner(tray_data.dpy, tray_data.xa_tray_selection);
    if (tray == None) return 255;
    /* 1. find window matching requested name */
    icon = x11_find_subwindow_by_name(
        tray_data.dpy, tray, settings.remote_click_name);
    if (icon == None) return 127;
    /* 2. form a message to tray requesting it to show the icon */
    rc = x11_send_client_msg32(tray_data.dpy, /* display */
        tray, /* destination */
        icon, /* window */
        tray_data.xa_tray_opcode, /* atom */
        settings.remote_click_cnt, /* data0 */
        STALONE_TRAY_REMOTE_CONTROL, /* data1 */
        settings.remote_click_btn, /* data2 */
        settings.remote_click_pos.x, /* data3 */
        settings.remote_click_pos.y /* data4 */
    );
    if (!rc) return 63;
    /* 3. Execute the click */
    /* 3.1. Sort out click position */
    XGetGeometry(tray_data.dpy, icon, &root, &idummy, &idummy, &w, &h, &udummy,
        &udummy);
    LOG_TRACE(("wid=0x%lx w=%d h=%d\n", icon, w, h));
    x = (settings.remote_click_pos.x == REMOTE_CLICK_POS_DEFAULT)
        ? w / 2
        : (unsigned int) settings.remote_click_pos.x;
    y = (settings.remote_click_pos.y == REMOTE_CLICK_POS_DEFAULT)
        ? h / 2
        : (unsigned int) settings.remote_click_pos.y;
    /* 3.2. Find subwindow to execute click on */
    win = x11_find_subwindow_at(tray_data.dpy, icon, &x, &y, depth);
    /* 3.3. Send mouse click(s) to target */
    LOG_TRACE(
        ("wid=0x%lx btn=%d x=%d y=%d\n", win, settings.remote_click_btn, x, y));
#define SEND_BTN_EVENT(press, time) \
    do { \
        x11_send_button(tray_data.dpy, /* dispslay */ \
            press, /* event type */ \
            win, /* target window */ \
            root, /* root window */ \
            time, /* time */ \
            settings.remote_click_btn, /* button */ \
            Button1Mask << (settings.remote_click_btn - 1), /* state mask */ \
            x, /* x coord (relative) */ \
            y); /* y coord (relative) */ \
    } while (0)
    for (i = 0; i < settings.remote_click_cnt; i++) {
        SEND_BTN_EVENT(
            1, x11_get_server_timestamp(tray_data.dpy, tray_data.tray));
        my_usleep(250);
        SEND_BTN_EVENT(
            0, x11_get_server_timestamp(tray_data.dpy, tray_data.tray));
    }
#undef SEND_BTN_EVENT
    return 0;
}

/* main() */
int main(int argc, char **argv)
{
    /* Read settings */
    tray_init();
    read_settings(argc, argv);
    /* Register cleanup and signal handlers */
    atexit(cleanup);
    signal(SIGUSR1, &request_tray_status_on_signal);
#ifdef _ST_EXIT_GRACEFULLY
    signal(SIGINT, &exit_on_signal);
    signal(SIGTERM, &exit_on_signal);
    signal(SIGPIPE, &exit_on_signal);
#endif
    /* Open display */
    if ((tray_data.dpy = XOpenDisplay(settings.display_str)) == NULL)
        DIE(("could not open display\n"));
    else
        LOG_TRACE(("Opened dpy %p\n", tray_data.dpy));
#ifdef _ST_EXIT_GRACEFULLY
    if ((async_dpy = XOpenDisplay(settings.display_str)) == NULL)
        DIE(("could not open display\n"));
    else
        LOG_TRACE(("Opened async dpy %p\n", async_dpy));
#endif
    if (settings.xsync) XSynchronize(tray_data.dpy, True);
    x11_trap_errors();
    /* Execute proper main() function */
    if (settings.remote_click_name != NULL)
        return remote_main(argc, argv);
    else
        return tray_main(argc, argv);
}
