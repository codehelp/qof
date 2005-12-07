/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
 

/** @file test-engine-stuff.h
 *  @brief This file declares testing functions for the engine.
 */

#ifndef TEST_ENGINE_STUFF_H
#define TEST_ENGINE_STUFF_H

#include "config.h"

#include <glib.h>
#include <stdlib.h>

#include "qofquery.h"
#include "gnc-date.h"
#include "qofbook.h"
#include "qofsession.h"

Timespec* get_random_timespec(void);
void random_timespec_zero_nsec (gboolean zero_nsec);
void random_timespec_usec_resolution (gboolean usec_resolution);

kvp_value* get_random_kvp_value(int type);

typedef struct
{
    guchar *data;
    int len;
} bin_data;

bin_data* get_random_binary_data(void);

kvp_frame* get_random_kvp_frame(void);
gnc_numeric get_random_gnc_numeric(void);
GUID* get_random_guid(void);
GList* get_random_glist(void);

void random_glist_strings_only (gboolean strings_only);
void kvp_exclude_type (KvpValueType kvp_type);
void set_max_kvp_depth (gint max_kvp_depth);
void set_max_kvp_frame_elements (gint max_kvp_frame_elements);

typedef enum
{
  RANDOM_QT      = 0,
  SIMPLE_QT      = 1 << 0,
  GUID_QT        = 1 << 5,
  ALL_QT         = (1 << 8) - 1
} TestQueryTypes;

QofQuery * get_random_query(void);
TestQueryTypes get_random_query_type (void);

QofBook * get_random_book (void);
QofSession * get_random_session (void);

#endif