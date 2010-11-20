/* - mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2010 Brian Aker
 *
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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef PLUGIN_USER_LOCKS_WAIT_UNTIL_H
#define PLUGIN_USER_LOCKS_WAIT_UNTIL_H

namespace user_locks {
namespace barriers {

class WaitUntil : public drizzled::Item_int_func
{
  drizzled::String value;

public:
  WaitUntil() :
    drizzled::Item_int_func()
  {}

  int64_t val_int();
  const char *func_name() const { return "wait_until"; }
  bool check_argument_count(int n) { return n == 2 ; }
};

} /* namespace barriers */
} /* namespace user_locks */

#endif /* PLUGIN_USER_LOCKS_WAIT_UNTIL_H */