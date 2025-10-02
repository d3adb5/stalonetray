/* -------------------------------
 * vim:tabstop=4:shiftwidth=4
 * icons.h
 * Tue, 24 Aug 2004 12:05:38 +0700
 * -------------------------------
 * Manipulations with the list of
 * tray icons
 * -------------------------------*/

#ifndef _ICONS_H_
#define _ICONS_H_

#include <X11/X.h>
#include <X11/Xmd.h>

/* Simple point & rect data structures */
struct Point {
    int x, y;
};
struct Rect {
    int x, y, w, h;
};

/* Tray icon layout data structure */
struct Layout {
    struct Rect grd_rect; /* The rect in the grid */
    struct Rect icn_rect; /* Real position inside the tray */
    struct Point wnd_sz; /* Size of the window of the icon */
};

/* Tray icon data structure */
struct TrayIcon {
    struct TrayIcon *next;
    struct TrayIcon *prev;
    Window wid; /* Window ID */
    Window mid_parent; /* Mid-parent ID */
    int cmode; /* Compatibility mode: CM_FDO/CM_KDE (see embed.h) */
    int is_embedded; /* Flag: is the icon succesfully embedded ? */
    int is_invalid; /* Flag: is the icon invalid ? */
    int is_visible; /* Flag: is the icon hidden ? */
    int is_resized; /* Flag: the icon has recently resized itself */
    int is_layed_out; /* Flag: the icon is succesfully layed out */
    int is_updated; /* Flag: the position of the icon needs to be updated */
    int is_xembed_supported; /* Flag: does the icon support xembed */
    unsigned long xembed_data[2]; /* XEMBED data */
    int num_size_resets; /* How many times size was reset */
    int is_size_set; /* Flag: has the size for the icon been set */
    int is_xembed_accepts_focus; /* Flag: does the icon want focus */
    long xembed_last_timestamp; /* The timestamp of last processed xembed
                                   message */
    long xembed_last_msgid; /* ID of the last processed xembed message */
    struct Layout l; /* Layout info */
};

/* Typedef for comparison function */
typedef int (*IconCmpFunc)(struct TrayIcon *, struct TrayIcon *);

/* Typedef for callback function */
typedef int (*IconCallbackFunc)(struct TrayIcon *);

/* Add the new icon to the list */
struct TrayIcon *icon_list_new(Window w, int cmode);

/* Delete the icon from the list */
int icon_list_free(struct TrayIcon *ti);

/* Return the next/previous icon in the list after the icon specified by ti */
struct TrayIcon *icon_list_next(struct TrayIcon *ti);
struct TrayIcon *icon_list_prev(struct TrayIcon *ti);

/*************************************************
 * BIG FAT  WARNING: backup/restore routines  will
 * memleak/fail  if  the  number  of icons in  the
 * list has changed between  backup/restore calls.
 * (in return, it does not invalidate pointers :P)
 *************************************************/

/* Back up the list */
int icon_list_backup();

/* Restore the list from the backup */
int icon_list_restore();

/* Free the back-up list */
int icon_list_backup_purge();

/* Apply a callback specified by cbk to all icons.
 * List is traversed in a natural order. Function stops
 * and returns current_icon if cbk(current_icon) == MATCH */
struct TrayIcon *icon_list_forall(IconCallbackFunc cbk);
/* For readability sake, we sometimes use this function */
#define icon_list_advanced_find icon_list_forall

/* Same as above, but start traversal from the icon specified by tgt */
struct TrayIcon *icon_list_forall_from(
    struct TrayIcon *tgt, IconCallbackFunc cbk);

/* Clear the whole list */
int icon_list_clean();

/* Clear the whole list, calling cbk for each icon */
int icon_list_clean_callback(IconCallbackFunc cbk);

/* Sort the list using comparison function specified by cmp.
 * Memo for writing comparison functions:
 * if a < b => cmp(a,b) < 0
 * if a = b => cmp(a,b) = 0
 * if a > b => cmp(a,b) > 0 */
void icon_list_sort(IconCmpFunc cmp);

/* Find the icon with wid == w */
struct TrayIcon *icon_list_find(Window w);

/* Find the icon with wid == w or parent wid == w */
struct TrayIcon *icon_list_find_ex(Window w);

#endif
