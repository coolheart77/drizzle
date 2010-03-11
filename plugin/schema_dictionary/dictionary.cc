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
#include "plugin/schema_dictionary/dictionary.h"

using namespace drizzled;

static ColumnsTool *columns;
static IndexPartsTool *index_parts;
static IndexesTool *indexes;
static ReferentialConstraintsTool *referential_constraints;
static SchemasTool *schemas;
static SchemaNames *schema_names;
static ShowColumns *show_columns;
static ShowIndexes *show_indexes;
static TableConstraintsTool *table_constraints;
static TablesTool *tables;
static ShowTables *local_tables;
static ShowTableStatus *table_status;
static ShowTemporaryTables *show_temporary_tables;


static int init(drizzled::plugin::Registry &registry)
{
  columns= new(std::nothrow)ColumnsTool;
  index_parts= new(std::nothrow)IndexPartsTool;
  indexes= new(std::nothrow)IndexesTool;
  local_tables= new(std::nothrow)ShowTables;
  referential_constraints= new(std::nothrow)ReferentialConstraintsTool;
  schema_names= new(std::nothrow)SchemaNames;
  schemas= new(std::nothrow)SchemasTool;
  show_columns= new(std::nothrow)ShowColumns;
  show_indexes= new(std::nothrow)ShowIndexes;
  show_temporary_tables= new(std::nothrow)ShowTemporaryTables;
  table_constraints= new(std::nothrow)TableConstraintsTool;
  table_status= new(std::nothrow)ShowTableStatus;
  tables= new(std::nothrow)TablesTool;

  registry.add(columns);
  registry.add(index_parts);
  registry.add(indexes);
  registry.add(local_tables);
  registry.add(referential_constraints);
  registry.add(schema_names);
  registry.add(schemas);
  registry.add(show_columns);
  registry.add(show_indexes);
  registry.add(show_temporary_tables);
  registry.add(table_constraints);
  registry.add(table_status);
  registry.add(tables);
  
  return 0;
}

static int finalize(drizzled::plugin::Registry &registry)
{
  registry.remove(columns);
  registry.remove(index_parts);
  registry.remove(indexes);
  registry.remove(local_tables);
  registry.remove(referential_constraints);
  registry.remove(schema_names);
  registry.remove(schemas);
  registry.remove(show_columns);
  registry.remove(show_indexes);
  registry.remove(show_temporary_tables);
  registry.remove(table_constraints);
  registry.remove(table_status);
  registry.remove(tables);
  delete columns;
  delete index_parts;
  delete indexes;
  delete local_tables;
  delete referential_constraints;
  delete schema_names;
  delete schemas;
  delete show_columns;
  delete show_indexes;
  delete show_temporary_tables;
  delete table_constraints;
  delete table_status;
  delete tables;

  return 0;
}

DRIZZLE_DECLARE_PLUGIN
{
  DRIZZLE_VERSION_ID,
  "schema_dictionary",
  "1.0",
  "Brian Aker",
  "Data Dictionary for schema, table, column, indexes, etc",
  PLUGIN_LICENSE_GPL,
  init,
  finalize,
  NULL,
  NULL
}
DRIZZLE_DECLARE_PLUGIN_END;
