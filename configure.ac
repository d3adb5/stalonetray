# $Id$
# vim:ft=m4

AC_INIT([stalonetray],[0.8.5])
AC_CONFIG_SRCDIR(src/main.c)
AC_CONFIG_HEADERS(src/config.h)
AM_INIT_AUTOMAKE([foreign])

dnl ============ General =============

AC_PROG_CC
AC_USE_SYSTEM_EXTENSIONS

AC_CHECK_HEADERS([ctype.h],,[AC_MSG_FAILURE([ctype.h is not found.])])
AC_CHECK_HEADERS([errno.h],,[AC_MSG_FAILURE([errno.h is not found.])])
AC_CHECK_HEADERS([signal.h],,[AC_MSG_FAILURE([signal.h is not found.])])
AC_CHECK_HEADERS([stdio.h],,[AC_MSG_FAILURE([stdio.h is not found.])])
AC_CHECK_HEADERS([stdlib.h],,[AC_MSG_FAILURE([stdlib.h is not found.])])
AC_CHECK_HEADERS([string.h],,[AC_MSG_FAILURE([string.h is not found.])])
AC_CHECK_HEADERS([time.h],,[AC_MSG_FAILURE([time.h is not found.])])
AC_CHECK_HEADERS([unistd.h],,[AC_MSG_FAILURE([unistd.h is not found.])])
AC_CHECK_HEADERS([libgen.h])

dnl ============ General =============

dnl ========== Backtrace =============

AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <execinfo.h>
             #include <sys/types.h>
            ]], [[void *array[10];
             size_t size;
             size = backtrace(array, 10);
             backtrace_symbols_fd(array, size, 0);
            ]])],[ac_cv_var_backtrace=yes],[ac_cv_var_backtrace=no])

if test x$ac_cv_var_backtrace = xyes; then
    AC_DEFINE(HAVE_BACKTRACE, 1, [System has usable backtrace implementation.])
else
    AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <ucontext.h>]], [[printstack(0);]])],[ac_cv_var_printstack=yes],[ac_cv_var_printstack=no])
    if test x$ac_cv_var_printstack = xyes; then
        AC_DEFINE(HAVE_PRINTSTACK, 1, [System has usable printstack implementation.])
    fi
fi

dnl ========== Backtrace =============

dnl ============= X11 ================

no_x=""
AC_PATH_XTRA
if test "x$no_x" = "xyes"; then
    echo "X11 libraries/headers could not be found."
    echo "If they are definetly installed, use"
    echo " --x-includes and --x-libraries to set the paths."
    echo
    echo "Aborting"
    echo
    exit 1
fi

dnl AC_SUBST(x_includes)
dnl AC_SUBST(x_libraries)

dnl ============= X11 ================

dnl ========= libxpm presence

ac_cv_xpm_supported=yes
AC_CHECK_LIB(Xpm,
             XpmReadFileToPixmap, ,
             [ac_cv_xpm_supported=no],
             [$X_LIBS])

case "$ac_cv_xpm_supported" in
    yes)
        AC_DEFINE(XPM_SUPPORTED, 1, [enable XPM background support])
    ;;
    *)
    ;;
esac
feature_list="XPM:$ac_cv_xpm_supported"

dnl ========= libXinerama presence

ac_cv_xinerama_supported=yes
AC_CHECK_LIB(Xinerama,
             XineramaQueryScreens, ,
             [ac_cv_xinerama_supported=no],
             [$X_LIBS])

case "$ac_cv_xinerama_supported" in
    yes)
        AC_DEFINE(XINERAMA_SUPPORTED, 1, [enable multiple monitor support])
    ;;
    *)
    ;;
esac
feature_list="$feature_list Xinerama:$ac_cv_xinerama_supported"

dnl ========= KDE icons support

AC_ARG_ENABLE(native-kde,
    [  --disable-native-kde    disable native kde tray icons support],
    [ac_cv_no_native_kde="$enableval"],
    [ac_cv_no_native_kde="yes"])
AC_ARG_ENABLE(debug,
    [  --enable-debug          enable debugging output\checks],
    [ac_cv_debug="$enableval"],
    [ac_cv_debug="yes"])
AC_ARG_ENABLE(events-trace,
    [  --enable-events-trace   enable full event trace (debug)],
    [ac_cv_events_trace="$enableval"],
    [ac_cv_events_trace="yes"])
AC_ARG_ENABLE(embedding-confirmation-delay,
    [  --enable-embedding-confirmation-delay  delay sending of embedding confirmation (debug)],
    [AS_IF([test "x$enableval" != xno],
        AC_DEFINE(DELAY_EMBEDDING_CONFIRMATION, 1, [delay sending of embedding confirmation])
        LDFLAGS="$LDFLAGS -lpthread"
    )],
    [])
AC_ARG_ENABLE(dump-win-info,
    [  --enable-dump-win-info  use xprop/xwininfo to dump icon window info],
    [ac_cv_dump_win_info="$enableval"],
    [ac_cv_dump_win_info="yes"])

AC_ARG_ENABLE(graceful-exit,
    [  --enable-graceful-exit  use non-portable hack to exit gracefuly on signal],
    [ac_cv_graceful_exit="$enableval"],
    [ac_cv_graceful_exit="yes"])

case "$ac_cv_debug" in
    yes)
        AC_DEFINE(DEBUG, 1, [enable debugging code])
        if test "x$GCC" = "xyes"; then
            CFLAGS="$CFLAGS -g"
        else
            echo "INFO: Since you use non-gcc compiler, please enable generation of debug info"
            echo "      for your compiler manually (via CFLAGS shell variable)"
        fi
    ;;
    *)
    ;;
esac
feature_list="$feature_list DEBUG:$ac_cv_debug"

kde_tray_support="kde_tray.o"

case "$ac_cv_no_native_kde" in
    no)
        AC_DEFINE(NO_NATIVE_KDE, 1, [disable native kde tray icons support])
        kde_tray_support=""
    ;;
    *)
    ;;
esac
AC_SUBST(kde_tray_support)

case "$ac_cv_dump_win_info" in
    yes)
        AC_DEFINE(ENABLE_DUMP_WIN_INFO, 1, [use xprop/xwininfo to dump icon window info])
    ;;
    *)
    ;;
esac

case "$ac_cv_graceful_exit" in
    no)
    ;;
    *)
        AC_DEFINE(ENABLE_GRACEFUL_EXIT_HACK, 1, [use non-portable hack to exit gracefuly on signal])
    ;;
esac

case "$ac_cv_events_trace" in
    yes)
        AC_DEFINE(TRACE_EVENTS, 1, [enable full event trace (debug)])
    ;;
    *)
    ;;
esac

feature_list="$feature_list KDE:$ac_cv_no_native_kde"
CPPFLAGS="$CPPFLAGS -DFEATURE_LIST='\"$feature_list\"'"

dnl ============ DocBook ============
dnl https://movementarian.org/docs/docbook-autotools/configure.html

AC_DEFUN([AX_CHECK_DOCBOOK], [
    XSLTPROC_FLAGS=--nonet
    DOCBOOK_ROOT=
    for i in \
        /usr/share/xml/docbook/stylesheet/docbook-xsl-nons \
        /usr/share/xml/docbook/xsl-stylesheets-*-nons/ \
        /usr/share/sgml/docbook/stylesheet/xsl/nwalsh \
        /usr/share/xml/docbook/stylesheet/docbook-xs \
        /usr/share/sgml/docbook/xsl-stylesheets/ \
        /usr/share/xsl/docbook \
        /usr/local/share/xsl/docbook \
    ; do
        if test -d "$i"; then
            DOCBOOK_ROOT=$i
        fi
    done
    AC_SUBST(DOCBOOK_ROOT)

    AC_CHECK_PROG(XSLTPROC,xsltproc,xsltproc,)
    XSLTPROC_WORKS=no
    if test -n "$XSLTPROC"; then
        AC_MSG_CHECKING([whether xsltproc works])
        DB_FILE="$DOCBOOK_ROOT/xhtml/docbook.xsl"

        $XSLTPROC $XSLTPROC_FLAGS $DB_FILE >/dev/null 2>&1 << END
<?xml version="1.0" encoding='ISO-8859-1'?>
<!DOCTYPE book PUBLIC "-//OASIS//DTD DocBook XML V4.4//EN" "http://www.oasis-open.org/docbook/xml/4.4/docbookx.dtd">
<book id="test">
</book>
END
        if test "$?" = 0; then
            XSLTPROC_WORKS=yes
        fi
        AC_MSG_RESULT($XSLTPROC_WORKS)
    fi
    AM_CONDITIONAL(have_xsltproc, test "$XSLTPROC_WORKS" = "yes")
    AC_SUBST(XSLTPROC_FLAGS)
])

AX_CHECK_DOCBOOK

dnl ============ Features ============

AC_CONFIG_FILES([
Makefile
src/Makefile
])
AC_OUTPUT

echo "*"
echo "* Debug                  : $ac_cv_debug"
if test "x$ac_cv_debug" = "xyes" -a "x$GCC" != "xyes"; then
    echo "*                          Since you are not using a gcc compatible compiler, please"
    echo "*                          set debug CFLAGS manually (if you want to)"
fi
echo "* Native KDE support     : $ac_cv_no_native_kde"
echo "* XPM background support : $ac_cv_xpm_supported"
echo "* Multi-monitor support  : $ac_cv_xinerama_supported"
echo "* Graceful exit support  : $ac_cv_graceful_exit"
echo "* Build documentation    : $XSLTPROC_WORKS"
echo "*"

