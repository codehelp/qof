/***************************************************************************
 *            test-querynew.c
 *
 *  Copyright  2004 Linas Vepstas <linas@linas.org>
 ****************************************************************************/
/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor Boston, MA  02110-1301,  USA
 */

#include <glib.h>
#include <stdio.h>

#include "qof.h"
#include "qofclass-p.h"
#include "qofquerycore-p.h"

#include "test-stuff.h"

#define TEST_MODULE_NAME	"TestModuleName"
#define TEST_MODULE_DESC "Test Object"
#define TEST_CORE		"TestCoreType"
#define TEST_PARAM		"test-param"
#define BAD_PARAM		"bad-param"

static void
obj_foreach (QofCollection * col, 
	QofEntityForeachCB cb __attribute__ ((unused)), gpointer u_d)
{
	int *foo = u_d;

	do_test (col != NULL, "foreach: NULL collection");
	success ("called foreach callback");

	*foo = 1;
}

static const char *
printable (gpointer obj)
{
	do_test (obj != NULL, "printable: object is NULL");
	success ("called printable callback");
	return ((const gchar *) obj);
}

static QofObject bus_obj = {
  .interface_version = QOF_OBJECT_VERSION,
  .e_type = TEST_MODULE_NAME,
  .type_label = TEST_MODULE_DESC,
  .create = NULL,
  .book_begin = NULL,
  .book_end = NULL,
  .is_dirty = NULL,
  .mark_clean = NULL,
  .foreach = obj_foreach,
  .printable = printable,
  .version_cmp = NULL,
};

static int
test_sort (gpointer a __attribute__ ((unused)), gpointer b __attribute__ ((unused)))
{
	return 0;
}

static int
test_core_param (gpointer a __attribute__ ((unused)))
{
	return 0;
}

static void
test_class (void)
{
	static QofParam params[] = {
		{TEST_PARAM, TEST_CORE, (QofAccessFunc) test_core_param,
			NULL, NULL},
		{NULL, NULL, NULL, NULL, NULL},
	};

	fprintf (stderr, "\tTesting the qof_query_object interface. \n"
			 "\tYou may see some \"** CRITICAL **\" messages, which you can safely ignore\n");
	do_test (qof_object_register (&bus_obj), "register test object");

	qof_class_register (TEST_MODULE_NAME, (QofSortFunc) test_sort, params);

	do_test (qof_class_get_parameter (TEST_MODULE_NAME, TEST_PARAM)
			 == &params[0], "qof_class_get_parameter");
	do_test (qof_class_get_parameter (NULL, NULL) == NULL,
			 "qof_class_get_parameter (NULL, NULL)");
	do_test (qof_class_get_parameter (TEST_MODULE_NAME, NULL) == NULL,
			 "qof_class_get_parameter (TEST_MODULE_NAME, NULL)");
	do_test (qof_class_get_parameter (TEST_MODULE_NAME, BAD_PARAM) == NULL,
			 "qof_class_get_parameter (TEST_MODULE_NAME, BAD_PARAM)");
	do_test (qof_class_get_parameter (NULL, TEST_PARAM) == NULL,
			 "qof_class_get_parameter (NULL, TEST_PARAM)");

	do_test (qof_class_get_parameter_getter (TEST_MODULE_NAME, TEST_PARAM)
			 == (QofAccessFunc) test_core_param,
			 "qof_class_get_parameter_getter");

	do_test (safe_strcmp (qof_class_get_parameter_type (TEST_MODULE_NAME,
														TEST_PARAM),
						  TEST_CORE) == 0, "qof_class_get_parameter_type");

	do_test (qof_class_get_default_sort (TEST_MODULE_NAME) ==
			 (QofSortFunc) test_sort, "qof_class_get_default_sort");
	do_test (qof_class_get_default_sort (NULL) == NULL,
			 "qof_class_get_default_sort (NULL)");
}

static void
test_query_core (void)
{

}

static void
test_querynew (void)
{
}

int
main (void)
{
	qof_init ();
	test_query_core ();
	test_class ();
	test_querynew ();
	print_test_results ();
	qof_close ();
	return get_rv ();
}
