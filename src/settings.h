/* -------------------------------
 * vim:tabstop=4:shiftwidth=4
 * settings.h
 * Sun, 12 Sep 2004 18:06:08 +0700
 * -------------------------------
 * settings parser\container
 * -------------------------------*/

#ifndef _SETTINGS_H_
#define _SETTINGS_H_

#include <X11/X.h>
#include <X11/Xlib.h>

#include "config.h"
#include "layout.h"

/* Default name of configuration file */
#define STALONETRAY_RC "stalonetrayrc"

struct WindowClass {
    char *name;
    struct WindowClass *next;
    struct WindowClass *prev;
};

struct Settings {
    /* Flags */
    int parent_bg; /* Enable pseudo-transparency using parents' background*/
    int deco_flags; /* Decoration flags */
    int transparent; /* Enable root-transparency */
    int skip_taskbar; /* Remove tray from wm`s taskbar */
    int sticky; /* Make tray sticky across desktops/pages */
    int xsync; /* Operate on X server syncronously */
    int pixmap_bg; /* Is pixmap used for background */
    int min_space_policy; /* Use placement that cause minimal grow */
    int full_pmt_search; /* Use non-first-match search algorithm */
    int vertical; /* Use vertical icon layout */
    int shrink_back_mode; /* Keep tray's window size minimal */
    int dockapp_mode; /* Activate dockapp mode */
    int kludge_flags; /* What kludges to activate */
    int scroll_everywhere; /* Whether scrolling is limited to the scrollbars only */

    int need_help; /* Print usage and exit */

    /* Lists of strings */
    struct WindowClass *ignored_classes; /* List of window classes to ignore */

    /* Strings */
    char *display_str; /* Name of the display */
    char *bg_color_str; /* Background color name */
    char *scrollbars_highlight_color_str; /* Name of color to highlight
                                             scrollbars with. NULL means
                                             highlighting is disabled */
    char *geometry_str; /* Geometry spec */
    char *max_geometry_str; /* Geometry spec */
    char *config_fname; /* Path to the configuration file */
    char *wnd_type; /* Window type */
    char *wnd_layer; /* Window layer */
    char *wnd_name; /* Window name (WM_NAME) */
    char *bg_pmap_path; /* Background pixmap path */
    char *tint_color_str; /* Color used for tinting */
    char *remote_click_name; /* Icon name to execute remote click on */

    /* Values */
    int icon_size; /* Icon size */
    struct Point slot_size; /* Grid slot size */
    int grow_gravity; /* Icon gravity (interpretation of icon_gravity_str) */
    int icon_gravity; /* Grow gravity (interpretation of grow_gravity_str) */
    int win_gravity; /* Tray window gravity (computed using grow gravity) */
    int bit_gravity; /* Tray window bit gravity (computed using icon_gravity)
                      */
    int geom_gravity; /* Tray window gravity when mapping the window (computed
                         using geometry_str) */
    int fuzzy_edges; /* Level of edges fuzziness (0 = disabled) */
    int tint_level; /* Tinting level (0 = disabled) */
    int scrollbars_mode; /* SB_MODE_NONE | SB_MODE_VERT | SB_MODE_HORZ */
    int scrollbars_size; /* Size of scrollbar windows in pixels */
    int scrollbars_inc; /* Step of scrollbar */
    int wm_strut_mode; /* WM strut mode */
    struct Point max_tray_dims; /* Maximal tray dimensions */
    struct Point max_layout_dims; /* Maximal layout dimensions */
    struct Point orig_tray_dims; /* Original tray dimensions */
    struct Point remote_click_pos; /* Remote click position */
    int remote_click_btn; /* Remote click button */
    int remote_click_cnt; /* Remote click count */

    XColor tint_color; /* Color used for tinting */

#ifdef DELAY_EMBEDDING_CONFIRMATION
    int confirmation_delay;
#endif

    XColor bg_color; /* Tray background color */
    XColor scrollbars_highlight_color; /* Color to highlight scrollbars with */
    int log_level; /* Debug level */

#ifdef XINERAMA_SUPPORTED
    int monitor; /* Which monitor to display tray in */
#endif
};

extern struct Settings settings;

/* Read settings from cmd line and configuration file */
int read_settings(int argc, char **argv);
/* Interpret all settings that either need an
 * open display or are interpreted from other
 * settings */
void interpret_settings();

#endif
