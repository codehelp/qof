AM_CFLAGS = \
  ${warnFLAGS} \
  -I.. -I../.. \
  -I${top_srcdir}/qof/ \
  -DLOCALE_DIR=\""$(datadir)/locale"\" \
  ${GLIB_CFLAGS} \
  ${DEPS_CFLAGS} \
  ${SQLITE_CFLAGS}

qof_LTLIBRARIES=libqof-backend-sqlite.la

qofdir=$(libdir)/qof${SONAME}

libqof_backend_sqlite_la_SOURCES = \
 qof-sqlite.c

libqof_backend_sqlite_la_LDFLAGS = \
 -L${top_builddir}/qof \
 -L${top_builddir}/lib/libsql \
 -avoid-version -module

libqof_backend_sqlite_la_LIBADD = \
 ${QOF_LIBS} \
 ${GLIB_LIBS} \
 ${SQLITE_LIBS} \
 ${SQL_PKG_LIB}

EXTRA_DIST = \
 qof-sqlite.h
