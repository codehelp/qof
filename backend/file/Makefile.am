SUBDIRS = .

qof_LTLIBRARIES=libqof-backend-qsf.la

qofdir=$(libdir)/qof${SONAME}

AM_CFLAGS = \
  ${warnFLAGS} \
  -I.. -I../.. \
  -I${top_srcdir}/qof \
  -DLOCALE_DIR=\""$(datadir)/locale"\" \
  -DQSF_SCHEMA_DIR=\"$(QSF_SCHEMA_DIR)\" \
  ${LIBXML2_CFLAGS} \
  ${GLIB_CFLAGS}

libqof_backend_qsf_la_SOURCES = \
  qsf-backend.c \
  qsf-xml-map.c \
  qsf-xml.c

libqof_backend_qsf_la_LDFLAGS = \
 -L${top_builddir}/qof \
 -avoid-version -module

libqof_backend_qsf_la_LIBADD = \
  ${QOF_LIBS} \
  ${LIBXML2_LIBS} \
  ${GLIB_LIBS}

qofincludedir = ${pkgincludedir}

qofinclude_HEADERS = \
  qof-backend-qsf.h

qsfschemadir = $(QSF_SCHEMA_DIR)
qsfschema_DATA = \
  qsf-object.xsd.xml \
  qsf-map.xsd.xml \
  pilot-qsf-GnuCashInvoice.xml \
  pilot-qsf-gncCustomer.xml

EXTRA_DIST = \
  $(qsfschema_DATA) \
  qsf-xml.h
