/* Copyright (C) 2009 Sun Microsystems

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA */

#ifndef PLUGIN_POOL_OF_THREADS_SESSION_SCHEDULER_H
#define PLUGIN_POOL_OF_THREADS_SESSION_SCHEDULER_H

#include <event.h>

namespace drizzled
{
class Session;
}

class session_scheduler
{
public:
  bool logged_in;
  struct event io_event;
  drizzled::Session *session;
  bool thread_attached;  /* Indicates if Session is attached to the OS thread */

  session_scheduler(drizzled::Session *);
  bool thread_attach();
  void thread_detach();
};

#endif /* PLUGIN_POOL_OF_THREADS_SESSION_SCHEDULER_H */
