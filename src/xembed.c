/* -------------------------------
 * vim:tabstop=4:shiftwidth=4
 * xembed.c
 * Tue, 24 Aug 2004 12:05:38 +0700
 * -------------------------------
 * XEMBED protocol implementation
 * -------------------------------*/

/* Currently broken:
 * - XEMBED_{ACTIVATE,REGISTER,UNREGISTER}_ACCELERATOR
 * - XEMBED_REQUEST_FOCUS */

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xmd.h>
#include <X11/Xutil.h>

#include "common.h"
#include "debug.h"
#include "wmh.h"
#include "xembed.h"
#include "xutils.h"

#include "list.h"

/* Internal return codes */
#define XEMBED_RESULT_OK 0
#define XEMBED_RESULT_UNSUPPORTED 1
#define XEMBED_RESULT_X11ERROR 2

/* XEMBED messages */
#define XEMBED_EMBEDDED_NOTIFY 0
#define XEMBED_WINDOW_ACTIVATE 1
#define XEMBED_WINDOW_DEACTIVATE 2
#define XEMBED_REQUEST_FOCUS 3
#define XEMBED_FOCUS_IN 4
#define XEMBED_FOCUS_OUT 5
#define XEMBED_FOCUS_NEXT 6
#define XEMBED_FOCUS_PREV 7
/* 8-9 were used for XEMBED_GRAB_KEY/XEMBED_UNGRAB_KEY */
#define XEMBED_MODALITY_ON 10
#define XEMBED_MODALITY_OFF 11
#define XEMBED_REGISTER_ACCELERATOR 12
#define XEMBED_UNREGISTER_ACCELERATOR 13
#define XEMBED_ACTIVATE_ACCELERATOR 14

/* Details for  XEMBED_FOCUS_IN */
#define XEMBED_FOCUS_CURRENT 0
#define XEMBED_FOCUS_FIRST 1
#define XEMBED_FOCUS_LAST 2

/* Modifiers field for XEMBED_REGISTER_ACCELERATOR */
#define XEMBED_MODIFIER_SHIFT (1 << 0)
#define XEMBED_MODIFIER_CONTROL (1 << 1)
#define XEMBED_MODIFIER_ALT (1 << 2)
#define XEMBED_MODIFIER_SUPER (1 << 3)
#define XEMBED_MODIFIER_HYPER (1 << 4)

/* Flags for XEMBED_ACTIVATE_ACCELERATOR */
#define XEMBED_ACCELERATOR_OVERLOADED (1 << 0)

/* Directions for focusing */
#define XEMBED_DIRECTION_DEFAULT 0
#define XEMBED_DIRECTION_UP_DOWN 1
#define XEMBED_DIRECTION_LEFT_RIGHT 2

/* Flags for _XEMBED_INFO */
#define XEMBED_MAPPED (1 << 0)

/* Structure to hold XEMBED accelerator data */
struct XEMBEDAccel {
    struct XEMBEDAccel *next, *prev;
    int overloaded; /* Is this accelerator overloaded? */
    long id; /* Accelerator Id */
    long symb; /* Symbol */
    long mods; /* Modifiers */
};

/* Shortcuts for sending XEMBED messages */
#define xembed_send_msg(dpy, dst, timestamp, msg, detail, data1, data2) \
    x11_send_client_msg32(dpy, dst, dst, tray_data.xembed_data.xa_xembed, \
        timestamp, msg, detail, data1, data2)

#define xembed_send_embedded_notify(dpy, src, dst, timestamp) \
    xembed_send_msg(dpy, dst, timestamp, XEMBED_EMBEDDED_NOTIFY, 0, src, 0)

#define xembed_send_window_activate(dpy, dst, timestamp) \
    xembed_send_msg(dpy, dst, timestamp, XEMBED_WINDOW_ACTIVATE, 0, 0, 0)

#define xembed_send_window_deactivate(dpy, dst, timestamp) \
    xembed_send_msg(dpy, dst, timestamp, XEMBED_WINDOW_DEACTIVATE, 0, 0, 0)

#define xembed_send_focus_in(dpy, dst, focus, timestamp) \
    xembed_send_msg(dpy, dst, timestamp, XEMBED_FOCUS_IN, focus, 0, 0)

#define xembed_send_focus_out(dpy, dst, timestamp) \
    xembed_send_msg(dpy, dst, timestamp, XEMBED_FOCUS_OUT, 0, 0, 0)

#define xembed_send_activate_accelerator(dpy, dst, timestamp, id, overloaded) \
    xembed_send_msg( \
        dpy, dst, timestamp, XEMBED_ACTIVATE_ACCELERATOR, id, overloaded, 0)

/* Retrive XEMBED data for the given icon */
int xembed_retrive_data(struct TrayIcon *ti);
/* Post icon XEMBED data to window property */
int xembed_post_data(struct TrayIcon *ti);
/* Returns the next icon in tab chain */
struct TrayIcon *xembed_next();
/* Returns the previous icon in tab chain */
struct TrayIcon *xembed_prev();
/* XEMBED event handler */
int xembed_process_kbd_event(XKeyEvent xkey);
/* Register new XEMBED accelerator */
void xembed_add_accel(long id, long symb, long mods);
/* Delete previously registered XEMBED accelerator */
void xembed_del_accel(long id);
/* Activate previously registered XEMBED accelerator */
void xembed_act_accel(struct XEMBEDAccel *accel);
/* Switch XEMBED focus to the specified icon */
void xembed_switch_focus_to(struct TrayIcon *tgt, long focus);
/* Broadcast the focus change to all icons */
void xembed_track_focus_change(int activate);
/* Process XEMBED message */
void xembed_message(XClientMessageEvent ev);
/* Tries to request focus from WM */
void xembed_request_focus_from_wm();

void xembed_init()
{
    /* 1. Initialize data structures */
    tray_data.xembed_data.window_has_focus = False;
    tray_data.xembed_data.focus_requested = False;
    tray_data.xembed_data.current = NULL;
    tray_data.xembed_data.accels = NULL;
    tray_data.xembed_data.timestamp = CurrentTime;
    tray_data.xembed_data.xa_xembed =
        XInternAtom(tray_data.dpy, "_XEMBED", False);
    tray_data.xembed_data.xa_xembed_info =
        XInternAtom(tray_data.dpy, "_XEMBED_INFO", False);
    /* 2. Create focus proxy (see XEMBED spec) */
    tray_data.xembed_data.focus_proxy = XCreateSimpleWindow(
        tray_data.dpy, tray_data.tray, -1, -1, 1, 1, 0, 0, 0);
    XSelectInput(tray_data.dpy, tray_data.xembed_data.focus_proxy,
        FocusChangeMask | KeyPressMask | KeyReleaseMask);
    XMapRaised(tray_data.dpy, tray_data.xembed_data.focus_proxy);
    if (!x11_ok()) DIE(("could not create focus proxy\n"));
    LOG_TRACE((
        "created focus proxy, wid=0x%lx\n", tray_data.xembed_data.focus_proxy));
}

void xembed_handle_event(XEvent ev)
{
    switch (ev.type) {
    case FocusOut:
        /* Broadcast that the focus has left tray window */
        LOG_TRACE(("FocusOut 0x%lx\n", ev.xfocus.window));
        if (ev.xfocus.window == tray_data.xembed_data.focus_proxy)
            xembed_track_focus_change(False);
        break;
    case ClientMessage:
        /* Handle XEMBED-related messages */
        if (ev.xclient.message_type == tray_data.xembed_data.xa_xembed) {
            xembed_message(ev.xclient);
        } else if (ev.xclient.message_type == tray_data.xa_tray_opcode) {
            /* we peek at _NET_SYSTEM_TRAY_OPCODE messages
             * to obtain proper timestamp for embedding */
            tray_data.xembed_data.timestamp = ev.xclient.data.l[0];
            if (tray_data.xembed_data.timestamp == CurrentTime)
                tray_data.xembed_data.timestamp =
                    x11_get_server_timestamp(tray_data.dpy, tray_data.tray);
        } else if (ev.xclient.message_type == tray_data.xa_wm_protocols
            && (unsigned long) ev.xclient.data.l[0] == tray_data.xa_wm_take_focus
            && tray_data.xembed_data.focus_requested) {
            XSetInputFocus(tray_data.dpy, tray_data.xembed_data.focus_proxy,
                RevertToParent, ev.xclient.data.l[1]);
            if (!x11_ok())
                DIE_IE(("Could not set focus to XEMBED focus proxy\n"));
            LOG_TRACE(("focus set to focus proxy\n"));
            xembed_track_focus_change(True);
            tray_data.xembed_data.focus_requested = False;
        }
        break;
    case KeyRelease:
    case KeyPress:
        /* Propagate key events to currently focused icon */
        tray_data.xembed_data.timestamp = ev.xkey.time;
        if (ev.type == KeyRelease && xembed_process_kbd_event(ev.xkey)) break;
        if (tray_data.xembed_data.current != NULL) {
            int rc;
            LOG_TRACE(("current icon accepts_focus: %d\n",
                tray_data.xembed_data.current->is_xembed_accepts_focus));
            rc = XSendEvent(tray_data.dpy, tray_data.xembed_data.current->wid,
                False, NoEventMask, &ev);
            if (!x11_ok() || rc == 0) {
                tray_data.xembed_data.current->is_invalid = True;
                return;
            }
            LOG_TRACE(("sent key event to 0x%lx\n",
                tray_data.xembed_data.current->wid));
        }
        break;
    }
}

int xembed_check_support(struct TrayIcon *ti)
{
    int rc = xembed_retrive_data(ti);
    ti->is_xembed_supported = (rc == XEMBED_RESULT_OK);
    return rc != XEMBED_RESULT_X11ERROR;
}

int xembed_get_mapped_state(struct TrayIcon *ti)
{
    /* It is OK to retrive data each time this function
     * is called, since there is some overhead only during
     * initialization, when xembed_retrive_data is called 2
     * times in a row(). */
    int rc = xembed_retrive_data(ti);
    if (ti->is_xembed_supported && rc == XEMBED_RESULT_OK)
        return ((ti->xembed_data[1] & XEMBED_MAPPED) != 0);
    else {
        ti->is_xembed_supported = False;
        ti->is_invalid = (rc == XEMBED_RESULT_X11ERROR);
        return False;
    }
}

int xembed_set_mapped_state(struct TrayIcon *ti, int state)
{
    if (!ti->is_xembed_supported) return FAILURE;
    if (state)
        ti->xembed_data[1] |= XEMBED_MAPPED;
    else
        ti->xembed_data[1] &= ~XEMBED_MAPPED;
    return xembed_post_data(ti);
}

int xembed_embed(struct TrayIcon *ti)
{
    /* if XEMBED is not supported, do nothing */
    if (!ti->is_xembed_supported) return SUCCESS;
    /* By default, consider that all icons accept focus */
    ti->is_xembed_accepts_focus = True;
    /* Send notification */
    if (!xembed_send_embedded_notify(tray_data.dpy, tray_data.tray, ti->wid,
            tray_data.xembed_data.timestamp))
        return FAILURE;
    ti->xembed_last_timestamp = tray_data.xembed_data.timestamp;
    ti->xembed_last_msgid = XEMBED_EMBEDDED_NOTIFY;
    if (tray_data.xembed_data.current == NULL) {
        /* No icon has focus. Set focus to this one */
        if (!xembed_send_focus_in(tray_data.dpy, ti->wid, XEMBED_FOCUS_FIRST,
                tray_data.xembed_data.timestamp))
            return FAILURE;
        tray_data.xembed_data.current = ti;
    }
    /* Send activation message if tray window has focus */
    if (tray_data.xembed_data.window_has_focus)
        return xembed_send_window_activate(
            tray_data.dpy, ti->wid, tray_data.xembed_data.timestamp);
    return SUCCESS;
}

int xembed_unembed(struct TrayIcon *ti)
{
    struct TrayIcon *tmp;
    tray_data.xembed_data.timestamp =
        x11_get_server_timestamp(tray_data.dpy, tray_data.tray);
    if (ti == tray_data.xembed_data.current) {
        /* Currently focused icon is being unembedded,
         * move focus to the next icon. */
        tmp = xembed_next();
        if (tmp == ti || tmp->is_xembed_accepts_focus == False) {
            xembed_switch_focus_to(NULL, 0);
        } else {
            xembed_switch_focus_to(tmp, XEMBED_FOCUS_FIRST);
        }
    }
    return SUCCESS;
}

/*********** implementation level ***************/
void xembed_switch_focus_to(struct TrayIcon *tgt, long focus)
{
    /* 1. Send "focus out" message to the currently focused icon */
    if (tray_data.xembed_data.current != NULL) {
        LOG_TRACE(("XEMBED focus was removed from icon 0x%lx (pointer %p)\n",
            tray_data.xembed_data.current->wid,
            tray_data.xembed_data.current));
        xembed_send_focus_out(tray_data.dpy,
            tray_data.xembed_data.current->wid,
            tray_data.xembed_data.timestamp);
    }
    /* 2. Send "focus in" message to the icon to be focused */
    if (tgt != NULL) {
        xembed_send_focus_in(
            tray_data.dpy, tgt->wid, focus, tray_data.xembed_data.timestamp);
        LOG_TRACE(("XEMBED focus was set to icon 0x%lx (pointer %p)\n",
            tgt->wid, tgt));
    } else {
        LOG_TRACE(("XEMBED focus was unset\n"));
    }
    tray_data.xembed_data.current = tgt;
}

static int activate = 0;

int broadcast_activate_msg(struct TrayIcon *ti)
{
    if (activate)
        xembed_send_window_activate(
            tray_data.dpy, ti->wid, tray_data.xembed_data.timestamp);
    else
        xembed_send_window_deactivate(
            tray_data.dpy, ti->wid, tray_data.xembed_data.timestamp);
    return NO_MATCH;
}

void xembed_track_focus_change(int in)
{
    if (tray_data.xembed_data.window_has_focus == in) return;
    tray_data.xembed_data.window_has_focus = in;
    activate = in;
    icon_list_forall(&broadcast_activate_msg);
    LOG_TRACE(("XEMBED focus is %s\n", in ? "ON" : "OFF"));
}

void xembed_message(XClientMessageEvent ev)
{
    long msgid;
    LOG_TRACE(("this is an _XEMBED message, window: 0x%lx, timestamp: %lu, "
               "opcode: %lu, \ndetail: 0x%lx, data1 = 0x%lx, data2 = 0x%lx\n",
        ev.window, ev.data.l[0], ev.data.l[1], ev.data.l[2], ev.data.l[3],
        ev.data.l[4]));
#if DEBUG
    if (tray_data.xembed_data.current != NULL)
        LOG_TRACE(("XEMBED focus is in window 0x%lx (pointer %p)\n",
            tray_data.xembed_data.current->wid,
            tray_data.xembed_data.current));
    else
        LOG_TRACE(("XEMBED focus is unset\n"));
#endif
    if (ev.window != tray_data.tray) {
        LOG_TRACE(("inoring _XEMBED message to some other window\n"));
        return;
    }
    /* Update timestamp if necessary */
    if (ev.data.l[0] == CurrentTime)
        ev.data.l[0] = x11_get_server_timestamp(tray_data.dpy, tray_data.tray);
    tray_data.xembed_data.timestamp = ev.data.l[0];
    msgid = ev.data.l[1];
    LOG_TRACE(("_XEMBED message %lu\n", msgid));
    switch (msgid) {
    case XEMBED_REQUEST_FOCUS: xembed_request_focus_from_wm(); break;
    case XEMBED_FOCUS_NEXT:
    case XEMBED_FOCUS_PREV:
        if (tray_data.xembed_data.current != NULL) {
            struct TrayIcon *old_focus, *new_focus;
            old_focus = tray_data.xembed_data.current;
            new_focus =
                (msgid == XEMBED_FOCUS_NEXT) ? xembed_next() : xembed_prev();
            if (new_focus->is_xembed_accepts_focus) {
                /* If the last message for the new focus target was
                 * focus_{next,prev} and it has the same timestamp as the
                 * current message, it is likely that the corresponding icon
                 * does not want to be focused at all. So mark it as not
                 * accepting focus. */
                if (new_focus->xembed_last_timestamp
                        == tray_data.xembed_data.timestamp
                    && (new_focus->xembed_last_msgid == XEMBED_FOCUS_NEXT
                        || new_focus->xembed_last_msgid
                            == XEMBED_FOCUS_PREV)) {
                    new_focus->is_xembed_accepts_focus = False;
                    new_focus = False;
                }
                old_focus->xembed_last_timestamp =
                    tray_data.xembed_data.timestamp;
                old_focus->xembed_last_msgid = msgid;
            } else
                new_focus = NULL;
            xembed_switch_focus_to(new_focus,
                (msgid == XEMBED_FOCUS_NEXT) ? XEMBED_FOCUS_FIRST
                                             : XEMBED_FOCUS_LAST);
        }
        break;
    case XEMBED_REGISTER_ACCELERATOR:
        xembed_add_accel(ev.data.l[2], ev.data.l[3], ev.data.l[4]);
        break;
    case XEMBED_UNREGISTER_ACCELERATOR: xembed_del_accel(ev.data.l[2]); break;
    default:
        LOG_TRACE(("Unhandled _XEMBED message, id = %ld\n", ev.data.l[1]));
        break;
    }
}

void xembed_add_accel(long id, long symb, long mods)
{
    struct XEMBEDAccel *xaccel, *tmp;
    xaccel = (struct XEMBEDAccel *)malloc(sizeof(struct XEMBEDAccel));
    if (xaccel == NULL) {
        LOG_ERR_OOM(("Could not register new XEMBED accelerator\n"));
        return;
    }
    xaccel->id = id;
    xaccel->symb = symb;
    xaccel->mods = mods;
    xaccel->overloaded = 0;
    /* Check if there are already registered accelerators that are overloaded
     * by this one */
    for (tmp = tray_data.xembed_data.accels; tmp != NULL; tmp = tmp->next)
        if (tmp->symb == symb && tmp->mods == mods) {
            xaccel->overloaded++;
            tmp->overloaded++;
        }
    LIST_ADD_ITEM(tray_data.xembed_data.accels, xaccel);
    LOG_TRACE(("added new XEMBED accelerator: id=0x%lx, sym=0x%lx, mods=0x%lx, "
               "overloaded=%d\n",
        id, symb, mods, xaccel->overloaded));
}

void xembed_del_accel(long id)
{
    struct XEMBEDAccel *tmp, *tgt = NULL;
    for (tmp = tray_data.xembed_data.accels; tmp != NULL; tmp = tmp->next)
        if (tmp->id == id) {
            tgt = tmp;
            return;
        }
    if (tgt == NULL) {
        LOG_TRACE(("refusing to remove unregistered XEMBED accelerator\n"));
        return;
    }
    /* Update overloaded status of the remaining accelerators */
    for (tmp = tray_data.xembed_data.accels; tmp != NULL; tmp = tmp->next)
        if (tmp->symb == tgt->symb && tmp->mods == tgt->mods)
            tmp->overloaded--;
    LIST_DEL_ITEM(tray_data.xembed_data.accels, tgt);
    LOG_TRACE(("removed XEMBED accelator id=0x%lx", tgt->id));
    free(tgt);
}

static struct XEMBEDAccel *cur_accel;

int xembed_act_accel_helper(struct TrayIcon *ti)
{
    xembed_send_activate_accelerator(tray_data.dpy, ti->wid,
        tray_data.xembed_data.timestamp, cur_accel->id,
        cur_accel->overloaded ? 1 : 0);
    return NO_MATCH;
}

void xembed_act_accel(struct XEMBEDAccel *accel)
{
    LOG_TRACE(
        ("activating XEMBED accelerator: id=0x%lx (symb=0x%lx, mods=0x%lx)\n",
            accel->id, accel->symb, accel->mods));
    cur_accel = accel;
    icon_list_forall(&xembed_act_accel_helper);
}

int xembed_process_kbd_event(XKeyEvent xkey)
{
    struct XEMBEDAccel *tmp;
    int hits = 0;
    KeySym keysym;
    static char buf[20];
    XLookupString(&xkey, buf, 20, &keysym, NULL);
    LOG_TRACE(
        ("Key event (type=%d) with keycode=0x%x, symb=0x%lx, state=0x%x\n",
            xkey.type, xkey.keycode, keysym, xkey.state));
    for (tmp = tray_data.xembed_data.accels; tmp != NULL; tmp = tmp->next)
        if ((unsigned long) tmp->symb == keysym && tmp->mods == xkey.state) {
            xembed_act_accel(tmp);
            hits = 1;
        }
    return hits;
}

struct TrayIcon *xembed_next()
{
    struct TrayIcon *tmp, *blocker;
    tmp = tray_data.xembed_data.current != NULL ? tray_data.xembed_data.current
                                                : NULL;
    blocker = tmp != NULL ? tmp : icon_list_next(NULL);
    do {
        tmp = layout_next(tmp);
    } while ((!tmp->is_xembed_supported || !tmp->is_xembed_accepts_focus)
        && tmp != blocker);
    return tmp;
}

struct TrayIcon *xembed_prev()
{
    struct TrayIcon *tmp, *blocker;
    tmp = tray_data.xembed_data.current != NULL ? tray_data.xembed_data.current
                                                : NULL;
    blocker = tmp != NULL ? tmp : icon_list_prev(tmp);
    do {
        tmp = layout_prev(tmp);
    } while ((!tmp->is_xembed_supported || !tmp->is_xembed_accepts_focus)
        && tmp != blocker);
    return tmp;
}

int xembed_retrive_data(struct TrayIcon *ti)
{
    Atom act_type;
    int act_fmt;
    unsigned long nitems, bytesafter, *data;
    unsigned char *tmpdata;
    int rc;
    /* NOTE: x11_get_win_prop32 is not used since we need to distinguish
     * between X11 errors and absence of the property */
    rc = XGetWindowProperty(tray_data.dpy, ti->wid,
        tray_data.xembed_data.xa_xembed_info, 0, 2, False,
        tray_data.xembed_data.xa_xembed_info, &act_type, &act_fmt, &nitems,
        &bytesafter, &tmpdata);
    if (!x11_ok() || rc != Success) return XEMBED_RESULT_X11ERROR;
    rc = (act_type == tray_data.xembed_data.xa_xembed_info && nitems == 2);
    if (rc) {
        data = (unsigned long *)tmpdata;
        ti->xembed_data[0] = data[0];
        ti->xembed_data[1] = data[1];
    }
    if (nitems && tmpdata != NULL) XFree(tmpdata);
    return rc ? XEMBED_RESULT_OK : XEMBED_RESULT_UNSUPPORTED;
}

int xembed_post_data(struct TrayIcon *ti)
{
    if (!ti->is_xembed_supported) return XEMBED_RESULT_UNSUPPORTED;
    XChangeProperty(tray_data.dpy, ti->wid,
        tray_data.xembed_data.xa_xembed_info,
        tray_data.xembed_data.xa_xembed_info, 32, PropModeReplace,
        (unsigned char *)ti->xembed_data, 2);
    return x11_ok() ? XEMBED_RESULT_OK : XEMBED_RESULT_X11ERROR;
}

void xembed_request_focus_from_wm()
{
    if (!tray_data.is_reparented) {
        x11_send_client_msg32(tray_data.dpy, DefaultRootWindow(tray_data.dpy),
            tray_data.tray,
            XInternAtom(tray_data.dpy, "_NET_ACTIVE_WINDOW", True),
            1, /* Request is from application */
            x11_get_server_timestamp(
                tray_data.dpy, tray_data.tray), /* Timestamp */
            0, /* None window is focused current (?) */
            0, /* Unused */
            0); /* Unused */
        tray_data.xembed_data.focus_requested = True;
    }
}
