AM_CFLAGS = \
  -I.. -I../.. -I../../.. \
  -I${top_srcdir}/qof \
   ${warnFLAGS} \
   ${LIBXML2_CFLAGS} \
   ${LIBGDA_CFLAGS} \
   ${GLIB_CFLAGS}

if USE_LIBGDA
LDADD = \
   ${top_builddir}/qof/libqof.la \
   ${GLIB_LIBS} \
   ${GMODULE_LIBS} \
   ${GOBJECT_LIBS} \
   ${GDA_PKG_LIBS}
else
LDADD = \
 -L${top_builddir}/lib/libsql ${SQL_PKG_LIB} \
   ${top_builddir}/qof/libqof.la \
   ${GLIB_LIBS} \
   ${GMODULE_LIBS} \
   ${GOBJECT_LIBS} \
   ${SQL_PKG_LIB}
endif

#   ${LIBXML2_LIBS} 

test_book_merge_SOURCES = \
  test-stuff.c \
  test-book-merge.c

test_date_SOURCES = \
  test-date.c \
  test-stuff.c \
  test-engine-stuff.c

test_guid_SOURCES = \
  test-stuff.c \
  test-guid.c

test_numeric_SOURCES = \
  test-stuff.c \
  test-numeric.c

test_object_SOURCES = \
  test-stuff.c \
  test-object.c

test_querynew_SOURCES = \
  test-stuff.c \
  test-querynew.c

test_recursive_SOURCES = \
  test-engine-stuff.c \
  test-stuff.c \
  test-recursive.c

test_event_SOURCES = \
  test-engine-stuff.c \
  test-stuff.c \
  test-event.c

test_sql_SOURCES = \
  test-engine-stuff.c \
  test-stuff.c \
  test-sql.c

noinst_PROGRAMS = \
  test-book-merge \
  test-date \
  test-guid \
  test-numeric \
  test-object \
  test-querynew \
  test-recursive \
  test-event \
  test-sql

TESTS = \
  test-book-merge \
  test-date \
  test-guid \
  test-numeric \
  test-object \
  test-querynew \
  test-recursive \
  test-event \
  test-sql

EXTRA_DIST = \
  test-stuff.h \
  test-engine-stuff.h
