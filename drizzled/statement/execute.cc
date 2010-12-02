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

#include "config.h"

#include "drizzled/statement/execute.h"
#include "drizzled/session.h"
#include "drizzled/user_var_entry.h"
#include "drizzled/plugin/listen.h"
#include "drizzled/plugin/client.h"
#include "drizzled/plugin/null_client.h"
#include "drizzled/plugin/client/concurrent.h"

namespace drizzled
{

void mysql_parse(drizzled::Session *session, const char *inBuf, uint32_t length);

namespace statement
{

Execute::Execute(Session *in_session,
                 drizzled::execute_string_t to_execute_arg,
                 bool is_quiet_arg,
                 bool is_concurrent_arg,
                 bool should_wait_arg) :
  Statement(in_session),
  is_quiet(is_quiet_arg),
  is_concurrent(is_concurrent_arg),
  should_wait(should_wait_arg),
  to_execute(to_execute_arg)
{
}
  

bool statement::Execute::parseVariable()
{
  if (to_execute.isVariable())
  {
    user_var_entry *var= getSession()->getVariable(to_execute, false);

    if (var && var->length && var->value && var->type == STRING_RESULT)
    {
      LEX_STRING tmp_for_var;
      tmp_for_var.str= var->value; 
      tmp_for_var.length= var->length; 
      to_execute.set(tmp_for_var);

      return true;
    }
  }

  return false;
}


bool statement::Execute::runStatement(plugin::NullClient *client, const std::string &arg)
{
  client->pushSQL(arg);
  if (not getSession()->executeStatement())
    return true;

  if (getSession()->is_error())
    return true;

  return false;
}


bool statement::Execute::execute()
{
  bool ret= execute_shell();

  // We have to restore ourselves at the top for delete() to work.
  getSession()->getLex()->statement= this;

  return ret;
}


bool statement::Execute::execute_shell()
{
  if (to_execute.length == 0)
  {
    my_error(ER_WRONG_ARGUMENTS, MYF(0), "Invalid Variable");
    return false;
  }

  if (to_execute.isVariable())
  {
    if (not parseVariable())
    {
      my_error(ER_WRONG_ARGUMENTS, MYF(0), "Invalid Variable");
      return false;
    }
  }

  if (is_concurrent)
  {
    boost_thread_shared_ptr thread;

    if (getSession()->isConcurrentExecuteAllowed())
    {
      plugin::client::Concurrent *client= new plugin::client::Concurrent;
      std::string execution_string(to_execute.str, to_execute.length);
      client->pushSQL(execution_string);
      Session::shared_ptr new_session(new Session(client));

      // We set the current schema.  @todo do the same with catalog
      if (not getSession()->getSchema().empty())
        new_session->set_db(getSession()->getSchema());

      new_session->setConcurrentExecute(false);

      // Overwrite the context in the next session, with what we have in our
      // session. Eventually we will allow someone to change the effective
      // user.
      new_session->getSecurityContext()= getSession()->getSecurityContext();

      if (Session::schedule(new_session))
      {
        Session::unlink(new_session);
      }
      else if (should_wait)
      {
        thread= new_session->getThread();
      }
    }
    else
    {
      my_error(ER_WRONG_ARGUMENTS, MYF(0), "A Concurrent Execution Session can not launch another session.");
      return false;
    }

    if (should_wait && thread && thread->joinable())
    {
      // We want to make sure that we can be killed
      boost::this_thread::restore_interruption dl(getSession()->getThreadInterupt());
      try {
        thread->join();
      }
      catch(boost::thread_interrupted const&)
      {
        // Just surpress and return the error
        my_error(drizzled::ER_QUERY_INTERRUPTED, MYF(0));

        return false;
      }
    }
  }
  else 
  {
    if (is_quiet)
    {
      plugin::Client *temp= getSession()->getClient();
      plugin::NullClient *null_client= new plugin::NullClient;

      getSession()->setClient(null_client);
      
      bool error_occured= false;
      bool is_savepoint= false;
      {
        std::string start_sql;
        if (getSession()->inTransaction())
        {
          // @todo Figure out something a bit more solid then this.
          start_sql.append("SAVEPOINT execute_internal_savepoint");
          is_savepoint= true;
        }
        else
        {
          start_sql.append("START TRANSACTION");
        }

        error_occured= runStatement(null_client, start_sql);
      }

      // @note this is copied from code in NULL client, all of this belongs
      // in the pluggable parser pieces.  
      if (not error_occured)
      {
        typedef boost::tokenizer<boost::escaped_list_separator<char> > Tokenizer;
        std::string full_string(to_execute.str, to_execute.length);
        Tokenizer tok(full_string, boost::escaped_list_separator<char>("\\", ";", "\""));

        for (Tokenizer::iterator iter= tok.begin();
             iter != tok.end() and getSession()->getKilled() != Session::KILL_CONNECTION;
             ++iter)
        {
          if (runStatement(null_client, *iter))
          {
            error_occured= true;
            break;
          }
        }

        // @todo Encapsulate logic later to method
        {
          std::string final_sql;
          if (is_savepoint)
          {
            if (error_occured)
            {
              final_sql.append("ROLLBACK TO SAVEPOINT execute_internal_savepoint");
            }
            else
            {
              final_sql.append("RELEASE SAVEPOINT execute_internal_savepoint");
            }
          }
          else
          {
            if (error_occured)
            {
              final_sql.append("ROLLBACK");
            }
            else
            {
              final_sql.append("COMMIT");
            }
          }

          // Run the cleanup command, we currently ignore if an error occurs
          // here.
          (void)runStatement(null_client, final_sql);
        }
      }

      getSession()->setClient(temp);
      if (getSession()->is_error())
      {
        getSession()->clear_error(true);
      }
      else
      {
        getSession()->clearDiagnostics();
      }

      getSession()->my_ok();

      null_client->close();
      delete null_client;
    }
    else
    {
      mysql_parse(getSession(), to_execute.str, to_execute.length);
    }
  }

  return true;
}

} /* namespace statement */
} /* namespace drizzled */

