/* ************************************
 * vim:tabstop=4:shiftwidth=4
 * xutils.c
 * Sun, 05 Mar 2006 17:56:56 +0600
 * ************************************
 * misc X11 utilities
 * ************************************/

#include <X11/Xmd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

#include <limits.h>
#include <unistd.h>

#include "common.h"

#include "xutils.h"

static int trapped_x11_error_code = 0;
static int (*old_x11_error_handler) (Display *, XErrorEvent *);

int x11_error_handler(Display *dpy, XErrorEvent *err)
{
	static char msg[PATH_MAX];
	trapped_x11_error_code = err->error_code;
	XGetErrorText(dpy, trapped_x11_error_code, msg, sizeof(msg)-1);
	DBG(0, ("X11 error: %s (request: %u/%u, resource 0x%x)\n", msg, err->request_code, err->minor_code, err->resourceid));
	return 0;
}

int x11_ok_helper(const char *file, const int line, const char *func)
{
	if (trapped_x11_error_code) {
		DBG(0, ("X11 error %d detected at %s:%d:%s\n",
					trapped_x11_error_code,
					file,
					line,
					func));
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

Bool x11_wait_for_timestamp(Display *dpy, XEvent *xevent, XPointer data)
{
	return (xevent->type == PropertyNotify &&
	    xevent->xproperty.window == *((Window *)data) &&
		xevent->xproperty.atom == timestamp_atom);
}

Time x11_get_server_timestamp(Display *dpy, Window wnd)
{
	unsigned char c = 's';
	XEvent xevent;

	if (timestamp_atom == None)
		timestamp_atom = XInternAtom(dpy, "STALONETRAY_TIMESTAMP", False);

	/* Trigger PropertyNotify event which has a timestamp field */
	XChangeProperty(dpy, wnd, timestamp_atom, timestamp_atom, 8, PropModeReplace, &c, 1);
	if (!x11_ok()) return CurrentTime;

	/* Wait for the event */
	timestamp_wnd = wnd;
	XIfEvent(dpy, &xevent, x11_wait_for_timestamp, (XPointer)&timestamp_wnd);

	return x11_ok() ? xevent.xproperty.time : CurrentTime;
}

int x11_get_win_prop32(Display *dpy, Window dst, Atom atom, Atom type, unsigned char **data, unsigned long *len)
{
	Atom act_type;
	int act_fmt, rc;
	unsigned long bytes_after, prop_len, buf_len;
	unsigned char *buf = NULL;	

	*data = NULL; *len = 0;
	/* Get the property size */
	rc = XGetWindowProperty(dpy, dst, atom,
		    0L, 0L, False, type, &act_type, &act_fmt,
			&buf_len, &bytes_after, &buf);

	/* The requested property does not exist */
	if (rc != Success || act_type != type || act_fmt != 32) return FAILURE;

	XFree(buf);

	/* Now go get the property */
	prop_len = bytes_after / 4;
	XGetWindowProperty(dpy, dst, atom,
			0L, prop_len, False, type, &act_type, &act_fmt,
			&buf_len, &bytes_after, &buf);

	if (x11_ok()) {
		*len = buf_len;
		*data = buf;
		return SUCCESS;
	} else
		return FAILURE;
}

int x11_send_client_msg32(Display *dpy, Window dst, Window wnd, Atom type, long data0, long data1, long data2, long data3, long data4)
{
	XEvent ev;
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
	return XSendEvent(dpy, dst, False, 0xFFFFFF, &ev);
}

int x11_set_window_size(Display *dpy, Window w, int x, int y)
{
	XSizeHints xsh;
	
	xsh.flags = PSize;
	xsh.width = x;
	xsh.height = y;

	XSetWMNormalHints(dpy, w, &xsh);
	XResizeWindow(dpy, w, x, y);

	if (!x11_ok()) {
		DBG(0, ("failed to force 0x%x size to %dx%d\n", w, x, y));
		return FAILURE;
	}

	return SUCCESS;
}

int x11_get_window_size(Display *dpy, Window w, int *x, int *y)
{
	XWindowAttributes xwa;

	XGetWindowAttributes(dpy, w, &xwa);
	
	if (!x11_ok()) {
		DBG(0, ("failed to get 0x%x attributes\n", w));
		return FAILURE;
	}

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
		DBG(4, ("flags = 0x%x\n", flags));
		if (flags & PMinSize) {
			DBG(4, ("min_width = %d, min_height = %d\n", xsh.min_width, xsh.min_height));
			*x = xsh.min_width;
			*y = xsh.min_height;
			rc = SUCCESS;
		}
	}
	return rc;
}

int x11_get_window_abs_coords(Display *dpy, Window dst, int *x, int *y)
{
	Window root, parent, *wjunk = NULL;
	int x11_, y_, x11__, y__;
	unsigned int junk;

	XGetGeometry(dpy, dst, &root, &x11_, &y_, &junk, &junk, &junk, &junk);
	XQueryTree(dpy, dst, &root, &parent, &wjunk, &junk);

	if (junk != 0) XFree(wjunk);

	if (!x11_ok())
		return FAILURE;

	if (parent == root) {
		*x = x11_;
		*y = y_;
	} else {
		if (x11_get_window_abs_coords(dpy, parent, &x11__, &y__)) {
			*x = x11_ + x11__;
			*y = y_ + y__;
		} else
			return FAILURE;
	}
	
	return SUCCESS;
}

void x11_extend_root_event_mask(Display *dpy, long mask)
{
	static long old_mask = 0;
	old_mask |= mask;
	XSelectInput(dpy, RootWindow(dpy, DefaultScreen(dpy)), old_mask);
}

#ifdef DEBUG
const char *x11_event_names[LASTEvent] = {
	"unknown0",
	"unknown1",
	"KeyPress",
	"KeyRelease",
	"ButtonPress",
	"ButtonRelease",
	"MotionNotify",
	"EnterNotify",
	"LeaveNotify",
	"FocusIn",
	"FocusOut",
	"KeymapNotify",
	"Expose",
	"GraphicsExpose",
	"NoExpose",
	"VisibilityNotify",
	"CreateNotify",
	"DestroyNotify",
	"UnmapNotify",
	"MapNotify",
	"MapRequest",
	"ReparentNotify",
	"ConfigureNotify",
	"ConfigureRequest",
	"GravityNotify",
	"ResizeRequest",
	"CirculateNotify",
	"CirculateRequest",
	"PropertyNotify",
	"SelectionClear",
	"SelectionRequest",
	"SelectionNotify",
	"ColormapNotify",
	"ClientMessage",
	"MappingNotify"
};

void x11_dump_win_info(Display *dpy, Window wid)
{	
#ifdef _ST_WITH_DUMP_WIN_INFO
	char cmd[PATH_MAX];

	DBG(4, ("Dumping info for 0x%x\n", wid));

	snprintf(cmd, PATH_MAX, "xwininfo -size -bits -stats -id 0x%x\n", (unsigned int) wid);
	system(cmd);

	snprintf(cmd, PATH_MAX, "xprop -id 0x%x\n", (unsigned int) wid);
	system(cmd);
#endif
}	
#endif
