/* -------------------------------
 * vim:tabstop=4:shiftwidth=4
 * settings.c
 * Sun, 12 Sep 2004 18:55:53 +0700
 * -------------------------------
 * settings parser\container
 * -------------------------------*/

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "common.h"
#include "debug.h"
#include "layout.h"
#include "list.h"
#include "settings.h"
#include "tray.h"

#include "wmh.h"
#include "xutils.h"

/* Here we keep our filthy settings */
struct Settings settings;

/* Parsers write fields into *parse_target; each Param's struct_offset is the
 * offsetof() into struct Settings for the field that param drives. This lets
 * settings_reload parse into a scratch struct without touching the live
 * `settings` until the merge step. */
static struct Settings *parse_target = &settings;

/* Resolve a parser's struct_offset into a typed lvalue inside parse_target. */
#define REF(type) ((type *) ((char *) parse_target + struct_offset))

/* strdup() that DIEs on OOM and passes NULL through unchanged. Used for every
 * string assigned into a settings field, so heap ownership is uniform across
 * the struct: every char* in struct Settings is either NULL or a heap pointer
 * owned by that struct (free_settings can release them blindly). */
static char *xstrdup(const char *s)
{
    char *r;
    if (s == NULL) return NULL;
    if ((r = strdup(s)) == NULL) DIE_OOM(("strdup() failed for \"%s\"\n", s));
    return r;
}

/* Replace a heap-owned string field with a strdup of `s`. Frees the old value
 * first; safe to call when *slot is NULL (e.g. immediately after memset). */
static void set_str(char **slot, const char *s)
{
    free(*slot);
    *slot = xstrdup(s);
}

/* Forward declaration; the body lives further down with the other parsers. */
void free_window_class_list(struct WindowClass *head);

void free_settings(struct Settings *s)
{
    free(s->bg_color_str);
    free(s->tint_color_str);
    free(s->scrollbars_highlight_color_str);
    free(s->display_str);
    free(s->geometry_str);
    free(s->max_geometry_str);
    free(s->wnd_type);
    free(s->wnd_layer);
    free(s->wnd_name);
    free(s->bg_pmap_path);
    free(s->config_fname);
    free(s->icon_order_file);
    free(s->remote_click_name);
    free_window_class_list(s->ignored_classes);
    memset(s, 0, sizeof(*s));
}

/* (Re-)populate *parse_target with default values. Safe to call repeatedly:
 * any existing heap content is freed first via free_settings(). */
void init_default_settings(void)
{
    free_settings(parse_target);

    set_str(&parse_target->bg_color_str, "gray");
    set_str(&parse_target->tint_color_str, "white");
    set_str(&parse_target->scrollbars_highlight_color_str, "white");
    set_str(&parse_target->max_geometry_str, "0x0");
    set_str(&parse_target->wnd_type, _NET_WM_WINDOW_TYPE_DOCK);
    set_str(&parse_target->wnd_name, PROGNAME);

#ifdef DEBUG
    parse_target->log_level = LOG_LEVEL_ERR;
#endif
    parse_target->icon_size = FALLBACK_ICON_SIZE;
    parse_target->slot_size.x = -1;
    parse_target->slot_size.y = -1;
    parse_target->deco_flags = DECO_NONE;
    parse_target->shrink_back_mode = 1;
    parse_target->sticky = 1;
    parse_target->skip_taskbar = 1;
    parse_target->grow_gravity = GRAV_N | GRAV_W;
    parse_target->icon_gravity = GRAV_N | GRAV_W;
    parse_target->full_pmt_search = 1;
    parse_target->scrollbars_size = -1;
    parse_target->scrollbars_mode = SB_MODE_NONE;
    parse_target->scrollbars_inc = -1;
    parse_target->wm_strut_mode = WM_STRUT_AUTO;
    parse_target->remote_click_btn = REMOTE_CLICK_BTN_DEFAULT;
    parse_target->remote_click_cnt = REMOTE_CLICK_CNT_DEFAULT;
    parse_target->remote_click_pos.x = REMOTE_CLICK_POS_DEFAULT;
    parse_target->remote_click_pos.y = REMOTE_CLICK_POS_DEFAULT;
    parse_target->drag_reorder = 1;
    parse_target->drag_modifier = Mod1Mask;
    parse_target->remember_icon_order = 1;
    parse_target->icon_order_timeout = 10;
#ifdef DELAY_EMBEDDING_CONFIRMATION
    parse_target->confirmation_delay = 3;
#endif
}

/* ******* general parsing utils ********* */

#define PARSING_ERROR(msg, str) \
    if (!silent) LOG_ERROR(("Parsing error: " msg ", \"%s\" found\n", str));

/* Parse highlight color */
int parse_scrollbars_highlight_color(int, const char **argv, size_t struct_offset, int)
{
    char **highlight_color = REF(char *);

    free(*highlight_color);
    *highlight_color = strcasecmp(argv[0], "disable") == 0
                         ? NULL
                         : xstrdup(argv[0]);
    return SUCCESS;
}

/* Parse log level */
int parse_log_level(int, const char **argv, size_t struct_offset, int silent)
{
    int *log_level = REF(int);

    if (!strcmp(argv[0], "err"))
        *log_level = LOG_LEVEL_ERR;
    else if (!strcmp(argv[0], "info"))
        *log_level = LOG_LEVEL_INFO;
    else if (!strcmp(argv[0], "trace"))
        *log_level = LOG_LEVEL_TRACE;
    else {
        PARSING_ERROR("err, info, or trace expected", argv[0]);
        return FAILURE;
    }
    return SUCCESS;
}

/* Parse list of ignored window classes */
int parse_ignored_classes(int argc, const char **argv, size_t struct_offset, int)
{
    struct WindowClass **classes = REF(struct WindowClass *);
    struct WindowClass *newclass;
    int i;

    for (i = 0; i < argc; i++) {
        newclass = malloc(sizeof(*newclass));
        if (newclass == NULL) DIE_OOM(("could not allocate WindowClass\n"));
        newclass->name = xstrdup(argv[i]);
        LIST_ADD_ITEM(*classes, newclass);
    }

    return SUCCESS;
}

/* Parse dockapp mode */
int parse_dockapp_mode(int, const char **argv, size_t struct_offset, int silent)
{
    int *dockapp_mode = REF(int);

    if (!strcmp(argv[0], "none"))
        *dockapp_mode = DOCKAPP_NONE;
    else if (!strcmp(argv[0], "simple"))
        *dockapp_mode = DOCKAPP_SIMPLE;
    else if (!strcmp(argv[0], "wmaker"))
        *dockapp_mode = DOCKAPP_WMAKER;
    else {
        PARSING_ERROR("none, simple, or wmaker expected", argv[0]);
        return FAILURE;
    }
    return SUCCESS;
}

/* Parse gravity string ORing resulting value
 * with current value of tgt */
int parse_gravity(int, const char **argv, size_t struct_offset, int silent)
{
    int *gravity = REF(int);
    const char *gravity_s = argv[0];
    int parsed = 0;

    if (strlen(gravity_s) > 2)
        goto fail;

    for (; *gravity_s; gravity_s++) {
        switch (tolower(*gravity_s)) {
            case 'n': parsed |= GRAV_N; break;
            case 's': parsed |= GRAV_S; break;
            case 'w': parsed |= GRAV_W; break;
            case 'e': parsed |= GRAV_E; break;
            default:
                goto fail;
        }
    }

    if ((parsed & GRAV_N && parsed & GRAV_S) || (parsed & GRAV_E && parsed & GRAV_W))
        goto fail;

    *gravity = parsed;

    return SUCCESS;

fail:
    PARSING_ERROR("gravity expected", gravity_s);
    return FAILURE;
}

/* Parse integer string storing resulting value in tgt */
int parse_int(int, const char **argv, size_t struct_offset, int silent)
{
    int *parsed = REF(int);
    char *invalid;

    *parsed = strtol(argv[0], &invalid, 0);

    if (*invalid != '\0') {
        PARSING_ERROR("integer expected", argv[0]);
        return FAILURE;
    }

    return SUCCESS;
}

/* Parse kludges mode */
int parse_kludges(int, const char **argv, size_t struct_offset, int silent)
{
    const char *token = strtok((char *) argv[0], ",");
    int *kludges = REF(int);

    for (; token != NULL; token = strtok(NULL, ",")) {
        if (!strcasecmp(token, "fix_window_pos"))
            *kludges |= KLUDGE_FIX_WND_POS;
        else if (!strcasecmp(token, "force_icons_size"))
            *kludges |= KLUDGE_FORCE_ICONS_SIZE;
        else if (!strcasecmp(token, "use_icons_hints"))
            *kludges |= KLUDGE_USE_ICONS_HINTS;
        else {
            PARSING_ERROR("kludge flag expected", token);
            return FAILURE;
        }
    }

    return SUCCESS;
}

/* Parse strut mode */
int parse_strut_mode(int, const char **argv, size_t struct_offset, int silent)
{
    int *strut_mode = REF(int);

    if (!strcasecmp(argv[0], "auto"))
        *strut_mode = WM_STRUT_AUTO;
    else if (!strcasecmp(argv[0], "top"))
        *strut_mode = WM_STRUT_TOP;
    else if (!strcasecmp(argv[0], "bottom"))
        *strut_mode = WM_STRUT_BOT;
    else if (!strcasecmp(argv[0], "left"))
        *strut_mode = WM_STRUT_LFT;
    else if (!strcasecmp(argv[0], "right"))
        *strut_mode = WM_STRUT_RHT;
    else if (!strcasecmp(argv[0], "none"))
        *strut_mode = WM_STRUT_NONE;
    else {
        PARSING_ERROR(
            "one of top, bottom, left, right, or auto expected", argv[0]);
        return FAILURE;
    }

    return SUCCESS;
}

/* Parse X11 modifier name (alt, shift, ctrl, super, none) into a mask. */
int parse_modifier(int, const char **argv, size_t struct_offset, int silent)
{
    unsigned int *mask = REF(unsigned int);
    const char *name = argv[0];

    if (!strcasecmp(name, "alt") || !strcasecmp(name, "mod1"))
        *mask = Mod1Mask;
    else if (!strcasecmp(name, "shift"))
        *mask = ShiftMask;
    else if (!strcasecmp(name, "ctrl") || !strcasecmp(name, "control"))
        *mask = ControlMask;
    else if (!strcasecmp(name, "super") || !strcasecmp(name, "mod4"))
        *mask = Mod4Mask;
    else if (!strcasecmp(name, "none"))
        *mask = 0;
    else {
        PARSING_ERROR("alt, shift, ctrl, super, or none expected", name);
        return FAILURE;
    }
    return SUCCESS;
}

/* Parse boolean string storing result in tgt*/
int parse_bool(int, const char **argv, size_t struct_offset, int silent)
{
    const char *true_str[] = {"yes", "on", "true", "1", NULL};
    const char *false_str[] = {"no", "off", "false", "0", NULL};
    int *boolean = REF(int);

    for (const char **s = true_str; *s; s++) {
        if (!strcasecmp(argv[0], *s)) {
            *boolean = True;
            return SUCCESS;
        }
    }

    for (const char **s = false_str; *s; s++) {
        if (!strcasecmp(argv[0], *s)) {
            *boolean = False;
            return SUCCESS;
        }
    }

    PARSING_ERROR("boolean expected", argv[0]);
    return FAILURE;
}

/* Backwards version of the boolean parser */
int parse_bool_rev(int argc, const char **argv, size_t struct_offset, int silent)
{
    int *boolean = REF(int);

    if (parse_bool(argc, argv, struct_offset, silent)) {
        *boolean = ! *boolean;
        return SUCCESS;
    }

    return FAILURE;
}

/* Parse window layer string storing result in tgt */
int parse_wnd_layer(int, const char **argv, size_t struct_offset, int silent)
{
    const char *layer;
    char **window_layer = REF(char *);

    if (!strcasecmp(argv[0], "top"))
        layer = _NET_WM_STATE_ABOVE;
    else if (!strcasecmp(argv[0], "bottom"))
        layer = _NET_WM_STATE_BELOW;
    else if (!strcasecmp(argv[0], "normal"))
        layer = NULL;
    else {
        PARSING_ERROR("window layer expected", argv[0]);
        return FAILURE;
    }

    free(*window_layer);
    *window_layer = xstrdup(layer);
    return SUCCESS;
}

/* Parse window type string storing result in tgt */
int parse_wnd_type(int, const char **argv, size_t struct_offset, int silent)
{
    const char *type;
    char **window_type = REF(char *);

    if (!strcasecmp(argv[0], "dock"))
        type = _NET_WM_WINDOW_TYPE_DOCK;
    else if (!strcasecmp(argv[0], "toolbar"))
        type = _NET_WM_WINDOW_TYPE_TOOLBAR;
    else if (!strcasecmp(argv[0], "utility"))
        type = _NET_WM_WINDOW_TYPE_UTILITY;
    else if (!strcasecmp(argv[0], "normal"))
        type = _NET_WM_WINDOW_TYPE_NORMAL;
    else if (!strcasecmp(argv[0], "desktop"))
        type = _NET_WM_WINDOW_TYPE_DESKTOP;
    else {
        PARSING_ERROR("window type expected", argv[0]);
        return FAILURE;
    }

    free(*window_type);
    *window_type = xstrdup(type);
    return SUCCESS;
}

/* Just copy string from arg to *tgt */
int parse_copystr(int, const char **argv, size_t struct_offset, int)
{
    char **stringref = REF(char *);
    free(*stringref);
    *stringref = xstrdup(argv[0]);
    return SUCCESS;
}

/* Parses window decoration specification */
int parse_deco(int, const char **argv, size_t struct_offset, int silent)
{
    int *decorations = REF(int);
    const char *arg = argv[0];

    if (!strcasecmp(arg, "none"))
        *decorations = DECO_NONE;
    else if (!strcasecmp(arg, "all"))
        *decorations = DECO_ALL;
    else if (!strcasecmp(arg, "border"))
        *decorations = DECO_BORDER;
    else if (!strcasecmp(arg, "title"))
        *decorations = DECO_TITLE;
    else {
        PARSING_ERROR("decoration specification expected", arg);
        return FAILURE;
    }
    return SUCCESS;
}

/* Parses window decoration specification */
int parse_sb_mode(int, const char **argv, size_t struct_offset, int silent)
{
    int *sb_mode = REF(int);

    if (!strcasecmp(argv[0], "none"))
        *sb_mode = 0;
    else if (!strcasecmp(argv[0], "vertical"))
        *sb_mode = SB_MODE_VERT;
    else if (!strcasecmp(argv[0], "horizontal"))
        *sb_mode = SB_MODE_HORZ;
    else if (!strcasecmp(argv[0], "all"))
        *sb_mode = SB_MODE_HORZ | SB_MODE_VERT;
    else {
        PARSING_ERROR("scrollbars specification expected", argv[0]);
        return FAILURE;
    }

    return SUCCESS;
}

int parse_remote_click_type(int, const char **argv, size_t struct_offset, int silent)
{
    int *remote_click_type = REF(int);

    if (!strcasecmp(argv[0], "single"))
        *remote_click_type = 1;
    else if (!strcasecmp(argv[0], "double"))
        *remote_click_type = 2;
    else {
        PARSING_ERROR("click type can be single or double", argv[0]);
        return FAILURE;
    }

    return SUCCESS;
}

int parse_pos(int, const char **argv, size_t struct_offset, int)
{
    struct Point *pos = REF(struct Point);
    unsigned int dummy;
    XParseGeometry(argv[0], &pos->x, &pos->y, &dummy, &dummy);
    return SUCCESS;
}

int parse_size(int, const char **argv, size_t struct_offset, int)
{
    struct Point *size = REF(struct Point);
    unsigned int width, height;
    int bitmask, dummy;

    bitmask = XParseGeometry(argv[0], &dummy, &dummy, &width, &height);

    if (bitmask == 0 || bitmask & ~(WidthValue | HeightValue))
        return FAILURE;
    if ((bitmask & HeightValue) == 0)
        height = width;

    size->x = (width > (unsigned int) INT_MAX) ? INT_MAX : (int) width;
    size->y = (height > (unsigned int) INT_MAX) ? INT_MAX : (int) height;

    return SUCCESS;
}

/************ CLI **************/

#define MAX_DEFAULT_ARGS 10

/* parameter parser function */
typedef int (*param_parser_t)(int argc, const char **argv, size_t struct_offset, int silent);

struct Param {
    const char *short_name; /* short command line parameter name */
    const char *long_name;  /* long command line parameter name */
    const char *rc_name;    /* parameter name for config file */

    /* Where the parser writes its result. Given as offsetof(struct Settings,
     * field) in each table entry below, and resolved against parse_target at
     * parse time via the REF() macro. */
    size_t struct_offset;

    const int pass; /* 0th pass parameters are parsed before rc file, */
                    /* 1st pass parameters are parsed after it */

    const int min_argc; /* minimum number of expected arguments */
    const int max_argc; /* maximum number of expected arguments, 0 for unlimited */

    const int default_argc;                     /* number of default arguments, if present */
    const char *default_argv[MAX_DEFAULT_ARGS]; /* default arguments if none are given */

    param_parser_t parser;  /* pointer to parsing function */

    /* Whether this parameter is safe to re-parse from the config file on
     * SIGHUP-driven reloads. parse_rc() skips entries with reloadable == 0
     * when it is being called for a reload, so that they aren't re-strdup'd
     * just to be thrown away. */
    const int reloadable;
};

struct Param params[] = {
    {.short_name = "-display",
        .long_name = NULL,
        .rc_name = "display",
        .struct_offset = offsetof(struct Settings, display_str),

        .pass = 1,

        .min_argc = 1,
        .max_argc = 1,

        .default_argc = 0,
        .default_argv = NULL,

        .parser = (param_parser_t)&parse_copystr},
    {.short_name = NULL,
        .long_name = "--log-level",
        .rc_name = "log_level",
        .struct_offset = offsetof(struct Settings, log_level),

        .pass = 1,

        .min_argc = 1,
        .max_argc = 1,

        .default_argc = 0,
        .default_argv = NULL,

        .parser = (param_parser_t)&parse_log_level,
        .reloadable = True},
    {.short_name = "-bg",
        .long_name = "--background",
        .rc_name = "background",
        .struct_offset = offsetof(struct Settings, bg_color_str),

        .pass = 1,

        .min_argc = 1,
        .max_argc = 1,

        .default_argc = 0,
        .default_argv = NULL,

        .parser = (param_parser_t)&parse_copystr,
        .reloadable = True},
    {.short_name = "-c",
        .long_name = "--config",
        .rc_name = NULL,
        .struct_offset = offsetof(struct Settings, config_fname),

        .pass = 0,

        .min_argc = 1,
        .max_argc = 1,

        .default_argc = 0,
        .default_argv = NULL,

        .parser = (param_parser_t)&parse_copystr},
    {.short_name = "-d",
        .long_name = "--decorations",
        .rc_name = "decorations",
        .struct_offset = offsetof(struct Settings, deco_flags),

        .pass = 1,

        .min_argc = 0,
        .max_argc = 1,

        .default_argc = 1,
        .default_argv = {"all"},

        .parser = (param_parser_t)&parse_deco,
        .reloadable = True},
    {.short_name = NULL,
        .long_name = "--dockapp-mode",
        .rc_name = "dockapp_mode",
        .struct_offset = offsetof(struct Settings, dockapp_mode),

        .pass = 1,

        .min_argc = 0,
        .max_argc = 1,

        .default_argc = 1,
        .default_argv = {"simple"},

        .parser = (param_parser_t)&parse_dockapp_mode},
    {.short_name = "-f",
        .long_name = "--fuzzy-edges",
        .rc_name = "fuzzy_edges",
        .struct_offset = offsetof(struct Settings, fuzzy_edges),

        .pass = 1,

        .min_argc = 0,
        .max_argc = 1,

        .default_argc = 1,
        .default_argv = {"2"},

        .parser = (param_parser_t)&parse_int,
        .reloadable = True},
    {.short_name = "-geometry",
        .long_name = "--geometry",
        .rc_name = "geometry",
        .struct_offset = offsetof(struct Settings, geometry_str),

        .pass = 1,

        .min_argc = 1,
        .max_argc = 1,

        .default_argc = 0,
        .default_argv = NULL,

        .parser = (param_parser_t)&parse_copystr},
    {.short_name = NULL,
        .long_name = "--grow-gravity",
        .rc_name = "grow_gravity",
        .struct_offset = offsetof(struct Settings, grow_gravity),

        .pass = 1,

        .min_argc = 1,
        .max_argc = 1,

        .default_argc = 0,
        .default_argv = NULL,

        .parser = (param_parser_t)&parse_gravity},
    {.short_name = NULL,
        .long_name = "--icon-gravity",
        .rc_name = "icon_gravity",
        .struct_offset = offsetof(struct Settings, icon_gravity),

        .pass = 1,

        .min_argc = 1,
        .max_argc = 1,

        .default_argc = 0,
        .default_argv = NULL,

        .parser = (param_parser_t)&parse_gravity},
    {.short_name = "-i",
        .long_name = "--icon-size",
        .rc_name = "icon_size",
        .struct_offset = offsetof(struct Settings, icon_size),

        .pass = 1,

        .min_argc = 1,
        .max_argc = 1,

        .default_argc = 0,
        .default_argv = NULL,

        .parser = (param_parser_t)&parse_int},
    {.short_name = "-h",
        .long_name = "--help",
        .rc_name = NULL,
        .struct_offset = offsetof(struct Settings, need_help),

        .pass = 0,

        .min_argc = 0,
        .max_argc = 0,

        .default_argc = 1,
        .default_argv = {"true"},

        .parser = (param_parser_t)&parse_bool},
    {.short_name = NULL,
        .long_name = "--kludges",
        .rc_name = "kludges",
        .struct_offset = offsetof(struct Settings, kludge_flags),

        .pass = 1,

        .min_argc = 1,
        .max_argc = 1,

        .default_argc = 0,
        .default_argv = NULL,

        .parser = (param_parser_t)&parse_kludges,
        .reloadable = True},
    {.short_name = NULL,
        .long_name = "--max-geometry",
        .rc_name = "max_geometry",
        .struct_offset = offsetof(struct Settings, max_geometry_str),

        .pass = 1,

        .min_argc = 1,
        .max_argc = 1,

        .default_argc = 0,
        .default_argv = NULL,

        .parser = (param_parser_t)&parse_copystr},
#ifdef _ST_WITH_XINERAMA
    {.short_name = "-m",
        .long_name = "--monitor",
        .rc_name = "monitor",
        .struct_offset = offsetof(struct Settings, monitor),

        .pass = 1,

        .min_argc = 1,
        .max_argc = 1,

        .default_argc = 0,
        .default_argv = NULL,

        .parser = (param_parser_t)&parse_int,
        .reloadable = True},
#endif
    {.short_name = NULL,
        .long_name = "--no-shrink",
        .rc_name = "no_shrink",
        .struct_offset = offsetof(struct Settings, shrink_back_mode),

        .pass = 1,

        .min_argc = 0,
        .max_argc = 1,

        .default_argc = 1,
        .default_argv = {"true"},

        .parser = (param_parser_t)&parse_bool_rev,
        .reloadable = True},
    {.short_name = "-p",
        .long_name = "--parent-bg",
        .rc_name = "parent_bg",
        .struct_offset = offsetof(struct Settings, parent_bg),

        .pass = 1,

        .min_argc = 0,
        .max_argc = 1,

        .default_argc = 1,
        .default_argv = {"true"},

        .parser = (param_parser_t)&parse_bool,
        .reloadable = True},
    {.short_name = "-r",
        .long_name = "--remote-click-icon",
        .rc_name = NULL,
        .struct_offset = offsetof(struct Settings, remote_click_name),

        .pass = 1,

        .min_argc = 1,
        .max_argc = 1,

        .default_argc = 0,
        .default_argv = NULL,

        .parser = (param_parser_t)&parse_copystr},
    {.short_name = NULL,
        .long_name = "--remote-click-button",
        .rc_name = NULL,
        .struct_offset = offsetof(struct Settings, remote_click_btn),

        .pass = 1,

        .min_argc = 1,
        .max_argc = 1,

        .default_argc = 0,
        .default_argv = NULL,

        .parser = (param_parser_t)&parse_int},
    {.short_name = NULL,
        .long_name = "--remote-click-position",
        .rc_name = NULL,
        .struct_offset = offsetof(struct Settings, remote_click_pos),

        .pass = 1,

        .min_argc = 1,
        .max_argc = 1,

        .default_argc = 0,
        .default_argv = NULL,

        .parser = (param_parser_t)&parse_pos},
    {.short_name = NULL,
        .long_name = "--remote-click-type",
        .rc_name = NULL,
        .struct_offset = offsetof(struct Settings, remote_click_cnt),

        .pass = 1,

        .min_argc = 1,
        .max_argc = 1,

        .default_argc = 0,
        .default_argv = NULL,

        .parser = (param_parser_t)&parse_remote_click_type},
    {.short_name = NULL,
        .long_name = "--scrollbars",
        .rc_name = "scrollbars",
        .struct_offset = offsetof(struct Settings, scrollbars_mode),

        .pass = 1,

        .min_argc = 1,
        .max_argc = 1,

        .default_argc = 0,
        .default_argv = NULL,

        .parser = (param_parser_t)&parse_sb_mode},
    {.short_name = NULL,
        .long_name = "--scrollbars-highlight",
        .rc_name = "scrollbars_highlight",
        .struct_offset =
            offsetof(struct Settings, scrollbars_highlight_color_str),

        .pass = 1,

        .min_argc = 1,
        .max_argc = 1,

        .default_argc = 0,
        .default_argv = NULL,

        .parser = (param_parser_t)&parse_scrollbars_highlight_color,
        .reloadable = True},
    {.short_name = NULL,
        .long_name = "--scrollbars-step",
        .rc_name = "scrollbars_step",
        .struct_offset = offsetof(struct Settings, scrollbars_inc),

        .pass = 1,

        .min_argc = 1,
        .max_argc = 1,

        .default_argc = 0,
        .default_argv = NULL,

        .parser = (param_parser_t)&parse_int},
    {.short_name = NULL,
        .long_name = "--scrollbars-size",
        .rc_name = "scrollbars_size",
        .struct_offset = offsetof(struct Settings, scrollbars_size),

        .pass = 1,

        .min_argc = 1,
        .max_argc = 1,

        .default_argc = 0,
        .default_argv = NULL,

        .parser = (param_parser_t)&parse_int},
    {.short_name = NULL,
        .long_name = "--scroll-everywhere",
        .rc_name = "scroll_everywhere",
        .struct_offset = offsetof(struct Settings, scroll_everywhere),

        .pass = 1,

        .min_argc = 0,
        .max_argc = 1,

        .default_argc = 1,
        .default_argv = {"true"},

        .parser = (param_parser_t)&parse_bool,
        .reloadable = True},
    {.short_name = NULL,
        .long_name = "--drag-reorder",
        .rc_name = "drag_reorder",
        .struct_offset = offsetof(struct Settings, drag_reorder),

        .pass = 1,

        .min_argc = 0,
        .max_argc = 1,

        .default_argc = 1,
        .default_argv = {"true"},

        .parser = (param_parser_t)&parse_bool},
    {.short_name = NULL,
        .long_name = "--drag-modifier",
        .rc_name = "drag_modifier",
        .struct_offset = offsetof(struct Settings, drag_modifier),

        .pass = 1,

        .min_argc = 1,
        .max_argc = 1,

        .default_argc = 0,
        .default_argv = NULL,

        .parser = (param_parser_t)&parse_modifier},
    {.short_name = NULL,
        .long_name = "--remember-icon-order",
        .rc_name = "remember_icon_order",
        .struct_offset = offsetof(struct Settings, remember_icon_order),

        .pass = 1,

        .min_argc = 0,
        .max_argc = 1,

        .default_argc = 1,
        .default_argv = {"true"},

        .parser = (param_parser_t)&parse_bool},
    {.short_name = NULL,
        .long_name = "--icon-order-file",
        .rc_name = "icon_order_file",
        .struct_offset = offsetof(struct Settings, icon_order_file),

        .pass = 1,

        .min_argc = 1,
        .max_argc = 1,

        .default_argc = 0,
        .default_argv = NULL,

        .parser = (param_parser_t)&parse_copystr},
    {.short_name = NULL,
        .long_name = "--icon-order-timeout",
        .rc_name = "icon_order_timeout",
        .struct_offset = offsetof(struct Settings, icon_order_timeout),

        .pass = 1,

        .min_argc = 1,
        .max_argc = 1,

        .default_argc = 0,
        .default_argv = NULL,

        .parser = (param_parser_t)&parse_int},
    {.short_name = NULL,
        .long_name = "--skip-taskbar",
        .rc_name = "skip_taskbar",
        .struct_offset = offsetof(struct Settings, skip_taskbar),

        .pass = 1,

        .min_argc = 0,
        .max_argc = 1,

        .default_argc = 1,
        .default_argv = {"true"},

        .parser = (param_parser_t)&parse_bool,
        .reloadable = True},
    {.short_name = "-s",
        .long_name = "--slot-size",
        .rc_name = "slot_size",
        .struct_offset = offsetof(struct Settings, slot_size),

        .pass = 1,

        .min_argc = 1,
        .max_argc = 1,

        .default_argc = 0,
        .default_argv = NULL,

        .parser = (param_parser_t)&parse_size},
    {.short_name = NULL,
        .long_name = "--sticky",
        .rc_name = "sticky",
        .struct_offset = offsetof(struct Settings, sticky),

        .pass = 1,

        .min_argc = 0,
        .max_argc = 1,

        .default_argc = 1,
        .default_argv = {"true"},

        .parser = (param_parser_t)&parse_bool,
        .reloadable = True},
    {.short_name = NULL,
        .long_name = "--tint-color",
        .rc_name = "tint_color",
        .struct_offset = offsetof(struct Settings, tint_color_str),

        .pass = 1,

        .min_argc = 1,
        .max_argc = 1,

        .default_argc = 0,
        .default_argv = NULL,

        .parser = (param_parser_t)&parse_copystr,
        .reloadable = True},
    {.short_name = NULL,
        .long_name = "--tint-level",
        .rc_name = "tint_level",
        .struct_offset = offsetof(struct Settings, tint_level),

        .pass = 1,

        .min_argc = 1,
        .max_argc = 1,

        .default_argc = 0,
        .default_argv = NULL,

        .parser = (param_parser_t)&parse_int,
        .reloadable = True},
    {.short_name = "-t",
        .long_name = "--transparent",
        .rc_name = "transparent",
        .struct_offset = offsetof(struct Settings, transparent),

        .pass = 1,

        .min_argc = 0,
        .max_argc = 1,

        .default_argc = 1,
        .default_argv = {"true"},

        .parser = (param_parser_t)&parse_bool,
        .reloadable = True},
    {.short_name = "-v",
        .long_name = "--vertical",
        .rc_name = "vertical",
        .struct_offset = offsetof(struct Settings, vertical),

        .pass = 1,

        .min_argc = 0,
        .max_argc = 1,

        .default_argc = 1,
        .default_argv = {"true"},

        .parser = (param_parser_t)&parse_bool},
    {.short_name = NULL,
        .long_name = "--window-layer",
        .rc_name = "window_layer",
        .struct_offset = offsetof(struct Settings, wnd_layer),

        .pass = 1,

        .min_argc = 1,
        .max_argc = 1,

        .default_argc = 0,
        .default_argv = NULL,

        .parser = (param_parser_t)&parse_wnd_layer,
        .reloadable = True},
    {.short_name = NULL,
        .long_name = "--window-name",
        .rc_name = "window_name",
        .struct_offset = offsetof(struct Settings, wnd_name),

        .pass = 1,

        .min_argc = 1,
        .max_argc = 1,

        .default_argc = 0,
        .default_argv = NULL,

        .parser = (param_parser_t)&parse_copystr,
        .reloadable = True},
    {.short_name = NULL,
        .long_name = "--window-strut",
        .rc_name = "window_strut",
        .struct_offset = offsetof(struct Settings, wm_strut_mode),

        .pass = 1,

        .min_argc = 1,
        .max_argc = 1,

        .default_argc = 0,
        .default_argv = NULL,

        .parser = (param_parser_t)&parse_strut_mode,
        .reloadable = True},
    {.short_name = NULL,
        .long_name = "--window-type",
        .rc_name = "window_type",
        .struct_offset = offsetof(struct Settings, wnd_type),

        .pass = 1,

        .min_argc = 1,
        .max_argc = 1,

        .default_argc = 0,
        .default_argv = NULL,

        .parser = (param_parser_t)&parse_wnd_type,
        .reloadable = True},
    {.short_name = NULL,
        .long_name = "--xsync",
        .rc_name = "xsync",
        .struct_offset = offsetof(struct Settings, xsync),

        .pass = 1,

        .min_argc = 0,
        .max_argc = 1,

        .default_argc = 1,
        .default_argv = {"true"},

        .parser = (param_parser_t)&parse_bool},
    {.short_name = NULL,
        .long_name = NULL,
        .rc_name = "ignore_classes",
        .struct_offset = offsetof(struct Settings, ignored_classes),

        .pass = 1,

        .min_argc = 1,
        .max_argc = 0,

        .default_argc = 0,
        .default_argv = NULL,

        .parser = (param_parser_t)&parse_ignored_classes,
        .reloadable = True},

#ifdef DELAY_EMBEDDING_CONFIRMATION
    {.short_name = NULL,
        .long_name = "--confirmation-delay",
        .rc_name = "confirmation_delay",
        .struct_offset = offsetof(struct Settings, confirmation_delay),

        .pass = 1,

        .min_argc = 1,
        .max_argc = 1,

        .default_argc = 0,
        .default_argv = NULL,

        .parser = (param_parser_t)&parse_int},
#endif

#ifdef _ST_WITH_XPM
    {.short_name = NULL,
        .long_name = "--pixmap-bg",
        .rc_name = "pixmap_bg",
        .struct_offset = offsetof(struct Settings, bg_pmap_path),

        .pass = 1,

        .min_argc = 1,
        .max_argc = 1,

        .default_argc = 0,
        .default_argv = NULL,

        .parser = (param_parser_t)&parse_copystr,
        .reloadable = True},
#endif
    /* Explicit sentinel: the loop `for (p = params; p->parser != NULL; ...)`
     * needs a terminator. Trailing zeroed memory used to do the trick, but
     * tools like AddressSanitizer surround globals with redzones and trip on
     * the read past the last entry. */
    {.parser = NULL},
};

void usage(char *progname)
{
    printf("\nstalonetray " VERSION " [ " FEATURE_LIST " ]\n");
    printf(
        "\nUsage: %s [options...]\n"
        "\n"
        "For short options argument can be specified as -o value or -ovalue.\n"
        "For long options argument can be specified as --option value or\n"
        "--option=value. All flag-options have expilicit optional boolean \n"
        "argument, which can be true (1, yes) or false (0, no).\n"
        "\n", progname);
    printf(
        "Possible options are:\n"
        "    -display <display>          use X display <display>\n"
        "    -bg, --background <color>   select background color (default: "
        "#777777)\n"
        "    -c, --config <filename>     read configuration from <file>\n"
        "                                (instead of default "
        "$HOME/.stalonetrayrc)\n"
        "    -d, --decorations <deco>    set what part of window decorations "
        "are\n"
        "                                visible; deco can be: none "
        "(default),\n"
        "                                title, border, all\n"
        "    --dockapp-mode [<mode>]     enable dockapp mode; mode can be "
        "none (default),\n"
        "                                simple (default if no mode "
        "specified), or wmaker\n"
        "    -f, --fuzzy-edges [<level>] set edges fuzziness level from\n"
        "                                0 (disabled) to 3 (maximum); works "
        "with\n"
        "                                tinting and/or pixmap background;\n"
        "                                if not specified, level defaults to "
        "2\n"
        "    [-]-geometry <geometry>     set initial tray`s geometry (width "
        "and height\n"
        "                                are defined in icon slots; offsets "
        "are defined\n"
        "                                in pixels)\n"
        "    --grow-gravity <gravity>    set tray`s grow gravity,\n"
        "                                either to N, S, W, E, NW, NE, SW, or "
        "SE\n"
        "    --icon-gravity <gravity>    icon positioning gravity (NW, NE, "
        "SW, SE)\n"
        "    -i, --icon-size <n>         set basic icon size to <n>; default "
        "is 24\n"
        "   --ignore-classes <classes>   ignore tray icons in xembed if the "
        "tray window has one of the given classes, separated by commas\n"
        "    -h, --help                  show this message\n"
        );
    printf(
#ifdef DEBUG
        "    --log-level <level>         set the level of output to either "
        "err\n"
        "                                (default), info, or trace\n"
#else
        "    --log-level <level>         set the level of output to either "
        "err\n"
        "                                (default), or info\n"
#endif
        "    --kludges <list>            enable specific kludges to work "
        "around\n"
        "                                non-conforming WMs and/or "
        "stalonetray bugs;\n"
        "                                argument is a comma-separated list "
        "of:\n"
        "                                 - fix_window_pos (fix window "
        "position),\n"
        "                                 - force_icons_size (ignore icon "
        "resizes),\n"
        "                                 - use_icons_hints (use icon size "
        "hints)\n"
        "    --max-geometry <geometry>   set tray maximal width and height; 0 "
        "indicates\n"
        "                                no limit in respective direction\n"
        "    -m, --monitor <monitor>     What monitor to display in\n"
        "    --no-shrink                 do not shrink window back after icon "
        "removal\n"
        "    -p, --parent-bg             use parent for background\n"
        "    --pixmap-bg <pixmap>        use pixmap for tray`s window "
        "background\n"
        "    -r, --remote-click-icon <name> remote control (assumes an "
        "instance of\n"
        "                                stalonetray is already an active "
        "tray on this\n"
        "                                screen); sends click to icon which "
        "window's \n"
        "                                name is <name>\n"
        "    --remote-click-button <n>   defines mouse button for "
        "--remote-click-icon\n"
        "    --remote-click-position <x>x<y> defines position for "
        "--remote-click-icon\n"
        "    --remote-click-type <type>  defines click type for "
        "--remote-click-icon;\n"
        "                                type can be either single, or "
        "double\n"
        "    --scrollbars <mode>         set scrollbar mode either to all, "
        "horizontal,\n"
        "                                vertical, or none\n"
        "    --scrollbars-highlight <mode> set scrollbar highlighting mode "
        "which can\n"
        "                                be either color spec (default color "
        "is red)\n"
        "                                or disable\n"
        "    --scrollbars-step <n>       set scrollbar step to n pixels\n"
        "    --scrollbars-size <n>       set scrollbar size to n pixels\n"
        "    --scroll-everywhere         enable scrolling outside of the scrollbars too\n"
        "    --drag-reorder [<bool>]     allow dragging icons to reorder them (default: true)\n"
        "    --drag-modifier <mod>       modifier that enables drag: alt (default),\n"
        "                                shift, ctrl, super, or none\n"
        "    --remember-icon-order [<bool>] persist icon order across tray restarts\n"
        "                                (default: true)\n"
        "    --icon-order-file <path>    path to the icon order state file\n"
        "                                (default: $XDG_STATE_HOME/stalonetray/order-<screen>)\n"
        "    --icon-order-timeout <n>    seconds to wait for icons to redock at\n"
        "                                startup before settling (default: 10)\n"
        "    --slot-size <w>[x<h>]       set icon slot size in pixels\n"
        "                                if omitted, hight is set equal to width\n"
        "    --skip-taskbar              hide tray`s window from the taskbar\n"
        "    --sticky                    make tray`s window sticky across "
        "multiple\n"
        "                                desktops/pages\n"
        "    -t, --transparent           enable root transparency\n"
        "    --tint-color <color>        tint tray background with color (not "
        "used\n"
        "                                with plain color background)\n"
        "    --tint-level <level>        set tinting level from 0 to 255\n"
        "    -v, --vertical              use vertical layout of icons "
        "(horizontal\n"
        "                                layout is used by default)\n"
        "    --window-layer <layer>      set tray`s window EWMH layer\n"
        "                                either to bottom, normal, or top\n"
        "    --window-strut <mode>       set window strut mode to either "
        "auto,\n"
        "                                left, right, top, or bottom\n"
        "    --window-type <type>        set tray`s window EWMH type to "
        "either\n"
        "                                normal, dock, toolbar, utility, "
        "desktop\n"
        "    --xsync                     operate on X server synchronously "
        "(SLOW)\n"
        "\n");
}

/* Parse command line parameters.
 *
 * Same self-tracking pattern as parse_rc: a static `reloading` flag flips on
 * after the first call so subsequent invocations skip non-reloadable params
 * and don't re-strdup values we'd never adopt. */
int parse_cmdline(int argc, char **argv, int pass)
{
    static int reloading = 0;
    struct Param *p, *match;
    char *progname = argv[0];
    const char **p_argv = NULL, *argbuf[MAX_DEFAULT_ARGS];
    int p_argc;

    while (--argc > 0) {
        argv++;
        match = NULL;
        for (p = params; p->parser != NULL; p++) {
            if (p->max_argc) {
                if (p->short_name != NULL && strstr(*argv, p->short_name) == *argv) {
                    if ((*argv)[strlen(p->short_name)] != '\0') { /* accept arguments in the form -a5 */
                        argbuf[0] = *argv + strlen(p->short_name);
                        p_argc = 1;
                        p_argv = argbuf;
                    } else if (argc > 1 && argv[1][0] != '-') { /* accept arguments in the form -a 5, do not accept values starting with '-' */
                        argbuf[0] = *(++argv);
                        p_argc = 1;
                        p_argv = argbuf;
                        argc--;
                    } else if (p->min_argc > 0) { /* argument is missing */
                        LOG_ERROR(("%s expects an argument\n", p->short_name));
                        break;
                    } else { /* argument is optional, use default value */
                        p_argc = p->default_argc;
                        p_argv = p->default_argv;
                    }
                } else if (p->long_name != NULL && strstr(*argv, p->long_name) == *argv) {
                    if ((*argv)[strlen(p->long_name)] == '=') {/* accept arguments in the form --abcd=5 */
                        argbuf[0] = *argv + strlen(p->long_name) + 1;
                        p_argc = 1;
                        p_argv = argbuf;
                    } else if ((*argv)[strlen(p->long_name)] == '\0') { /* accept arguments in the from --abcd 5 */
                        if (argc > 1 && argv[1][0] != '-') { /* arguments cannot start with the dash */
                            argbuf[0] = *(++argv);
                            p_argc = 1;
                            p_argv = argbuf;
                            argc--;
                        } else if (p->min_argc > 0) { /* argument is missing */
                            LOG_ERROR(("%s expects an argument\n", p->long_name));
                            break;
                        } else { /* argument is optional, use default value */
                            p_argc = p->default_argc;
                            p_argv = p->default_argv;
                        }
                    } else
                        continue; /* just in case when there can be both --abc and --abcd */
                } else
                    continue;
                match = p;
                break;
            } else if (strcmp(*argv, p->short_name) == 0 || strcmp(*argv, p->long_name) == 0) {
                match = p;
                p_argc = p->default_argc;
                p_argv = p->default_argv;
                break;
            }
        }

#define USAGE_AND_DIE() \
    do { \
        usage(progname); \
        DIE(("Could not parse command line\n")); \
    } while (0)

        if (match == NULL) USAGE_AND_DIE();
        if (match->pass != pass) continue;
        if (reloading && !match->reloadable) {
            LOG_TRACE(("cmdline: skipping non-reloadable param \"%s\"\n",
                match->long_name != NULL ? match->long_name
                                         : match->short_name));
            continue;
        }
        if (p_argv == NULL) DIE_IE(("Argument cannot be NULL!\n"));

        LOG_TRACE(("cmdline: pass %d, param \"%s\", args: [", pass, match->long_name != NULL ? match->long_name : match->short_name));

        for (int i = 0; i < p_argc - 1; i++)
            LOG_TRACE(("\"%s\", ", p_argv[i]));

        LOG_TRACE(("\"%s\"]\n", p_argv[p_argc - 1]));

        if (!match->parser(p_argc, p_argv, match->struct_offset, match->min_argc == 0)) {
            if (match->min_argc == 0) {
                assert(p_argv != match->default_argv);
                match->parser(match->default_argc, match->default_argv, match->struct_offset, False);
                argc++;
                argv--;
            } else
                USAGE_AND_DIE();
        }
    }

    if (settings.need_help) {
        usage(progname);
        exit(0);
    }

    /* From here on, subsequent calls behave as reloads. */
    reloading = 1;
    return SUCCESS;
}

/************ .stalonetrayrc ************/

/**************************************************************************************
 * <line> ::= [<whitespaces>] [(<arg> <whitespaces>)* <arg>] [<whitespaces>]
 *<comment> <arg> ::= "<arbitrary-text>"|<text-without-spaces-and-#> <comment>
 *::= # <arbitrary-text>
 **************************************************************************************/

/* Does exactly what its name says */
#define SKIP_SPACES(p) \
    { \
        for (; *p != 0 && isspace((int)*p); p++) \
            ; \
    }
/* Break the line in argc, argv pair */
int get_args(char *line, int *argc, char ***argv)
{
    int q_flag = 0;
    char *arg_start, *q_pos;
    *argc = 0;
    *argv = NULL;
    /* 1. Strip leading spaces */
    SKIP_SPACES(line);
    if (0 == *line) { /* meaningless line */
        return SUCCESS;
    }
    arg_start = line;
    /* 2. Strip comments */
    for (; 0 != *line; line++) {
        q_flag = ('"' == *line) ? !q_flag : q_flag;
        if ('#' == *line && !q_flag) {
            *line = 0;
            break;
        }
    }
    if (q_flag) { /* disbalance of quotes */
        LOG_ERROR(("Disbalance of quotes\n"));
        return FAILURE;
    }
    if (arg_start == line) { /* meaningless line */
        return SUCCESS;
    }
    line--;
    /* 3. Strip trailing spaces */
    for (; line != arg_start && isspace((int)*line); line--)
        ;
    if (arg_start == line) { /* meaningless line */
        return FAILURE;
    }
    *(line + 1) = 0; /* this _is_ really ok since isspace(0) != 0 */
    line = arg_start;
    /* 4. Extract arguments */
    do {
        (*argc)++;
        /* Add space to store one more argument */
        if (NULL == (*argv = realloc(*argv, *argc * sizeof(char *))))
            DIE_OOM(("Could not allocate memory to parse parameters\n"));
        if (*arg_start
            == '"') { /* 4.1. argument is quoted: find matching quote */
            arg_start++;
            (*argv)[*argc - 1] = arg_start;
            if (NULL == (q_pos = strchr(arg_start, '"'))) {
                free(*argv);
                DIE_IE(("Quotes balance calculation failed\n"));
                return FAILURE;
            }
            arg_start = q_pos;
        } else { /* 4.2. whitespace-separated argument: find fist whitespace */
            (*argv)[*argc - 1] = arg_start;
            for (; 0 != *arg_start && !isspace((int)*arg_start); arg_start++)
                ;
        }
        if (*arg_start != 0) {
            *arg_start = 0;
            arg_start++;
            SKIP_SPACES(arg_start);
        }
    } while (*arg_start != 0);
    return SUCCESS;
}

/* Locate a config file by joining the given path components and stat()ing
 * the result. Returns a freshly-allocated path on success, NULL otherwise --
 * the heap allocation keeps settings.config_fname uniformly heap-owned
 * regardless of how it was populated. */
char *find_rc(const char *path_part1, const char *path_part2, const char *rc)
{
    char buf[PATH_MAX];
    int len;
    struct stat statbuf;

    if (path_part1 == NULL) return NULL;
    if (path_part2 == NULL) {
        LOG_TRACE(("looking for config file in '%s/%s'\n", path_part1, rc));
        len = snprintf(buf, sizeof(buf), "%s/%s", path_part1, rc);
    } else {
        LOG_TRACE(("looking for config file in '%s/%s/%s'\n", path_part1,
            path_part2, rc));
        len = snprintf(buf, sizeof(buf), "%s/%s/%s", path_part1, path_part2, rc);
    }

    if (len < 0 || (size_t) len >= sizeof(buf)) return NULL;
    if (stat(buf, &statbuf) != 0) return NULL;

    return xstrdup(buf);
}

#define READ_BUF_SZ 512

/* Reports a parse error for the current rc-file line. On the first parse_rc
 * call (startup load) it DIEs so misconfigured files surface loudly; on a
 * subsequent call (a SIGHUP-driven reload) it only logs so a typo in a live
 * edit can't take down the running tray. The caller decides what comes
 * after a logged error -- usually setting rc = FAILURE and letting the
 * surrounding parse loop unwind. */
#define PARSE_ERROR(reloading, lnum, fmt, ...) \
    do { \
        if (reloading) { \
            LOG_ERROR(("reload: %s:%d: " fmt, settings.config_fname, lnum, \
                ##__VA_ARGS__)); \
        } else { \
            DIE(("Configuration file parse error at %s:%d: " fmt, \
                settings.config_fname, lnum, ##__VA_ARGS__)); \
        } \
    } while (0)

/* Parses a single rc-file line that has already been read into `buf`.
 * Owns the lifetime of the argv that get_args() allocates. Returns SUCCESS
 * for a valid directive (including one silently skipped on a reload because
 * its param is non-reloadable), an empty line, or a comment-only line.
 * Returns FAILURE only in reload mode after PARSE_ERROR has logged. */
static int parse_rc_line(char *buf, int lnum, int reloading)
{
    int argc, rc = SUCCESS;
    char **argv = NULL;
    struct Param *p, *match = NULL;

    if (!get_args(buf, &argc, &argv)) {
        PARSE_ERROR(reloading, lnum, "could not parse line\n");
        rc = FAILURE;
    } else if (argc > 0) {
        for (p = params; p->parser != NULL; p++)
            if (p->rc_name != NULL && strcmp(argv[0], p->rc_name) == 0) {
                match = p;
                break;
            }

        if (match == NULL) {
            PARSE_ERROR(reloading, lnum,
                "unrecognized rc file keyword \"%s\".\n", argv[0]);
            rc = FAILURE;
        } else if (argc - 1 < match->min_argc
            || (match->max_argc && argc - 1 > match->max_argc)) {
            PARSE_ERROR(reloading, lnum,
                "invalid number of args for \"%s\" (%d-%d required)\n",
                match->rc_name, match->min_argc, match->max_argc);
            rc = FAILURE;
        } else if (reloading && !match->reloadable) {
            LOG_TRACE(("rc: skipping non-reloadable param \"%s\"\n",
                match->rc_name));
        } else {
            int p_argc = argc - 1;
            const char **p_argv = (const char **) argv + 1;
            if (p_argc == 0) {
                p_argc = match->default_argc;
                p_argv = match->default_argv;
            }

            LOG_TRACE(("rc: param \"%s\", args [\"", match->rc_name));
            for (int i = 0; i < p_argc - 1; i++)
                LOG_TRACE(("\"%s\", ", p_argv[i]));
            LOG_TRACE(("\"%s\"]\n", p_argv[p_argc - 1]));

            if (!match->parser(
                    p_argc, p_argv, match->struct_offset, False)) {
                PARSE_ERROR(reloading, lnum,
                    "could not parse argument for \"%s\".\n", argv[0]);
                rc = FAILURE;
            }
        }
    }

    free(argv);
    return rc;
}

/* Parses the active rc file into *parse_target.
 *
 * Tracks whether this is the first call ("loading") or a subsequent one
 * ("reloading") via a static flag so the function can apply slightly
 * different behavior on a SIGHUP-driven reload without leaking that detail
 * into the public API. On reloads we (a) skip params with reloadable == 0
 * inside parse_rc_line(), and (b) downgrade fatal errors to a logged
 * FAILURE so a malformed live edit doesn't take down the tray. The first
 * load preserves the original strict behavior and DIEs on errors. */
int parse_rc(void)
{
    static int reloading = 0;
    FILE *cfg = NULL;
    int rc = SUCCESS;
    int lnum = 0;
    char buf[READ_BUF_SZ + 1];

    if (settings.config_fname == NULL)
        settings.config_fname =
            find_rc(getenv("HOME"), NULL, "." STALONETRAY_RC);
    if (settings.config_fname == NULL)
        settings.config_fname =
            find_rc(getenv("XDG_CONFIG_HOME"), NULL, STALONETRAY_RC);
    if (settings.config_fname == NULL)
        settings.config_fname =
            find_rc(getenv("HOME"), ".config", STALONETRAY_RC);

    if (settings.config_fname == NULL) {
        LOG_INFO(("could not find any config files.\n"));
        /* On reload, treat a vanished config as a soft failure so the caller
         * keeps the live settings instead of falling back to defaults. */
        if (reloading) rc = FAILURE;
    } else if ((cfg = fopen(settings.config_fname, "r")) == NULL) {
        LOG_ERROR(("could not open %s (%s)\n", settings.config_fname,
            strerror(errno)));
        if (reloading) rc = FAILURE;
    } else {
        LOG_INFO(("using config file \"%s\"\n", settings.config_fname));
        buf[READ_BUF_SZ] = 0;
        while (rc == SUCCESS && !feof(cfg)) {
            lnum++;
            if (fgets(buf, READ_BUF_SZ, cfg) == NULL) {
                if (ferror(cfg))
                    LOG_ERROR(("read error (%s)\n", strerror(errno)));
                break;
            }
            rc = parse_rc_line(buf, lnum, reloading);
        }
        fclose(cfg);
    }

    /* From here on, the function behaves as a reload. */
    reloading = 1;
    return rc;
}

/* Interpret all settings that need an open display or other settings */
void interpret_settings(void)
{
    static int gravity_matrix[11] = {ForgetGravity, EastGravity, WestGravity,
        ForgetGravity, SouthGravity, SouthEastGravity, SouthWestGravity,
        ForgetGravity, NorthGravity, NorthEastGravity, NorthWestGravity};
    int geom_flags;
    int dummy;
    XWindowAttributes root_wa;
    /* Sanitize icon size */
    val_range(settings.icon_size, MIN_ICON_SIZE, INT_MAX);
    if (settings.slot_size.x < settings.icon_size)
        settings.slot_size.x = settings.icon_size;
    if (settings.slot_size.y < settings.icon_size)
        settings.slot_size.y = settings.icon_size;
    /* Sanitize scrollbar settings */
    if (settings.scrollbars_mode != SB_MODE_NONE) {
        val_range(settings.scrollbars_inc, settings.slot_size.x / 2, INT_MAX);
        if (settings.scrollbars_size < 0)
            settings.scrollbars_size = settings.slot_size.x / 4;
    }
    /* Sanitize all gravity strings */
    settings.icon_gravity |= ((settings.icon_gravity & GRAV_V) ? 0 : GRAV_N);
    settings.icon_gravity |= ((settings.icon_gravity & GRAV_H) ? 0 : GRAV_W);
    settings.win_gravity = gravity_matrix[settings.grow_gravity];
    settings.bit_gravity = gravity_matrix[settings.icon_gravity];
    /* Parse all background-related settings */
#ifdef _ST_WITH_XPM
    settings.pixmap_bg = (settings.bg_pmap_path != NULL);
#endif
    if (settings.pixmap_bg) {
        settings.parent_bg = False;
        settings.transparent = False;
    }
    if (settings.transparent) settings.parent_bg = False;
    /* Parse color-related settings */
    if (!x11_parse_color(
            tray_data.dpy, settings.bg_color_str, &settings.bg_color))
        DIE(("Could not parse background color \"%s\"\n",
            settings.bg_color_str));
    if (settings.scrollbars_highlight_color_str != NULL) {
        if (!x11_parse_color(tray_data.dpy,
                settings.scrollbars_highlight_color_str,
                &settings.scrollbars_highlight_color)) {
            DIE(("Could not parse scrollbars highlight color \"%s\"\n",
                settings.bg_color_str));
        }
    }
    /* Sanitize tint level value */
    val_range(settings.tint_level, 0, 255);
    if (settings.tint_level) {
        /* Parse tint color */
        if (!x11_parse_color(
                tray_data.dpy, settings.tint_color_str, &settings.tint_color))
            DIE(("Could not parse tint color \"%s\"\n",
                settings.tint_color_str));
    }
    /* Sanitize edges fuzziness */
    val_range(settings.fuzzy_edges, 0, 3);
    /* Get dimensions of root window */
    if (!XGetWindowAttributes(
            tray_data.dpy, DefaultRootWindow(tray_data.dpy), &root_wa))
        DIE(("Could not get root window dimensions.\n"));
    tray_data.root_wnd.x = 0;
    tray_data.root_wnd.y = 0;
    tray_data.root_wnd.width = root_wa.width;
    tray_data.root_wnd.height = root_wa.height;
    /* Parse geometry */
#define DEFAULT_GEOMETRY "1x1+0+0"
    tray_data.xsh.flags = PResizeInc | PBaseSize;
    tray_data.xsh.x = 0;
    tray_data.xsh.y = 0;
    tray_data.xsh.width_inc = settings.slot_size.x;
    tray_data.xsh.height_inc = settings.slot_size.y;
    tray_data.xsh.base_width = 0;
    tray_data.xsh.base_height = 0;
    tray_calc_window_size(
        0, 0, &tray_data.xsh.base_width, &tray_data.xsh.base_height);
    geom_flags = XWMGeometry(tray_data.dpy, DefaultScreen(tray_data.dpy),
        settings.geometry_str, DEFAULT_GEOMETRY, 0, &tray_data.xsh,
        &tray_data.xsh.x, &tray_data.xsh.y, &tray_data.xsh.width,
        &tray_data.xsh.height, &tray_data.xsh.win_gravity);
    tray_data.xsh.win_gravity = settings.win_gravity;
    tray_data.xsh.min_width = tray_data.xsh.width;
    tray_data.xsh.min_height = tray_data.xsh.height;
    tray_data.xsh.max_width = tray_data.xsh.width;
    tray_data.xsh.min_height = tray_data.xsh.height;
    tray_data.xsh.flags =
        PResizeInc | PBaseSize | PMinSize | PMaxSize | PWinGravity;
    tray_calc_tray_area_size(tray_data.xsh.width, tray_data.xsh.height,
        &settings.orig_tray_dims.x, &settings.orig_tray_dims.y);
    /* Dockapp mode */
    if (settings.dockapp_mode == DOCKAPP_WMAKER)
        tray_data.xsh.flags |= USPosition;
    else {
        if (geom_flags & (XValue | YValue))
            tray_data.xsh.flags |= USPosition;
        else
            tray_data.xsh.flags |= PPosition;
        if (geom_flags & (WidthValue | HeightValue))
            tray_data.xsh.flags |= USSize;
        else
            tray_data.xsh.flags |= PSize;
    }
    LOG_TRACE(("final geometry: %dx%d at (%d,%d)\n", tray_data.xsh.width,
        tray_data.xsh.height, tray_data.xsh.x, tray_data.xsh.y));
    if ((geom_flags & XNegative) && (geom_flags & YNegative))
        settings.geom_gravity = SouthEastGravity;
    else if (geom_flags & YNegative)
        settings.geom_gravity = SouthWestGravity;
    else if (geom_flags & XNegative)
        settings.geom_gravity = NorthEastGravity;
    else
        settings.geom_gravity = NorthWestGravity;
    /* Set tray maximal width/height */
    geom_flags = XParseGeometry(settings.max_geometry_str, &dummy, &dummy,
        (unsigned int *)&settings.max_tray_dims.x,
        (unsigned int *)&settings.max_tray_dims.y);
    LOG_TRACE(("max geometry from max_geometry_str: %dx%d\n",
        settings.max_tray_dims.x, settings.max_tray_dims.y));
    if (!settings.max_tray_dims.x)
        settings.max_tray_dims.x = root_wa.width;
    else {
        settings.max_tray_dims.x *= settings.slot_size.x;
        val_range(
            settings.max_tray_dims.x, settings.orig_tray_dims.x, INT_MAX);
    }
    if (!settings.max_tray_dims.y)
        settings.max_tray_dims.y = root_wa.height;
    else {
        settings.max_tray_dims.y *= settings.slot_size.y;
        val_range(
            settings.max_tray_dims.y, settings.orig_tray_dims.y, INT_MAX);
    }
    LOG_TRACE(("max geometry after normalization: %dx%d\n",
        settings.max_tray_dims.x, settings.max_tray_dims.y));
    /* XXX: this assumes certain degree of symmetry and in some point
     * in the future this may not be the case... */
    tray_calc_window_size(0, 0, &tray_data.scrollbars_data.scroll_base.x,
        &tray_data.scrollbars_data.scroll_base.y);
    tray_data.scrollbars_data.scroll_base.x /= 2;
    tray_data.scrollbars_data.scroll_base.y /= 2;
}

void free_window_class_list(struct WindowClass *head)
{
    struct WindowClass *c, *next;
    for (c = head; c != NULL; c = next) {
        next = c->next;
        free(c->name);
        free(c);
    }
}

int settings_reload(int argc, char **argv, struct Settings *out_old)
{
    /* Parse a fresh set of values into a scratch struct without touching the
     * live settings yet. parse_target redirects every parser's writes here. */
    struct Settings tmp = {0};
    XColor tmp_color;
    int ok;

    parse_target = &tmp;
    init_default_settings();
    parse_cmdline(argc, argv, 0);
    ok = parse_rc();
    parse_target = &settings;
    if (ok != SUCCESS) {
        /* Syntax error / missing config / etc. tmp may be partially
         * populated; free everything in it and leave the live settings
         * untouched. */
        free_settings(&tmp);
        memset(out_old, 0, sizeof(*out_old));
        return FAILURE;
    }
    parse_target = &tmp;
    parse_cmdline(argc, argv, 1);
    parse_target = &settings;

    /* Snapshot live settings (shallow). out_old now aliases every live heap
     * pointer; the transfers below carefully drop the aliases for fields we
     * leave alone so the caller can free_settings(out_old) blindly. */
    *out_old = settings;

    /* Heap fields that are not reloadable stay in live `settings`; clear the
     * aliases from out_old so free_settings(out_old) doesn't free strings
     * still owned by `settings`. */
    out_old->display_str = NULL;
    out_old->geometry_str = NULL;
    out_old->max_geometry_str = NULL;
    out_old->config_fname = NULL;
    out_old->icon_order_file = NULL;
    out_old->remote_click_name = NULL;

    /* For reloadable fields, move the freshly-parsed value from tmp into
     * live settings. The pre-reload value (still aliased by out_old after
     * the shallow snapshot above) is no longer referenced by `settings`, so
     * the caller's free_settings(out_old) is what releases it. */
#define ADOPT(f) \
    do { \
        settings.f = tmp.f; \
        tmp.f = (__typeof__(tmp.f)) 0; \
    } while (0)
    ADOPT(log_level);
    ADOPT(min_space_policy);
    ADOPT(full_pmt_search);
    ADOPT(shrink_back_mode);
    ADOPT(kludge_flags);
    ADOPT(scroll_everywhere);
#ifdef _ST_WITH_XINERAMA
    ADOPT(monitor);
#endif
    ADOPT(wm_strut_mode);
    ADOPT(sticky);
    ADOPT(skip_taskbar);
    ADOPT(deco_flags);
    ADOPT(fuzzy_edges);
    ADOPT(tint_level);
    ADOPT(transparent);
    ADOPT(parent_bg);
    ADOPT(bg_pmap_path);
    ADOPT(bg_color_str);
    ADOPT(tint_color_str);
    ADOPT(scrollbars_highlight_color_str);
    ADOPT(wnd_name);
    ADOPT(wnd_layer);
    ADOPT(wnd_type);
    ADOPT(ignored_classes);
#undef ADOPT

    /* Discard non-reloadable values tmp accumulated (e.g. defaults assigned
     * by init_default_settings or cmdline overrides parse_cmdline let
     * through before the reloading flag flipped on the first call). */
    free_settings(&tmp);

    /* Mirror interpret_settings()'s derivations for the fields we just
     * adopted, so the apply step downstream sees a coherent state. */
    val_range(settings.tint_level, 0, 255);
    val_range(settings.fuzzy_edges, 0, 3);
#ifdef _ST_WITH_XPM
    settings.pixmap_bg = (settings.bg_pmap_path != NULL);
#endif
    if (settings.pixmap_bg) {
        settings.parent_bg = False;
        settings.transparent = False;
    }
    if (settings.transparent) settings.parent_bg = False;

    /* Re-parse colors into a temporary first; on parse failure leave the
     * old XColor in place so the tray doesn't end up with a garbage pixel.
     * (Skip silently if no live display is available, e.g. inside tests.) */
    if (tray_data.dpy != NULL) {
        if (x11_parse_color(tray_data.dpy, settings.bg_color_str, &tmp_color))
            settings.bg_color = tmp_color;
        else
            LOG_ERROR(("reload: could not parse background color \"%s\"\n",
                settings.bg_color_str));
        if (settings.tint_level
            && x11_parse_color(
                tray_data.dpy, settings.tint_color_str, &tmp_color))
            settings.tint_color = tmp_color;
        if (settings.scrollbars_highlight_color_str != NULL
            && x11_parse_color(tray_data.dpy,
                settings.scrollbars_highlight_color_str, &tmp_color))
            settings.scrollbars_highlight_color = tmp_color;
    }

    return SUCCESS;
}

/************** "main" ***********/
int read_settings(int argc, char **argv)
{
    init_default_settings();
    /* Parse 0th pass command line args */
    parse_cmdline(argc, argv, 0);
    /* Parse configuration files */
    parse_rc();
    /* Parse 1st pass command line args */
    parse_cmdline(argc, argv, 1);
    /* Display some settings */
    LOG_TRACE(("stalonetray " VERSION " [ " FEATURE_LIST " ]\n"));
    LOG_TRACE(("bg_color_str = \"%s\"\n", settings.bg_color_str));
    LOG_TRACE(("config_fname = \"%s\"\n", settings.config_fname));
    LOG_TRACE(("deco_flags = 0x%x\n", settings.deco_flags));
    LOG_TRACE(("display_str = \"%s\"\n", settings.display_str));
    LOG_TRACE(("dockapp_mode = %d\n", settings.dockapp_mode));
    LOG_TRACE(("full_pmt_search = %d\n", settings.full_pmt_search));
    LOG_TRACE(("geometry_str = \"%s\"\n", settings.geometry_str));
    LOG_TRACE(("grow_gravity = 0x%x\n", settings.grow_gravity));
    LOG_TRACE(("icon_gravity = 0x%x\n", settings.icon_gravity));
    LOG_TRACE(("icon_size = %d\n", settings.icon_size));
    LOG_TRACE(("log_level = %d\n", settings.log_level));
    LOG_TRACE(("max_tray_dims.x = %d\n", settings.max_tray_dims.x));
    LOG_TRACE(("max_tray_dims.y = %d\n", settings.max_tray_dims.y));
    LOG_TRACE(("min_space_policy = %d\n", settings.min_space_policy));
    LOG_TRACE(("need_help = %d\n", settings.need_help));
    LOG_TRACE(("parent_bg = %d\n", settings.parent_bg));
    LOG_TRACE(("scrollbars_highlight_color_str = \"%s\"\n",
        settings.scrollbars_highlight_color_str));
    LOG_TRACE(("scrollbars_highlight_color.pixel = %ld\n",
        settings.scrollbars_highlight_color.pixel));
    LOG_TRACE(("scrollbars_inc = %d\n", settings.scrollbars_inc));
    LOG_TRACE(("scrollbars_mode = %d\n", settings.scrollbars_mode));
    LOG_TRACE(("scrollbars_size = %d\n", settings.scrollbars_size));
    LOG_TRACE(("shrink_back_mode = %d\n", settings.shrink_back_mode));
    LOG_TRACE(("slot_size.x = %d\n", settings.slot_size.x));
    LOG_TRACE(("slot_size.y = %d\n", settings.slot_size.y));
    LOG_TRACE(("vertical = %d\n", settings.vertical));
    LOG_TRACE(("xsync = %d\n", settings.xsync));
    return SUCCESS;
}
