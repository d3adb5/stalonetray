/* -------------------------------
 * vim:tabstop=4:shiftwidth=4
 * order.c
 * -------------------------------
 * Persist icon order across tray restarts. See order.h for the model.
 * -------------------------------*/

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "common.h"
#include "debug.h"
#include "icons.h"
#include "layout.h"
#include "order.h"
#include "settings.h"
#include "tray.h"
#include "xutils.h"

#define SESSION_PROP "_STALONETRAY_SESSION_ID"
#define LINE_BUF_SZ 1024

struct OrderEntry {
    Window wid; /* the window id seen at save time; informational only --
                 * apps recreate their dock window across a tray restart, so
                 * matching is done on WM_CLASS, not this */
    char *res_class;
    char *res_name;
    unsigned long pid; /* _NET_WM_PID, or 0 if it was unset; disambiguates
                        * several icons that share a class/name */
    int claimed;
    Window claimed_wid; /* live wid of the icon that claimed this entry */
};

static Atom xa_net_wm_pid = None;

static struct {
    struct OrderEntry *entries;
    int n_entries;
    int n_reclaimed;
    int startup_active;
    time_t startup_start;
    unsigned long session_token;
    char path[PATH_MAX];
    char dir[PATH_MAX];
} order;

/* ---- small helpers ---- */

/* Treat NULL and "" alike when comparing class/name fields. */
static int str_eq(const char *a, const char *b)
{
    if (a == NULL) a = "";
    if (b == NULL) b = "";
    return strcmp(a, b) == 0;
}

/* Fetch a live window's WM_CLASS. Either out param may come back NULL. */
static int order_get_class(Window wid, char **res_class, char **res_name)
{
    XClassHint hint = {NULL, NULL};
    *res_class = NULL;
    *res_name = NULL;
    if (!XGetClassHint(tray_data.dpy, wid, &hint)) {
        x11_ok();
        return 0;
    }
    if (hint.res_class != NULL) *res_class = strdup(hint.res_class);
    if (hint.res_name != NULL) *res_name = strdup(hint.res_name);
    if (hint.res_class != NULL) XFree(hint.res_class);
    if (hint.res_name != NULL) XFree(hint.res_name);
    return 1;
}

/* Read _NET_WM_PID off a live window, or 0 if unset. */
static unsigned long order_get_pid(Window wid)
{
    unsigned char *data = NULL;
    unsigned long n = 0, pid = 0;
    if (xa_net_wm_pid == None) return 0;
    if (x11_get_window_prop32(tray_data.dpy, wid, xa_net_wm_pid, XA_CARDINAL,
            &data, &n)
        && n >= 1 && data != NULL)
        pid = (unsigned long) (((long *) data)[0]);
    if (data != NULL) XFree(data);
    return pid;
}

/* mkdir -p for order.dir, best effort. */
static void order_make_dir(void)
{
    char buf[PATH_MAX];
    size_t i, len;
    len = strlen(order.dir);
    if (len == 0 || len >= sizeof(buf)) return;
    memcpy(buf, order.dir, len + 1);
    for (i = 1; i < len; i++) {
        if (buf[i] != '/') continue;
        buf[i] = '\0';
        mkdir(buf, 0700);
        buf[i] = '/';
    }
    mkdir(buf, 0700);
}

/* ---- session token ---- */

static unsigned long order_generate_token(void)
{
    unsigned long t = 0;
    FILE *f = fopen("/dev/urandom", "rb");
    if (f != NULL) {
        if (fread(&t, sizeof(t), 1, f) != 1) t = 0;
        fclose(f);
    }
    if (t == 0) t = (unsigned long) time(NULL) ^ ((unsigned long) getpid() << 16);
    return t & 0xFFFFFFFFUL;
}

/* Read the per-server session token from the root window, creating it if this
 * is a fresh X server. A new token means the saved snapshot is from a previous
 * session and must be discarded. */
static unsigned long order_session_token(void)
{
    Window root = DefaultRootWindow(tray_data.dpy);
    Atom prop = XInternAtom(tray_data.dpy, SESSION_PROP, False);
    unsigned char *data = NULL;
    unsigned long n = 0, token;
    long val;
    if (x11_get_window_prop32(tray_data.dpy, root, prop, XA_CARDINAL, &data, &n)
        && n >= 1 && data != NULL) {
        token = ((unsigned long) (((long *) data)[0])) & 0xFFFFFFFFUL;
        XFree(data);
        LOG_TRACE(("found existing session token %08lx\n", token));
        return token;
    }
    if (data != NULL) XFree(data);
    token = order_generate_token();
    val = (long) token;
    XChangeProperty(tray_data.dpy, root, prop, XA_CARDINAL, 32, PropModeReplace,
        (unsigned char *) &val, 1);
    x11_ok();
    LOG_TRACE(("established new session token %08lx\n", token));
    return token;
}

/* ---- snapshot storage ---- */

static void order_clear_snapshot(void)
{
    int i;
    for (i = 0; i < order.n_entries; i++) {
        free(order.entries[i].res_class);
        free(order.entries[i].res_name);
    }
    free(order.entries);
    order.entries = NULL;
    order.n_entries = 0;
}

static void order_add_entry(
    Window wid, const char *res_class, const char *res_name, unsigned long pid)
{
    struct OrderEntry *grown =
        realloc(order.entries, (order.n_entries + 1) * sizeof(*grown));
    if (grown == NULL) {
        LOG_ERR_OOM(("could not grow icon order snapshot\n"));
        return;
    }
    order.entries = grown;
    order.entries[order.n_entries].wid = wid;
    order.entries[order.n_entries].res_class = strdup(res_class != NULL ? res_class : "");
    order.entries[order.n_entries].res_name = strdup(res_name != NULL ? res_name : "");
    order.entries[order.n_entries].pid = pid;
    order.entries[order.n_entries].claimed = 0;
    order.entries[order.n_entries].claimed_wid = None;
    order.n_entries++;
}

/* ---- path resolution ---- */

static int order_resolve_path(void)
{
    const char *base, *home;
    if (settings.icon_order_file != NULL) {
        /* Explicit file: derive its directory for mkdir. */
        char *slash;
        if (strlen(settings.icon_order_file) >= sizeof(order.path)) return 0;
        strcpy(order.path, settings.icon_order_file);
        strcpy(order.dir, settings.icon_order_file);
        slash = strrchr(order.dir, '/');
        if (slash != NULL)
            *slash = '\0';
        else
            order.dir[0] = '\0';
        return 1;
    }
    base = getenv("XDG_STATE_HOME");
    if (base != NULL && base[0] != '\0') {
        if (snprintf(order.dir, sizeof(order.dir), "%s/stalonetray", base)
            >= (int) sizeof(order.dir))
            return 0;
    } else {
        home = getenv("HOME");
        if (home == NULL || home[0] == '\0') return 0;
        if (snprintf(order.dir, sizeof(order.dir), "%s/.local/state/stalonetray",
                home)
            >= (int) sizeof(order.dir))
            return 0;
    }
    if (snprintf(order.path, sizeof(order.path), "%s/order-%d", order.dir,
            DefaultScreen(tray_data.dpy))
        >= (int) sizeof(order.path))
        return 0;
    return 1;
}

/* ---- file parsing ---- */

/* Parse the file into order.entries plus the file's session token. Returns
 * whether a session line was seen. */
static int order_load_file(unsigned long *file_token)
{
    FILE *f;
    char line[LINE_BUF_SZ];
    int have_session = 0;
    f = fopen(order.path, "r");
    if (f == NULL) {
        LOG_TRACE(("no icon order file at %s\n", order.path));
        return 0;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        char *nl, *cursor, *widstr, *cls, *nm, *pidstr;
        Window wid;
        unsigned long pid;
        nl = strchr(line, '\n');
        if (nl != NULL) *nl = '\0';
        if (line[0] == '#' || line[0] == '\0') continue;
        if (strncmp(line, "session ", 8) == 0) {
            *file_token = strtoul(line + 8, NULL, 16) & 0xFFFFFFFFUL;
            have_session = 1;
            continue;
        }
        /* Fields: wid <tab> class <tab> name <tab> pid. pid is optional so
         * that files written by older versions still load (pid = 0). */
        cursor = line;
        widstr = strsep(&cursor, "\t");
        cls = strsep(&cursor, "\t");
        nm = strsep(&cursor, "\t");
        pidstr = strsep(&cursor, "\t");
        if (widstr == NULL || widstr[0] == '\0') continue;
        wid = (Window) strtoul(widstr, NULL, 0);
        if (wid == None) continue;
        pid = pidstr != NULL ? strtoul(pidstr, NULL, 10) : 0;
        order_add_entry(wid, cls, nm, pid);
        LOG_TRACE(("loaded order entry: 0x%lx \"%s\".\"%s\" pid %lu\n", wid,
            nm != NULL ? nm : "", cls != NULL ? cls : "", pid));
    }
    fclose(f);
    return have_session;
}

/* ---- startup placement ---- */

/* Rank (snapshot index) of a present icon, or INT_MAX if it isn't a restored
 * one. Used to keep restored icons sorted ahead of gravity-placed newcomers. */
static int order_rank_of(Window wid)
{
    int i;
    for (i = 0; i < order.n_entries; i++)
        if (order.entries[i].claimed && order.entries[i].claimed_wid == wid)
            return i;
    return INT_MAX;
}

/* Lowest-ranked unclaimed entry matching this icon, or -1. WM_CLASS is the
 * key; when several entries share it, a matching _NET_WM_PID picks the exact
 * one (same process survives a tray restart). Absent/ambiguous pid falls back
 * to greedy claim by dock order. */
static int order_find_match(
    const char *res_class, const char *res_name, unsigned long pid)
{
    int i;
    if (pid != 0)
        for (i = 0; i < order.n_entries; i++)
            if (!order.entries[i].claimed && order.entries[i].pid == pid
                && str_eq(order.entries[i].res_class, res_class)
                && str_eq(order.entries[i].res_name, res_name))
                return i;
    for (i = 0; i < order.n_entries; i++)
        if (!order.entries[i].claimed
            && str_eq(order.entries[i].res_class, res_class)
            && str_eq(order.entries[i].res_name, res_name))
            return i;
    return -1;
}

static void order_settle(int via_timeout)
{
    if (!order.startup_active) return;
    order.startup_active = 0;
    if (via_timeout)
        LOG_INFO(("icon order: restored %d of %d icons (%ds timeout; %d did not "
                  "return)\n",
            order.n_reclaimed, order.n_entries, settings.icon_order_timeout,
            order.n_entries - order.n_reclaimed));
    else
        LOG_INFO(("icon order: restored all %d icons\n", order.n_entries));
    order_clear_snapshot();
    /* Capture the settled order (also writes a fresh file in a new session). */
    order_save();
}

void order_place_icon(struct TrayIcon *ti)
{
    int idx;
    char *rc = NULL, *rn = NULL;
    unsigned long pid;
    struct TrayIcon *p, *before = NULL;
    if (!order.startup_active) return;
    /* Match on WM_CLASS (+ _NET_WM_PID to disambiguate): window ids are not
     * stable across a tray restart since many apps recreate their dock
     * window when they redock. */
    order_get_class(ti->wid, &rc, &rn);
    pid = order_get_pid(ti->wid);
    idx = order_find_match(rc, rn, pid);
    LOG_TRACE(("icon 0x%lx is \"%s\".\"%s\" pid %lu -> snapshot entry %d\n",
        ti->wid, rn != NULL ? rn : "", rc != NULL ? rc : "", pid, idx));
    free(rc);
    free(rn);
    if (idx < 0) {
        LOG_TRACE(("icon 0x%lx has no snapshot entry, leaving to gravity\n",
            ti->wid));
        return;
    }
    order.entries[idx].claimed = 1;
    order.entries[idx].claimed_wid = ti->wid;
    order.n_reclaimed++;
    /* Splice ti ahead of the first icon that outranks it (gravity-placed icons
     * rank INT_MAX, so restored icons cluster ahead of them in rank order). */
    for (p = icons_head; p != NULL; p = p->next) {
        if (p == ti) continue;
        if (order_rank_of(p->wid) > idx) {
            before = p;
            break;
        }
    }
    icon_list_move_before(ti, before);
    layout_relayout_in_list_order();
    LOG_TRACE(("icon order: restored 0x%lx to rank %d (%d/%d)\n", ti->wid, idx,
        order.n_reclaimed, order.n_entries));
    if (order.n_reclaimed >= order.n_entries) order_settle(0);
}

/* ---- public entry points ---- */

void order_init(void)
{
    unsigned long file_token = 0;
    int have_session;
    order.entries = NULL;
    order.n_entries = 0;
    order.n_reclaimed = 0;
    order.startup_active = 0;
    if (!settings.remember_icon_order) return;
    xa_net_wm_pid = XInternAtom(tray_data.dpy, "_NET_WM_PID", False);
    if (!order_resolve_path()) {
        LOG_ERROR(("icon order: could not resolve state file path; disabling\n"));
        settings.remember_icon_order = 0;
        return;
    }
    order.session_token = order_session_token();
    have_session = order_load_file(&file_token);
    if (!have_session || file_token != order.session_token) {
        if (order.n_entries > 0)
            LOG_INFO(("icon order: snapshot is from a previous X session, "
                      "discarding\n"));
        else
            LOG_INFO(("icon order: no usable snapshot, starting fresh\n"));
        order_clear_snapshot();
        return; /* tracking mode: saves happen on every change */
    }
    if (order.n_entries == 0) {
        LOG_INFO(("icon order: snapshot is empty, starting fresh\n"));
        return;
    }
    order.startup_active = 1;
    order.startup_start = time(NULL);
    LOG_INFO(("icon order: restoring %d icons from %s (%ds timeout)\n",
        order.n_entries, order.path, settings.icon_order_timeout));
}

void order_save(void)
{
    FILE *f;
    char tmp[PATH_MAX];
    struct TrayIcon *ti;
    if (!settings.remember_icon_order) return;
    if (order.startup_active) return; /* deferred until settle */
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", order.path) >= (int) sizeof(tmp)) {
        LOG_ERROR(("icon order: path too long, not saving\n"));
        return;
    }
    order_make_dir();
    f = fopen(tmp, "w");
    if (f == NULL) {
        LOG_TRACE(("icon order: could not open %s for writing\n", tmp));
        return;
    }
    fprintf(f, "# stalonetray icon order\n");
    fprintf(f, "session %08lx\n", order.session_token);
    for (ti = icons_head; ti != NULL; ti = ti->next) {
        char *rc = NULL, *rn = NULL;
        if (!ti->is_visible) continue;
        order_get_class(ti->wid, &rc, &rn);
        fprintf(f, "0x%lx\t%s\t%s\t%lu\n", ti->wid, rc != NULL ? rc : "",
            rn != NULL ? rn : "", order_get_pid(ti->wid));
        free(rc);
        free(rn);
    }
    fclose(f);
    if (rename(tmp, order.path) != 0) {
        LOG_TRACE(("icon order: could not rename %s to %s\n", tmp, order.path));
        unlink(tmp);
        return;
    }
    LOG_TRACE(("icon order: saved to %s\n", order.path));
}

void order_periodic(void)
{
    if (!order.startup_active) return;
    if (time(NULL) - order.startup_start >= settings.icon_order_timeout)
        order_settle(1);
}

int order_startup_pending(void)
{
    return order.startup_active;
}
