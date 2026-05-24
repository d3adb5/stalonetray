/* Unit tests for settings ownership and reload semantics.
 *
 * These tests exercise the settings module without an X display: settings.c
 * and the rest of core_sources are linked, but tray_data.dpy stays NULL so
 * the X-dependent code paths short-circuit. */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <setjmp.h> /* cmocka requires this before <cmocka.h> */
#include <cmocka.h>

#include "../src/settings.h"
#include "../src/tray.h"

/* Helper: write a temp rc file with the given body and point
 * settings.config_fname at it. Caller is responsible for unlink+free via
 * cleanup_tmp_rc(). */
static char *write_tmp_rc(const char *body)
{
    char *path = strdup("/tmp/stalonetrayrc.test.XXXXXX");
    int fd = mkstemp(path);
    assert_true(fd >= 0);
    ssize_t n = write(fd, body, strlen(body));
    assert_true(n == (ssize_t) strlen(body));
    close(fd);
    return path;
}

static void cleanup_tmp_rc(char *path)
{
    if (path != NULL) {
        unlink(path);
        free(path);
    }
}

/* Reset the global `settings` between tests, returning ownership of all the
 * strdups init_default_settings allocated so the next test starts clean. */
static int reset_settings(void **state)
{
    (void) state;
    free_settings(&settings);
    init_default_settings();
    return 0;
}

static int teardown_settings(void **state)
{
    (void) state;
    free_settings(&settings);
    return 0;
}

/* ---------------------------------------------------------------- defaults */

static void test_init_default_settings_strdups_strings(void **state)
{
    (void) state;
    /* Defaults should be heap pointers, not the static literals previously
     * baked into init_default_settings. The values themselves are still the
     * documented defaults. */
    assert_non_null(settings.bg_color_str);
    assert_string_equal(settings.bg_color_str, "gray");
    assert_non_null(settings.tint_color_str);
    assert_string_equal(settings.tint_color_str, "white");
    assert_non_null(settings.scrollbars_highlight_color_str);
    assert_string_equal(settings.scrollbars_highlight_color_str, "white");
    assert_non_null(settings.max_geometry_str);
    assert_string_equal(settings.max_geometry_str, "0x0");
    assert_non_null(settings.wnd_type);
    assert_non_null(settings.wnd_name);

    /* Some scalar defaults to make sure init isn't a complete no-op. */
    assert_int_equal(settings.sticky, 1);
    assert_int_equal(settings.skip_taskbar, 1);
    assert_int_equal(settings.shrink_back_mode, 1);
}

/* --------------------------------------------------------------- free_settings */

static void test_free_settings_zeroes_struct(void **state)
{
    (void) state;
    free_settings(&settings);
    assert_null(settings.bg_color_str);
    assert_null(settings.tint_color_str);
    assert_null(settings.wnd_type);
    assert_null(settings.wnd_name);
    assert_null(settings.ignored_classes);
    /* Idempotent: a second free shouldn't crash. */
    free_settings(&settings);
}

static void test_free_settings_safe_on_zero_init(void **state)
{
    (void) state;
    /* Equivalent to a fresh on-stack snapshot: every pointer NULL. */
    struct Settings blank = {0};
    free_settings(&blank);  /* must not crash */
    assert_null(blank.bg_color_str);
}

/* --------------------------------------------------------- settings_reload */

/* Cover the round-trip: a reloadable setting in the config takes effect, a
 * non-reloadable setting keeps its pre-reload value, and the out_old handed
 * back can be free_settings()'d without double-freeing the live struct. */
static void test_settings_reload_adopts_reloadable_keeps_others(void **state)
{
    (void) state;
    /* Mutate two reloadable fields and one non-reloadable field so we can
     * verify reload (a) overrides reloadable with the config value and (b)
     * leaves the non-reloadable field alone even when the config touches it. */
    free(settings.bg_color_str);
    settings.bg_color_str = strdup("starting-bg");
    settings.tint_level = 7;
    settings.icon_size = 24;        /* non-reloadable */

    char *rc_path = write_tmp_rc(
        "background blue\n"
        "tint_level 42\n"
        "icon_size 99\n");
    free(settings.config_fname);
    settings.config_fname = strdup(rc_path);

    char *argv[] = {(char *) "test"};
    struct Settings old;
    int rc = settings_reload(1, argv, &old);
    assert_int_equal(rc, 1 /* SUCCESS */);

    /* Reloadable fields adopt the config values. */
    assert_string_equal(settings.bg_color_str, "blue");
    assert_int_equal(settings.tint_level, 42);

    /* Non-reloadable: parse_rc + parse_cmdline skip these on a reload, so the
     * live value remains untouched (NOT 99 from the config). */
    assert_int_equal(settings.icon_size, 24);

    /* out_old keeps the pre-reload reloadable values for the caller's diff. */
    assert_string_equal(old.bg_color_str, "starting-bg");
    assert_int_equal(old.tint_level, 7);

    /* Non-reloadable string slots in out_old must be NULL so this free is
     * safe (otherwise it would alias still-live pointers in `settings`). */
    assert_null(old.display_str);
    assert_null(old.geometry_str);
    assert_null(old.max_geometry_str);
    assert_null(old.config_fname);

    free_settings(&old);
    cleanup_tmp_rc(rc_path);
}

/* On a malformed config we must not crash, must not touch the live settings,
 * and must return FAILURE so the caller skips the apply step. */
static void test_settings_reload_rolls_back_on_syntax_error(void **state)
{
    (void) state;
    free(settings.bg_color_str);
    settings.bg_color_str = strdup("preserved");

    char *rc_path = write_tmp_rc("this_is_not_a_valid_keyword foo\n");
    free(settings.config_fname);
    settings.config_fname = strdup(rc_path);

    char *argv[] = {(char *) "test"};
    struct Settings old;
    int rc = settings_reload(1, argv, &old);
    assert_int_equal(rc, 0 /* FAILURE */);

    /* Live setting is untouched; out_old is safe to free (all NULL). */
    assert_string_equal(settings.bg_color_str, "preserved");
    assert_null(old.bg_color_str);
    free_settings(&old);

    cleanup_tmp_rc(rc_path);
}

/* The unique-ownership invariant: after reload, the previous reloadable
 * heap allocation is owned by out_old (not by settings) so freeing both is
 * safe and doesn't double-free. */
static void test_settings_reload_no_double_free(void **state)
{
    (void) state;
    char *rc_path = write_tmp_rc("background green\n");
    free(settings.config_fname);
    settings.config_fname = strdup(rc_path);

    char *argv[] = {(char *) "test"};
    struct Settings old;
    int rc = settings_reload(1, argv, &old);
    assert_int_equal(rc, 1 /* SUCCESS */);
    /* Same pointer would mean double-free territory. */
    assert_ptr_not_equal(settings.bg_color_str, old.bg_color_str);

    free_settings(&old);
    /* If old.bg_color_str had aliased settings.bg_color_str, the next read
     * would be a use-after-free. We just dereference it to make tools like
     * valgrind / ASan flag the bug if it ever returns. */
    assert_non_null(settings.bg_color_str);
    assert_string_equal(settings.bg_color_str, "green");

    cleanup_tmp_rc(rc_path);
}

/* ------------------------------------------------------------- parse_rc */

/* Convenience: drop a one-liner rc into a temp file and point
 * settings.config_fname at it, run parse_rc(), and clean up. */
static int parse_rc_with_body(const char *body)
{
    char *path = write_tmp_rc(body);
    free(settings.config_fname);
    settings.config_fname = strdup(path);
    int rc = parse_rc();
    cleanup_tmp_rc(path);
    return rc;
}

static void test_parse_rc_string_field(void **state)
{
    (void) state;
    assert_int_equal(parse_rc_with_body("background salmon\n"), 1 /* SUCCESS */);
    assert_string_equal(settings.bg_color_str, "salmon");
}

static void test_parse_rc_int_field(void **state)
{
    (void) state;
    assert_int_equal(parse_rc_with_body("tint_level 123\n"), 1 /* SUCCESS */);
    assert_int_equal(settings.tint_level, 123);
}

static void test_parse_rc_bool_field(void **state)
{
    (void) state;
    /* sticky defaults to 1; verify the parser actually flips it. */
    assert_int_equal(settings.sticky, 1);
    assert_int_equal(parse_rc_with_body("sticky no\n"), 1 /* SUCCESS */);
    assert_int_equal(settings.sticky, 0);
}

static void test_parse_rc_enum_field(void **state)
{
    (void) state;
    /* window_type takes a symbolic name and stores the EWMH atom string. */
    assert_int_equal(
        parse_rc_with_body("window_type utility\n"), 1 /* SUCCESS */);
    assert_string_equal(settings.wnd_type, "_NET_WM_WINDOW_TYPE_UTILITY");
}

static void test_parse_rc_list_field(void **state)
{
    (void) state;
    assert_null(settings.ignored_classes);
    assert_int_equal(
        parse_rc_with_body("ignore_classes Mumble Discord rustdesk\n"),
        1 /* SUCCESS */);

    /* The list is built head-first by LIST_ADD_ITEM, so iteration order is
     * the reverse of the rc file order. Just collect names into a set and
     * make sure each one shows up. */
    int saw_mumble = 0, saw_discord = 0, saw_rustdesk = 0, count = 0;
    for (struct WindowClass *c = settings.ignored_classes; c != NULL;
        c = c->next) {
        count++;
        if (strcmp(c->name, "Mumble") == 0) saw_mumble = 1;
        else if (strcmp(c->name, "Discord") == 0) saw_discord = 1;
        else if (strcmp(c->name, "rustdesk") == 0) saw_rustdesk = 1;
    }
    assert_int_equal(count, 3);
    assert_true(saw_mumble && saw_discord && saw_rustdesk);
}

static void test_parse_rc_ignores_comments_and_blank_lines(void **state)
{
    (void) state;
    int rc = parse_rc_with_body(
        "# leading comment\n"
        "\n"
        "background turquoise\n"
        "    # comment with leading whitespace\n"
        "\n"
        "tint_level 5\n");
    assert_int_equal(rc, 1 /* SUCCESS */);
    assert_string_equal(settings.bg_color_str, "turquoise");
    assert_int_equal(settings.tint_level, 5);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(
            test_init_default_settings_strdups_strings, reset_settings,
            teardown_settings),
        cmocka_unit_test_setup_teardown(
            test_free_settings_zeroes_struct, reset_settings,
            teardown_settings),
        cmocka_unit_test(test_free_settings_safe_on_zero_init),
        cmocka_unit_test_setup_teardown(
            test_settings_reload_adopts_reloadable_keeps_others,
            reset_settings, teardown_settings),
        cmocka_unit_test_setup_teardown(
            test_settings_reload_rolls_back_on_syntax_error, reset_settings,
            teardown_settings),
        cmocka_unit_test_setup_teardown(
            test_settings_reload_no_double_free, reset_settings,
            teardown_settings),
        /* parse_rc value-level tests. Pattern to extend: write a one-line
         * rc with parse_rc_with_body(), then assert the field's new value
         * on `settings`. Only reloadable params are guaranteed to be
         * parsed here (parse_rc's static reloading flag flips to 1 after
         * the first test above, and non-reloadable params get skipped
         * from then on). */
        cmocka_unit_test_setup_teardown(test_parse_rc_string_field,
            reset_settings, teardown_settings),
        cmocka_unit_test_setup_teardown(test_parse_rc_int_field,
            reset_settings, teardown_settings),
        cmocka_unit_test_setup_teardown(test_parse_rc_bool_field,
            reset_settings, teardown_settings),
        cmocka_unit_test_setup_teardown(test_parse_rc_enum_field,
            reset_settings, teardown_settings),
        cmocka_unit_test_setup_teardown(test_parse_rc_list_field,
            reset_settings, teardown_settings),
        cmocka_unit_test_setup_teardown(
            test_parse_rc_ignores_comments_and_blank_lines, reset_settings,
            teardown_settings),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
