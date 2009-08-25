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

#ifndef DRIZZLED_STATEMENT_FLUSH_H
#define DRIZZLED_STATEMENT_FLUSH_H

#include <drizzled/statement.h>

class Session;

namespace drizzled
{
namespace statement
{

class Flush : public Statement
{
public:
  Flush(Session *in_session)
    :
      Statement(in_session, SQLCOM_FLUSH)
  {}

  bool execute();

private:

  /**
   * Reload/resets privileges and the different caches.
   *
   * @note Depending on 'options', it may be very bad to write the
   * query to the binlog (e.g. FLUSH SLAVE); this is a
   * pointer where reloadCache() will put 0 if
   * it thinks we really should not write to the binlog.
   * Otherwise it will put 1.
   * 
   * @return Error status code
   * @retval 0 Ok
   * @retval !=0  Error; session->killed is set or session->is_error() is true
   */
  bool reloadCache();
};

} /* end namespace statement */

} /* end namespace drizzled */

#endif /* DRIZZLED_STATEMENT_FLUSH_H */