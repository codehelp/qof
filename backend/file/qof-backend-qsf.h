/***************************************************************************
 *            qof-backend-qsf.h
 *
 *  Sat Jun 11 19:34:36 2005
 *  Copyright  2005  Neil Williams
 *  linux@codehelp.co.uk
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
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

/** @addtogroup Backend
    @{ */
/** @addtogroup QSF QOF Serialisation Format

This is the public interface of the qof-backend-qsf library.

QSF - QOF Serialization Format is an XML serialization format
i.e. it lays out a QOF object in a series of XML tags. The format will  
consist of two component formats:

qof-qsf for the QSF object data and

qsf-map to map sets of QSF objects between QOF applications.

Maps exist to allow complex conversions between objects where object parameters
need to be calculated, combined or processed using conditionals. Some QSF objects
can be converted using XSL or other standard tools. For more information on maps,
see http://code.neil.williamsleesmill.me.uk/map.html

QSF object files will contain user data and are to be exported from QOF applications 
under user control or they can be hand-edited. QSF maps contain application data and 
can be created by application developers from application source code. Tools may be 
created later to generate maps interactively but maps require application support as 
well as an understanding of QOF objects in the import and output applications and are 
intended to remain within the scope of application developers rather than users.

A QSF file written by one QOF application will need an appropriate QSF map before the 
data can be accessed by a different application using QOF. Any QSF objects that are 
not defined in the map will be ignored. QSF files written and accessed by the same 
application can use maps if necessary or can simply import the QSF data as a whole.

Unless specifically mentioned otherwise, all defined strings are case-sensitive.

Full documentation of this format is at:

http://code.neil.williamsleesmill.me.uk/qsf.html

QSF itself is now being built into the QOF library for use with pilot-link to allow
Palm objects to be described in QOF, written to XML as QSF and imported directly into 
GnuCash and other QOF-compliant applications. As a generic format, it does not depend 
on any pre-defined objects - as the current GnuCash XML format depends on AccountGroup. 
Instead, QSF is a simple container for all QOF objects.

QSF grew from the qof_book_merge code base and uses the qof_book_merge code that is now 
part of QOF. Any QofBook generated by QSF still needs to be merged into the existing 
application data using qof_book_merge. See ::BookMerge.

QSF can be used as an export or offline storage format for QOF applications (although 
long term storage may be best performed using separate (non-XML) methods, depending 
on the application).

QSF is designed to cope with partial QofBooks at the QofObject level. There is no 
requirement for specific objects to always be defined, as long as each QOF object 
specified is fully defined, no orphan or missing parameters are allowed. Part of the
handling for partial books requires a storage mechanism for references to entities
that are not within reach of the current book. This requires a little extra coding
in the QSF QofBackend to contain the reference data so that when the book is
written out, the reference can be included. When the file is imported back in, a
little extra code then rebuilds those references during the merge.

Copying entites from an existing QofBook using the qof_entity_copy routines will 
automatically create the reference table. If your QOF objects use references to other
entities, books that are created manually also need to create a reference table.

Work is continuing on supporting QSF in GnuCash and QOF. QSF is a very open format - 
the majority of the work will be in standardising object types and creating maps that 
convert between objects. Applications that read QSF should ignore any objects that do 
not match the available maps and warn the user about missing data. 

Anyone is free to create their own QSF objects, subject to the GNU GPL. It is intended 
that QSF can be used as the flexible, open and free format for QOF data - providing 
all that is lacking from a typical CSV export with all the validation benefits of XML 
and the complex data handling of QOF. The QSF object and map formats remain under the 
GNU GPL licence and QSF is free software.

\todo
	- Adding more map support, some parts of the map are still not coded. equals, 
		variables and the conditional logic may not be up to the task of the
		datebook repetition calculations.

QSF is in three sections:
	- QSF Backend : a QofBackend for file:/ QSF objects and maps.
		qsf-backend.c
	- QSF Object  : Input, export and validation of QSF object files.
		qsf-xml.c
	- QSF Map : Validation, processing and conversion routines.
		qsf-xml-map.c
		
To work with QSF, your QOF objects must have:
	- a create: function in the QofObject definition
	- a foreach: function in the QofObject definition
	- QofParam params[] registered with QOF using
		qof_class_register and containing all necessary parameters
		to reconstruct this object without any further information.
	- Logical distinction between those parameters that should be
		set (have a QofAccessFunc and QofSetterFunc) and those that 
		should only be calculated (only a QofAccessFunc).

If you begin your QSF session with ::QOF_STDOUT as the book_id,
QSF will write to STDOUT - usually a terminal. This is used by QOF
applications to provide data streaming. If you don't want terminal
output, take care to check the path given to 
::qof_session_begin - don't try to change it later!

The XML is validated against the QSF object schema before being
written (to file or stdout).

Check the QofBackendError - don't assume the file is OK.

    @{ */
/** @file qof-backend-qsf.h
    @brief  QSF API - Backend, maps, objects and configuration.
    @author Copyright (C) 2004-2005 Neil Williams <linux@codehelp.co.uk>
*/

#ifndef _QOF_BACKEND_QSF_H
#define _QOF_BACKEND_QSF_H

#include "qoflog.h"
#include "qofbackend.h"

/** \brief Describe this backend to the application. 

Sets QSF Backend Version 0.2, access method = file:

The version number only changes if:
-# The map schema is modified, or
-# The object schema is modified, or
-# The QofBackendProvider struct is modified in QOF to
support new members and QSF can support the new function, or
-# The QofBackendOption settings are modified.

v0.2 introduces the QSF_MAP_FILES QofBackendOption.

Initialises the backend and provides access to the
functions that will load and save the data. Initialises
default values for the QofBackendOption KvpFrame.

Calls gettext because QofBackendOption
strings are translatable.
*/
void qsf_provider_init (void);

/** \name Supported backend configurations
@{
*/

/** \brief compression level

\b Type: gint64 (KVP_TYPE_GINT64)

Pass a pointer to an integer value between 0 and 9.
*/
#define QSF_COMPRESS    "compression_level"

/** \brief selected QSF maps 

\b Type: GList* (KVP_TYPE_GLIST) of const char* (QOF_TYPE_STRING) values.

Defaults to the pre-installed QSF map(s) but may be overridden
by the application to pass full path(s) of user selected 
QSF map file(s).

If you override the list, it is advisable to only specify the single
map file for this QSF object to reduce the amount of error checking
required within the backend. Simply reset the QofBackendOption before
another file is to be opened. It is up to the application to decide how
to offer multiple map selections to the user.

*/
#define QSF_MAP_FILES   "selected_map_files"

/** \brief Encoding string.

 Defaults of UTF-8. */
#define QSF_ENCODING    "encoding_string"

/** \brief Convert QOF_TYPE_DATE to QOF_TYPE_TIME

QOF_TYPE_DATE is deprecated and some files will need 
conversion. Applications can set this option so that if
a QOF_TYPE_DATE field (&lt;date&gt;) is encountered, a
QOF_TYPE_TIME field (&lt;time&gt;) will be passed to the
application and written out when the file is saved.

Applications that support the new QOF time format 
need to enable this option to prevent data loss when
loading older files. It is safe to leave the option enabled,
whether or not the file contains any date fields.

Applications that still use date must not set this option 
until time values are supported.

Although this could be passed to the user to specify, 
most applications should set this option at start-up
and without user intervention.

\b Type: gint64 (KVP_TYPE_GINT64)

Pass a pointer to a non-zero integer value to enable.

Zero by default. Ignored if QOF was built with
deprecated code disabled.
*/
#define QSF_DATE_CONVERT "convert_date_to_time"

/** @} */
/** @} */
/** @} */

#endif /* _QOF_BACKEND_QSF_H */
