/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2009 Sun Microsystems
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

#ifndef DRIZZLED_COMMAND_H
#define DRIZZLED_COMMAND_H

#include <drizzled/server_includes.h>
#include <drizzled/definitions.h>
#include <drizzled/error.h>
#include <drizzled/sql_parse.h>
#include <drizzled/sql_base.h>
#include <drizzled/show.h>

class Session;
class TableList;
class Item;

namespace drizzled
{
namespace command
{

/**
 * @class SqlCommand
 * @brief Represents a command to be executed
 */
class SqlCommand
{
public:
  SqlCommand(enum enum_sql_command in_comm_type,
             Session *in_session)
    : comm_type(in_comm_type),
      session(in_session)
  {}

  virtual ~SqlCommand() {}

  /**
   * Execute the command.
   *
   * @return 1 on failure; 0 on success
   */
  virtual int execute()= 0;

protected:

  /**
   * The type of this command.
   */
  enum enum_sql_command comm_type;

  /**
   * A session handler.
   */
  Session *session;
};

} /* end namespace command */

} /* end namespace drizzled */

#endif /* DRIZZLED_COMMAND_H */