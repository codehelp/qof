
/*
 *  My Application
 *  Prototype for a new QOF DWI backend .... under development.
 *
 * Basic parsing works basic, copyin, copyout works.
 */

#include <qof.h>
#include <qofbook-p.h>

#include "my-instance.h"

/* ============================================================== */
/* Perform various bits of system initialization */
void init (void);
void 
init (void)
{
	qof_object_initialize();
	qof_query_init();
	my_inst_register ();
}

void shutdown(void);
void 
shutdown(void)
{
}

/* ============================================================== */
/* print out the contents of the book. */

static void
print_one (QofEntity *myent, gpointer data)
{
	const char * str = qof_object_printable (MYINST_ID, myent);
	printf ("%s", str);
}

void print_all (QofBook *book);
void
print_all (QofBook *book)
{
	qof_object_foreach (MYINST_ID, book, print_one, NULL);
	printf ("-------------------------------------------------\n");
}

/* ============================================================== */
/* Run a query, print the results */
void my_app_run_query (QofSqlQuery *q, char *sql_str);
void 
my_app_run_query (QofSqlQuery *q, char *sql_str)
{
   GList *results, *n;

   /* Run the query */
   results = qof_sql_query_run (q, sql_str);

   printf ("------------------------------------------\n");
   printf ("Query string is: %s\n", sql_str);
   printf ("Query returned %d results:\n", g_list_length(results));
   for (n=results; n; n=n->next)
   {
      MyInst *m = n->data;
      printf ("Found a matching object, a=%d b=%d memo=\"%s\"\n", 
          m->a, m->b, m->memo);
   }
   printf ("\n");
}

/* ============================================================== */
MyInst* add_entity (QofBook *book);
MyInst *
add_entity (QofBook *book)
{
	MyInst *me = my_inst_new (book);

	my_inst_set_a (me, 66);

	time_t now = time(0);
	my_inst_set_b (me, now);

	char buff[300];
	sprintf (buff, "Hee Haw the time now is %s", ctime(&now));
	int len = strlen (buff);
	buff[len-1] = 0;  /* remove trailing newline char */
	my_inst_set_memo (me, buff);

	printf ("After adding some random entity, "
	        "the contents of the book now are:\n");
	print_all (book);
	return me;
}

int 
main (int argc, char *argv[])
{
	init();

	QofSession *sess = qof_session_new();
	qof_session_begin (sess, "sql://myinst", FALSE, TRUE);
	qof_session_load (sess, NULL);
	
	QofBook *book = qof_session_get_book(sess);

	/* Create a new query */
	QofSqlQuery *qry =  qof_sql_query_new ();

	/* Set the book to be searched */
	qof_sql_query_set_book(qry, book);

	/* Run a query */
	// my_app_run_query (qry, "SELECT * FROM MyInst WHERE MyObj_a = 1;");
	my_app_run_query (qry, "SELECT * FROM MyInst;");
	
#if 0
	qof_map_set_book (map, book);

	qof_map_set_database (map, db);

	/* -------------------------------------- */
	GUID guid;
   string_to_guid ("fedcba0123456789abcdef0123456789", &guid);
	qof_map_copy_from_db (map, &guid);

	/* Print all of the entities in the book, so that we can see 
	 * our handiwork. */
	printf ("After running the copy_from_db() routine, "
	        "the contents of the book are:\n");
	print_all (book);

	qof_map_copy_multiple_from_db (map, "select * from myinstance;");

	printf ("After running the copy_all_from_db() routine, "
	        "the contents of the book are:\n");
	print_all (book);

	/* -------------------------------------- */
	MyInst *me = add_entity (book);
	qof_map_write_to_db (map, QOF_INSTANCE (me));

	me->a = 42;
	qof_map_write_to_db (map, QOF_INSTANCE (me));

	/* force a rollback to occur */
	me->a = 128;
	Timespec ts = qof_instance_get_last_update (&me->inst);
	ts.tv_nsec -=10;
	qof_instance_set_last_update (&me->inst, ts);
	qof_map_write_to_db (map, QOF_INSTANCE (me));
#endif

	shutdown ();

	return 0;
}

/* ======================== END OF FILE ========================= */
/* ============================================================== */
