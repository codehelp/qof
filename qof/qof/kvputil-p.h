/********************************************************************\
 * kvputil-p.h -- misc odd-job kvp utils (private file)             *
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

#ifndef XACC_KVP_UTIL_P_H
#define XACC_KVP_UTIL_P_H

#include "guid.h"
#include "kvpframe.h"

/** @addtogroup KVP
    @{ 
*/
/** @file kvputil-p.h
 * @brief misc odd-job kvp utils engine-private routines
 * @author Copyright (C) 2001, 2003 Linas Vepstas <linas@linas.org>               *
*/
/** @name KvpBag Bags of GUID Pointers 
 @{ 
*/

/** The qof_kvp_bag_add() routine is used to maintain a collection 
 *  of pointers in a kvp tree.
 *
 *  The thing being pointed at is uniquely identified by its GUID. 
 *  This routine is typically used to create a linked list, and/or
 *  a collection of pointers to objects that are 'related' to each 
 *  other in some way.
 *
 *  The var-args should be pairs of strings (const char *) followed by
 *  the corresponding GUID pointer (const GUID *).  Terminate the varargs
 *  with a NULL as the last string argument.
 *
 *  The actual 'pointer' is stored in a subdirectory in a bag located at
 *  the node directory 'path'.  A 'bag' is merely a collection of
 *  (unamed) values.  The name of our bag is 'path'. A bag can contain
 *  any kind of values, including frames.  This routine will create a
 *  frame, and put it in the bag.  The frame will contain named data
 *  from the subroutine arguments.  Thus, for example:
 *
 *  qof_kvp_array (kvp, "foo", secs, "acct_guid", aguid, 
 *                                   "book_guid", bguid, NULL);
 *
 *  will create a frame containing "/acct_guid" and "/book_guid", whose
 *  values are aguid and bguid respecitvely.  The frame will also
 *  contain "/date", whose value will be secs.  This frame will be
 *  placed into the bag located at "foo". 
 *
 *  This routine returns a pointer to the frame that was created, or 
 *  NULL if an error occured.
 
*/

KvpFrame *
qof_kvp_bag_add (KvpFrame * kvp_root, const gchar *path, 
				QofTime *qt, const gchar *first_name, ...);

/** The gnc_kvp_bag_merge() routine will move the bag contents from
 *    the 'kvp_from', to the 'into' bag.  It will then delete the 
 *    'from' bag from the kvp tree.
 
 @todo Rename qof_kvp_bag_merge
 */
void 
qof_kvp_bag_merge (KvpFrame * kvp_into, const gchar *intopath,
				   KvpFrame * kvp_from, const gchar *frompath);

/** The gnc_kvp_bag_find_by_guid() routine examines the bag pointed
 *    located at root.  It looks for a frame in that bag that has the
 *    guid value of "desired_guid" filed under the key name "guid_name".
 *    If it finds that matching guid, then it returns a pointer to 
 *    the KVP frame that contains it.  If it is not found, or if there
 *    is any other error, NULL is returned.
 
 @todo rename qof_kvp_bag_find_by_guid
 */

KvpFrame *
qof_kvp_bag_find_by_guid (KvpFrame * root, const gchar *path,
						  const gchar *guid_name,
						  GUID * desired_guid);

/** Remove the given frame from the bag.  The frame is removed,
 *  however, it is not deleted.  Note that the frame pointer must
 *  be a pointer to the actual frame (for example, as returned by
 *  gnc_kvp_bag_find_by_guid() for by gnc_kvp_bag_add()), and not
 *  some copy of the frame.
 */

void 
qof_kvp_bag_remove_frame (KvpFrame * root, const gchar *path,
						  KvpFrame * fr);

/** @} */
/** @} */
#endif /* XACC_KVP_UTIL_P_H */
