/* -------------------------------
 * vim:tabstop=4:shiftwidth=4
 * drag.c
 * -------------------------------
 * Drag-to-reorder tray icons.
 *
 * We arm a passive XGrabButton(Button1 + modifier) on each icon's mid-parent.
 * When the user presses Button1 with the modifier, X promotes the grab to an
 * active one; we then track motion until ButtonRelease, splicing the icon list
 * to follow the cursor and relaying out peers in real time.
 * -------------------------------*/

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>

#include "common.h"
#include "debug.h"
#include "drag.h"
#include "embed.h"
#include "icons.h"
#include "layout.h"
#include "order.h"
#include "settings.h"
#include "tray.h"
#include "xutils.h"

/* Locks that may be combined with the user-chosen modifier and that we must
 * grab for explicitly, since XGrabButton matches modifier masks exactly.
 * The list mirrors what most X11 apps do: CapsLock, NumLock (Mod2 by default),
 * and the two together. */
static const unsigned int IGNORED_LOCKS[] = {
    0,
    LockMask,
    Mod2Mask,
    LockMask | Mod2Mask,
};

static struct {
    int active;
    struct TrayIcon *src;
    /* Pointer position when the drag started (root coords). */
    int anchor_root_x, anchor_root_y;
    /* src->l.icn_rect position when the drag started (tray-relative). */
    int anchor_icn_x, anchor_icn_y;
    Cursor cursor;
    /* Marker showing the slot src will drop into. */
    Window indicator;
} drag_state;

static struct TrayIcon *drag_pick_target(int tray_x, int tray_y);
static int drag_pick_target_cbk(struct TrayIcon *ti);
static void drag_follow_cursor(int root_x, int root_y);
static void drag_update_indicator(void);
static void drag_finish(void);

/* Closure for drag_pick_target. */
static struct {
    int x, y;
} pick_pt;

void drag_init(void)
{
    XColor color;
    drag_state.active = 0;
    drag_state.src = NULL;
    drag_state.cursor = None;
    drag_state.indicator = None;
    if (!settings.drag_reorder) return;
    drag_state.cursor = XCreateFontCursor(tray_data.dpy, XC_fleur);
    /* A lightweight always-allocated marker window, shown only while dragging.
     * It is sized and positioned per-drag in drag_update_indicator(). */
    if (!x11_parse_color(tray_data.dpy, "gold", &color))
        x11_parse_color(tray_data.dpy, "yellow", &color);
    drag_state.indicator = XCreateSimpleWindow(tray_data.dpy, tray_data.tray,
        0, 0, 1, 1, 0, color.pixel, color.pixel);
}

void drag_install_grab(struct TrayIcon *ti)
{
    unsigned int i;
    if (!settings.drag_reorder || settings.drag_modifier == 0) return;
    if (ti->mid_parent == None) return;
    /* The passive grab activates only when the modifier+button match; the
     * icon's own client keeps receiving unmodified clicks. */
    XSelectInput(tray_data.dpy, ti->mid_parent,
        ButtonPressMask | ButtonReleaseMask | ButtonMotionMask);
    for (i = 0; i < sizeof(IGNORED_LOCKS) / sizeof(IGNORED_LOCKS[0]); i++)
        XGrabButton(tray_data.dpy, Button1,
            settings.drag_modifier | IGNORED_LOCKS[i], ti->mid_parent,
            False, ButtonPressMask | ButtonReleaseMask | ButtonMotionMask,
            GrabModeAsync, GrabModeAsync, None, drag_state.cursor);
}

int drag_icon_is_floating(struct TrayIcon *ti)
{
    return drag_state.active && drag_state.src == ti;
}

void drag_forget_icon(struct TrayIcon *ti)
{
    if (drag_state.src == ti) {
        drag_state.active = 0;
        drag_state.src = NULL;
        if (drag_state.indicator != None)
            XUnmapWindow(tray_data.dpy, drag_state.indicator);
    }
}

void drag_handle_event(XEvent ev)
{
    struct TrayIcon *ti, *target;
    int tray_x, tray_y;

    switch (ev.type) {
    case ButtonPress:
        if (drag_state.active) break;
        if (ev.xbutton.button != Button1) break;
        if (settings.drag_modifier == 0
            || (ev.xbutton.state & settings.drag_modifier)
                != settings.drag_modifier)
            break;
        ti = icon_list_find_by_mid_parent(ev.xbutton.window);
        if (ti == NULL || !ti->is_visible || !ti->is_layed_out) break;
        drag_state.active = 1;
        drag_state.src = ti;
        drag_state.anchor_root_x = ev.xbutton.x_root;
        drag_state.anchor_root_y = ev.xbutton.y_root;
        drag_state.anchor_icn_x = ti->l.icn_rect.x;
        drag_state.anchor_icn_y = ti->l.icn_rect.y;
        /* Show the landing marker, then float the icon above everything (incl.
         * the marker). Moves during the drag don't restack, so this ordering
         * holds for the whole gesture. */
        drag_update_indicator();
        if (drag_state.indicator != None) {
            XMapWindow(tray_data.dpy, drag_state.indicator);
            XRaiseWindow(tray_data.dpy, drag_state.indicator);
        }
        XRaiseWindow(tray_data.dpy, ti->mid_parent);
        LOG_TRACE(("drag start: icon 0x%lx at (%d,%d)\n", ti->wid,
            ti->l.icn_rect.x, ti->l.icn_rect.y));
        break;

    case MotionNotify:
        if (!drag_state.active) break;
        drag_follow_cursor(ev.xmotion.x_root, ev.xmotion.y_root);
        /* Translate pointer to tray-relative coordinates and pick a target. */
        tray_x = ev.xmotion.x_root - tray_data.xsh.x;
        tray_y = ev.xmotion.y_root - tray_data.xsh.y;
        target = drag_pick_target(tray_x, tray_y);
        if (target == NULL || target == drag_state.src) break;
        /* Decide whether src lands before or after target by comparing the
         * pointer to the target's midpoint along the major axis. */
        {
            int past_mid;
            struct TrayIcon *before;
            if (settings.vertical)
                past_mid = tray_y
                    >= target->l.icn_rect.y + target->l.icn_rect.h / 2;
            else
                past_mid = tray_x
                    >= target->l.icn_rect.x + target->l.icn_rect.w / 2;
            /* Use raw next/prev (not icon_list_next, which wraps) so that
             * splicing past the tail moves src to the end. */
            before = past_mid ? target->next : target;
            if (before == drag_state.src) before = before->next;
            /* Debounce on the resulting position, not on target identity:
             * src is already where it belongs when its successor is `before`.
             * This lets crossing a neighbour's midpoint re-trigger the splice
             * without first leaving that neighbour's rect. */
            if (drag_state.src->next == before) break;
            icon_list_move_before(drag_state.src, before);
        }
        layout_relayout_in_list_order();
        drag_update_indicator();
        /* Non-forced: only icons whose slot actually moved get repositioned,
         * which keeps the rest of the tray from flickering on each shuffle. */
        embedder_update_positions(False);
        break;

    case ButtonRelease:
        if (!drag_state.active) break;
        if (ev.xbutton.button != Button1) break;
        drag_finish();
        break;
    }
}

/* Return the visible icon (other than src) whose icn_rect contains
 * (tray_x, tray_y), or NULL. */
static struct TrayIcon *drag_pick_target(int tray_x, int tray_y)
{
    pick_pt.x = tray_x;
    pick_pt.y = tray_y;
    return icon_list_advanced_find(&drag_pick_target_cbk);
}

static int drag_pick_target_cbk(struct TrayIcon *ti)
{
    if (ti == drag_state.src) return NO_MATCH;
    if (!ti->is_visible || !ti->is_layed_out) return NO_MATCH;
    if (pick_pt.x >= ti->l.icn_rect.x
        && pick_pt.x < ti->l.icn_rect.x + ti->l.icn_rect.w
        && pick_pt.y >= ti->l.icn_rect.y
        && pick_pt.y < ti->l.icn_rect.y + ti->l.icn_rect.h)
        return MATCH;
    return NO_MATCH;
}

static void drag_follow_cursor(int root_x, int root_y)
{
    struct TrayIcon *ti = drag_state.src;
    int new_icn_x = drag_state.anchor_icn_x + (root_x - drag_state.anchor_root_x);
    int new_icn_y = drag_state.anchor_icn_y + (root_y - drag_state.anchor_root_y);
    /* mid_parent is centered inside the icn_rect by the same offset embed.c
     * applies in CALC_INNER_POS. */
    int inner_x = (ti->l.icn_rect.w - ti->l.wnd_sz.x) / 2;
    int inner_y = (ti->l.icn_rect.h - ti->l.wnd_sz.y) / 2;
    XMoveWindow(tray_data.dpy, ti->mid_parent,
        new_icn_x + inner_x, new_icn_y + inner_y);
}

/* Size and position the landing marker to mirror src's reserved slot. The
 * slot is read from src->l.icn_rect, which the relayout keeps pointing at
 * where src will drop (even though mid_parent is off following the cursor). */
static void drag_update_indicator(void)
{
    struct TrayIcon *ti = drag_state.src;
    int inner_x, inner_y;
    if (drag_state.indicator == None || ti == NULL) return;
    inner_x = (ti->l.icn_rect.w - ti->l.wnd_sz.x) / 2;
    inner_y = (ti->l.icn_rect.h - ti->l.wnd_sz.y) / 2;
    XMoveResizeWindow(tray_data.dpy, drag_state.indicator,
        ti->l.icn_rect.x + inner_x, ti->l.icn_rect.y + inner_y,
        ti->l.wnd_sz.x, ti->l.wnd_sz.y);
}

static void drag_finish(void)
{
    struct TrayIcon *src = drag_state.src;
    LOG_TRACE(("drag end: icon 0x%lx\n", src ? src->wid : None));
    if (drag_state.indicator != None)
        XUnmapWindow(tray_data.dpy, drag_state.indicator);
    drag_state.active = 0;
    drag_state.src = NULL;
    /* mid_parent has been tracking the cursor; mark it dirty so the embedder
     * snaps it back to its slot, while peers (already in place) stay put. */
    if (src != NULL) src->is_updated = True;
    layout_relayout_in_list_order();
    embedder_update_positions(False);
    if (src != NULL && src->mid_parent != None)
        XLowerWindow(tray_data.dpy, src->mid_parent);
    tray_update_window_props();
    order_save();
}

