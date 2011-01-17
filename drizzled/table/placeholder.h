/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
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

/* Structs that defines the Table */

#ifndef DRIZZLED_TABLE_PLACEHOLDER_H
#define DRIZZLED_TABLE_PLACEHOLDER_H

namespace drizzled
{

namespace table
{

class Placeholder : public table::Concurrent
{
  instance::Shared private_share;

public:
  Placeholder(Session *session, identifier::Table &identifier) :
    table::Concurrent(),
    private_share(identifier)
  {
    setShare(&private_share);
    in_use= session;

    locked_by_name= true;
  }

  bool isPlaceHolder(void) const
  {
    return true;
  }

  void release(void)
  {
    table::instance::release(getMutableShare());
  }
};

} /* namespace table */
} /* namespace drizzled */

#endif /* DRIZZLED_TABLE_PLACEHOLDER_H */
