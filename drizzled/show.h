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

/**
 * @file
 *
 * Contains function declarations that deal with the SHOW commands.  These
 * will eventually go away, but for now we split these definitions out into
 * their own header file for easier maintenance
 */
#ifndef DRIZZLE_SERVER_SHOW_H
#define DRIZZLE_SERVER_SHOW_H

#include <drizzled/sql_list.h>
#include <drizzled/lex_string.h>
#include <drizzled/sql_parse.h>

#include <vector>

/* Forward declarations */
class String;
class JOIN;
class Session;
struct st_ha_create_information;
typedef st_ha_create_information HA_CREATE_INFO;
struct TableList;
class InfoSchemaTable;

typedef struct system_status_var STATUS_VAR;


enum find_files_result {
  FIND_FILES_OK,
  FIND_FILES_OOM,
  FIND_FILES_DIR
};

typedef struct st_lookup_field_values
{
  LEX_STRING db_value, table_value;
  bool wild_db_value, wild_table_value;
} LOOKUP_FIELD_VALUES;

find_files_result find_files(Session *session, std::vector<LEX_STRING*> &files, const char *db,
                             const char *path, const char *wild, bool dir);
bool calc_lookup_values_from_cond(Session *session, COND *cond, TableList *table,
                                  LOOKUP_FIELD_VALUES *lookup_field_vals);
bool get_lookup_field_values(Session *session, COND *cond, TableList *tables,
                             LOOKUP_FIELD_VALUES *lookup_field_values);
int make_db_list(Session *session, std::vector<LEX_STRING*> &files,
                 LOOKUP_FIELD_VALUES *lookup_field_vals, bool *with_i_schema);
SHOW_VAR *getFrontOfStatusVars();

int store_create_info(TableList *table_list, String *packet, HA_CREATE_INFO  *create_info_arg);

bool schema_table_store_record(Session *session, Table *table);

int get_quote_char_for_identifier();
int wild_case_compare(const CHARSET_INFO * const cs, 
                      const char *str,const char *wildstr);

InfoSchemaTable *find_schema_table(const char* table_name);
bool make_schema_select(Session *session,  Select_Lex *sel,
                        const std::string& schema_table_name);
bool mysql_schema_table(Session *session, LEX *lex, TableList *table_list);
bool get_schema_tables_result(JOIN *join, enum enum_schema_table_state executed_place);

bool mysqld_show_open_tables(Session *session,const char *wild);
bool mysqld_show_logs(Session *session);
void mysqld_list_fields(Session *session,TableList *table, const char *wild);
int mysqld_dump_create_info(Session *session, TableList *table_list, int fd);
bool drizzled_show_create(Session *session, TableList *table_list);
bool mysqld_show_create_db(Session *session, char *dbname, bool if_not_exists);

int mysqld_show_status(Session *session);
int mysqld_show_variables(Session *session,const char *wild);
bool mysqld_show_storage_engines(Session *session);
bool mysqld_show_column_types(Session *session);
void mysqld_list_processes(Session *session,const char *user, bool verbose);
void calc_sum_of_all_status(STATUS_VAR *to);

int add_status_vars(SHOW_VAR *list);
void remove_status_vars(SHOW_VAR *list);
void init_status_vars();
void free_status_vars();
void reset_status_vars();
void add_infoschema_table(InfoSchemaTable *schema_table);
void remove_infoschema_table(InfoSchemaTable *table);

#endif /* DRIZZLE_SERVER_SHOW_H */
