/* Copyright (C) 2000-2006 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */


/**
  @file

  @brief
  This file implements classes defined in field.h
*/

#ifdef USE_PRAGMA_IMPLEMENTATION
#pragma implementation				// gcc: Class implementation
#endif

#include "mysql_priv.h"
#include "sql_select.h"
#include <m_ctype.h>
#include <errno.h>

// Maximum allowed exponent value for converting string to decimal
#define MAX_EXPONENT 1024

/*****************************************************************************
  Instansiate templates and static variables
*****************************************************************************/

#ifdef HAVE_EXPLICIT_TEMPLATE_INSTANTIATION
template class List<Create_field>;
template class List_iterator<Create_field>;
#endif

uchar Field_null::null[1]={1};
const char field_separator=',';

#define DOUBLE_TO_STRING_CONVERSION_BUFFER_SIZE FLOATING_POINT_BUFFER
#define LONGLONG_TO_STRING_CONVERSION_BUFFER_SIZE 128
#define DECIMAL_TO_STRING_CONVERSION_BUFFER_SIZE 128
#define BLOB_PACK_LENGTH_TO_MAX_LENGH(arg) \
((ulong) ((1LL << min(arg, 4) * 8) - 1LL))

/*
  Rules for merging different types of fields in UNION

  NOTE: to avoid 256*256 table, gap in table types numeration is skiped
  following #defines describe that gap and how to canculate number of fields
  and index of field in thia array.
*/
#define FIELDTYPE_TEAR_FROM (MYSQL_TYPE_VARCHAR + 1)
#define FIELDTYPE_TEAR_TO   (MYSQL_TYPE_NEWDECIMAL - 1)
#define FIELDTYPE_NUM (FIELDTYPE_TEAR_FROM + (255 - FIELDTYPE_TEAR_TO))
inline int field_type2index (enum_field_types field_type)
{
  return (field_type < FIELDTYPE_TEAR_FROM ?
          field_type :
          ((int)FIELDTYPE_TEAR_FROM) + (field_type - FIELDTYPE_TEAR_TO) - 1);
}


static enum_field_types field_types_merge_rules [FIELDTYPE_NUM][FIELDTYPE_NUM]=
{
  /* MYSQL_TYPE_DECIMAL -> */
  {
  //MYSQL_TYPE_DECIMAL      MYSQL_TYPE_TINY
    MYSQL_TYPE_NEWDECIMAL,  MYSQL_TYPE_NEWDECIMAL,
  //MYSQL_TYPE_SHORT        MYSQL_TYPE_LONG
    MYSQL_TYPE_NEWDECIMAL,  MYSQL_TYPE_NEWDECIMAL,
  //MYSQL_TYPE_FLOAT        MYSQL_TYPE_DOUBLE
    MYSQL_TYPE_DOUBLE,      MYSQL_TYPE_DOUBLE,
  //MYSQL_TYPE_NULL         MYSQL_TYPE_TIMESTAMP
    MYSQL_TYPE_NEWDECIMAL,  MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_LONGLONG     MYSQL_TYPE_INT24
  //MYSQL_TYPE_DECIMAL,     MYSQL_TYPE_DECIMAL,
  //MYSQL_TYPE_DATE         MYSQL_TYPE_TIME
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_DATETIME     MYSQL_TYPE_YEAR
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NEWDATE      MYSQL_TYPE_VARCHAR
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NEWDECIMAL   MYSQL_TYPE_ENUM
    MYSQL_TYPE_NEWDECIMAL,  MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_SET
    MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_BLOB         MYSQL_TYPE_VAR_STRING
    MYSQL_TYPE_BLOB,        MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_STRING       
    MYSQL_TYPE_STRING
  },
  /* MYSQL_TYPE_TINY -> */
  {
  //MYSQL_TYPE_DECIMAL      MYSQL_TYPE_TINY
    MYSQL_TYPE_NEWDECIMAL,  MYSQL_TYPE_TINY,
  //MYSQL_TYPE_SHORT        MYSQL_TYPE_LONG
    MYSQL_TYPE_SHORT,       MYSQL_TYPE_LONG,
  //MYSQL_TYPE_FLOAT        MYSQL_TYPE_DOUBLE
    MYSQL_TYPE_FLOAT,       MYSQL_TYPE_DOUBLE,
  //MYSQL_TYPE_NULL         MYSQL_TYPE_TIMESTAMP
    MYSQL_TYPE_TINY,        MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_LONGLONG     MYSQL_TYPE_INT24
    MYSQL_TYPE_LONGLONG,    MYSQL_TYPE_INT24,
  //MYSQL_TYPE_DATE         MYSQL_TYPE_TIME
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_DATETIME     MYSQL_TYPE_YEAR
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_TINY,
  //MYSQL_TYPE_NEWDATE      MYSQL_TYPE_VARCHAR
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NEWDECIMAL   MYSQL_TYPE_ENUM
    MYSQL_TYPE_NEWDECIMAL,  MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_SET
    MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_BLOB         MYSQL_TYPE_VAR_STRING
    MYSQL_TYPE_BLOB,        MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_STRING       
    MYSQL_TYPE_STRING     
  },
  /* MYSQL_TYPE_SHORT -> */
  {
  //MYSQL_TYPE_DECIMAL      MYSQL_TYPE_TINY
    MYSQL_TYPE_NEWDECIMAL,  MYSQL_TYPE_SHORT,
  //MYSQL_TYPE_SHORT        MYSQL_TYPE_LONG
    MYSQL_TYPE_SHORT,       MYSQL_TYPE_LONG,
  //MYSQL_TYPE_FLOAT        MYSQL_TYPE_DOUBLE
    MYSQL_TYPE_FLOAT,       MYSQL_TYPE_DOUBLE,
  //MYSQL_TYPE_NULL         MYSQL_TYPE_TIMESTAMP
    MYSQL_TYPE_SHORT,       MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_LONGLONG     MYSQL_TYPE_INT24
    MYSQL_TYPE_LONGLONG,    MYSQL_TYPE_INT24,
  //MYSQL_TYPE_DATE         MYSQL_TYPE_TIME
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_DATETIME     MYSQL_TYPE_YEAR
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_SHORT,
  //MYSQL_TYPE_NEWDATE      MYSQL_TYPE_VARCHAR
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NEWDECIMAL   MYSQL_TYPE_ENUM
    MYSQL_TYPE_NEWDECIMAL,  MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_SET
    MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_BLOB         MYSQL_TYPE_VAR_STRING
    MYSQL_TYPE_BLOB,        MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_STRING
    MYSQL_TYPE_STRING
  },
  /* MYSQL_TYPE_LONG -> */
  {
  //MYSQL_TYPE_DECIMAL      MYSQL_TYPE_TINY
    MYSQL_TYPE_NEWDECIMAL,  MYSQL_TYPE_LONG,
  //MYSQL_TYPE_SHORT        MYSQL_TYPE_LONG
    MYSQL_TYPE_LONG,        MYSQL_TYPE_LONG,
  //MYSQL_TYPE_FLOAT        MYSQL_TYPE_DOUBLE
    MYSQL_TYPE_DOUBLE,      MYSQL_TYPE_DOUBLE,
  //MYSQL_TYPE_NULL         MYSQL_TYPE_TIMESTAMP
    MYSQL_TYPE_LONG,         MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_LONGLONG     MYSQL_TYPE_INT24
    MYSQL_TYPE_LONGLONG,    MYSQL_TYPE_INT24,
  //MYSQL_TYPE_DATE         MYSQL_TYPE_TIME
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_DATETIME     MYSQL_TYPE_YEAR
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_LONG,
  //MYSQL_TYPE_NEWDATE      MYSQL_TYPE_VARCHAR
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NEWDECIMAL   MYSQL_TYPE_ENUM
    MYSQL_TYPE_NEWDECIMAL,  MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_SET
    MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_BLOB         MYSQL_TYPE_VAR_STRING
    MYSQL_TYPE_BLOB,        MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_STRING
    MYSQL_TYPE_STRING
  },
  /* MYSQL_TYPE_FLOAT -> */
  {
  //MYSQL_TYPE_DECIMAL      MYSQL_TYPE_TINY
    MYSQL_TYPE_DOUBLE,      MYSQL_TYPE_FLOAT,
  //MYSQL_TYPE_SHORT        MYSQL_TYPE_LONG
    MYSQL_TYPE_FLOAT,       MYSQL_TYPE_DOUBLE,
  //MYSQL_TYPE_FLOAT        MYSQL_TYPE_DOUBLE
    MYSQL_TYPE_FLOAT,       MYSQL_TYPE_DOUBLE,
  //MYSQL_TYPE_NULL         MYSQL_TYPE_TIMESTAMP
    MYSQL_TYPE_FLOAT,       MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_LONGLONG     MYSQL_TYPE_INT24
    MYSQL_TYPE_FLOAT,       MYSQL_TYPE_INT24,
  //MYSQL_TYPE_DATE         MYSQL_TYPE_TIME
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_DATETIME     MYSQL_TYPE_YEAR
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_FLOAT,
  //MYSQL_TYPE_NEWDATE      MYSQL_TYPE_VARCHAR
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NEWDECIMAL   MYSQL_TYPE_ENUM
    MYSQL_TYPE_DOUBLE,      MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_SET
    MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_BLOB         MYSQL_TYPE_VAR_STRING
    MYSQL_TYPE_BLOB,        MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_STRING
    MYSQL_TYPE_STRING
  },
  /* MYSQL_TYPE_DOUBLE -> */
  {
  //MYSQL_TYPE_DECIMAL      MYSQL_TYPE_TINY
    MYSQL_TYPE_DOUBLE,      MYSQL_TYPE_DOUBLE,
  //MYSQL_TYPE_SHORT        MYSQL_TYPE_LONG
    MYSQL_TYPE_DOUBLE,      MYSQL_TYPE_DOUBLE,
  //MYSQL_TYPE_FLOAT        MYSQL_TYPE_DOUBLE
    MYSQL_TYPE_DOUBLE,      MYSQL_TYPE_DOUBLE,
  //MYSQL_TYPE_NULL         MYSQL_TYPE_TIMESTAMP
    MYSQL_TYPE_DOUBLE,      MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_LONGLONG     MYSQL_TYPE_INT24
    MYSQL_TYPE_DOUBLE,      MYSQL_TYPE_INT24,
  //MYSQL_TYPE_DATE         MYSQL_TYPE_TIME
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_DATETIME     MYSQL_TYPE_YEAR
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_DOUBLE,
  //MYSQL_TYPE_NEWDATE      MYSQL_TYPE_VARCHAR
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NEWDECIMAL   MYSQL_TYPE_ENUM
    MYSQL_TYPE_DOUBLE,      MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_SET
    MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_BLOB         MYSQL_TYPE_VAR_STRING
    MYSQL_TYPE_BLOB,        MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_STRING
    MYSQL_TYPE_STRING
  },
  /* MYSQL_TYPE_NULL -> */
  {
  //MYSQL_TYPE_DECIMAL      MYSQL_TYPE_TINY
    MYSQL_TYPE_NEWDECIMAL,  MYSQL_TYPE_TINY,
  //MYSQL_TYPE_SHORT        MYSQL_TYPE_LONG
    MYSQL_TYPE_SHORT,       MYSQL_TYPE_LONG,
  //MYSQL_TYPE_FLOAT        MYSQL_TYPE_DOUBLE
    MYSQL_TYPE_FLOAT,       MYSQL_TYPE_DOUBLE,
  //MYSQL_TYPE_NULL         MYSQL_TYPE_TIMESTAMP
    MYSQL_TYPE_NULL,        MYSQL_TYPE_TIMESTAMP,
  //MYSQL_TYPE_LONGLONG     MYSQL_TYPE_INT24
    MYSQL_TYPE_LONGLONG,    MYSQL_TYPE_INT24,
  //MYSQL_TYPE_DATE         MYSQL_TYPE_TIME
    MYSQL_TYPE_NEWDATE,     MYSQL_TYPE_TIME,
  //MYSQL_TYPE_DATETIME     MYSQL_TYPE_YEAR
    MYSQL_TYPE_DATETIME,    MYSQL_TYPE_YEAR,
  //MYSQL_TYPE_NEWDATE      MYSQL_TYPE_VARCHAR
    MYSQL_TYPE_NEWDATE,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NEWDECIMAL   MYSQL_TYPE_ENUM
    MYSQL_TYPE_NEWDECIMAL,  MYSQL_TYPE_ENUM,
  //MYSQL_TYPE_SET
    MYSQL_TYPE_SET,
  //MYSQL_TYPE_BLOB         MYSQL_TYPE_VAR_STRING
    MYSQL_TYPE_BLOB,        MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_STRING
    MYSQL_TYPE_STRING
  },
  /* MYSQL_TYPE_TIMESTAMP -> */
  {
  //MYSQL_TYPE_DECIMAL      MYSQL_TYPE_TINY
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_SHORT        MYSQL_TYPE_LONG
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_FLOAT        MYSQL_TYPE_DOUBLE
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NULL         MYSQL_TYPE_TIMESTAMP
    MYSQL_TYPE_TIMESTAMP,   MYSQL_TYPE_TIMESTAMP,
  //MYSQL_TYPE_LONGLONG     MYSQL_TYPE_INT24
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_DATE         MYSQL_TYPE_TIME
    MYSQL_TYPE_DATETIME,    MYSQL_TYPE_DATETIME,
  //MYSQL_TYPE_DATETIME     MYSQL_TYPE_YEAR
    MYSQL_TYPE_DATETIME,    MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NEWDATE      MYSQL_TYPE_VARCHAR
    MYSQL_TYPE_NEWDATE,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NEWDECIMAL   MYSQL_TYPE_ENUM
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_SET
    MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_BLOB         MYSQL_TYPE_VAR_STRING
    MYSQL_TYPE_BLOB,        MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_STRING
    MYSQL_TYPE_STRING
  },
  /* MYSQL_TYPE_LONGLONG -> */
  {
  //MYSQL_TYPE_DECIMAL      MYSQL_TYPE_TINY
    MYSQL_TYPE_NEWDECIMAL,  MYSQL_TYPE_LONGLONG,
  //MYSQL_TYPE_SHORT        MYSQL_TYPE_LONG
    MYSQL_TYPE_LONGLONG,    MYSQL_TYPE_LONGLONG,
  //MYSQL_TYPE_FLOAT        MYSQL_TYPE_DOUBLE
    MYSQL_TYPE_DOUBLE,      MYSQL_TYPE_DOUBLE,
  //MYSQL_TYPE_NULL         MYSQL_TYPE_TIMESTAMP
    MYSQL_TYPE_LONGLONG,    MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_LONGLONG     MYSQL_TYPE_INT24
    MYSQL_TYPE_LONGLONG,    MYSQL_TYPE_LONG,
  //MYSQL_TYPE_DATE         MYSQL_TYPE_TIME
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_DATETIME     MYSQL_TYPE_YEAR
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_LONGLONG,
  //MYSQL_TYPE_NEWDATE      MYSQL_TYPE_VARCHAR
    MYSQL_TYPE_NEWDATE,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NEWDECIMAL   MYSQL_TYPE_ENUM
    MYSQL_TYPE_NEWDECIMAL,  MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_SET
    MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_BLOB         MYSQL_TYPE_VAR_STRING
    MYSQL_TYPE_BLOB,        MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_STRING
    MYSQL_TYPE_STRING
  },
  /* MYSQL_TYPE_INT24 -> */
  {
  //MYSQL_TYPE_DECIMAL      MYSQL_TYPE_TINY
    MYSQL_TYPE_NEWDECIMAL,  MYSQL_TYPE_INT24,
  //MYSQL_TYPE_SHORT        MYSQL_TYPE_LONG
    MYSQL_TYPE_INT24,       MYSQL_TYPE_LONG,
  //MYSQL_TYPE_FLOAT        MYSQL_TYPE_DOUBLE
    MYSQL_TYPE_FLOAT,       MYSQL_TYPE_DOUBLE,
  //MYSQL_TYPE_NULL         MYSQL_TYPE_TIMESTAMP
    MYSQL_TYPE_INT24,       MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_LONGLONG     MYSQL_TYPE_INT24
    MYSQL_TYPE_LONGLONG,    MYSQL_TYPE_INT24,
  //MYSQL_TYPE_DATE         MYSQL_TYPE_TIME
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_DATETIME     MYSQL_TYPE_YEAR
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_INT24,
  //MYSQL_TYPE_NEWDATE      MYSQL_TYPE_VARCHAR
    MYSQL_TYPE_NEWDATE,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NEWDECIMAL    MYSQL_TYPE_ENUM
    MYSQL_TYPE_NEWDECIMAL,  MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_SET
    MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_BLOB         MYSQL_TYPE_VAR_STRING
    MYSQL_TYPE_BLOB,        MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_STRING
    MYSQL_TYPE_STRING
  },
  /* MYSQL_TYPE_DATE -> */
  {
  //MYSQL_TYPE_DECIMAL      MYSQL_TYPE_TINY
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_SHORT        MYSQL_TYPE_LONG
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_FLOAT        MYSQL_TYPE_DOUBLE
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NULL         MYSQL_TYPE_TIMESTAMP
    MYSQL_TYPE_NEWDATE,     MYSQL_TYPE_DATETIME,
  //MYSQL_TYPE_LONGLONG     MYSQL_TYPE_INT24
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_DATE         MYSQL_TYPE_TIME
    MYSQL_TYPE_NEWDATE,     MYSQL_TYPE_DATETIME,
  //MYSQL_TYPE_DATETIME     MYSQL_TYPE_YEAR
    MYSQL_TYPE_DATETIME,    MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NEWDATE      MYSQL_TYPE_VARCHAR
    MYSQL_TYPE_NEWDATE,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NEWDECIMAL   MYSQL_TYPE_ENUM
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_SET
    MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_BLOB         MYSQL_TYPE_VAR_STRING
    MYSQL_TYPE_BLOB,        MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_STRING
    MYSQL_TYPE_STRING
  },
  /* MYSQL_TYPE_TIME -> */
  {
  //MYSQL_TYPE_DECIMAL      MYSQL_TYPE_TINY
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_SHORT        MYSQL_TYPE_LONG
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_FLOAT        MYSQL_TYPE_DOUBLE
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NULL         MYSQL_TYPE_TIMESTAMP
    MYSQL_TYPE_TIME,        MYSQL_TYPE_DATETIME,
  //MYSQL_TYPE_LONGLONG     MYSQL_TYPE_INT24
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_DATE         MYSQL_TYPE_TIME
    MYSQL_TYPE_DATETIME,    MYSQL_TYPE_TIME,
  //MYSQL_TYPE_DATETIME     MYSQL_TYPE_YEAR
    MYSQL_TYPE_DATETIME,    MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NEWDATE      MYSQL_TYPE_VARCHAR
    MYSQL_TYPE_NEWDATE,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NEWDECIMAL   MYSQL_TYPE_ENUM
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_SET
    MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_BLOB         MYSQL_TYPE_VAR_STRING
    MYSQL_TYPE_BLOB,        MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_STRING
    MYSQL_TYPE_STRING
  },
  /* MYSQL_TYPE_DATETIME -> */
  {
  //MYSQL_TYPE_DECIMAL      MYSQL_TYPE_TINY
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_SHORT        MYSQL_TYPE_LONG
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_FLOAT        MYSQL_TYPE_DOUBLE
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NULL         MYSQL_TYPE_TIMESTAMP
    MYSQL_TYPE_DATETIME,    MYSQL_TYPE_DATETIME,
  //MYSQL_TYPE_LONGLONG     MYSQL_TYPE_INT24
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_DATE         MYSQL_TYPE_TIME
    MYSQL_TYPE_DATETIME,    MYSQL_TYPE_DATETIME,
  //MYSQL_TYPE_DATETIME     MYSQL_TYPE_YEAR
    MYSQL_TYPE_DATETIME,    MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NEWDATE      MYSQL_TYPE_VARCHAR
    MYSQL_TYPE_NEWDATE,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NEWDECIMAL   MYSQL_TYPE_ENUM
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_SET
    MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_BLOB         MYSQL_TYPE_VAR_STRING
    MYSQL_TYPE_BLOB,        MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_STRING
    MYSQL_TYPE_STRING
  },
  /* MYSQL_TYPE_YEAR -> */
  {
  //MYSQL_TYPE_DECIMAL      MYSQL_TYPE_TINY
    MYSQL_TYPE_NEWDECIMAL,  MYSQL_TYPE_TINY,
  //MYSQL_TYPE_SHORT        MYSQL_TYPE_LONG
    MYSQL_TYPE_SHORT,       MYSQL_TYPE_LONG,
  //MYSQL_TYPE_FLOAT        MYSQL_TYPE_DOUBLE
    MYSQL_TYPE_FLOAT,       MYSQL_TYPE_DOUBLE,
  //MYSQL_TYPE_NULL         MYSQL_TYPE_TIMESTAMP
    MYSQL_TYPE_YEAR,        MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_LONGLONG     MYSQL_TYPE_INT24
    MYSQL_TYPE_LONGLONG,    MYSQL_TYPE_INT24,
  //MYSQL_TYPE_DATE         MYSQL_TYPE_TIME
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_DATETIME     MYSQL_TYPE_YEAR
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_YEAR,
  //MYSQL_TYPE_NEWDATE      MYSQL_TYPE_VARCHAR
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NEWDECIMAL   MYSQL_TYPE_ENUM
    MYSQL_TYPE_NEWDECIMAL,  MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_SET
    MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_BLOB         MYSQL_TYPE_VAR_STRING
    MYSQL_TYPE_BLOB,        MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_STRING
    MYSQL_TYPE_STRING
  },
  /* MYSQL_TYPE_NEWDATE -> */
  {
  //MYSQL_TYPE_DECIMAL      MYSQL_TYPE_TINY
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_SHORT        MYSQL_TYPE_LONG
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_FLOAT        MYSQL_TYPE_DOUBLE
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NULL         MYSQL_TYPE_TIMESTAMP
    MYSQL_TYPE_NEWDATE,     MYSQL_TYPE_DATETIME,
  //MYSQL_TYPE_LONGLONG     MYSQL_TYPE_INT24
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_DATE         MYSQL_TYPE_TIME
    MYSQL_TYPE_NEWDATE,     MYSQL_TYPE_DATETIME,
  //MYSQL_TYPE_DATETIME     MYSQL_TYPE_YEAR
    MYSQL_TYPE_DATETIME,    MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NEWDATE      MYSQL_TYPE_VARCHAR
    MYSQL_TYPE_NEWDATE,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NEWDECIMAL   MYSQL_TYPE_ENUM
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_SET
    MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_BLOB         MYSQL_TYPE_VAR_STRING
    MYSQL_TYPE_BLOB,        MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_STRING
    MYSQL_TYPE_STRING
  },
  /* MYSQL_TYPE_VARCHAR -> */
  {
  //MYSQL_TYPE_DECIMAL      MYSQL_TYPE_TINY
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_SHORT        MYSQL_TYPE_LONG
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_FLOAT        MYSQL_TYPE_DOUBLE
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NULL         MYSQL_TYPE_TIMESTAMP
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_LONGLONG     MYSQL_TYPE_INT24
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_DATE         MYSQL_TYPE_TIME
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_DATETIME     MYSQL_TYPE_YEAR
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NEWDATE      MYSQL_TYPE_VARCHAR
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NEWDECIMAL   MYSQL_TYPE_ENUM
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_SET
    MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_BLOB         MYSQL_TYPE_VAR_STRING
    MYSQL_TYPE_BLOB,        MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_STRING
    MYSQL_TYPE_VARCHAR
  },
  /* MYSQL_TYPE_NEWDECIMAL -> */
  {
  //MYSQL_TYPE_DECIMAL      MYSQL_TYPE_TINY
    MYSQL_TYPE_NEWDECIMAL,  MYSQL_TYPE_NEWDECIMAL,
  //MYSQL_TYPE_SHORT        MYSQL_TYPE_LONG
    MYSQL_TYPE_NEWDECIMAL,  MYSQL_TYPE_NEWDECIMAL,
  //MYSQL_TYPE_FLOAT        MYSQL_TYPE_DOUBLE
    MYSQL_TYPE_DOUBLE,      MYSQL_TYPE_DOUBLE,
  //MYSQL_TYPE_NULL         MYSQL_TYPE_TIMESTAMP
    MYSQL_TYPE_NEWDECIMAL,  MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_LONGLONG     MYSQL_TYPE_INT24
    MYSQL_TYPE_NEWDECIMAL,  MYSQL_TYPE_NEWDECIMAL,
  //MYSQL_TYPE_DATE         MYSQL_TYPE_TIME
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_DATETIME     MYSQL_TYPE_YEAR
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_NEWDECIMAL,
  //MYSQL_TYPE_NEWDATE      MYSQL_TYPE_VARCHAR
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NEWDECIMAL   MYSQL_TYPE_ENUM
    MYSQL_TYPE_NEWDECIMAL,  MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_SET
    MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_BLOB         MYSQL_TYPE_VAR_STRING
    MYSQL_TYPE_BLOB,        MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_STRING
    MYSQL_TYPE_STRING
  },
  /* MYSQL_TYPE_ENUM -> */
  {
  //MYSQL_TYPE_DECIMAL      MYSQL_TYPE_TINY
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_SHORT        MYSQL_TYPE_LONG
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_FLOAT        MYSQL_TYPE_DOUBLE
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NULL         MYSQL_TYPE_TIMESTAMP
    MYSQL_TYPE_ENUM,        MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_LONGLONG     MYSQL_TYPE_INT24
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_DATE         MYSQL_TYPE_TIME
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_DATETIME     MYSQL_TYPE_YEAR
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NEWDATE      MYSQL_TYPE_VARCHAR
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NEWDECIMAL   MYSQL_TYPE_ENUM
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_SET
    MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_BLOB         MYSQL_TYPE_VAR_STRING
    MYSQL_TYPE_BLOB,        MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_STRING
    MYSQL_TYPE_STRING
  },
  /* MYSQL_TYPE_SET -> */
  {
  //MYSQL_TYPE_DECIMAL      MYSQL_TYPE_TINY
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_SHORT        MYSQL_TYPE_LONG
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_FLOAT        MYSQL_TYPE_DOUBLE
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NULL         MYSQL_TYPE_TIMESTAMP
    MYSQL_TYPE_SET,         MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_LONGLONG     MYSQL_TYPE_INT24
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_DATE         MYSQL_TYPE_TIME
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_DATETIME     MYSQL_TYPE_YEAR
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NEWDATE      MYSQL_TYPE_VARCHAR
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NEWDECIMAL   MYSQL_TYPE_ENUM
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_SET
    MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_BLOB         MYSQL_TYPE_VAR_STRING
    MYSQL_TYPE_BLOB,        MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_STRING
    MYSQL_TYPE_STRING
  },
  /* MYSQL_TYPE_BLOB -> */
  {
  //MYSQL_TYPE_DECIMAL      MYSQL_TYPE_TINY
    MYSQL_TYPE_BLOB,        MYSQL_TYPE_BLOB,
  //MYSQL_TYPE_SHORT        MYSQL_TYPE_LONG
    MYSQL_TYPE_BLOB,        MYSQL_TYPE_BLOB,
  //MYSQL_TYPE_FLOAT        MYSQL_TYPE_DOUBLE
    MYSQL_TYPE_BLOB,        MYSQL_TYPE_BLOB,
  //MYSQL_TYPE_NULL         MYSQL_TYPE_TIMESTAMP
    MYSQL_TYPE_BLOB,        MYSQL_TYPE_BLOB,
  //MYSQL_TYPE_LONGLONG     MYSQL_TYPE_INT24
    MYSQL_TYPE_BLOB,        MYSQL_TYPE_BLOB,
  //MYSQL_TYPE_DATE         MYSQL_TYPE_TIME
    MYSQL_TYPE_BLOB,        MYSQL_TYPE_BLOB,
  //MYSQL_TYPE_DATETIME     MYSQL_TYPE_YEAR
    MYSQL_TYPE_BLOB,        MYSQL_TYPE_BLOB,
  //MYSQL_TYPE_NEWDATE      MYSQL_TYPE_VARCHAR
    MYSQL_TYPE_BLOB,        MYSQL_TYPE_BLOB,
  //MYSQL_TYPE_NEWDECIMAL   MYSQL_TYPE_ENUM
    MYSQL_TYPE_BLOB,        MYSQL_TYPE_BLOB,
  //MYSQL_TYPE_SET
    MYSQL_TYPE_BLOB,
  //MYSQL_TYPE_BLOB         MYSQL_TYPE_VAR_STRING
    MYSQL_TYPE_BLOB,        MYSQL_TYPE_BLOB,
  //MYSQL_TYPE_STRING
    MYSQL_TYPE_BLOB
  },
  /* MYSQL_TYPE_VAR_STRING -> */
  {
  //MYSQL_TYPE_DECIMAL      MYSQL_TYPE_TINY
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_SHORT        MYSQL_TYPE_LONG
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_FLOAT        MYSQL_TYPE_DOUBLE
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NULL         MYSQL_TYPE_TIMESTAMP
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_LONGLONG     MYSQL_TYPE_INT24
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_DATE         MYSQL_TYPE_TIME
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_DATETIME     MYSQL_TYPE_YEAR
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NEWDATE      MYSQL_TYPE_VARCHAR
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NEWDECIMAL   MYSQL_TYPE_ENUM
    MYSQL_TYPE_VARCHAR,     MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_SET
    MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_BLOB         MYSQL_TYPE_VAR_STRING
    MYSQL_TYPE_BLOB,        MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_STRING
    MYSQL_TYPE_VARCHAR
  },
  /* MYSQL_TYPE_STRING -> */
  {
  //MYSQL_TYPE_DECIMAL      MYSQL_TYPE_TINY
    MYSQL_TYPE_STRING,      MYSQL_TYPE_STRING,
  //MYSQL_TYPE_SHORT        MYSQL_TYPE_LONG
    MYSQL_TYPE_STRING,      MYSQL_TYPE_STRING,
  //MYSQL_TYPE_FLOAT        MYSQL_TYPE_DOUBLE
    MYSQL_TYPE_STRING,      MYSQL_TYPE_STRING,
  //MYSQL_TYPE_NULL         MYSQL_TYPE_TIMESTAMP
    MYSQL_TYPE_STRING,      MYSQL_TYPE_STRING,
  //MYSQL_TYPE_LONGLONG     MYSQL_TYPE_INT24
    MYSQL_TYPE_STRING,      MYSQL_TYPE_STRING,
  //MYSQL_TYPE_DATE         MYSQL_TYPE_TIME
    MYSQL_TYPE_STRING,      MYSQL_TYPE_STRING,
  //MYSQL_TYPE_DATETIME     MYSQL_TYPE_YEAR
    MYSQL_TYPE_STRING,      MYSQL_TYPE_STRING,
  //MYSQL_TYPE_NEWDATE      MYSQL_TYPE_VARCHAR
    MYSQL_TYPE_STRING,      MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_NEWDECIMAL   MYSQL_TYPE_ENUM
    MYSQL_TYPE_STRING,      MYSQL_TYPE_STRING,
  //MYSQL_TYPE_SET
    MYSQL_TYPE_STRING,
  //MYSQL_TYPE_BLOB         MYSQL_TYPE_VAR_STRING
    MYSQL_TYPE_BLOB,        MYSQL_TYPE_VARCHAR,
  //MYSQL_TYPE_STRING
    MYSQL_TYPE_STRING
  }
};

/**
  Return type of which can carry value of both given types in UNION result.

  @param a  type for merging
  @param b  type for merging

  @return
    type of field
*/

enum_field_types Field::field_type_merge(enum_field_types a,
                                         enum_field_types b)
{
  assert(a < FIELDTYPE_TEAR_FROM || a > FIELDTYPE_TEAR_TO);
  assert(b < FIELDTYPE_TEAR_FROM || b > FIELDTYPE_TEAR_TO);
  return field_types_merge_rules[field_type2index(a)]
                                [field_type2index(b)];
}


static Item_result field_types_result_type [FIELDTYPE_NUM]=
{
  //MYSQL_TYPE_DECIMAL      MYSQL_TYPE_TINY
  DECIMAL_RESULT,           INT_RESULT,
  //MYSQL_TYPE_SHORT        MYSQL_TYPE_LONG
  INT_RESULT,               INT_RESULT,
  //MYSQL_TYPE_FLOAT        MYSQL_TYPE_DOUBLE
  REAL_RESULT,              REAL_RESULT,
  //MYSQL_TYPE_NULL         MYSQL_TYPE_TIMESTAMP
  STRING_RESULT,            STRING_RESULT,
  //MYSQL_TYPE_LONGLONG     MYSQL_TYPE_INT24
  INT_RESULT,               INT_RESULT,
  //MYSQL_TYPE_DATE         MYSQL_TYPE_TIME
  STRING_RESULT,            STRING_RESULT,
  //MYSQL_TYPE_DATETIME     MYSQL_TYPE_YEAR
  STRING_RESULT,            INT_RESULT,
  //MYSQL_TYPE_NEWDATE      MYSQL_TYPE_VARCHAR
  STRING_RESULT,            STRING_RESULT,
  //MYSQL_TYPE_NEWDECIMAL   MYSQL_TYPE_ENUM
  DECIMAL_RESULT,           STRING_RESULT,
  //MYSQL_TYPE_SET
  STRING_RESULT,
  //MYSQL_TYPE_BLOB         MYSQL_TYPE_VAR_STRING
  STRING_RESULT,            STRING_RESULT,
  //MYSQL_TYPE_STRING
  STRING_RESULT
};


/*
  Test if the given string contains important data:
  not spaces for character string,
  or any data for binary string.

  SYNOPSIS
    test_if_important_data()
    cs          Character set
    str         String to test
    strend      String end

  RETURN
    false - If string does not have important data
    true  - If string has some important data
*/

static bool
test_if_important_data(CHARSET_INFO *cs, const char *str, const char *strend)
{
  if (cs != &my_charset_bin)
    str+= cs->cset->scan(cs, str, strend, MY_SEQ_SPACES);
  return (str < strend);
}


/**
  Detect Item_result by given field type of UNION merge result.

  @param field_type  given field type

  @return
    Item_result (type of internal MySQL expression result)
*/

Item_result Field::result_merge_type(enum_field_types field_type)
{
  assert(field_type < FIELDTYPE_TEAR_FROM || field_type
              > FIELDTYPE_TEAR_TO);
  return field_types_result_type[field_type2index(field_type)];
}

/*****************************************************************************
  Static help functions
*****************************************************************************/


/**
  Check whether a field type can be partially indexed by a key.

  This is a static method, rather than a virtual function, because we need
  to check the type of a non-Field in mysql_alter_table().

  @param type  field type

  @retval
    true  Type can have a prefixed key
  @retval
    false Type can not have a prefixed key
*/

bool Field::type_can_have_key_part(enum enum_field_types type)
{
  switch (type) {
  case MYSQL_TYPE_VARCHAR:
  case MYSQL_TYPE_BLOB:
  case MYSQL_TYPE_VAR_STRING:
  case MYSQL_TYPE_STRING:
    return true;
  default:
    return false;
  }
}


/**
  Numeric fields base class constructor.
*/
Field_num::Field_num(uchar *ptr_arg,uint32 len_arg, uchar *null_ptr_arg,
                     uchar null_bit_arg, utype unireg_check_arg,
                     const char *field_name_arg,
                     uint8 dec_arg, bool zero_arg, bool unsigned_arg)
  :Field(ptr_arg, len_arg, null_ptr_arg, null_bit_arg,
         unireg_check_arg, field_name_arg),
  dec(dec_arg),zerofill(zero_arg),unsigned_flag(unsigned_arg)
{
  if (zerofill)
    flags|=ZEROFILL_FLAG;
  if (unsigned_flag)
    flags|=UNSIGNED_FLAG;
}


void Field_num::prepend_zeros(String *value)
{
  int diff;
  if ((diff= (int) (field_length - value->length())) > 0)
  {
    bmove_upp((uchar*) value->ptr()+field_length,
              (uchar*) value->ptr()+value->length(),
	      value->length());
    bfill((uchar*) value->ptr(),diff,'0');
    value->length(field_length);
    (void) value->c_ptr_quick();		// Avoid warnings in purify
  }
}

/**
  Test if given number is a int.

  @todo
    Make this multi-byte-character safe

  @param str		String to test
  @param length        Length of 'str'
  @param int_end	Pointer to char after last used digit
  @param cs		Character set

  @note
    This is called after one has called strntoull10rnd() function.

  @retval
    0	OK
  @retval
    1	error: empty string or wrong integer.
  @retval
    2   error: garbage at the end of string.
*/

int Field_num::check_int(CHARSET_INFO *cs, const char *str, int length, 
                         const char *int_end, int error)
{
  /* Test if we get an empty string or wrong integer */
  if (str == int_end || error == MY_ERRNO_EDOM)
  {
    char buff[128];
    String tmp(buff, (uint32) sizeof(buff), system_charset_info);
    tmp.copy(str, length, system_charset_info);
    push_warning_printf(table->in_use, MYSQL_ERROR::WARN_LEVEL_WARN,
                        ER_TRUNCATED_WRONG_VALUE_FOR_FIELD, 
                        ER(ER_TRUNCATED_WRONG_VALUE_FOR_FIELD),
                        "integer", tmp.c_ptr(), field_name,
                        (ulong) table->in_use->row_count);
    return 1;
  }
  /* Test if we have garbage at the end of the given string. */
  if (test_if_important_data(cs, int_end, str + length))
  {
    set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, WARN_DATA_TRUNCATED, 1);
    return 2;
  }
  return 0;
}


/*
  Conver a string to an integer then check bounds.
  
  SYNOPSIS
    Field_num::get_int
    cs            Character set
    from          String to convert
    len           Length of the string
    rnd           OUT longlong value
    unsigned_max  max unsigned value
    signed_min    min signed value
    signed_max    max signed value

  DESCRIPTION
    The function calls strntoull10rnd() to get an integer value then
    check bounds and errors returned. In case of any error a warning
    is raised.

  RETURN
    0   ok
    1   error
*/

bool Field_num::get_int(CHARSET_INFO *cs, const char *from, uint len,
                        longlong *rnd, uint64_t unsigned_max, 
                        longlong signed_min, longlong signed_max)
{
  char *end;
  int error;
  
  *rnd= (longlong) cs->cset->strntoull10rnd(cs, from, len,
                                            unsigned_flag, &end,
                                            &error);
  if (unsigned_flag)
  {

    if ((((uint64_t) *rnd > unsigned_max) && (*rnd= (longlong) unsigned_max)) ||
        error == MY_ERRNO_ERANGE)
    {
      goto out_of_range;
    }
  }
  else
  {
    if (*rnd < signed_min)
    {
      *rnd= signed_min;
      goto out_of_range;
    }
    else if (*rnd > signed_max)
    {
      *rnd= signed_max;
      goto out_of_range;
    }
  }
  if (table->in_use->count_cuted_fields &&
      check_int(cs, from, len, end, error))
    return 1;
  return 0;

out_of_range:
  set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_OUT_OF_RANGE, 1);
  return 1;
}

/**
  Process decimal library return codes and issue warnings for overflow and
  truncation.

  @param op_result  decimal library return code (E_DEC_* see include/decimal.h)

  @retval
    1  there was overflow
  @retval
    0  no error or some other errors except overflow
*/

int Field::warn_if_overflow(int op_result)
{
  if (op_result == E_DEC_OVERFLOW)
  {
    set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_OUT_OF_RANGE, 1);
    return 1;
  }
  if (op_result == E_DEC_TRUNCATED)
  {
    set_warning(MYSQL_ERROR::WARN_LEVEL_NOTE, WARN_DATA_TRUNCATED, 1);
    /* We return 0 here as this is not a critical issue */
  }
  return 0;
}


#ifdef NOT_USED
static bool test_if_real(const char *str,int length, CHARSET_INFO *cs)
{
  cs= system_charset_info; // QQ move test_if_real into CHARSET_INFO struct

  while (length && my_isspace(cs,*str))
  {						// Allow start space
    length--; str++;
  }
  if (!length)
    return 0;
  if (*str == '+' || *str == '-')
  {
    length--; str++;
    if (!length || !(my_isdigit(cs,*str) || *str == '.'))
      return 0;
  }
  while (length && my_isdigit(cs,*str))
  {
    length--; str++;
  }
  if (!length)
    return 1;
  if (*str == '.')
  {
    length--; str++;
    while (length && my_isdigit(cs,*str))
    {
      length--; str++;
    }
  }
  if (!length)
    return 1;
  if (*str == 'E' || *str == 'e')
  {
    if (length < 3 || (str[1] != '+' && str[1] != '-') || 
        !my_isdigit(cs,str[2]))
      return 0;
    length-=3;
    str+=3;
    while (length && my_isdigit(cs,*str))
    {
      length--; str++;
    }
  }
  for (; length ; length--, str++)
  {						// Allow end space
    if (!my_isspace(cs,*str))
      return 0;
  }
  return 1;
}
#endif


/**
  Interpret field value as an integer but return the result as a string.

  This is used for printing bit_fields as numbers while debugging.
*/

String *Field::val_int_as_str(String *val_buffer, my_bool unsigned_val)
{
  CHARSET_INFO *cs= &my_charset_bin;
  uint length;
  longlong value= val_int();

  if (val_buffer->alloc(MY_INT64_NUM_DECIMAL_DIGITS))
    return 0;
  length= (uint) (*cs->cset->longlong10_to_str)(cs, (char*) val_buffer->ptr(),
                                                MY_INT64_NUM_DECIMAL_DIGITS,
                                                unsigned_val ? 10 : -10,
                                                value);
  val_buffer->length(length);
  return val_buffer;
}


/// This is used as a table name when the table structure is not set up
Field::Field(uchar *ptr_arg,uint32 length_arg,uchar *null_ptr_arg,
	     uchar null_bit_arg,
	     utype unireg_check_arg, const char *field_name_arg)
  :ptr(ptr_arg), null_ptr(null_ptr_arg),
   table(0), orig_table(0), table_name(0),
   field_name(field_name_arg),
   key_start(0), part_of_key(0), part_of_key_not_clustered(0),
   part_of_sortkey(0), unireg_check(unireg_check_arg),
   field_length(length_arg), null_bit(null_bit_arg), 
   is_created_from_null_item(false)
{
  flags=null_ptr ? 0: NOT_NULL_FLAG;
  comment.str= (char*) "";
  comment.length=0;
  field_index= 0;
}


void Field::hash(ulong *nr, ulong *nr2)
{
  if (is_null())
  {
    *nr^= (*nr << 1) | 1;
  }
  else
  {
    uint len= pack_length();
    CHARSET_INFO *cs= charset();
    cs->coll->hash_sort(cs, ptr, len, nr, nr2);
  }
}

size_t
Field::do_last_null_byte() const
{
  assert(null_ptr == NULL || null_ptr >= table->record[0]);
  if (null_ptr)
    return (size_t) (null_ptr - table->record[0]) + 1;
  return LAST_NULL_BYTE_UNDEF;
}


void Field::copy_from_tmp(int row_offset)
{
  memcpy(ptr,ptr+row_offset,pack_length());
  if (null_ptr)
  {
    *null_ptr= (uchar) ((null_ptr[0] & (uchar) ~(uint) null_bit) | (null_ptr[row_offset] & (uchar) null_bit));
  }
}


bool Field::send_binary(Protocol *protocol)
{
  char buff[MAX_FIELD_WIDTH];
  String tmp(buff,sizeof(buff),charset());
  val_str(&tmp);
  return protocol->store(tmp.ptr(), tmp.length(), tmp.charset());
}


/**
   Check to see if field size is compatible with destination.

   This method is used in row-based replication to verify that the slave's
   field size is less than or equal to the master's field size. The 
   encoded field metadata (from the master or source) is decoded and compared
   to the size of this field (the slave or destination). 

   @param   field_metadata   Encoded size in field metadata

   @retval 0 if this field's size is < the source field's size
   @retval 1 if this field's size is >= the source field's size
*/
int Field::compatible_field_size(uint field_metadata)
{
  uint const source_size= pack_length_from_metadata(field_metadata);
  uint const destination_size= row_pack_length();
  return (source_size <= destination_size);
}


int Field::store(const char *to, uint length, CHARSET_INFO *cs,
                 enum_check_fields check_level)
{
  int res;
  enum_check_fields old_check_level= table->in_use->count_cuted_fields;
  table->in_use->count_cuted_fields= check_level;
  res= store(to, length, cs);
  table->in_use->count_cuted_fields= old_check_level;
  return res;
}


/**
   Pack the field into a format suitable for storage and transfer.

   To implement packing functionality, only the virtual function
   should be overridden. The other functions are just convenience
   functions and hence should not be overridden.

   The value of <code>low_byte_first</code> is dependent on how the
   packed data is going to be used: for local use, e.g., temporary
   store on disk or in memory, use the native format since that is
   faster. For data that is going to be transfered to other machines
   (e.g., when writing data to the binary log), data should always be
   stored in little-endian format.

   @note The default method for packing fields just copy the raw bytes
   of the record into the destination, but never more than
   <code>max_length</code> characters.

   @param to
   Pointer to memory area where representation of field should be put.

   @param from
   Pointer to memory area where record representation of field is
   stored.

   @param max_length
   Maximum length of the field, as given in the column definition. For
   example, for <code>CHAR(1000)</code>, the <code>max_length</code>
   is 1000. This information is sometimes needed to decide how to pack
   the data.

   @param low_byte_first
   @c true if integers should be stored little-endian, @c false if
   native format should be used. Note that for little-endian machines,
   the value of this flag is a moot point since the native format is
   little-endian.
*/
uchar *
Field::pack(uchar *to, const uchar *from, uint max_length,
            bool low_byte_first __attribute__((unused)))
{
  uint32 length= pack_length();
  set_if_smaller(length, max_length);
  memcpy(to, from, length);
  return to+length;
}

/**
   Unpack a field from row data.

   This method is used to unpack a field from a master whose size of
   the field is less than that of the slave.

   The <code>param_data</code> parameter is a two-byte integer (stored
   in the least significant 16 bits of the unsigned integer) usually
   consisting of two parts: the real type in the most significant byte
   and a original pack length in the least significant byte.

   The exact layout of the <code>param_data</code> field is given by
   the <code>Table_map_log_event::save_field_metadata()</code>.

   This is the default method for unpacking a field. It just copies
   the memory block in byte order (of original pack length bytes or
   length of field, whichever is smaller).

   @param   to         Destination of the data
   @param   from       Source of the data
   @param   param_data Real type and original pack length of the field
                       data

   @param low_byte_first
   If this flag is @c true, all composite entities (e.g., lengths)
   should be unpacked in little-endian format; otherwise, the entities
   are unpacked in native order.

   @return  New pointer into memory based on from + length of the data
*/
const uchar *
Field::unpack(uchar* to, const uchar *from, uint param_data,
              bool low_byte_first __attribute__((unused)))
{
  uint length=pack_length();
  int from_type= 0;
  /*
    If from length is > 255, it has encoded data in the upper bits. Need
    to mask it out.
  */
  if (param_data > 255)
  {
    from_type= (param_data & 0xff00) >> 8U;  // real_type.
    param_data= param_data & 0x00ff;        // length.
  }

  if ((param_data == 0) ||
      (length == param_data) ||
      (from_type != real_type()))
  {
    memcpy(to, from, length);
    return from+length;
  }

  uint len= (param_data && (param_data < length)) ?
            param_data : length;

  memcpy(to, from, param_data > length ? length : len);
  return from+len;
}


my_decimal *Field::val_decimal(my_decimal *decimal __attribute__((__unused__)))
{
  /* This never have to be called */
  assert(0);
  return 0;
}


void Field_num::add_zerofill_and_unsigned(String &res) const
{
  if (unsigned_flag)
    res.append(STRING_WITH_LEN(" unsigned"));
  if (zerofill)
    res.append(STRING_WITH_LEN(" zerofill"));
}


void Field::make_field(Send_field *field)
{
  if (orig_table && orig_table->s->db.str && *orig_table->s->db.str)
  {
    field->db_name= orig_table->s->db.str;
    field->org_table_name= orig_table->s->table_name.str;
  }
  else
    field->org_table_name= field->db_name= "";
  if (orig_table)
  {
    field->table_name= orig_table->alias;
    field->org_col_name= field_name;
  }
  else
  {
    field->table_name= "";
    field->org_col_name= "";
  }
  field->col_name= field_name;
  field->charsetnr= charset()->number;
  field->length=field_length;
  field->type=type();
  field->flags=table->maybe_null ? (flags & ~NOT_NULL_FLAG) : flags;
  field->decimals= 0;
}


/**
  Conversion from decimal to longlong with checking overflow and
  setting correct value (min/max) in case of overflow.

  @param val             value which have to be converted
  @param unsigned_flag   type of integer in which we convert val
  @param err             variable to pass error code

  @return
    value converted from val
*/
longlong Field::convert_decimal2longlong(const my_decimal *val,
                                         bool unsigned_flag, int *err)
{
  longlong i;
  if (unsigned_flag)
  {
    if (val->sign())
    {
      set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_OUT_OF_RANGE, 1);
      i= 0;
      *err= 1;
    }
    else if (warn_if_overflow(my_decimal2int(E_DEC_ERROR &
                                           ~E_DEC_OVERFLOW & ~E_DEC_TRUNCATED,
                                           val, true, &i)))
    {
      i= ~(longlong) 0;
      *err= 1;
    }
  }
  else if (warn_if_overflow(my_decimal2int(E_DEC_ERROR &
                                         ~E_DEC_OVERFLOW & ~E_DEC_TRUNCATED,
                                         val, false, &i)))
  {
    i= (val->sign() ? LONGLONG_MIN : LONGLONG_MAX);
    *err= 1;
  }
  return i;
}


/**
  Storing decimal in integer fields.

  @param val       value for storing

  @note
    This method is used by all integer fields, real/decimal redefine it

  @retval
    0     OK
  @retval
    !=0  error
*/

int Field_num::store_decimal(const my_decimal *val)
{
  int err= 0;
  longlong i= convert_decimal2longlong(val, unsigned_flag, &err);
  return test(err | store(i, unsigned_flag));
}


/**
  Return decimal value of integer field.

  @param decimal_value     buffer for storing decimal value

  @note
    This method is used by all integer fields, real/decimal redefine it.
    All longlong values fit in our decimal buffer which cal store 8*9=72
    digits of integer number

  @return
    pointer to decimal buffer with value of field
*/

my_decimal* Field_num::val_decimal(my_decimal *decimal_value)
{
  assert(result_type() == INT_RESULT);
  longlong nr= val_int();
  int2my_decimal(E_DEC_FATAL_ERROR, nr, unsigned_flag, decimal_value);
  return decimal_value;
}


Field_str::Field_str(uchar *ptr_arg,uint32 len_arg, uchar *null_ptr_arg,
                     uchar null_bit_arg, utype unireg_check_arg,
                     const char *field_name_arg, CHARSET_INFO *charset_arg)
  :Field(ptr_arg, len_arg, null_ptr_arg, null_bit_arg,
         unireg_check_arg, field_name_arg)
{
  field_charset= charset_arg;
  if (charset_arg->state & MY_CS_BINSORT)
    flags|=BINARY_FLAG;
  field_derivation= DERIVATION_IMPLICIT;
}


void Field_num::make_field(Send_field *field)
{
  Field::make_field(field);
  field->decimals= dec;
}

/**
  Decimal representation of Field_str.

  @param d         value for storing

  @note
    Field_str is the base class for fields like Field_enum,
    Field_date and some similar. Some dates use fraction and also
    string value should be converted to floating point value according
    our rules, so we use double to store value of decimal in string.

  @todo
    use decimal2string?

  @retval
    0     OK
  @retval
    !=0  error
*/

int Field_str::store_decimal(const my_decimal *d)
{
  double val;
  /* TODO: use decimal2string? */
  int err= warn_if_overflow(my_decimal2double(E_DEC_FATAL_ERROR &
                                            ~E_DEC_OVERFLOW, d, &val));
  return err | store(val);
}


my_decimal *Field_str::val_decimal(my_decimal *decimal_value)
{
  longlong nr= val_int();
  int2my_decimal(E_DEC_FATAL_ERROR, nr, 0, decimal_value);
  return decimal_value;
}


uint Field::fill_cache_field(CACHE_FIELD *copy)
{
  uint store_length;
  copy->str=ptr;
  copy->length=pack_length();
  copy->blob_field=0;
  if (flags & BLOB_FLAG)
  {
    copy->blob_field=(Field_blob*) this;
    copy->strip=0;
    copy->length-= table->s->blob_ptr_size;
    return copy->length;
  }
  else if (!zero_pack() &&
           (type() == MYSQL_TYPE_STRING && copy->length >= 4 &&
            copy->length < 256))
  {
    copy->strip=1;				/* Remove end space */
    store_length= 2;
  }
  else
  {
    copy->strip=0;
    store_length= 0;
  }
  return copy->length+ store_length;
}


bool Field::get_date(MYSQL_TIME *ltime,uint fuzzydate)
{
  char buff[40];
  String tmp(buff,sizeof(buff),&my_charset_bin),*res;
  if (!(res=val_str(&tmp)) ||
      str_to_datetime_with_warn(res->ptr(), res->length(),
                                ltime, fuzzydate) <= MYSQL_TIMESTAMP_ERROR)
    return 1;
  return 0;
}

bool Field::get_time(MYSQL_TIME *ltime)
{
  char buff[40];
  String tmp(buff,sizeof(buff),&my_charset_bin),*res;
  if (!(res=val_str(&tmp)) ||
      str_to_time_with_warn(res->ptr(), res->length(), ltime))
    return 1;
  return 0;
}

/**
  This is called when storing a date in a string.

  @note
    Needs to be changed if/when we want to support different time formats.
*/

int Field::store_time(MYSQL_TIME *ltime,
                      timestamp_type type_arg __attribute__((__unused__)))
{
  char buff[MAX_DATE_STRING_REP_LENGTH];
  uint length= (uint) my_TIME_to_str(ltime, buff);
  return store(buff, length, &my_charset_bin);
}


bool Field::optimize_range(uint idx, uint part)
{
  return test(table->file->index_flags(idx, part, 1) & HA_READ_RANGE);
}


Field *Field::new_field(MEM_ROOT *root, struct st_table *new_table,
                        bool keep_type __attribute__((unused)))
{
  Field *tmp;
  if (!(tmp= (Field*) memdup_root(root,(char*) this,size_of())))
    return 0;

  if (tmp->table->maybe_null)
    tmp->flags&= ~NOT_NULL_FLAG;
  tmp->table= new_table;
  tmp->key_start.init(0);
  tmp->part_of_key.init(0);
  tmp->part_of_sortkey.init(0);
  tmp->unireg_check= Field::NONE;
  tmp->flags&= (NOT_NULL_FLAG | BLOB_FLAG | UNSIGNED_FLAG |
                ZEROFILL_FLAG | BINARY_FLAG | ENUM_FLAG | SET_FLAG);
  tmp->reset_fields();
  return tmp;
}


Field *Field::new_key_field(MEM_ROOT *root, struct st_table *new_table,
                            uchar *new_ptr, uchar *new_null_ptr,
                            uint new_null_bit)
{
  Field *tmp;
  if ((tmp= new_field(root, new_table, table == new_table)))
  {
    tmp->ptr=      new_ptr;
    tmp->null_ptr= new_null_ptr;
    tmp->null_bit= new_null_bit;
  }
  return tmp;
}


/* This is used to generate a field in TABLE from TABLE_SHARE */

Field *Field::clone(MEM_ROOT *root, struct st_table *new_table)
{
  Field *tmp;
  if ((tmp= (Field*) memdup_root(root,(char*) this,size_of())))
  {
    tmp->init(new_table);
    tmp->move_field_offset((my_ptrdiff_t) (new_table->record[0] -
                                           new_table->s->default_values));
  }
  return tmp;
}


/****************************************************************************
  Field_null, a field that always return NULL
****************************************************************************/

void Field_null::sql_type(String &res) const
{
  res.set_ascii(STRING_WITH_LEN("null"));
}


/****************************************************************************
** Field_new_decimal
****************************************************************************/

Field_new_decimal::Field_new_decimal(uchar *ptr_arg,
                                     uint32 len_arg, uchar *null_ptr_arg,
                                     uchar null_bit_arg,
                                     enum utype unireg_check_arg,
                                     const char *field_name_arg,
                                     uint8 dec_arg,bool zero_arg,
                                     bool unsigned_arg)
  :Field_num(ptr_arg, len_arg, null_ptr_arg, null_bit_arg,
             unireg_check_arg, field_name_arg, dec_arg, zero_arg, unsigned_arg)
{
  precision= my_decimal_length_to_precision(len_arg, dec_arg, unsigned_arg);
  set_if_smaller(precision, DECIMAL_MAX_PRECISION);
  assert((precision <= DECIMAL_MAX_PRECISION) &&
              (dec <= DECIMAL_MAX_SCALE));
  bin_size= my_decimal_get_binary_size(precision, dec);
}


Field_new_decimal::Field_new_decimal(uint32 len_arg,
                                     bool maybe_null_arg,
                                     const char *name,
                                     uint8 dec_arg,
                                     bool unsigned_arg)
  :Field_num((uchar*) 0, len_arg,
             maybe_null_arg ? (uchar*) "": 0, 0,
             NONE, name, dec_arg, 0, unsigned_arg)
{
  precision= my_decimal_length_to_precision(len_arg, dec_arg, unsigned_arg);
  set_if_smaller(precision, DECIMAL_MAX_PRECISION);
  assert((precision <= DECIMAL_MAX_PRECISION) &&
              (dec <= DECIMAL_MAX_SCALE));
  bin_size= my_decimal_get_binary_size(precision, dec);
}


int Field_new_decimal::reset(void)
{
  store_value(&decimal_zero);
  return 0;
}


/**
  Generate max/min decimal value in case of overflow.

  @param decimal_value     buffer for value
  @param sign              sign of value which caused overflow
*/

void Field_new_decimal::set_value_on_overflow(my_decimal *decimal_value,
                                              bool sign)
{
  max_my_decimal(decimal_value, precision, decimals());
  if (sign)
  {
    if (unsigned_flag)
      my_decimal_set_zero(decimal_value);
    else
      decimal_value->sign(true);
  }
  return;
}


/**
  Store decimal value in the binary buffer.

  Checks if decimal_value fits into field size.
  If it does, stores the decimal in the buffer using binary format.
  Otherwise sets maximal number that can be stored in the field.

  @param decimal_value   my_decimal

  @retval
    0 ok
  @retval
    1 error
*/

bool Field_new_decimal::store_value(const my_decimal *decimal_value)
{
  int error= 0;

  /* check that we do not try to write negative value in unsigned field */
  if (unsigned_flag && decimal_value->sign())
  {
    set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_OUT_OF_RANGE, 1);
    error= 1;
    decimal_value= &decimal_zero;
  }

  if (warn_if_overflow(my_decimal2binary(E_DEC_FATAL_ERROR & ~E_DEC_OVERFLOW,
                                         decimal_value, ptr, precision, dec)))
  {
    my_decimal buff;
    set_value_on_overflow(&buff, decimal_value->sign());
    my_decimal2binary(E_DEC_FATAL_ERROR, &buff, ptr, precision, dec);
    error= 1;
  }
  return(error);
}


int Field_new_decimal::store(const char *from, uint length,
                             CHARSET_INFO *charset_arg)
{
  int err;
  my_decimal decimal_value;

  if ((err= str2my_decimal(E_DEC_FATAL_ERROR &
                           ~(E_DEC_OVERFLOW | E_DEC_BAD_NUM),
                           from, length, charset_arg,
                           &decimal_value)) &&
      table->in_use->abort_on_warning)
  {
    /* Because "from" is not NUL-terminated and we use %s in the ER() */
    String from_as_str;
    from_as_str.copy(from, length, &my_charset_bin);

    push_warning_printf(table->in_use, MYSQL_ERROR::WARN_LEVEL_ERROR,
                        ER_TRUNCATED_WRONG_VALUE_FOR_FIELD,
                        ER(ER_TRUNCATED_WRONG_VALUE_FOR_FIELD),
                        "decimal", from_as_str.c_ptr(), field_name,
                        (ulong) table->in_use->row_count);

    return(err);
  }

  switch (err) {
  case E_DEC_TRUNCATED:
    set_warning(MYSQL_ERROR::WARN_LEVEL_NOTE, WARN_DATA_TRUNCATED, 1);
    break;
  case E_DEC_OVERFLOW:
    set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_OUT_OF_RANGE, 1);
    set_value_on_overflow(&decimal_value, decimal_value.sign());
    break;
  case E_DEC_BAD_NUM:
    {
      /* Because "from" is not NUL-terminated and we use %s in the ER() */
      String from_as_str;
      from_as_str.copy(from, length, &my_charset_bin);

    push_warning_printf(table->in_use, MYSQL_ERROR::WARN_LEVEL_WARN,
                        ER_TRUNCATED_WRONG_VALUE_FOR_FIELD,
                        ER(ER_TRUNCATED_WRONG_VALUE_FOR_FIELD),
                          "decimal", from_as_str.c_ptr(), field_name,
                        (ulong) table->in_use->row_count);
    my_decimal_set_zero(&decimal_value);

    break;
    }
  }

  store_value(&decimal_value);
  return(err);
}


/**
  @todo
  Fix following when double2my_decimal when double2decimal
  will return E_DEC_TRUNCATED always correctly
*/

int Field_new_decimal::store(double nr)
{
  my_decimal decimal_value;
  int err;

  err= double2my_decimal(E_DEC_FATAL_ERROR & ~E_DEC_OVERFLOW, nr,
                         &decimal_value);
  if (err)
  {
    if (check_overflow(err))
      set_value_on_overflow(&decimal_value, decimal_value.sign());
    /* Only issue a warning if store_value doesn't issue an warning */
    table->in_use->got_warning= 0;
  }
  if (store_value(&decimal_value))
    err= 1;
  else if (err && !table->in_use->got_warning)
    err= warn_if_overflow(err);
  return(err);
}


int Field_new_decimal::store(longlong nr, bool unsigned_val)
{
  my_decimal decimal_value;
  int err;

  if ((err= int2my_decimal(E_DEC_FATAL_ERROR & ~E_DEC_OVERFLOW,
                           nr, unsigned_val, &decimal_value)))
  {
    if (check_overflow(err))
      set_value_on_overflow(&decimal_value, decimal_value.sign());
    /* Only issue a warning if store_value doesn't issue an warning */
    table->in_use->got_warning= 0;
  }
  if (store_value(&decimal_value))
    err= 1;
  else if (err && !table->in_use->got_warning)
    err= warn_if_overflow(err);
  return err;
}


int Field_new_decimal::store_decimal(const my_decimal *decimal_value)
{
  return store_value(decimal_value);
}


int Field_new_decimal::store_time(MYSQL_TIME *ltime,
                                  timestamp_type t_type __attribute__((__unused__)))
{
    my_decimal decimal_value;
    return store_value(date2my_decimal(ltime, &decimal_value));
}


double Field_new_decimal::val_real(void)
{
  double dbl;
  my_decimal decimal_value;
  my_decimal2double(E_DEC_FATAL_ERROR, val_decimal(&decimal_value), &dbl);
  return dbl;
}


longlong Field_new_decimal::val_int(void)
{
  longlong i;
  my_decimal decimal_value;
  my_decimal2int(E_DEC_FATAL_ERROR, val_decimal(&decimal_value),
                 unsigned_flag, &i);
  return i;
}


my_decimal* Field_new_decimal::val_decimal(my_decimal *decimal_value)
{
  binary2my_decimal(E_DEC_FATAL_ERROR, ptr, decimal_value,
                    precision, dec);
  return(decimal_value);
}


String *Field_new_decimal::val_str(String *val_buffer,
                                   String *val_ptr __attribute__((unused)))
{
  my_decimal decimal_value;
  uint fixed_precision= zerofill ? precision : 0;
  my_decimal2string(E_DEC_FATAL_ERROR, val_decimal(&decimal_value),
                    fixed_precision, dec, '0', val_buffer);
  return val_buffer;
}


int Field_new_decimal::cmp(const uchar *a,const uchar*b)
{
  return memcmp(a, b, bin_size);
}


void Field_new_decimal::sort_string(uchar *buff,
                                    uint length __attribute__((unused)))
{
  memcpy(buff, ptr, bin_size);
}


void Field_new_decimal::sql_type(String &str) const
{
  CHARSET_INFO *cs= str.charset();
  str.length(cs->cset->snprintf(cs, (char*) str.ptr(), str.alloced_length(),
                                "decimal(%d,%d)", precision, (int)dec));
  add_zerofill_and_unsigned(str);
}


/**
   Save the field metadata for new decimal fields.

   Saves the precision in the first byte and decimals() in the second
   byte of the field metadata array at index of *metadata_ptr and 
   *(metadata_ptr + 1).

   @param   metadata_ptr   First byte of field metadata

   @returns number of bytes written to metadata_ptr
*/
int Field_new_decimal::do_save_field_metadata(uchar *metadata_ptr)
{
  *metadata_ptr= precision;
  *(metadata_ptr + 1)= decimals();
  return 2;
}


/**
   Returns the number of bytes field uses in row-based replication 
   row packed size.

   This method is used in row-based replication to determine the number
   of bytes that the field consumes in the row record format. This is
   used to skip fields in the master that do not exist on the slave.

   @param   field_metadata   Encoded size in field metadata

   @returns The size of the field based on the field metadata.
*/
uint Field_new_decimal::pack_length_from_metadata(uint field_metadata)
{
  uint const source_precision= (field_metadata >> 8U) & 0x00ff;
  uint const source_decimal= field_metadata & 0x00ff; 
  uint const source_size= my_decimal_get_binary_size(source_precision, 
                                                     source_decimal);
  return (source_size);
}


/**
   Check to see if field size is compatible with destination.

   This method is used in row-based replication to verify that the slave's
   field size is less than or equal to the master's field size. The 
   encoded field metadata (from the master or source) is decoded and compared
   to the size of this field (the slave or destination). 

   @param   field_metadata   Encoded size in field metadata

   @retval 0 if this field's size is < the source field's size
   @retval 1 if this field's size is >= the source field's size
*/
int Field_new_decimal::compatible_field_size(uint field_metadata)
{
  int compatible= 0;
  uint const source_precision= (field_metadata >> 8U) & 0x00ff;
  uint const source_decimal= field_metadata & 0x00ff; 
  uint const source_size= my_decimal_get_binary_size(source_precision, 
                                                     source_decimal);
  uint const destination_size= row_pack_length();
  compatible= (source_size <= destination_size);
  if (compatible)
    compatible= (source_precision <= precision) &&
                (source_decimal <= decimals());
  return (compatible);
}


uint Field_new_decimal::is_equal(Create_field *new_field)
{
  return ((new_field->sql_type == real_type()) &&
          ((new_field->flags & UNSIGNED_FLAG) == 
           (uint) (flags & UNSIGNED_FLAG)) &&
          ((new_field->flags & AUTO_INCREMENT_FLAG) ==
           (uint) (flags & AUTO_INCREMENT_FLAG)) &&
          (new_field->length == max_display_length()) &&
          (new_field->decimals == dec));
}


/**
   Unpack a decimal field from row data.

   This method is used to unpack a decimal or numeric field from a master
   whose size of the field is less than that of the slave.
  
   @param   to         Destination of the data
   @param   from       Source of the data
   @param   param_data Precision (upper) and decimal (lower) values

   @return  New pointer into memory based on from + length of the data
*/
const uchar *
Field_new_decimal::unpack(uchar* to,
                          const uchar *from,
                          uint param_data,
                          bool low_byte_first)
{
  if (param_data == 0)
    return Field::unpack(to, from, param_data, low_byte_first);

  uint from_precision= (param_data & 0xff00) >> 8U;
  uint from_decimal= param_data & 0x00ff;
  uint length=pack_length();
  uint from_pack_len= my_decimal_get_binary_size(from_precision, from_decimal);
  uint len= (param_data && (from_pack_len < length)) ?
            from_pack_len : length;
  if ((from_pack_len && (from_pack_len < length)) ||
      (from_precision < precision) ||
      (from_decimal < decimals()))
  {
    /*
      If the master's data is smaller than the slave, we need to convert
      the binary to decimal then resize the decimal converting it back to
      a decimal and write that to the raw data buffer.
    */
    decimal_digit_t dec_buf[DECIMAL_MAX_PRECISION];
    decimal_t dec;
    dec.len= from_precision;
    dec.buf= dec_buf;
    /*
      Note: bin2decimal does not change the length of the field. So it is
      just the first step the resizing operation. The second step does the
      resizing using the precision and decimals from the slave.
    */
    bin2decimal((uchar *)from, &dec, from_precision, from_decimal);
    decimal2bin(&dec, to, precision, decimals());
  }
  else
    memcpy(to, from, len); // Sizes are the same, just copy the data.
  return from+len;
}

/****************************************************************************
** tiny int
****************************************************************************/

int Field_tiny::store(const char *from,uint len,CHARSET_INFO *cs)
{
  int error;
  longlong rnd;
  
  error= get_int(cs, from, len, &rnd, 255, -128, 127);
  ptr[0]= unsigned_flag ? (char) (uint64_t) rnd : (char) rnd;
  return error;
}


int Field_tiny::store(double nr)
{
  int error= 0;
  nr=rint(nr);
  if (unsigned_flag)
  {
    if (nr < 0.0)
    {
      *ptr=0;
      set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_OUT_OF_RANGE, 1);
      error= 1;
    }
    else if (nr > 255.0)
    {
      *ptr=(char) 255;
      set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_OUT_OF_RANGE, 1);
      error= 1;
    }
    else
      *ptr=(char) nr;
  }
  else
  {
    if (nr < -128.0)
    {
      *ptr= (char) -128;
      set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_OUT_OF_RANGE, 1);
      error= 1;
    }
    else if (nr > 127.0)
    {
      *ptr=127;
      set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_OUT_OF_RANGE, 1);
      error= 1;
    }
    else
      *ptr=(char) (int) nr;
  }
  return error;
}


int Field_tiny::store(longlong nr, bool unsigned_val)
{
  int error= 0;

  if (unsigned_flag)
  {
    if (nr < 0 && !unsigned_val)
    {
      *ptr= 0;
      set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_OUT_OF_RANGE, 1);
      error= 1;
    }
    else if ((uint64_t) nr > (uint64_t) 255)
    {
      *ptr= (char) 255;
      set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_OUT_OF_RANGE, 1);
      error= 1;
    }
    else
      *ptr=(char) nr;
  }
  else
  {
    if (nr < 0 && unsigned_val)
      nr= 256;                                    // Generate overflow
    if (nr < -128)
    {
      *ptr= (char) -128;
      set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_OUT_OF_RANGE, 1);
      error= 1;
    }
    else if (nr > 127)
    {
      *ptr=127;
      set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_OUT_OF_RANGE, 1);
      error= 1;
    }
    else
      *ptr=(char) nr;
  }
  return error;
}


double Field_tiny::val_real(void)
{
  int tmp= unsigned_flag ? (int) ptr[0] :
    (int) ((signed char*) ptr)[0];
  return (double) tmp;
}


longlong Field_tiny::val_int(void)
{
  int tmp= unsigned_flag ? (int) ptr[0] :
    (int) ((signed char*) ptr)[0];
  return (longlong) tmp;
}


String *Field_tiny::val_str(String *val_buffer,
			    String *val_ptr __attribute__((unused)))
{
  CHARSET_INFO *cs= &my_charset_bin;
  uint length;
  uint mlength=max(field_length+1,5*cs->mbmaxlen);
  val_buffer->alloc(mlength);
  char *to=(char*) val_buffer->ptr();

  if (unsigned_flag)
    length= (uint) cs->cset->long10_to_str(cs,to,mlength, 10,
					   (long) *ptr);
  else
    length= (uint) cs->cset->long10_to_str(cs,to,mlength,-10,
					   (long) *((signed char*) ptr));
  
  val_buffer->length(length);
  if (zerofill)
    prepend_zeros(val_buffer);
  return val_buffer;
}

bool Field_tiny::send_binary(Protocol *protocol)
{
  return protocol->store_tiny((longlong) (int8) ptr[0]);
}

int Field_tiny::cmp(const uchar *a_ptr, const uchar *b_ptr)
{
  signed char a,b;
  a=(signed char) a_ptr[0]; b= (signed char) b_ptr[0];
  if (unsigned_flag)
    return ((uchar) a < (uchar) b) ? -1 : ((uchar) a > (uchar) b) ? 1 : 0;
  return (a < b) ? -1 : (a > b) ? 1 : 0;
}

void Field_tiny::sort_string(uchar *to,uint length __attribute__((unused)))
{
  if (unsigned_flag)
    *to= *ptr;
  else
    to[0] = (char) (ptr[0] ^ (uchar) 128);	/* Revers signbit */
}

void Field_tiny::sql_type(String &res) const
{
  CHARSET_INFO *cs=res.charset();
  res.length(cs->cset->snprintf(cs,(char*) res.ptr(),res.alloced_length(),
			  "tinyint(%d)",(int) field_length));
  add_zerofill_and_unsigned(res);
}

/****************************************************************************
 Field type short int (2 byte)
****************************************************************************/

int Field_short::store(const char *from,uint len,CHARSET_INFO *cs)
{
  int store_tmp;
  int error;
  longlong rnd;
  
  error= get_int(cs, from, len, &rnd, UINT_MAX16, INT_MIN16, INT_MAX16);
  store_tmp= unsigned_flag ? (int) (uint64_t) rnd : (int) rnd;
#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
  {
    int2store(ptr, store_tmp);
  }
  else
#endif
    shortstore(ptr, (short) store_tmp);
  return error;
}


int Field_short::store(double nr)
{
  int error= 0;
  int16 res;
  nr=rint(nr);
  if (unsigned_flag)
  {
    if (nr < 0)
    {
      res=0;
      set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_OUT_OF_RANGE, 1);
      error= 1;
    }
    else if (nr > (double) UINT_MAX16)
    {
      res=(int16) UINT_MAX16;
      set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_OUT_OF_RANGE, 1);
      error= 1;
    }
    else
      res=(int16) (uint16) nr;
  }
  else
  {
    if (nr < (double) INT_MIN16)
    {
      res=INT_MIN16;
      set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_OUT_OF_RANGE, 1);
      error= 1;
    }
    else if (nr > (double) INT_MAX16)
    {
      res=INT_MAX16;
      set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_OUT_OF_RANGE, 1);
      error= 1;
    }
    else
      res=(int16) (int) nr;
  }
#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
  {
    int2store(ptr,res);
  }
  else
#endif
    shortstore(ptr,res);
  return error;
}


int Field_short::store(longlong nr, bool unsigned_val)
{
  int error= 0;
  int16 res;

  if (unsigned_flag)
  {
    if (nr < 0L && !unsigned_val)
    {
      res=0;
      set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_OUT_OF_RANGE, 1);
      error= 1;
    }
    else if ((uint64_t) nr > (uint64_t) UINT_MAX16)
    {
      res=(int16) UINT_MAX16;
      set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_OUT_OF_RANGE, 1);
      error= 1;
    }
    else
      res=(int16) (uint16) nr;
  }
  else
  {
    if (nr < 0 && unsigned_val)
      nr= UINT_MAX16+1;                         // Generate overflow

    if (nr < INT_MIN16)
    {
      res=INT_MIN16;
      set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_OUT_OF_RANGE, 1);
      error= 1;
    }
    else if (nr > (longlong) INT_MAX16)
    {
      res=INT_MAX16;
      set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_OUT_OF_RANGE, 1);
      error= 1;
    }
    else
      res=(int16) nr;
  }
#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
  {
    int2store(ptr,res);
  }
  else
#endif
    shortstore(ptr,res);
  return error;
}


double Field_short::val_real(void)
{
  short j;
#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
    j=sint2korr(ptr);
  else
#endif
    shortget(j,ptr);
  return unsigned_flag ? (double) (unsigned short) j : (double) j;
}

longlong Field_short::val_int(void)
{
  short j;
#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
    j=sint2korr(ptr);
  else
#endif
    shortget(j,ptr);
  return unsigned_flag ? (longlong) (unsigned short) j : (longlong) j;
}


String *Field_short::val_str(String *val_buffer,
			     String *val_ptr __attribute__((unused)))
{
  CHARSET_INFO *cs= &my_charset_bin;
  uint length;
  uint mlength=max(field_length+1,7*cs->mbmaxlen);
  val_buffer->alloc(mlength);
  char *to=(char*) val_buffer->ptr();
  short j;
#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
    j=sint2korr(ptr);
  else
#endif
    shortget(j,ptr);

  if (unsigned_flag)
    length=(uint) cs->cset->long10_to_str(cs, to, mlength, 10, 
					  (long) (uint16) j);
  else
    length=(uint) cs->cset->long10_to_str(cs, to, mlength,-10, (long) j);
  val_buffer->length(length);
  if (zerofill)
    prepend_zeros(val_buffer);
  return val_buffer;
}


bool Field_short::send_binary(Protocol *protocol)
{
  return protocol->store_short(Field_short::val_int());
}


int Field_short::cmp(const uchar *a_ptr, const uchar *b_ptr)
{
  short a,b;
#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
  {
    a=sint2korr(a_ptr);
    b=sint2korr(b_ptr);
  }
  else
#endif
  {
    shortget(a,a_ptr);
    shortget(b,b_ptr);
  }

  if (unsigned_flag)
    return ((unsigned short) a < (unsigned short) b) ? -1 :
    ((unsigned short) a > (unsigned short) b) ? 1 : 0;
  return (a < b) ? -1 : (a > b) ? 1 : 0;
}

void Field_short::sort_string(uchar *to,uint length __attribute__((unused)))
{
#ifdef WORDS_BIGENDIAN
  if (!table->s->db_low_byte_first)
  {
    if (unsigned_flag)
      to[0] = ptr[0];
    else
      to[0] = (char) (ptr[0] ^ 128);		/* Revers signbit */
    to[1]   = ptr[1];
  }
  else
#endif
  {
    if (unsigned_flag)
      to[0] = ptr[1];
    else
      to[0] = (char) (ptr[1] ^ 128);		/* Revers signbit */
    to[1]   = ptr[0];
  }
}

void Field_short::sql_type(String &res) const
{
  CHARSET_INFO *cs=res.charset();
  res.length(cs->cset->snprintf(cs,(char*) res.ptr(),res.alloced_length(),
			  "smallint(%d)",(int) field_length));
  add_zerofill_and_unsigned(res);
}


/****************************************************************************
** long int
****************************************************************************/

int Field_long::store(const char *from,uint len,CHARSET_INFO *cs)
{
  long store_tmp;
  int error;
  longlong rnd;
  
  error= get_int(cs, from, len, &rnd, UINT_MAX32, INT_MIN32, INT_MAX32);
  store_tmp= unsigned_flag ? (long) (uint64_t) rnd : (long) rnd;
#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
  {
    int4store(ptr, store_tmp);
  }
  else
#endif
    longstore(ptr, store_tmp);
  return error;
}


int Field_long::store(double nr)
{
  int error= 0;
  int32 res;
  nr=rint(nr);
  if (unsigned_flag)
  {
    if (nr < 0)
    {
      res=0;
      error= 1;
    }
    else if (nr > (double) UINT_MAX32)
    {
      res= INT_MAX32;
      set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_OUT_OF_RANGE, 1);
      error= 1;
    }
    else
      res=(int32) (ulong) nr;
  }
  else
  {
    if (nr < (double) INT_MIN32)
    {
      res=(int32) INT_MIN32;
      error= 1;
    }
    else if (nr > (double) INT_MAX32)
    {
      res=(int32) INT_MAX32;
      error= 1;
    }
    else
      res=(int32) (longlong) nr;
  }
  if (error)
    set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_OUT_OF_RANGE, 1);

#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
  {
    int4store(ptr,res);
  }
  else
#endif
    longstore(ptr,res);
  return error;
}


int Field_long::store(longlong nr, bool unsigned_val)
{
  int error= 0;
  int32 res;

  if (unsigned_flag)
  {
    if (nr < 0 && !unsigned_val)
    {
      res=0;
      error= 1;
    }
    else if ((uint64_t) nr >= (1LL << 32))
    {
      res=(int32) (uint32) ~0L;
      error= 1;
    }
    else
      res=(int32) (uint32) nr;
  }
  else
  {
    if (nr < 0 && unsigned_val)
      nr= ((longlong) INT_MAX32) + 1;           // Generate overflow
    if (nr < (longlong) INT_MIN32) 
    {
      res=(int32) INT_MIN32;
      error= 1;
    }
    else if (nr > (longlong) INT_MAX32)
    {
      res=(int32) INT_MAX32;
      error= 1;
    }
    else
      res=(int32) nr;
  }
  if (error)
    set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_OUT_OF_RANGE, 1);

#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
  {
    int4store(ptr,res);
  }
  else
#endif
    longstore(ptr,res);
  return error;
}


double Field_long::val_real(void)
{
  int32 j;
#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
    j=sint4korr(ptr);
  else
#endif
    longget(j,ptr);
  return unsigned_flag ? (double) (uint32) j : (double) j;
}

longlong Field_long::val_int(void)
{
  int32 j;
  /* See the comment in Field_long::store(long long) */
  assert(table->in_use == current_thd);
#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
    j=sint4korr(ptr);
  else
#endif
    longget(j,ptr);
  return unsigned_flag ? (longlong) (uint32) j : (longlong) j;
}

String *Field_long::val_str(String *val_buffer,
			    String *val_ptr __attribute__((unused)))
{
  CHARSET_INFO *cs= &my_charset_bin;
  uint length;
  uint mlength=max(field_length+1,12*cs->mbmaxlen);
  val_buffer->alloc(mlength);
  char *to=(char*) val_buffer->ptr();
  int32 j;
#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
    j=sint4korr(ptr);
  else
#endif
    longget(j,ptr);

  if (unsigned_flag)
    length=cs->cset->long10_to_str(cs,to,mlength, 10,(long) (uint32)j);
  else
    length=cs->cset->long10_to_str(cs,to,mlength,-10,(long) j);
  val_buffer->length(length);
  if (zerofill)
    prepend_zeros(val_buffer);
  return val_buffer;
}


bool Field_long::send_binary(Protocol *protocol)
{
  return protocol->store_long(Field_long::val_int());
}

int Field_long::cmp(const uchar *a_ptr, const uchar *b_ptr)
{
  int32 a,b;
#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
  {
    a=sint4korr(a_ptr);
    b=sint4korr(b_ptr);
  }
  else
#endif
  {
    longget(a,a_ptr);
    longget(b,b_ptr);
  }
  if (unsigned_flag)
    return ((uint32) a < (uint32) b) ? -1 : ((uint32) a > (uint32) b) ? 1 : 0;
  return (a < b) ? -1 : (a > b) ? 1 : 0;
}

void Field_long::sort_string(uchar *to,uint length __attribute__((unused)))
{
#ifdef WORDS_BIGENDIAN
  if (!table->s->db_low_byte_first)
  {
    if (unsigned_flag)
      to[0] = ptr[0];
    else
      to[0] = (char) (ptr[0] ^ 128);		/* Revers signbit */
    to[1]   = ptr[1];
    to[2]   = ptr[2];
    to[3]   = ptr[3];
  }
  else
#endif
  {
    if (unsigned_flag)
      to[0] = ptr[3];
    else
      to[0] = (char) (ptr[3] ^ 128);		/* Revers signbit */
    to[1]   = ptr[2];
    to[2]   = ptr[1];
    to[3]   = ptr[0];
  }
}


void Field_long::sql_type(String &res) const
{
  CHARSET_INFO *cs=res.charset();
  res.length(cs->cset->snprintf(cs,(char*) res.ptr(),res.alloced_length(),
			  "int(%d)",(int) field_length));
  add_zerofill_and_unsigned(res);
}

/****************************************************************************
 Field type longlong int (8 bytes)
****************************************************************************/

int Field_longlong::store(const char *from,uint len,CHARSET_INFO *cs)
{
  int error= 0;
  char *end;
  uint64_t tmp;

  tmp= cs->cset->strntoull10rnd(cs,from,len,unsigned_flag,&end,&error);
  if (error == MY_ERRNO_ERANGE)
  {
    set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_OUT_OF_RANGE, 1);
    error= 1;
  }
  else if (table->in_use->count_cuted_fields && 
           check_int(cs, from, len, end, error))
    error= 1;
  else
    error= 0;
#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
  {
    int8store(ptr,tmp);
  }
  else
#endif
    longlongstore(ptr,tmp);
  return error;
}


int Field_longlong::store(double nr)
{
  int error= 0;
  longlong res;

  nr= rint(nr);
  if (unsigned_flag)
  {
    if (nr < 0)
    {
      res=0;
      error= 1;
    }
    else if (nr >= (double) ULONGLONG_MAX)
    {
      res= ~(longlong) 0;
      error= 1;
    }
    else
      res=(longlong) (uint64_t) nr;
  }
  else
  {
    if (nr <= (double) LONGLONG_MIN)
    {
      res= LONGLONG_MIN;
      error= (nr < (double) LONGLONG_MIN);
    }
    else if (nr >= (double) (uint64_t) LONGLONG_MAX)
    {
      res= LONGLONG_MAX;
      error= (nr > (double) LONGLONG_MAX);
    }
    else
      res=(longlong) nr;
  }
  if (error)
    set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_OUT_OF_RANGE, 1);

#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
  {
    int8store(ptr,res);
  }
  else
#endif
    longlongstore(ptr,res);
  return error;
}


int Field_longlong::store(longlong nr, bool unsigned_val)
{
  int error= 0;

  if (nr < 0)                                   // Only possible error
  {
    /*
      if field is unsigned and value is signed (< 0) or
      if field is signed and value is unsigned we have an overflow
    */
    if (unsigned_flag != unsigned_val)
    {
      nr= unsigned_flag ? (uint64_t) 0 : (uint64_t) LONGLONG_MAX;
      set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_OUT_OF_RANGE, 1);
      error= 1;
    }
  }

#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
  {
    int8store(ptr,nr);
  }
  else
#endif
    longlongstore(ptr,nr);
  return error;
}


double Field_longlong::val_real(void)
{
  longlong j;
#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
  {
    j=sint8korr(ptr);
  }
  else
#endif
    longlongget(j,ptr);
  /* The following is open coded to avoid a bug in gcc 3.3 */
  if (unsigned_flag)
  {
    uint64_t tmp= (uint64_t) j;
    return uint64_t2double(tmp);
  }
  return (double) j;
}


longlong Field_longlong::val_int(void)
{
  longlong j;
#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
    j=sint8korr(ptr);
  else
#endif
    longlongget(j,ptr);
  return j;
}


String *Field_longlong::val_str(String *val_buffer,
				String *val_ptr __attribute__((unused)))
{
  CHARSET_INFO *cs= &my_charset_bin;
  uint length;
  uint mlength=max(field_length+1,22*cs->mbmaxlen);
  val_buffer->alloc(mlength);
  char *to=(char*) val_buffer->ptr();
  longlong j;
#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
    j=sint8korr(ptr);
  else
#endif
    longlongget(j,ptr);

  length=(uint) (cs->cset->longlong10_to_str)(cs,to,mlength,
					unsigned_flag ? 10 : -10, j);
  val_buffer->length(length);
  if (zerofill)
    prepend_zeros(val_buffer);
  return val_buffer;
}


bool Field_longlong::send_binary(Protocol *protocol)
{
  return protocol->store_longlong(Field_longlong::val_int(), unsigned_flag);
}


int Field_longlong::cmp(const uchar *a_ptr, const uchar *b_ptr)
{
  longlong a,b;
#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
  {
    a=sint8korr(a_ptr);
    b=sint8korr(b_ptr);
  }
  else
#endif
  {
    longlongget(a,a_ptr);
    longlongget(b,b_ptr);
  }
  if (unsigned_flag)
    return ((uint64_t) a < (uint64_t) b) ? -1 :
    ((uint64_t) a > (uint64_t) b) ? 1 : 0;
  return (a < b) ? -1 : (a > b) ? 1 : 0;
}

void Field_longlong::sort_string(uchar *to,uint length __attribute__((unused)))
{
#ifdef WORDS_BIGENDIAN
  if (!table->s->db_low_byte_first)
  {
    if (unsigned_flag)
      to[0] = ptr[0];
    else
      to[0] = (char) (ptr[0] ^ 128);		/* Revers signbit */
    to[1]   = ptr[1];
    to[2]   = ptr[2];
    to[3]   = ptr[3];
    to[4]   = ptr[4];
    to[5]   = ptr[5];
    to[6]   = ptr[6];
    to[7]   = ptr[7];
  }
  else
#endif
  {
    if (unsigned_flag)
      to[0] = ptr[7];
    else
      to[0] = (char) (ptr[7] ^ 128);		/* Revers signbit */
    to[1]   = ptr[6];
    to[2]   = ptr[5];
    to[3]   = ptr[4];
    to[4]   = ptr[3];
    to[5]   = ptr[2];
    to[6]   = ptr[1];
    to[7]   = ptr[0];
  }
}


void Field_longlong::sql_type(String &res) const
{
  CHARSET_INFO *cs=res.charset();
  res.length(cs->cset->snprintf(cs,(char*) res.ptr(),res.alloced_length(),
			  "bigint(%d)",(int) field_length));
  add_zerofill_and_unsigned(res);
}


/*
  Floating-point numbers
 */

uchar *
Field_real::pack(uchar *to, const uchar *from,
                 uint max_length, bool low_byte_first)
{
  assert(max_length >= pack_length());
#ifdef WORDS_BIGENDIAN
  if (low_byte_first != table->s->db_low_byte_first)
  {
    const uchar *dptr= from + pack_length();
    while (dptr-- > from)
      *to++ = *dptr;
    return(to);
  }
  else
#endif
    return(Field::pack(to, from, max_length, low_byte_first));
}

const uchar *
Field_real::unpack(uchar *to, const uchar *from,
                   uint param_data, bool low_byte_first)
{
#ifdef WORDS_BIGENDIAN
  if (low_byte_first != table->s->db_low_byte_first)
  {
    const uchar *dptr= from + pack_length();
    while (dptr-- > from)
      *to++ = *dptr;
    return(from + pack_length());
  }
  else
#endif
    return(Field::unpack(to, from, param_data, low_byte_first));
}

/****************************************************************************
  single precision float
****************************************************************************/

int Field_float::store(const char *from,uint len,CHARSET_INFO *cs)
{
  int error;
  char *end;
  double nr= my_strntod(cs,(char*) from,len,&end,&error);
  if (error || (!len || (((uint)(end-from) != len) && table->in_use->count_cuted_fields)))
  {
    set_warning(MYSQL_ERROR::WARN_LEVEL_WARN,
                (error ? ER_WARN_DATA_OUT_OF_RANGE : WARN_DATA_TRUNCATED), 1);
    error= error ? 1 : 2;
  }
  Field_float::store(nr);
  return error;
}


int Field_float::store(double nr)
{
  int error= truncate(&nr, FLT_MAX);
  float j= (float)nr;

#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
  {
    float4store(ptr,j);
  }
  else
#endif
    memcpy_fixed(ptr,(uchar*) &j,sizeof(j));
  return error;
}


int Field_float::store(longlong nr, bool unsigned_val)
{
  return Field_float::store(unsigned_val ? uint64_t2double((uint64_t) nr) :
                            (double) nr);
}


double Field_float::val_real(void)
{
  float j;
#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
  {
    float4get(j,ptr);
  }
  else
#endif
    memcpy_fixed((uchar*) &j,ptr,sizeof(j));
  return ((double) j);
}

longlong Field_float::val_int(void)
{
  float j;
#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
  {
    float4get(j,ptr);
  }
  else
#endif
    memcpy_fixed((uchar*) &j,ptr,sizeof(j));
  return (longlong) rint(j);
}


String *Field_float::val_str(String *val_buffer,
			     String *val_ptr __attribute__((unused)))
{
  float nr;
#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
  {
    float4get(nr,ptr);
  }
  else
#endif
    memcpy_fixed((uchar*) &nr,ptr,sizeof(nr));

  uint to_length=max(field_length,70);
  val_buffer->alloc(to_length);
  char *to=(char*) val_buffer->ptr();
  size_t len;

  if (dec >= NOT_FIXED_DEC)
    len= my_gcvt(nr, MY_GCVT_ARG_FLOAT, to_length - 1, to, NULL);
  else
  {
    /*
      We are safe here because the buffer length is >= 70, and
      fabs(float) < 10^39, dec < NOT_FIXED_DEC. So the resulting string
      will be not longer than 69 chars + terminating '\0'.
    */
    len= my_fcvt(nr, dec, to, NULL);
  }
  val_buffer->length((uint) len);
  if (zerofill)
    prepend_zeros(val_buffer);
  return val_buffer;
}


int Field_float::cmp(const uchar *a_ptr, const uchar *b_ptr)
{
  float a,b;
#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
  {
    float4get(a,a_ptr);
    float4get(b,b_ptr);
  }
  else
#endif
  {
    memcpy_fixed(&a,a_ptr,sizeof(float));
    memcpy_fixed(&b,b_ptr,sizeof(float));
  }
  return (a < b) ? -1 : (a > b) ? 1 : 0;
}

#define FLT_EXP_DIG (sizeof(float)*8-FLT_MANT_DIG)

void Field_float::sort_string(uchar *to,uint length __attribute__((unused)))
{
  float nr;
#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
  {
    float4get(nr,ptr);
  }
  else
#endif
    memcpy_fixed(&nr,ptr,sizeof(float));

  uchar *tmp= to;
  if (nr == (float) 0.0)
  {						/* Change to zero string */
    tmp[0]=(uchar) 128;
    bzero((char*) tmp+1,sizeof(nr)-1);
  }
  else
  {
#ifdef WORDS_BIGENDIAN
    memcpy_fixed(tmp,&nr,sizeof(nr));
#else
    tmp[0]= ptr[3]; tmp[1]=ptr[2]; tmp[2]= ptr[1]; tmp[3]=ptr[0];
#endif
    if (tmp[0] & 128)				/* Negative */
    {						/* make complement */
      uint i;
      for (i=0 ; i < sizeof(nr); i++)
	tmp[i]= (uchar) (tmp[i] ^ (uchar) 255);
    }
    else
    {
      ushort exp_part=(((ushort) tmp[0] << 8) | (ushort) tmp[1] |
		       (ushort) 32768);
      exp_part+= (ushort) 1 << (16-1-FLT_EXP_DIG);
      tmp[0]= (uchar) (exp_part >> 8);
      tmp[1]= (uchar) exp_part;
    }
  }
}


bool Field_float::send_binary(Protocol *protocol)
{
  return protocol->store((float) Field_float::val_real(), dec, (String*) 0);
}


/**
   Save the field metadata for float fields.

   Saves the pack length in the first byte.

   @param   metadata_ptr   First byte of field metadata

   @returns number of bytes written to metadata_ptr
*/
int Field_float::do_save_field_metadata(uchar *metadata_ptr)
{
  *metadata_ptr= pack_length();
  return 1;
}


void Field_float::sql_type(String &res) const
{
  if (dec == NOT_FIXED_DEC)
  {
    res.set_ascii(STRING_WITH_LEN("float"));
  }
  else
  {
    CHARSET_INFO *cs= res.charset();
    res.length(cs->cset->snprintf(cs,(char*) res.ptr(),res.alloced_length(),
			    "float(%d,%d)",(int) field_length,dec));
  }
  add_zerofill_and_unsigned(res);
}


/****************************************************************************
  double precision floating point numbers
****************************************************************************/

int Field_double::store(const char *from,uint len,CHARSET_INFO *cs)
{
  int error;
  char *end;
  double nr= my_strntod(cs,(char*) from, len, &end, &error);
  if (error || (!len || (((uint) (end-from) != len) && table->in_use->count_cuted_fields)))
  {
    set_warning(MYSQL_ERROR::WARN_LEVEL_WARN,
                (error ? ER_WARN_DATA_OUT_OF_RANGE : WARN_DATA_TRUNCATED), 1);
    error= error ? 1 : 2;
  }
  Field_double::store(nr);
  return error;
}


int Field_double::store(double nr)
{
  int error= truncate(&nr, DBL_MAX);

#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
  {
    float8store(ptr,nr);
  }
  else
#endif
    doublestore(ptr,nr);
  return error;
}


int Field_double::store(longlong nr, bool unsigned_val)
{
  return Field_double::store(unsigned_val ? uint64_t2double((uint64_t) nr) :
                             (double) nr);
}

/*
  If a field has fixed length, truncate the double argument pointed to by 'nr'
  appropriately.
  Also ensure that the argument is within [-max_value; max_value] range.
*/

int Field_real::truncate(double *nr, double max_value)
{
  int error= 1;
  double res= *nr;
  
  if (isnan(res))
  {
    res= 0;
    set_null();
    set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_OUT_OF_RANGE, 1);
    goto end;
  }
  else if (unsigned_flag && res < 0)
  {
    res= 0;
    set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_OUT_OF_RANGE, 1);
    goto end;
  }

  if (!not_fixed)
  {
    uint order= field_length - dec;
    uint step= array_elements(log_10) - 1;
    max_value= 1.0;
    for (; order > step; order-= step)
      max_value*= log_10[step];
    max_value*= log_10[order];
    max_value-= 1.0 / log_10[dec];

    /* Check for infinity so we don't get NaN in calculations */
    if (!my_isinf(res))
    {
      double tmp= rint((res - floor(res)) * log_10[dec]) / log_10[dec];
      res= floor(res) + tmp;
    }
  }
  
  if (res < -max_value)
  {
   res= -max_value;
   set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_OUT_OF_RANGE, 1);
  }
  else if (res > max_value)
  {
    res= max_value;
    set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_OUT_OF_RANGE, 1);
  }
  else
    error= 0;

end:
  *nr= res;
  return error;
}


int Field_real::store_decimal(const my_decimal *dm)
{
  double dbl;
  my_decimal2double(E_DEC_FATAL_ERROR, dm, &dbl);
  return store(dbl);
}

double Field_double::val_real(void)
{
  double j;
#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
  {
    float8get(j,ptr);
  }
  else
#endif
    doubleget(j,ptr);
  return j;
}

longlong Field_double::val_int(void)
{
  double j;
  longlong res;
#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
  {
    float8get(j,ptr);
  }
  else
#endif
    doubleget(j,ptr);
  /* Check whether we fit into longlong range */
  if (j <= (double) LONGLONG_MIN)
  {
    res= (longlong) LONGLONG_MIN;
    goto warn;
  }
  if (j >= (double) (uint64_t) LONGLONG_MAX)
  {
    res= (longlong) LONGLONG_MAX;
    goto warn;
  }
  return (longlong) rint(j);

warn:
  {
    char buf[DOUBLE_TO_STRING_CONVERSION_BUFFER_SIZE];
    String tmp(buf, sizeof(buf), &my_charset_latin1), *str;
    str= val_str(&tmp, 0);
    push_warning_printf(current_thd, MYSQL_ERROR::WARN_LEVEL_WARN,
                        ER_TRUNCATED_WRONG_VALUE,
                        ER(ER_TRUNCATED_WRONG_VALUE), "INTEGER",
                        str->c_ptr());
  }
  return res;
}


my_decimal *Field_real::val_decimal(my_decimal *decimal_value)
{
  double2my_decimal(E_DEC_FATAL_ERROR, val_real(), decimal_value);
  return decimal_value;
}


String *Field_double::val_str(String *val_buffer,
			      String *val_ptr __attribute__((unused)))
{
  double nr;
#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
  {
    float8get(nr,ptr);
  }
  else
#endif
    doubleget(nr,ptr);

  uint to_length=max(field_length, DOUBLE_TO_STRING_CONVERSION_BUFFER_SIZE);
  val_buffer->alloc(to_length);
  char *to=(char*) val_buffer->ptr();
  size_t len;

  if (dec >= NOT_FIXED_DEC)
    len= my_gcvt(nr, MY_GCVT_ARG_DOUBLE, to_length - 1, to, NULL);
  else
    len= my_fcvt(nr, dec, to, NULL);

  val_buffer->length((uint) len);
  if (zerofill)
    prepend_zeros(val_buffer);
  return val_buffer;
}

bool Field_double::send_binary(Protocol *protocol)
{
  return protocol->store((double) Field_double::val_real(), dec, (String*) 0);
}


int Field_double::cmp(const uchar *a_ptr, const uchar *b_ptr)
{
  double a,b;
#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
  {
    float8get(a,a_ptr);
    float8get(b,b_ptr);
  }
  else
#endif
  {
    doubleget(a, a_ptr);
    doubleget(b, b_ptr);
  }
  return (a < b) ? -1 : (a > b) ? 1 : 0;
}


#define DBL_EXP_DIG (sizeof(double)*8-DBL_MANT_DIG)

/* The following should work for IEEE */

void Field_double::sort_string(uchar *to,uint length __attribute__((unused)))
{
  double nr;
#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
  {
    float8get(nr,ptr);
  }
  else
#endif
    doubleget(nr,ptr);
  change_double_for_sort(nr, to);
}


/**
   Save the field metadata for double fields.

   Saves the pack length in the first byte of the field metadata array
   at index of *metadata_ptr.

   @param   metadata_ptr   First byte of field metadata

   @returns number of bytes written to metadata_ptr
*/
int Field_double::do_save_field_metadata(uchar *metadata_ptr)
{
  *metadata_ptr= pack_length();
  return 1;
}


void Field_double::sql_type(String &res) const
{
  CHARSET_INFO *cs=res.charset();
  if (dec == NOT_FIXED_DEC)
  {
    res.set_ascii(STRING_WITH_LEN("double"));
  }
  else
  {
    res.length(cs->cset->snprintf(cs,(char*) res.ptr(),res.alloced_length(),
			    "double(%d,%d)",(int) field_length,dec));
  }
  add_zerofill_and_unsigned(res);
}


/**
  TIMESTAMP type holds datetime values in range from 1970-01-01 00:00:01 UTC to 
  2038-01-01 00:00:00 UTC stored as number of seconds since Unix 
  Epoch in UTC.
  
  Up to one of timestamps columns in the table can be automatically 
  set on row update and/or have NOW() as default value.
  TABLE::timestamp_field points to Field object for such timestamp with 
  auto-set-on-update. TABLE::time_stamp holds offset in record + 1 for this
  field, and is used by handler code which performs updates required.
  
  Actually SQL-99 says that we should allow niladic functions (like NOW())
  as defaults for any field. Current limitations (only NOW() and only 
  for one TIMESTAMP field) are because of restricted binary .frm format 
  and should go away in the future.
  
  Also because of this limitation of binary .frm format we use 5 different
  unireg_check values with TIMESTAMP field to distinguish various cases of
  DEFAULT or ON UPDATE values. These values are:
  
  TIMESTAMP_OLD_FIELD - old timestamp, if there was not any fields with
    auto-set-on-update (or now() as default) in this table before, then this 
    field has NOW() as default and is updated when row changes, else it is 
    field which has 0 as default value and is not automatically updated.
  TIMESTAMP_DN_FIELD - field with NOW() as default but not set on update
    automatically (TIMESTAMP DEFAULT NOW())
  TIMESTAMP_UN_FIELD - field which is set on update automatically but has not 
    NOW() as default (but it may has 0 or some other const timestamp as 
    default) (TIMESTAMP ON UPDATE NOW()).
  TIMESTAMP_DNUN_FIELD - field which has now() as default and is auto-set on 
    update. (TIMESTAMP DEFAULT NOW() ON UPDATE NOW())
  NONE - field which is not auto-set on update with some other than NOW() 
    default value (TIMESTAMP DEFAULT 0).

  Note that TIMESTAMP_OLD_FIELDs are never created explicitly now, they are 
  left only for preserving ability to read old tables. Such fields replaced 
  with their newer analogs in CREATE TABLE and in SHOW CREATE TABLE. This is 
  because we want to prefer NONE unireg_check before TIMESTAMP_OLD_FIELD for 
  "TIMESTAMP DEFAULT 'Const'" field. (Old timestamps allowed such 
  specification too but ignored default value for first timestamp, which of 
  course is non-standard.) In most cases user won't notice any change, only
  exception is different behavior of old/new timestamps during ALTER TABLE.
 */

Field_timestamp::Field_timestamp(uchar *ptr_arg,
                                 uint32 len_arg __attribute__((__unused__)),
                                 uchar *null_ptr_arg, uchar null_bit_arg,
                                 enum utype unireg_check_arg,
                                 const char *field_name_arg,
                                 TABLE_SHARE *share,
                                 CHARSET_INFO *cs)
  :Field_str(ptr_arg, MAX_DATETIME_WIDTH, null_ptr_arg, null_bit_arg,
	     unireg_check_arg, field_name_arg, cs)
{
  /* For 4.0 MYD and 4.0 InnoDB compatibility */
  flags|= ZEROFILL_FLAG | UNSIGNED_FLAG;
  if (!share->timestamp_field && unireg_check != NONE)
  {
    /* This timestamp has auto-update */
    share->timestamp_field= this;
    flags|= TIMESTAMP_FLAG;
    if (unireg_check != TIMESTAMP_DN_FIELD)
      flags|= ON_UPDATE_NOW_FLAG;
  }
}


Field_timestamp::Field_timestamp(bool maybe_null_arg,
                                 const char *field_name_arg,
                                 CHARSET_INFO *cs)
  :Field_str((uchar*) 0, MAX_DATETIME_WIDTH,
             maybe_null_arg ? (uchar*) "": 0, 0,
	     NONE, field_name_arg, cs)
{
  /* For 4.0 MYD and 4.0 InnoDB compatibility */
  flags|= ZEROFILL_FLAG | UNSIGNED_FLAG;
    if (unireg_check != TIMESTAMP_DN_FIELD)
      flags|= ON_UPDATE_NOW_FLAG;
}


/**
  Get auto-set type for TIMESTAMP field.

  Returns value indicating during which operations this TIMESTAMP field
  should be auto-set to current timestamp.
*/
timestamp_auto_set_type Field_timestamp::get_auto_set_type() const
{
  switch (unireg_check)
  {
  case TIMESTAMP_DN_FIELD:
    return TIMESTAMP_AUTO_SET_ON_INSERT;
  case TIMESTAMP_UN_FIELD:
    return TIMESTAMP_AUTO_SET_ON_UPDATE;
  case TIMESTAMP_OLD_FIELD:
    /*
      Although we can have several such columns in legacy tables this
      function should be called only for first of them (i.e. the one
      having auto-set property).
    */
    assert(table->timestamp_field == this);
    /* Fall-through */
  case TIMESTAMP_DNUN_FIELD:
    return TIMESTAMP_AUTO_SET_ON_BOTH;
  default:
    /*
      Normally this function should not be called for TIMESTAMPs without
      auto-set property.
    */
    assert(0);
    return TIMESTAMP_NO_AUTO_SET;
  }
}


int Field_timestamp::store(const char *from,
                           uint len,
                           CHARSET_INFO *cs __attribute__((__unused__)))
{
  MYSQL_TIME l_time;
  my_time_t tmp= 0;
  int error;
  bool have_smth_to_conv;
  bool in_dst_time_gap;
  THD *thd= table ? table->in_use : current_thd;

  /* We don't want to store invalid or fuzzy datetime values in TIMESTAMP */
  have_smth_to_conv= (str_to_datetime(from, len, &l_time, 1, &error) >
                      MYSQL_TIMESTAMP_ERROR);

  if (error || !have_smth_to_conv)
  {
    error= 1;
    set_datetime_warning(MYSQL_ERROR::WARN_LEVEL_WARN, WARN_DATA_TRUNCATED,
                         from, len, MYSQL_TIMESTAMP_DATETIME, 1);
  }

  /* Only convert a correct date (not a zero date) */
  if (have_smth_to_conv && l_time.month)
  {
    if (!(tmp= TIME_to_timestamp(thd, &l_time, &in_dst_time_gap)))
    {
      set_datetime_warning(MYSQL_ERROR::WARN_LEVEL_WARN,
                           ER_WARN_DATA_OUT_OF_RANGE,
                           from, len, MYSQL_TIMESTAMP_DATETIME, !error);
      error= 1;
    }
    else if (in_dst_time_gap)
    {
      set_datetime_warning(MYSQL_ERROR::WARN_LEVEL_WARN,
                           ER_WARN_INVALID_TIMESTAMP,
                           from, len, MYSQL_TIMESTAMP_DATETIME, !error);
      error= 1;
    }
  }
  store_timestamp(tmp);
  return error;
}


int Field_timestamp::store(double nr)
{
  int error= 0;
  if (nr < 0 || nr > 99991231235959.0)
  {
    set_datetime_warning(MYSQL_ERROR::WARN_LEVEL_WARN,
                         ER_WARN_DATA_OUT_OF_RANGE,
                         nr, MYSQL_TIMESTAMP_DATETIME);
    nr= 0;					// Avoid overflow on buff
    error= 1;
  }
  error|= Field_timestamp::store((longlong) rint(nr), false);
  return error;
}


int Field_timestamp::store(longlong nr,
                           bool unsigned_val __attribute__((__unused__)))
{
  MYSQL_TIME l_time;
  my_time_t timestamp= 0;
  int error;
  bool in_dst_time_gap;
  THD *thd= table ? table->in_use : current_thd;

  /* We don't want to store invalid or fuzzy datetime values in TIMESTAMP */
  longlong tmp= number_to_datetime(nr, &l_time, (thd->variables.sql_mode &
                                                 MODE_NO_ZERO_DATE) |
                                   MODE_NO_ZERO_IN_DATE, &error);
  if (tmp == -1LL)
  {
    error= 2;
  }

  if (!error && tmp)
  {
    if (!(timestamp= TIME_to_timestamp(thd, &l_time, &in_dst_time_gap)))
    {
      set_datetime_warning(MYSQL_ERROR::WARN_LEVEL_WARN,
                           ER_WARN_DATA_OUT_OF_RANGE,
                           nr, MYSQL_TIMESTAMP_DATETIME, 1);
      error= 1;
    }
    if (in_dst_time_gap)
    {
      set_datetime_warning(MYSQL_ERROR::WARN_LEVEL_WARN,
                           ER_WARN_INVALID_TIMESTAMP,
                           nr, MYSQL_TIMESTAMP_DATETIME, 1);
      error= 1;
    }
  } else if (error)
    set_datetime_warning(MYSQL_ERROR::WARN_LEVEL_WARN,
                         WARN_DATA_TRUNCATED,
                         nr, MYSQL_TIMESTAMP_DATETIME, 1);

  store_timestamp(timestamp);
  return error;
}

double Field_timestamp::val_real(void)
{
  return (double) Field_timestamp::val_int();
}

longlong Field_timestamp::val_int(void)
{
  uint32 temp;
  MYSQL_TIME time_tmp;
  THD  *thd= table ? table->in_use : current_thd;

  thd->time_zone_used= 1;
#ifdef WORDS_BIGENDIAN
  if (table && table->s->db_low_byte_first)
    temp=uint4korr(ptr);
  else
#endif
    longget(temp,ptr);

  if (temp == 0L)				// No time
    return(0);					/* purecov: inspected */
  
  thd->variables.time_zone->gmt_sec_to_TIME(&time_tmp, (my_time_t)temp);
  
  return time_tmp.year * 10000000000LL + time_tmp.month * 100000000LL +
         time_tmp.day * 1000000L + time_tmp.hour * 10000L +
         time_tmp.minute * 100 + time_tmp.second;
}


String *Field_timestamp::val_str(String *val_buffer, String *val_ptr)
{
  uint32 temp, temp2;
  MYSQL_TIME time_tmp;
  THD *thd= table ? table->in_use : current_thd;
  char *to;

  val_buffer->alloc(field_length+1);
  to= (char*) val_buffer->ptr();
  val_buffer->length(field_length);

  thd->time_zone_used= 1;
#ifdef WORDS_BIGENDIAN
  if (table && table->s->db_low_byte_first)
    temp=uint4korr(ptr);
  else
#endif
    longget(temp,ptr);

  if (temp == 0L)
  {				      /* Zero time is "000000" */
    val_ptr->set(STRING_WITH_LEN("0000-00-00 00:00:00"), &my_charset_bin);
    return val_ptr;
  }
  val_buffer->set_charset(&my_charset_bin);	// Safety
  
  thd->variables.time_zone->gmt_sec_to_TIME(&time_tmp,(my_time_t)temp);

  temp= time_tmp.year % 100;
  if (temp < YY_PART_YEAR - 1)
  {
    *to++= '2';
    *to++= '0';
  }
  else
  {
    *to++= '1';
    *to++= '9';
  }
  temp2=temp/10; temp=temp-temp2*10;
  *to++= (char) ('0'+(char) (temp2));
  *to++= (char) ('0'+(char) (temp));
  *to++= '-';
  temp=time_tmp.month;
  temp2=temp/10; temp=temp-temp2*10;
  *to++= (char) ('0'+(char) (temp2));
  *to++= (char) ('0'+(char) (temp));
  *to++= '-';
  temp=time_tmp.day;
  temp2=temp/10; temp=temp-temp2*10;
  *to++= (char) ('0'+(char) (temp2));
  *to++= (char) ('0'+(char) (temp));
  *to++= ' ';
  temp=time_tmp.hour;
  temp2=temp/10; temp=temp-temp2*10;
  *to++= (char) ('0'+(char) (temp2));
  *to++= (char) ('0'+(char) (temp));
  *to++= ':';
  temp=time_tmp.minute;
  temp2=temp/10; temp=temp-temp2*10;
  *to++= (char) ('0'+(char) (temp2));
  *to++= (char) ('0'+(char) (temp));
  *to++= ':';
  temp=time_tmp.second;
  temp2=temp/10; temp=temp-temp2*10;
  *to++= (char) ('0'+(char) (temp2));
  *to++= (char) ('0'+(char) (temp));
  *to= 0;
  return val_buffer;
}


bool Field_timestamp::get_date(MYSQL_TIME *ltime, uint fuzzydate)
{
  long temp;
  THD *thd= table ? table->in_use : current_thd;
  thd->time_zone_used= 1;
#ifdef WORDS_BIGENDIAN
  if (table && table->s->db_low_byte_first)
    temp=uint4korr(ptr);
  else
#endif
    longget(temp,ptr);
  if (temp == 0L)
  {				      /* Zero time is "000000" */
    if (fuzzydate & TIME_NO_ZERO_DATE)
      return 1;
    bzero((char*) ltime,sizeof(*ltime));
  }
  else
  {
    thd->variables.time_zone->gmt_sec_to_TIME(ltime, (my_time_t)temp);
  }
  return 0;
}

bool Field_timestamp::get_time(MYSQL_TIME *ltime)
{
  return Field_timestamp::get_date(ltime,0);
}


bool Field_timestamp::send_binary(Protocol *protocol)
{
  MYSQL_TIME tm;
  Field_timestamp::get_date(&tm, 0);
  return protocol->store(&tm);
}


int Field_timestamp::cmp(const uchar *a_ptr, const uchar *b_ptr)
{
  int32 a,b;
#ifdef WORDS_BIGENDIAN
  if (table && table->s->db_low_byte_first)
  {
    a=sint4korr(a_ptr);
    b=sint4korr(b_ptr);
  }
  else
#endif
  {
  longget(a,a_ptr);
  longget(b,b_ptr);
  }
  return ((uint32) a < (uint32) b) ? -1 : ((uint32) a > (uint32) b) ? 1 : 0;
}


void Field_timestamp::sort_string(uchar *to,uint length __attribute__((unused)))
{
#ifdef WORDS_BIGENDIAN
  if (!table || !table->s->db_low_byte_first)
  {
    to[0] = ptr[0];
    to[1] = ptr[1];
    to[2] = ptr[2];
    to[3] = ptr[3];
  }
  else
#endif
  {
    to[0] = ptr[3];
    to[1] = ptr[2];
    to[2] = ptr[1];
    to[3] = ptr[0];
  }
}


void Field_timestamp::sql_type(String &res) const
{
  res.set_ascii(STRING_WITH_LEN("timestamp"));
}


void Field_timestamp::set_time()
{
  THD *thd= table ? table->in_use : current_thd;
  long tmp= (long) thd->query_start();
  set_notnull();
  store_timestamp(tmp);
}

/****************************************************************************
** time type
** In string context: HH:MM:SS
** In number context: HHMMSS
** Stored as a 3 byte unsigned int
****************************************************************************/

int Field_time::store(const char *from,
                      uint len,
                      CHARSET_INFO *cs __attribute__((__unused__)))
{
  MYSQL_TIME ltime;
  long tmp;
  int error= 0;
  int warning;

  if (str_to_time(from, len, &ltime, &warning))
  {
    tmp=0L;
    error= 2;
    set_datetime_warning(MYSQL_ERROR::WARN_LEVEL_WARN, WARN_DATA_TRUNCATED,
                         from, len, MYSQL_TIMESTAMP_TIME, 1);
  }
  else
  {
    if (warning & MYSQL_TIME_WARN_TRUNCATED)
    {
      set_datetime_warning(MYSQL_ERROR::WARN_LEVEL_WARN,
                           WARN_DATA_TRUNCATED,
                           from, len, MYSQL_TIMESTAMP_TIME, 1);
      error= 1;
    }
    if (warning & MYSQL_TIME_WARN_OUT_OF_RANGE)
    {
      set_datetime_warning(MYSQL_ERROR::WARN_LEVEL_WARN, 
                           ER_WARN_DATA_OUT_OF_RANGE,
                           from, len, MYSQL_TIMESTAMP_TIME, !error);
      error= 1;
    }
    if (ltime.month)
      ltime.day=0;
    tmp=(ltime.day*24L+ltime.hour)*10000L+(ltime.minute*100+ltime.second);
  }
  
  if (ltime.neg)
    tmp= -tmp;
  int3store(ptr,tmp);
  return error;
}


int Field_time::store_time(MYSQL_TIME *ltime,
                           timestamp_type time_type __attribute__((__unused__)))
{
  long tmp= ((ltime->month ? 0 : ltime->day * 24L) + ltime->hour) * 10000L +
            (ltime->minute * 100 + ltime->second);
  if (ltime->neg)
    tmp= -tmp;
  return Field_time::store((longlong) tmp, false);
}


int Field_time::store(double nr)
{
  long tmp;
  int error= 0;
  if (nr > (double)TIME_MAX_VALUE)
  {
    tmp= TIME_MAX_VALUE;
    set_datetime_warning(MYSQL_ERROR::WARN_LEVEL_WARN,
                         ER_WARN_DATA_OUT_OF_RANGE, nr, MYSQL_TIMESTAMP_TIME);
    error= 1;
  }
  else if (nr < (double)-TIME_MAX_VALUE)
  {
    tmp= -TIME_MAX_VALUE;
    set_datetime_warning(MYSQL_ERROR::WARN_LEVEL_WARN, 
                         ER_WARN_DATA_OUT_OF_RANGE, nr, MYSQL_TIMESTAMP_TIME);
    error= 1;
  }
  else
  {
    tmp=(long) floor(fabs(nr));			// Remove fractions
    if (nr < 0)
      tmp= -tmp;
    if (tmp % 100 > 59 || tmp/100 % 100 > 59)
    {
      tmp=0;
      set_datetime_warning(MYSQL_ERROR::WARN_LEVEL_WARN, 
                           ER_WARN_DATA_OUT_OF_RANGE, nr,
                           MYSQL_TIMESTAMP_TIME);
      error= 1;
    }
  }
  int3store(ptr,tmp);
  return error;
}


int Field_time::store(longlong nr, bool unsigned_val)
{
  long tmp;
  int error= 0;
  if (nr < (longlong) -TIME_MAX_VALUE && !unsigned_val)
  {
    tmp= -TIME_MAX_VALUE;
    set_datetime_warning(MYSQL_ERROR::WARN_LEVEL_WARN, 
                         ER_WARN_DATA_OUT_OF_RANGE, nr,
                         MYSQL_TIMESTAMP_TIME, 1);
    error= 1;
  }
  else if (nr > (longlong) TIME_MAX_VALUE || (nr < 0 && unsigned_val))
  {
    tmp= TIME_MAX_VALUE;
    set_datetime_warning(MYSQL_ERROR::WARN_LEVEL_WARN, 
                         ER_WARN_DATA_OUT_OF_RANGE, nr,
                         MYSQL_TIMESTAMP_TIME, 1);
    error= 1;
  }
  else
  {
    tmp=(long) nr;
    if (tmp % 100 > 59 || tmp/100 % 100 > 59)
    {
      tmp=0;
      set_datetime_warning(MYSQL_ERROR::WARN_LEVEL_WARN, 
                           ER_WARN_DATA_OUT_OF_RANGE, nr,
                           MYSQL_TIMESTAMP_TIME, 1);
      error= 1;
    }
  }
  int3store(ptr,tmp);
  return error;
}


double Field_time::val_real(void)
{
  uint32 j= (uint32) uint3korr(ptr);
  return (double) j;
}

longlong Field_time::val_int(void)
{
  return (longlong) sint3korr(ptr);
}


/**
  @note
  This function is multi-byte safe as the result string is always of type
  my_charset_bin
*/

String *Field_time::val_str(String *val_buffer,
			    String *val_ptr __attribute__((unused)))
{
  MYSQL_TIME ltime;
  val_buffer->alloc(MAX_DATE_STRING_REP_LENGTH);
  long tmp=(long) sint3korr(ptr);
  ltime.neg= 0;
  if (tmp < 0)
  {
    tmp= -tmp;
    ltime.neg= 1;
  }
  ltime.day= (uint) 0;
  ltime.hour= (uint) (tmp/10000);
  ltime.minute= (uint) (tmp/100 % 100);
  ltime.second= (uint) (tmp % 100);
  make_time((DATE_TIME_FORMAT*) 0, &ltime, val_buffer);
  return val_buffer;
}


/**
  @note
  Normally we would not consider 'time' as a valid date, but we allow
  get_date() here to be able to do things like
  DATE_FORMAT(time, "%l.%i %p")
*/
 
bool Field_time::get_date(MYSQL_TIME *ltime, uint fuzzydate)
{
  long tmp;
  THD *thd= table ? table->in_use : current_thd;
  if (!(fuzzydate & TIME_FUZZY_DATE))
  {
    push_warning_printf(thd, MYSQL_ERROR::WARN_LEVEL_WARN,
                        ER_WARN_DATA_OUT_OF_RANGE,
                        ER(ER_WARN_DATA_OUT_OF_RANGE), field_name,
                        thd->row_count);
    return 1;
  }
  tmp=(long) sint3korr(ptr);
  ltime->neg=0;
  if (tmp < 0)
  {
    ltime->neg= 1;
    tmp=-tmp;
  }
  ltime->hour=tmp/10000;
  tmp-=ltime->hour*10000;
  ltime->minute=   tmp/100;
  ltime->second= tmp % 100;
  ltime->year= ltime->month= ltime->day= ltime->second_part= 0;
  return 0;
}


bool Field_time::get_time(MYSQL_TIME *ltime)
{
  long tmp=(long) sint3korr(ptr);
  ltime->neg=0;
  if (tmp < 0)
  {
    ltime->neg= 1;
    tmp=-tmp;
  }
  ltime->day= 0;
  ltime->hour=   (int) (tmp/10000);
  tmp-=ltime->hour*10000;
  ltime->minute= (int) tmp/100;
  ltime->second= (int) tmp % 100;
  ltime->second_part=0;
  ltime->time_type= MYSQL_TIMESTAMP_TIME;
  return 0;
}


bool Field_time::send_binary(Protocol *protocol)
{
  MYSQL_TIME tm;
  Field_time::get_time(&tm);
  tm.day= tm.hour/24;				// Move hours to days
  tm.hour-= tm.day*24;
  return protocol->store_time(&tm);
}


int Field_time::cmp(const uchar *a_ptr, const uchar *b_ptr)
{
  int32 a,b;
  a=(int32) sint3korr(a_ptr);
  b=(int32) sint3korr(b_ptr);
  return (a < b) ? -1 : (a > b) ? 1 : 0;
}

void Field_time::sort_string(uchar *to,uint length __attribute__((unused)))
{
  to[0] = (uchar) (ptr[2] ^ 128);
  to[1] = ptr[1];
  to[2] = ptr[0];
}

void Field_time::sql_type(String &res) const
{
  res.set_ascii(STRING_WITH_LEN("time"));
}

/****************************************************************************
** year type
** Save in a byte the year 0, 1901->2155
** Can handle 2 byte or 4 byte years!
****************************************************************************/

int Field_year::store(const char *from, uint len,CHARSET_INFO *cs)
{
  char *end;
  int error;
  longlong nr= cs->cset->strntoull10rnd(cs, from, len, 0, &end, &error);

  if (nr < 0 || (nr >= 100 && nr <= 1900) || nr > 2155 || 
      error == MY_ERRNO_ERANGE)
  {
    *ptr=0;
    set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_OUT_OF_RANGE, 1);
    return 1;
  }
  if (table->in_use->count_cuted_fields && 
      (error= check_int(cs, from, len, end, error)))
  {
    if (error == 1)  /* empty or incorrect string */
    {
      *ptr= 0;
      return 1;
    }
    error= 1;
  }

  if (nr != 0 || len != 4)
  {
    if (nr < YY_PART_YEAR)
      nr+=100;					// 2000 - 2069
    else if (nr > 1900)
      nr-= 1900;
  }
  *ptr= (char) (uchar) nr;
  return error;
}


int Field_year::store(double nr)
{
  if (nr < 0.0 || nr >= 2155.0)
  {
    (void) Field_year::store((longlong) -1, false);
    return 1;
  }
  return Field_year::store((longlong) nr, false);
}


int Field_year::store(longlong nr,
                      bool unsigned_val __attribute__((__unused__)))
{
  if (nr < 0 || (nr >= 100 && nr <= 1900) || nr > 2155)
  {
    *ptr= 0;
    set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_OUT_OF_RANGE, 1);
    return 1;
  }
  if (nr != 0 || field_length != 4)		// 0000 -> 0; 00 -> 2000
  {
    if (nr < YY_PART_YEAR)
      nr+=100;					// 2000 - 2069
    else if (nr > 1900)
      nr-= 1900;
  }
  *ptr= (char) (uchar) nr;
  return 0;
}


bool Field_year::send_binary(Protocol *protocol)
{
  uint64_t tmp= Field_year::val_int();
  return protocol->store_short(tmp);
}


double Field_year::val_real(void)
{
  return (double) Field_year::val_int();
}


longlong Field_year::val_int(void)
{
  int tmp= (int) ptr[0];
  if (field_length != 4)
    tmp%=100;					// Return last 2 char
  else if (tmp)
    tmp+=1900;
  return (longlong) tmp;
}


String *Field_year::val_str(String *val_buffer,
			    String *val_ptr __attribute__((unused)))
{
  val_buffer->alloc(5);
  val_buffer->length(field_length);
  char *to=(char*) val_buffer->ptr();
  sprintf(to,field_length == 2 ? "%02d" : "%04d",(int) Field_year::val_int());
  return val_buffer;
}


void Field_year::sql_type(String &res) const
{
  CHARSET_INFO *cs=res.charset();
  res.length(cs->cset->snprintf(cs,(char*)res.ptr(),res.alloced_length(),
			  "year(%d)",(int) field_length));
}


/****************************************************************************
** The new date type
** This is identical to the old date type, but stored on 3 bytes instead of 4
** In number context: YYYYMMDD
****************************************************************************/

/*
  Store string into a date field

  SYNOPSIS
    Field_newdate::store()
    from                Date string
    len                 Length of date field
    cs                  Character set (not used)

  RETURN
    0  ok
    1  Value was cut during conversion
    2  Wrong date string
    3  Datetime value that was cut (warning level NOTE)
       This is used by opt_range.cc:get_mm_leaf(). Note that there is a
       nearly-identical class Field_date doesn't ever return 3 from its
       store function.
*/

int Field_newdate::store(const char *from,
                         uint len,
                         CHARSET_INFO *cs __attribute__((__unused__)))
{
  long tmp;
  MYSQL_TIME l_time;
  int error;
  THD *thd= table ? table->in_use : current_thd;
  enum enum_mysql_timestamp_type ret;
  if ((ret= str_to_datetime(from, len, &l_time,
                            (TIME_FUZZY_DATE |
                             (thd->variables.sql_mode &
                              (MODE_NO_ZERO_IN_DATE | MODE_NO_ZERO_DATE |
                               MODE_INVALID_DATES))),
                            &error)) <= MYSQL_TIMESTAMP_ERROR)
  {
    tmp= 0;
    error= 2;
  }
  else
  {
    tmp= l_time.day + l_time.month*32 + l_time.year*16*32;
    if (!error && (ret != MYSQL_TIMESTAMP_DATE) &&
        (l_time.hour || l_time.minute || l_time.second || l_time.second_part))
      error= 3;                                 // Datetime was cut (note)
  }

  if (error)
    set_datetime_warning(error == 3 ? MYSQL_ERROR::WARN_LEVEL_NOTE :
                         MYSQL_ERROR::WARN_LEVEL_WARN,
                         WARN_DATA_TRUNCATED,
                         from, len, MYSQL_TIMESTAMP_DATE, 1);

  int3store(ptr, tmp);
  return error;
}


int Field_newdate::store(double nr)
{
  if (nr < 0.0 || nr > 99991231235959.0)
  {
    int3store(ptr,(int32) 0);
    set_datetime_warning(MYSQL_ERROR::WARN_LEVEL_WARN,
                         WARN_DATA_TRUNCATED, nr, MYSQL_TIMESTAMP_DATE);
    return 1;
  }
  return Field_newdate::store((longlong) rint(nr), false);
}


int Field_newdate::store(longlong nr,
                         bool unsigned_val __attribute__((__unused__)))
{
  MYSQL_TIME l_time;
  longlong tmp;
  int error;
  THD *thd= table ? table->in_use : current_thd;
  if (number_to_datetime(nr, &l_time,
                         (TIME_FUZZY_DATE |
                          (thd->variables.sql_mode &
                           (MODE_NO_ZERO_IN_DATE | MODE_NO_ZERO_DATE |
                            MODE_INVALID_DATES))),
                         &error) == -1LL)
  {
    tmp= 0L;
    error= 2;
  }
  else
    tmp= l_time.day + l_time.month*32 + l_time.year*16*32;

  if (!error && l_time.time_type != MYSQL_TIMESTAMP_DATE &&
      (l_time.hour || l_time.minute || l_time.second || l_time.second_part))
    error= 3;

  if (error)
    set_datetime_warning(error == 3 ? MYSQL_ERROR::WARN_LEVEL_NOTE :
                         MYSQL_ERROR::WARN_LEVEL_WARN,
                         error == 2 ? 
                         ER_WARN_DATA_OUT_OF_RANGE : WARN_DATA_TRUNCATED,
                         nr,MYSQL_TIMESTAMP_DATE, 1);

  int3store(ptr,tmp);
  return error;
}


int Field_newdate::store_time(MYSQL_TIME *ltime,timestamp_type time_type)
{
  long tmp;
  int error= 0;
  if (time_type == MYSQL_TIMESTAMP_DATE ||
      time_type == MYSQL_TIMESTAMP_DATETIME)
  {
    tmp=ltime->year*16*32+ltime->month*32+ltime->day;
    if (check_date(ltime, tmp != 0,
                   (TIME_FUZZY_DATE |
                    (current_thd->variables.sql_mode &
                     (MODE_NO_ZERO_IN_DATE | MODE_NO_ZERO_DATE |
                      MODE_INVALID_DATES))), &error))
    {
      char buff[MAX_DATE_STRING_REP_LENGTH];
      String str(buff, sizeof(buff), &my_charset_latin1);
      make_date((DATE_TIME_FORMAT *) 0, ltime, &str);
      set_datetime_warning(MYSQL_ERROR::WARN_LEVEL_WARN, WARN_DATA_TRUNCATED,
                           str.ptr(), str.length(), MYSQL_TIMESTAMP_DATE, 1);
    }
    if (!error && ltime->time_type != MYSQL_TIMESTAMP_DATE &&
        (ltime->hour || ltime->minute || ltime->second || ltime->second_part))
    {
      char buff[MAX_DATE_STRING_REP_LENGTH];
      String str(buff, sizeof(buff), &my_charset_latin1);
      make_datetime((DATE_TIME_FORMAT *) 0, ltime, &str);
      set_datetime_warning(MYSQL_ERROR::WARN_LEVEL_NOTE,
                           WARN_DATA_TRUNCATED,
                           str.ptr(), str.length(), MYSQL_TIMESTAMP_DATE, 1);
      error= 3;
    }
  }
  else
  {
    tmp=0;
    error= 1;
    set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, WARN_DATA_TRUNCATED, 1);
  }
  int3store(ptr,tmp);
  return error;
}


bool Field_newdate::send_binary(Protocol *protocol)
{
  MYSQL_TIME tm;
  Field_newdate::get_date(&tm,0);
  return protocol->store_date(&tm);
}


double Field_newdate::val_real(void)
{
  return (double) Field_newdate::val_int();
}


longlong Field_newdate::val_int(void)
{
  ulong j= uint3korr(ptr);
  j= (j % 32L)+(j / 32L % 16L)*100L + (j/(16L*32L))*10000L;
  return (longlong) j;
}


String *Field_newdate::val_str(String *val_buffer,
			       String *val_ptr __attribute__((unused)))
{
  val_buffer->alloc(field_length);
  val_buffer->length(field_length);
  uint32 tmp=(uint32) uint3korr(ptr);
  int part;
  char *pos=(char*) val_buffer->ptr()+10;

  /* Open coded to get more speed */
  *pos--=0;					// End NULL
  part=(int) (tmp & 31);
  *pos--= (char) ('0'+part%10);
  *pos--= (char) ('0'+part/10);
  *pos--= '-';
  part=(int) (tmp >> 5 & 15);
  *pos--= (char) ('0'+part%10);
  *pos--= (char) ('0'+part/10);
  *pos--= '-';
  part=(int) (tmp >> 9);
  *pos--= (char) ('0'+part%10); part/=10;
  *pos--= (char) ('0'+part%10); part/=10;
  *pos--= (char) ('0'+part%10); part/=10;
  *pos=   (char) ('0'+part);
  return val_buffer;
}


bool Field_newdate::get_date(MYSQL_TIME *ltime,uint fuzzydate)
{
  uint32 tmp=(uint32) uint3korr(ptr);
  ltime->day=   tmp & 31;
  ltime->month= (tmp >> 5) & 15;
  ltime->year=  (tmp >> 9);
  ltime->time_type= MYSQL_TIMESTAMP_DATE;
  ltime->hour= ltime->minute= ltime->second= ltime->second_part= ltime->neg= 0;
  return ((!(fuzzydate & TIME_FUZZY_DATE) && (!ltime->month || !ltime->day)) ?
          1 : 0);
}


bool Field_newdate::get_time(MYSQL_TIME *ltime)
{
  return Field_newdate::get_date(ltime,0);
}


int Field_newdate::cmp(const uchar *a_ptr, const uchar *b_ptr)
{
  uint32 a,b;
  a=(uint32) uint3korr(a_ptr);
  b=(uint32) uint3korr(b_ptr);
  return (a < b) ? -1 : (a > b) ? 1 : 0;
}


void Field_newdate::sort_string(uchar *to,uint length __attribute__((unused)))
{
  to[0] = ptr[2];
  to[1] = ptr[1];
  to[2] = ptr[0];
}


void Field_newdate::sql_type(String &res) const
{
  res.set_ascii(STRING_WITH_LEN("date"));
}


/****************************************************************************
** datetime type
** In string context: YYYY-MM-DD HH:MM:DD
** In number context: YYYYMMDDHHMMDD
** Stored as a 8 byte unsigned int. Should sometimes be change to a 6 byte int.
****************************************************************************/

int Field_datetime::store(const char *from,
                          uint len,
                          CHARSET_INFO *cs __attribute__((__unused__)))
{
  MYSQL_TIME time_tmp;
  int error;
  uint64_t tmp= 0;
  enum enum_mysql_timestamp_type func_res;
  THD *thd= table ? table->in_use : current_thd;

  func_res= str_to_datetime(from, len, &time_tmp,
                            (TIME_FUZZY_DATE |
                             (thd->variables.sql_mode &
                              (MODE_NO_ZERO_IN_DATE | MODE_NO_ZERO_DATE |
                               MODE_INVALID_DATES))),
                            &error);
  if ((int) func_res > (int) MYSQL_TIMESTAMP_ERROR)
    tmp= TIME_to_uint64_t_datetime(&time_tmp);
  else
    error= 1;                                 // Fix if invalid zero date

  if (error)
    set_datetime_warning(MYSQL_ERROR::WARN_LEVEL_WARN,
                         ER_WARN_DATA_OUT_OF_RANGE,
                         from, len, MYSQL_TIMESTAMP_DATETIME, 1);

#ifdef WORDS_BIGENDIAN
  if (table && table->s->db_low_byte_first)
  {
    int8store(ptr,tmp);
  }
  else
#endif
    longlongstore(ptr,tmp);
  return error;
}


int Field_datetime::store(double nr)
{
  int error= 0;
  if (nr < 0.0 || nr > 99991231235959.0)
  {
    set_datetime_warning(MYSQL_ERROR::WARN_LEVEL_WARN, 
                         ER_WARN_DATA_OUT_OF_RANGE,
                         nr, MYSQL_TIMESTAMP_DATETIME);
    nr= 0.0;
    error= 1;
  }
  error|= Field_datetime::store((longlong) rint(nr), false);
  return error;
}


int Field_datetime::store(longlong nr,
                          bool unsigned_val __attribute__((__unused__)))
{
  MYSQL_TIME not_used;
  int error;
  longlong initial_nr= nr;
  THD *thd= table ? table->in_use : current_thd;

  nr= number_to_datetime(nr, &not_used, (TIME_FUZZY_DATE |
                                         (thd->variables.sql_mode &
                                          (MODE_NO_ZERO_IN_DATE |
                                           MODE_NO_ZERO_DATE |
                                           MODE_INVALID_DATES))), &error);

  if (nr == -1LL)
  {
    nr= 0;
    error= 2;
  }

  if (error)
    set_datetime_warning(MYSQL_ERROR::WARN_LEVEL_WARN,
                         error == 2 ? ER_WARN_DATA_OUT_OF_RANGE :
                         WARN_DATA_TRUNCATED, initial_nr,
                         MYSQL_TIMESTAMP_DATETIME, 1);

#ifdef WORDS_BIGENDIAN
  if (table && table->s->db_low_byte_first)
  {
    int8store(ptr,nr);
  }
  else
#endif
    longlongstore(ptr,nr);
  return error;
}


int Field_datetime::store_time(MYSQL_TIME *ltime,timestamp_type time_type)
{
  longlong tmp;
  int error= 0;
  /*
    We don't perform range checking here since values stored in TIME
    structure always fit into DATETIME range.
  */
  if (time_type == MYSQL_TIMESTAMP_DATE ||
      time_type == MYSQL_TIMESTAMP_DATETIME)
  {
    tmp=((ltime->year*10000L+ltime->month*100+ltime->day)*1000000LL+
	 (ltime->hour*10000L+ltime->minute*100+ltime->second));
    if (check_date(ltime, tmp != 0,
                   (TIME_FUZZY_DATE |
                    (current_thd->variables.sql_mode &
                     (MODE_NO_ZERO_IN_DATE | MODE_NO_ZERO_DATE |
                      MODE_INVALID_DATES))), &error))
    {
      char buff[MAX_DATE_STRING_REP_LENGTH];
      String str(buff, sizeof(buff), &my_charset_latin1);
      make_datetime((DATE_TIME_FORMAT *) 0, ltime, &str);
      set_datetime_warning(MYSQL_ERROR::WARN_LEVEL_WARN, WARN_DATA_TRUNCATED,
                           str.ptr(), str.length(), MYSQL_TIMESTAMP_DATETIME,1);
    }
  }
  else
  {
    tmp=0;
    error= 1;
    set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, WARN_DATA_TRUNCATED, 1);
  }
#ifdef WORDS_BIGENDIAN
  if (table && table->s->db_low_byte_first)
  {
    int8store(ptr,tmp);
  }
  else
#endif
    longlongstore(ptr,tmp);
  return error;
}

bool Field_datetime::send_binary(Protocol *protocol)
{
  MYSQL_TIME tm;
  Field_datetime::get_date(&tm, TIME_FUZZY_DATE);
  return protocol->store(&tm);
}


double Field_datetime::val_real(void)
{
  return (double) Field_datetime::val_int();
}

longlong Field_datetime::val_int(void)
{
  longlong j;
#ifdef WORDS_BIGENDIAN
  if (table && table->s->db_low_byte_first)
    j=sint8korr(ptr);
  else
#endif
    longlongget(j,ptr);
  return j;
}


String *Field_datetime::val_str(String *val_buffer,
				String *val_ptr __attribute__((unused)))
{
  val_buffer->alloc(field_length);
  val_buffer->length(field_length);
  uint64_t tmp;
  long part1,part2;
  char *pos;
  int part3;

#ifdef WORDS_BIGENDIAN
  if (table && table->s->db_low_byte_first)
    tmp=sint8korr(ptr);
  else
#endif
    longlongget(tmp,ptr);

  /*
    Avoid problem with slow longlong arithmetic and sprintf
  */

  part1=(long) (tmp/1000000LL);
  part2=(long) (tmp - (uint64_t) part1*1000000LL);

  pos=(char*) val_buffer->ptr() + MAX_DATETIME_WIDTH;
  *pos--=0;
  *pos--= (char) ('0'+(char) (part2%10)); part2/=10;
  *pos--= (char) ('0'+(char) (part2%10)); part3= (int) (part2 / 10);
  *pos--= ':';
  *pos--= (char) ('0'+(char) (part3%10)); part3/=10;
  *pos--= (char) ('0'+(char) (part3%10)); part3/=10;
  *pos--= ':';
  *pos--= (char) ('0'+(char) (part3%10)); part3/=10;
  *pos--= (char) ('0'+(char) part3);
  *pos--= ' ';
  *pos--= (char) ('0'+(char) (part1%10)); part1/=10;
  *pos--= (char) ('0'+(char) (part1%10)); part1/=10;
  *pos--= '-';
  *pos--= (char) ('0'+(char) (part1%10)); part1/=10;
  *pos--= (char) ('0'+(char) (part1%10)); part3= (int) (part1/10);
  *pos--= '-';
  *pos--= (char) ('0'+(char) (part3%10)); part3/=10;
  *pos--= (char) ('0'+(char) (part3%10)); part3/=10;
  *pos--= (char) ('0'+(char) (part3%10)); part3/=10;
  *pos=(char) ('0'+(char) part3);
  return val_buffer;
}

bool Field_datetime::get_date(MYSQL_TIME *ltime, uint fuzzydate)
{
  longlong tmp=Field_datetime::val_int();
  uint32 part1,part2;
  part1=(uint32) (tmp/1000000LL);
  part2=(uint32) (tmp - (uint64_t) part1*1000000LL);

  ltime->time_type=	MYSQL_TIMESTAMP_DATETIME;
  ltime->neg=		0;
  ltime->second_part=	0;
  ltime->second=	(int) (part2%100);
  ltime->minute=	(int) (part2/100%100);
  ltime->hour=		(int) (part2/10000);
  ltime->day=		(int) (part1%100);
  ltime->month= 	(int) (part1/100%100);
  ltime->year= 		(int) (part1/10000);
  return (!(fuzzydate & TIME_FUZZY_DATE) && (!ltime->month || !ltime->day)) ? 1 : 0;
}

bool Field_datetime::get_time(MYSQL_TIME *ltime)
{
  return Field_datetime::get_date(ltime,0);
}

int Field_datetime::cmp(const uchar *a_ptr, const uchar *b_ptr)
{
  longlong a,b;
#ifdef WORDS_BIGENDIAN
  if (table && table->s->db_low_byte_first)
  {
    a=sint8korr(a_ptr);
    b=sint8korr(b_ptr);
  }
  else
#endif
  {
    longlongget(a,a_ptr);
    longlongget(b,b_ptr);
  }
  return ((uint64_t) a < (uint64_t) b) ? -1 :
    ((uint64_t) a > (uint64_t) b) ? 1 : 0;
}

void Field_datetime::sort_string(uchar *to,uint length __attribute__((unused)))
{
#ifdef WORDS_BIGENDIAN
  if (!table || !table->s->db_low_byte_first)
  {
    to[0] = ptr[0];
    to[1] = ptr[1];
    to[2] = ptr[2];
    to[3] = ptr[3];
    to[4] = ptr[4];
    to[5] = ptr[5];
    to[6] = ptr[6];
    to[7] = ptr[7];
  }
  else
#endif
  {
    to[0] = ptr[7];
    to[1] = ptr[6];
    to[2] = ptr[5];
    to[3] = ptr[4];
    to[4] = ptr[3];
    to[5] = ptr[2];
    to[6] = ptr[1];
    to[7] = ptr[0];
  }
}


void Field_datetime::sql_type(String &res) const
{
  res.set_ascii(STRING_WITH_LEN("datetime"));
}

/****************************************************************************
** string type
** A string may be varchar or binary
****************************************************************************/

/*
  Report "not well formed" or "cannot convert" error
  after storing a character string info a field.

  SYNOPSIS
    check_string_copy_error()
    field                    - Field
    well_formed_error_pos    - where not well formed data was first met
    cannot_convert_error_pos - where a not-convertable character was first met
    end                      - end of the string
    cs                       - character set of the string

  NOTES
    As of version 5.0 both cases return the same error:
  
      "Invalid string value: 'xxx' for column 't' at row 1"
  
  Future versions will possibly introduce a new error message:

      "Cannot convert character string: 'xxx' for column 't' at row 1"

  RETURN
    false - If errors didn't happen
    true  - If an error happened
*/

static bool
check_string_copy_error(Field_str *field,
                        const char *well_formed_error_pos,
                        const char *cannot_convert_error_pos,
                        const char *end,
                        CHARSET_INFO *cs)
{
  const char *pos, *end_orig;
  char tmp[64], *t;
  
  if (!(pos= well_formed_error_pos) &&
      !(pos= cannot_convert_error_pos))
    return false;

  end_orig= end;
  set_if_smaller(end, pos + 6);

  for (t= tmp; pos < end; pos++)
  {
    /*
      If the source string is ASCII compatible (mbminlen==1)
      and the source character is in ASCII printable range (0x20..0x7F),
      then display the character as is.
      
      Otherwise, if the source string is not ASCII compatible (e.g. UCS2),
      or the source character is not in the printable range,
      then print the character using HEX notation.
    */
    if (((unsigned char) *pos) >= 0x20 &&
        ((unsigned char) *pos) <= 0x7F &&
        cs->mbminlen == 1)
    {
      *t++= *pos;
    }
    else
    {
      *t++= '\\';
      *t++= 'x';
      *t++= _dig_vec_upper[((unsigned char) *pos) >> 4];
      *t++= _dig_vec_upper[((unsigned char) *pos) & 15];
    }
  }
  if (end_orig > end)
  {
    *t++= '.';
    *t++= '.';
    *t++= '.';
  }
  *t= '\0';
  push_warning_printf(field->table->in_use, 
                      field->table->in_use->abort_on_warning ?
                      MYSQL_ERROR::WARN_LEVEL_ERROR :
                      MYSQL_ERROR::WARN_LEVEL_WARN,
                      ER_TRUNCATED_WRONG_VALUE_FOR_FIELD, 
                      ER(ER_TRUNCATED_WRONG_VALUE_FOR_FIELD),
                      "string", tmp, field->field_name,
                      (ulong) field->table->in_use->row_count);
  return true;
}


/*
  Check if we lost any important data and send a truncation error/warning

  SYNOPSIS
    Field_longstr::report_if_important_data()
    ptr                      - Truncated rest of string
    end                      - End of truncated string

  RETURN VALUES
    0   - None was truncated (or we don't count cut fields)
    2   - Some bytes was truncated

  NOTE
    Check if we lost any important data (anything in a binary string,
    or any non-space in others). If only trailing spaces was lost,
    send a truncation note, otherwise send a truncation error.
*/

int
Field_longstr::report_if_important_data(const char *ptr, const char *end)
{
  if ((ptr < end) && table->in_use->count_cuted_fields)
  {
    if (test_if_important_data(field_charset, ptr, end))
    {
      if (table->in_use->abort_on_warning)
        set_warning(MYSQL_ERROR::WARN_LEVEL_ERROR, ER_DATA_TOO_LONG, 1);
      else
        set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, WARN_DATA_TRUNCATED, 1);
    }
    else /* If we lost only spaces then produce a NOTE, not a WARNING */
      set_warning(MYSQL_ERROR::WARN_LEVEL_NOTE, WARN_DATA_TRUNCATED, 1);
    return 2;
  }
  return 0;
}


	/* Copy a string and fill with space */

int Field_string::store(const char *from,uint length,CHARSET_INFO *cs)
{
  uint copy_length;
  const char *well_formed_error_pos;
  const char *cannot_convert_error_pos;
  const char *from_end_pos;

  /* See the comment for Field_long::store(long long) */
  assert(table->in_use == current_thd);

  copy_length= well_formed_copy_nchars(field_charset,
                                       (char*) ptr, field_length,
                                       cs, from, length,
                                       field_length / field_charset->mbmaxlen,
                                       &well_formed_error_pos,
                                       &cannot_convert_error_pos,
                                       &from_end_pos);

  /* Append spaces if the string was shorter than the field. */
  if (copy_length < field_length)
    field_charset->cset->fill(field_charset,(char*) ptr+copy_length,
                              field_length-copy_length,
                              field_charset->pad_char);

  if (check_string_copy_error(this, well_formed_error_pos,
                              cannot_convert_error_pos, from + length, cs))
    return 2;

  return report_if_important_data(from_end_pos, from + length);
}


/**
  Store double value in Field_string or Field_varstring.

  Pretty prints double number into field_length characters buffer.

  @param nr            number
*/

int Field_str::store(double nr)
{
  char buff[DOUBLE_TO_STRING_CONVERSION_BUFFER_SIZE];
  uint local_char_length= field_length / charset()->mbmaxlen;
  size_t length;
  my_bool error;

  length= my_gcvt(nr, MY_GCVT_ARG_DOUBLE, local_char_length, buff, &error);
  if (error)
  {
    if (table->in_use->abort_on_warning)
      set_warning(MYSQL_ERROR::WARN_LEVEL_ERROR, ER_DATA_TOO_LONG, 1);
    else
      set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, WARN_DATA_TRUNCATED, 1);
  }
  return store(buff, length, charset());
}


uint Field::is_equal(Create_field *new_field)
{
  return (new_field->sql_type == real_type());
}


/* If one of the fields is binary and the other one isn't return 1 else 0 */

bool Field_str::compare_str_field_flags(Create_field *new_field, uint32 flag_arg)
{
  return (((new_field->flags & (BINCMP_FLAG | BINARY_FLAG)) &&
          !(flag_arg & (BINCMP_FLAG | BINARY_FLAG))) ||
         (!(new_field->flags & (BINCMP_FLAG | BINARY_FLAG)) &&
          (flag_arg & (BINCMP_FLAG | BINARY_FLAG))));
}


uint Field_str::is_equal(Create_field *new_field)
{
  if (compare_str_field_flags(new_field, flags))
    return 0;

  return ((new_field->sql_type == real_type()) &&
	  new_field->charset == field_charset &&
	  new_field->length == max_display_length());
}


int Field_string::store(longlong nr, bool unsigned_val)
{
  char buff[64];
  int  l;
  CHARSET_INFO *cs=charset();
  l= (cs->cset->longlong10_to_str)(cs,buff,sizeof(buff),
                                   unsigned_val ? 10 : -10, nr);
  return Field_string::store(buff,(uint)l,cs);
}


int Field_longstr::store_decimal(const my_decimal *d)
{
  char buff[DECIMAL_MAX_STR_LENGTH+1];
  String str(buff, sizeof(buff), &my_charset_bin);
  my_decimal2string(E_DEC_FATAL_ERROR, d, 0, 0, 0, &str);
  return store(str.ptr(), str.length(), str.charset());
}

uint32 Field_longstr::max_data_length() const
{
  return field_length + (field_length > 255 ? 2 : 1);
}


double Field_string::val_real(void)
{
  int error;
  char *end;
  CHARSET_INFO *cs= charset();
  double result;
  
  result=  my_strntod(cs,(char*) ptr,field_length,&end,&error);
  if (!table->in_use->no_errors &&
      (error || (field_length != (uint32)(end - (char*) ptr) && 
                 !check_if_only_end_space(cs, end,
                                          (char*) ptr + field_length))))
  {
    char buf[DOUBLE_TO_STRING_CONVERSION_BUFFER_SIZE];
    String tmp(buf, sizeof(buf), cs);
    tmp.copy((char*) ptr, field_length, cs);
    push_warning_printf(current_thd, MYSQL_ERROR::WARN_LEVEL_WARN,
                        ER_TRUNCATED_WRONG_VALUE, 
                        ER(ER_TRUNCATED_WRONG_VALUE),
                        "DOUBLE", tmp.c_ptr());
  }
  return result;
}


longlong Field_string::val_int(void)
{
  int error;
  char *end;
  CHARSET_INFO *cs= charset();
  longlong result;

  result= my_strntoll(cs, (char*) ptr,field_length,10,&end,&error);
  if (!table->in_use->no_errors &&
      (error || (field_length != (uint32)(end - (char*) ptr) && 
                 !check_if_only_end_space(cs, end,
                                          (char*) ptr + field_length))))
  {
    char buf[LONGLONG_TO_STRING_CONVERSION_BUFFER_SIZE];
    String tmp(buf, sizeof(buf), cs);
    tmp.copy((char*) ptr, field_length, cs);
    push_warning_printf(current_thd, MYSQL_ERROR::WARN_LEVEL_WARN,
                        ER_TRUNCATED_WRONG_VALUE, 
                        ER(ER_TRUNCATED_WRONG_VALUE),
                        "INTEGER", tmp.c_ptr());
  }
  return result;
}


String *Field_string::val_str(String *val_buffer __attribute__((unused)),
			      String *val_ptr)
{
  /* See the comment for Field_long::store(long long) */
  assert(table->in_use == current_thd);
  uint length;
  if (table->in_use->variables.sql_mode &
      MODE_PAD_CHAR_TO_FULL_LENGTH)
    length= my_charpos(field_charset, ptr, ptr + field_length, field_length);
  else
    length= field_charset->cset->lengthsp(field_charset, (const char*) ptr,
                                          field_length);
  val_ptr->set((const char*) ptr, length, field_charset);
  return val_ptr;
}


my_decimal *Field_string::val_decimal(my_decimal *decimal_value)
{
  int err= str2my_decimal(E_DEC_FATAL_ERROR, (char*) ptr, field_length,
                          charset(), decimal_value);
  if (!table->in_use->no_errors && err)
  {
    char buf[DECIMAL_TO_STRING_CONVERSION_BUFFER_SIZE];
    CHARSET_INFO *cs= charset();
    String tmp(buf, sizeof(buf), cs);
    tmp.copy((char*) ptr, field_length, cs);
    push_warning_printf(current_thd, MYSQL_ERROR::WARN_LEVEL_WARN,
                        ER_TRUNCATED_WRONG_VALUE, 
                        ER(ER_TRUNCATED_WRONG_VALUE),
                        "DECIMAL", tmp.c_ptr());
  }

  return decimal_value;
}


int Field_string::cmp(const uchar *a_ptr, const uchar *b_ptr)
{
  uint a_len, b_len;

  if (field_charset->mbmaxlen != 1)
  {
    uint char_len= field_length/field_charset->mbmaxlen;
    a_len= my_charpos(field_charset, a_ptr, a_ptr + field_length, char_len);
    b_len= my_charpos(field_charset, b_ptr, b_ptr + field_length, char_len);
  }
  else
    a_len= b_len= field_length;
  /*
    We have to remove end space to be able to compare multi-byte-characters
    like in latin_de 'ae' and 0xe4
  */
  return field_charset->coll->strnncollsp(field_charset,
                                          a_ptr, a_len,
                                          b_ptr, b_len,
                                          0);
}


void Field_string::sort_string(uchar *to,uint length)
{
  uint tmp= my_strnxfrm(field_charset,
                                 to, length,
                                 ptr, field_length);
  assert(tmp == length);
}


void Field_string::sql_type(String &res) const
{
  THD *thd= table->in_use;
  CHARSET_INFO *cs=res.charset();
  ulong length;

  length= cs->cset->snprintf(cs,(char*) res.ptr(),
                             res.alloced_length(), "%s(%d)",
                             ((type() == MYSQL_TYPE_VAR_STRING &&
                               !thd->variables.new_mode) ?
                              (has_charset() ? "varchar" : "varbinary") :
			      (has_charset() ? "char" : "binary")),
                             (int) field_length / charset()->mbmaxlen);
  res.length(length);
  if ((thd->variables.sql_mode & (MODE_MYSQL323 | MODE_MYSQL40)) &&
      has_charset() && (charset()->state & MY_CS_BINSORT))
    res.append(STRING_WITH_LEN(" binary"));
}


uchar *Field_string::pack(uchar *to, const uchar *from,
                          uint max_length,
                          bool low_byte_first __attribute__((unused)))
{
  uint length=      min(field_length,max_length);
  uint local_char_length= max_length/field_charset->mbmaxlen;
  if (length > local_char_length)
    local_char_length= my_charpos(field_charset, from, from+length,
                                  local_char_length);
  set_if_smaller(length, local_char_length);
  while (length && from[length-1] == field_charset->pad_char)
    length--;

  // Length always stored little-endian
  *to++= (uchar) length;
  if (field_length > 255)
    *to++= (uchar) (length >> 8);

  // Store the actual bytes of the string
  memcpy(to, from, length);
  return to+length;
}


/**
   Unpack a string field from row data.

   This method is used to unpack a string field from a master whose size 
   of the field is less than that of the slave. Note that there can be a
   variety of field types represented with this class. Certain types like
   ENUM or SET are processed differently. Hence, the upper byte of the 
   @c param_data argument contains the result of field->real_type() from
   the master.

   @param   to         Destination of the data
   @param   from       Source of the data
   @param   param_data Real type (upper) and length (lower) values

   @return  New pointer into memory based on from + length of the data
*/
const uchar *
Field_string::unpack(uchar *to,
                     const uchar *from,
                     uint param_data,
                     bool low_byte_first __attribute__((unused)))
{
  uint from_length=
    param_data ? min(param_data & 0x00ff, field_length) : field_length;
  uint length;

  if (from_length > 255)
  {
    length= uint2korr(from);
    from+= 2;
  }
  else
    length= (uint) *from++;

  memcpy(to, from, length);
  // Pad the string with the pad character of the fields charset
  bfill(to + length, field_length - length, field_charset->pad_char);
  return from+length;
}


/**
   Save the field metadata for string fields.

   Saves the real type in the first byte and the field length in the 
   second byte of the field metadata array at index of *metadata_ptr and
   *(metadata_ptr + 1).

   @param   metadata_ptr   First byte of field metadata

   @returns number of bytes written to metadata_ptr
*/
int Field_string::do_save_field_metadata(uchar *metadata_ptr)
{
  *metadata_ptr= real_type();
  *(metadata_ptr + 1)= field_length;
  return 2;
}


/*
  Compare two packed keys

  SYNOPSIS
    pack_cmp()
     a			New key
     b			Original key
     length		Key length
     insert_or_update	1 if this is an insert or update

  RETURN
    < 0	  a < b
    0	  a = b
    > 0   a > b
*/

int Field_string::pack_cmp(const uchar *a, const uchar *b, uint length,
                           my_bool insert_or_update)
{
  uint a_length, b_length;
  if (length > 255)
  {
    a_length= uint2korr(a);
    b_length= uint2korr(b);
    a+= 2;
    b+= 2;
  }
  else
  {
    a_length= (uint) *a++;
    b_length= (uint) *b++;
  }
  return field_charset->coll->strnncollsp(field_charset,
                                          a, a_length,
                                          b, b_length,
                                          insert_or_update);
}


/**
  Compare a packed key against row.

  @param key		        Original key
  @param length		Key length. (May be less than field length)
  @param insert_or_update	1 if this is an insert or update

  @return
    < 0	  row < key
  @return
    0	  row = key
  @return
    > 0   row > key
*/

int Field_string::pack_cmp(const uchar *key, uint length,
                           my_bool insert_or_update)
{
  uint row_length, local_key_length;
  uchar *end;
  if (length > 255)
  {
    local_key_length= uint2korr(key);
    key+= 2;
  }
  else
    local_key_length= (uint) *key++;
  
  /* Only use 'length' of key, not field_length */
  end= ptr + length;
  while (end > ptr && end[-1] == ' ')
    end--;
  row_length= (uint) (end - ptr);

  return field_charset->coll->strnncollsp(field_charset,
                                          ptr, row_length,
                                          key, local_key_length,
                                          insert_or_update);
}


uint Field_string::packed_col_length(const uchar *data_ptr, uint length)
{
  if (length > 255)
    return uint2korr(data_ptr)+2;
  return (uint) *data_ptr + 1;
}


uint Field_string::max_packed_col_length(uint max_length)
{
  return (max_length > 255 ? 2 : 1)+max_length;
}


uint Field_string::get_key_image(uchar *buff,
                                 uint length,
                                 imagetype type_arg __attribute__((__unused__)))
{
  uint bytes = my_charpos(field_charset, (char*) ptr,
                          (char*) ptr + field_length,
                          length / field_charset->mbmaxlen);
  memcpy(buff, ptr, bytes);
  if (bytes < length)
    field_charset->cset->fill(field_charset, (char*) buff + bytes,
                              length - bytes, field_charset->pad_char);
  return bytes;
}


Field *Field_string::new_field(MEM_ROOT *root, struct st_table *new_table,
                               bool keep_type)
{
  Field *field;
  if (type() != MYSQL_TYPE_VAR_STRING || keep_type)
    field= Field::new_field(root, new_table, keep_type);
  else if ((field= new Field_varstring(field_length, maybe_null(), field_name,
                                       new_table->s, charset())))
  {
    /*
      Old VARCHAR field which should be modified to a VARCHAR on copy
      This is done to ensure that ALTER TABLE will convert old VARCHAR fields
      to now VARCHAR fields.
    */
    field->init(new_table);
    /*
      Normally orig_table is different from table only if field was created
      via ::new_field.  Here we alter the type of field, so ::new_field is
      not applicable. But we still need to preserve the original field
      metadata for the client-server protocol.
    */
    field->orig_table= orig_table;
  }
  return field;
}


/****************************************************************************
  VARCHAR type
  Data in field->ptr is stored as:
    1 or 2 bytes length-prefix-header  (from Field_varstring::length_bytes)
    data

  NOTE:
  When VARCHAR is stored in a key (for handler::index_read() etc) it's always
  stored with a 2 byte prefix. (Just like blob keys).

  Normally length_bytes is calculated as (field_length < 256 : 1 ? 2)
  The exception is if there is a prefix key field that is part of a long
  VARCHAR, in which case field_length for this may be 1 but the length_bytes
  is 2.
****************************************************************************/

const uint Field_varstring::MAX_SIZE= UINT_MAX16;

/**
   Save the field metadata for varstring fields.

   Saves the field length in the first byte. Note: may consume
   2 bytes. Caller must ensure second byte is contiguous with
   first byte (e.g. array index 0,1).

   @param   metadata_ptr   First byte of field metadata

   @returns number of bytes written to metadata_ptr
*/
int Field_varstring::do_save_field_metadata(uchar *metadata_ptr)
{
  char *ptr= (char *)metadata_ptr;
  assert(field_length <= 65535);
  int2store(ptr, field_length);
  return 2;
}

int Field_varstring::store(const char *from,uint length,CHARSET_INFO *cs)
{
  uint copy_length;
  const char *well_formed_error_pos;
  const char *cannot_convert_error_pos;
  const char *from_end_pos;

  copy_length= well_formed_copy_nchars(field_charset,
                                       (char*) ptr + length_bytes,
                                       field_length,
                                       cs, from, length,
                                       field_length / field_charset->mbmaxlen,
                                       &well_formed_error_pos,
                                       &cannot_convert_error_pos,
                                       &from_end_pos);

  if (length_bytes == 1)
    *ptr= (uchar) copy_length;
  else
    int2store(ptr, copy_length);

  if (check_string_copy_error(this, well_formed_error_pos,
                              cannot_convert_error_pos, from + length, cs))
    return 2;

  return report_if_important_data(from_end_pos, from + length);
}


int Field_varstring::store(longlong nr, bool unsigned_val)
{
  char buff[64];
  uint  length;
  length= (uint) (field_charset->cset->longlong10_to_str)(field_charset,
                                                          buff,
                                                          sizeof(buff),
                                                          (unsigned_val ? 10:
                                                           -10),
                                                           nr);
  return Field_varstring::store(buff, length, field_charset);
}


double Field_varstring::val_real(void)
{
  int not_used;
  char *end_not_used;
  uint length= length_bytes == 1 ? (uint) *ptr : uint2korr(ptr);
  return my_strntod(field_charset, (char*) ptr+length_bytes, length,
                    &end_not_used, &not_used);
}


longlong Field_varstring::val_int(void)
{
  int not_used;
  char *end_not_used;
  uint length= length_bytes == 1 ? (uint) *ptr : uint2korr(ptr);
  return my_strntoll(field_charset, (char*) ptr+length_bytes, length, 10,
                     &end_not_used, &not_used);
}

String *Field_varstring::val_str(String *val_buffer __attribute__((unused)),
				 String *val_ptr)
{
  uint length=  length_bytes == 1 ? (uint) *ptr : uint2korr(ptr);
  val_ptr->set((const char*) ptr+length_bytes, length, field_charset);
  return val_ptr;
}


my_decimal *Field_varstring::val_decimal(my_decimal *decimal_value)
{
  uint length= length_bytes == 1 ? (uint) *ptr : uint2korr(ptr);
  str2my_decimal(E_DEC_FATAL_ERROR, (char*) ptr+length_bytes, length,
                 charset(), decimal_value);
  return decimal_value;
}


int Field_varstring::cmp_max(const uchar *a_ptr, const uchar *b_ptr,
                             uint max_len)
{
  uint a_length, b_length;
  int diff;

  if (length_bytes == 1)
  {
    a_length= (uint) *a_ptr;
    b_length= (uint) *b_ptr;
  }
  else
  {
    a_length= uint2korr(a_ptr);
    b_length= uint2korr(b_ptr);
  }
  set_if_smaller(a_length, max_len);
  set_if_smaller(b_length, max_len);
  diff= field_charset->coll->strnncollsp(field_charset,
                                         a_ptr+
                                         length_bytes,
                                         a_length,
                                         b_ptr+
                                         length_bytes,
                                         b_length,0);
  return diff;
}


/**
  @note
    varstring and blob keys are ALWAYS stored with a 2 byte length prefix
*/

int Field_varstring::key_cmp(const uchar *key_ptr, uint max_key_length)
{
  uint length=  length_bytes == 1 ? (uint) *ptr : uint2korr(ptr);
  uint local_char_length= max_key_length / field_charset->mbmaxlen;

  local_char_length= my_charpos(field_charset, ptr + length_bytes,
                          ptr + length_bytes + length, local_char_length);
  set_if_smaller(length, local_char_length);
  return field_charset->coll->strnncollsp(field_charset, 
                                          ptr + length_bytes,
                                          length,
                                          key_ptr+
                                          HA_KEY_BLOB_LENGTH,
                                          uint2korr(key_ptr), 0);
}


/**
  Compare to key segments (always 2 byte length prefix).

  @note
    This is used only to compare key segments created for index_read().
    (keys are created and compared in key.cc)
*/

int Field_varstring::key_cmp(const uchar *a,const uchar *b)
{
  return field_charset->coll->strnncollsp(field_charset,
                                          a + HA_KEY_BLOB_LENGTH,
                                          uint2korr(a),
                                          b + HA_KEY_BLOB_LENGTH,
                                          uint2korr(b),
                                          0);
}


void Field_varstring::sort_string(uchar *to,uint length)
{
  uint tot_length=  length_bytes == 1 ? (uint) *ptr : uint2korr(ptr);

  if (field_charset == &my_charset_bin)
  {
    /* Store length last in high-byte order to sort longer strings first */
    if (length_bytes == 1)
      to[length-1]= tot_length;
    else
      mi_int2store(to+length-2, tot_length);
    length-= length_bytes;
  }
 
  tot_length= my_strnxfrm(field_charset,
			  to, length, ptr + length_bytes,
			  tot_length);
  assert(tot_length == length);
}


enum ha_base_keytype Field_varstring::key_type() const
{
  enum ha_base_keytype res;

  if (binary())
    res= length_bytes == 1 ? HA_KEYTYPE_VARBINARY1 : HA_KEYTYPE_VARBINARY2;
  else
    res= length_bytes == 1 ? HA_KEYTYPE_VARTEXT1 : HA_KEYTYPE_VARTEXT2;
  return res;
}


void Field_varstring::sql_type(String &res) const
{
  THD *thd= table->in_use;
  CHARSET_INFO *cs=res.charset();
  ulong length;

  length= cs->cset->snprintf(cs,(char*) res.ptr(),
                             res.alloced_length(), "%s(%d)",
                              (has_charset() ? "varchar" : "varbinary"),
                             (int) field_length / charset()->mbmaxlen);
  res.length(length);
  if ((thd->variables.sql_mode & (MODE_MYSQL323 | MODE_MYSQL40)) &&
      has_charset() && (charset()->state & MY_CS_BINSORT))
    res.append(STRING_WITH_LEN(" binary"));
}


uint32 Field_varstring::data_length()
{
  return length_bytes == 1 ? (uint32) *ptr : uint2korr(ptr);
}

uint32 Field_varstring::used_length()
{
  return length_bytes == 1 ? 1 + (uint32) (uchar) *ptr : 2 + uint2korr(ptr);
}

/*
  Functions to create a packed row.
  Here the number of length bytes are depending on the given max_length
*/

uchar *Field_varstring::pack(uchar *to, const uchar *from,
                             uint max_length,
                             bool low_byte_first __attribute__((unused)))
{
  uint length= length_bytes == 1 ? (uint) *from : uint2korr(from);
  set_if_smaller(max_length, field_length);
  if (length > max_length)
    length=max_length;

  /* Length always stored little-endian */
  *to++= length & 0xFF;
  if (max_length > 255)
    *to++= (length >> 8) & 0xFF;

  /* Store bytes of string */
  if (length > 0)
    memcpy(to, from+length_bytes, length);
  return to+length;
}


uchar *
Field_varstring::pack_key(uchar *to, const uchar *key, uint max_length,
                          bool low_byte_first __attribute__((unused)))
{
  uint length=  length_bytes == 1 ? (uint) *key : uint2korr(key);
  uint local_char_length= ((field_charset->mbmaxlen > 1) ?
                     max_length/field_charset->mbmaxlen : max_length);
  key+= length_bytes;
  if (length > local_char_length)
  {
    local_char_length= my_charpos(field_charset, key, key+length,
                                  local_char_length);
    set_if_smaller(length, local_char_length);
  }
  *to++= (char) (length & 255);
  if (max_length > 255)
    *to++= (char) (length >> 8);
  if (length)
    memcpy(to, key, length);
  return to+length;
}


/**
  Unpack a key into a record buffer.

  A VARCHAR key has a maximum size of 64K-1.
  In its packed form, the length field is one or two bytes long,
  depending on 'max_length'.

  @param to                          Pointer into the record buffer.
  @param key                         Pointer to the packed key.
  @param max_length                  Key length limit from key description.

  @return
    Pointer to end of 'key' (To the next key part if multi-segment key)
*/

const uchar *
Field_varstring::unpack_key(uchar *to __attribute__((__unused__)),
                            const uchar *key, uint max_length,
                            bool low_byte_first __attribute__((unused)))
{
  /* get length of the blob key */
  uint32 length= *key++;
  if (max_length > 255)
    length+= (*key++) << 8;

  /* put the length into the record buffer */
  if (length_bytes == 1)
    *ptr= (uchar) length;
  else
    int2store(ptr, length);
  memcpy(ptr + length_bytes, key, length);
  return key + length;
}

/**
  Create a packed key that will be used for storage in the index tree.

  @param to		Store packed key segment here
  @param from		Key segment (as given to index_read())
  @param max_length  	Max length of key

  @return
    end of key storage
*/

uchar *
Field_varstring::pack_key_from_key_image(uchar *to, const uchar *from, uint max_length,
                                         bool low_byte_first __attribute__((unused)))
{
  /* Key length is always stored as 2 bytes */
  uint length= uint2korr(from);
  if (length > max_length)
    length= max_length;
  *to++= (char) (length & 255);
  if (max_length > 255)
    *to++= (char) (length >> 8);
  if (length)
    memcpy(to, from+HA_KEY_BLOB_LENGTH, length);
  return to+length;
}


/**
   Unpack a varstring field from row data.

   This method is used to unpack a varstring field from a master
   whose size of the field is less than that of the slave.

   @note
   The string length is always packed little-endian.
  
   @param   to         Destination of the data
   @param   from       Source of the data
   @param   param_data Length bytes from the master's field data

   @return  New pointer into memory based on from + length of the data
*/
const uchar *
Field_varstring::unpack(uchar *to, const uchar *from,
                        uint param_data,
                        bool low_byte_first __attribute__((unused)))
{
  uint length;
  uint l_bytes= (param_data && (param_data < field_length)) ? 
                (param_data <= 255) ? 1 : 2 : length_bytes;
  if (l_bytes == 1)
  {
    to[0]= *from++;
    length= to[0];
    if (length_bytes == 2)
      to[1]= 0;
  }
  else /* l_bytes == 2 */
  {
    length= uint2korr(from);
    to[0]= *from++;
    to[1]= *from++;
  }
  if (length)
    memcpy(to+ length_bytes, from, length);
  return from+length;
}


int Field_varstring::pack_cmp(const uchar *a, const uchar *b,
                              uint key_length_arg,
                              my_bool insert_or_update)
{
  uint a_length, b_length;
  if (key_length_arg > 255)
  {
    a_length=uint2korr(a); a+= 2;
    b_length=uint2korr(b); b+= 2;
  }
  else
  {
    a_length= (uint) *a++;
    b_length= (uint) *b++;
  }
  return field_charset->coll->strnncollsp(field_charset,
                                          a, a_length,
                                          b, b_length,
                                          insert_or_update);
}


int Field_varstring::pack_cmp(const uchar *b, uint key_length_arg,
                              my_bool insert_or_update)
{
  uchar *a= ptr+ length_bytes;
  uint a_length=  length_bytes == 1 ? (uint) *ptr : uint2korr(ptr);
  uint b_length;
  uint local_char_length= ((field_charset->mbmaxlen > 1) ?
                           key_length_arg / field_charset->mbmaxlen :
                           key_length_arg);

  if (key_length_arg > 255)
  {
    b_length=uint2korr(b); b+= HA_KEY_BLOB_LENGTH;
  }
  else
    b_length= (uint) *b++;

  if (a_length > local_char_length)
  {
    local_char_length= my_charpos(field_charset, a, a+a_length,
                                  local_char_length);
    set_if_smaller(a_length, local_char_length);
  }

  return field_charset->coll->strnncollsp(field_charset,
                                          a, a_length,
                                          b, b_length,
                                          insert_or_update);
}


uint Field_varstring::packed_col_length(const uchar *data_ptr, uint length)
{
  if (length > 255)
    return uint2korr(data_ptr)+2;
  return (uint) *data_ptr + 1;
}


uint Field_varstring::max_packed_col_length(uint max_length)
{
  return (max_length > 255 ? 2 : 1)+max_length;
}

uint Field_varstring::get_key_image(uchar *buff,
                                    uint length,
                                    imagetype type __attribute__((__unused__)))
{
  uint f_length=  length_bytes == 1 ? (uint) *ptr : uint2korr(ptr);
  uint local_char_length= length / field_charset->mbmaxlen;
  uchar *pos= ptr+length_bytes;
  local_char_length= my_charpos(field_charset, pos, pos + f_length,
                                local_char_length);
  set_if_smaller(f_length, local_char_length);
  /* Key is always stored with 2 bytes */
  int2store(buff,f_length);
  memcpy(buff+HA_KEY_BLOB_LENGTH, pos, f_length);
  if (f_length < length)
  {
    /*
      Must clear this as we do a memcmp in opt_range.cc to detect
      identical keys
    */
    bzero(buff+HA_KEY_BLOB_LENGTH+f_length, (length-f_length));
  }
  return HA_KEY_BLOB_LENGTH+f_length;
}


void Field_varstring::set_key_image(const uchar *buff,uint length)
{
  length= uint2korr(buff);			// Real length is here
  (void) Field_varstring::store((const char*) buff+HA_KEY_BLOB_LENGTH, length,
                                field_charset);
}


int Field_varstring::cmp_binary(const uchar *a_ptr, const uchar *b_ptr,
                                uint32 max_length)
{
  uint32 a_length,b_length;

  if (length_bytes == 1)
  {
    a_length= (uint) *a_ptr;
    b_length= (uint) *b_ptr;
  }
  else
  {
    a_length= uint2korr(a_ptr);
    b_length= uint2korr(b_ptr);
  }
  set_if_smaller(a_length, max_length);
  set_if_smaller(b_length, max_length);
  if (a_length != b_length)
    return 1;
  return memcmp(a_ptr+length_bytes, b_ptr+length_bytes, a_length);
}


Field *Field_varstring::new_field(MEM_ROOT *root, struct st_table *new_table,
                                  bool keep_type)
{
  Field_varstring *res= (Field_varstring*) Field::new_field(root, new_table,
                                                            keep_type);
  if (res)
    res->length_bytes= length_bytes;
  return res;
}


Field *Field_varstring::new_key_field(MEM_ROOT *root,
                                      struct st_table *new_table,
                                      uchar *new_ptr, uchar *new_null_ptr,
                                      uint new_null_bit)
{
  Field_varstring *res;
  if ((res= (Field_varstring*) Field::new_key_field(root,
                                                    new_table,
                                                    new_ptr,
                                                    new_null_ptr,
                                                    new_null_bit)))
  {
    /* Keys length prefixes are always packed with 2 bytes */
    res->length_bytes= 2;
  }
  return res;
}


uint Field_varstring::is_equal(Create_field *new_field)
{
  if (new_field->sql_type == real_type() &&
      new_field->charset == field_charset)
  {
    if (new_field->length == max_display_length())
      return IS_EQUAL_YES;
    if (new_field->length > max_display_length() &&
	((new_field->length <= 255 && max_display_length() <= 255) ||
	 (new_field->length > 255 && max_display_length() > 255)))
      return IS_EQUAL_PACK_LENGTH; // VARCHAR, longer variable length
  }
  return IS_EQUAL_NO;
}


void Field_varstring::hash(ulong *nr, ulong *nr2)
{
  if (is_null())
  {
    *nr^= (*nr << 1) | 1;
  }
  else
  {
    uint len=  length_bytes == 1 ? (uint) *ptr : uint2korr(ptr);
    CHARSET_INFO *cs= charset();
    cs->coll->hash_sort(cs, ptr + length_bytes, len, nr, nr2);
  }
}


/****************************************************************************
** blob type
** A blob is saved as a length and a pointer. The length is stored in the
** packlength slot and may be from 1-4.
****************************************************************************/

Field_blob::Field_blob(uchar *ptr_arg, uchar *null_ptr_arg, uchar null_bit_arg,
		       enum utype unireg_check_arg, const char *field_name_arg,
                       TABLE_SHARE *share, uint blob_pack_length,
		       CHARSET_INFO *cs)
  :Field_longstr(ptr_arg, BLOB_PACK_LENGTH_TO_MAX_LENGH(blob_pack_length),
                 null_ptr_arg, null_bit_arg, unireg_check_arg, field_name_arg,
                 cs),
   packlength(blob_pack_length)
{
  flags|= BLOB_FLAG;
  share->blob_fields++;
  /* TODO: why do not fill table->s->blob_field array here? */
}


void Field_blob::store_length(uchar *i_ptr,
                              uint i_packlength,
                              uint32 i_number,
                              bool low_byte_first __attribute__((__unused__)))
{
  switch (i_packlength) {
  case 1:
    i_ptr[0]= (uchar) i_number;
    break;
  case 2:
#ifdef WORDS_BIGENDIAN
    if (low_byte_first)
    {
      int2store(i_ptr,(unsigned short) i_number);
    }
    else
#endif
      shortstore(i_ptr,(unsigned short) i_number);
    break;
  case 3:
    int3store(i_ptr,i_number);
    break;
  case 4:
#ifdef WORDS_BIGENDIAN
    if (low_byte_first)
    {
      int4store(i_ptr,i_number);
    }
    else
#endif
      longstore(i_ptr,i_number);
  }
}


uint32 Field_blob::get_length(const uchar *pos,
                              uint packlength_arg,
                              bool low_byte_first __attribute__((__unused__)))
{
  switch (packlength_arg) {
  case 1:
    return (uint32) pos[0];
  case 2:
    {
      uint16 tmp;
#ifdef WORDS_BIGENDIAN
      if (low_byte_first)
	tmp=sint2korr(pos);
      else
#endif
	shortget(tmp,pos);
      return (uint32) tmp;
    }
  case 3:
    return (uint32) uint3korr(pos);
  case 4:
    {
      uint32 tmp;
#ifdef WORDS_BIGENDIAN
      if (low_byte_first)
	tmp=uint4korr(pos);
      else
#endif
	longget(tmp,pos);
      return (uint32) tmp;
    }
  }
  return 0;					// Impossible
}


/**
  Put a blob length field into a record buffer.

  Depending on the maximum length of a blob, its length field is
  put into 1 to 4 bytes. This is a property of the blob object,
  described by 'packlength'.

  @param pos                 Pointer into the record buffer.
  @param length              The length value to put.
*/

void Field_blob::put_length(uchar *pos, uint32 length)
{
  switch (packlength) {
  case 1:
    *pos= (char) length;
    break;
  case 2:
    int2store(pos, length);
    break;
  case 3:
    int3store(pos, length);
    break;
  case 4:
    int4store(pos, length);
    break;
  }
}


int Field_blob::store(const char *from,uint length,CHARSET_INFO *cs)
{
  uint copy_length, new_length;
  const char *well_formed_error_pos;
  const char *cannot_convert_error_pos;
  const char *from_end_pos, *tmp;
  char buff[STRING_BUFFER_USUAL_SIZE];
  String tmpstr(buff,sizeof(buff), &my_charset_bin);

  if (!length)
  {
    bzero(ptr,Field_blob::pack_length());
    return 0;
  }

  if (from == value.ptr())
  {
    uint32 dummy_offset;
    if (!String::needs_conversion(length, cs, field_charset, &dummy_offset))
    {
      Field_blob::store_length(length);
      bmove(ptr+packlength,(char*) &from,sizeof(char*));
      return 0;
    }
    if (tmpstr.copy(from, length, cs))
      goto oom_error;
    from= tmpstr.ptr();
  }

  new_length= min(max_data_length(), field_charset->mbmaxlen * length);
  if (value.alloc(new_length))
    goto oom_error;


  if (f_is_hex_escape(flags))
  {
    copy_length= my_copy_with_hex_escaping(field_charset,
                                           (char*) value.ptr(), new_length,
                                            from, length);
    Field_blob::store_length(copy_length);
    tmp= value.ptr();
    bmove(ptr + packlength, (uchar*) &tmp, sizeof(char*));
    return 0;
  }
  /*
    "length" is OK as "nchars" argument to well_formed_copy_nchars as this
    is never used to limit the length of the data. The cut of long data
    is done with the new_length value.
  */
  copy_length= well_formed_copy_nchars(field_charset,
                                       (char*) value.ptr(), new_length,
                                       cs, from, length,
                                       length,
                                       &well_formed_error_pos,
                                       &cannot_convert_error_pos,
                                       &from_end_pos);

  Field_blob::store_length(copy_length);
  tmp= value.ptr();
  bmove(ptr+packlength,(uchar*) &tmp,sizeof(char*));

  if (check_string_copy_error(this, well_formed_error_pos,
                              cannot_convert_error_pos, from + length, cs))
    return 2;

  return report_if_important_data(from_end_pos, from + length);

oom_error:
  /* Fatal OOM error */
  bzero(ptr,Field_blob::pack_length());
  return -1; 
}


int Field_blob::store(double nr)
{
  CHARSET_INFO *cs=charset();
  value.set_real(nr, NOT_FIXED_DEC, cs);
  return Field_blob::store(value.ptr(),(uint) value.length(), cs);
}


int Field_blob::store(longlong nr, bool unsigned_val)
{
  CHARSET_INFO *cs=charset();
  value.set_int(nr, unsigned_val, cs);
  return Field_blob::store(value.ptr(), (uint) value.length(), cs);
}


double Field_blob::val_real(void)
{
  int not_used;
  char *end_not_used, *blob;
  uint32 length;
  CHARSET_INFO *cs;

  memcpy_fixed(&blob,ptr+packlength,sizeof(char*));
  if (!blob)
    return 0.0;
  length= get_length(ptr);
  cs= charset();
  return my_strntod(cs, blob, length, &end_not_used, &not_used);
}


longlong Field_blob::val_int(void)
{
  int not_used;
  char *blob;
  memcpy_fixed(&blob,ptr+packlength,sizeof(char*));
  if (!blob)
    return 0;
  uint32 length=get_length(ptr);
  return my_strntoll(charset(),blob,length,10,NULL,&not_used);
}

String *Field_blob::val_str(String *val_buffer __attribute__((unused)),
			    String *val_ptr)
{
  char *blob;
  memcpy_fixed(&blob,ptr+packlength,sizeof(char*));
  if (!blob)
    val_ptr->set("",0,charset());	// A bit safer than ->length(0)
  else
    val_ptr->set((const char*) blob,get_length(ptr),charset());
  return val_ptr;
}


my_decimal *Field_blob::val_decimal(my_decimal *decimal_value)
{
  const char *blob;
  size_t length;
  memcpy_fixed(&blob, ptr+packlength, sizeof(const uchar*));
  if (!blob)
  {
    blob= "";
    length= 0;
  }
  else
    length= get_length(ptr);

  str2my_decimal(E_DEC_FATAL_ERROR, blob, length, charset(),
                 decimal_value);
  return decimal_value;
}


int Field_blob::cmp(const uchar *a,uint32 a_length, const uchar *b,
		    uint32 b_length)
{
  return field_charset->coll->strnncollsp(field_charset, 
                                          a, a_length, b, b_length,
                                          0);
}


int Field_blob::cmp_max(const uchar *a_ptr, const uchar *b_ptr,
                        uint max_length)
{
  uchar *blob1,*blob2;
  memcpy_fixed(&blob1,a_ptr+packlength,sizeof(char*));
  memcpy_fixed(&blob2,b_ptr+packlength,sizeof(char*));
  uint a_len= get_length(a_ptr), b_len= get_length(b_ptr);
  set_if_smaller(a_len, max_length);
  set_if_smaller(b_len, max_length);
  return Field_blob::cmp(blob1,a_len,blob2,b_len);
}


int Field_blob::cmp_binary(const uchar *a_ptr, const uchar *b_ptr,
			   uint32 max_length)
{
  char *a,*b;
  uint diff;
  uint32 a_length,b_length;
  memcpy_fixed(&a,a_ptr+packlength,sizeof(char*));
  memcpy_fixed(&b,b_ptr+packlength,sizeof(char*));
  a_length=get_length(a_ptr);
  if (a_length > max_length)
    a_length=max_length;
  b_length=get_length(b_ptr);
  if (b_length > max_length)
    b_length=max_length;
  diff=memcmp(a,b,min(a_length,b_length));
  return diff ? diff : (int) (a_length - b_length);
}


/* The following is used only when comparing a key */

uint Field_blob::get_key_image(uchar *buff,
                               uint length,
                               imagetype type_arg __attribute__((__unused__)))
{
  uint32 blob_length= get_length(ptr);
  uchar *blob;

  get_ptr(&blob);
  uint local_char_length= length / field_charset->mbmaxlen;
  local_char_length= my_charpos(field_charset, blob, blob + blob_length,
                          local_char_length);
  set_if_smaller(blob_length, local_char_length);

  if ((uint32) length > blob_length)
  {
    /*
      Must clear this as we do a memcmp in opt_range.cc to detect
      identical keys
    */
    bzero(buff+HA_KEY_BLOB_LENGTH+blob_length, (length-blob_length));
    length=(uint) blob_length;
  }
  int2store(buff,length);
  memcpy(buff+HA_KEY_BLOB_LENGTH, blob, length);
  return HA_KEY_BLOB_LENGTH+length;
}


void Field_blob::set_key_image(const uchar *buff,uint length)
{
  length= uint2korr(buff);
  (void) Field_blob::store((const char*) buff+HA_KEY_BLOB_LENGTH, length,
                           field_charset);
}


int Field_blob::key_cmp(const uchar *key_ptr, uint max_key_length)
{
  uchar *blob1;
  uint blob_length=get_length(ptr);
  memcpy_fixed(&blob1,ptr+packlength,sizeof(char*));
  CHARSET_INFO *cs= charset();
  uint local_char_length= max_key_length / cs->mbmaxlen;
  local_char_length= my_charpos(cs, blob1, blob1+blob_length,
                                local_char_length);
  set_if_smaller(blob_length, local_char_length);
  return Field_blob::cmp(blob1, blob_length,
			 key_ptr+HA_KEY_BLOB_LENGTH,
			 uint2korr(key_ptr));
}

int Field_blob::key_cmp(const uchar *a,const uchar *b)
{
  return Field_blob::cmp(a+HA_KEY_BLOB_LENGTH, uint2korr(a),
			 b+HA_KEY_BLOB_LENGTH, uint2korr(b));
}


/**
   Save the field metadata for blob fields.

   Saves the pack length in the first byte of the field metadata array
   at index of *metadata_ptr.

   @param   metadata_ptr   First byte of field metadata

   @returns number of bytes written to metadata_ptr
*/
int Field_blob::do_save_field_metadata(uchar *metadata_ptr)
{
  *metadata_ptr= pack_length_no_ptr();
  return 1;
}


uint32 Field_blob::sort_length() const
{
  return (uint32) (current_thd->variables.max_sort_length + 
                   (field_charset == &my_charset_bin ? 0 : packlength));
}


void Field_blob::sort_string(uchar *to,uint length)
{
  uchar *blob;
  uint blob_length=get_length();

  if (!blob_length)
    bzero(to,length);
  else
  {
    if (field_charset == &my_charset_bin)
    {
      uchar *pos;

      /*
        Store length of blob last in blob to shorter blobs before longer blobs
      */
      length-= packlength;
      pos= to+length;

      switch (packlength) {
      case 1:
        *pos= (char) blob_length;
        break;
      case 2:
        mi_int2store(pos, blob_length);
        break;
      case 3:
        mi_int3store(pos, blob_length);
        break;
      case 4:
        mi_int4store(pos, blob_length);
        break;
      }
    }
    memcpy_fixed(&blob,ptr+packlength,sizeof(char*));
    
    blob_length=my_strnxfrm(field_charset,
                            to, length, blob, blob_length);
    assert(blob_length == length);
  }
}


void Field_blob::sql_type(String &res) const
{
  const char *str;
  uint length;
  switch (packlength) {
  default: str="tiny"; length=4; break;
  case 2:  str="";     length=0; break;
  case 3:  str="medium"; length= 6; break;
  case 4:  str="long";  length=4; break;
  }
  res.set_ascii(str,length);
  if (charset() == &my_charset_bin)
    res.append(STRING_WITH_LEN("blob"));
  else
  {
    res.append(STRING_WITH_LEN("text"));
  }
}

uchar *Field_blob::pack(uchar *to, const uchar *from,
                        uint max_length, bool low_byte_first)
{
  uchar *save= ptr;
  ptr= (uchar*) from;
  uint32 length=get_length();			// Length of from string

  /*
    Store max length, which will occupy packlength bytes. If the max
    length given is smaller than the actual length of the blob, we
    just store the initial bytes of the blob.
  */
  store_length(to, packlength, min(length, max_length), low_byte_first);

  /*
    Store the actual blob data, which will occupy 'length' bytes.
   */
  if (length > 0)
  {
    get_ptr((uchar**) &from);
    memcpy(to+packlength, from,length);
  }
  ptr=save;					// Restore org row pointer
  return(to+packlength+length);
}


/**
   Unpack a blob field from row data.

   This method is used to unpack a blob field from a master whose size of 
   the field is less than that of the slave. Note: This method is included
   to satisfy inheritance rules, but is not needed for blob fields. It
   simply is used as a pass-through to the original unpack() method for
   blob fields.

   @param   to         Destination of the data
   @param   from       Source of the data
   @param   param_data @c true if base types should be stored in little-
                       endian format, @c false if native format should
                       be used.

   @return  New pointer into memory based on from + length of the data
*/
const uchar *Field_blob::unpack(uchar *to __attribute__((__unused__)),
                                const uchar *from,
                                uint param_data,
                                bool low_byte_first)
{
  uint const master_packlength=
    param_data > 0 ? param_data & 0xFF : packlength;
  uint32 const length= get_length(from, master_packlength, low_byte_first);
  bitmap_set_bit(table->write_set, field_index);
  store(reinterpret_cast<const char*>(from) + master_packlength,
        length, field_charset);
  return(from + master_packlength + length);
}

/* Keys for blobs are like keys on varchars */

int Field_blob::pack_cmp(const uchar *a, const uchar *b, uint key_length_arg,
                         my_bool insert_or_update)
{
  uint a_length, b_length;
  if (key_length_arg > 255)
  {
    a_length=uint2korr(a); a+=2;
    b_length=uint2korr(b); b+=2;
  }
  else
  {
    a_length= (uint) *a++;
    b_length= (uint) *b++;
  }
  return field_charset->coll->strnncollsp(field_charset,
                                          a, a_length,
                                          b, b_length,
                                          insert_or_update);
}


int Field_blob::pack_cmp(const uchar *b, uint key_length_arg,
                         my_bool insert_or_update)
{
  uchar *a;
  uint a_length, b_length;
  memcpy_fixed(&a,ptr+packlength,sizeof(char*));
  if (!a)
    return key_length_arg > 0 ? -1 : 0;

  a_length= get_length(ptr);
  if (key_length_arg > 255)
  {
    b_length= uint2korr(b); b+=2;
  }
  else
    b_length= (uint) *b++;
  return field_charset->coll->strnncollsp(field_charset,
                                          a, a_length,
                                          b, b_length,
                                          insert_or_update);
}

/** Create a packed key that will be used for storage from a MySQL row. */

uchar *
Field_blob::pack_key(uchar *to, const uchar *from, uint max_length,
                     bool low_byte_first __attribute__((unused)))
{
  uchar *save= ptr;
  ptr= (uchar*) from;
  uint32 length=get_length();        // Length of from string
  uint local_char_length= ((field_charset->mbmaxlen > 1) ?
                           max_length/field_charset->mbmaxlen : max_length);
  if (length)
    get_ptr((uchar**) &from);
  if (length > local_char_length)
    local_char_length= my_charpos(field_charset, from, from+length,
                                  local_char_length);
  set_if_smaller(length, local_char_length);
  *to++= (uchar) length;
  if (max_length > 255)				// 2 byte length
    *to++= (uchar) (length >> 8);
  memcpy(to, from, length);
  ptr=save;					// Restore org row pointer
  return to+length;
}


/**
  Unpack a blob key into a record buffer.

  A blob key has a maximum size of 64K-1.
  In its packed form, the length field is one or two bytes long,
  depending on 'max_length'.
  Depending on the maximum length of a blob, its length field is
  put into 1 to 4 bytes. This is a property of the blob object,
  described by 'packlength'.
  Blobs are internally stored apart from the record buffer, which
  contains a pointer to the blob buffer.


  @param to                          Pointer into the record buffer.
  @param from                        Pointer to the packed key.
  @param max_length                  Key length limit from key description.

  @return
    Pointer into 'from' past the last byte copied from packed key.
*/

const uchar *
Field_blob::unpack_key(uchar *to, const uchar *from, uint max_length,
                       bool low_byte_first __attribute__((unused)))
{
  /* get length of the blob key */
  uint32 length= *from++;
  if (max_length > 255)
    length+= *from++ << 8;

  /* put the length into the record buffer */
  put_length(to, length);

  /* put the address of the blob buffer or NULL */
  if (length)
    memcpy_fixed(to + packlength, &from, sizeof(from));
  else
    bzero(to + packlength, sizeof(from));

  /* point to first byte of next field in 'from' */
  return from + length;
}


/** Create a packed key that will be used for storage from a MySQL key. */

uchar *
Field_blob::pack_key_from_key_image(uchar *to, const uchar *from, uint max_length,
                                    bool low_byte_first __attribute__((unused)))
{
  uint length=uint2korr(from);
  if (length > max_length)
    length=max_length;
  *to++= (char) (length & 255);
  if (max_length > 255)
    *to++= (char) (length >> 8);
  if (length)
    memcpy(to, from+HA_KEY_BLOB_LENGTH, length);
  return to+length;
}


uint Field_blob::packed_col_length(const uchar *data_ptr, uint length)
{
  if (length > 255)
    return uint2korr(data_ptr)+2;
  return (uint) *data_ptr + 1;
}


uint Field_blob::max_packed_col_length(uint max_length)
{
  return (max_length > 255 ? 2 : 1)+max_length;
}


uint Field_blob::is_equal(Create_field *new_field)
{
  if (compare_str_field_flags(new_field, flags))
    return 0;

  return ((new_field->sql_type == get_blob_type_from_length(max_data_length()))
          && new_field->charset == field_charset &&
          ((Field_blob *)new_field->field)->max_data_length() ==
          max_data_length());
}


/****************************************************************************
** enum type.
** This is a string which only can have a selection of different values.
** If one uses this string in a number context one gets the type number.
****************************************************************************/

enum ha_base_keytype Field_enum::key_type() const
{
  switch (packlength) {
  default: return HA_KEYTYPE_BINARY;
  case 2: return HA_KEYTYPE_USHORT_INT;
  case 3: return HA_KEYTYPE_UINT24;
  case 4: return HA_KEYTYPE_ULONG_INT;
  case 8: return HA_KEYTYPE_ULONGLONG;
  }
}

void Field_enum::store_type(uint64_t value)
{
  switch (packlength) {
  case 1: ptr[0]= (uchar) value;  break;
  case 2:
#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
  {
    int2store(ptr,(unsigned short) value);
  }
  else
#endif
    shortstore(ptr,(unsigned short) value);
  break;
  case 3: int3store(ptr,(long) value); break;
  case 4:
#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
  {
    int4store(ptr,value);
  }
  else
#endif
    longstore(ptr,(long) value);
  break;
  case 8:
#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
  {
    int8store(ptr,value);
  }
  else
#endif
    longlongstore(ptr,value); break;
  }
}


/**
  @note
    Storing a empty string in a enum field gives a warning
    (if there isn't a empty value in the enum)
*/

int Field_enum::store(const char *from,uint length,CHARSET_INFO *cs)
{
  int err= 0;
  uint32 not_used;
  char buff[STRING_BUFFER_USUAL_SIZE];
  String tmpstr(buff,sizeof(buff), &my_charset_bin);

  /* Convert character set if necessary */
  if (String::needs_conversion(length, cs, field_charset, &not_used))
  { 
    uint dummy_errors;
    tmpstr.copy(from, length, cs, field_charset, &dummy_errors);
    from= tmpstr.ptr();
    length=  tmpstr.length();
  }

  /* Remove end space */
  length= field_charset->cset->lengthsp(field_charset, from, length);
  uint tmp=find_type2(typelib, from, length, field_charset);
  if (!tmp)
  {
    if (length < 6) // Can't be more than 99999 enums
    {
      /* This is for reading numbers with LOAD DATA INFILE */
      char *end;
      tmp=(uint) my_strntoul(cs,from,length,10,&end,&err);
      if (err || end != from+length || tmp > typelib->count)
      {
	tmp=0;
	set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, WARN_DATA_TRUNCATED, 1);
      }
      if (!table->in_use->count_cuted_fields)
        err= 0;
    }
    else
      set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, WARN_DATA_TRUNCATED, 1);
  }
  store_type((uint64_t) tmp);
  return err;
}


int Field_enum::store(double nr)
{
  return Field_enum::store((longlong) nr, false);
}


int Field_enum::store(longlong nr,
                      bool unsigned_val __attribute__((__unused__)))
{
  int error= 0;
  if ((uint64_t) nr > typelib->count || nr == 0)
  {
    set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, WARN_DATA_TRUNCATED, 1);
    if (nr != 0 || table->in_use->count_cuted_fields)
    {
      nr= 0;
      error= 1;
    }
  }
  store_type((uint64_t) (uint) nr);
  return error;
}


double Field_enum::val_real(void)
{
  return (double) Field_enum::val_int();
}


longlong Field_enum::val_int(void)
{
  switch (packlength) {
  case 1:
    return (longlong) ptr[0];
  case 2:
  {
    uint16 tmp;
#ifdef WORDS_BIGENDIAN
    if (table->s->db_low_byte_first)
      tmp=sint2korr(ptr);
    else
#endif
      shortget(tmp,ptr);
    return (longlong) tmp;
  }
  case 3:
    return (longlong) uint3korr(ptr);
  case 4:
  {
    uint32 tmp;
#ifdef WORDS_BIGENDIAN
    if (table->s->db_low_byte_first)
      tmp=uint4korr(ptr);
    else
#endif
      longget(tmp,ptr);
    return (longlong) tmp;
  }
  case 8:
  {
    longlong tmp;
#ifdef WORDS_BIGENDIAN
    if (table->s->db_low_byte_first)
      tmp=sint8korr(ptr);
    else
#endif
      longlongget(tmp,ptr);
    return tmp;
  }
  }
  return 0;					// impossible
}


/**
   Save the field metadata for enum fields.

   Saves the real type in the first byte and the pack length in the 
   second byte of the field metadata array at index of *metadata_ptr and
   *(metadata_ptr + 1).

   @param   metadata_ptr   First byte of field metadata

   @returns number of bytes written to metadata_ptr
*/
int Field_enum::do_save_field_metadata(uchar *metadata_ptr)
{
  *metadata_ptr= real_type();
  *(metadata_ptr + 1)= pack_length();
  return 2;
}


String *Field_enum::val_str(String *val_buffer __attribute__((unused)),
			    String *val_ptr)
{
  uint tmp=(uint) Field_enum::val_int();
  if (!tmp || tmp > typelib->count)
    val_ptr->set("", 0, field_charset);
  else
    val_ptr->set((const char*) typelib->type_names[tmp-1],
		 typelib->type_lengths[tmp-1],
		 field_charset);
  return val_ptr;
}

int Field_enum::cmp(const uchar *a_ptr, const uchar *b_ptr)
{
  uchar *old= ptr;
  ptr= (uchar*) a_ptr;
  uint64_t a=Field_enum::val_int();
  ptr= (uchar*) b_ptr;
  uint64_t b=Field_enum::val_int();
  ptr= old;
  return (a < b) ? -1 : (a > b) ? 1 : 0;
}

void Field_enum::sort_string(uchar *to,uint length __attribute__((unused)))
{
  uint64_t value=Field_enum::val_int();
  to+=packlength-1;
  for (uint i=0 ; i < packlength ; i++)
  {
    *to-- = (uchar) (value & 255);
    value>>=8;
  }
}


void Field_enum::sql_type(String &res) const
{
  char buffer[255];
  String enum_item(buffer, sizeof(buffer), res.charset());

  res.length(0);
  res.append(STRING_WITH_LEN("enum("));

  bool flag=0;
  uint *len= typelib->type_lengths;
  for (const char **pos= typelib->type_names; *pos; pos++, len++)
  {
    uint dummy_errors;
    if (flag)
      res.append(',');
    /* convert to res.charset() == utf8, then quote */
    enum_item.copy(*pos, *len, charset(), res.charset(), &dummy_errors);
    append_unescaped(&res, enum_item.ptr(), enum_item.length());
    flag= 1;
  }
  res.append(')');
}


Field *Field_enum::new_field(MEM_ROOT *root, struct st_table *new_table,
                             bool keep_type)
{
  Field_enum *res= (Field_enum*) Field::new_field(root, new_table, keep_type);
  if (res)
    res->typelib= copy_typelib(root, typelib);
  return res;
}


/*
   set type.
   This is a string which can have a collection of different values.
   Each string value is separated with a ','.
   For example "One,two,five"
   If one uses this string in a number context one gets the bits as a longlong
   number.
*/


int Field_set::store(const char *from,uint length,CHARSET_INFO *cs)
{
  bool got_warning= 0;
  int err= 0;
  char *not_used;
  uint not_used2;
  uint32 not_used_offset;
  char buff[STRING_BUFFER_USUAL_SIZE];
  String tmpstr(buff,sizeof(buff), &my_charset_bin);

  /* Convert character set if necessary */
  if (String::needs_conversion(length, cs, field_charset, &not_used_offset))
  { 
    uint dummy_errors;
    tmpstr.copy(from, length, cs, field_charset, &dummy_errors);
    from= tmpstr.ptr();
    length=  tmpstr.length();
  }
  uint64_t tmp= find_set(typelib, from, length, field_charset,
                          &not_used, &not_used2, &got_warning);
  if (!tmp && length && length < 22)
  {
    /* This is for reading numbers with LOAD DATA INFILE */
    char *end;
    tmp=my_strntoull(cs,from,length,10,&end,&err);
    if (err || end != from+length ||
	tmp > (uint64_t) (((longlong) 1 << typelib->count) - (longlong) 1))
    {
      tmp=0;      
      set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, WARN_DATA_TRUNCATED, 1);
    }
  }
  else if (got_warning)
    set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, WARN_DATA_TRUNCATED, 1);
  store_type(tmp);
  return err;
}


int Field_set::store(longlong nr,
                     bool unsigned_val __attribute__((__unused__)))
{
  int error= 0;
  uint64_t max_nr= set_bits(uint64_t, typelib->count);
  if ((uint64_t) nr > max_nr)
  {
    nr&= max_nr;
    set_warning(MYSQL_ERROR::WARN_LEVEL_WARN, WARN_DATA_TRUNCATED, 1);
    error=1;
  }
  store_type((uint64_t) nr);
  return error;
}


String *Field_set::val_str(String *val_buffer,
			   String *val_ptr __attribute__((unused)))
{
  uint64_t tmp=(uint64_t) Field_enum::val_int();
  uint bitnr=0;

  val_buffer->length(0);
  val_buffer->set_charset(field_charset);
  while (tmp && bitnr < (uint) typelib->count)
  {
    if (tmp & 1)
    {
      if (val_buffer->length())
	val_buffer->append(&field_separator, 1, &my_charset_latin1);
      String str(typelib->type_names[bitnr],
		 typelib->type_lengths[bitnr],
		 field_charset);
      val_buffer->append(str);
    }
    tmp>>=1;
    bitnr++;
  }
  return val_buffer;
}


void Field_set::sql_type(String &res) const
{
  char buffer[255];
  String set_item(buffer, sizeof(buffer), res.charset());

  res.length(0);
  res.append(STRING_WITH_LEN("set("));

  bool flag=0;
  uint *len= typelib->type_lengths;
  for (const char **pos= typelib->type_names; *pos; pos++, len++)
  {
    uint dummy_errors;
    if (flag)
      res.append(',');
    /* convert to res.charset() == utf8, then quote */
    set_item.copy(*pos, *len, charset(), res.charset(), &dummy_errors);
    append_unescaped(&res, set_item.ptr(), set_item.length());
    flag= 1;
  }
  res.append(')');
}

/**
  @retval
    1  if the fields are equally defined
  @retval
    0  if the fields are unequally defined
*/

bool Field::eq_def(Field *field)
{
  if (real_type() != field->real_type() || charset() != field->charset() ||
      pack_length() != field->pack_length())
    return 0;
  return 1;
}

/**
  @return
  returns 1 if the fields are equally defined
*/
bool Field_enum::eq_def(Field *field)
{
  if (!Field::eq_def(field))
    return 0;
  TYPELIB *from_lib=((Field_enum*) field)->typelib;

  if (typelib->count < from_lib->count)
    return 0;
  for (uint i=0 ; i < from_lib->count ; i++)
    if (my_strnncoll(field_charset,
                     (const uchar*)typelib->type_names[i],
                     strlen(typelib->type_names[i]),
                     (const uchar*)from_lib->type_names[i],
                     strlen(from_lib->type_names[i])))
      return 0;
  return 1;
}

/**
  @return
  returns 1 if the fields are equally defined
*/
bool Field_num::eq_def(Field *field)
{
  if (!Field::eq_def(field))
    return 0;
  Field_num *from_num= (Field_num*) field;

  if (unsigned_flag != from_num->unsigned_flag ||
      (zerofill && !from_num->zerofill && !zero_pack()) ||
      dec != from_num->dec)
    return 0;
  return 1;
}


uint Field_num::is_equal(Create_field *new_field)
{
  return ((new_field->sql_type == real_type()) &&
	  ((new_field->flags & UNSIGNED_FLAG) == (uint) (flags &
							 UNSIGNED_FLAG)) &&
	  ((new_field->flags & AUTO_INCREMENT_FLAG) ==
	   (uint) (flags & AUTO_INCREMENT_FLAG)) &&
	  (new_field->length <= max_display_length()));
}


/*****************************************************************************
  Handling of field and Create_field
*****************************************************************************/

/**
  Convert create_field::length from number of characters to number of bytes.
*/

void Create_field::create_length_to_internal_length(void)
{
  switch (sql_type) {
  case MYSQL_TYPE_BLOB:
  case MYSQL_TYPE_VAR_STRING:
  case MYSQL_TYPE_STRING:
  case MYSQL_TYPE_VARCHAR:
    length*= charset->mbmaxlen;
    key_length= length;
    pack_length= calc_pack_length(sql_type, length);
    break;
  case MYSQL_TYPE_ENUM:
  case MYSQL_TYPE_SET:
    /* Pack_length already calculated in sql_parse.cc */
    length*= charset->mbmaxlen;
    key_length= pack_length;
    break;
  case MYSQL_TYPE_NEWDECIMAL:
    key_length= pack_length=
      my_decimal_get_binary_size(my_decimal_length_to_precision(length,
								decimals,
								flags &
								UNSIGNED_FLAG),
				 decimals);
    break;
  default:
    key_length= pack_length= calc_pack_length(sql_type, length);
    break;
  }
}


/**
  Init for a tmp table field. To be extended if need be.
*/
void Create_field::init_for_tmp_table(enum_field_types sql_type_arg,
                                      uint32 length_arg, uint32 decimals_arg,
                                      bool maybe_null, bool is_unsigned)
{
  field_name= "";
  sql_type= sql_type_arg;
  char_length= length= length_arg;;
  unireg_check= Field::NONE;
  interval= 0;
  charset= &my_charset_bin;
  pack_flag= (FIELDFLAG_NUMBER |
              ((decimals_arg & FIELDFLAG_MAX_DEC) << FIELDFLAG_DEC_SHIFT) |
              (maybe_null ? FIELDFLAG_MAYBE_NULL : 0) |
              (is_unsigned ? 0 : FIELDFLAG_DECIMAL));
}


/**
  Initialize field definition for create.

  @param thd                   Thread handle
  @param fld_name              Field name
  @param fld_type              Field type
  @param fld_length            Field length
  @param fld_decimals          Decimal (if any)
  @param fld_type_modifier     Additional type information
  @param fld_default_value     Field default value (if any)
  @param fld_on_update_value   The value of ON UPDATE clause
  @param fld_comment           Field comment
  @param fld_change            Field change
  @param fld_interval_list     Interval list (if any)
  @param fld_charset           Field charset

  @retval
    false on success
  @retval
    true  on error
*/

bool Create_field::init(THD *thd, char *fld_name, enum_field_types fld_type,
                        char *fld_length, char *fld_decimals,
                        uint fld_type_modifier, Item *fld_default_value,
                        Item *fld_on_update_value, LEX_STRING *fld_comment,
                        char *fld_change, List<String> *fld_interval_list,
                        CHARSET_INFO *fld_charset,
                        uint fld_geom_type __attribute__((__unused__)),
                        enum column_format_type column_format)
{
  uint sign_len, allowed_type_modifier= 0;
  ulong max_field_charlength= MAX_FIELD_CHARLENGTH;

  field= 0;
  field_name= fld_name;
  def= fld_default_value;
  flags= fld_type_modifier;
  flags|= (((uint)column_format & COLUMN_FORMAT_MASK) << COLUMN_FORMAT_FLAGS);
  unireg_check= (fld_type_modifier & AUTO_INCREMENT_FLAG ?
                 Field::NEXT_NUMBER : Field::NONE);
  decimals= fld_decimals ? (uint)atoi(fld_decimals) : 0;
  if (decimals >= NOT_FIXED_DEC)
  {
    my_error(ER_TOO_BIG_SCALE, MYF(0), decimals, fld_name,
             NOT_FIXED_DEC-1);
    return(true);
  }

  sql_type= fld_type;
  length= 0;
  change= fld_change;
  interval= 0;
  pack_length= key_length= 0;
  charset= fld_charset;
  interval_list.empty();

  comment= *fld_comment;
  /*
    Set NO_DEFAULT_VALUE_FLAG if this field doesn't have a default value and
    it is NOT NULL, not an AUTO_INCREMENT field and not a TIMESTAMP.
  */
  if (!fld_default_value && !(fld_type_modifier & AUTO_INCREMENT_FLAG) &&
      (fld_type_modifier & NOT_NULL_FLAG) && fld_type != MYSQL_TYPE_TIMESTAMP)
    flags|= NO_DEFAULT_VALUE_FLAG;

  if (fld_length && !(length= (uint) atoi(fld_length)))
    fld_length= 0; /* purecov: inspected */
  sign_len= fld_type_modifier & UNSIGNED_FLAG ? 0 : 1;

  switch (fld_type) {
  case MYSQL_TYPE_TINY:
    if (!fld_length)
      length= MAX_TINYINT_WIDTH+sign_len;
    allowed_type_modifier= AUTO_INCREMENT_FLAG;
    break;
  case MYSQL_TYPE_SHORT:
    if (!fld_length)
      length= MAX_SMALLINT_WIDTH+sign_len;
    allowed_type_modifier= AUTO_INCREMENT_FLAG;
    break;
  case MYSQL_TYPE_LONG:
    if (!fld_length)
      length= MAX_INT_WIDTH+sign_len;
    allowed_type_modifier= AUTO_INCREMENT_FLAG;
    break;
  case MYSQL_TYPE_LONGLONG:
    if (!fld_length)
      length= MAX_BIGINT_WIDTH;
    allowed_type_modifier= AUTO_INCREMENT_FLAG;
    break;
  case MYSQL_TYPE_NULL:
    break;
  case MYSQL_TYPE_NEWDECIMAL:
    my_decimal_trim(&length, &decimals);
    if (length > DECIMAL_MAX_PRECISION)
    {
      my_error(ER_TOO_BIG_PRECISION, MYF(0), length, fld_name,
               DECIMAL_MAX_PRECISION);
      return(true);
    }
    if (length < decimals)
    {
      my_error(ER_M_BIGGER_THAN_D, MYF(0), fld_name);
      return(true);
    }
    length=
      my_decimal_precision_to_length(length, decimals,
                                     fld_type_modifier & UNSIGNED_FLAG);
    pack_length=
      my_decimal_get_binary_size(length, decimals);
    break;
  case MYSQL_TYPE_VARCHAR:
    /*
      Long VARCHAR's are automaticly converted to blobs in mysql_prepare_table
      if they don't have a default value
    */
    max_field_charlength= MAX_FIELD_VARCHARLENGTH;
    break;
  case MYSQL_TYPE_STRING:
    break;
  case MYSQL_TYPE_BLOB:
    if (fld_default_value)
    {
      /* Allow empty as default value. */
      String str,*res;
      res= fld_default_value->val_str(&str);
      /*
        A default other than '' is always an error, and any non-NULL
        specified default is an error in strict mode.
      */
      if (res->length() || (thd->variables.sql_mode &
                            (MODE_STRICT_TRANS_TABLES |
                             MODE_STRICT_ALL_TABLES)))
      {
        my_error(ER_BLOB_CANT_HAVE_DEFAULT, MYF(0),
                 fld_name); /* purecov: inspected */
        return(true);
      }
      else
      {
        /*
          Otherwise a default of '' is just a warning.
        */
        push_warning_printf(thd, MYSQL_ERROR::WARN_LEVEL_WARN,
                            ER_BLOB_CANT_HAVE_DEFAULT,
                            ER(ER_BLOB_CANT_HAVE_DEFAULT),
                            fld_name);
      }
      def= 0;
    }
    flags|= BLOB_FLAG;
    break;
  case MYSQL_TYPE_YEAR:
    if (!fld_length || length != 2)
      length= 4; /* Default length */
    flags|= ZEROFILL_FLAG | UNSIGNED_FLAG;
    break;
  case MYSQL_TYPE_FLOAT:
    /* change FLOAT(precision) to FLOAT or DOUBLE */
    allowed_type_modifier= AUTO_INCREMENT_FLAG;
    if (fld_length && !fld_decimals)
    {
      uint tmp_length= length;
      if (tmp_length > PRECISION_FOR_DOUBLE)
      {
        my_error(ER_WRONG_FIELD_SPEC, MYF(0), fld_name);
        return(true);
      }
      else if (tmp_length > PRECISION_FOR_FLOAT)
      {
        sql_type= MYSQL_TYPE_DOUBLE;
        length= DBL_DIG+7; /* -[digits].E+### */
      }
      else
        length= FLT_DIG+6; /* -[digits].E+## */
      decimals= NOT_FIXED_DEC;
      break;
    }
    if (!fld_length && !fld_decimals)
    {
      length=  FLT_DIG+6;
      decimals= NOT_FIXED_DEC;
    }
    if (length < decimals &&
        decimals != NOT_FIXED_DEC)
    {
      my_error(ER_M_BIGGER_THAN_D, MYF(0), fld_name);
      return(true);
    }
    break;
  case MYSQL_TYPE_DOUBLE:
    allowed_type_modifier= AUTO_INCREMENT_FLAG;
    if (!fld_length && !fld_decimals)
    {
      length= DBL_DIG+7;
      decimals= NOT_FIXED_DEC;
    }
    if (length < decimals &&
        decimals != NOT_FIXED_DEC)
    {
      my_error(ER_M_BIGGER_THAN_D, MYF(0), fld_name);
      return(true);
    }
    break;
  case MYSQL_TYPE_TIMESTAMP:
    if (!fld_length)
    {
      /* Compressed date YYYYMMDDHHMMSS */
      length= MAX_DATETIME_COMPRESSED_WIDTH;
    }
    else if (length != MAX_DATETIME_WIDTH)
    {
      /*
        We support only even TIMESTAMP lengths less or equal than 14
        and 19 as length of 4.1 compatible representation.
      */
      length= ((length+1)/2)*2; /* purecov: inspected */
      length= min(length, MAX_DATETIME_COMPRESSED_WIDTH); /* purecov: inspected */
    }
    flags|= ZEROFILL_FLAG | UNSIGNED_FLAG;
    if (fld_default_value)
    {
      /* Grammar allows only NOW() value for ON UPDATE clause */
      if (fld_default_value->type() == Item::FUNC_ITEM && 
          ((Item_func*)fld_default_value)->functype() == Item_func::NOW_FUNC)
      {
        unireg_check= (fld_on_update_value ? Field::TIMESTAMP_DNUN_FIELD:
                                             Field::TIMESTAMP_DN_FIELD);
        /*
          We don't need default value any longer moreover it is dangerous.
          Everything handled by unireg_check further.
        */
        def= 0;
      }
      else
        unireg_check= (fld_on_update_value ? Field::TIMESTAMP_UN_FIELD:
                                             Field::NONE);
    }
    else
    {
      /*
        If we have default TIMESTAMP NOT NULL column without explicit DEFAULT
        or ON UPDATE values then for the sake of compatiblity we should treat
        this column as having DEFAULT NOW() ON UPDATE NOW() (when we don't
        have another TIMESTAMP column with auto-set option before this one)
        or DEFAULT 0 (in other cases).
        So here we are setting TIMESTAMP_OLD_FIELD only temporary, and will
        replace this value by TIMESTAMP_DNUN_FIELD or NONE later when
        information about all TIMESTAMP fields in table will be availiable.

        If we have TIMESTAMP NULL column without explicit DEFAULT value
        we treat it as having DEFAULT NULL attribute.
      */
      unireg_check= (fld_on_update_value ? Field::TIMESTAMP_UN_FIELD :
                     (flags & NOT_NULL_FLAG ? Field::TIMESTAMP_OLD_FIELD :
                                              Field::NONE));
    }
    break;
  case MYSQL_TYPE_DATE:
    /* Old date type. */
    sql_type= MYSQL_TYPE_NEWDATE;
    /* fall trough */
  case MYSQL_TYPE_NEWDATE:
    length= 10;
    break;
  case MYSQL_TYPE_TIME:
    length= 10;
    break;
  case MYSQL_TYPE_DATETIME:
    length= MAX_DATETIME_WIDTH;
    break;
  case MYSQL_TYPE_SET:
    {
      pack_length= get_set_pack_length(fld_interval_list->elements);

      List_iterator<String> it(*fld_interval_list);
      String *tmp;
      while ((tmp= it++))
        interval_list.push_back(tmp);
      /*
        Set fake length to 1 to pass the below conditions.
        Real length will be set in mysql_prepare_table()
        when we know the character set of the column
      */
      length= 1;
      break;
    }
  case MYSQL_TYPE_ENUM:
    {
      /* Should be safe. */
      pack_length= get_enum_pack_length(fld_interval_list->elements);

      List_iterator<String> it(*fld_interval_list);
      String *tmp;
      while ((tmp= it++))
        interval_list.push_back(tmp);
      length= 1; /* See comment for MYSQL_TYPE_SET above. */
      break;
   }
  case MYSQL_TYPE_VAR_STRING:
  case MYSQL_TYPE_INT24:
    assert(0);  /* Impossible, we killed it */
    break;
  }
  /* Remember the value of length */
  char_length= length;

  if (!(flags & BLOB_FLAG) &&
      ((length > max_field_charlength && fld_type != MYSQL_TYPE_SET &&
        fld_type != MYSQL_TYPE_ENUM &&
        (fld_type != MYSQL_TYPE_VARCHAR || fld_default_value)) ||
       (!length &&
        fld_type != MYSQL_TYPE_STRING &&
        fld_type != MYSQL_TYPE_VARCHAR)))
  {
    my_error((fld_type == MYSQL_TYPE_VAR_STRING ||
              fld_type == MYSQL_TYPE_VARCHAR ||
              fld_type == MYSQL_TYPE_STRING) ?  ER_TOO_BIG_FIELDLENGTH :
                                                ER_TOO_BIG_DISPLAYWIDTH,
              MYF(0),
              fld_name, max_field_charlength); /* purecov: inspected */
    return(true);
  }
  fld_type_modifier&= AUTO_INCREMENT_FLAG;
  if ((~allowed_type_modifier) & fld_type_modifier)
  {
    my_error(ER_WRONG_FIELD_SPEC, MYF(0), fld_name);
    return(true);
  }

  return(false); /* success */
}


enum_field_types get_blob_type_from_length(ulong length __attribute__((__unused__)))
{
  enum_field_types type;

  type= MYSQL_TYPE_BLOB;

  return type;
}


/*
  Make a field from the .frm file info
*/

uint32 calc_pack_length(enum_field_types type,uint32 length)
{
  switch (type) {
  case MYSQL_TYPE_VAR_STRING:
  case MYSQL_TYPE_STRING:
  case MYSQL_TYPE_VARCHAR:     return (length + (length < 256 ? 1: 2));
  case MYSQL_TYPE_YEAR:
  case MYSQL_TYPE_TINY	: return 1;
  case MYSQL_TYPE_SHORT : return 2;
  case MYSQL_TYPE_DATE:
  case MYSQL_TYPE_NEWDATE:
  case MYSQL_TYPE_TIME:   return 3;
  case MYSQL_TYPE_TIMESTAMP:
  case MYSQL_TYPE_LONG	: return 4;
  case MYSQL_TYPE_FLOAT : return sizeof(float);
  case MYSQL_TYPE_DOUBLE: return sizeof(double);
  case MYSQL_TYPE_DATETIME:
  case MYSQL_TYPE_LONGLONG: return 8;	/* Don't crash if no longlong */
  case MYSQL_TYPE_NULL	: return 0;
  case MYSQL_TYPE_BLOB:		return 4+portable_sizeof_char_ptr;
  case MYSQL_TYPE_SET:
  case MYSQL_TYPE_ENUM:
  case MYSQL_TYPE_NEWDECIMAL:
    abort(); return 0;                          // This shouldn't happen
  default:
    return 0;
  }
}


uint pack_length_to_packflag(uint type)
{
  switch (type) {
    case 1: return f_settype((uint) MYSQL_TYPE_TINY);
    case 2: return f_settype((uint) MYSQL_TYPE_SHORT);
    case 3: assert(1);
    case 4: return f_settype((uint) MYSQL_TYPE_LONG);
    case 8: return f_settype((uint) MYSQL_TYPE_LONGLONG);
  }
  return 0;					// This shouldn't happen
}


Field *make_field(TABLE_SHARE *share, uchar *ptr, uint32 field_length,
		  uchar *null_pos, uchar null_bit,
		  uint pack_flag,
		  enum_field_types field_type,
		  CHARSET_INFO *field_charset,
		  Field::utype unireg_check,
		  TYPELIB *interval,
		  const char *field_name)
{
  if (!f_maybe_null(pack_flag))
  {
    null_pos=0;
    null_bit=0;
  }
  else
  {
    null_bit= ((uchar) 1) << null_bit;
  }

  switch (field_type) {
  case MYSQL_TYPE_DATE:
  case MYSQL_TYPE_NEWDATE:
  case MYSQL_TYPE_TIME:
  case MYSQL_TYPE_DATETIME:
  case MYSQL_TYPE_TIMESTAMP:
    field_charset= &my_charset_bin;
  default: break;
  }

  if (f_is_alpha(pack_flag))
  {
    if (!f_is_packed(pack_flag))
    {
      if (field_type == MYSQL_TYPE_STRING ||
          field_type == MYSQL_TYPE_VAR_STRING)
        return new Field_string(ptr,field_length,null_pos,null_bit,
                                unireg_check, field_name,
                                field_charset);
      if (field_type == MYSQL_TYPE_VARCHAR)
        return new Field_varstring(ptr,field_length,
                                   HA_VARCHAR_PACKLENGTH(field_length),
                                   null_pos,null_bit,
                                   unireg_check, field_name,
                                   share,
                                   field_charset);
      return 0;                                 // Error
    }

    uint pack_length=calc_pack_length((enum_field_types)
				      f_packtype(pack_flag),
				      field_length);

    if (f_is_blob(pack_flag))
      return new Field_blob(ptr,null_pos,null_bit,
			    unireg_check, field_name, share,
			    pack_length, field_charset);
    if (interval)
    {
      if (f_is_enum(pack_flag))
	return new Field_enum(ptr,field_length,null_pos,null_bit,
				  unireg_check, field_name,
				  pack_length, interval, field_charset);
      else
	return new Field_set(ptr,field_length,null_pos,null_bit,
			     unireg_check, field_name,
			     pack_length, interval, field_charset);
    }
  }

  switch (field_type) {
  case MYSQL_TYPE_NEWDECIMAL:
    return new Field_new_decimal(ptr,field_length,null_pos,null_bit,
                                 unireg_check, field_name,
                                 f_decimals(pack_flag),
                                 f_is_zerofill(pack_flag) != 0,
                                 f_is_dec(pack_flag) == 0);
  case MYSQL_TYPE_FLOAT:
    return new Field_float(ptr,field_length,null_pos,null_bit,
			   unireg_check, field_name,
			   f_decimals(pack_flag),
			   f_is_zerofill(pack_flag) != 0,
			   f_is_dec(pack_flag)== 0);
  case MYSQL_TYPE_DOUBLE:
    return new Field_double(ptr,field_length,null_pos,null_bit,
			    unireg_check, field_name,
			    f_decimals(pack_flag),
			    f_is_zerofill(pack_flag) != 0,
			    f_is_dec(pack_flag)== 0);
  case MYSQL_TYPE_TINY:
    return new Field_tiny(ptr,field_length,null_pos,null_bit,
			  unireg_check, field_name,
			  f_is_zerofill(pack_flag) != 0,
			  f_is_dec(pack_flag) == 0);
  case MYSQL_TYPE_SHORT:
    return new Field_short(ptr,field_length,null_pos,null_bit,
			   unireg_check, field_name,
			   f_is_zerofill(pack_flag) != 0,
			   f_is_dec(pack_flag) == 0);
  case MYSQL_TYPE_LONG:
    return new Field_long(ptr,field_length,null_pos,null_bit,
			   unireg_check, field_name,
			   f_is_zerofill(pack_flag) != 0,
			   f_is_dec(pack_flag) == 0);
  case MYSQL_TYPE_LONGLONG:
    return new Field_longlong(ptr,field_length,null_pos,null_bit,
			      unireg_check, field_name,
			      f_is_zerofill(pack_flag) != 0,
			      f_is_dec(pack_flag) == 0);
  case MYSQL_TYPE_TIMESTAMP:
    return new Field_timestamp(ptr,field_length, null_pos, null_bit,
                               unireg_check, field_name, share,
                               field_charset);
  case MYSQL_TYPE_YEAR:
    return new Field_year(ptr,field_length,null_pos,null_bit,
			  unireg_check, field_name);
  case MYSQL_TYPE_DATE:
  case MYSQL_TYPE_NEWDATE:
    return new Field_newdate(ptr,null_pos,null_bit,
			     unireg_check, field_name, field_charset);
  case MYSQL_TYPE_TIME:
    return new Field_time(ptr,null_pos,null_bit,
			  unireg_check, field_name, field_charset);
  case MYSQL_TYPE_DATETIME:
    return new Field_datetime(ptr,null_pos,null_bit,
			      unireg_check, field_name, field_charset);
  case MYSQL_TYPE_NULL:
    return new Field_null(ptr, field_length, unireg_check, field_name,
                          field_charset);
  default:					// Impossible (Wrong version)
    break;
  }
  return 0;
}


/** Create a field suitable for create of table. */

Create_field::Create_field(Field *old_field,Field *orig_field)
{
  field=      old_field;
  field_name=change=old_field->field_name;
  length=     old_field->field_length;
  flags=      old_field->flags;
  unireg_check=old_field->unireg_check;
  pack_length=old_field->pack_length();
  key_length= old_field->key_length();
  sql_type=   old_field->real_type();
  charset=    old_field->charset();		// May be NULL ptr
  comment=    old_field->comment;
  decimals=   old_field->decimals();

  /* Fix if the original table had 4 byte pointer blobs */
  if (flags & BLOB_FLAG)
    pack_length= (pack_length- old_field->table->s->blob_ptr_size +
		  portable_sizeof_char_ptr);

  switch (sql_type) {
  case MYSQL_TYPE_BLOB:
    sql_type= MYSQL_TYPE_BLOB;
    length/= charset->mbmaxlen;
    key_length/= charset->mbmaxlen;
    break;
  case MYSQL_TYPE_STRING:
    /* Change CHAR -> VARCHAR if dynamic record length */
    if (old_field->type() == MYSQL_TYPE_VAR_STRING)
      sql_type= MYSQL_TYPE_VARCHAR;
    /* fall through */

  case MYSQL_TYPE_ENUM:
  case MYSQL_TYPE_SET:
  case MYSQL_TYPE_VARCHAR:
  case MYSQL_TYPE_VAR_STRING:
    /* This is corrected in create_length_to_internal_length */
    length= (length+charset->mbmaxlen-1) / charset->mbmaxlen;
    break;
  default:
    break;
  }

  if (flags & (ENUM_FLAG | SET_FLAG))
    interval= ((Field_enum*) old_field)->typelib;
  else
    interval=0;
  def=0;
  char_length= length;

  if (!(flags & (NO_DEFAULT_VALUE_FLAG | BLOB_FLAG)) &&
      old_field->ptr && orig_field &&
      (sql_type != MYSQL_TYPE_TIMESTAMP ||                /* set def only if */
       old_field->table->timestamp_field != old_field ||  /* timestamp field */ 
       unireg_check == Field::TIMESTAMP_UN_FIELD))        /* has default val */
  {
    char buff[MAX_FIELD_WIDTH];
    String tmp(buff,sizeof(buff), charset);
    my_ptrdiff_t diff;

    /* Get the value from default_values */
    diff= (my_ptrdiff_t) (orig_field->table->s->default_values-
                          orig_field->table->record[0]);
    orig_field->move_field_offset(diff);	// Points now at default_values
    if (!orig_field->is_real_null())
    {
      char buff[MAX_FIELD_WIDTH], *pos;
      String tmp(buff, sizeof(buff), charset), *res;
      res= orig_field->val_str(&tmp);
      pos= (char*) sql_strmake(res->ptr(), res->length());
      def= new Item_string(pos, res->length(), charset);
    }
    orig_field->move_field_offset(-diff);	// Back to record[0]
  }
}


/**
  maximum possible display length for blob.

  @return
    length
*/

uint32 Field_blob::max_display_length()
{
  switch (packlength)
  {
  case 1:
    return 255 * field_charset->mbmaxlen;
  case 2:
    return 65535 * field_charset->mbmaxlen;
  case 3:
    return 16777215 * field_charset->mbmaxlen;
  case 4:
    return (uint32) 4294967295U;
  default:
    assert(0); // we should never go here
    return 0;
  }
}


/*****************************************************************************
 Warning handling
*****************************************************************************/

/**
  Produce warning or note about data saved into field.

  @param level            - level of message (Note/Warning/Error)
  @param code             - error code of message to be produced
  @param cuted_increment  - whenever we should increase cut fields count or not

  @note
    This function won't produce warning and increase cut fields counter
    if count_cuted_fields == CHECK_FIELD_IGNORE for current thread.

    if count_cuted_fields == CHECK_FIELD_IGNORE then we ignore notes.
    This allows us to avoid notes in optimisation, like convert_constant_item().

  @retval
    1 if count_cuted_fields == CHECK_FIELD_IGNORE and error level is not NOTE
  @retval
    0 otherwise
*/

bool 
Field::set_warning(MYSQL_ERROR::enum_warning_level level, uint code,
                   int cuted_increment)
{
  /*
    If this field was created only for type conversion purposes it
    will have table == NULL.
  */
  THD *thd= table ? table->in_use : current_thd;
  if (thd->count_cuted_fields)
  {
    thd->cuted_fields+= cuted_increment;
    push_warning_printf(thd, level, code, ER(code), field_name,
                        thd->row_count);
    return 0;
  }
  return level >= MYSQL_ERROR::WARN_LEVEL_WARN;
}


/**
  Produce warning or note about datetime string data saved into field.

  @param level            level of message (Note/Warning/Error)
  @param code             error code of message to be produced
  @param str              string value which we tried to save
  @param str_length       length of string which we tried to save
  @param ts_type          type of datetime value (datetime/date/time)
  @param cuted_increment  whenever we should increase cut fields count or not

  @note
    This function will always produce some warning but won't increase cut
    fields counter if count_cuted_fields ==FIELD_CHECK_IGNORE for current
    thread.
*/

void 
Field::set_datetime_warning(MYSQL_ERROR::enum_warning_level level, uint code, 
                            const char *str, uint str_length, 
                            timestamp_type ts_type, int cuted_increment)
{
  THD *thd= table ? table->in_use : current_thd;
  if ((thd->really_abort_on_warning() &&
       level >= MYSQL_ERROR::WARN_LEVEL_WARN) ||
      set_warning(level, code, cuted_increment))
    make_truncated_value_warning(thd, level, str, str_length, ts_type,
                                 field_name);
}


/**
  Produce warning or note about integer datetime value saved into field.

  @param level            level of message (Note/Warning/Error)
  @param code             error code of message to be produced
  @param nr               numeric value which we tried to save
  @param ts_type          type of datetime value (datetime/date/time)
  @param cuted_increment  whenever we should increase cut fields count or not

  @note
    This function will always produce some warning but won't increase cut
    fields counter if count_cuted_fields == FIELD_CHECK_IGNORE for current
    thread.
*/

void 
Field::set_datetime_warning(MYSQL_ERROR::enum_warning_level level, uint code, 
                            longlong nr, timestamp_type ts_type,
                            int cuted_increment)
{
  THD *thd= table ? table->in_use : current_thd;
  if (thd->really_abort_on_warning() ||
      set_warning(level, code, cuted_increment))
  {
    char str_nr[22];
    char *str_end= longlong10_to_str(nr, str_nr, -10);
    make_truncated_value_warning(thd, level, str_nr, (uint) (str_end - str_nr), 
                                 ts_type, field_name);
  }
}


/**
  Produce warning or note about double datetime data saved into field.

  @param level            level of message (Note/Warning/Error)
  @param code             error code of message to be produced
  @param nr               double value which we tried to save
  @param ts_type          type of datetime value (datetime/date/time)

  @note
    This function will always produce some warning but won't increase cut
    fields counter if count_cuted_fields == FIELD_CHECK_IGNORE for current
    thread.
*/

void 
Field::set_datetime_warning(MYSQL_ERROR::enum_warning_level level, uint code, 
                            double nr, timestamp_type ts_type)
{
  THD *thd= table ? table->in_use : current_thd;
  if (thd->really_abort_on_warning() ||
      set_warning(level, code, 1))
  {
    /* DBL_DIG is enough to print '-[digits].E+###' */
    char str_nr[DBL_DIG + 8];
    uint str_len= my_sprintf(str_nr, (str_nr, "%g", nr));
    make_truncated_value_warning(thd, level, str_nr, str_len, ts_type,
                                 field_name);
  }
}
