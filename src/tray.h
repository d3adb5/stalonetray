/* -------------------------------
 * vim:tabstop=4:shiftwidth=4
 * tray.h
 * Wed, 29 Sep 2004 23:10:02 +0700
 * -------------------------------
 * Common tray routines
 * -------------------------------*/

#ifndef _TRAY_H_
#define _TRAY_H_

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <limits.h>

#include "common.h"
#include "icons.h"
#include "scrollbars.h"
#include "xembed.h"

#ifdef _ST_WITH_XINERAMA
#include <X11/extensions/Xinerama.h>
#endif

/* Tray opcode messages from System Tray Protocol Specification
 * http:freedesktop.org/Standards/systemtray-spec/systemtray-spec-0.2.html */
#define SYSTEM_TRAY_REQUEST_DOCK 0
/* These two are unused */
#define SYSTEM_TRAY_BEGIN_MESSAGE 1
#define SYSTEM_TRAY_CANCEL_MESSAGE 2
/* Custom message: remote control */
#define STALONE_TRAY_REMOTE_CONTROL 0xFFFD
/* Custom message: request for status */
#define STALONE_TRAY_STATUS_REQUESTED 0xFFFE
/* Custom message: confirmation of embedding */
#define STALONE_TRAY_DOCK_CONFIRMED 0xFFFF
/* Name of tray selection atom */
#define TRAY_SEL_ATOM "_NET_SYSTEM_TRAY_S"
/* Name of tray orientation atom*/
#define TRAY_ORIENTATION_ATOM "_NET_SYSTEM_TRAY_ORIENTATION"
/* Name of tray orientation atom*/
#define STALONETRAY_REMOTE_ATOM "STALONETRAY_REMOTE"
/* Values of tray orientation property */
#define _NET_SYSTEM_TRAY_ORIENTATION_HORZ 0
#define _NET_SYSTEM_TRAY_ORIENTATION_VERT 1

/* Window decoration flags */
#define DECO_BORDER (1 << 0)
#define DECO_TITLE (1 << 1)
#define DECO_NONE 0
#define DECO_ALL (DECO_BORDER | DECO_TITLE)

/* WM struts flags */
#define WM_STRUT_NONE 0
#define WM_STRUT_LFT 1
#define WM_STRUT_RHT 2
#define WM_STRUT_TOP 3
#define WM_STRUT_BOT 4
#define WM_STRUT_AUTO 5

/* Dockapp modes */
#define DOCKAPP_NONE 0
#define DOCKAPP_SIMPLE 1
#define DOCKAPP_WMAKER 2

/* Kludge flags */
#define KLUDGE_FIX_WND_SIZE (1L << 1)
#define KLUDGE_FIX_WND_POS (1L << 2)
#define KLUDGE_USE_ICONS_HINTS (1L << 3)
#define KLUDGE_FORCE_ICONS_SIZE (1L << 4)

/* Remote click constants */
#define REMOTE_CLICK_BTN_DEFAULT Button1
#define REMOTE_CLICK_POS_DEFAULT INT_MAX
#define REMOTE_CLICK_CNT_DEFAULT 1

/* Structure to hold all tray data */
struct TrayData {
    /* General */
    Window tray; /* ID of tray window */
    Window hint_win; /* ID of icon window */
    Display *dpy; /* Display pointer */
    XSizeHints xsh; /* Size & position of the tray window */
    XSizeHints root_wnd; /* Size & position :) of the root window */
    Window old_selection_owner; /* Old owner of tray selection */
    int terminated; /* Exit flag */
    int is_active; /* Is the tray active? */
    int is_reparented; /* Was the tray reparented in smth like FvwmButtons ? */
    int kde_tray_old_mode; /* Use legacy scheme to handle KDE icons via
                              MapNotify */

    /* Atoms */
    Atom xa_tray_selection; /* Atom: _NET_SYSTEM_TRAY_SELECTION_S<creen number>
                             */
    Atom xa_tray_opcode; /* Atom: _NET_SYSTEM_TRAY_MESSAGE_OPCODE */
    Atom xa_tray_data; /* Atom: _NET_SYSTEM_TRAY_MESSAGE_DATA */
    Atom xa_wm_protocols; /* Atom: WM_PROTOCOLS */
    Atom xa_wm_delete_window; /* Atom: WM_DELETE_WINDOW */
    Atom xa_net_wm_ping; /* Atom: WM_PING */
    Atom xa_wm_take_focus; /* Atom: WM_TAKE_FOCUS */
    Atom xa_kde_net_system_tray_windows; /* Atom: _KDE_NET_SYSTEM_TRAY_WINDOWS
                                          */
    Atom xa_net_client_list; /* Atom: _NET_CLIENT_LIST */

    /* Background pixmap */
    Atom xa_xrootpmap_id; /* Atom: _XROOTPMAP_ID */
    Atom xa_xsetroot_id; /* Atom: _XSETROOT_ID */
    Pixmap bg_pmap; /* Pixmap for tray background */
    struct Point bg_pmap_dims; /* Background pixmap dimensions */

#ifdef _ST_WITH_XINERAMA
    /* Xinerama data */
    int xinerama_active; /* Is Xinerama active? */
    int n_monitors; /* Number of Xinerama screens */
    XineramaScreenInfo *monitors; /* Xinerama screens info */
#endif

    /* XEMBED data */
    struct XEMBEDData xembed_data; /* XEMBED data */

    /* Scrollbar data */
    struct ScrollbarsData scrollbars_data;
};
extern struct TrayData tray_data;

/* Initialize all tray data structures */
void tray_init();
/* Create tray window */
void tray_create_window(int argc, char **argv);
/* Create phony tray window so that certain x11_ calls work */
void tray_create_phony_window();
/* Initialize tray selection atoms */
void tray_init_selection_atoms();
/* Acquire tray selection */
void tray_acquire_selection();
/* Show tray window */
void tray_show_window();
/* Refresh tray window */
void tray_refresh_window(int exposures);
/* Update tray background (and pixmap, if update_pixmap is true) */
int tray_update_bg(int update_pixmap);
/* Calculate tray window size given the size of icon area in pixels. */
int tray_calc_window_size(
    int base_width, int base_height, int *new_width, int *new_height);
/* Calculate size of icon area given the tray window size in pixels. */
int tray_calc_tray_area_size(
    int wnd_width, int wnd_height, int *base_width, int *base_height);
/* Update window struts (if enabled) */
int tray_update_window_strut();
/* Update tray window size and hints */
int tray_update_window_props();
/* Set tray window WM hints */
int tray_set_wm_hints();

#endif
