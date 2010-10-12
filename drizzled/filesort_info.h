/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008 Sun Microsystems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "drizzled/sql_sort.h"
#ifndef DRIZZLED_FILESORT_INFO_H
#define DRIZZLED_FILESORT_INFO_H


namespace drizzled
{

/* Information on state of filesort */
class filesort_info
{
public:
  internal::IO_CACHE *io_cache;           /* If sorted through filesort */
  unsigned char     **sort_keys;        /* Buffer for sorting keys */
  unsigned char     *buffpek;           /* Buffer for buffpek structures */
  uint32_t      buffpek_len;        /* Max number of buffpeks in the buffer */
  unsigned char     *addon_buf;         /* Pointer to a buffer if sorted with fields */
  size_t    addon_length;       /* Length of the buffer */
  sort_addon_field *addon_field;     /* Pointer to the fields info */
  void    (*unpack)(sort_addon_field *, unsigned char *); /* To unpack back */
  unsigned char     *record_pointers;    /* If sorted in memory */
  ha_rows   found_records;      /* How many records in sort */

  filesort_info() :
    io_cache(0),
    sort_keys(0),
    buffpek(0),
    buffpek_len(0),
    addon_buf(0),
    addon_length(0),
    addon_field(0),
    unpack(0),
    record_pointers(0),
    found_records()
  { }
};

} /* namespace drizzled */

#endif /* DRIZZLED_FILESORT_INFO_H */
