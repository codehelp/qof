/********************************************************************\
 * qofsession-p.h -- private functions for QOF sessions.            *
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
Copyright (c) 2001 Linux Developers Group
Copyright (c) 1998-2003 Linas Vepstas <linas@linas.org>
Copyright (c) 2006 Neil Williams <linux@codehelp.co.uk> 
 */

#ifndef QOF_SESSION_P_H
#define QOF_SESSION_P_H

#include "qofbook.h"
#include "qofsession.h"

struct _QofSession
{
	/** \deprecated This is just a "fake" entry point to allow me to pass a Session as
	 * an Entity.  NOTE:  THIS IS NOT AN ENTITY!  THE ONLY PART OF ENTITY
	 * THAT IS VALID IS E_TYPE!
	 */
	QofEntity entity;

	/* A book holds pointers to the various types of datasets.
	 * A session may have multiple books. */
	GList *books;

	/* The requested book id, in the form or a URI, such as
	 * file:/some/where, or sql:server.host.com:555
	 */
	gchar *book_id;

	/** If no backend is available, record the failure Id. */
	QofErrorId last_err;

	/** If no backend is available, record the failure string. */
	gchar *error_message;

	/* Pointer to the backend that is actually used to move data
	 * between the persistent store and the local engine.  */
	QofBackend *backend;
};

QofBackend *
qof_session_get_backend (QofSession * session);

#endif
