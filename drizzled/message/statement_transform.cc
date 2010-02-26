/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2009 Sun Microsystems
 *  Copyright (c) 2010 Jay Pipes
 *
 *  Authors:
 *
 *    Jay Pipes <jaypipes@gmail.com>
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
 * Implementation of various routines that can be used to convert
 * Statement messages to other formats, including SQL strings.
 */

#include "config.h"

#include "drizzled/message/statement_transform.h"
#include "drizzled/message/transaction.pb.h"
#include "drizzled/message/table.pb.h"

#include <string>
#include <vector>

using namespace std;

namespace drizzled
{

enum message::TransformSqlError
message::transformStatementToSql(const message::Statement &source,
                                 vector<string> &sql_strings,
                                 enum message::TransformSqlVariant sql_variant,
                                 bool already_in_transaction)
{
  message::TransformSqlError error= NONE;

  switch (source.type())
  {
  case message::Statement::INSERT:
    {
      if (! source.has_insert_header())
      {
        error= MISSING_HEADER;
        return error;
      }
      if (! source.has_insert_data())
      {
        error= MISSING_DATA;
        return error;
      }

      const message::InsertHeader &insert_header= source.insert_header();
      const message::InsertData &insert_data= source.insert_data();
      size_t num_keys= insert_data.record_size();
      size_t x;

      if (num_keys > 1 && ! already_in_transaction)
        sql_strings.push_back("START TRANSACTION");

      for (x= 0; x < num_keys; ++x)
      {
        string destination;

        error= transformInsertRecordToSql(insert_header,
                                          insert_data.record(x),
                                          &destination,
                                          sql_variant);
        if (error != NONE)
          break;

        sql_strings.push_back(destination);
      }

      if (num_keys > 1 && ! already_in_transaction)
      {
        if (error == NONE)
          sql_strings.push_back("COMMIT");
        else
          sql_strings.push_back("ROLLBACK");
      }
    }
    break;
  case message::Statement::UPDATE:
    {
      if (! source.has_update_header())
      {
        error= MISSING_HEADER;
        return error;
      }
      if (! source.has_update_data())
      {
        error= MISSING_DATA;
        return error;
      }

      const message::UpdateHeader &update_header= source.update_header();
      const message::UpdateData &update_data= source.update_data();
      size_t num_keys= update_data.record_size();
      size_t x;

      if (num_keys > 1 && ! already_in_transaction)
        sql_strings.push_back("START TRANSACTION");

      for (x= 0; x < num_keys; ++x)
      {
        string destination;

        error= transformUpdateRecordToSql(update_header,
                                          update_data.record(x),
                                          &destination,
                                          sql_variant);
        if (error != NONE)
          break;

        sql_strings.push_back(destination);
      }

      if (num_keys > 1 && ! already_in_transaction)
      {
        if (error == NONE)
          sql_strings.push_back("COMMIT");
        else
          sql_strings.push_back("ROLLBACK");
      }
    }
    break;
  case message::Statement::DELETE:
    {
      if (! source.has_delete_header())
      {
        error= MISSING_HEADER;
        return error;
      }
      if (! source.has_delete_data())
      {
        error= MISSING_DATA;
        return error;
      }

      const message::DeleteHeader &delete_header= source.delete_header();
      const message::DeleteData &delete_data= source.delete_data();
      size_t num_keys= delete_data.record_size();
      size_t x;

      if (num_keys > 1 && ! already_in_transaction)
        sql_strings.push_back("START TRANSACTION");

      for (x= 0; x < num_keys; ++x)
      {
        string destination;

        error= transformDeleteRecordToSql(delete_header,
                                          delete_data.record(x),
                                          &destination,
                                          sql_variant);
        if (error != NONE)
          break;

        sql_strings.push_back(destination);
      }

      if (num_keys > 1 && ! already_in_transaction)
      {
        if (error == NONE)
          sql_strings.push_back("COMMIT");
        else
          sql_strings.push_back("ROLLBACK");
      }
    }
    break;
  case message::Statement::TRUNCATE_TABLE:
    {
      assert(source.has_truncate_table_statement());
      string destination;
      error= message::transformTruncateTableStatementToSql(source.truncate_table_statement(),
                                                           &destination,
                                                           sql_variant);
      sql_strings.push_back(destination);
    }
    break;
  case message::Statement::CREATE_SCHEMA:
    {
      assert(source.has_create_schema_statement());
      string destination;
      error= message::transformCreateSchemaStatementToSql(source.create_schema_statement(),
                                                          &destination,
                                                          sql_variant);
      sql_strings.push_back(destination);
    }
    break;
  case message::Statement::DROP_SCHEMA:
    {
      assert(source.has_drop_schema_statement());
      string destination;
      error= message::transformDropSchemaStatementToSql(source.drop_schema_statement(),
                                                        &destination,
                                                        sql_variant);
      sql_strings.push_back(destination);
    }
    break;
  case message::Statement::DROP_TABLE:
    {
      assert(source.has_drop_table_statement());
      string destination;
      error= message::transformDropTableStatementToSql(source.drop_table_statement(),
                                                       &destination,
                                                       sql_variant);
      sql_strings.push_back(destination);
    }
    break;
  case message::Statement::SET_VARIABLE:
    {
      assert(source.has_set_variable_statement());
      string destination;
      error= message::transformSetVariableStatementToSql(source.set_variable_statement(),
                                                       &destination,
                                                       sql_variant);
      sql_strings.push_back(destination);
    }
    break;
  case message::Statement::RAW_SQL:
  default:
    sql_strings.push_back(source.sql());
    break;
  }
  return error;
}

enum message::TransformSqlError
message::transformInsertHeaderToSql(const message::InsertHeader &header,
                                    string &destination,
                                    enum message::TransformSqlVariant sql_variant)
{
  char quoted_identifier= '`';
  if (sql_variant == ANSI)
    quoted_identifier= '"';

  destination.assign("INSERT INTO ", 12);
  destination.push_back(quoted_identifier);
  destination.append(header.table_metadata().schema_name());
  destination.push_back(quoted_identifier);
  destination.push_back('.');
  destination.push_back(quoted_identifier);
  destination.append(header.table_metadata().table_name());
  destination.push_back(quoted_identifier);
  destination.append(" (", 2);

  /* Add field list to SQL string... */
  size_t num_fields= header.field_metadata_size();
  size_t x;

  for (x= 0; x < num_fields; ++x)
  {
    const message::FieldMetadata &field_metadata= header.field_metadata(x);
    if (x != 0)
      destination.push_back(',');
    
    destination.push_back(quoted_identifier);
    destination.append(field_metadata.name());
    destination.push_back(quoted_identifier);
  }

  return NONE;
}

enum message::TransformSqlError
message::transformInsertRecordToSql(const message::InsertHeader &header,
                                    const message::InsertRecord &record,
                                    string &destination,
                                    enum message::TransformSqlVariant sql_variant)
{
  enum message::TransformSqlError error= transformInsertHeaderToSql(header,
                                                                    destination,
                                                                    sql_variant);

  char quoted_identifier= '`';
  if (sql_variant == ANSI)
    quoted_identifier= '"';

  destination.append(") VALUES (");

  /* Add insert values */
  size_t num_fields= header.field_metadata_size();
  size_t x;
  bool should_quote_field_value= false;
  
  for (x= 0; x < num_fields; ++x)
  {
    if (x != 0)
      destination.push_back(',');

    const message::FieldMetadata &field_metadata= header.field_metadata(x);

    should_quote_field_value= message::shouldQuoteFieldValue(field_metadata.type());

    if (should_quote_field_value)
      destination.push_back('\'');

    if (field_metadata.type() == message::Table::Field::BLOB)
    {
      /* 
        * We do this here because BLOB data is returned
        * in a string correctly, but calling append()
        * without a length will result in only the string
        * up to a \0 being output here.
        */
      string raw_data(record.insert_value(x));
      destination.append(raw_data.c_str(), raw_data.size());
    }
    else
    {
      destination.append(record.insert_value(x));
    }

    if (should_quote_field_value)
      destination.push_back('\'');
  }
  destination.push_back(')');

  return error;
}

enum message::TransformSqlError
message::transformInsertStatementToSql(const message::InsertHeader &header,
                                       const message::InsertData &data,
                                       string &destination,
                                       enum message::TransformSqlVariant sql_variant)
{
  enum message::TransformSqlError error= transformInsertHeaderToSql(header,
                                                                    destination,
                                                                    sql_variant);

  char quoted_identifier= '`';
  if (sql_variant == ANSI)
    quoted_identifier= '"';

  destination.append(") VALUES (", 10);

  /* Add insert values */
  size_t num_records= data.record_size();
  size_t num_fields= header.field_metadata_size();
  size_t x, y;
  bool should_quote_field_value= false;
  
  for (x= 0; x < num_records; ++x)
  {
    if (x != 0)
      destination.append("),(", 3);

    for (y= 0; y < num_fields; ++y)
    {
      if (y != 0)
        destination.push_back(',');

      const message::FieldMetadata &field_metadata= header.field_metadata(y);
      
      should_quote_field_value= message::shouldQuoteFieldValue(field_metadata.type());

      if (should_quote_field_value)
        destination.push_back('\'');

      if (field_metadata.type() == message::Table::Field::BLOB)
      {
        /* 
         * We do this here because BLOB data is returned
         * in a string correctly, but calling append()
         * without a length will result in only the string
         * up to a \0 being output here.
         */
        string raw_data(data.record(x).insert_value(y));
        destination.append(raw_data.c_str(), raw_data.size());
      }
      else
      {
        destination.append(data.record(x).insert_value(y));
      }

      if (should_quote_field_value)
        destination.push_back('\'');
    }
  }
  destination.push_back(')');

  return error;
}

enum message::TransformSqlError
message::transformUpdateHeaderToSql(const message::UpdateHeader &header,
                                    string &destination,
                                    enum message::TransformSqlVariant sql_variant)
{
  char quoted_identifier= '`';
  if (sql_variant == ANSI)
    quoted_identifier= '"';

  destination.assign("UPDATE ", 7);
  destination.push_back(quoted_identifier);
  destination.append(header.table_metadata().schema_name());
  destination.push_back(quoted_identifier);
  destination.push_back('.');
  destination.push_back(quoted_identifier);
  destination.append(header.table_metadata().table_name());
  destination.push_back(quoted_identifier);
  destination.append(" SET ", 5);

  return NONE;
}

enum message::TransformSqlError
message::transformUpdateRecordToSql(const message::UpdateHeader &header,
                                    const message::UpdateRecord &record,
                                    string &destination,
                                    enum message::TransformSqlVariant sql_variant)
{
  enum message::TransformSqlError error= transformUpdateHeaderToSql(header,
                                                                    destination,
                                                                    sql_variant);

  char quoted_identifier= '`';
  if (sql_variant == ANSI)
    quoted_identifier= '"';

  /* Add field SET list to SQL string... */
  size_t num_set_fields= header.set_field_metadata_size();
  size_t x;
  bool should_quote_field_value= false;

  for (x= 0; x < num_set_fields; ++x)
  {
    const message::FieldMetadata &field_metadata= header.set_field_metadata(x);
    if (x != 0)
      destination.push_back(',');
    
    destination.push_back(quoted_identifier);
    destination.append(field_metadata.name());
    destination.push_back(quoted_identifier);
    destination.push_back('=');

    should_quote_field_value= message::shouldQuoteFieldValue(field_metadata.type());

    if (should_quote_field_value)
      destination.push_back('\'');

    if (field_metadata.type() == message::Table::Field::BLOB)
    {
      /* 
       * We do this here because BLOB data is returned
       * in a string correctly, but calling append()
       * without a length will result in only the string
       * up to a \0 being output here.
       */
      string raw_data(record.after_value(x));
      destination.append(raw_data.c_str(), raw_data.size());
    }
    else
    {
      destination.append(record.after_value(x));
    }

    if (should_quote_field_value)
      destination.push_back('\'');
  }

  size_t num_key_fields= header.key_field_metadata_size();

  destination.append(" WHERE ", 7);
  for (x= 0; x < num_key_fields; ++x) 
  {
    const message::FieldMetadata &field_metadata= header.key_field_metadata(x);
    
    if (x != 0)
      destination.append(" AND ", 5); /* Always AND condition with a multi-column PK */

    destination.push_back(quoted_identifier);
    destination.append(field_metadata.name());
    destination.push_back(quoted_identifier);

    destination.push_back('=');

    should_quote_field_value= message::shouldQuoteFieldValue(field_metadata.type());

    if (should_quote_field_value)
      destination.push_back('\'');

    if (field_metadata.type() == message::Table::Field::BLOB)
    {
      /* 
       * We do this here because BLOB data is returned
       * in a string correctly, but calling append()
       * without a length will result in only the string
       * up to a \0 being output here.
       */
      string raw_data(record.key_value(x));
      destination.append(raw_data.c_str(), raw_data.size());
    }
    else
    {
      destination.append(record.key_value(x));
    }

    if (should_quote_field_value)
      destination.push_back('\'');
  }

  return error;
}

enum message::TransformSqlError
message::transformDeleteHeaderToSql(const message::DeleteHeader &header,
                                    string &destination,
                                    enum message::TransformSqlVariant sql_variant)
{
  char quoted_identifier= '`';
  if (sql_variant == ANSI)
    quoted_identifier= '"';

  destination.assign("DELETE FROM ", 12);
  destination.push_back(quoted_identifier);
  destination.append(header.table_metadata().schema_name());
  destination.push_back(quoted_identifier);
  destination.push_back('.');
  destination.push_back(quoted_identifier);
  destination.append(header.table_metadata().table_name());
  destination.push_back(quoted_identifier);

  return NONE;
}

enum message::TransformSqlError
message::transformDeleteRecordToSql(const message::DeleteHeader &header,
                                    const message::DeleteRecord &record,
                                    string &destination,
                                    enum message::TransformSqlVariant sql_variant)
{
  enum message::TransformSqlError error= transformDeleteHeaderToSql(header,
                                                                    destination,
                                                                    sql_variant);
  char quoted_identifier= '`';
  if (sql_variant == ANSI)
    quoted_identifier= '"';

  /* Add WHERE clause to SQL string... */
  uint32_t num_key_fields= header.key_field_metadata_size();
  uint32_t x;
  bool should_quote_field_value= false;

  destination.append(" WHERE ", 7);
  for (x= 0; x < num_key_fields; ++x) 
  {
    const message::FieldMetadata &field_metadata= header.key_field_metadata(x);
    
    if (x != 0)
      destination.append(" AND ", 5); /* Always AND condition with a multi-column PK */

    destination.push_back(quoted_identifier);
    destination.append(field_metadata.name());
    destination.push_back(quoted_identifier);

    destination.push_back('=');

    should_quote_field_value= message::shouldQuoteFieldValue(field_metadata.type());

    if (should_quote_field_value)
      destination.push_back('\'');

    if (field_metadata.type() == message::Table::Field::BLOB)
    {
      /* 
       * We do this here because BLOB data is returned
       * in a string correctly, but calling append()
       * without a length will result in only the string
       * up to a \0 being output here.
       */
      string raw_data(record.key_value(x));
      destination.append(raw_data.c_str(), raw_data.size());
    }
    else
    {
      destination.append(record.key_value(x));
    }

    if (should_quote_field_value)
      destination.push_back('\'');
  }

  return error;
}

enum message::TransformSqlError
message::transformDeleteStatementToSql(const message::DeleteHeader &header,
                                       const message::DeleteData &data,
                                       string &destination,
                                       enum message::TransformSqlVariant sql_variant)
{
  enum message::TransformSqlError error= transformDeleteHeaderToSql(header,
                                                                    destination,
                                                                    sql_variant);
  char quoted_identifier= '`';
  if (sql_variant == ANSI)
    quoted_identifier= '"';

  /* Add WHERE clause to SQL string... */
  uint32_t num_key_fields= header.key_field_metadata_size();
  uint32_t num_key_records= data.record_size();
  uint32_t x, y;
  bool should_quote_field_value= false;

  destination.append(" WHERE ", 7);
  for (x= 0; x < num_key_records; ++x)
  {
    if (x != 0)
      destination.append(" OR ", 4); /* Always OR condition for multiple key records */

    if (num_key_fields > 1)
      destination.push_back('(');

    for (y= 0; y < num_key_fields; ++y) 
    {
      const message::FieldMetadata &field_metadata= header.key_field_metadata(y);
      
      if (y != 0)
        destination.append(" AND ", 5); /* Always AND condition with a multi-column PK */

      destination.push_back(quoted_identifier);
      destination.append(field_metadata.name());
      destination.push_back(quoted_identifier);

      destination.push_back('=');

      should_quote_field_value= message::shouldQuoteFieldValue(field_metadata.type());

      if (should_quote_field_value)
        destination.push_back('\'');

      if (field_metadata.type() == message::Table::Field::BLOB)
      {
        /* 
         * We do this here because BLOB data is returned
         * in a string correctly, but calling append()
         * without a length will result in only the string
         * up to a \0 being output here.
         */
        string raw_data(data.record(x).key_value(y));
        destination.append(raw_data.c_str(), raw_data.size());
      }
      else
      {
        destination.append(data.record(x).key_value(y));
      }

      if (should_quote_field_value)
        destination.push_back('\'');
    }
    if (num_key_fields > 1)
      destination.push_back(')');
  }
  return error;
}

enum message::TransformSqlError
message::transformDropSchemaStatementToSql(const message::DropSchemaStatement &statement,
                                           string &destination,
                                           enum message::TransformSqlVariant sql_variant)
{
  char quoted_identifier= '`';
  if (sql_variant == ANSI)
    quoted_identifier= '"';

  destination.append("DROP SCHEMA ", 12);
  destination.push_back(quoted_identifier);
  destination.append(statement.schema_name());
  destination.push_back(quoted_identifier);

  return NONE;
}

enum message::TransformSqlError
message::transformCreateSchemaStatementToSql(const message::CreateSchemaStatement &statement,
                                             string &destination,
                                             enum message::TransformSqlVariant sql_variant)
{
  char quoted_identifier= '`';
  if (sql_variant == ANSI)
    quoted_identifier= '"';

  const message::Schema &schema= statement.schema();

  destination.append("CREATE SCHEMA ", 14);
  destination.push_back(quoted_identifier);
  destination.append(schema.name());
  destination.push_back(quoted_identifier);

  if (schema.has_collation())
  {
    destination.append(" COLLATE ", 9);
    destination.append(schema.collation());
  }

  return NONE;
}

enum message::TransformSqlError
message::transformDropTableStatementToSql(const message::DropTableStatement &statement,
                                           string &destination,
                                           enum message::TransformSqlVariant sql_variant)
{
  char quoted_identifier= '`';
  if (sql_variant == ANSI)
    quoted_identifier= '"';

  const message::TableMetadata &table_metadata= statement.table_metadata();

  destination.append("DROP TABLE ", 11);

  /* Add the IF EXISTS clause if necessary */
  if (statement.has_if_exists_clause() &&
      statement.if_exists_clause() == true)
  {
    destination.append("IF EXISTS ", 10);
  }

  destination.push_back(quoted_identifier);
  destination.append(table_metadata.schema_name());
  destination.push_back(quoted_identifier);
  destination.push_back('.');
  destination.push_back(quoted_identifier);
  destination.append(table_metadata.table_name());
  destination.push_back(quoted_identifier);

  return NONE;
}

enum message::TransformSqlError
message::transformTruncateTableStatementToSql(const message::TruncateTableStatement &statement,
                                              string &destination,
                                              enum message::TransformSqlVariant sql_variant)
{
  char quoted_identifier= '`';
  if (sql_variant == ANSI)
    quoted_identifier= '"';

  const message::TableMetadata &table_metadata= statement.table_metadata();

  destination.append("TRUNCATE TABLE ", 15);
  destination.push_back(quoted_identifier);
  destination.append(table_metadata.schema_name());
  destination.push_back(quoted_identifier);
  destination.push_back('.');
  destination.push_back(quoted_identifier);
  destination.append(table_metadata.table_name());
  destination.push_back(quoted_identifier);

  return NONE;
}

enum message::TransformSqlError
message::transformSetVariableStatementToSql(const message::SetVariableStatement &statement,
                                            string &destination,
                                            enum message::TransformSqlVariant sql_variant)
{
  (void) sql_variant;
  const message::FieldMetadata &variable_metadata= statement.variable_metadata();
  bool should_quote_field_value= message::shouldQuoteFieldValue(variable_metadata.type());

  destination.append("SET GLOBAL ", 11); /* Only global variables are replicated */
  destination.append(variable_metadata.name());
  destination.push_back('=');

  if (should_quote_field_value)
    destination.push_back('\'');
  
  destination.append(statement.variable_value());

  if (should_quote_field_value)
    destination.push_back('\'');

  return NONE;
}

enum TransformSqlError
transformFieldMetadataToSql(const Table::Field &field,
                            std::string &destination,
                            enum TransformSqlVariant sql_variant= DRIZZLE)
{
  char quoted_identifier= '`';
  if (sql_variant == ANSI)
    quoted_identifier= '"';

  destination.push_back(quoted_identifier);
  destination.append(field.name());
  destination.push_back(quoted_identifier);

  Table::Field::FieldType field_type= field.type();


  switch (field_type)
  {
    case Table::Field::DOUBLE:
    destination.append(" DOUBLE ", 8);
    break;
  case message::Table::Field::VARCHAR:
    {
      destination.append(" VARCHAR(", 9);
      stringstream ss;
      ss << field.string_options().length() << ")";
      destination.append(ss.str());
    }
    break;
  case Table::Field::BLOB:
    destination.append(" BLOB ", 6);
    if (field.string_options().has_collation_id())
    {
      destination.append("COLLATION=", 10);
      destination.append(field.string_options().collation_id());
      detination.push_back(' ');
    }
    break;
  case Table::Field::ENUM:
    {
      size_t num_field_values= field.set_options.field_value_size();
      destination.append(" ENUM(", 6);
      for (size_t x= 0; x < num_field_values; ++x)
      {
        const string &type= field.set_options().field_value(x);

        if (x != 0)
          destination.push_back(',');

        destination.push_back(''');
        destination.append(type);
        destination.push_back(''');
      }
      destination.push_back(')');
      destination.push_back(' ');
      break;
    }
  case message::Table::Field::INTEGER:
    destination.append(" INT ", 5);
    break;
  case message::Table::Field::BIGINT:
    destination.append(" BIGINT ", 8);
    break;
  case message::Table::Field::DECIMAL:
    cout << " DECIMAL(" << field.numeric_options().precision() << "," << field.numeric_options().scale() << ") ";
    break;
  case message::Table::Field::DATE:
    cout << " DATE ";
    break;
  case message::Table::Field::TIMESTAMP:
    cout << " TIMESTAMP ";
    break;
  case message::Table::Field::DATETIME:
    cout << " DATETIME ";
    break;
  }

  if (field.type() == message::Table::Field::INTEGER
      || field.type() == message::Table::Field::BIGINT)
  {
    if (field.has_constraints()
        && field.constraints().has_is_unsigned())
      if (field.constraints().is_unsigned())
        cout << " UNSIGNED";

    if (field.has_numeric_options() &&
      field.numeric_options().is_autoincrement())
      cout << " AUTOINCREMENT ";
  }

  if (!( field.has_constraints()
	 && field.constraints().is_nullable()))
    cout << " NOT NULL ";

  if (field.type() == message::Table::Field::BLOB
      || field.type() == message::Table::Field::VARCHAR)
  {
    if (field.string_options().has_collation())
      cout << " COLLATE " << field.string_options().collation();
  }

  if (field.options().has_default_value())
    cout << " DEFAULT `" << field.options().default_value() << "` " ;

  if (field.options().has_default_bin_value())
  {
    string v= field.options().default_bin_value();
    cout << " DEFAULT 0x";
    for(unsigned int i=0; i< v.length(); i++)
    {
      printf("%.2x", *(v.c_str()+i));
    }
  }

  if (field.type() == message::Table::Field::TIMESTAMP)
    if (field.timestamp_options().has_auto_updates()
      && field.timestamp_options().auto_updates())
      cout << " ON UPDATE CURRENT_TIMESTAMP";

  if (field.has_comment())
    cout << " COMMENT `" << field.comment() << "` ";
  return NONE;
}

bool message::shouldQuoteFieldValue(message::Table::Field::FieldType in_type)
{
  switch (in_type)
  {
  case message::Table::Field::DOUBLE:
  case message::Table::Field::DECIMAL:
  case message::Table::Field::INTEGER:
  case message::Table::Field::BIGINT:
  case message::Table::Field::ENUM:
    return false;
  default:
    return true;
  } 
}

drizzled::message::Table::Field::FieldType message::internalFieldTypeToFieldProtoType(enum enum_field_types type)
{
  switch (type) {
  case DRIZZLE_TYPE_LONG:
    return message::Table::Field::INTEGER;
  case DRIZZLE_TYPE_DOUBLE:
    return message::Table::Field::DOUBLE;
  case DRIZZLE_TYPE_NULL:
    assert(false); /* Not a user definable type */
    return message::Table::Field::INTEGER; /* unreachable */
  case DRIZZLE_TYPE_TIMESTAMP:
    return message::Table::Field::TIMESTAMP;
  case DRIZZLE_TYPE_LONGLONG:
    return message::Table::Field::BIGINT;
  case DRIZZLE_TYPE_DATETIME:
    return message::Table::Field::DATETIME;
  case DRIZZLE_TYPE_DATE:
    return message::Table::Field::DATE;
  case DRIZZLE_TYPE_VARCHAR:
    return message::Table::Field::VARCHAR;
  case DRIZZLE_TYPE_DECIMAL:
    return message::Table::Field::DECIMAL;
  case DRIZZLE_TYPE_ENUM:
    return message::Table::Field::ENUM;
  case DRIZZLE_TYPE_BLOB:
    return message::Table::Field::BLOB;
  }

  assert(false);
  return message::Table::Field::INTEGER; /* unreachable */
}

} /* namespace drizzled */
