/* -------------------------------
 * vim:tabstop=4:shiftwidth=4
 * ewmh.h
 * Thu, 30 Mar 2006 23:12:43 +0700
 * -------------------------------
 * EWMH/MWM support
 * ------------------------------- */

#ifndef _EWMH_H_
#define _EWMH_H_

#include <X11/X.h>
#include <X11/Xlib.h>

/* Defines for Motif WM functions */
#define MWM_FUNC_ALL (1L << 0)
#define MWM_FUNC_RESIZE (1L << 1)
#define MWM_FUNC_MOVE (1L << 2)
#define MWM_FUNC_MINIMIZE (1L << 3)
#define MWM_FUNC_MAXIMIZE (1L << 4)
#define MWM_FUNC_CLOSE (1L << 5)
/* Defines for Motif WM decorations */
#define MWM_DECOR_ALL (1L << 0)
#define MWM_DECOR_BORDER (1L << 1)
#define MWM_DECOR_RESIZEH (1L << 2)
#define MWM_DECOR_TITLE (1L << 3)
#define MWM_DECOR_MENU (1L << 4)
#define MWM_DECOR_MINIMIZE (1L << 5)
#define MWM_DECOR_MAXIMIZE (1L << 6)

/* EWMH atoms */
#define _NET_SUPPORTED "_NET_SUPPORTED"
#define _NET_SUPPORTING_WM_CHECK "_NET_SUPPORTING_WM_CHECK"
#define _NET_WM_DESKTOP "_NET_WM_DESKTOP"
#define _NET_ACTIVE_WINDOW "_NET_ACTIVE_WINDOW"
#define _NET_CLIENT_LIST "_NET_CLIENT_LIST"
#define _NET_WM_PING "_NET_WM_PING"
#define _NET_WM_STRUT_PARTIAL "_NET_WM_STRUT_PARTIAL"
#define _NET_WM_STRUT "_NET_WM_STRUT"

/* Defines for EWMH window types */
#define _NET_WM_WINDOW_TYPE_DESKTOP "_NET_WM_WINDOW_TYPE_DESKTOP"
#define _NET_WM_WINDOW_TYPE_DOCK "_NET_WM_WINDOW_TYPE_DOCK"
#define _NET_WM_WINDOW_TYPE_TOOLBAR "_NET_WM_WINDOW_TYPE_TOOLBAR"
#define _NET_WM_WINDOW_TYPE_MENU "_NET_WM_WINDOW_TYPE_MENU"
#define _NET_WM_WINDOW_TYPE_UTILITY "_NET_WM_WINDOW_TYPE_UTILITY"
#define _NET_WM_WINDOW_TYPE_SPLASH "_NET_WM_WINDOW_TYPE_SPLASH"
#define _NET_WM_WINDOW_TYPE_DIALOG "_NET_WM_WINDOW_TYPE_DIALOG"
#define _NET_WM_WINDOW_TYPE_NORMAL "_NET_WM_WINDOW_TYPE_NORMAL"

/* Defined for EWMH window states */
#define _NET_WM_STATE_MODAL "_NET_WM_STATE_MODAL"
#define _NET_WM_STATE_STICKY "_NET_WM_STATE_STICKY"
#define _NET_WM_STATE_MAXIMIZED_VERT "_NET_WM_STATE_MAXIMIZED_VERT"
#define _NET_WM_STATE_MAXIMIZED_HORZ "_NET_WM_STATE_MAXIMIZED_HORZ"
#define _NET_WM_STATE_SHADED "_NET_WM_STATE_SHADED"
#define _NET_WM_STATE_SKIP_TASKBAR "_NET_WM_STATE_SKIP_TASKBAR"
#define _NET_WM_STATE_SKIP_PAGER "_NET_WM_STATE_SKIP_PAGER"
#define _NET_WM_STATE_HIDDEN "_NET_WM_STATE_HIDDEN"
#define _NET_WM_STATE_FULLSCREEN "_NET_WM_STATE_FULLSCREEN"
#define _NET_WM_STATE_ABOVE "_NET_WM_STATE_ABOVE"
#define _NET_WM_STATE_BELOW "_NET_WM_STATE_BELOW"
#define _NET_WM_STATE_DEMANDS_ATTENTION "_NET_WM_STATE_DEMANDS_ATTENTION"

/* Flags for window state manipulations */
#define _NET_WM_STATE_REMOVE 0 /* remove/unset property */
#define _NET_WM_STATE_ADD 1 /* add/set property */
#define _NET_WM_STATE_TOGGLE 2 /* toggle property  */

#define _NET_WM_STRUT_PARTIAL_SZ 12
#define _NET_WM_STRUT_SZ 4
typedef unsigned long wm_strut_t[_NET_WM_STRUT_PARTIAL_SZ];
#define WM_STRUT_IDX_LFT 0
#define WM_STRUT_IDX_RHT 1
#define WM_STRUT_IDX_TOP 2
#define WM_STRUT_IDX_BOT 3
#define WM_STRUT_IDX_START(B) B * 2 + 4
#define WM_STRUT_IDX_END(B) B * 2 + 5

/* Check if WM that supports EWMH hints is present on given display */
int ewmh_wm_present(Display *dpy);
/* Add window type for the window wnd */
int ewmh_add_window_state(Display *dpy, Window wnd, char *state);
/* Add window type for the window wnd */
int ewmh_add_window_type(Display *dpy, Window wnd, char *type);
/* Set data for _NET_WM_STRUT_PARTIAL hint */
int ewmh_set_window_strut(Display *dpy, Window wnd, wm_strut_t wm_strut);
/* Set CARD32 value of EWMH atom for a given window */
int ewmh_set_window_atom32(
    Display *dpy, Window wnd, char *prop_name, CARD32 value);
/* Set Motif WM hints for window wnd; read MWM spec for more info */
int mwm_set_hints(Display *dpy, Window wnd, unsigned long decorations,
    unsigned long functions);

#ifdef DEBUG
/* Dumps EWMH atoms supported by WM */
int ewmh_list_supported_atoms(Display *dpy);
/* Dump all EWMH states that have been set for the window wnd */
int ewmh_dump_window_states(Display *dpy, Window wnd);
#endif

#endif
