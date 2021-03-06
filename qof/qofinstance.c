/********************************************************************\
 * qofinstance.c -- handler for fields common to all objects        *
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

/*
 * Object instance holds many common fields that most
 * gnucash objects use.
 *
 * Copyright (C) 2003 Linas Vepstas <linas@linas.org>
 */

#include "config.h"
#include <glib.h>
#include "qof.h"
#include "kvputil-p.h"
#include "qofbook-p.h"
#include "qofid-p.h"
#include "qofinstance-p.h"

static QofLogModule log_module = QOF_MOD_ENGINE;

/* ========================================================== */

QofInstance *
qof_instance_create (QofIdType type, QofBook * book)
{
	QofInstance *inst;

	inst = g_new0 (QofInstance, 1);
	qof_instance_init (inst, type, book);
	return inst;
}

void
qof_instance_init (QofInstance * inst, QofIdType type, QofBook * book)
{
	QofCollection *col;

	inst->book = book;
	inst->kvp_data = kvp_frame_new ();
	inst->update_time = qof_time_get_current ();
	inst->editlevel = 0;
	inst->do_free = FALSE;
	inst->dirty = FALSE;

	col = qof_book_get_collection (book, type);
	qof_entity_init (&inst->entity, type, col);
}

void
qof_instance_release (QofInstance * inst)
{
	kvp_frame_delete (inst->kvp_data);
	inst->editlevel = 0;
	inst->do_free = FALSE;
	inst->dirty = FALSE;
	qof_entity_release (&inst->entity);
}

const GUID *
qof_instance_get_guid (QofInstance * inst)
{
	if (!inst)
		return NULL;
	return &inst->entity.guid;
}

QofBook *
qof_instance_get_book (QofInstance * inst)
{
	if (!inst)
		return NULL;
	return inst->book;
}

KvpFrame *
qof_instance_get_slots (QofInstance * inst)
{
	if (!inst)
		return NULL;
	return inst->kvp_data;
}

QofTime *
qof_instance_get_update_time (QofInstance * inst)
{
	if (!inst)
	{
		QofTime *time;

		time = qof_time_get_current ();
		return time;
	}
	return inst->update_time;
}

int
qof_instance_version_cmp (QofInstance * left, QofInstance * right)
{
	if (!left && !right)
		return 0;
	if (!left)
		return -1;
	if (!right)
		return +1;
	return qof_time_cmp (left->update_time, right->update_time);
}

gboolean
qof_instance_is_dirty (QofInstance * inst)
{
	QofCollection *coll;

	if (!inst)
	{
		return FALSE;
	}
	coll = inst->entity.collection;
	if (qof_collection_is_dirty (coll))
	{
		return inst->dirty;
	}
	inst->dirty = FALSE;
	return FALSE;
}

void
qof_instance_set_dirty (QofInstance * inst)
{
	QofCollection *coll;

	inst->dirty = TRUE;
	coll = inst->entity.collection;
	qof_collection_mark_dirty (coll);
}

gboolean
qof_instance_check_edit (QofInstance * inst)
{
	if (inst->editlevel > 0)
	{
		return TRUE;
	}
	return FALSE;
}

gboolean
qof_instance_do_free (QofInstance * inst)
{
	return inst->do_free;
}

void
qof_instance_mark_free (QofInstance * inst)
{
	inst->do_free = TRUE;
}

/* ========================================================== */
/* setters */

void
qof_instance_mark_clean (QofInstance * inst)
{
	if (!inst)
		return;
	inst->dirty = FALSE;
}

void
qof_instance_set_slots (QofInstance * inst, KvpFrame * frm)
{
	if (!inst)
		return;
	if (inst->kvp_data && (inst->kvp_data != frm))
	{
		kvp_frame_delete (inst->kvp_data);
	}

	inst->dirty = TRUE;
	inst->kvp_data = frm;
}

void
qof_instance_set_update_time (QofInstance * inst, QofTime * time)
{
	if (!inst)
		return;
	qof_time_free (inst->update_time);
	inst->update_time = time;
}

void
qof_instance_gemini (QofInstance * to, QofInstance * from)
{
	QofTime *qt;

	/* Books must differ for a gemini to be meaningful */
	if (!from || !to || (from->book == to->book))
		return;

	qt = qof_time_get_current ();

	/* Make a note of where the copy came from */
	qof_kvp_bag_add (to->kvp_data, "gemini", qt,
		"inst_guid", &from->entity.guid,
		"book_guid", &from->book->inst.entity.guid, NULL);
	qof_kvp_bag_add (from->kvp_data, "gemini", qt,
		"inst_guid", &to->entity.guid,
		"book_guid", &to->book->inst.entity.guid, NULL);

	to->dirty = TRUE;
}

QofInstance *
qof_instance_lookup_twin (QofInstance * src, QofBook * target_book)
{
	QofCollection *col;
	KvpFrame *fr;
	GUID *twin_guid;
	QofInstance *twin;

	if (!src || !target_book)
		return NULL;
	ENTER (" ");

	fr = qof_kvp_bag_find_by_guid (src->kvp_data, "gemini",
		"book_guid", &target_book->inst.entity.guid);

	twin_guid = kvp_frame_get_guid (fr, "inst_guid");

	col = qof_book_get_collection (target_book, src->entity.e_type);
	twin = (QofInstance *) qof_collection_lookup_entity (col, twin_guid);

	LEAVE (" found twin=%p", twin);
	return twin;
}

/* ========================== END OF FILE ======================= */
