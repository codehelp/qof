## Process this file with automake to produce Makefile.in

lib_LTLIBRARIES = libqofsql.la

libqofsql_la_LDFLAGS= -version-info $(LIBQOFSQL_LIBRARY_VERSION) 

libqofsql_la_SOURCES = \
	parser.c \
	parser.h \
	lexer.c \
	sql_parser.c \
	sql_parser.h \
	mem.c \
	mem.h \
	sql_display.c \
	sql_tree.c \
	sql_tree.h

libqofsql_la_LIBADD = ${GLIB_LIBS}

qofsqlincludedir = ${pkgincludedir}
qofsqlinclude_HEADERS = sql_parser.h

noinst_PROGRAMS = sql_parse_test

EXTRA_DIST =       \
	README          \
	lexer.l         \
	parser.y  

AM_CFLAGS = -g     \
	${CFLAGS}        \
	${GLIB_CFLAGS}

#INCLUDES = \
#	-I$(includedir) 

parser.c: parser.y
	$(YACC) -v -d -o parser.c -p sql parser.y

lexer.c: lexer.l parser.c
	$(LEX) -olexer.c -Psql lexer.l

sql_parse_test_SOURCES=sql_parse_test.c
# sql_parse_test_LDFLAGS = $(LIBGDA_LIBS)
sql_parse_test_DEPENDENCIES = ./libqofsql.la
sql_parse_test_LDADD = ./libqofsql.la ${GLIB_LIBS}

tests: sql_parse_test

