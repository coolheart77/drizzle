/* - mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*-
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

/**
 * @file 
 *   Implementation of the methods for I_S tables.
 */

#include <drizzled/server_includes.h>
#include <drizzled/session.h>
#include <drizzled/show.h>
#include <drizzled/tztime.h>
#include <drizzled/sql_base.h>
#include <drizzled/plugin/client.h>
#include "drizzled/join_table.h"

#include "info_schema_methods.h"

#include <vector>
#include <string>

using namespace std;

static inline void make_upper(char *buf)
{
  for (; *buf; buf++)
    *buf= my_toupper(system_charset_info, *buf);
}

static bool show_status_array(Session *session, const char *wild,
                              SHOW_VAR *variables,
                              enum enum_var_type value_type,
                              struct system_status_var *status_var,
                              const char *prefix, Table *table,
                              bool ucase_names)
{
  MY_ALIGNED_BYTE_ARRAY(buff_data, SHOW_VAR_FUNC_BUFF_SIZE, int64_t);
  char * const buff= (char *) &buff_data;
  char *prefix_end;
  /* the variable name should not be longer than 64 characters */
  char name_buffer[64];
  int len;
  SHOW_VAR tmp, *var;

  prefix_end= strncpy(name_buffer, prefix, sizeof(name_buffer)-1);
  prefix_end+= strlen(prefix);

  if (*prefix)
    *prefix_end++= '_';
  len=name_buffer + sizeof(name_buffer) - prefix_end;

  for (; variables->name; variables++)
  {
    strncpy(prefix_end, variables->name, len);
    name_buffer[sizeof(name_buffer)-1]=0;       /* Safety */
    if (ucase_names)
      make_upper(name_buffer);

    /*
      if var->type is SHOW_FUNC, call the function.
      Repeat as necessary, if new var is again SHOW_FUNC
    */
    for (var=variables; var->type == SHOW_FUNC; var= &tmp)
      ((mysql_show_var_func)((st_show_var_func_container *)var->value)->func)(&tmp, buff);

    SHOW_TYPE show_type=var->type;
    if (show_type == SHOW_ARRAY)
    {
      show_status_array(session, wild, (SHOW_VAR *) var->value, value_type,
                        status_var, name_buffer, table, ucase_names);
    }
    else
    {
      if (!(wild && wild[0] && wild_case_compare(system_charset_info,
                                                 name_buffer, wild)))
      {
        char *value=var->value;
        const char *pos, *end;                  // We assign a lot of const's
        pthread_mutex_lock(&LOCK_global_system_variables);

        if (show_type == SHOW_SYS)
        {
          show_type= ((sys_var*) value)->show_type();
          value= (char*) ((sys_var*) value)->value_ptr(session, value_type,
                                                       &null_lex_str);
        }

        pos= end= buff;
        /*
          note that value may be == buff. All SHOW_xxx code below
          should still work in this case
        */
        switch (show_type) {
        case SHOW_DOUBLE_STATUS:
          value= ((char *) status_var + (ulong) value);
          /* fall through */
        case SHOW_DOUBLE:
          /* 6 is the default precision for '%f' in sprintf() */
          end= buff + my_fcvt(*(double *) value, 6, buff, NULL);
          break;
        case SHOW_LONG_STATUS:
          value= ((char *) status_var + (ulong) value);
          /* fall through */
        case SHOW_LONG:
          end= int10_to_str(*(long*) value, buff, 10);
          break;
        case SHOW_LONGLONG_STATUS:
          value= ((char *) status_var + (uint64_t) value);
          /* fall through */
        case SHOW_LONGLONG:
          end= int64_t10_to_str(*(int64_t*) value, buff, 10);
          break;
        case SHOW_SIZE:
          {
            stringstream ss (stringstream::in);
            ss << *(size_t*) value;

            string str= ss.str();
            strncpy(buff, str.c_str(), str.length());
            end= buff+ str.length();
          }
          break;
        case SHOW_HA_ROWS:
          end= int64_t10_to_str((int64_t) *(ha_rows*) value, buff, 10);
          break;
        case SHOW_BOOL:
          end+= sprintf(buff,"%s", *(bool*) value ? "ON" : "OFF");
          break;
        case SHOW_MY_BOOL:
          end+= sprintf(buff,"%s", *(bool*) value ? "ON" : "OFF");
          break;
        case SHOW_INT:
        case SHOW_INT_NOFLUSH: // the difference lies in refresh_status()
          end= int10_to_str((long) *(uint32_t*) value, buff, 10);
          break;
        case SHOW_HAVE:
        {
          SHOW_COMP_OPTION tmp_option= *(SHOW_COMP_OPTION *)value;
          pos= show_comp_option_name[(int) tmp_option];
          end= strchr(pos, '\0');
          break;
        }
        case SHOW_CHAR:
        {
          if (!(pos= value))
            pos= "";
          end= strchr(pos, '\0');
          break;
        }
       case SHOW_CHAR_PTR:
        {
          if (!(pos= *(char**) value))
            pos= "";
          end= strchr(pos, '\0');
          break;
        }
        case SHOW_KEY_CACHE_LONG:
          value= (char*) dflt_key_cache + (ulong)value;
          end= int10_to_str(*(long*) value, buff, 10);
          break;
        case SHOW_KEY_CACHE_LONGLONG:
          value= (char*) dflt_key_cache + (ulong)value;
	  end= int64_t10_to_str(*(int64_t*) value, buff, 10);
	  break;
        case SHOW_UNDEF:
          break;                                        // Return empty string
        case SHOW_SYS:                                  // Cannot happen
        default:
          assert(0);
          break;
        }
        table->restoreRecordAsDefault();
        table->field[0]->store(name_buffer, strlen(name_buffer),
                               system_charset_info);
        table->field[1]->store(pos, (uint32_t) (end - pos), system_charset_info);
        table->field[1]->set_notnull();

        pthread_mutex_unlock(&LOCK_global_system_variables);

        if (schema_table_store_record(session, table))
          return true;
      }
    }
  }

  return false;
}


void store_key_column_usage(Table *table, 
                            LEX_STRING *db_name,
                            LEX_STRING *table_name, 
                            const char *key_name,
                            uint32_t key_len, 
                            const char *con_type, 
                            uint32_t con_len,
                            int64_t idx)
{
  const CHARSET_INFO * const cs= system_charset_info;
  table->field[1]->store(db_name->str, db_name->length, cs);
  table->field[2]->store(key_name, key_len, cs);
  table->field[4]->store(db_name->str, db_name->length, cs);
  table->field[5]->store(table_name->str, table_name->length, cs);
  table->field[6]->store(con_type, con_len, cs);
  table->field[7]->store((int64_t) idx, true);
}

int StatsISMethods::processTable(Session *session, TableList *tables,
                                  Table *table, bool res,
                                  LEX_STRING *db_name,
                                  LEX_STRING *table_name) const
{
  const CHARSET_INFO * const cs= system_charset_info;
  if (res)
  {
    if (session->lex->sql_command != SQLCOM_SHOW_KEYS)
    {
      /*
        I.e. we are in SELECT FROM INFORMATION_SCHEMA.STATISTICS
        rather than in SHOW KEYS
      */
      if (session->is_error())
      {
        push_warning(session, DRIZZLE_ERROR::WARN_LEVEL_WARN,
                     session->main_da.sql_errno(), session->main_da.message());
      }
      session->clear_error();
      res= 0;
    }
    return (res);
  }
  else
  {
    Table *show_table= tables->table;
    KEY *key_info=show_table->s->key_info;
    if (show_table->cursor)
    {
      show_table->cursor->info(HA_STATUS_VARIABLE |
                               HA_STATUS_NO_LOCK |
                               HA_STATUS_TIME);
    }
    for (uint32_t i=0 ; i < show_table->s->keys ; i++,key_info++)
    {
      KEY_PART_INFO *key_part= key_info->key_part;
      const char *str;
      for (uint32_t j=0 ; j < key_info->key_parts ; j++,key_part++)
      {
        table->restoreRecordAsDefault();
        table->field[1]->store(db_name->str, db_name->length, cs);
        table->field[2]->store(table_name->str, table_name->length, cs);
        table->field[3]->store((int64_t) ((key_info->flags &
                                            HA_NOSAME) ? 0 : 1), true);
        table->field[4]->store(db_name->str, db_name->length, cs);
        table->field[5]->store(key_info->name, strlen(key_info->name), cs);
        table->field[6]->store((int64_t) (j+1), true);
        str=(key_part->field ? key_part->field->field_name :
             "?unknown field?");
        table->field[7]->store(str, strlen(str), cs);
        if (show_table->cursor)
        {
          if (show_table->cursor->index_flags(i, j, 0) & HA_READ_ORDER)
          {
            table->field[8]->store(((key_part->key_part_flag &
                                     HA_REVERSE_SORT) ?
                                    "D" : "A"), 1, cs);
            table->field[8]->set_notnull();
          }
          KEY *key=show_table->key_info+i;
          if (key->rec_per_key[j])
          {
            ha_rows records=(show_table->cursor->stats.records /
                             key->rec_per_key[j]);
            table->field[9]->store((int64_t) records, true);
            table->field[9]->set_notnull();
          }
          str= show_table->cursor->index_type(i);
          table->field[13]->store(str, strlen(str), cs);
        }
        if ((key_part->field &&
             key_part->length !=
             show_table->s->field[key_part->fieldnr-1]->key_length()))
        {
          table->field[10]->store((int64_t) key_part->length /
                                  key_part->field->charset()->mbmaxlen, true);
          table->field[10]->set_notnull();
        }
        uint32_t flags= key_part->field ? key_part->field->flags : 0;
        const char *pos=(char*) ((flags & NOT_NULL_FLAG) ? "" : "YES");
        table->field[12]->store(pos, strlen(pos), cs);
        if (!show_table->s->keys_in_use.test(i))
        {
          table->field[14]->store(STRING_WITH_LEN("disabled"), cs);
        }
        else
        {
          table->field[14]->store("", 0, cs);
        }
        table->field[14]->set_notnull();
        assert(test(key_info->flags & HA_USES_COMMENT) ==
                   (key_info->comment.length > 0));
        if (key_info->flags & HA_USES_COMMENT)
        {
          table->field[15]->store(key_info->comment.str,
                                  key_info->comment.length, cs);
        }
        if (schema_table_store_record(session, table))
        {
          return (1);
        }
      }
    }
  }
  return(res);
}

int StatusISMethods::fillTable(Session *session, TableList *tables)
{
  LEX *lex= session->lex;
  const char *wild= lex->wild ? lex->wild->ptr() : NULL;
  int res= 0;
  STATUS_VAR *tmp1, tmp;
  const string schema_table_name= tables->schema_table->getTableName();
  enum enum_var_type option_type;
  bool upper_case_names= (schema_table_name.compare("STATUS") != 0);

  if (schema_table_name.compare("STATUS") == 0)
  {
    option_type= lex->option_type;
    if (option_type == OPT_GLOBAL)
      tmp1= &tmp;
    else
      tmp1= session->initial_status_var;
  }
  else if (schema_table_name.compare("GLOBAL_STATUS") == 0)
  {
    option_type= OPT_GLOBAL;
    tmp1= &tmp;
  }
  else
  {
    option_type= OPT_SESSION;
    tmp1= &session->status_var;
  }

  pthread_mutex_lock(&LOCK_status);
  if (option_type == OPT_GLOBAL)
    calc_sum_of_all_status(&tmp);
  res= show_status_array(session, wild,
                         getFrontOfStatusVars(),
                         option_type, tmp1, "", tables->table,
                         upper_case_names);
  pthread_mutex_unlock(&LOCK_status);
  return(res);
}

int TabNamesISMethods::oldFormat(Session *session, drizzled::plugin::InfoSchemaTable *schema_table)
  const
{
  char tmp[128];
  String buffer(tmp,sizeof(tmp), session->charset());
  LEX *lex= session->lex;
  Name_resolution_context *context= &lex->select_lex.context;
  const drizzled::plugin::InfoSchemaTable::Columns columns= schema_table->getColumns();

  const drizzled::plugin::ColumnInfo *column= columns[2];
  buffer.length(0);
  buffer.append(column->getOldName().c_str());
  buffer.append(lex->select_lex.db);
  if (lex->wild && lex->wild->ptr())
  {
    buffer.append(STRING_WITH_LEN(" ("));
    buffer.append(lex->wild->ptr());
    buffer.append(')');
  }
  Item_field *field= new Item_field(context,
                                    NULL, NULL, column->getName().c_str());
  if (session->add_item_to_list(field))
  {
    return 1;
  }
  field->set_name(buffer.ptr(), buffer.length(), system_charset_info);
  if (session->lex->verbose)
  {
    field->set_name(buffer.ptr(), buffer.length(), system_charset_info);
    column= columns[3];
    field= new Item_field(context, NULL, NULL, column->getName().c_str());
    if (session->add_item_to_list(field))
    {
      return 1;
    }
    field->set_name(column->getOldName().c_str(),
                    column->getOldName().length(),
                    system_charset_info);
  }
  return 0;
}

int VariablesISMethods::fillTable(Session *session, TableList *tables)
{
  int res= 0;
  LEX *lex= session->lex;
  const char *wild= lex->wild ? lex->wild->ptr() : NULL;
  const string schema_table_name= tables->schema_table->getTableName();
  enum enum_var_type option_type= OPT_SESSION;
  bool upper_case_names= (schema_table_name.compare("VARIABLES") != 0);
  bool sorted_vars= (schema_table_name.compare("VARIABLES") == 0);

  if (lex->option_type == OPT_GLOBAL ||
      schema_table_name.compare("GLOBAL_VARIABLES") == 0)
  {
    option_type= OPT_GLOBAL;
  }

  pthread_rwlock_rdlock(&LOCK_system_variables_hash);
  res= show_status_array(session, wild, enumerate_sys_vars(session, sorted_vars),
                         option_type, NULL, "", tables->table, upper_case_names);
  pthread_rwlock_unlock(&LOCK_system_variables_hash);
  return(res);
}

