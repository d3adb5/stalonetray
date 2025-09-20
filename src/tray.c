/* ************************************
 * vim:tabstop=4:shiftwidth=4:cindent:fen:fdm=syntax
 * tray.c
 * Tue, 07 Mar 2006 10:36:10 +0600
 * ************************************
 * tray functions
 * ************************************/

#include "config.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xmd.h>
#include <X11/Xutil.h>

#ifdef XPM_SUPPORTED
#include <X11/xpm.h>
#endif

#include <unistd.h>

#include "common.h"
#include "embed.h"
#include "icons.h"
#include "image.h"
#include "settings.h"
#include "tray.h"
#include "wmh.h"
#include "xutils.h"

#ifndef NO_NATIVE_KDE
#endif

#include "debug.h"

void tray_init()
{
    tray_data.tray = None;
    tray_data.hint_win = None;
    tray_data.dpy = NULL;
    tray_data.terminated = False;
    tray_data.bg_pmap = None;
    tray_data.xa_xrootpmap_id = None;
    tray_data.xa_xsetroot_id = None;
    tray_data.kde_tray_old_mode = 0;

#ifdef XINERAMA_SUPPORTED
    tray_data.xinerama_active = False;
    tray_data.n_monitors = 0;
    tray_data.monitors = NULL;
#endif

    scrollbars_init();
}

#ifdef XPM_SUPPORTED
int tray_init_pixmap_bg()
{
    XpmAttributes xpma;
    Pixmap mask = None;
    int rc;
    xpma.valuemask = 0;
    rc = XpmReadFileToPixmap(tray_data.dpy, tray_data.tray,
        settings.bg_pmap_path, &tray_data.bg_pmap, &mask, &xpma);
    if (rc != XpmSuccess) {
        DIE(("Could not read background pixmap from %s.\n",
            settings.bg_pmap_path));
        return FAILURE;
    }
    /* Ignore the mask */
    if (mask != None) XFreePixmap(tray_data.dpy, mask);
    tray_data.bg_pmap_dims.x = xpma.height;
    tray_data.bg_pmap_dims.y = xpma.width;
    LOG_TRACE(
        ("created background pixmap (%dx%d)\n", xpma.width, xpma.height));
    return SUCCESS;
}
#endif

/* Mostly from FVWM:fvwm/colorset.c:172 */
Pixmap tray_get_root_pixmap(Atom prop)
{
    Atom type;
    int format;
    unsigned long length, after;
    unsigned char *reteval = NULL;
    int ret;
    Pixmap pix = None;
    Window root_window =
        XRootWindow(tray_data.dpy, DefaultScreen(tray_data.dpy));
    ret = XGetWindowProperty(tray_data.dpy, root_window, prop, 0L, 1L, False,
        XA_PIXMAP, &type, &format, &length, &after, &reteval);
    if (x11_ok() && ret == Success && type == XA_PIXMAP && format == 32
        && length == 1 && after == 0)
        pix = (Pixmap)(*(long *)reteval);
    if (reteval) XFree(reteval);
    return pix;
}

/* Originally from FVWM:fvwm/colorset.c:195 */
int tray_update_root_bg_pmap(Pixmap *pmap, int *width, int *height)
{
    unsigned int w = 0, h = 0;
    int rc = 0;
    XID dummy;
    Pixmap pix = None;
    /* Retrive root image pixmap */
    /* Try _XROOTPMAP_ID first */
    if (tray_data.xa_xrootpmap_id != None)
        pix = tray_get_root_pixmap(tray_data.xa_xrootpmap_id);
    /* Else try _XSETROOT_ID */
    if (!pix && tray_data.xa_xsetroot_id != None)
        pix = tray_get_root_pixmap(tray_data.xa_xsetroot_id);
    /* Get root pixmap size */
    if (pix)
        rc = XGetGeometry(tray_data.dpy, pix, &dummy, (int *)&dummy,
            (int *)&dummy, &w, &h, (unsigned int *)&dummy,
            (unsigned int *)&dummy);
    if (!x11_ok() || !pix || !rc) {
        LOG_TRACE(("could not update root background pixmap\n"));
        return FAILURE;
    }
    *pmap = pix;
    if (width != NULL) *width = w;
    if (height != NULL) *height = h;
    LOG_TRACE(("got new root pixmap: 0x%lx (%ix%i)\n", pix, w, h));
    return SUCCESS;
}

/* XXX: most of Pixmaps are not really needed. Use XImages instead.
 * GCs, XImages and Pixmaps stay allocated during cleanup, valgrind
 * complains. Who cares?
 * TODO: move pixmaps/GCs into tray_data and free them during the exit. */
int tray_update_bg(int update_pixmap)
{
    static Pixmap root_pmap = None;
    static Pixmap bg_pmap = None;
    static Pixmap final_pmap = None;
    static GC root_gc = None;
    static GC bg_gc = None;
    static GC final_gc = None;
    static int old_width = -1, old_height = -1;
    struct Rect clr; /* clipping rectangle */
    struct Rect clr_loc; /* clipping rectangle in local coords */
    XImage *bg_img;
    int bg_pmap_updated = False;
#define make_gc(pmap, gc) \
    do { \
        XGCValues gcv; \
        if (gc != None) XFreeGC(tray_data.dpy, gc); \
        gcv.graphics_exposures = False; \
        gc = XCreateGC(tray_data.dpy, pmap, GCGraphicsExposures, &gcv); \
    } while (0)
#define make_pmap(pmap) \
    do { \
        if (pmap != None) XFreePixmap(tray_data.dpy, pmap); \
        pmap = XCreatePixmap(tray_data.dpy, tray_data.tray, \
            tray_data.xsh.width, tray_data.xsh.height, \
            DefaultDepth(tray_data.dpy, DefaultScreen(tray_data.dpy))); \
    } while (0)
#define recreate_pixmap(pmap, gc) \
    do { \
        make_pmap(pmap); \
        make_gc(pmap, gc); \
    } while (0)
    /* No need to update background if it is a color one */
    if (!settings.transparent && !settings.pixmap_bg) return SUCCESS;
    /* Calculate clipping rect */
    clr.x = cutoff(tray_data.xsh.x, 0, tray_data.root_wnd.width);
    clr.y = cutoff(tray_data.xsh.y, 0, tray_data.root_wnd.height);
    clr.w = cutoff(tray_data.xsh.x + tray_data.xsh.width, 0,
                tray_data.root_wnd.width)
        - clr.x;
    clr.h = cutoff(tray_data.xsh.y + tray_data.xsh.height, 0,
                tray_data.root_wnd.height)
        - clr.y;
    /* There's no need to update background if tray is not visible */
    /* TODO: visibility is better to be tracked using some events */
    if (clr.w == 0 || clr.h == 0) return SUCCESS;
    /* Calculate local clipping rect */
    clr_loc.x = clr.x - tray_data.xsh.x;
    clr_loc.y = clr.y - tray_data.xsh.y;
    clr_loc.w = clr.w;
    clr_loc.h = clr.h;
    if (old_width != tray_data.xsh.width || old_height != tray_data.xsh.height
        || final_pmap == None)
        recreate_pixmap(final_pmap, final_gc);
    /* Update root pixmap if asked and necessary */
    if ((root_pmap == None || update_pixmap)
        && (settings.transparent || settings.fuzzy_edges)) {
        if (!tray_update_root_bg_pmap(&root_pmap, NULL, NULL)) {
            /* More gracefull solution */
            LOG_TRACE(("still waiting for root pixmap\n"));
            return SUCCESS;
        } else
            make_gc(root_pmap, root_gc);
    }
    /* We don't have to update background if only window position has
     * changed unless we depend on root pixmap */
    if (tray_data.xsh.width == old_width && tray_data.xsh.width == old_height
        && !settings.transparent && !settings.fuzzy_edges) {
        return SUCCESS;
    }
    /* If pixmap bg is used, bg_pmap holds tinted and tiled background pixmap,
     * so there's no need to update it unless window size has changed */
    if (settings.pixmap_bg
        && (bg_pmap == None || old_width != tray_data.xsh.width
            || old_height != tray_data.xsh.height)) {
        int i, j;
        recreate_pixmap(bg_pmap, bg_gc);
        for (i = 0; i < tray_data.xsh.width / tray_data.bg_pmap_dims.x + 1;
             i++)
            for (j = 0;
                 j < tray_data.xsh.height / tray_data.bg_pmap_dims.y + 1;
                 j++) {
                XCopyArea(tray_data.dpy, tray_data.bg_pmap, bg_pmap, bg_gc, 0,
                    0, tray_data.bg_pmap_dims.x, tray_data.bg_pmap_dims.y,
                    i * tray_data.bg_pmap_dims.x,
                    j * tray_data.bg_pmap_dims.y);
            }
        bg_pmap_updated = True;
    } else
        /* If root transparency is enabled, it is neccessary to copy portion of
         * root pixmap under the window (root_pmap) to bg_pmap */
        /* XXX: must correctly work around situations when bg pixmap is smaller
           than root window (but how?) */
        if (settings.transparent) {
        recreate_pixmap(bg_pmap, bg_gc);
        XCopyArea(tray_data.dpy, root_pmap, bg_pmap, bg_gc, tray_data.xsh.x,
            tray_data.xsh.y, tray_data.xsh.width, tray_data.xsh.height, 0, 0);
    }
    /* Create an XImage from bg_pmap */
    bg_img = XGetImage(tray_data.dpy, bg_pmap, 0, 0, tray_data.xsh.width,
        tray_data.xsh.height, XAllPlanes(), ZPixmap);
    if (bg_img == NULL) return FAILURE;
    /* Tint the image if necessary. If bg_pmap was not updated, tinting
     * is not needed, since it has been already done */
    if (settings.tint_level
        && ((settings.pixmap_bg && bg_pmap_updated) || settings.transparent)) {
        image_tint(bg_img, &settings.tint_color, settings.tint_level);
        XPutImage(tray_data.dpy, bg_pmap, bg_gc, bg_img, 0, 0, 0, 0,
            tray_data.xsh.width, tray_data.xsh.height);
        bg_pmap_updated = False;
    }
    /* Apply fuzzy edges */
    /* XXX: THIS IS UGLY */
    if (settings.fuzzy_edges) {
        static CARD8 *alpha_mask = NULL;
        /* Portion of root pixmap under the tray */
        XImage *root_img = XGetImage(tray_data.dpy, root_pmap, clr.x, clr.y,
            clr.w, clr.h, XAllPlanes(), ZPixmap);
        /* Proxy structure to work on */
        XImage *tmp_img = NULL;
        static Pixmap tmp_pmap = None;
        static GC tmp_gc = None;
        if (root_img == NULL) {
            LOG_ERROR(
                ("Failed to get image of root pixmap under tray window\n"));
            LOG_TRACE(("Clipping rectangle: %dx%d+%d+%d\n", clr.w, clr.h,
                clr.x, clr.y));
            return x11_ok();
        }
        /* Alpha mask needs to be updated only on size changes */
        if (old_width != tray_data.xsh.width
            || old_height != tray_data.xsh.height) {
            recreate_pixmap(tmp_pmap, tmp_gc);
            if (alpha_mask != NULL) free(alpha_mask);
            alpha_mask = image_create_alpha_mask(settings.fuzzy_edges,
                tray_data.xsh.width, tray_data.xsh.height);
        }
        XPutImage(tray_data.dpy, tmp_pmap, tmp_gc, root_img, clr_loc.x,
            clr_loc.y, 0, 0, clr_loc.w, clr_loc.h);
        tmp_img = XGetImage(tray_data.dpy, tmp_pmap, 0, 0, tray_data.xsh.width,
            tray_data.xsh.height, XAllPlanes(), ZPixmap);
        if (alpha_mask != NULL && tmp_img != NULL)
            image_compose(bg_img, tmp_img, alpha_mask);
        XDestroyImage(root_img);
        if (tmp_img != NULL) XDestroyImage(tmp_img);
    }
    XPutImage(tray_data.dpy, final_pmap, final_gc, bg_img, 0, 0, 0, 0,
        tray_data.xsh.width, tray_data.xsh.height);
    XSetWindowBackgroundPixmap(tray_data.dpy, tray_data.tray, final_pmap);
    XDestroyImage(bg_img);
    old_width = tray_data.xsh.width;
    old_height = tray_data.xsh.height;
    RETURN_STATUS(x11_ok());
}

void tray_refresh_window(int exposures)
{
    LOG_TRACE(("refreshing tray window\n"));
    icon_list_forall(&embedder_refresh);
    x11_refresh_window(tray_data.dpy, tray_data.tray, tray_data.xsh.width,
        tray_data.xsh.height, exposures);
    scrollbars_refresh(exposures);
}

int tray_calc_window_size(
    int base_width, int base_height, int *wnd_width, int *wnd_height)
{
    *wnd_width = base_width;
    *wnd_height = base_height;
    if (settings.scrollbars_mode & SB_MODE_HORZ)
        *wnd_width += settings.scrollbars_size * 2;
    if (settings.scrollbars_mode & SB_MODE_VERT)
        *wnd_height += settings.scrollbars_size * 2;
    return SUCCESS;
}

int tray_calc_tray_area_size(
    int wnd_width, int wnd_height, int *base_width, int *base_height)
{
    *base_width = wnd_width;
    *base_height = wnd_height;
    if (settings.scrollbars_mode & SB_MODE_HORZ)
        *base_width -= settings.scrollbars_size * 2;
    if (settings.scrollbars_mode & SB_MODE_VERT)
        *base_height -= settings.scrollbars_size * 2;
    return SUCCESS;
}

int tray_update_window_strut()
{
    /* Set window strut */
    if (settings.wm_strut_mode != WM_STRUT_NONE) {
        int strut_mode;
        if (settings.wm_strut_mode == WM_STRUT_AUTO) {
            /* Do autodetection: if some edge of tray is adjacent to one
             * of screen edges, we could set window strut to that */
            int h_strut_mode, v_strut_mode;
            int p_strut_mode, s_strut_mode;
            h_strut_mode =
                (tray_data.xsh.x == 0 ? WM_STRUT_LFT
                                      : (tray_data.xsh.x + tray_data.xsh.width
                                                  == tray_data.root_wnd.width
                                              ? WM_STRUT_RHT
                                              : WM_STRUT_NONE));
            v_strut_mode =
                (tray_data.xsh.y == 0 ? WM_STRUT_TOP
                                      : (tray_data.xsh.x + tray_data.xsh.height
                                                  == tray_data.root_wnd.height
                                              ? WM_STRUT_BOT
                                              : WM_STRUT_NONE));
            /* If tray is vertical, horizontal strut mode has higher priority,
             * else vertical strut mode has higher priority */
            if (settings.vertical) {
                p_strut_mode = h_strut_mode;
                s_strut_mode = v_strut_mode;
            } else {
                p_strut_mode = v_strut_mode;
                s_strut_mode = h_strut_mode;
            }
            if (p_strut_mode != WM_STRUT_NONE)
                strut_mode = p_strut_mode;
            else
                strut_mode = s_strut_mode;
        } else
            strut_mode = settings.wm_strut_mode;
        LOG_TRACE(("computed final strut mode: %d\n", strut_mode));
        /* Update respective window hint */
        if (strut_mode != WM_STRUT_NONE) {
            wm_strut_t wm_strut;
            int base_idx;
            memset(wm_strut, 0, sizeof(wm_strut));
            LOG_TRACE(
                ("current tray geometry: %dx%d+%d+%d\n", tray_data.xsh.width,
                    tray_data.xsh.height, tray_data.xsh.x, tray_data.xsh.y));
            if (strut_mode == WM_STRUT_TOP || strut_mode == WM_STRUT_BOT) {
                if (strut_mode == WM_STRUT_TOP) {
                    base_idx = WM_STRUT_IDX_TOP;
                    wm_strut[WM_STRUT_IDX_TOP] =
                        tray_data.xsh.y + tray_data.xsh.height;
                } else {
                    base_idx = WM_STRUT_IDX_BOT;
                    wm_strut[WM_STRUT_IDX_BOT] =
                        tray_data.root_wnd.height - tray_data.xsh.y;
                }
                wm_strut[WM_STRUT_IDX_START(base_idx)] = tray_data.xsh.x;
                wm_strut[WM_STRUT_IDX_END(base_idx)] =
                    tray_data.xsh.x + tray_data.xsh.width - 1;
            } else {
                if (strut_mode == WM_STRUT_LFT) {
                    base_idx = WM_STRUT_IDX_LFT;
                    wm_strut[WM_STRUT_IDX_LFT] =
                        tray_data.xsh.x + tray_data.xsh.width;
                } else {
                    base_idx = WM_STRUT_IDX_RHT;
                    wm_strut[WM_STRUT_IDX_RHT] =
                        tray_data.root_wnd.width - tray_data.xsh.x;
                }
                wm_strut[WM_STRUT_IDX_START(base_idx)] = tray_data.xsh.y;
                wm_strut[WM_STRUT_IDX_END(base_idx)] =
                    tray_data.xsh.y + tray_data.xsh.height - 1;
            }
            {
                int i;
                for (i = 0; i < _NET_WM_STRUT_PARTIAL_SZ; i++)
                    LOG_TRACE(("computed hints [%d] = %lu\n", i, wm_strut[i]));
            }
            ewmh_set_window_strut(tray_data.dpy, tray_data.tray, wm_strut);
        }
    }
    return SUCCESS;
}

int tray_update_window_props()
{
    XSizeHints xsh;
    int cur_base_width, cur_base_height;
    int new_width, new_height;
    int layout_width, layout_height;
    /* Phase 1: calculate new tray window dimensions.
     * Algorithm summary:
     * new_dims =
     *      if (layout_dims > max_dims) max_dims;
     * else if ((shrink_back && layout_dims > orig_dims) ||
     *          (layout_dims > current_dims)) layout_dims;
     * else if (shrink_back) orig_dims;
     * else                  current_dims;
     */
    x11_get_window_size(tray_data.dpy, tray_data.tray, &tray_data.xsh.width,
        &tray_data.xsh.height);
    layout_get_size(&layout_width, &layout_height);
    LOG_TRACE(("layout geometry: %dx%d\n", layout_width, layout_height));
    tray_calc_tray_area_size(tray_data.xsh.width, tray_data.xsh.height,
        &cur_base_width, &cur_base_height);
    LOG_TRACE(
        ("base tray geometry: %dx%d\n", cur_base_width, cur_base_height));
    LOG_TRACE(("orig tray geometry: %dx%d\n", settings.orig_tray_dims.x,
        settings.orig_tray_dims.y));
    LOG_TRACE(("max tray geometry: %dx%d\n", settings.max_tray_dims.x,
        settings.max_tray_dims.y));
#define CALC_DIM(tgt, cur, layout, max, orig) \
    if (layout > max) \
        tgt = max; \
    else if ((settings.shrink_back_mode && layout > orig) || layout > cur) \
        tgt = layout; \
    else if (settings.shrink_back_mode) \
        tgt = orig; \
    else \
        tgt = cur;
    CALC_DIM(new_width, cur_base_width, layout_width, settings.max_tray_dims.x,
        settings.orig_tray_dims.x);
    CALC_DIM(new_height, cur_base_height, layout_height,
        settings.max_tray_dims.y, settings.orig_tray_dims.y);
    LOG_TRACE(("new base tray geometry: %dx%d\n", new_width, new_height));
    tray_calc_window_size(new_width, new_height, &new_width, &new_height);
    LOG_TRACE(("new tray geometry: %dx%d\n", new_width, new_height));
#if 1
    /* Not sure if this is really necessary */
    xsh.x = tray_data.xsh.x;
    xsh.y = tray_data.xsh.y;
    xsh.width = new_width;
    xsh.height = new_height;
#endif
    xsh.min_width = new_width;
    xsh.min_height = new_height;
    xsh.max_width = new_width;
    xsh.max_height = new_height;
    xsh.width_inc = settings.slot_size.x;
    xsh.height_inc = settings.slot_size.y;
    tray_calc_window_size(0, 0, &xsh.base_width, &xsh.base_height);
    xsh.win_gravity = NorthWestGravity;
    xsh.flags = tray_data.xsh.flags;
    XSetWMNormalHints(tray_data.dpy, tray_data.tray, &xsh);
    /* Phase 2: set new window size
     * This check helps to avod extra (erroneous) moves of the window when
     * geometry changes are not updated yet, but tray_update_window_props() was
     * called once again */
    if (new_width != tray_data.xsh.width
        || new_height != tray_data.xsh.height) {
        /* Apparently, not every WM (hello, WindowMaker!) handles gravity the
         * way I have expected (i.e. using it to calculate reference point as
         * described in ICCM/WM specs). Perhaps, I was dreaming.  So, prior to
         * resizing trays window, it is necessary to recalculate window
         * absolute position and shift it according to grow gravity settings */
        x11_get_window_abs_coords(
            tray_data.dpy, tray_data.tray, &tray_data.xsh.x, &tray_data.xsh.y);
        LOG_TRACE(("old tray window geometry: %dx%d+%d+%d\n", new_width,
            new_height, tray_data.xsh.x, tray_data.xsh.y));
        if (settings.grow_gravity & GRAV_E)
            tray_data.xsh.x -= new_width - tray_data.xsh.width;
        else if (!(settings.grow_gravity & GRAV_H))
            tray_data.xsh.x -= (new_width - tray_data.xsh.width) / 2;
        if (settings.grow_gravity & GRAV_S)
            tray_data.xsh.y -= new_height - tray_data.xsh.height;
        else if (!(settings.grow_gravity & GRAV_V))
            tray_data.xsh.y -= (new_height - tray_data.xsh.height) / 2;
        tray_data.xsh.width = new_width;
        tray_data.xsh.height = new_height;
        LOG_TRACE(("new tray window geometry: %dx%d+%d+%d\n", new_width,
            new_height, tray_data.xsh.x, tray_data.xsh.y));
        XResizeWindow(tray_data.dpy, tray_data.tray, new_width, new_height);
        XMoveWindow(
            tray_data.dpy, tray_data.tray, tray_data.xsh.x, tray_data.xsh.y);
        if (!x11_ok()) {
            LOG_TRACE(("could not update tray window geometry\n"));
            return FAILURE;
        }
    } else {
        /* XXX: Why do we need this again? */
        XResizeWindow(tray_data.dpy, tray_data.tray, tray_data.xsh.width,
            tray_data.xsh.height);
    }
    tray_update_window_strut();
    scrollbars_update();
    return SUCCESS;
}

void tray_create_window(int argc, char **argv)
{
    XTextProperty wm_name;
    XSetWindowAttributes xswa;
    XClassHint xch;
    XWMHints xwmh;
    Atom net_system_tray_orientation;
    Atom orient;
    Atom protocols_atoms[3];
    /* Create some atoms */
    tray_data.xa_wm_delete_window =
        XInternAtom(tray_data.dpy, "WM_DELETE_WINDOW", False);
    tray_data.xa_net_wm_ping =
        XInternAtom(tray_data.dpy, "_NET_WM_PING", False);
    tray_data.xa_wm_take_focus =
        XInternAtom(tray_data.dpy, "WM_TAKE_FOCUS", False);
    tray_data.xa_wm_protocols =
        XInternAtom(tray_data.dpy, "WM_PROTOCOLS", False);
    tray_data.xa_kde_net_system_tray_windows =
        XInternAtom(tray_data.dpy, "_KDE_NET_SYSTEM_TRAY_WINDOWS", False);
    tray_data.xa_net_client_list =
        XInternAtom(tray_data.dpy, "_NET_CLIENT_LIST", False);
    /* Create tray window */
    tray_data.tray = XCreateSimpleWindow(tray_data.dpy,
        DefaultRootWindow(tray_data.dpy), tray_data.xsh.x, tray_data.xsh.y,
        tray_data.xsh.width, tray_data.xsh.height, 0, settings.bg_color.pixel,
        settings.bg_color.pixel);
    LOG_TRACE(("created tray window: 0x%lx\n", tray_data.tray));
    if (settings.dockapp_mode == DOCKAPP_WMAKER) {
        tray_data.hint_win = XCreateSimpleWindow(tray_data.dpy,
            DefaultRootWindow(tray_data.dpy), 0, 0, 1, 1, 0,
            settings.bg_color.pixel, settings.bg_color.pixel);
        LOG_TRACE(("created hint_win window: 0x%lx\n", tray_data.hint_win));
    }
    /* Set tray window properties */
    xswa.bit_gravity = settings.bit_gravity;
    xswa.win_gravity = settings.win_gravity;
    xswa.backing_store = settings.parent_bg ? NotUseful : WhenMapped;
    XChangeWindowAttributes(tray_data.dpy, tray_data.tray,
        CWBitGravity | CWWinGravity | CWBackingStore, &xswa);
    {
        /* XXX: use XStoreName ?*/
        int numtries = 0;
        /* First, try user-supplied value */
        while (1) {
            if (XmbTextListToTextProperty(
                    tray_data.dpy, &settings.wnd_name, 1, XTextStyle, &wm_name)
                != Success)
                /* Retry with default value */
                settings.wnd_name = PROGNAME;
            else
                break;
            if (numtries++ > 1)
                DIE(("Invalid window name \"%s\"\n", settings.wnd_name));
        }
    }
    XSetWMName(tray_data.dpy, tray_data.tray, &wm_name);
    XFree(wm_name.value);
    /* Setup class hints */
    xch.res_class = PROGNAME;
    xch.res_name = PROGNAME;
    /* Setup window manager hints */
    xwmh.flags = StateHint | InputHint;
    xwmh.input = False;
    xwmh.initial_state =
        settings.dockapp_mode != DOCKAPP_NONE ? WithdrawnState : NormalState;
    /* Apply hints */
    XSetWMHints(tray_data.dpy, tray_data.tray, &xwmh);
    XSetClassHint(tray_data.dpy, tray_data.tray, &xch);
    XSetWMNormalHints(tray_data.dpy, tray_data.tray, &tray_data.xsh);
    XSetCommand(tray_data.dpy, tray_data.tray, argv, argc);
    /* Set properties of hint window if WindowMaker dockapp mode enabled */
    if (settings.dockapp_mode == DOCKAPP_WMAKER) {
        xwmh.flags |= IconWindowHint | IconPositionHint | WindowGroupHint;
        xwmh.initial_state = WithdrawnState;
        xwmh.icon_x = tray_data.xsh.x;
        xwmh.icon_y = tray_data.xsh.y;
        xwmh.icon_window = tray_data.tray;
        xwmh.window_group = tray_data.hint_win;
        XSetClassHint(tray_data.dpy, tray_data.hint_win, &xch);
        XSetWMHints(tray_data.dpy, tray_data.hint_win, &xwmh);
    }
    /* v0.2 tray protocol support */
    orient = settings.vertical ? _NET_SYSTEM_TRAY_ORIENTATION_HORZ
                               : _NET_SYSTEM_TRAY_ORIENTATION_VERT;
    net_system_tray_orientation =
        XInternAtom(tray_data.dpy, TRAY_ORIENTATION_ATOM, False);
    XChangeProperty(tray_data.dpy, tray_data.tray, net_system_tray_orientation,
        net_system_tray_orientation, 32, PropModeReplace,
        (unsigned char *)&orient, 1);
    /* Ask X server / WM to report certain events */
    protocols_atoms[0] = tray_data.xa_wm_delete_window;
    protocols_atoms[1] = tray_data.xa_wm_take_focus;
    protocols_atoms[2] = tray_data.xa_net_wm_ping;
    XSetWMProtocols(tray_data.dpy, tray_data.tray, protocols_atoms, 3);
    XSelectInput(tray_data.dpy, tray_data.tray,
        StructureNotifyMask | FocusChangeMask | PropertyChangeMask
            | ExposureMask);
    x11_extend_root_event_mask(tray_data.dpy, PropertyChangeMask);
    scrollbars_create();
    /* Set tray window background if necessary */
#ifdef XPM_SUPPORTED
    if (settings.pixmap_bg) tray_init_pixmap_bg();
#endif
    if (settings.parent_bg)
        XSetWindowBackgroundPixmap(
            tray_data.dpy, tray_data.tray, ParentRelative);
    else if (settings.transparent || settings.fuzzy_edges) {
        tray_data.xa_xrootpmap_id =
            XInternAtom(tray_data.dpy, "_XROOTPMAP_ID", False);
        tray_data.xa_xsetroot_id =
            XInternAtom(tray_data.dpy, "_XSETROOT_ID", False);
    }
    tray_update_bg(True);
}

void tray_create_phony_window()
{
    /* Create fake tray window */
    tray_data.tray = XCreateSimpleWindow(
        tray_data.dpy, DefaultRootWindow(tray_data.dpy), 0, 0, 1, 1, 0, 0, 0);
    /* Select for PropertyNotify so that x11_get_server_timestamp() works */
    XSelectInput(tray_data.dpy, tray_data.tray, PropertyChangeMask);
}

int tray_set_wm_hints()
{
    int mwm_decor = 0;
    if (settings.deco_flags & DECO_TITLE)
        mwm_decor |= MWM_DECOR_TITLE | MWM_DECOR_MENU;
    if (settings.deco_flags & DECO_BORDER)
        mwm_decor |= MWM_DECOR_RESIZEH | MWM_DECOR_BORDER;
    mwm_set_hints(tray_data.dpy, tray_data.tray, mwm_decor, MWM_FUNC_ALL);
    if (settings.sticky) {
        ewmh_add_window_state(
            tray_data.dpy, tray_data.tray, _NET_WM_STATE_STICKY);
        ewmh_set_window_atom32(
            tray_data.dpy, tray_data.tray, _NET_WM_DESKTOP, 0xFFFFFFFF);
    }
    if (settings.skip_taskbar)
        ewmh_add_window_state(
            tray_data.dpy, tray_data.tray, _NET_WM_STATE_SKIP_TASKBAR);
    if (settings.wnd_layer != NULL)
        ewmh_add_window_state(
            tray_data.dpy, tray_data.tray, settings.wnd_layer);
    if (strcmp(settings.wnd_type, _NET_WM_WINDOW_TYPE_NORMAL) != 0)
        ewmh_add_window_type(tray_data.dpy, tray_data.tray, settings.wnd_type);
    /* Alwas add NORMAL window type for WM that do not support (some) special
     * types */
    ewmh_add_window_type(
        tray_data.dpy, tray_data.tray, _NET_WM_WINDOW_TYPE_NORMAL);
    return SUCCESS;
}

void tray_init_selection_atoms()
{
    static char *tray_sel_atom_name = NULL;
    /* Obtain selection atom name basing on current screen number */
    if (tray_sel_atom_name == NULL) {
        tray_sel_atom_name = (char *)malloc(strlen(TRAY_SEL_ATOM) + 10);
        if (tray_sel_atom_name == NULL)
            DIE_OOM(("could not allocate memory for selection atom name\n"));
        snprintf(tray_sel_atom_name, strlen(TRAY_SEL_ATOM) + 10, "%s%u",
            TRAY_SEL_ATOM, DefaultScreen(tray_data.dpy));
    }
    LOG_TRACE(("tray_sel_atom_name=%s\n", tray_sel_atom_name));
    /* Initialize atom values */
    tray_data.xa_tray_selection =
        XInternAtom(tray_data.dpy, tray_sel_atom_name, False);
    tray_data.xa_tray_opcode =
        XInternAtom(tray_data.dpy, "_NET_SYSTEM_TRAY_OPCODE", False);
    tray_data.xa_tray_data =
        XInternAtom(tray_data.dpy, "_NET_SYSTEM_TRAY_MESSAGE_DATA", False);
}

void tray_acquire_selection()
{
    Time timestamp = x11_get_server_timestamp(tray_data.dpy, tray_data.tray);
    tray_init_selection_atoms();
    /* Save old selection owner */
    tray_data.old_selection_owner =
        XGetSelectionOwner(tray_data.dpy, tray_data.xa_tray_selection);
    LOG_TRACE(("old selection owner: 0x%lx\n", tray_data.old_selection_owner));
    /* Acquire selection */
    XSetSelectionOwner(
        tray_data.dpy, tray_data.xa_tray_selection, tray_data.tray, timestamp);
    /* Check if we have really got the selection */
    if (XGetSelectionOwner(tray_data.dpy, tray_data.xa_tray_selection)
        != tray_data.tray) {
        DIE(("could not set selection owner.\nMay be another (greedy) tray "
             "running?\n"));
    } else {
        tray_data.is_active = True;
        LOG_TRACE(("ok, got _NET_SYSTEM_TRAY selection\n"));
    }
    /* Send the message notifying about new MANAGER */
    x11_send_client_msg32(tray_data.dpy, DefaultRootWindow(tray_data.dpy),
        DefaultRootWindow(tray_data.dpy),
        XInternAtom(tray_data.dpy, "MANAGER", False), timestamp,
        tray_data.xa_tray_selection, tray_data.tray, 0, 0);
}

void tray_show_window()
{
    tray_set_wm_hints();
    tray_update_window_props();
    XMapRaised(tray_data.dpy, tray_data.tray);
    if (settings.dockapp_mode == DOCKAPP_NONE)
        XMoveWindow(
            tray_data.dpy, tray_data.tray, tray_data.xsh.x, tray_data.xsh.y);
    if (settings.dockapp_mode == DOCKAPP_WMAKER)
        XMapWindow(tray_data.dpy, tray_data.hint_win);
    /* XXX: I do not why, but for some WM it is
     * required to set hints / window properties
     * after and before window creation */
    /* TODO: check if this is really necessary */
    tray_set_wm_hints();
    tray_update_window_props();
}
