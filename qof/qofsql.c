/********************************************************************\
 * qofsql.c -- QOF client-side SQL parser                           *
 *                                                                  *
 * This program is free software; you can redistribute it and/or    *
 * modify it under the terms of the GNU General Public License as   *
 * published by the Free Software Foundation; either version 2 of   *
 * the License, or (at your option) any later version.              *
 *                                                                  *
 * This program is distributed in the hope that it will be useful,  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    *
 * GNU General Public License for more details.                     *
 *                                                                  *
 * You should have received a copy of the GNU General Public License*
 * along with this program; if not, contact:                        *
 *                                                                  *
 * Free Software Foundation           Voice:  +1-617-542-5942       *
 * 51 Franklin Street, Fifth Floor    Fax:    +1-617-542-2652       *
 * Boston, MA  02110-1301,  USA       gnu@gnu.org                   *
 *                                                                  *
\********************************************************************/

/**
    @file qofsql.c
    @brief QOF client-side SQL parser - interfaces with libgda.
    @author Copyright (C) 2004 Linas Vepstas <linas@linas.org>
    @author Copyright 2008 Neil Williams <linux@codehelp.co.uk>

 Intended to parse incoming SQL into QOF queries (SELECT or INSERT)
 and prepare SQL commands from QOF entities (CREATE, UPDATE, INSERT,
 DELETE and DROP) for use in SQL-based backends.
*/

#include "config.h"
#include <stdlib.h>				/* for working atoll */
#include <errno.h>
#include <glib.h>
#include <libintl.h>
#ifdef HAVE_GDA
#include <libsql/sql_parser.h>
#else
#include "sql_parser.h"
#endif
#include <time.h>
#include "qof.h"
#include "qofsql-p.h"
#include "qofquery-p.h"

#define _(String) dgettext (GETTEXT_PACKAGE, String)
/** One KVP table per file for all instances. */
static gchar * kvp_table_name = NULL;

#define QSQL_KVP_TABLE "sql_kvp"

#define END_DB_VERSION " dbversion int );"

static QofLogModule log_module = QOF_MOD_QUERY;

/* =================================================================== */

struct _QofSqlQuery
{
	sql_statement *parse_result;
	QofQuery *qof_query;
	QofBook *book;
	gchar *single_global_tablename;
	KvpFrame *kvp_join;
	GList *param_list;
	QofEntity *inserted_entity;
};

/* ========================================================== */

QofSqlQuery *
qof_sql_query_new (void)
{
	QofSqlQuery *sqn = (QofSqlQuery *) g_new0 (QofSqlQuery, 1);

	sqn->qof_query = NULL;
	sqn->parse_result = NULL;
	sqn->book = NULL;
	sqn->single_global_tablename = NULL;
	sqn->kvp_join = NULL;

	return sqn;
}

/* ========================================================== */

void
qof_sql_query_destroy (QofSqlQuery * q)
{
	if (!q)
		return;
	qof_query_destroy (q->qof_query);
	sql_destroy (q->parse_result);
	g_free (q);
}

/* ========================================================== */

QofQuery *
qof_sql_query_get_query (QofSqlQuery * q)
{
	if (!q)
		return NULL;
	return q->qof_query;
}

/* ========================================================== */

void
qof_sql_query_set_book (QofSqlQuery * q, QofBook * book)
{
	if (!q)
		return;
	q->book = book;
}

/* ========================================================== */

void
qof_sql_query_set_kvp (QofSqlQuery * q, KvpFrame * kvp)
{
	if (!q)
		return;
	q->kvp_join = kvp;
}

/* ========================================================== */

static inline void
get_table_and_param (char *str, char **tab, char **param)
{
	char *end = strchr (str, '.');
	if (!end)
	{
		*tab = 0;
		*param = str;
		return;
	}
	*end = 0;
	*tab = str;
	*param = end + 1;
}

static inline char *
dequote_string (char *str)
{
	size_t len;
	/* strip out quotation marks ...  */
	if (('\'' == str[0]) || ('\"' == str[0]))
	{
		str++;
		len = strlen (str);
		str[len - 1] = 0;
	}
	return str;
}

static QofQuery *
handle_single_condition (QofSqlQuery * query, sql_condition * cond)
{
	gchar tmpbuff[128];
	GSList *param_list;
	GList *guid_list;
	QofQueryPredData *pred_data;
	sql_field_item *sparam, *svalue;
	gchar *qparam_name, *qvalue_name, *table_name, *param_name;
	gchar *sep, *path, *str, *p;
	QofQuery *qq;
	KvpValue *kv, *kval;
	KvpValueType kvt;
	QofQueryCompare qop;
	guint len;
	QofType param_type;
	QofGuidMatch gm;

	pred_data = NULL;
	if (NULL == cond)
	{
		PWARN ("missing condition");
		return NULL;
	}
	/* -------------------------------- */
	/* field to match, assumed, for now to be on the left */
	/* XXX fix this so it can be either left or right */
	if (NULL == cond->d.pair.left)
	{
		PWARN ("missing left parameter");
		return NULL;
	}
	sparam = cond->d.pair.left->item;
	if (SQL_name != sparam->type)
	{
		PWARN ("we support only parameter names at this time (parsed %d)",
			sparam->type);
		return NULL;
	}
	qparam_name = sparam->d.name->data;
	if (NULL == qparam_name)
	{
		PWARN ("missing parameter name");
		return NULL;
	}

	/* -------------------------------- */
	/* value to match, assumed, for now, to be on the right. */
	/* XXX fix this so it can be either left or right */
	if (NULL == cond->d.pair.right)
	{
		PWARN ("missing right parameter");
		return NULL;
	}
	svalue = cond->d.pair.right->item;
	if (SQL_name != svalue->type)
	{
		PWARN ("we support only simple values (parsed as %d)",
			svalue->type);
		return NULL;
	}
	qvalue_name = svalue->d.name->data;
	if (NULL == qvalue_name)
	{
		PWARN ("missing value");
		return NULL;
	}
	qvalue_name = dequote_string (qvalue_name);
	qvalue_name = (char *) qof_util_whitespace_filter (qvalue_name);

	/* Look to see if its the special KVP value holder.
	 * If it is, look up the value. */
	if (0 == strncasecmp (qvalue_name, "kvp://", 6))
	{
		if (NULL == query->kvp_join)
		{
			PWARN ("missing kvp frame");
			return NULL;
		}
		kv = kvp_frame_get_value (query->kvp_join, qvalue_name + 5);
		/* If there's no value, its not an error; 
		 * we just don't do this predicate */
		if (!kv)
			return NULL;
		kvt = kvp_value_get_type (kv);

		tmpbuff[0] = 0x0;
		qvalue_name = tmpbuff;
		switch (kvt)
		{
		case KVP_TYPE_GINT64:
		{
				gint64 ival = kvp_value_get_gint64 (kv);
				sprintf (tmpbuff, "%" G_GINT64_FORMAT "\n", ival);
				break;
		}
		case KVP_TYPE_DOUBLE:
		{
				double ival = kvp_value_get_double (kv);
				sprintf (tmpbuff, "%26.18g\n", ival);
				break;
		}
		case KVP_TYPE_STRING:
			/* If there's no value, its not an error; 
			 * we just don't do this predicate */
			qvalue_name = kvp_value_get_string (kv);
			if (!qvalue_name)
				return NULL;
			break;
		case KVP_TYPE_GUID:
		case KVP_TYPE_TIME :
		case KVP_TYPE_BOOLEAN :
		case KVP_TYPE_BINARY:
		case KVP_TYPE_GLIST:
		case KVP_TYPE_NUMERIC:
		case KVP_TYPE_FRAME:
			PWARN ("unhandled kvp type=%d", kvt);
			return NULL;
		}
	}

	/* -------------------------------- */
	/* Now start building the QOF parameter */
	param_list = qof_query_build_param_list (qparam_name, NULL);

	/* Get the where-term comparison operator */
	switch (cond->op)
	{
	case SQL_eq:
		qop = QOF_COMPARE_EQUAL;
		break;
	case SQL_gt:
		qop = QOF_COMPARE_GT;
		break;
	case SQL_lt:
		qop = QOF_COMPARE_LT;
		break;
	case SQL_geq:
		qop = QOF_COMPARE_GTE;
		break;
	case SQL_leq:
		qop = QOF_COMPARE_LTE;
		break;
	case SQL_diff:
		qop = QOF_COMPARE_NEQ;
		break;
	default:
		/* XXX for string-type queries, we should be able to
		 * support 'IN' for substring search.  Also regex. */
		PWARN ("Unsupported compare op (parsed as %u)", cond->op);
		return NULL;
	}

	/* OK, need to know the type of the thing being matched 
	 * in order to build the correct predicate.  Get the type 
	 * from the object parameters. */
	get_table_and_param (qparam_name, &table_name, &param_name);
	if (NULL == table_name)
	{
		table_name = query->single_global_tablename;
	}
	if (NULL == table_name)
	{
		PWARN ("Need to specify an object class to query");
		return NULL;
	}

	if (FALSE == qof_class_is_registered (table_name))
	{
		PWARN ("The query object \'%s\' is not known", table_name);
		return NULL;
	}

	param_type = qof_class_get_parameter_type (table_name, param_name);
	if (!param_type)
	{
		PWARN ("The parameter \'%s\' on object \'%s\' is not known",
			param_name, table_name);
		return NULL;
	}

	if (!strcmp (param_type, QOF_TYPE_STRING))
	{
		pred_data = qof_query_string_predicate (qop,	/* comparison to make */
			qvalue_name,		/* string to match */
			QOF_STRING_MATCH_CASEINSENSITIVE,	/* case matching */
			FALSE);				/* use_regexp */
	}
	else if (!strcmp (param_type, QOF_TYPE_CHAR))
	{
		QofCharMatch cm = QOF_CHAR_MATCH_ANY;
		if (QOF_COMPARE_NEQ == qop)
			cm = QOF_CHAR_MATCH_NONE;
		pred_data = qof_query_char_predicate (cm, qvalue_name);
	}
	else if (!strcmp (param_type, QOF_TYPE_INT32))
	{
		gint32 ival = atoi (qvalue_name);
		pred_data = qof_query_int32_predicate (qop, ival);
	}
	else if (!strcmp (param_type, QOF_TYPE_INT64))
	{
		gint64 ival = atoll (qvalue_name);
		pred_data = qof_query_int64_predicate (qop, ival);
	}
	else if (!strcmp (param_type, QOF_TYPE_DOUBLE))
	{
		double ival = atof (qvalue_name);
		pred_data = qof_query_double_predicate (qop, ival);
	}
	else if (!strcmp (param_type, QOF_TYPE_BOOLEAN))
	{
		gboolean ival = qof_util_bool_to_int (qvalue_name);
		pred_data = qof_query_boolean_predicate (qop, ival);
	}
	else if (!safe_strcmp (param_type, QOF_TYPE_TIME))
	{
		QofDate *qd;
		QofTime *qt;

		qd = qof_date_parse (qvalue_name, QOF_DATE_FORMAT_UTC);
		qt = qof_date_to_qtime (qd);
		qof_date_free (qd);
		pred_data = 
			qof_query_time_predicate (qop, QOF_DATE_MATCH_NORMAL, 
			qt);
	}
	else if (!strcmp (param_type, QOF_TYPE_NUMERIC))
	{
		QofNumeric ival;
		qof_numeric_from_string (qvalue_name, &ival);
		pred_data =
			qof_query_numeric_predicate (qop, QOF_NUMERIC_MATCH_ANY, ival);
	}
	else if (!strcmp (param_type, QOF_TYPE_DEBCRED))
	{
		/* DEBCRED is likely to be deprecated before libqof2 */
		QofNumeric ival;
		qof_numeric_from_string (qvalue_name, &ival);
		pred_data =
			qof_query_numeric_predicate (qop, QOF_NUMERIC_MATCH_ANY, ival);
	}
	else if (!strcmp (param_type, QOF_TYPE_GUID))
	{
		GUID guid;
		gboolean rc = string_to_guid (qvalue_name, &guid);
		if (0 == rc)
		{
			PWARN ("unable to parse guid: %s", qvalue_name);
			return NULL;
		}

		// XXX less, than greater than don't make sense,
		// should check for those bad conditions

		gm = QOF_GUID_MATCH_ANY;
		if (QOF_COMPARE_NEQ == qop)
			gm = QOF_GUID_MATCH_NONE;
		guid_list = g_list_append (NULL, &guid);
		pred_data = qof_query_guid_predicate (gm, guid_list);

		g_list_free (guid_list);
	}
	else if (!strcmp (param_type, QOF_TYPE_KVP))
	{
		/* We are expecting an encoded value that looks like
		 * /some/path/string:value
		 */
		sep = strchr (qvalue_name, ':');
		if (!sep)
			return NULL;
		*sep = 0;
		path = qvalue_name;
		str = sep + 1;
		/* If str has only digits, we know its a plain number.
		 * If its numbers and a decimal point, assume a float
		 * If its numbers and a slash, assume numeric
		 * If its 32 bytes of hex, assume GUID
		 * If it looks like an iso date ... 
		 * else assume its a string.
		 */
		kval = NULL;
		len = strlen (str);
		if ((32 == len) && (32 == strspn (str, "0123456789abcdef")))
		{
			GUID guid;
			string_to_guid (str, &guid);
			kval = kvp_value_new_guid (&guid);
		}
		else if (len == strspn (str, "0123456789"))
		{
			kval = kvp_value_new_gint64 (atoll (str));
		}
		else if ((p = strchr (str, '.')) &&
			((len - 1) == (strspn (str, "0123456789") +
					strspn (p + 1, "0123456789"))))
		{
			kval = kvp_value_new_double (atof (str));
		}

		else if ((p = strchr (str, '/')) &&
			((len - 1) == (strspn (str, "0123456789") +
					strspn (p + 1, "0123456789"))))
		{
			QofNumeric num;
			qof_numeric_from_string (str, &num);
			kval = kvp_value_new_numeric (num);
		}
		else if ((p = strchr (str, '-')) &&
			(p = strchr (p + 1, '-')) &&
			(p = strchr (p + 1, ' ')) &&
			(p = strchr (p + 1, ':')) && (p = strchr (p + 1, ':')))
		{
			QofDate *qd;
			QofTime *qt;

			qd = qof_date_parse (str, QOF_DATE_FORMAT_UTC);
			qt = qof_date_to_qtime (qd);
			kval =
				kvp_value_new_time (qt);
			qof_date_free (qd);
		}

		/* The default handler is a string */
		if (NULL == kval)
		{
			kval = kvp_value_new_string (str);
		}
		pred_data = qof_query_kvp_predicate_path (qop, path, kval);
	}
	else
	{
		PWARN ("The predicate type \"%s\" is unsupported for now",
			param_type);
		return NULL;
	}

	qq = qof_query_create ();
	qof_query_add_term (qq, param_list, pred_data, QOF_QUERY_FIRST_TERM);
	return qq;
}

/* ========================================================== */

static QofQuery *
handle_where (QofSqlQuery * query, sql_where * swear)
{
	QofQueryOp qop;
	QofQuery *qq;

	switch (swear->type)
	{
	case SQL_pair:
		{
			QofQuery *qleft = handle_where (query, swear->d.pair.left);
			QofQuery *qright = handle_where (query, swear->d.pair.right);
			if (NULL == qleft)
				return qright;
			if (NULL == qright)
				return qleft;
			switch (swear->d.pair.op)
			{
			case SQL_and:
				qop = QOF_QUERY_AND;
				break;
			case SQL_or:
				qop = QOF_QUERY_OR;
				break;
				/* XXX should add support for nand, nor, xor */
			default:
				qof_query_destroy (qleft);
				qof_query_destroy (qright);
				return NULL;
			}
			qq = qof_query_merge (qleft, qright, qop);
			qof_query_destroy (qleft);
			qof_query_destroy (qright);
			return qq;
		}
	case SQL_negated:
		{
			QofQuery *qq = handle_where (query, swear->d.negated);
			QofQuery *qneg = qof_query_invert (qq);
			qof_query_destroy (qq);
			return qneg;
		}

	case SQL_single:
		{
			sql_condition *cond = swear->d.single;
			return handle_single_condition (query, cond);
		}
	}
	return NULL;
}

/* ========================================================== */

static void
handle_sort_order (QofSqlQuery * query, GList * sorder_list)
{
	GSList *qsp[3];
	GList *n;
	gboolean direction[3];
	int i;
	sql_order_field *sorder;
	char *qparam_name;

	if (!sorder_list)
		return;

	for (i = 0; i < 3; i++)
	{
		qsp[i] = NULL;
		direction[i] = 0;

		if (sorder_list)
		{
			sorder = sorder_list->data;

			/* Set the sort direction */
			if (SQL_asc == sorder->order_type)
				direction[i] = TRUE;

			/* Find the parameter name */
			qparam_name = NULL;
			n = sorder->name;
			if (n)
			{
				qparam_name = n->data;
				if (qparam_name)
				{
					qsp[i] =
						qof_query_build_param_list (qparam_name, NULL);
				}
				n = n->next;	/* next parameter */
			}
			else
			{
				/* if no next parameter, then next order-by */
				sorder_list = sorder_list->next;
			}
		}
	}

	qof_query_set_sort_order (query->qof_query, qsp[0], qsp[1], qsp[2]);
	qof_query_set_sort_increasing (query->qof_query, direction[0],
		direction[1], direction[2]);
}

/* INSERT INTO handlers =================================================== */

static void
qof_sql_insertCB (const QofParam * param, const gchar * insert_string,
	QofSqlQuery * query)
{
	QofIdTypeConst type;
	sql_insert_statement *sis;
	gboolean G_GNUC_UNUSED registered_type;
	QofEntity *ent;
	/* cm_ prefix used for variables that hold the data to commit */
	QofNumeric cm_numeric;
	gdouble cm_double;
	gboolean cm_boolean;
	gint32 cm_i32;
	gint64 cm_i64;
	gchar cm_char, *tail;
	GUID *cm_guid;
/*	KvpFrame       *cm_kvp;
	KvpValue       *cm_value;
	KvpValueType   cm_type;*/
	void (*string_setter) (QofEntity *, const gchar *);
	void (*time_setter) (QofEntity *, QofTime *);
	void (*numeric_setter) (QofEntity *, QofNumeric);
	void (*double_setter) (QofEntity *, gdouble);
	void (*boolean_setter) (QofEntity *, gboolean);
	void (*i32_setter) (QofEntity *, gint32);
	void (*i64_setter) (QofEntity *, gint64);
	void (*char_setter) (QofEntity *, gchar);
/*	void (*kvp_frame_setter) (QofEntity*, KvpFrame*);*/

	g_return_if_fail (param || insert_string || query);
	ent = query->inserted_entity;
	sis = query->parse_result->statement;
	type = g_strdup_printf ("%s", sis->table->d.simple);

	ENTER (" param=%s param_type=%s type=%s content=%s",
		param->param_name, param->param_type, type, insert_string);
	if (safe_strcmp (param->param_type, QOF_TYPE_STRING) == 0)
	{
		string_setter =
			(void (*)(QofEntity *, const char *)) param->param_setfcn;
		if (string_setter != NULL)
		{
			string_setter (ent, insert_string);
		}
		registered_type = TRUE;
	}
	if (safe_strcmp (param->param_type, QOF_TYPE_TIME) == 0)
	{
		QofDate *qd;
		QofTime *qt;
		time_setter = 
			(void (*)(QofEntity *, QofTime *)) param->param_setfcn;
		qd = qof_date_parse (insert_string, QOF_DATE_FORMAT_UTC);
		qt = qof_date_to_qtime (qd);
		if((time_setter != NULL) && (qof_time_is_valid(qt)))
		{
			time_setter (ent, qt);
		}
	}
	if ((safe_strcmp (param->param_type, QOF_TYPE_NUMERIC) == 0) ||
		(safe_strcmp (param->param_type, QOF_TYPE_DEBCRED) == 0))
	{
		numeric_setter =
			(void (*)(QofEntity *, QofNumeric)) param->param_setfcn;
		qof_numeric_from_string (insert_string, &cm_numeric);
		if (numeric_setter != NULL)
		{
			numeric_setter (ent, cm_numeric);
		}
	}
	if (safe_strcmp (param->param_type, QOF_TYPE_GUID) == 0)
	{
		cm_guid = g_new (GUID, 1);
		if (TRUE != string_to_guid (insert_string, cm_guid))
		{
			LEAVE (" string to guid failed for %s", insert_string);
			return;
		}
/*			reference_type = xmlGetProp(node, QSF_OBJECT_TYPE);
		if(0 == safe_strcmp(QOF_PARAM_GUID, reference_type)) 
		{
			qof_entity_set_guid(qsf_ent, cm_guid);
		}
		else {
			reference = qof_entity_get_reference_from(qsf_ent, cm_param);
			if(reference) {
				params->referenceList = g_list_append(params->referenceList, reference);
			}
		}*/
	}
	if (safe_strcmp (param->param_type, QOF_TYPE_INT32) == 0)
	{
		errno = 0;
		cm_i32 = (gint32) strtol (insert_string, &tail, 0);
		if (errno == 0)
		{
			i32_setter =
				(void (*)(QofEntity *, gint32)) param->param_setfcn;
			if (i32_setter != NULL)
			{
				i32_setter (ent, cm_i32);
			}
		}
		else
		{
			QofBackend *backend;
			QofBook *book;

			book = qof_instance_get_book ((QofInstance *) ent);
			backend = qof_book_get_backend (book);
			qof_error_set_be (backend, qof_error_register 
			(_("When converting SQLite strings into numbers, an "
			"overflow has been detected. The SQLite database "
			"'%s' contains invalid data in a field that is meant "
			"to hold a number."), TRUE));
		}
	}
	if (safe_strcmp (param->param_type, QOF_TYPE_INT64) == 0)
	{
		errno = 0;
		cm_i64 = strtoll (insert_string, &tail, 0);
		if (errno == 0)
		{
			i64_setter =
				(void (*)(QofEntity *, gint64)) param->param_setfcn;
			if (i64_setter != NULL)
			{
				i64_setter (ent, cm_i64);
			}
		}
		else
		{
			QofBackend *backend;
			QofBook *book;

			book = qof_instance_get_book ((QofInstance *) ent);
			backend = qof_book_get_backend (book);
			qof_error_set_be (backend, qof_error_register 
			(_("When converting SQLite strings into numbers, an "
			"overflow has been detected. The SQLite database "
			"'%s' contains invalid data in a field that is meant "
			"to hold a number."), TRUE));
		}
	}
	if (safe_strcmp (param->param_type, QOF_TYPE_DOUBLE) == 0)
	{
		errno = 0;
		cm_double = strtod (insert_string, &tail);
		if (errno == 0)
		{
			double_setter =
				(void (*)(QofEntity *, double)) param->param_setfcn;
			if (double_setter != NULL)
			{
				double_setter (ent, cm_double);
			}
		}
	}
	if (safe_strcmp (param->param_type, QOF_TYPE_BOOLEAN) == 0)
	{
		gint b;
		b = qof_util_bool_to_int (insert_string);
		if (b == 1)
		{
			cm_boolean = TRUE;
		}
		else
		{
			cm_boolean = FALSE;
		}
		boolean_setter =
			(void (*)(QofEntity *, gboolean)) param->param_setfcn;
		if (boolean_setter != NULL)
		{
			boolean_setter (ent, cm_boolean);
		}
	}
	if (safe_strcmp (param->param_type, QOF_TYPE_KVP) == 0)
	{

	}
	if (safe_strcmp (param->param_type, QOF_TYPE_CHAR) == 0)
	{
		cm_char = *insert_string;
		char_setter = (void (*)(QofEntity *, char)) param->param_setfcn;
		if (char_setter != NULL)
		{
			char_setter (ent, cm_char);
		}
	}
	LEAVE (" ");
}

static void
qof_query_set_insert_table (QofSqlQuery * query)
{
	sql_insert_statement *sis;
	sql_table *sis_t;
	sis = query->parse_result->statement;
	switch (sis->table->type)
	{
	case SQL_simple:
		{
			sis_t = sis->table;
			query->single_global_tablename =
				g_strdup_printf ("%s", sis_t->d.simple);
			qof_query_search_for (query->qof_query,
				query->single_global_tablename);
			PINFO (" insert set to table: %s", sis_t->d.simple);
			break;
		}
	default:
		{
			PWARN ("SQL insert only handles simple statements");
		}
	}
}

static QofEntity *
qof_query_insert (QofSqlQuery * query)
{
	GList *field_list, *value_list, *cur;
	const gchar *param_name;
	gchar *value;
	QofIdType type;
	const QofParam *param;
	QofInstance *inst;
	sql_insert_statement *sis;
	sql_field *field;
	sql_field_item *item;

	ENTER (" ");
	query->param_list = NULL;
	type = NULL;
	param = NULL;
	value = NULL;
	field_list = NULL;
	value_list = NULL;
	param_name = NULL;
	sis = query->parse_result->statement;
	if (!sis->fields || !sis->values)
	{
		LEAVE (" NULL insert statement");
		return NULL;
	}
	type = g_strdup (query->single_global_tablename);
	inst = (QofInstance *) qof_object_new_instance (type, query->book);
	if (inst == NULL)
	{
		LEAVE (" unable to create instance of type %s", type);
		return NULL;
	}
	query->inserted_entity = &inst->entity;
	value_list = sis->values;
	for (field_list = sis->fields; field_list != NULL;
		field_list = field_list->next)
	{
		field = value_list->data;
		item = field->item;
		for (cur = item->d.name; cur != NULL; cur = cur->next)
		{
			value =
				g_strdup_printf ("%s",
				dequote_string ((char *) cur->data));
		}
		field = field_list->data;
		item = field->item;
		for (cur = item->d.name; cur != NULL; cur = cur->next)
		{
			param_name = g_strdup_printf ("%s", (char *) cur->data);
			param = qof_class_get_parameter (type, param_name);
		}
		if (param && value)
		{
			qof_sql_insertCB (param, value, query);
		}
		value_list = g_list_next (value_list);
	}
	LEAVE (" ");
	return query->inserted_entity;
}

static const char *
sql_type_as_string (sql_statement_type type)
{
	switch (type)
	{
	case SQL_select:
		{
			return "SELECT";
		}
	case SQL_insert:
		{
			return "INSERT";
		}
	case SQL_delete:
		{
			return "DELETE";
		}
	case SQL_update:
		{
			return "UPDATE";
		}
	default:
		{
			return "unknown";
		}
	}
}

void
qof_sql_query_parse (QofSqlQuery * query, const char *str)
{
	GList *tables;
	char *buf;
	sql_select_statement *sss;
	sql_where *swear;

	if (!query)
		return;
	ENTER (" ");
	/* Delete old query, if any */
	if (query->qof_query)
	{
		qof_query_destroy (query->qof_query);
		sql_destroy (query->parse_result);
		query->qof_query = NULL;
	}

	/* Parse the SQL string */
	buf = g_strdup (str);
	query->parse_result = sql_parse (buf);
	g_free (buf);

	if (!query->parse_result)
	{
		LEAVE ("parse error");
		return;
	}

	if ((SQL_select != query->parse_result->type)
		&& (SQL_insert != query->parse_result->type))
	{
		LEAVE
			("currently, only SELECT or INSERT statements are supported, "
			"got type=%s", sql_type_as_string (query->parse_result->type));
		return;
	}

	/* If the user wrote "SELECT * FROM tablename WHERE ..."
	 * then we have a single global tablename.  But if the 
	 * user wrote "SELECT * FROM tableA, tableB WHERE ..."
	 * then we don't have a single unique table-name.
	 */
	tables = sql_statement_get_tables (query->parse_result);
	if (1 == g_list_length (tables))
	{
		query->single_global_tablename = tables->data;
	}
	/* if this is an insert, we're done with the parse. */
	if (SQL_insert == query->parse_result->type)
	{
		query->qof_query = qof_query_create ();
		qof_query_set_insert_table (query);
		LEAVE (" insert statement parsed OK");
		return;
	}
	sss = query->parse_result->statement;
	swear = sss->where;
	if (swear)
	{
		/* Walk over the where terms, turn them into QOF predicates */
		query->qof_query = handle_where (query, swear);
		if (NULL == query->qof_query)
		{
			LEAVE (" no query found");
			return;
		}
	}
	else
	{
		query->qof_query = qof_query_create ();
	}
	/* Provide support for different sort orders */
	handle_sort_order (query, sss->order);

	/* We also want to set the type of thing to search for.
	 * SELECT * FROM table1, table2, ... is not supported.
	 * Use sequential queries and build a partial book.
	 */
	qof_query_search_for (query->qof_query,
		query->single_global_tablename);
	LEAVE (" success");
}

/* ========================================================== */

GList *
qof_sql_query_run (QofSqlQuery * query, const char *str)
{
	GList *results;

	if (!query)
		return NULL;

	qof_sql_query_parse (query, str);
	if (NULL == query->qof_query)
	{
		PINFO (" Null query");
		return NULL;
	}

	qof_query_set_book (query->qof_query, query->book);
	/* Maybe log this sucker */
	if (qof_log_check (log_module, QOF_LOG_DETAIL))
	{
		qof_query_print (query->qof_query);
	}
	if (SQL_insert == query->parse_result->type)
	{
		results = NULL;
		results = g_list_append (results, qof_query_insert (query));
		return results;
	}

	results = qof_query_run (query->qof_query);

	return results;
}

GList *
qof_sql_query_rerun (QofSqlQuery * query)
{
	GList *results;

	if (!query)
		return NULL;

	if (NULL == query->qof_query)
		return NULL;

	qof_query_set_book (query->qof_query, query->book);

	/* Maybe log this sucker */
	if (qof_log_check (log_module, QOF_LOG_DETAIL))
	{
		qof_query_print (query->qof_query);
	}

	results = qof_query_run (query->qof_query);

	return results;
}

/** simple helper struct for passing between SQL-generating functions */
typedef struct ent_and_string
{
	QofEntity * ent;
	gchar * str;
	gchar * kvp_str;
	gchar * full_kvp_path;
}eas;

static gulong kvp_id = 0;
static gboolean kvp_table_exists = FALSE;

static void
create_sql_from_param_cb (QofParam * param, gpointer user_data)
{
	eas * data;
	gchar * value;
	GList * references;

	g_return_if_fail (param);
	data = (eas*) user_data;
	ENTER ("%s %s", data->ent->e_type, param->param_name);
	g_return_if_fail (data->ent);
	if (!data->str)
		data->str = g_strdup ("");
	/* avoid creating database fields for calculated values */
	if (!param->param_setfcn)
		return;
	/* avoid setting KVP even if a param_setfcn has been set
	   because a QofSetterFunc for KVP is quite pointless. */
	if (0 == safe_strcmp (param->param_type, QOF_TYPE_KVP))
		return;
	references = qof_class_get_referenceList (data->ent->e_type);
	if (g_list_find (references, param))
	{
		/* \bug will need to use QofEntityReference here
		if partial books are actually to be supported. */
		QofEntity *e;
		e = param->param_getfcn (data->ent, param);
		value = g_strnfill (GUID_ENCODING_LENGTH + 1, ' ');
		guid_to_string_buff (qof_entity_get_guid (e), value);
		PINFO (" ref=%p GUID=%s", e, value);
	}
	else
		value = qof_util_param_to_string (data->ent, param);
	if (value)
		g_strescape (value, NULL);
	if (!value)
		value = g_strdup ("");
	if (!g_str_has_suffix (data->str, "("))
	{
		gchar *tmp;
		tmp = g_strjoin ("", data->str, ", '", value, "'", NULL);
		g_free (data->str);
		data->str = tmp;
	}
	else
	{
		gchar * tmp;
		tmp = g_strjoin ("", data->str, "'", value, "'", NULL);
		g_free (data->str);
		data->str = tmp;
	}
	LEAVE ("%s", data->str);
}

static gchar *
string_param_to_sql (QofParam * param)
{
	/** \note These strings are fairly standard SQL
	  but SQL means different things to different programs.
	  Keep these strings as simple and plain as possible.
	  Hopefully, what works in SQLite0 will work in most
	  other SQL based programs.
	  */
	/* Handle the entity GUID. Ensure that reference GUIDs
	   must not also try to be primary keys and can be NULL. */
	if ((0 == safe_strcmp (param->param_type, QOF_TYPE_GUID)) &&
		(0 == safe_strcmp (param->param_name, QOF_PARAM_GUID)))
		return g_strdup_printf (" %s char(32) primary key not null",
			param->param_name);
	if (0 == safe_strcmp (param->param_type, QOF_TYPE_GUID))
		return g_strdup_printf (" %s char(32)", param->param_name);
	/* avoid creating database fields for calculated values */
	if (!param->param_setfcn)
		return NULL;
	if (0 == safe_strcmp (param->param_type, QOF_TYPE_STRING))
		return g_strdup_printf (" %s mediumtext", param->param_name);
	if (0 == safe_strcmp (param->param_type, QOF_TYPE_BOOLEAN))
		return g_strdup_printf (" %s int", param->param_name);
	if ((0 == safe_strcmp (param->param_type, QOF_TYPE_NUMERIC))
		|| (0 == safe_strcmp (param->param_type, QOF_TYPE_DOUBLE))
		|| (0 == safe_strcmp (param->param_type, QOF_TYPE_DEBCRED)))
	{
		return g_strdup_printf (" %s text", param->param_name);
	}
	if (0 == safe_strcmp (param->param_type, QOF_TYPE_INT32))
		return g_strdup_printf (" %s int", param->param_name);
	if (0 == safe_strcmp (param->param_type, QOF_TYPE_TIME))
		return g_strdup_printf (" %s datetime", param->param_name);
	if (0 == safe_strcmp (param->param_type, QOF_TYPE_CHAR))
		return g_strdup_printf (" %s char(1)", param->param_name);
	/* kvp data is stored separately - actually this is really
	   a no-op because entities do not need a param_setfcn for kvp data. */
	if (0 == safe_strcmp (param->param_type, QOF_TYPE_KVP))
		return g_strdup ("");
/**  \bug This won't work - collect types can vary unpredictably - need
	a KVP implementation. */
	if (0 == safe_strcmp (param->param_type, QOF_TYPE_COLLECT))
		return g_strdup ("");
//		return g_strdup_printf (" %s char(32)", param->param_name);
	/* catch references */
	return g_strdup_printf (" %s char(32)", param->param_name);
}

/* used by create/insert */
static void
string_param_foreach (QofParam * param, gpointer user_data)
{
	gchar *p_str, * tmp;
	eas * data;

	PINFO ("string_param_foreach:%s", param->param_name);
	data = (eas *)user_data;
	if ((0 == safe_strcmp (param->param_type, QOF_TYPE_KVP)) &&
		(kvp_table_exists == FALSE))
	{
		g_free (data->kvp_str);
		data->kvp_str = 
			g_strdup_printf (" CREATE TABLE %s (%s, %s, %s, %s, %s, %s",
			kvp_table_name, "kvp_id int primary key not null",
			"guid char(32)", "path mediumtext", "type mediumtext",
			"value text", END_DB_VERSION);
		return;
	}
	p_str = string_param_to_sql (param);
	/* skip empty values (no param_setfcn) */
	if (!p_str)
		return;
	if (!data->str)
		data->str = g_strdup("");
	tmp = g_strjoin ("", data->str, p_str, ",", NULL);
	g_free (data->str);
	data->str = tmp;
	g_free (p_str);
}

/** \brief list just the parameter names

 \note Must match the number and order of the
list of parameter values from ::create_each_param
*/
static void
create_param_list (QofParam * param, gpointer user_data)
{
	eas * data = (eas *)user_data;
	g_return_if_fail (data->str);
	/* avoid creating database fields for calculated values */
	if (!param->param_setfcn)
		return;
	/* avoid setting KVP even if a param_setfcn has been set
	   because a QofSetterFunc for KVP is quite pointless. */
	if (0 == safe_strcmp (param->param_type, QOF_TYPE_KVP))
	{
		PINFO (" kvp support tag");
		return;
	}
	if (!g_str_has_suffix (data->str, "("))
	{
		gchar *add;
		add = g_strconcat (data->str, ", ", param->param_name, NULL);
		g_free (data->str);
		data->str = add;
	}
	else
	{
		gchar * add;
		add = g_strjoin ("", data->str, param->param_name, NULL);
		g_free (data->str);
		data->str = add;
	}
}

/** returns the VALUES for INSERT in pre-defined order */
static void
kvpvalue_to_sql_insert (const gchar * key, KvpValue * val, gpointer user_data)
{
	eas * data;
	KvpValueType n;
	gchar * path;

	path = g_strdup("");
	ENTER (" ");
	data = (eas*)user_data;
	g_return_if_fail (key && val && data);
	n = kvp_value_get_type (val);
	switch (n)
	{
	case KVP_TYPE_GINT64:
	case KVP_TYPE_DOUBLE:
	case KVP_TYPE_NUMERIC:
	case KVP_TYPE_STRING:
	case KVP_TYPE_GUID:
	case KVP_TYPE_TIME:
	case KVP_TYPE_BOOLEAN:
		{
			path = g_strjoin ("/", data->full_kvp_path, key, NULL);
			data->str =
				g_strdup_printf ("'%s', '%s', '%s'", key, path,
				kvp_value_to_bare_string (val));
			DEBUG (" %s", data->str);
			break;
		}
	case KVP_TYPE_FRAME:
		{
			path = g_strjoin ("/", data->full_kvp_path, key, NULL);
			g_free (data->full_kvp_path);
			data->full_kvp_path = path;
			kvp_frame_for_each_slot (kvp_value_get_frame (val),
				kvpvalue_to_sql_insert, data);
			break;
		}
	default:
		{
			PERR (" unsupported value = %d", kvp_value_get_type (val));
			break;
		}
	}
	LEAVE (" %s", data->str);
}

/** returns the VALUES for UPDATE in pre-defined order */
static void
kvpvalue_to_sql_update (const gchar * key, KvpValue * val, gpointer user_data)
{
	eas * data;
	KvpValueType n;
	gchar * path;

	ENTER (" key=%s", key);
	data = (eas*)user_data;
	g_return_if_fail (key && val && data);
	n = kvp_value_get_type (val);
	switch (n)
	{
	case KVP_TYPE_GINT64:
	case KVP_TYPE_DOUBLE:
	case KVP_TYPE_NUMERIC:
	case KVP_TYPE_STRING:
	case KVP_TYPE_GUID:
	case KVP_TYPE_TIME:
	case KVP_TYPE_BOOLEAN:
		{
			path = g_strjoin ("/", data->full_kvp_path, key, NULL);
			data->str =
				g_strdup_printf ("type='%s', value='%s' WHERE path='%s' and ", 
					key, kvp_value_to_bare_string (val), path);
			DEBUG (" %s", data->str);
			break;
		}
	case KVP_TYPE_FRAME:
		{
			path = g_strjoin ("/", data->full_kvp_path, key, NULL);
			g_free (data->full_kvp_path);
			data->full_kvp_path = path;
			kvp_frame_for_each_slot (kvp_value_get_frame (val),
				kvpvalue_to_sql_update, data);
			break;
		}
	default:
		{
			PERR (" unsupported value = %d", kvp_value_get_type (val));
			break;
		}
	}
	LEAVE (" %s", data->str);
}

gchar *
qof_sql_object_create_table (QofObject * obj)
{
	gchar * sql_str, * start;
	eas data;

	if (!kvp_table_name)
		kvp_table_name = g_strdup(QSQL_KVP_TABLE);
	ENTER ("create table for %s", obj->e_type);
	start = g_strdup_printf ("CREATE TABLE %s (", obj->e_type);
	data.ent = NULL;
	data.str = g_strdup("");
	data.kvp_str = g_strdup("");
	qof_class_param_foreach (obj->e_type, string_param_foreach, &data);
	sql_str = g_strjoin ("", start, data.str, END_DB_VERSION, data.kvp_str, NULL);
	g_free (start);
	g_free (data.kvp_str);
	g_free (data.str);
	LEAVE ("sql_str=%s", sql_str);
	return sql_str;
}

gchar *
qof_sql_entity_create_table (QofEntity * ent)
{
	gchar * sql_str, * start;
	eas data;

	g_return_val_if_fail (ent, NULL);
	if (!kvp_table_name)
		kvp_table_name = g_strdup(QSQL_KVP_TABLE);
	ENTER ("create table for %s", ent->e_type);
	start = g_strdup_printf ("CREATE TABLE %s (", ent->e_type);
	data.ent = ent;
	data.str = g_strdup("");
	data.kvp_str = g_strdup("");
	qof_class_param_foreach (ent->e_type, string_param_foreach, &data);
	sql_str = g_strjoin ("", start, data.str, END_DB_VERSION, data.kvp_str, NULL);
	g_free (start);
	LEAVE ("sql_str=%s", sql_str);
	return sql_str;
}

gchar *
qof_sql_entity_insert (QofEntity * ent)
{
	KvpFrame * slots;
	eas data;
	gchar * command, * fields, * values, *id, *gstr, *sql_str, *kvp;

	data.ent = ent;
	data.str = g_strdup("");
	if (!kvp_table_name)
		kvp_table_name = g_strdup(QSQL_KVP_TABLE);
	ENTER (" insert a single '%s'", ent->e_type);
	gstr = g_strnfill (GUID_ENCODING_LENGTH + 1, ' ');
	guid_to_string_buff (qof_instance_get_guid ((QofInstance *) ent), gstr);
	DEBUG (" guid=%s", gstr);
	command = g_strdup_printf ("INSERT into %s (guid ", ent->e_type);
	// store param list in fields
	qof_class_param_foreach (ent->e_type, create_param_list, &data);
	fields = g_strdup(data.str);
	// store param values in values
	g_free (data.str);
	data.str = g_strdup("");
	data.full_kvp_path = g_strdup("");
	qof_class_param_foreach (ent->e_type, create_sql_from_param_cb, &data);
	values = data.str;
	/* handle KVP */
	kvp = g_strdup("");
	slots = qof_instance_get_slots ((QofInstance *) ent);
	if (!kvp_frame_is_empty(slots))
	{
		id = g_strdup_printf ("%lu", kvp_id);
		g_free (kvp);
		kvp_frame_for_each_slot (slots, kvpvalue_to_sql_insert, &data);
		kvp = g_strconcat (" INSERT into ", kvp_table_name,
			"  (kvp_id, guid, type, path, value) VALUES ('", id, "', '", 
			gstr, "', ", data.str, ");", NULL);
		/* increment the index value of the KVP table */
		kvp_id++;
		g_free (data.str);
	}
	sql_str = g_strjoin ("", command, fields, ") VALUES ('", gstr, "' ", 
		values, ");", kvp, NULL);
	g_free (command);
	g_free (fields);
	g_free (gstr);
	g_free (values);
	g_free (data.full_kvp_path);
	LEAVE ("sql_str=%s", sql_str);
	return sql_str;
}

static void
collect_kvp (QofEntity * ent, gpointer user_data)
{
	KvpFrame * slots;
	KvpValue * collguid;
	gchar * gstr, * name, * key;
	const QofParam * G_GNUC_UNUSED param;

	name = (gchar *) user_data;
	param = qof_class_get_parameter (ent->e_type, name);
	key = g_strconcat ("collection/", name, "/guid/", NULL);
	gstr = g_strnfill (GUID_ENCODING_LENGTH + 1, ' ');
	guid_to_string_buff (qof_instance_get_guid ((QofInstance*)ent), gstr);
	collguid = kvp_value_new_string(gstr);
	slots = qof_instance_get_slots((QofInstance*)ent);
	kvp_frame_set_value (slots, key, collguid);
	g_free (key);
	g_free (gstr);
}

gchar *
qof_sql_entity_update (QofEntity * ent)
{
	gchar *gstr, * sql_str, * param_str;
	QofInstance * inst;
	const QofParam * param;
	inst = (QofInstance*)ent;

	if (!inst->param)
		return NULL;
	ENTER (" modified %s param:%s", ent->e_type, inst->param->param_name);
	gstr = g_strnfill (GUID_ENCODING_LENGTH + 1, ' ');
	guid_to_string_buff (qof_instance_get_guid (inst), gstr);
	param = inst->param;
	if (0 == safe_strcmp (param->param_type, QOF_TYPE_COLLECT))
	{
		gchar * name;
		QofCollection * coll;

		coll = param->param_getfcn (ent, param);
		name = g_strdup (param->param_name);
		qof_collection_foreach (coll, collect_kvp, name);
		g_free (name);
		return NULL;
	}
	param_str = qof_util_param_to_string (ent, inst->param);
	if (param_str)
		g_strescape (param_str, NULL);
	sql_str = g_strconcat ("UPDATE ", ent->e_type, " SET ",
		inst->param->param_name, " = '", param_str,
		"' WHERE ", QOF_TYPE_GUID, "='", gstr, "';", NULL);
	LEAVE ("sql_str=%s", sql_str);
	return sql_str;
}

gchar *
qof_sql_entity_update_kvp (QofEntity * ent)
{
	eas data;
	gchar *gstr, * sql_str;
	QofInstance * inst;
	gchar * start;
	KvpFrame * slots;
	inst = (QofInstance*)ent;

	if (!inst->param)
		return NULL;
	sql_str = NULL;
	if (kvp_frame_is_empty (qof_instance_get_slots ((QofInstance*)ent)))
		return NULL;

	ENTER (" modified %s param:%s", ent->e_type, inst->param->param_name);
	gstr = g_strnfill (GUID_ENCODING_LENGTH + 1, ' ');
	guid_to_string_buff (qof_instance_get_guid (inst), gstr);
	data.str = g_strdup("");
	data.full_kvp_path = g_strdup("");
	slots = qof_instance_get_slots ((QofInstance*)ent);
	start = g_strjoin ("", "UPDATE ", kvp_table_name, " SET ", NULL);
	/** \note the WHERE condition tests the path and the GUID
	as each entity (one GUID) can have more than one KVP. */
	kvp_frame_for_each_slot (slots, kvpvalue_to_sql_update, &data);
	sql_str = g_strjoin ("", start, data.str, " guid='", gstr, "';", NULL);
	g_free (start);
	g_free (data.full_kvp_path);
	g_free (data.str);
	LEAVE ("sql_str=%s", sql_str);
	return sql_str;
}

gchar *
qof_sql_entity_update_list (QofEntity * ent, GList **params)
{
	return NULL;
}

gchar *
qof_sql_entity_delete (QofEntity * ent)
{
	gchar * gstr, * sql_str;
	ENTER (" %s", ent->e_type);
	gstr = g_strnfill (GUID_ENCODING_LENGTH + 1, ' ');
	guid_to_string_buff (qof_entity_get_guid (ent), gstr);
	sql_str = g_strconcat ("DELETE from ", ent->e_type, " WHERE ",
		QOF_TYPE_GUID, "='", gstr, "';", "DELETE from ", kvp_table_name, 
		" WHERE kvp_id ", "='", gstr, "';", NULL);
	g_free (gstr);
	return sql_str;
}

gchar *
qof_sql_entity_drop_table (QofEntity * ent)
{
	gchar * sql_str;
	ENTER (" drop table for '%s'", ent->e_type);
	sql_str = g_strdup_printf ("DROP TABLE %s;", ent->e_type);
	LEAVE ("sql_str=%s", sql_str);
	return sql_str;
}

void qof_sql_entity_set_kvp_tablename (const gchar * name)
{
	g_return_if_fail (name);
	kvp_table_name = g_strdup(name);
}

void qof_sql_entity_set_kvp_id (gulong id)
{
	kvp_id = id;
}

gulong qof_sql_entity_get_kvp_id (void)
{
	return kvp_id;
}

void qof_sql_entity_set_kvp_exists (gboolean exist)
{
	kvp_table_exists = exist;
}

/* ========================== END OF FILE =================== */
