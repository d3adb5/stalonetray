/* ************************************
 * vim:tabstop=4:shiftwidth=4
 * xutils.c
 * Sun, 05 Mar 2006 17:56:56 +0600
 * ************************************
 * misc X11 utilities
 * ************************************/

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xmd.h>
#include <X11/Xutil.h>

#include <limits.h>
#include <unistd.h>
#include <string.h>

#include "xutils.h"

#include "common.h"
#include "debug.h"

static int trapped_x11_error_code = 0;
static int (*old_x11_error_handler)(Display *, XErrorEvent *) = NULL;
static int current_x11_connection_status = 1;
static int (*old_x11_io_error_handler)(Display *) = NULL;

int x11_io_error_handler(Display *dpy)
{
    current_x11_connection_status = 0;
    old_x11_io_error_handler(dpy);
    DIE(("Connection to X11 server lost. Dying.\n"));
}

int x11_connection_status()
{
    return current_x11_connection_status;
}

int x11_error_handler(Display *dpy, XErrorEvent *err)
{
    static char msg[PATH_MAX], req_num_str[32], req_str[PATH_MAX];
    trapped_x11_error_code = err->error_code;
    XGetErrorText(dpy, trapped_x11_error_code, msg, sizeof(msg) - 1);
    snprintf(req_num_str, 32, "%d", err->request_code);
    XGetErrorDatabaseText(
        dpy, "XRequest", req_num_str, "Unknown", req_str, PATH_MAX);
    LOG_ERROR(("X11 error: %s (request: %s, resource 0x%x)\n", msg, req_str,
        err->request_code));
    return 0;
}

int x11_ok_helper(const char *file, const int line, const char *func)
{
    if (trapped_x11_error_code) {
        LOG_ERROR(("X11 error %d detected at %s:%d:%s\n",
            trapped_x11_error_code, file, line, func));
        trapped_x11_error_code = 0;
        return FAILURE;
    } else
        return SUCCESS;
}

int x11_current_error()
{
    return trapped_x11_error_code;
}

void x11_trap_errors()
{
    old_x11_io_error_handler = XSetIOErrorHandler(x11_io_error_handler);
    old_x11_error_handler = XSetErrorHandler(x11_error_handler);
    trapped_x11_error_code = 0;
}

int x11_untrap_errors()
{
    XSetErrorHandler(old_x11_error_handler);
    return trapped_x11_error_code;
}

static Window timestamp_wnd;
static Atom timestamp_atom = None;

Bool x11_wait_for_timestamp(Display *, XEvent *xevent, XPointer data)
{
    return ((xevent->type == PropertyNotify
                && xevent->xproperty.window == *((Window *)data)
                && xevent->xproperty.atom == timestamp_atom)
        || (xevent->type == DestroyNotify
            && xevent->xdestroywindow.window == *((Window *)data)));
}

Time x11_get_server_timestamp(Display *dpy, Window wnd)
{
    unsigned char c = 's';
    XEvent xevent;

    if (timestamp_atom == None)
        timestamp_atom = XInternAtom(dpy, "STALONETRAY_TIMESTAMP", False);

    x11_ok(); /* Just reset the status (XXX) */
    /* Trigger PropertyNotify event which has a timestamp field */
    XChangeProperty(
        dpy, wnd, timestamp_atom, timestamp_atom, 8, PropModeReplace, &c, 1);
    if (!x11_ok()) return CurrentTime;

    /* Wait for the event
     * XXX: this may block... */
    timestamp_wnd = wnd;
    XIfEvent(dpy, &xevent, x11_wait_for_timestamp, (XPointer)&timestamp_wnd);

    return x11_ok() ? xevent.xproperty.time : CurrentTime;
}

int x11_get_window_prop32(Display *dpy, Window dst, Atom atom, Atom type,
    unsigned char **data, unsigned long *len)
{
    Atom act_type;
    int act_fmt, rc;
    unsigned long bytes_after, prop_len, buf_len;
    unsigned char *buf = NULL;

    *data = NULL;
    *len = 0;
    /* Get the property size */
    rc = XGetWindowProperty(dpy, dst, atom, 0L, 1L, False, type, &act_type,
        &act_fmt, &buf_len, &bytes_after, &buf);

    /* The requested property does not exist */
    if (!x11_ok() || rc != Success || act_type != type || act_fmt != 32)
        return FAILURE;

    if (buf != NULL) XFree(buf);

    /* Now go get the property */
    prop_len = bytes_after / 4 + 1;
    XGetWindowProperty(dpy, dst, atom, 0L, prop_len, False, type, &act_type,
        &act_fmt, &buf_len, &bytes_after, &buf);

    if (x11_ok()) {
        *len = buf_len;
        *data = buf;
        return SUCCESS;
    } else {
        return FAILURE;
    }
}

int x11_send_client_msg32(Display *dpy, Window dst, Window wnd, Atom type,
    long data0, long data1, long data2, long data3, long data4)
{
    XEvent ev;
    Status rc;
    ev.xclient.type = ClientMessage;
    ev.xclient.serial = 0;
    ev.xclient.send_event = True;
    ev.xclient.message_type = type;
    ev.xclient.window = wnd;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = data0;
    ev.xclient.data.l[1] = data1;
    ev.xclient.data.l[2] = data2;
    ev.xclient.data.l[3] = data3;
    ev.xclient.data.l[4] = data4;
    /* XXX: Replace 0xFFFFFF for better portability? */
    /* XXX: This should actually read NoEventMask...
     * seems like extra parameter is necessary */
    rc = XSendEvent(dpy, dst, False, 0xFFFFFF, &ev);
    return x11_ok() && rc != 0;
}

int x11_send_visibility(Display *, Window dst, long state)
{
    XEvent xe;
    int rc;
    xe.type = VisibilityNotify;
    xe.xvisibility.window = dst;
    xe.xvisibility.state = state;
    rc = XSendEvent(tray_data.dpy, dst, True, NoEventMask, &xe);
    return x11_ok() && rc != 0;
}

int x11_send_button(Display *dpy, int press, Window dst, Window root,
    Time time, unsigned int button, unsigned int state, int x, int y)
{
    XEvent ev;
    int rx, ry, rc, idummy;
    unsigned int udummy;
    Window dst_root;

    if (!x11_get_window_abs_coords(dpy, dst, &rx, &ry)) return FAILURE;
    XGetGeometry(dpy, dst, &dst_root, &idummy, &idummy, &udummy, &udummy,
        &udummy, &udummy);
    if (!x11_ok()) return FAILURE;

    ev.type = press ? ButtonPress : ButtonRelease;
    ev.xbutton.display = dpy;
    ev.xbutton.window = dst;
    ev.xbutton.subwindow = None;
    ev.xbutton.root = root;
    ev.xbutton.time = time;
    ev.xbutton.x = x;
    ev.xbutton.y = y;
    ev.xbutton.x_root = rx + x;
    ev.xbutton.y_root = ry + y;
    ev.xbutton.button = button;
    ev.xbutton.state = state;
    ev.xbutton.same_screen = (root == dst_root);

    rc = XSendEvent(dpy, dst, True,
        SubstructureNotifyMask | (press ? ButtonPressMask : ButtonReleaseMask),
        &ev);
    return rc && x11_ok();
}

int x11_send_expose(
    Display *, Window dst, int x, int y, int width, int height)
{
    XEvent xe;
    int rc;
    xe.type = Expose;
    xe.xexpose.window = dst;
    xe.xexpose.x = x;
    xe.xexpose.y = y;
    xe.xexpose.width = width;
    xe.xexpose.height = height;
    xe.xexpose.count = 0;
    rc = XSendEvent(tray_data.dpy, dst, True, NoEventMask, &xe);
    return x11_ok() && rc != 0;
}

int x11_refresh_window(
    Display *, Window dst, int width, int height, int exposures)
{
    x11_send_visibility(tray_data.dpy, dst, VisibilityFullyObscured);
    x11_send_visibility(tray_data.dpy, dst, VisibilityUnobscured);
    XClearArea(tray_data.dpy, dst, 0, 0, width, height, exposures);
    return x11_ok();
}

int x11_set_window_size(Display *dpy, Window w, int x, int y)
{
    XSizeHints xsh;
    xsh.flags = PSize;
    xsh.width = x;
    xsh.height = y;
    XSetWMNormalHints(dpy, w, &xsh);
    XResizeWindow(dpy, w, x, y);
    if (!x11_ok()) return FAILURE;
    return SUCCESS;
}

int x11_get_window_size(Display *dpy, Window w, int *x, int *y)
{
    XWindowAttributes xwa;
    XGetWindowAttributes(dpy, w, &xwa);
    if (!x11_ok()) return FAILURE;
    *x = xwa.width;
    *y = xwa.height;
    return SUCCESS;
}

int x11_get_window_min_size(Display *dpy, Window w, int *x, int *y)
{
    XSizeHints xsh;
    long flags = 0;
    int rc = FAILURE;
    if (XGetWMNormalHints(dpy, w, &xsh, &flags)) {
        flags = xsh.flags & flags;
        if (flags & PMinSize) {
            *x = xsh.min_width;
            *y = xsh.min_height;
            rc = SUCCESS;
        }
    }
    return x11_ok() && rc;
}

int x11_get_window_abs_coords(Display *dpy, Window dst, int *x, int *y)
{
    XWindowAttributes xwa;
    Window child;
#if 0
	x11_ok(); /* XXX: this should go away, shouldn't it? */
#endif
    XGetWindowAttributes(dpy, dst, &xwa);
    XTranslateCoordinates(dpy, dst, xwa.root, 0, 0, x, y, &child);
    return x11_ok();
}

char *x11_get_window_name(Display *dpy, Window dst, char *def)
{
    static char *name;
    if (name != NULL) XFree(name);
    if (!XFetchName(dpy, dst, &name)) name = NULL;
    return (name != NULL ? name : def);
}

Window x11_find_subwindow_by_name(Display *dpy, Window tgt, char *name)
{
    char *tgt_name = NULL;
    Window ret = None, *children, dummy;
    unsigned int i, nchildren;
    if (XFetchName(dpy, tgt, &tgt_name)) {
        LOG_TRACE(("tgt_name=\"%s\", name=\"%s\"\n", tgt_name, name));
        if (!strcmp(tgt_name, name)) ret = tgt;
    }
    if (!x11_ok()) return None;
    if (tgt_name != NULL) XFree(tgt_name);
    if (ret != None) return ret;
    XQueryTree(dpy, tgt, &dummy, &dummy, &children, &nchildren);
    if (!x11_ok()) return None;
    for (i = 0; i < nchildren; i++) {
        ret = x11_find_subwindow_by_name(dpy, children[i], name);
        if (ret != None) break;
    }
    if (children != NULL) XFree(children);
    return ret;
}

/* x and y are relative to top at input, and relative to found window on return
 */
Window x11_find_subwindow_at(
    Display *dpy, Window top, int *x, int *y, int depth)
{
    int d, c, bx = 0, by = 0;
    Window dummy, *children;
    unsigned int nchildren;
    Window cur = top, old = None;
    for (d = 1; d != depth && cur != old && old != None; d++) {
        old = cur;
        /* Query children of current window and traverse them starting
         * from the top in stacking order (i.e. from the end of the list
         * returned by XQueryTree) */
        XQueryTree(dpy, cur, &dummy, &dummy, &children, &nchildren);
        LOG_TRACE(("cur=0x%lx nchildren=%d\n", cur, nchildren));
        if (!x11_ok()) goto fail;
        /* Exit the loop if window has no children */
        if (nchildren == 0) break;
        /* Loop over children starting from topmost */
        for (c = nchildren - 1; c >= 0; c--) {
            XWindowAttributes xwa;
            XGetWindowAttributes(dpy, children[c], &xwa);
            if (!x11_ok()) goto fail;
            /* Check if this child contains the (x,y) point */
            if (xwa.x + bx <= *x && *x < xwa.x + xwa.width + bx
                && xwa.y + by <= *y && *y < xwa.y + xwa.height + by) {
                cur = children[c];
                bx += xwa.x;
                by += xwa.y;
                break;
            }
        }
        if (children != NULL) XFree(children);
    }
    *x -= bx;
    *y -= by;
    return cur;
fail:
    if (children != NULL) XFree(children);
    return None;
}

void x11_extend_root_event_mask(Display *dpy, long mask)
{
    static long old_mask = 0;
    old_mask |= mask;
    XSelectInput(dpy, DefaultRootWindow(dpy), old_mask);
}

int x11_parse_color(Display *dpy, char *str, XColor *color)
{
    int rc;
    rc = XParseColor(
        dpy, XDefaultColormap(dpy, DefaultScreen(dpy)), str, color);
    if (rc) XAllocColor(dpy, XDefaultColormap(dpy, DefaultScreen(dpy)), color);
    return x11_ok() && rc;
}

char *x11_get_window_class(Display *dpy, Window w) {
    XClassHint hint;
    Status res = 0;
    char *classname = NULL;

    res = XGetClassHint(dpy, w, &hint);
    if (res == 0) {
        LOG_TRACE(("XGetClassHint(dpy, 0x%lx, &hint) failed: %x", w, res));
        return NULL;
    }

    classname = strdup(hint.res_class);
    XFree(hint.res_name);
    XFree(hint.res_class);

    return classname;
}

#ifdef DEBUG
const char *x11_event_names[LASTEvent] = {"unknown0", "unknown1", "KeyPress",
    "KeyRelease", "ButtonPress", "ButtonRelease", "MotionNotify",
    "EnterNotify", "LeaveNotify", "FocusIn", "FocusOut", "KeymapNotify",
    "Expose", "GraphicsExpose", "NoExpose", "VisibilityNotify", "CreateNotify",
    "DestroyNotify", "UnmapNotify", "MapNotify", "MapRequest",
    "ReparentNotify", "ConfigureNotify", "ConfigureRequest", "GravityNotify",
    "ResizeRequest", "CirculateNotify", "CirculateRequest", "PropertyNotify",
    "SelectionClear", "SelectionRequest", "SelectionNotify", "ColormapNotify",
    "ClientMessage", "MappingNotify"};

void x11_dump_win_info(Display *dpy, Window wid)
{
#if defined(DEBUG) && defined(_ST_WITH_DUMP_WIN_INFO)
    if (settings.log_level >= LOG_LEVEL_TRACE) {
        char cmd[PATH_MAX];
        int rc;
        snprintf(cmd, PATH_MAX,
            "xwininfo -tree -size -bits -stats -id 0x%x 1>&2\n",
            (unsigned int)wid);
        rc = system(cmd);
        if (rc != 0) LOG_ERROR(("command failed: '%s'\n", cmd));
        snprintf(cmd, PATH_MAX, "xprop -id 0x%x 1>&2\n", (unsigned int)wid);
        rc = system(cmd);
        if (rc != 0) LOG_ERROR(("command failed: '%s'\n", cmd));
    }
#endif
}
#endif
