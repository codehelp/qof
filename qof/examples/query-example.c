
/** @file query-example.c
 *  @breif Example program showing query object usage. 
 *  @author Copyright (c) 2003 Linas Vepstas <linas@linas.org>
 *
 *  This example program shows how to configure an arbitrary
 *  programmer-defined oject "MyObj" so that the Query routines
 *  can be used on it.  It consists of four basic peices:
 *  -- The object definition, showing how to hook into the query system.
 *  -- Generic application intialization, including the creation of
 *     a number of instances of MyObj.
 *  -- QOF intialization, required before QOF can be used.
 *  -- The definition and running of a query, and a printout of the
 *     results.
 */

#include <glib.h>

#include "gncObject.h"
#include "QueryObject.h"

/* ===================================================== */

/* Define "my" object.  Replace by your object. */
typedef struct myobj_s 
{
   int a;          /* Some value */
   int b;          /* Some other value */
   char *memo;     /* Some string value */
} MyObj;

GSList *all_my_objs = NULL;

MyObj *
my_obj_new (GNCBook *book)
{
   MyObj *m = g_new0 (MyObj,1);

   /* Make sure we keep track of ever object; otherwise we won't
    * be able to search over them.  */
   all_my_objs = g_slist_prepend (all_my_objs, m);

   return m;
}

/* Generic object getters */
int
my_obj_get_a (MyObj *m)
{
   return m->a;
}

int
my_obj_get_b (MyObj *m)
{
   return m->b;
}

const char *
my_obj_get_memo (MyObj *m)
{
   return m->memo;
}

/* Loop over every instance of MyObj, and apply the callback to it.
 * This routine must be defined for queries to be possible. */
void
my_obj_foreach (GNCBook *book, foreachObjectCB cb, gpointer ud)
{
   GSList *n;
   
   for (n=all_my_objs; n; n=n->next)
   {
      cb (n->data, ud);
   }
}

/* Provide a default mechanism to sort MyObj 
 * This is neeeded so that query results can be returned in
 * some 'reasonable' order.  If you don't want to sort,
 * just have this function always return 0.
 */ 
my_obj_order (MyObj *a, MyObj *b)
{
   if ( (a) && !(b) ) return -1;
   if ( !(a) && (b) ) return +1;
   if ( !(a) && !(b) ) return 0;

   if ((a)->a > (b)->a) return +1;
   if ((a)->a == (b)->a) return 0;
   return -1;
}

/* ===================================================== */
/* Provide infrastructure to register my object with QOF */

#define MYOBJ_ID  "MyObj"

static GncObject_t myobj_object_def = 
{
   interface_version: GNC_OBJECT_VERSION,
   name:              MYOBJ_ID,
   type_label:        "My Blinking Object",
   book_begin:        NULL,
   book_end:          NULL,
   is_dirty:          NULL,
   mark_clean:        NULL,
   foreach:           my_obj_foreach,
   printable:         NULL,
};

/* Some arbitrary names for data getters that the query will employ */
#define MYOBJ_A    "MyObj_a"
#define MYOBJ_B    "MyObj_b"
#define MYOBJ_MEMO "MyObj_memo"
gboolean myObjRegister (void)
{
   /* Associate an ASCII name to each getter, as well as the return type */
   static QueryObjectDef params[] = {
     { MYOBJ_A,     QUERYCORE_INT32, (QueryAccess)my_obj_get_a },
     { MYOBJ_B,     QUERYCORE_INT32, (QueryAccess)my_obj_get_b },
     { MYOBJ_MEMO,  QUERYCORE_STRING, (QueryAccess)my_obj_get_memo },
     { NULL },
   };

   gncQueryObjectRegister (MYOBJ_ID, (QuerySort)my_obj_order, params);
   return gncObjectRegister (&myobj_object_def);
}

/* ===================================================== */

GNCBook *
my_app_init (void)
{
   GNCBook *book;
   
   /* Perform the application object registeration */
   myObjRegister ();

   /* Create a new top-level object container */
   book =  gnc_book_new();

   return book;
}

void
my_app_shutdown (GNCBook *book)
{
   /* Terminate our storage. This prevents the system from
    * being used any further. */
   gnc_book_destroy (book);
}

void
my_app_create_data (GNCBook *book)
{
   MyObj *m;

   /* Pretend our app has some objects; we will perform
    * the search over these objects */
   
   m = my_obj_new (book);
   m->a = 1;
   m->b = 1;
   m->memo = "Hiho Silver!";
   
   m = my_obj_new (book);
   m->a = 1;
   m->b = 42;
   m->memo = "The Answer to the Question";
   
   m = my_obj_new (book);
   m->a = 99;
   m->b = 1;
   m->memo = "M M M My Sharona";
}
   
/* ===================================================== */
/* A routine that will build an actual query, run it, and
 * print the results.
 */

void
my_app_run_query (GNCBook *book)
{
   QueryPredData_t pred_data;
   GSList *param_list;
   QueryNew *q;
   GList *results, *n;

   /* Create a new query */
   q =  gncQueryCreate ();

   /* Set the object type to be searched for */
   gncQuerySearchFor (q, MYOBJ_ID);
   
   /* Set the book to be searched */
   gncQuerySetBook(q, book);

   /* Describe the query to be performed.
    * We want to find all objects whose "memo" field matches
    * a particular string, or all objects whose "b" field is 42.
    */

   param_list = gncQueryBuildParamList (MYOBJ_MEMO, /* field to match */
                   NULL); 
   pred_data = gncQueryStringPredicate (
                   COMPARE_EQUAL,                   /* comparison to make */
                   "M M M My Sharona",              /* string to match */
                   STRING_MATCH_CASEINSENSITIVE,    /* case matching */
                   FALSE);                          /* use_regexp */
   gncQueryAddTerm (q, param_list, pred_data, 
                   QUERY_FIRST_TERM);               /* How to combine terms */
   
   param_list = gncQueryBuildParamList (MYOBJ_B,    /* field to match */
                   NULL); 
   pred_data = gncQueryInt32Predicate (
                   COMPARE_EQUAL,                   /* comparison to make */
                   42);                             /* value to match */
   
   gncQueryAddTerm (q, param_list, pred_data, 
                   QUERY_OR);                       /* How to combine terms */
   
   /* Run the query */
   results = gncQueryRun (q);

   /* Print out the results */
   printf ("Query returned %d results:\n", g_list_length(results));
   for (n=results; n; n=n->next)
   {
      MyObj *m = n->data;
      printf ("Found a matching object, a=%d b=%d memo=\"%s\"\n", 
          m->a, m->b, m->memo);
   }
   
   /* The query isn't needed any more; discard it */
   gncQueryDestroy (q);

}

/* ===================================================== */
 
int
main (int argc, char *argv[]) 
{
   GNCBook *book;
   
   /* Initialize the QOF framework */
   gnc_engine_get_string_cache();
   xaccGUIDInit ();
   gncObjectInitialize ();
   gncQueryNewInit ();
   gnc_book_register ();
              
   /* Do application-specific things */
   book = my_app_init();
   my_app_create_data(book);
   my_app_run_query (book);
   my_app_shutdown (book);

   /* Perform a clean shutdown */
   gncQueryNewShutdown ();
   gnc_engine_string_cache_destroy ();
   gncObjectShutdown ();
   xaccGUIDShutdown ();
}

/* =================== END OF FILE ===================== */
