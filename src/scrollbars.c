/* ************************************
 * vim:tabstop=4:shiftwidth=4:cindent:fen:fdm=syntax
 * tray.c
 * Mon, 09 Mar 2009 12:38:30 -0400
 * ************************************
 * scrollbars functions
 * ************************************/

#include <limits.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xmd.h>
#include <X11/Xutil.h>

#include "embed.h"
#include "tray.h"
#include "xutils.h"

void scrollbars_init()
{
    tray_data.scrollbars_data.scroll_pos.x = 0;
    tray_data.scrollbars_data.scroll_pos.y = 0;
    tray_data.scrollbars_data.scroll_base.x = 0;
    tray_data.scrollbars_data.scroll_base.y = 0;
    tray_data.scrollbars_data.scrollbar_down = -1;
}

void scrollbars_create()
{
    int i;
    /*#define DEBUG_SCROLLBAR_POSITIONS*/

    if (settings.scrollbars_mode & SB_MODE_VERT) {
        tray_data.scrollbars_data.scrollbar[SB_WND_TOP] =
            XCreateSimpleWindow(tray_data.dpy, tray_data.tray, 0, 0, 1, 1, 0,
#ifdef DEBUG_SCROLLBAR_POSITIONS
                0xff00ff, 0xff00ff);
#else
                settings.bg_color.pixel, settings.bg_color.pixel);
#endif
        tray_data.scrollbars_data.scrollbar[SB_WND_BOT] =
            XCreateSimpleWindow(tray_data.dpy, tray_data.tray, 0, 0, 1, 1, 0,
#ifdef DEBUG_SCROLLBAR_POSITIONS
                0xffff00, 0xffff00);
#else
                settings.bg_color.pixel, settings.bg_color.pixel);
#endif
    } else {
        tray_data.scrollbars_data.scrollbar[SB_WND_TOP] = None;
        tray_data.scrollbars_data.scrollbar[SB_WND_BOT] = None;
    }
    if (settings.scrollbars_mode & SB_MODE_HORZ) {
        tray_data.scrollbars_data.scrollbar[SB_WND_LFT] =
            XCreateSimpleWindow(tray_data.dpy, tray_data.tray, 0, 0, 1, 1, 0,
#ifdef DEBUG_SCROLLBAR_POSITIONS
                0x00ff00, 0x00ff00);
#else
                settings.bg_color.pixel, settings.bg_color.pixel);
#endif
        tray_data.scrollbars_data.scrollbar[SB_WND_RHT] =
            XCreateSimpleWindow(tray_data.dpy, tray_data.tray, 0, 0, 1, 1, 0,
#ifdef DEBUG_SCROLLBAR_POSITIONS
                0x00ffff, 0x00ffff);
#else
                settings.bg_color.pixel, settings.bg_color.pixel);
#endif
    } else {
        tray_data.scrollbars_data.scrollbar[SB_WND_LFT] = None;
        tray_data.scrollbars_data.scrollbar[SB_WND_RHT] = None;
    }
    scrollbars_update();
    
    if (settings.scroll_everywhere && settings.scrollbars_mode != SB_MODE_NONE) {
        XGrabButton(tray_data.dpy, Button5, AnyModifier, tray_data.tray, False,
                ButtonReleaseMask, GrabModeAsync, GrabModeAsync, None, None);
        XGrabButton(tray_data.dpy, Button4, AnyModifier, tray_data.tray, False,
                ButtonReleaseMask, GrabModeAsync, GrabModeAsync, None, None);
    }

    for (i = 0; i < SB_WND_MAX; i++)
        if (tray_data.scrollbars_data.scrollbar[i] != None) {
#ifndef DEBUG_SCROLLBAR_POSITIONS
            XSetWindowBackgroundPixmap(tray_data.dpy,
                tray_data.scrollbars_data.scrollbar[i], ParentRelative);
#endif
            XMapRaised(tray_data.dpy, tray_data.scrollbars_data.scrollbar[i]);
            XSelectInput(tray_data.dpy, tray_data.scrollbars_data.scrollbar[i],
                ButtonPressMask | ButtonReleaseMask | ButtonMotionMask
                    | EnterWindowMask | LeaveWindowMask);
        }
}

int scrollbars_refresh(int exposures)
{
    int i;
    for (i = 0; i < SB_WND_MAX; i++)
        if (tray_data.scrollbars_data.scrollbar[i] != None)
            x11_refresh_window(tray_data.dpy,
                tray_data.scrollbars_data.scrollbar[i],
                tray_data.scrollbars_data.scrollbar_xsh[i].width,
                tray_data.scrollbars_data.scrollbar_xsh[i].height, exposures);
    return SUCCESS;
}

int scrollbars_update()
{
    int offset, i;
    static int initialized = 0;

    XSizeHints scrollbar_xsh_local[SB_WND_MAX];

    if (settings.scrollbars_mode & SB_MODE_HORZ) {
        offset =
            (settings.vertical && (settings.scrollbars_mode & SB_MODE_VERT))
            ? settings.scrollbars_size
            : 0;

        scrollbar_xsh_local[SB_WND_LFT].x = 0;
        scrollbar_xsh_local[SB_WND_LFT].y = offset;
        scrollbar_xsh_local[SB_WND_LFT].width = settings.scrollbars_size;
        scrollbar_xsh_local[SB_WND_LFT].height =
            tray_data.xsh.height - offset * 2;

        scrollbar_xsh_local[SB_WND_RHT] = scrollbar_xsh_local[SB_WND_LFT];
        scrollbar_xsh_local[SB_WND_RHT].x =
            tray_data.xsh.width - settings.scrollbars_size;
    }
    if (settings.scrollbars_mode & SB_MODE_VERT) {
        offset =
            (settings.vertical && (settings.scrollbars_mode & SB_MODE_HORZ))
            ? settings.scrollbars_size
            : 0;

        scrollbar_xsh_local[SB_WND_TOP].x = offset;
        scrollbar_xsh_local[SB_WND_TOP].y = 0;
        scrollbar_xsh_local[SB_WND_TOP].width =
            tray_data.xsh.width - offset * 2;
        scrollbar_xsh_local[SB_WND_TOP].height = settings.scrollbars_size;

        scrollbar_xsh_local[SB_WND_BOT] = scrollbar_xsh_local[SB_WND_TOP];
        scrollbar_xsh_local[SB_WND_BOT].y =
            tray_data.xsh.height - settings.scrollbars_size;
    }

    for (i = 0; i < SB_WND_MAX; i++)
        if (tray_data.scrollbars_data.scrollbar[i] != None
            && (!initialized
                || (scrollbar_xsh_local[i].x
                        != tray_data.scrollbars_data.scrollbar_xsh[i].x
                    || scrollbar_xsh_local[i].y
                        != tray_data.scrollbars_data.scrollbar_xsh[i].y
                    || scrollbar_xsh_local[i].width
                        != tray_data.scrollbars_data.scrollbar_xsh[i].width
                    || scrollbar_xsh_local[i].height
                        != tray_data.scrollbars_data.scrollbar_xsh[i]
                               .height))) {
            XMoveResizeWindow(tray_data.dpy,
                tray_data.scrollbars_data.scrollbar[i],
                scrollbar_xsh_local[i].x, scrollbar_xsh_local[i].y,
                scrollbar_xsh_local[i].width, scrollbar_xsh_local[i].height);
            tray_data.scrollbars_data.scrollbar_xsh[i] =
                scrollbar_xsh_local[i];
        }

    initialized = 1;
    return x11_ok();
}

int scrollbars_get_id(Window wid, int x, int y)
{
    int i;
    for (i = 0; i < SB_WND_MAX; i++)
        if (wid == tray_data.scrollbars_data.scrollbar[i] && 0 <= x && 0 <= y
            && x < tray_data.scrollbars_data.scrollbar_xsh[i].width
            && y < tray_data.scrollbars_data.scrollbar_xsh[i].height) {
            return i;
        }
    return -1;
}

void scrollbars_validate_scroll_pos()
{
    int layout_width, layout_height;
    int base_width, base_height;
    struct Point max_scroll_pos;

    layout_get_size(&layout_width, &layout_height);
    tray_calc_tray_area_size(
        tray_data.xsh.width, tray_data.xsh.height, &base_width, &base_height);

    max_scroll_pos.x = layout_width - base_width;
    max_scroll_pos.y = layout_height - base_height;

    val_range(max_scroll_pos.x, 0, INT_MAX);
    val_range(max_scroll_pos.y, 0, INT_MAX);
    LOG_TRACE(("computed max scroll position: %dx%d\n", max_scroll_pos.x,
        max_scroll_pos.y));

    val_range(tray_data.scrollbars_data.scroll_pos.x, 0, max_scroll_pos.x);
    val_range(tray_data.scrollbars_data.scroll_pos.y, 0, max_scroll_pos.y);
}

int scrollbars_click(int id)
{
    /* TODO: implement scroll gravity (i.e. scroll weel must scroll in
     * direction that agrees with current tray orientation) */
    /* TODO: scrollbars_inc must be settable via cmdline */

    /* last entry is for action that just sanitizes current scroll
     * position after icon removal */
    static struct Point scrollbars_deltas[SB_WND_MAX + 1] = {
        {0, -1}, {0, 1}, {-1, 0}, {1, 0}, {0, 0}};

    tray_data.scrollbars_data.scroll_pos.x +=
        (settings.icon_gravity & GRAV_W ? 1 : -1) * scrollbars_deltas[id].x
        * settings.scrollbars_inc;
    tray_data.scrollbars_data.scroll_pos.y +=
        (settings.icon_gravity & GRAV_N ? 1 : -1) * scrollbars_deltas[id].y
        * settings.scrollbars_inc;

    scrollbars_validate_scroll_pos();
    LOG_TRACE(("computed new scroll position: %dx%d\n",
        tray_data.scrollbars_data.scroll_pos.x,
        tray_data.scrollbars_data.scroll_pos.y));

    icon_list_forall(&layout_translate_to_window);
    embedder_update_positions(id != SB_WND_MAX);
    return SUCCESS;
}

void scrollbars_handle_event(XEvent ev)
{
    int id;
    switch (ev.type) {
    case EnterNotify:
    case LeaveNotify:
        LOG_TRACE(("%s, wid=0x%lx x=%d y=%d\n",
            ev.type == EnterNotify ? "EnterNotify" : "LeaveNotify",
            ev.xcrossing.window, ev.xcrossing.x, ev.xcrossing.y));
        if (settings.scrollbars_highlight_color_str != NULL
            && (id = scrollbars_get_id(ev.xcrossing.window, 0, 0)) != -1) {
            if (ev.type == EnterNotify)
                scrollbars_highlight_on(id);
            else
                scrollbars_highlight_off(id);
        }
        break;
    case ButtonPress:
        LOG_TRACE(("ButtonPress, state=0x%x\n", ev.xbutton.state));
        if (ev.xbutton.button == Button1
            && (id = scrollbars_get_id(
                    ev.xbutton.window, ev.xbutton.x, ev.xbutton.y))
                != -1) {
            tray_data.scrollbars_data.scrollbar_down = id;
            tray_data.scrollbars_data.scrollbar_repeat_active = 1;
            tray_data.scrollbars_data.scrollbar_repeat_counter =
                SCROLLBAR_REPEAT_COUNTDOWN_MAX_1ST;
            tray_data.scrollbars_data.scrollbar_repeats_done = 0;
        }
        break;
    case MotionNotify:
        LOG_TRACE(("MotionNotify, state=0x%x\n", ev.xbutton.state));
        tray_data.scrollbars_data.scrollbar_repeat_active =
            (tray_data.scrollbars_data.scrollbar_down != -1
                && scrollbars_get_id(
                       ev.xmotion.window, ev.xmotion.x, ev.xmotion.y)
                    == tray_data.scrollbars_data.scrollbar_down);
        break;
    case ButtonRelease:
        LOG_TRACE(("ButtonRelease, state=0x%x\n", ev.xbutton.state));
        switch (ev.xbutton.button) {
        case Button1:
            if (tray_data.scrollbars_data.scrollbar_down != -1) {
#if 0
				/* If no repeats were done, advance scroll position */
				if ((scrollbars_get_id(ev.xbutton.window, ev.xbutton.x, ev.xbutton.y) != -1) &&
					(tray_data.scrollbars_data.scrollbar_repeats_done == 0))
				{
					scrollbars_click(tray_data.scrollbars_data.scrollbar_down);
				}
#endif
                tray_data.scrollbars_data.scrollbar_down = -1;
                tray_data.scrollbars_data.scrollbar_repeat_active = 0;
            }
            break;
        case Button4:
            if (settings.vertical && settings.scrollbars_mode & SB_MODE_VERT)
                scrollbars_click(SB_WND_TOP);
            else if (settings.scrollbars_mode & SB_MODE_HORZ)
                scrollbars_click(SB_WND_LFT);
            break;
        case Button5:
            if (settings.vertical && settings.scrollbars_mode & SB_MODE_VERT)
                scrollbars_click(SB_WND_BOT);
            else if (settings.scrollbars_mode & SB_MODE_HORZ)
                scrollbars_click(SB_WND_RHT);
            break;
        default: break;
        }
        break;
    }
}

void scrollbars_periodic_tasks()
{
    if (tray_data.scrollbars_data.scrollbar_down != -1
        && tray_data.scrollbars_data.scrollbar_repeat_active) {
        scrollbars_click(tray_data.scrollbars_data.scrollbar_down);
    }
}

int scrollbars_scroll_to(struct TrayIcon *ti)
{
    struct Rect tray_viewport_rect;
    tray_viewport_rect.x = tray_data.scrollbars_data.scroll_base.x,
    tray_viewport_rect.y = tray_data.scrollbars_data.scroll_base.y,
    tray_calc_tray_area_size(tray_data.xsh.width, tray_data.xsh.height,
        &tray_viewport_rect.w, &tray_viewport_rect.h);
    /* Check if icon is already visible. If so, nothing needs to be done */
    if (RECTS_ISECT(tray_viewport_rect, ti->l.icn_rect)) return SUCCESS;
    /* Update scroll pos so that the icon is visible */
    /* TODO: must be in separate function, with a reverse */
    tray_data.scrollbars_data.scroll_pos.x =
        (settings.icon_gravity & GRAV_W ? 1 : -1)
        * (ti->l.icn_rect.x - tray_data.scrollbars_data.scroll_base.x);
    tray_data.scrollbars_data.scroll_pos.y =
        (settings.icon_gravity & GRAV_W ? 1 : -1)
        * (ti->l.icn_rect.y - tray_data.scrollbars_data.scroll_base.y);
    scrollbars_validate_scroll_pos();
    LOG_TRACE(("computed required scroll position: %dx%d\n",
        tray_data.scrollbars_data.scroll_pos.x,
        tray_data.scrollbars_data.scroll_pos.y));
    icon_list_forall(&layout_translate_to_window);
    embedder_update_positions(True);
    return SUCCESS;
}

int scrollbars_highlight_on(int id)
{
    Window sb_wid;
    sb_wid =
        (0 <= id && id < 4) ? tray_data.scrollbars_data.scrollbar[id] : None;
    if (sb_wid != None)
        XSetWindowBackground(
            tray_data.dpy, sb_wid, settings.scrollbars_highlight_color.pixel);
    scrollbars_refresh(1);
    return SUCCESS;
}

int scrollbars_highlight_off(int id)
{
    Window sb_wid;
    sb_wid =
        (0 <= id && id < 4) ? tray_data.scrollbars_data.scrollbar[id] : None;
    if (sb_wid != None)
        XSetWindowBackgroundPixmap(tray_data.dpy, sb_wid, ParentRelative);
    scrollbars_refresh(1);
    return SUCCESS;
}
