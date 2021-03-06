/********************************************************************\
 * qofid.h -- QOF entity type identification system                 *
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

#ifndef QOF_ID_H
#define QOF_ID_H

/** @addtogroup Class
    @{ */
/** @addtogroup Entity

    This file defines an API that adds types to the GUID's.
    GUID's with types can be used to identify and reference 
    typed entities.  

    The idea here is that a GUID can be used to uniquely identify
    some thing.  By adding a type, one can then talk about the 
    type of thing identified.  By adding a collection, one can
    then work with a handle to a collection of things of a given
    type, each uniquely identified by a given ID.  QOF Entities
    can be used independently of any other part of the system.
    In particular, Entities can be useful even if one is not using
    the Query ond Object parts of the QOF system.
  
    Identifiers are globally-unique and permanent, i.e., once
    an entity has been assigned an identifier, it retains that same
    identifier for its lifetime.
    Identifiers can be encoded as hex strings. 
   
    GUID Identifiers are 'typed' with strings.  The native ids used 
    by QOF are defined below.
	-# An id with type QOF_ID_NONE does not 
    refer to any entity.
	-# An id with type QOF_ID_NULL does not refer
	to any entity, and will never refer to any entity.
	=# An identifier with any other type may refer to an
    actual entity, but that is not guaranteed as that entity does
	not have to exist within the current book. (See ::PARTIAL_QOFBOOK).
	Also, creating a new entity from a data source involves creating
	a temporary GUID and then setting the value from the data source.
	If an id does refer to an entity, the type of the entity will match
	the type of the identifier. 

    If you have a type name, and you want to have a way of finding
    a collection that is associated with that type, then you must use
    Books.

	Entities can refer to other entities as well as to the basic
	QOF types, using the qofclass parameters.

 @{ */
/** @file qofid.h
    @brief QOF entity type identification system 
    @author Copyright (C) 2000 Dave Peticolas <peticola@cs.ucdavis.edu> 
    @author Copyright (C) 2003 Linas Vepstas <linas@linas.org>
	@author Copyright (C) 2006-2007 Neil Williams <linux@codehelp.co.uk>
*/

#include <string.h>
#include "guid.h"

/** QofIdType declaration */
typedef const gchar *QofIdType;
/** QofIdTypeConst declaration */
typedef const gchar *QofIdTypeConst;
/** QofLogModule declaration */
typedef const gchar *QofLogModule;

#define QOF_ID_NONE           NULL
#define QOF_ID_NULL           "null"

#define QOF_ID_BOOK           "Book"
#define QOF_ID_SESSION        "Session"

/** simple,cheesy cast but holds water for now */
#define QOF_ENTITY(object) ((QofEntity *)(object))

/** Inline string comparision; compiler will optimize away most of this */
#define QSTRCMP(da,db) ({                \
  gint val = 0;                          \
  if ((da) && (db)) {                    \
    if ((da) != (db)) {                  \
      val = strcmp ((da), (db));         \
    }                                    \
  } else                                 \
  if ((!(da)) && (db)) {                 \
    val = -1;                            \
  } else                                 \
  if ((da) && (!(db))) {                 \
    val = 1;                             \
  }                                      \
  val; /* block assumes value of last statment */  \
})

/** return TRUE if object is of the given type */
#define QOF_CHECK_TYPE(obj,type) (((obj) != NULL) && \
  (0 == QSTRCMP((type),(((QofEntity *)(obj))->e_type))))

/** cast object to the indicated type,
print error message if its bad  */
#define QOF_CHECK_CAST(obj,e_type,c_type) (                   \
  QOF_CHECK_TYPE((obj),(e_type)) ?                            \
  (c_type *) (obj) :                                          \
  (c_type *) ({                                               \
     g_log (G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL,               \
       "Error: Bad QofEntity at %s:%d", __FILE__, __LINE__);  \
     (obj);                                                   \
  }))

/** QofEntity declaration */
typedef struct QofEntity_s QofEntity;
/** QofCollection declaration 

@param e_type QofIdType
@param is_dirty gboolean
@param hash_of_entities GHashTable
@param data gpointer, place where object class can hang arbitrary data

*/
typedef struct QofCollection_s QofCollection;

/** QofEntity structure

@param e_type 	Entity type
@param guid		GUID for the entity
@param collection	Entity collection
*/

struct QofEntity_s
{
	QofIdType e_type;
	GUID guid;
	QofCollection *collection;
};

/** @name QOF Entity Initialization & Shutdown 
 @{ */
/** Initialise the memory associated with an entity */
void qof_entity_init (QofEntity *, QofIdType, QofCollection *);

/** Release the data associated with this entity. Dont actually free
 * the memory associated with the instance. */
void qof_entity_release (QofEntity *);
/** @} */

/** Return the GUID of this entity */
const GUID *qof_entity_get_guid (QofEntity *);

/** @name Collections of Entities 
 @{ */

/** create a new collection of entities of type */
QofCollection *qof_collection_new (QofIdType type);

/** return the number of entities in the collection. */
guint qof_collection_count (QofCollection * col);

/** destroy the collection */
void qof_collection_destroy (QofCollection * col);

/** return the type that the collection stores */
QofIdType qof_collection_get_type (QofCollection *);

/** Find the entity going only from its guid */
QofEntity *
qof_collection_lookup_entity (QofCollection *, const GUID *);

/** Callback type for qof_entity_foreach */
typedef void (*QofEntityForeachCB) (QofEntity *, gpointer user_data);

/** Call the callback for each entity in the collection. */
void 
qof_collection_foreach (QofCollection *, QofEntityForeachCB,
						gpointer user_data);

/** Store arbitrary object-defined data 
 *
 * XXX We need to add a callback for when the collection is being
 * destroyed, so that the user has a chance to clean up anything
 * that was put in the 'data' member here.
 */
gpointer 
qof_collection_get_data (QofCollection * col);

/** Retrieve arbitrary object-defined data */
void 
qof_collection_set_data (QofCollection * col, gpointer user_data);

/** Return value of 'dirty' flag on collection */
gboolean 
qof_collection_is_dirty (QofCollection * col);

/** @name QOF_TYPE_COLLECT: Linking one entity to many of one type

\note These are \b NOT the same as the main collections in the book.

QOF_TYPE_COLLECT is a secondary collection, used to select entities
of one object type as references of another entity.
\sa QOF_TYPE_CHOICE.

@{
*/
/** \brief Add an entity to a QOF_TYPE_COLLECT.

\note These are \b NOT the same as the main collections in the book.

Entities can be
freely added and merged across these secondary collections, they
will not be removed from the original collection as they would
by using ::qof_entity_insert_entity or ::qof_entity_remove_entity. 

*/
gboolean 
qof_collection_add_entity (QofCollection * coll, QofEntity * ent);

/** \brief Merge two QOF_TYPE_COLLECT of the same type.

\note \b NOT the same as the main collections in the book.

QOF_TYPE_COLLECT uses a secondary collection, independent of
those in the book. Entities will not be removed from the
original collection as when using ::qof_entity_insert_entity
or ::qof_entity_remove_entity.

*/
gboolean 
qof_collection_merge (QofCollection * target, QofCollection * merge);

/** \brief Compare two secondary collections.

Performs a deep comparision of the collections. Each QofEntity in
each collection is looked up in the other collection, via the GUID.

\return 0 if the collections are identical or both are NULL
otherwise -1 if target is NULL or either collection contains an entity with an invalid
GUID or if the types of the two collections do not match,
or +1 if merge is NULL or if any entity exists in one collection but
not in the other.
*/
gint 
qof_collection_compare (QofCollection * target, QofCollection * merge);

/** \brief Create a secondary collection from a GList

@param type The QofIdType of the QofCollection \b and of 
	\b all entities in the GList.
@param glist GList of entities of the same QofIdType.

@return NULL if any of the entities fail to match the
	QofCollection type, else a pointer to the collection
	on success.
*/
QofCollection *
qof_collection_from_glist (QofIdType type, GList * glist);

/** @} */
/** @} */

#endif /* QOF_ID_H */
/** @} */
/** @} */
