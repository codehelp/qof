
/** @file my-object.h
 *  @breif Example definition of a queriable object. 
 *  @author Copyright (c) 2003 Linas Vepstas <linas@linas.org>
 *
 *  This example program shows how to configure an arbitrary
 *  programmer-defined object "MyObj" so that the Query routines
 *  can be used on it.  It shows one part of the total:
 *  -- The object definition, showing how to hook into the query system.
 */

#ifndef MY_OBJECT_H_
#define MY_OBJECT_H_

#include <qof/qof.h>

/* Define "my" object.  Replace by your object. */
typedef struct myobj_s 
{
   int a;          /* Some value */
   int b;          /* Some other value */
   char *memo;     /* Some string value */
} MyObj;

/* String identifying the 'type' or 'class' of this object */
#define MYOBJ_ID  "MyObj"

/* Some arbitrary names for data that can be queried on this object. */
#define MYOBJ_A    "MyObj_a"
#define MYOBJ_B    "MyObj_b"
#define MYOBJ_MEMO "MyObj_memo"


MyObj * my_obj_new (QofBook *book);

/* Generic object getters */
int my_obj_get_a (MyObj *m);
int my_obj_get_b (MyObj *m);
const char * my_obj_get_memo (MyObj *m);

gboolean myObjRegister (void);

#endif /* MY_OBJECT_H_ */

