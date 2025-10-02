/* -------------------------------
 * vim:tabstop=4:shiftwidth=4
 * debug.c
 * Mon, 01 May 2006 01:44:42 +0700
 * -------------------------------
 * Debugging code/utilities
 * -------------------------------*/

#include "debug.h"
#include "xutils.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef DBG_PRINT_PID
#include <sys/types.h>
#include <unistd.h>
#endif

int debug_output_disabled = 0;

/* Disables all output from debugging macros */
void debug_disable_output()
{
    debug_output_disabled = 1;
}

/* Print the message to STDERR (varadic macros is not used in the sake of
 * portability) */
void print_message_to_stderr(const char *fmt, ...)
{
    static char msg[PATH_MAX];
    va_list va;
    va_start(va, fmt);
    vsnprintf(msg, PATH_MAX, fmt, va);
    va_end(va);
    fprintf(stderr, "%s", msg);
}

#ifdef DEBUG

int trace_mode = False;

void print_trace_header(
    const char *funcname, const char *fname, const int line)
{
    static pid_t pid = 0;
#ifdef TRACE_TIMESTAMP
    {
        static char timestr[PATH_MAX + 1];
        time_t curtime = time(NULL);
        struct tm *loctime = localtime(&curtime);
        strftime(timestr, PATH_MAX, "%b %e %H:%M:%S", loctime);
        fprintf(stderr, "%s ", timestr);
    }
#endif
#ifdef TRACE_WM_NAME
    fprintf(stderr, "%s ", settings.wnd_name);
#endif
#ifdef TRACE_PID
    if (!pid) pid = getpid();
    fprintf(stderr, "(%d) ", pid);
#endif
#ifdef TRACE_DPY
    if (tray_data.dpy != NULL)
        fprintf(stderr, "(%s) ", DisplayString(tray_data.dpy));
    else
        fprintf(stderr, "(dpy) ");
#endif
#ifdef TRACE_VERBOSE_LOCATION
    fprintf(stderr, "(%s:%4d) ", fname, line);
#endif
    fprintf(stderr, "%s(): ", funcname);
}
#endif

/* Print the summary of icon data */
int print_icon_data(struct TrayIcon *ti)
{
#ifdef DEBUG
    XWindowAttributes xwa;
#endif
    LOG_INFO(("wid = 0x%lx\n", ti->wid));
    LOG_TRACE(("  self = %p\n", ti));
    LOG_TRACE(("  prev = %p\n", ti->prev));
    LOG_TRACE(("  next = %p\n", ti->next));
    LOG_TRACE(("  invalid = %d\n", ti->is_invalid));
    LOG_TRACE(("  layed_out = %d\n", ti->is_layed_out));
    LOG_TRACE(("  update_pos = %d\n", ti->is_updated));
    LOG_INFO(("  name = %s\n",
        x11_get_window_name(tray_data.dpy, ti->wid, "<unknown>")));
    LOG_INFO(("  visible = %d\n", ti->is_visible));
    LOG_INFO(("  position (grid) = %dx%d+%d+%d\n", ti->l.grd_rect.w,
        ti->l.grd_rect.h, ti->l.grd_rect.x, ti->l.grd_rect.y));
    LOG_INFO(("  position (pixels) = %dx%d+%d+%d\n", ti->l.icn_rect.w,
        ti->l.icn_rect.h, ti->l.icn_rect.x, ti->l.icn_rect.y));
    LOG_INFO(("  wnd_sz = %dx%d\n", ti->l.wnd_sz.x, ti->l.wnd_sz.y));
    LOG_TRACE(("  cmode = %d\n", ti->cmode));
    LOG_INFO(("  xembed = %d\n", ti->is_xembed_supported));
    if (ti->is_xembed_supported) {
        LOG_TRACE(
            ("  xembed_accepts_focus = %d\n", ti->is_xembed_accepts_focus));
        LOG_TRACE(
            ("  xembed_last_timestamp = %ld\n", ti->xembed_last_timestamp));
        LOG_TRACE(("  xembed_last_msgid = %ld\n", ti->xembed_last_msgid));
    }
    LOG_INFO(("  embedded = %d\n", ti->is_embedded));
#ifdef DEBUG
    if (ti->is_embedded) {
        LOG_TRACE(("  mid-parent = 0x%lx\n", ti->mid_parent));
        if (!XGetWindowAttributes(tray_data.dpy, ti->mid_parent, &xwa)) {
            LOG_TRACE((
                "  ERR: XGetWindowAttributes() on mid-parent window faied\n"));
        } else {
            LOG_TRACE(("  mid-parent wid = 0x%lx\n", ti->mid_parent));
            LOG_TRACE(("  mid-parent state = %s\n",
                xwa.map_state == IsUnmapped
                    ? "Unmapped"
                    : (xwa.map_state == IsUnviewable ? "Unviewable"
                                                     : "Viewable")));
            LOG_TRACE(("  mid-parent geometry = %dx%d+%d+%d\n", xwa.width,
                xwa.height, xwa.x, xwa.y));
        }
    }
    if (settings.log_level > LOG_LEVEL_INFO) {
        Window real_parent, root, *children = NULL;
        unsigned int nchildren = 0;
        int rc;
        rc = XQueryTree(tray_data.dpy, ti->wid, &root, &real_parent, &children,
            &nchildren);
        if (rc && children != NULL && nchildren > 0) XFree(children);
        if (!rc) {
            LOG_TRACE(("  ERR: XQueryTree() failed\n"));
        } else {
            LOG_TRACE(
                ("  parent wid from XQueryTree() = 0x%lx\n", real_parent));
        }
    }
    if (!XGetWindowAttributes(tray_data.dpy, ti->wid, &xwa)) {
        LOG_TRACE(("  ERR: XGetWindowAttributes() on icon window failed\n"));
    } else {
        LOG_TRACE(("  geometry from XGetWindowAttributes() = %dx%d+%d+%d\n",
            xwa.width, xwa.height, xwa.x, xwa.y));
        LOG_TRACE(("  mapstate from XGetWindowAttributes() = %s\n",
            xwa.map_state == IsUnmapped
                ? "Unmapped"
                : (xwa.map_state == IsUnviewable ? "Unviewable"
                                                 : "Viewable")));
    }
#endif
    /* This resets x11 error state (which might have left from X11 calls above)
     */
    x11_ok();
    return NO_MATCH;
}

void dump_icon_list()
{
    icon_list_forall(&print_icon_data);
}
