/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2010 Mark Atwood
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

#ifndef PLUGIN_SYSLOG_WRAP_H
#define PLUGIN_SYSLOG_WRAP_H

#include <stdarg.h>

class WrapSyslog
{
 private:
  WrapSyslog(const WrapSyslog&);
  WrapSyslog& operator=(const WrapSyslog&);

  WrapSyslog();

  bool openlog_check;
  char openlog_ident[32];

 public:
  ~WrapSyslog();
  static WrapSyslog& singleton();

  static int getFacilityByName(const char *);
  static int getPriorityByName(const char *);

  void openlog(char *ident);
  void vlog(int facility, int priority, const char *format, va_list ap);
  void log(int facility, int priority, const char *format, ...);
};

#endif /* PLUGIN_SYSLOG_WRAP_H */