bin_PROGRAMS = stalonetray
stalonetray_SOURCES = main.c icons.c icons.h xembed.c xembed.h layout.c layout.h embed.c embed.h \
                      settings.c settings.h xutils.c xutils.h tray.c tray.h debug.h list.h \
					  wmh.c wmh.h common.h debug.c image.c image.h \
					  scrollbars.c scrollbars.h xinerama.c xinerama.h
EXTRA_stalonetray_SOURCES = kde_tray.c kde_tray.h
stalonetray_DEPENDENCIES = @kde_tray_support@
AM_CFLAGS = @X_CFLAGS@
stalonetray_LDADD = @kde_tray_support@ @X_LIBS@ @X_PRE_LIBS@ -lX11 @X_EXTRA_LIBS@
