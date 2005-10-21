/********************************************************************\
 * qof_query.c -- Implement predicate API for searching for objects *
 * Copyright (C) 2002 Derek Atkins <warlord@MIT.EDU>                *
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
 * 59 Temple Place - Suite 330        Fax:    +1-617-542-2652       *
 * Boston, MA  02111-1307,  USA       gnu@gnu.org                   *
 *                                                                  *
\********************************************************************/

#include "config.h"

#include <sys/types.h>
#include <time.h>
#include <glib.h>
#include <regex.h>
#include <string.h>

#include "gnc-trace.h"
#include "gnc-engine-util.h"

#include "qofbackend-p.h"
#include "qofbook.h"
#include "qofbook-p.h"
#include "qofclass.h"
#include "qofclass-p.h"
#include "qofobject.h"
#include "qofquery.h"
#include "qofquery-p.h"
#include "qofquerycore.h"
#include "qofquerycore-p.h"

static QofLogModule log_module = QOF_MOD_QUERY;

struct _QofQueryTerm 
{
  GSList *                param_list;
  QofQueryPredData        *pdata;
  gboolean                invert;

  /* These values are filled in during "compilation" of the query
   * term, based upon the obj_name, param_name, and searched-for
   * object type.  If conv_fcn is NULL, then we don't know how to
   * convert types.
   */
  GSList *                param_fcns;
  QofQueryPredicateFunc   pred_fcn;
};

struct _QofQuerySort 
{
  GSList *            param_list;
  gint                options;
  gboolean            increasing;

  /* These values are filled in during "compilation" of the query
   * term, based upon the obj_name, param_name, and searched-for
   * object type.  If conv_fcn is NULL, then we don't know how to
   * convert types.
   */
  gboolean            use_default;
  GSList *            param_fcns;     /* Chain of paramters to walk */
  QofSortFunc         obj_cmp;        /* In case you are comparing objects */
  QofCompareFunc      comp_fcn;       /* When you are comparing core types */
};

/* The QUERY structure */
struct _QofQuery 
{
  /* The object type that we're searching for */
  QofIdType         search_for;

  /* terms is a list of the OR-terms in a sum-of-products 
   * logical expression. */
  GList *           terms;  

  /* sorting and chopping is independent of the search filter */

  QofQuerySort      primary_sort;
  QofQuerySort      secondary_sort;
  QofQuerySort      tertiary_sort;
  QofSortFunc       defaultSort;        /* <- Computed from search_for */

  /* The maximum number of results to return */
  int               max_results;

  /* list of books that will be participating in the query */
  GList *           books;

  /* a map of book to backend-compiled queries */
  GHashTable*       be_compiled;

  /* cache the results so we don't have to run the whole search 
   * again until it's really necessary */
  int               changed;

  GList *           results;
};

typedef struct _QofQueryCB 
{
  QofQuery *        query;
  GList *           list;
  int               count;
} QofQueryCB;

/* initial_term will be owned by the new Query */
static void query_init (QofQuery *q, QofQueryTerm *initial_term)
{
  GList * or = NULL;
  GList *and = NULL;
  GHashTable *ht;

  if (initial_term) {
    or = g_list_alloc ();
    and = g_list_alloc ();
    and->data = initial_term;
    or->data = and;
  }

  if(q->terms)
    qof_query_clear (q);

  g_list_free (q->results);
  g_list_free (q->books);

  g_slist_free (q->primary_sort.param_list);
  g_slist_free (q->secondary_sort.param_list);
  g_slist_free (q->tertiary_sort.param_list);

  g_slist_free (q->primary_sort.param_fcns);
  g_slist_free (q->secondary_sort.param_fcns);
  g_slist_free (q->tertiary_sort.param_fcns);

  ht = q->be_compiled;
  memset (q, 0, sizeof (*q));
  q->be_compiled = ht;

  q->terms = or;
  q->changed = 1;
  q->max_results = -1;

  q->primary_sort.param_list = g_slist_prepend (NULL, QUERY_DEFAULT_SORT);
  q->primary_sort.increasing = TRUE;
  q->secondary_sort.increasing = TRUE;
  q->tertiary_sort.increasing = TRUE;
}

static void swap_terms (QofQuery *q1, QofQuery *q2)
{
  GList *g;

  if (!q1 || !q2) return;

  g = q1->terms;
  q1->terms = q2->terms;
  q2->terms = g;

  g = q1->books;
  q1->books = q2->books;
  q2->books = g;

  q1->changed = 1;
  q2->changed = 1;
}

static void free_query_term (QofQueryTerm *qt)
{
  if (!qt) return;

  qof_query_core_predicate_free (qt->pdata);
  g_slist_free (qt->param_list);
  g_slist_free (qt->param_fcns);
  g_free (qt);
}

static QofQueryTerm * copy_query_term (QofQueryTerm *qt)
{
  QofQueryTerm *new_qt;
  if (!qt) return NULL;

  new_qt = g_new0 (QofQueryTerm, 1);
  memcpy (new_qt, qt, sizeof(QofQueryTerm));
  new_qt->param_list = g_slist_copy (qt->param_list);
  new_qt->param_fcns = g_slist_copy (qt->param_fcns);
  new_qt->pdata = qof_query_core_predicate_copy (qt->pdata);
  return new_qt;
}

static GList * copy_and_terms (GList *and_terms)
{
  GList *and = NULL;
  GList *cur_and;

  for(cur_and = and_terms; cur_and; cur_and = cur_and->next)
  {
    and = g_list_prepend(and, copy_query_term (cur_and->data));
  }

  return g_list_reverse(and);
}

static GList * 
copy_or_terms(GList * or_terms) 
{
  GList * or = NULL;
  GList * cur_or;

  for(cur_or = or_terms; cur_or; cur_or = cur_or->next)
  {
    or = g_list_prepend(or, copy_and_terms(cur_or->data));
  }

  return g_list_reverse(or);
}

static void copy_sort (QofQuerySort *dst, const QofQuerySort *src)
{
  memcpy (dst, src, sizeof (*dst));
  dst->param_list = g_slist_copy (src->param_list);
  dst->param_fcns = g_slist_copy (src->param_fcns);
}

static void free_sort (QofQuerySort *s)
{
  g_slist_free (s->param_list);
  s->param_list = NULL;

  g_slist_free (s->param_fcns);
  s->param_fcns = NULL;
}

static void free_members (QofQuery *q)
{
  GList * cur_or;

  if (q == NULL) return;

  for(cur_or = q->terms; cur_or; cur_or = cur_or->next) 
  {
    GList * cur_and;

    for(cur_and = cur_or->data; cur_and; cur_and = cur_and->next) 
    {
      free_query_term(cur_and->data);
      cur_and->data = NULL;
    }

    g_list_free(cur_or->data);
    cur_or->data = NULL;
  }

  free_sort (&(q->primary_sort));
  free_sort (&(q->secondary_sort));
  free_sort (&(q->tertiary_sort));

  g_list_free(q->terms);
  q->terms = NULL;

  g_list_free(q->books);
  q->books = NULL;

  g_list_free(q->results);
  q->results = NULL;
}

static int cmp_func (QofQuerySort *sort, QofSortFunc default_sort,
                     gconstpointer a, gconstpointer b)
{
  QofParam *param = NULL;
  GSList *node;
  gpointer conva, convb;

  g_return_val_if_fail (sort, 0);

  /* See if this is a default sort */
  if (sort->use_default) 
  {
    if (default_sort) return default_sort ((gpointer)a, (gpointer)b);
    return 0;
  }

  /* If no parameters, consider them equal */
  if (!sort->param_fcns) return 0;

  /* no compare function, consider the two objects equal */
  if (!sort->comp_fcn && !sort->obj_cmp) return 0;
  
  /* Do the list of conversions */
  conva = (gpointer)a;
  convb = (gpointer)b;
  for (node = sort->param_fcns; node; node = node->next) 
  {
    param = node->data;

    /* The last term is really the "parameter getter",
     * unless we're comparing objects ;) */
    if (!node->next && !sort->obj_cmp)
      break;

    /* Do the converstions */
    conva = (param->param_getfcn) (conva, param);
    convb = (param->param_getfcn) (convb, param);
  }

  /* And now return the (appropriate) compare */
  if (sort->comp_fcn)
  {
    int rc = sort->comp_fcn (conva, convb, sort->options, param);
    return rc;
  }

  return sort->obj_cmp (conva, convb);
}

static QofQuery * sortQuery = NULL;

static int sort_func (gconstpointer a, gconstpointer b)
{
  int retval;

  g_return_val_if_fail (sortQuery, 0);

  retval = cmp_func (&(sortQuery->primary_sort), sortQuery->defaultSort, a, b);
  if (retval == 0) 
  {
    retval = cmp_func (&(sortQuery->secondary_sort), sortQuery->defaultSort,
                       a, b);
    if (retval == 0) 
    {
      retval = cmp_func (&(sortQuery->tertiary_sort), sortQuery->defaultSort,
                         a, b);
      return sortQuery->tertiary_sort.increasing ? retval : -retval;
    } 
    else 
    {
      return sortQuery->secondary_sort.increasing ? retval : -retval;
    }
  } 
  else 
  {
    return sortQuery->primary_sort.increasing ? retval : -retval;
  }
}

/* ==================================================================== */ 
/* This is the main workhorse for performing the query.  For each
 * object, it walks over all of the query terms to see if the 
 * object passes the seive.
 */

static int 
check_object (QofQuery *q, gpointer object)
{
  GList     * and_ptr;
  GList     * or_ptr;
  QofQueryTerm * qt;
  int       and_terms_ok=1;
  
  ENTER (" object=%p terms=%p name=%s", 
          object, q->terms, qof_object_printable (q->search_for, object));

  for(or_ptr = q->terms; or_ptr; or_ptr = or_ptr->next) 
  {
    and_terms_ok = 1;
    for(and_ptr = or_ptr->data; and_ptr; and_ptr = and_ptr->next) 
    {
      qt = (QofQueryTerm *)(and_ptr->data);
      if (qt->param_fcns && qt->pred_fcn) 
      {
        GSList *node;
        QofParam *param = NULL;
        gpointer conv_obj = object;

        /* iterate through the conversions */
        for (node = qt->param_fcns; node; node = node->next) 
        {
          param = node->data;

          /* The last term is the actual parameter getter */
          if (!node->next) break;

          conv_obj = param->param_getfcn (conv_obj, param);
        }

        if (((qt->pred_fcn)(conv_obj, param, qt->pdata)) == qt->invert) 
        {
          and_terms_ok = 0;
          break;
        }
      } 
      else 
      {
        /* XXX: Don't know how to do this conversion -- do we care? */
      }
    }
    if (and_terms_ok) 
    {
      LEAVE (" (terms are OK)");
      return 1;
    }
  }

  /* If there are no terms, assume a "match any" applies.
   * A query with no terms is still meaningful, since the user
   * may want to get all objects, but in a particular sorted 
   * order.
   */
  LEAVE (" ");
  if (NULL == q->terms) return 1;
  return 0;
}

/* walk the list of parameters, starting with the given object, and
 * compile the list of parameter get-functions.  Save the last valid
 * parameter definition in "final" and return the list of functions.
 *
 * returns NULL if the first parameter is bad (and final is unchanged).
 */
static GSList * 
compile_params (GSList *param_list, QofIdType start_obj, 
                QofParam const **final)
{
  const QofParam *objDef = NULL;
  GSList *fcns = NULL;

  ENTER ("param_list=%p id=%s", param_list, start_obj);
  g_return_val_if_fail (param_list, NULL);
  g_return_val_if_fail (start_obj, NULL);
  g_return_val_if_fail (final, NULL);

  for (; param_list; param_list = param_list->next) 
  {
    QofIdType param_name = param_list->data;
    objDef = qof_class_get_parameter (start_obj, param_name);

    /* If it doesn't exist, then we've reached the end */
    if (!objDef) break;

    /* Save off this parameter */
    fcns = g_slist_prepend (fcns, (gpointer) objDef);

    /* Save this off, just in case */
    *final = objDef;

    /* And reset for the next parameter */
    start_obj = (QofIdType) objDef->param_type;
  }

  LEAVE ("fcns=%p", fcns);
  return (g_slist_reverse (fcns));
}

static void 
compile_sort (QofQuerySort *sort, QofIdType obj)
{
  const QofParam *resObj = NULL;

  ENTER ("sort=%p id=%s params=%p", sort, obj, sort->param_list);
  sort->use_default = FALSE;

  g_slist_free (sort->param_fcns);
  sort->param_fcns = NULL;
  sort->comp_fcn = NULL;
  sort->obj_cmp = NULL;

  /* An empty param_list implies "no sort" */
  if (!sort->param_list) { LEAVE (" "); return; }

  /* Walk the parameter list of obtain the parameter functions */
  sort->param_fcns = compile_params (sort->param_list, obj, &resObj);

  /* If we have valid parameters, grab the compare function,
   * If not, check if this is the default sort.
   */
  if (sort->param_fcns) 
  {
    sort->comp_fcn = qof_query_core_get_compare (resObj->param_type);

    /* Hrm, perhaps this is an object compare, not a core compare? */
    if (sort->comp_fcn == NULL)
    {
      sort->obj_cmp = qof_class_get_default_sort (resObj->param_type);
    }
  } 
  else if (!safe_strcmp (sort->param_list->data, QUERY_DEFAULT_SORT))
  {
    sort->use_default = TRUE;
  }
  LEAVE ("sort=%p id=%s", sort, obj);
}

static void compile_terms (QofQuery *q)
{
  GList *or_ptr, *and_ptr, *node;

  ENTER (" query=%p", q);
  /* Find the specific functions for this Query.  Note that the
   * Query's search_for should now be set to the new type.
   */
  for (or_ptr = q->terms; or_ptr; or_ptr = or_ptr->next) {
    for (and_ptr = or_ptr->data; and_ptr; and_ptr = and_ptr->next) {
      QofQueryTerm *qt = and_ptr->data;
      const QofParam *resObj = NULL;
      
      g_slist_free (qt->param_fcns);
      qt->param_fcns = NULL;

      /* Walk the parameter list of obtain the parameter functions */
      qt->param_fcns = compile_params (qt->param_list, q->search_for,
                                       &resObj);

      /* If we have valid parameters, grab the predicate function,
       * If not, see if this is the default sort.
       */

      if (qt->param_fcns)
        qt->pred_fcn = qof_query_core_get_predicate (resObj->param_type);
      else
        qt->pred_fcn = NULL;
    }
  }

  /* Update the sort functions */
  compile_sort (&(q->primary_sort), q->search_for);
  compile_sort (&(q->secondary_sort), q->search_for);
  compile_sort (&(q->tertiary_sort), q->search_for);

  q->defaultSort = qof_class_get_default_sort (q->search_for);

  /* Now compile the backend instances */
  for (node = q->books; node; node = node->next) {
    QofBook *book = node->data;
    QofBackend *be = book->backend;

    if (be && be->compile_query) {
      gpointer result = (be->compile_query)(be, q);
      if (result)
        g_hash_table_insert (q->be_compiled, book, result);
    }
  }
  LEAVE (" query=%p", q);
}

static void check_item_cb (gpointer object, gpointer user_data)
{
  QofQueryCB *ql = user_data;

  if (!object || !ql) return;

  if (check_object (ql->query, object)) {
    ql->list = g_list_prepend (ql->list, object);
    ql->count++;
  }
  return;
}

static int param_list_cmp (GSList *l1, GSList *l2)
{
  while (1) {
    int ret;

    /* Check the easy stuff */
    if (!l1 && !l2) return 0;
    if (!l1 && l2) return -1;
    if (l1 && !l2) return 1;

    ret = safe_strcmp (l1->data, l2->data);
    if (ret)
      return ret;

    l1 = l1->next;
    l2 = l2->next;
  }
}

static GList * merge_books (GList *l1, GList *l2)
{
  GList *res = NULL;
  GList *node;

  res = g_list_copy (l1);

  for (node = l2; node; node = node->next) {
    if (g_list_index (res, node->data) == -1)
      res = g_list_prepend (res, node->data);
  }

  return res;
}

static gboolean
query_free_compiled (gpointer key, gpointer value, gpointer not_used)
{
  QofBook* book = key;
  QofBackend* be = book->backend;

  if (be && be->free_query)
    (be->free_query)(be, value);

  return TRUE;
}

/* clear out any cached query_compilations */
static void query_clear_compiles (QofQuery *q)
{
  g_hash_table_foreach_remove (q->be_compiled, query_free_compiled, NULL);
}

/********************************************************************/
/* PUBLISHED API FUNCTIONS */

GSList *
qof_query_build_param_list (char const *param, ...)
{
  GSList *param_list = NULL;
  char const *this_param;
  va_list ap;

  if (!param)
    return NULL;

  va_start (ap, param);

  for (this_param = param; this_param; this_param = va_arg (ap, const char *))
    param_list = g_slist_prepend (param_list, (gpointer)this_param);

  va_end (ap);

  return g_slist_reverse (param_list);
}

void qof_query_add_term (QofQuery *q, GSList *param_list,                      
                      QofQueryPredData *pred_data, QofQueryOp op)
{
  QofQueryTerm *qt;
  QofQuery *qr, *qs;

  if (!q || !param_list || !pred_data) return;

  qt = g_new0 (QofQueryTerm, 1);
  qt->param_list = param_list;
  qt->pdata = pred_data;
  qs = qof_query_create ();
  query_init (qs, qt);

  if (qof_query_has_terms (q))
    qr = qof_query_merge (q, qs, op);
  else
    qr = qof_query_merge (q, qs, QOF_QUERY_OR);

  swap_terms (q, qr);
  qof_query_destroy (qs);
  qof_query_destroy (qr);
}

void qof_query_purge_terms (QofQuery *q, GSList *param_list)
{
  QofQueryTerm *qt;
  GList *or, *and;

  if (!q || !param_list) return;

  for (or = q->terms; or; or = or->next) {
    for (and = or->data; and; and = and->next) {
      qt = and->data;
      if (!param_list_cmp (qt->param_list, param_list)) {
        if (g_list_length (or->data) == 1) {
          q->terms = g_list_remove_link (q->terms, or);
          g_list_free_1 (or);
          or = q->terms;
          break;
        } else {
          or->data = g_list_remove_link (or->data, and);
          g_list_free_1 (and);
          and = or->data;
          if (!and) break;
        }
        q->changed = 1;
        free_query_term (qt);
      }
    }
    if (!or) break;
  }
}

GList * qof_query_run (QofQuery *q)
{
  GList *matching_objects = NULL;
  GList *node;
  int        object_count = 0;

  if (!q) return NULL;
  g_return_val_if_fail (q->search_for, NULL);
  g_return_val_if_fail (q->books, NULL);
  ENTER (" q=%p", q);

  /* XXX: Prioritize the query terms? */

  /* prepare the Query for processing */
  if (q->changed) 
  {
    query_clear_compiles (q);
    compile_terms (q);
  }

  /* Maybe log this sucker */
  if (gnc_should_log (log_module, GNC_LOG_DETAIL)) qof_query_print (q);

  /* Now run the query over all the objects and save the results */
  {
    QofQueryCB qcb;

    memset (&qcb, 0, sizeof (qcb));
    qcb.query = q;

    /* For each book */
    for (node=q->books; node; node=node->next) 
    {
      QofBook *book = node->data;
      QofBackend *be = book->backend;

      /* run the query in the backend */
      if (be) 
      {
        gpointer compiled_query = g_hash_table_lookup (q->be_compiled, book);

        if (compiled_query && be->run_query)
        {
          (be->run_query) (be, compiled_query);
        }
      }

      /* And then iterate over all the objects */
      qof_object_foreach (q->search_for, book, (QofEntityForeachCB) check_item_cb, &qcb);
    }

    matching_objects = qcb.list;
    object_count = qcb.count;
  }
  PINFO ("matching objects=%p count=%d", matching_objects, object_count);

  /* There is no absolute need to reverse this list, since it's being
   * sorted below. However, in the common case, we will be searching
   * in a confined location where the objects are already in order,
   * thus reversing will put us in the correct order we want and make
   * the sorting go much faster.
   */
  matching_objects = g_list_reverse(matching_objects);

  /* Now sort the matching objects based on the search criteria
   * sortQuery is an unforgivable use of static global data...  
   * I just can't figure out how else to do this sanely.
   */
  if (q->primary_sort.comp_fcn || q->primary_sort.obj_cmp ||
      (q->primary_sort.use_default && q->defaultSort))
  {
    sortQuery = q;
    matching_objects = g_list_sort(matching_objects, sort_func);
    sortQuery = NULL;
  }

  /* Crop the list to limit the number of splits. */
  if((object_count > q->max_results) && (q->max_results > -1)) 
  {
    if(q->max_results > 0) 
    {
      GList *mptr;

      /* mptr is set to the first node of what will be the new list */
      mptr = g_list_nth(matching_objects, object_count - q->max_results);
      /* mptr should not be NULL, but let's be safe */
      if (mptr != NULL) 
      {
        if (mptr->prev != NULL) mptr->prev->next = NULL;
        mptr->prev = NULL;
      }
      g_list_free(matching_objects);
      matching_objects = mptr;
    }
    else 
    { 
      /* q->max_results == 0 */
      g_list_free(matching_objects);
      matching_objects = NULL;
    }
    object_count = q->max_results;
  }
  
  q->changed = 0;
  
  g_list_free(q->results);
  q->results = matching_objects;
  
  LEAVE (" q=%p", q);
  return matching_objects;
}

GList *
qof_query_last_run (QofQuery *query)
{
  if (!query)
    return NULL;

  return query->results;
}

void qof_query_clear (QofQuery *query)
{
  QofQuery *q2 = qof_query_create ();
  swap_terms (query, q2);
  qof_query_destroy (q2);

  g_list_free (query->books);
  query->books = NULL;
  g_list_free (query->results);
  query->results = NULL;
  query->changed = 1;
}

QofQuery * qof_query_create (void)
{
  QofQuery *qp = g_new0 (QofQuery, 1);
  qp->be_compiled = g_hash_table_new (g_direct_hash, g_direct_equal);
  query_init (qp, NULL);
  return qp;
}

void qof_query_search_for (QofQuery *q, QofIdTypeConst obj_type)
{
  if (!q || !obj_type)
    return;

  if (safe_strcmp (q->search_for, obj_type)) {
    q->search_for = (QofIdType) obj_type;
    q->changed = 1;
  }
}

QofQuery * qof_query_create_for (QofIdTypeConst obj_type)
{
  QofQuery *q;
  if (!obj_type)
    return NULL;
  q = qof_query_create ();
  qof_query_search_for (q, obj_type);
  return q;
}

int qof_query_has_terms (QofQuery *q)
{
  if (!q) return 0;
  return g_list_length (q->terms);
}

int qof_query_num_terms (QofQuery *q)
{
  GList *o;
  int n = 0;
  if (!q) return 0;
  for (o = q->terms; o; o=o->next)
    n += g_list_length(o->data);
  return n;
}

gboolean qof_query_has_term_type (QofQuery *q, GSList *term_param)
{
  GList *or;
  GList *and;

  if (!q || !term_param)
    return FALSE;

  for(or = q->terms; or; or = or->next) {
    for(and = or->data; and; and = and->next) {
      QofQueryTerm *qt = and->data;
      if (!param_list_cmp (term_param, qt->param_list))
        return TRUE;
    }
  }

  return FALSE;
}

GSList * qof_query_get_term_type (QofQuery *q, GSList *term_param)
{
  GList *or;
  GList *and;
  GSList *results = NULL;

  if (!q || !term_param)
    return FALSE;

  for(or = q->terms; or; or = or->next) {
    for(and = or->data; and; and = and->next) {
      QofQueryTerm *qt = and->data;
      if (!param_list_cmp (term_param, qt->param_list))
        results = g_slist_append(results, qt->pdata);
    }
  }

  return results;
}

void qof_query_destroy (QofQuery *q)
{
  if (!q) return;
  free_members (q);
  query_clear_compiles (q);
  g_hash_table_destroy (q->be_compiled);
  g_free (q);
}

QofQuery * qof_query_copy (QofQuery *q)
{
  QofQuery *copy;
  GHashTable *ht;

  if (!q) return NULL;
  copy = qof_query_create ();
  ht = copy->be_compiled;
  free_members (copy);

  memcpy (copy, q, sizeof (QofQuery));

  copy->be_compiled = ht;
  copy->terms = copy_or_terms (q->terms);
  copy->books = g_list_copy (q->books);
  copy->results = g_list_copy (q->results);

  copy_sort (&(copy->primary_sort), &(q->primary_sort));
  copy_sort (&(copy->secondary_sort), &(q->secondary_sort));
  copy_sort (&(copy->tertiary_sort), &(q->tertiary_sort));

  copy->changed = 1;

  return copy;
}

/* *******************************************************************
 * qof_query_invert 
 * return a newly-allocated Query object which is the 
 * logical inverse of the original.
 ********************************************************************/

QofQuery * qof_query_invert (QofQuery *q)
{
  QofQuery  * retval;
  QofQuery  * right, * left, * iright, * ileft;
  QofQueryTerm * qt;
  GList  * aterms;
  GList  * cur;
  GList  * new_oterm;
  int    num_or_terms;

  if (!q)
    return NULL;

  num_or_terms = g_list_length(q->terms);

  switch(num_or_terms) 
  {
  case 0:
    retval = qof_query_create();
    retval->max_results = q->max_results;
    break;

    /* This is the DeMorgan expansion for a single AND expression. */
    /* !(abc) = !a + !b + !c */
  case 1:
    retval = qof_query_create();
    retval->max_results = q->max_results;
    retval->books = g_list_copy (q->books);
    retval->search_for = q->search_for;
    retval->changed = 1;

    aterms = g_list_nth_data(q->terms, 0);
    new_oterm = NULL;
    for(cur=aterms; cur; cur=cur->next) {
      qt = copy_query_term(cur->data);
      qt->invert = !(qt->invert);
      new_oterm = g_list_append(NULL, qt);

      /* g_list_append() can take forever, so let's do this for speed
       * in "large" queries.
       */
      retval->terms = g_list_reverse(retval->terms);
      retval->terms = g_list_prepend(retval->terms, new_oterm);
      retval->terms = g_list_reverse(retval->terms);
    }
    break;

    /* If there are multiple OR-terms, we just recurse by 
     * breaking it down to !(a + b + c) = 
     * !a * !(b + c) = !a * !b * !c.  */
  default:
    right        = qof_query_create();
    right->terms = copy_or_terms(g_list_nth(q->terms, 1));

    left         = qof_query_create();
    left->terms  = g_list_append(NULL, 
                                 copy_and_terms(g_list_nth_data(q->terms, 0)));

    iright       = qof_query_invert(right);
    ileft        = qof_query_invert(left);

    retval = qof_query_merge(iright, ileft, QOF_QUERY_AND);
    retval->books          = g_list_copy (q->books);
    retval->max_results    = q->max_results;
    retval->search_for     = q->search_for;
    retval->changed        = 1;

    qof_query_destroy(iright);
    qof_query_destroy(ileft);
    qof_query_destroy(right);
    qof_query_destroy(left);
    break;
  }

  return retval;
}

/* *******************************************************************
 * qof_query_merge
 * combine 2 Query objects by the logical operation in "op".
 ********************************************************************/

QofQuery * 
qof_query_merge(QofQuery *q1, QofQuery *q2, QofQueryOp op)
{
  
  QofQuery * retval = NULL;
  QofQuery * i1, * i2;
  QofQuery * t1, * t2;
  GList * i, * j;
  QofIdType search_for;

  if(!q1) return q2;
  if(!q2) return q1;

  if (q1->search_for && q2->search_for)
    g_return_val_if_fail (safe_strcmp (q1->search_for, q2->search_for) == 0,
                          NULL);

  search_for = (q1->search_for ? q1->search_for : q2->search_for);

  /* Avoid merge surprises if op==QOF_QUERY_AND but q1 is empty.
   * The goal of this tweak is to all the user to start with
   * an empty q1 and then append to it recursively
   * (and q1 (and q2 (and q3 (and q4 ....))))
   * without bombing out because the append started with an 
   * empty list.
   * We do essentially the same check in qof_query_add_term()
   * so that the first term added to an empty query doesn't screw up.
   */
  if ((QOF_QUERY_AND == op) && (0 == qof_query_has_terms (q1)))
  {
    op = QOF_QUERY_OR;
  }

  switch(op) 
  {
  case QOF_QUERY_OR:
    retval = qof_query_create();
    retval->terms = 
      g_list_concat(copy_or_terms(q1->terms), copy_or_terms(q2->terms));
    retval->books           = merge_books (q1->books, q2->books);
    retval->max_results    = q1->max_results;
    retval->changed        = 1;
    break;

  case QOF_QUERY_AND:
    retval = qof_query_create();
    retval->books          = merge_books (q1->books, q2->books);
    retval->max_results    = q1->max_results;
    retval->changed        = 1;

    /* g_list_append() can take forever, so let's build the list in
     * reverse and then reverse it at the end, to deal better with
     * "large" queries.
     */
    for(i=q1->terms; i; i=i->next) 
    {
      for(j=q2->terms; j; j=j->next) 
      {
        retval->terms = 
          g_list_prepend(retval->terms, 
                        g_list_concat
                        (copy_and_terms(i->data),
                         copy_and_terms(j->data)));
      }
    }
    retval->terms = g_list_reverse(retval->terms);
    break;

  case QOF_QUERY_NAND:
    /* !(a*b) = (!a + !b) */
    i1     = qof_query_invert(q1);
    i2     = qof_query_invert(q2);
    retval = qof_query_merge(i1, i2, QOF_QUERY_OR);
    qof_query_destroy(i1);
    qof_query_destroy(i2);
    break;

  case QOF_QUERY_NOR:
    /* !(a+b) = (!a*!b) */
    i1     = qof_query_invert(q1);
    i2     = qof_query_invert(q2);
    retval = qof_query_merge(i1, i2, QOF_QUERY_AND);
    qof_query_destroy(i1);
    qof_query_destroy(i2);
    break;

  case QOF_QUERY_XOR:
    /* a xor b = (a * !b) + (!a * b) */
    i1     = qof_query_invert(q1);
    i2     = qof_query_invert(q2);
    t1     = qof_query_merge(q1, i2, QOF_QUERY_AND);
    t2     = qof_query_merge(i1, q2, QOF_QUERY_AND);
    retval = qof_query_merge(t1, t2, QOF_QUERY_OR);

    qof_query_destroy(i1);
    qof_query_destroy(i2);
    qof_query_destroy(t1);
    qof_query_destroy(t2);     
    break;
  }

  retval->search_for = search_for;
  return retval;
}

void
qof_query_merge_in_place(QofQuery *q1, QofQuery *q2, QofQueryOp op)
{
  QofQuery *tmp_q;

  if (!q1 || !q2)
    return;

  tmp_q = qof_query_merge (q1, q2, op);
  swap_terms (q1, tmp_q);
  qof_query_destroy (tmp_q);
}

void
qof_query_set_sort_order (QofQuery *q,
                      GSList *params1, GSList *params2, GSList *params3)
{
  if (!q) return;
  if (q->primary_sort.param_list)
    g_slist_free (q->primary_sort.param_list);
  q->primary_sort.param_list = params1;
  q->primary_sort.options = 0;

  if (q->secondary_sort.param_list)
    g_slist_free (q->secondary_sort.param_list);
  q->secondary_sort.param_list = params2;
  q->secondary_sort.options = 0;

  if (q->tertiary_sort.param_list)
    g_slist_free (q->tertiary_sort.param_list);
  q->tertiary_sort.param_list = params3;
  q->tertiary_sort.options = 0;

  q->changed = 1;
}

void qof_query_set_sort_options (QofQuery *q, gint prim_op, gint sec_op,
                             gint tert_op)
{
  if (!q) return;
  q->primary_sort.options = prim_op;
  q->secondary_sort.options = sec_op;
  q->tertiary_sort.options = tert_op;
}

void qof_query_set_sort_increasing (QofQuery *q, gboolean prim_inc,
                                gboolean sec_inc, gboolean tert_inc)
{
  if (!q) return;
  q->primary_sort.increasing = prim_inc;
  q->secondary_sort.increasing = sec_inc;
  q->tertiary_sort.increasing = tert_inc;
}

void qof_query_set_max_results (QofQuery *q, int n)
{
  if (!q) return;
  q->max_results = n;
}

void qof_query_add_guid_list_match (QofQuery *q, GSList *param_list,
                               GList *guid_list, QofGuidMatch options,
                               QofQueryOp op)
{
  QofQueryPredData *pdata;

  if (!q || !param_list) return;

  if (!guid_list)
    g_return_if_fail (options == QOF_GUID_MATCH_NULL);

  pdata = qof_query_guid_predicate (options, guid_list);
  qof_query_add_term (q, param_list, pdata, op);
}

void qof_query_add_guid_match (QofQuery *q, GSList *param_list,
                           const GUID *guid, QofQueryOp op)
{
  GList *g = NULL;

  if (!q || !param_list) return;

  if (guid)
    g = g_list_prepend (g, (gpointer)guid);

  qof_query_add_guid_list_match (q, param_list, g,
                            g ? QOF_GUID_MATCH_ANY : QOF_GUID_MATCH_NULL, op);

  g_list_free (g);
}

void qof_query_set_book (QofQuery *q, QofBook *book)
{
  GSList *slist = NULL;
  if (!q || !book) return;

  /* Make sure this book is only in the list once */
  if (g_list_index (q->books, book) == -1)
    q->books = g_list_prepend (q->books, book);

  g_slist_prepend (slist, QOF_PARAM_GUID);
  g_slist_prepend (slist, QOF_PARAM_BOOK);
  qof_query_add_guid_match (q, slist,
                        qof_book_get_guid(book), QOF_QUERY_AND);
}

GList * qof_query_get_books (QofQuery *q)
{
  if (!q) return NULL;
  return q->books;
}

void qof_query_add_boolean_match (QofQuery *q, GSList *param_list, gboolean value,
                              QofQueryOp op)
{
  QofQueryPredData *pdata;
  if (!q || !param_list) return;

  pdata = qof_query_boolean_predicate (QOF_COMPARE_EQUAL, value);
  qof_query_add_term (q, param_list, pdata, op);
}

/**********************************************************************/
/* PRIVATE PUBLISHED API FUNCTIONS                                    */

void qof_query_init (void)
{
  ENTER (" ");
  qof_query_core_init ();
  qof_class_init ();
}

void qof_query_shutdown (void)
{
  qof_class_shutdown ();
  qof_query_core_shutdown ();
}

int qof_query_get_max_results (QofQuery *q)
{
  if (!q) return 0;
  return q->max_results;
}

QofIdType qof_query_get_search_for (QofQuery *q)
{
  if (!q) return NULL;
  return q->search_for;
}

GList * qof_query_get_terms (QofQuery *q)
{
  if (!q) return NULL;
  return q->terms;
}

GSList * qof_query_term_get_param_path (QofQueryTerm *qt)
{
  if (!qt)
    return NULL;
  return qt->param_list;
}

QofQueryPredData *qof_query_term_get_pred_data (QofQueryTerm *qt)
{
  if (!qt)
    return NULL;
  return qt->pdata;
}

gboolean qof_query_term_is_inverted (QofQueryTerm *qt)
{
  if (!qt)
    return FALSE;
  return qt->invert;
}

void qof_query_get_sorts (QofQuery *q, QofQuerySort **primary,
                       QofQuerySort **secondary, QofQuerySort **tertiary)
{
  if (!q)
    return;
  if (primary)
    *primary = &(q->primary_sort);
  if (secondary)
    *secondary = &(q->secondary_sort);
  if (tertiary)
    *tertiary = &(q->tertiary_sort);
}

GSList * qof_query_sort_get_param_path (QofQuerySort *qs)
{
  if (!qs)
    return NULL;
  return qs->param_list;
}

gint qof_query_sort_get_sort_options (QofQuerySort *qs)
{
  if (!qs)
    return 0;
  return qs->options;
}

gboolean qof_query_sort_get_increasing (QofQuerySort *qs)
{
  if (!qs)
    return FALSE;
  return qs->increasing;
}

static gboolean 
qof_query_term_equal (QofQueryTerm *qt1, QofQueryTerm *qt2)
{
  if (qt1 == qt2) return TRUE;
  if (!qt1 || !qt2) return FALSE;

  if (qt1->invert != qt2->invert) return FALSE;
  if (param_list_cmp (qt1->param_list, qt2->param_list)) return FALSE;
  return qof_query_core_predicate_equal (qt1->pdata, qt2->pdata);
}

static gboolean 
qof_query_sort_equal (QofQuerySort* qs1, QofQuerySort* qs2)
{
  if (qs1 == qs2) return TRUE;
  if (!qs1 || !qs2) return FALSE;

  /* "Empty" sorts are equivalent, regardless of the flags */
  if (!qs1->param_list && !qs2->param_list) return TRUE;

  if (qs1->options != qs2->options) return FALSE;
  if (qs1->increasing != qs2->increasing) return FALSE;
  return (param_list_cmp (qs1->param_list, qs2->param_list) == 0);
}

gboolean qof_query_equal (QofQuery *q1, QofQuery *q2)
{
  GList *or1, *or2;

  if (q1 == q2) return TRUE;
  if (!q1 || !q2) return FALSE;

  if (g_list_length (q1->terms) != g_list_length (q2->terms)) return FALSE;
  if (q1->max_results != q2->max_results) return FALSE;

  for (or1 = q1->terms, or2 = q2->terms; or1;
       or1 = or1->next, or2 = or2->next)
  {
    GList *and1, *and2;

    and1 = or1->data;
    and2 = or2->data;

    if (g_list_length (and1) != g_list_length (and2)) return FALSE;

    for ( ; and1; and1 = and1->next, and2 = and2->next)
      if (!qof_query_term_equal (and1->data, and2->data))
        return FALSE;
  }

  if (!qof_query_sort_equal (&(q1->primary_sort), &(q2->primary_sort)))
    return FALSE;
  if (!qof_query_sort_equal (&(q1->secondary_sort), &(q2->secondary_sort)))
    return FALSE;
  if (!qof_query_sort_equal (&(q1->tertiary_sort), &(q2->tertiary_sort)))
    return FALSE;

  return TRUE;
}

/* **************************************************************************/
/* Query Print functions.  qof_query_print is public; everthing else supports
 * that.
 * Just call qof_query_print(QofQuery *q), and it will print out the query 
 * contents to stderr.
*/

/* Static prototypes */
static GList *qof_query_printSearchFor (QofQuery * query, GList * output);
static GList *qof_query_printTerms (QofQuery * query, GList * output);
static GList *qof_query_printSorts (QofQuerySort *s[], const gint numSorts,
                                  GList * output);
static GList *qof_query_printAndTerms (GList * terms, GList * output);
static gchar *qof_query_printStringForHow (QofQueryCompare how);
static gchar *qof_query_printStringMatch (QofStringMatch s);
static gchar *qof_query_printDateMatch (QofDateMatch d);
static gchar *qof_query_printNumericMatch (QofNumericMatch n);
static gchar *qof_query_printGuidMatch (QofGuidMatch g);
static gchar *qof_query_printCharMatch (QofCharMatch c);
static GString *qof_query_printPredData (QofQueryPredData *pd);
static GString *qof_query_printParamPath (GSList * parmList);
static void qof_query_printValueForParam (QofQueryPredData *pd, GString * gs);
static void qof_query_printOutput (GList * output);

/*
        This function cycles through a QofQuery object, and
        prints out the values of the various members of the query
*/
void
qof_query_print (QofQuery * query)
{
  GList *output;
  GString *str;
  QofQuerySort *s[3];
  gint maxResults = 0, numSorts = 3;

  ENTER (" ");

  if (!query) {
      LEAVE("query is (null)");
      return;
  }

  output = NULL;
  str = NULL;
  maxResults = qof_query_get_max_results (query);

  output = qof_query_printSearchFor (query, output);
  output = qof_query_printTerms (query, output);

  qof_query_get_sorts (query, &s[0], &s[1], &s[2]);

  if (s[0])
  {
    output = qof_query_printSorts (s, numSorts, output);
  }

  str = g_string_new (" ");
  g_string_sprintf (str, "Maximum number of results: %d", maxResults);
  output = g_list_append (output, str);

  qof_query_printOutput (output);
  LEAVE (" ");
}

static void
qof_query_printOutput (GList * output)
{
  GList *lst;

  for (lst = output; lst; lst = lst->next)
  {
    GString *line = (GString *) lst->data;

    fprintf (stderr, "%s\n", line->str);
    g_string_free (line, TRUE);
    line = NULL;
  }
}

/*
        Get the search_for type--This is the type of Object
        we are searching for (SPLIT, TRANS, etc)
*/
static GList *
qof_query_printSearchFor (QofQuery * query, GList * output)
{
  QofIdType searchFor;
  GString *gs;

  searchFor = qof_query_get_search_for (query);
  gs = g_string_new ("Query Object Type: ");
  g_string_append (gs, (NULL == searchFor)? "(null)" : searchFor);
  output = g_list_append (output, gs);

  return output;
}                               /* qof_query_printSearchFor */

/*
        Run through the terms of the query.  This is a outer-inner
        loop.  The elements of the outer loop are ORed, and the
        elements of the inner loop are ANDed.
*/
static GList *
qof_query_printTerms (QofQuery * query, GList * output)
{

  GList *terms, *lst;

  terms = qof_query_get_terms (query);

  for (lst = terms; lst; lst = lst->next)
  {
    output = g_list_append (output, g_string_new ("OR and AND Terms:"));

    if (lst->data)
    {
      output = qof_query_printAndTerms (lst->data, output);
    }
    else
    {
      output =
        g_list_append (output, g_string_new ("  No data for AND terms"));
    }
  }

  return output;
}                               /* qof_query_printTerms */

/*
        Process the sort parameters
        If this function is called, the assumption is that the first sort
        not null.
*/
static GList *
qof_query_printSorts (QofQuerySort *s[], const gint numSorts, GList * output)
{
  GSList *gsl, *n = NULL;
  gint curSort;
  GString *gs = g_string_new ("  Sort Parameters:\n");

  for (curSort = 0; curSort < numSorts; curSort++)
  {
    gboolean increasing;
    if (!s[curSort])
    {
      break;
    }
    increasing = qof_query_sort_get_increasing (s[curSort]);

    gsl = qof_query_sort_get_param_path (s[curSort]); 
    if (gsl) g_string_sprintfa (gs, "    Param: ");
    for (n=gsl; n; n = n->next)
    {
      QofIdType param_name = n->data;
      if (gsl != n)g_string_sprintfa (gs, "\n           ");
      g_string_sprintfa (gs, "%s", param_name);
    }
    if (gsl) 
    {
      g_string_sprintfa (gs, " %s\n", increasing ? "DESC" : "ASC");
      g_string_sprintfa (gs, "    Options: 0x%x\n", s[curSort]->options);
    }
  }

  output = g_list_append (output, gs);
  return output;

}                               /* qof_query_printSorts */

/*
        Process the AND terms of the query.  This is a GList
        of WHERE terms that will be ANDed
*/
static GList *
qof_query_printAndTerms (GList * terms, GList * output)
{
  const char *prefix = "  AND Terms:";
  QofQueryTerm *qt;
  QofQueryPredData *pd;
  GSList *path;
  GList *lst;
  gboolean invert;

  output = g_list_append (output, g_string_new (prefix));
  for (lst = terms; lst; lst = lst->next)
  {
    qt = (QofQueryTerm *) lst->data;
    pd = qof_query_term_get_pred_data (qt);
    path = qof_query_term_get_param_path (qt);
    invert = qof_query_term_is_inverted (qt);

    if (invert) output = g_list_append (output, 
                                     g_string_new("    INVERT SENSE "));
    output = g_list_append (output, qof_query_printParamPath (path));
    output = g_list_append (output, qof_query_printPredData (pd));
    output = g_list_append (output, g_string_new("\n"));
  }

  return output;
}                               /* qof_query_printAndTerms */

/*
        Process the parameter types of the predicate data
*/
static GString *
qof_query_printParamPath (GSList * parmList)
{
  GSList *list = NULL;
  GString *gs = g_string_new ("    Param List:\n");
  g_string_append (gs, "      ");
  for (list = parmList; list; list = list->next)
  {
    g_string_append (gs, (gchar *) list->data);
    if (list->next)
      g_string_append (gs, "->");
  }

  return gs;
}                               /* qof_query_printParamPath */

/*
        Process the PredData of the AND terms
*/
static GString *
qof_query_printPredData (QofQueryPredData *pd)
{
  GString *gs;

  gs = g_string_new ("    Pred Data:\n      ");
  g_string_append (gs, (gchar *) pd->type_name);

  /* Char Predicate and GUID predicate don't use the 'how' field. */
  if (safe_strcmp (pd->type_name, QOF_TYPE_CHAR) &&
      safe_strcmp (pd->type_name, QOF_TYPE_GUID))
  {
    g_string_sprintfa (gs, "\n      how: %s",
                       qof_query_printStringForHow (pd->how));
  }

  qof_query_printValueForParam (pd, gs);

  return gs;
}                               /* qof_query_printPredData */

/*
        Get a string representation for the
        QofCompareFunc enum type.
*/
static gchar *
qof_query_printStringForHow (QofQueryCompare how)
{

  switch (how)
  {
    case QOF_COMPARE_LT:
      return "QOF_COMPARE_LT";
    case QOF_COMPARE_LTE:
      return "QOF_COMPARE_LTE";
    case QOF_COMPARE_EQUAL:
      return "QOF_COMPARE_EQUAL";
    case QOF_COMPARE_GT:
      return "QOF_COMPARE_GT";
    case QOF_COMPARE_GTE:
      return "QOF_COMPARE_GTE";
    case QOF_COMPARE_NEQ:
      return "QOF_COMPARE_NEQ";
  }

  return "INVALID HOW";
}                               /* qncQueryPrintStringForHow */


static void
qof_query_printValueForParam (QofQueryPredData *pd, GString * gs)
{

  if (!safe_strcmp (pd->type_name, QOF_TYPE_GUID))
  {
    GList *node;
    query_guid_t pdata = (query_guid_t) pd;
    g_string_sprintfa (gs, "\n      Match type %s",
                       qof_query_printGuidMatch (pdata->options));
    for (node = pdata->guids; node; node = node->next)
    {
	  /* THREAD-UNSAFE */
      g_string_sprintfa (gs, ", guids: %s",
			 guid_to_string ((GUID *) node->data));
    }
    return;
  }
  if (!safe_strcmp (pd->type_name, QOF_TYPE_STRING))
  {
    query_string_t pdata = (query_string_t) pd;
    g_string_sprintfa (gs, "\n      Match type %s",
                       qof_query_printStringMatch (pdata->options));
    g_string_sprintfa (gs, " %s string: %s",
                       pdata->is_regex ? "Regex" : "Not regex",
                       pdata->matchstring);
    return;
  }
  if (!safe_strcmp (pd->type_name, QOF_TYPE_NUMERIC))
  {
    query_numeric_t pdata = (query_numeric_t) pd;
    g_string_sprintfa (gs, "\n      Match type %s",
                       qof_query_printNumericMatch (pdata->options));
    g_string_sprintfa (gs, " gnc_numeric: %s",
                       gnc_num_dbg_to_string (pdata->amount));
    return;
  }
  if (!safe_strcmp (pd->type_name, QOF_TYPE_KVP))
  {
    GSList *node;
    query_kvp_t pdata = (query_kvp_t) pd;
    g_string_sprintfa (gs, "\n      kvp path: ");
    for (node = pdata->path; node; node = node->next)
    {
      g_string_sprintfa (gs, "/%s", (gchar *) node->data);
    }
    g_string_sprintfa (gs, "\n");
    g_string_sprintfa (gs, "      kvp value: %s\n", 
                         kvp_value_to_string (pdata->value));
    return;
  }
  if (!safe_strcmp (pd->type_name, QOF_TYPE_INT64))
  {
    query_int64_t pdata = (query_int64_t) pd;
    g_string_sprintfa (gs, " int64: %" G_GINT64_FORMAT, pdata->val);
    return;
  }
  if (!safe_strcmp (pd->type_name, QOF_TYPE_INT32))
  {
    query_int32_t pdata = (query_int32_t) pd;
    g_string_sprintfa (gs, " int32: %d", pdata->val);
    return;
  }
  if (!safe_strcmp (pd->type_name, QOF_TYPE_DOUBLE))
  {
    query_double_t pdata = (query_double_t) pd;
    g_string_sprintfa (gs, " double: %.18g", pdata->val);
    return;
  }
  if (!safe_strcmp (pd->type_name, QOF_TYPE_DATE))
  {
    query_date_t pdata = (query_date_t) pd;
    g_string_sprintfa (gs, "\n      Match type %s",
                       qof_query_printDateMatch (pdata->options));
    g_string_sprintfa (gs, " query_date: %s", gnc_print_date (pdata->date));
    return;
  }
  if (!safe_strcmp (pd->type_name, QOF_TYPE_CHAR))
  {
    query_char_t pdata = (query_char_t) pd;
    g_string_sprintfa (gs, "\n      Match type %s",
                       qof_query_printCharMatch (pdata->options));
    g_string_sprintfa (gs, " char list: %s", pdata->char_list);
    return;
  }
  if (!safe_strcmp (pd->type_name, QOF_TYPE_BOOLEAN))
  {
    query_boolean_t pdata = (query_boolean_t) pd;
    g_string_sprintfa (gs, " boolean: %s", pdata->val?"TRUE":"FALSE");
    return;
  }
  /** \todo QOF_TYPE_COLLECT */
  return;
}                               /* qof_query_printValueForParam */

/*
 * Print out a string representation of the
 * QofStringMatch enum
 */
static gchar *
qof_query_printStringMatch (QofStringMatch s)
{
  switch (s)
  {
    case QOF_STRING_MATCH_NORMAL:
      return "QOF_STRING_MATCH_NORMAL";
    case QOF_STRING_MATCH_CASEINSENSITIVE:
      return "QOF_STRING_MATCH_CASEINSENSITIVE";
  }
  return "UNKNOWN MATCH TYPE";
}                               /* qof_query_printStringMatch */

/*
 * Print out a string representation of the
 * QofDateMatch enum
 */
static gchar *
qof_query_printDateMatch (QofDateMatch d)
{
  switch (d)
  {
    case QOF_DATE_MATCH_NORMAL:
      return "QOF_DATE_MATCH_NORMAL";
    case QOF_DATE_MATCH_DAY:
      return "QOF_DATE_MATCH_DAY";
  }
  return "UNKNOWN MATCH TYPE";
}                               /* qof_query_printDateMatch */

/*
 * Print out a string representation of the
 * QofNumericMatch enum
 */
static gchar *
qof_query_printNumericMatch (QofNumericMatch n)
{
  switch (n)
  {
    case QOF_NUMERIC_MATCH_DEBIT:
      return "QOF_NUMERIC_MATCH_DEBIT";
    case QOF_NUMERIC_MATCH_CREDIT:
      return "QOF_NUMERIC_MATCH_CREDIT";
    case QOF_NUMERIC_MATCH_ANY:
      return "QOF_NUMERIC_MATCH_ANY";
  }
  return "UNKNOWN MATCH TYPE";
}                               /* qof_query_printNumericMatch */

/*
 * Print out a string representation of the
 * QofGuidMatch enum
 */
static gchar *
qof_query_printGuidMatch (QofGuidMatch g)
{
  switch (g)
  {
    case QOF_GUID_MATCH_ANY:
      return "QOF_GUID_MATCH_ANY";
    case QOF_GUID_MATCH_ALL:
      return "QOF_GUID_MATCH_ALL";
    case QOF_GUID_MATCH_NONE:
      return "QOF_GUID_MATCH_NONE";
    case QOF_GUID_MATCH_NULL:
      return "QOF_GUID_MATCH_NULL";
    case QOF_GUID_MATCH_LIST_ANY:
      return "QOF_GUID_MATCH_LIST_ANY";
  }

  return "UNKNOWN MATCH TYPE";
}                               /* qof_query_printGuidMatch */

/*
 * Print out a string representation of the
 * QofCharMatch enum
 */
static gchar *
qof_query_printCharMatch (QofCharMatch c)
{
  switch (c)
  {
    case QOF_CHAR_MATCH_ANY:
      return "QOF_CHAR_MATCH_ANY";
    case QOF_CHAR_MATCH_NONE:
      return "QOF_CHAR_MATCH_NONE";
  }
  return "UNKNOWN MATCH TYPE";
}                               /* qof_query_printGuidMatch */

/* ======================== END OF FILE =================== */
