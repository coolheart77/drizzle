/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008 Sun Microsystems, Inc.
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


#ifndef PLUGIN_UTILITY_FUNCTIONS_BIT_COUNT_H
#define PLUGIN_UTILITY_FUNCTIONS_BIT_COUNT_H

#include <drizzled/function/func.h>
#include <drizzled/function/math/int.h>

namespace drizzled
{

namespace utility_functions
{

class BitCount :public Item_int_func
{
public:
  BitCount() :
    Item_int_func() {}

  int64_t val_int();
  const char *func_name() const { return "bit_count"; }
  void fix_length_and_dec() { max_length=2; }
};

} /* namespace utility_functions */
} /* namespace drizzled */


#endif /* PLUGIN_UTILITY_FUNCTIONS_BIT_COUNT_H */