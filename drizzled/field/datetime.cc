/* - mode: c++ c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008 MySQL
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

#include "drizzled/server_includes.h"
#include "drizzled/field/datetime.h"
#include "drizzled/error.h"
#include "drizzled/table.h"
#include "drizzled/temporal.h"
#include "drizzled/session.h"

#include <sstream>
#include <string>


/****************************************************************************
** datetime type
** In string context: YYYY-MM-DD HH:MM:DD
** In number context: YYYYMMDDHHMMDD
** Stored as a 8 byte unsigned int. Should sometimes be change to a 6 byte int.
****************************************************************************/

int Field_datetime::store(const char *from,
                          uint32_t len,
                          const CHARSET_INFO * const )
{
  /* 
   * Try to create a DateTime from the supplied string.  Throw an error
   * if unable to create a valid DateTime.  
   */
  drizzled::DateTime temporal;
  if (! temporal.from_string(from, (size_t) len))
  {
    my_error(ER_INVALID_DATETIME_VALUE, MYF(ME_FATALERROR), from);
    return 2;
  }
  /* Create the stored integer format. @TODO This should go away. Should be up to engine... */
  int64_t int_value;
  temporal.to_int64_t(&int_value);

#ifdef WORDS_BIGENDIAN
  if (table && table->s->db_low_byte_first)
  {
    int8store(ptr, int_value);
  }
  else
#endif
    int64_tstore(ptr, int_value);
  return 0;
}

int Field_datetime::store(double from)
{
  if (from < 0.0 || from > 99991231235959.0)
  {
    /* Convert the double to a string using stringstream */
    std::stringstream ss;
    std::string tmp;
    ss.precision(18); /* 18 places should be fine for error display of double input. */
    ss << from; ss >> tmp;

    my_error(ER_INVALID_DATETIME_VALUE, MYF(ME_FATALERROR), tmp.c_str());
    return 2;
  }
  return Field_datetime::store((int64_t) rint(from), false);
}

int Field_datetime::store(int64_t from, bool)
{
  /* 
   * Try to create a DateTime from the supplied integer.  Throw an error
   * if unable to create a valid DateTime.  
   */
  drizzled::DateTime temporal;
  if (! temporal.from_int64_t(from))
  {
    /* Convert the integer to a string using stringstream */
    std::stringstream ss;
    std::string tmp;
    ss << from; ss >> tmp;

    my_error(ER_INVALID_DATETIME_VALUE, MYF(ME_FATALERROR), tmp.c_str());
    return 2;
  }

  /* 
   * Because "from" may be a silly MySQL-like "datetime number" (like, oh, 101)
   * we must here get the value of the DateTime as its *real* int64_t, after
   * the conversion above has been done...yuck. God, save us.
   */
  int64_t int_value;
  temporal.to_int64_t(&int_value);

#ifdef WORDS_BIGENDIAN
  if (table && table->s->db_low_byte_first)
  {
    int8store(ptr, int_value);
  }
  else
#endif
    int64_tstore(ptr, int_value);
  return 0;
}

int Field_datetime::store_time(DRIZZLE_TIME *ltime, enum enum_drizzle_timestamp_type)
{
  drizzled::DateTime temporal;

  temporal.set_years(ltime->year);
  temporal.set_months(ltime->month);
  temporal.set_days(ltime->day);
  temporal.set_hours(ltime->hour);
  temporal.set_minutes(ltime->minute);
  temporal.set_seconds(ltime->second);

  if (! temporal.is_valid())
  {
    char tmp_string[MAX_DATE_STRING_REP_LENGTH];
    size_t tmp_string_len;

    temporal.to_string(tmp_string, &tmp_string_len);
    my_error(ER_INVALID_DATETIME_VALUE, MYF(ME_FATALERROR), tmp_string);
    return 1;
  }

  int64_t int_value;
  temporal.to_int64_t(&int_value);

#ifdef WORDS_BIGENDIAN
  if (table && table->s->db_low_byte_first)
  {
    int8store(ptr, int_value);
  }
  else
#endif
    int64_tstore(ptr, int_value);
  return 0;
}

double Field_datetime::val_real(void)
{
  return (double) Field_datetime::val_int();
}

int64_t Field_datetime::val_int(void)
{
  int64_t j;
#ifdef WORDS_BIGENDIAN
  if (table && table->s->db_low_byte_first)
    j=sint8korr(ptr);
  else
#endif
    int64_tget(j,ptr);
  return j;
}


String *Field_datetime::val_str(String *val_buffer,
				String *)
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
    int64_tget(tmp,ptr);

  /*
    Avoid problem with slow int64_t arithmetic and sprintf
  */

  part1=(long) (tmp/INT64_C(1000000));
  part2=(long) (tmp - (uint64_t) part1*INT64_C(1000000));

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

bool Field_datetime::get_date(DRIZZLE_TIME *ltime, uint32_t fuzzydate)
{
  int64_t tmp=Field_datetime::val_int();
  uint32_t part1,part2;
  part1=(uint32_t) (tmp/INT64_C(1000000));
  part2=(uint32_t) (tmp - (uint64_t) part1*INT64_C(1000000));

  ltime->time_type=	DRIZZLE_TIMESTAMP_DATETIME;
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

bool Field_datetime::get_time(DRIZZLE_TIME *ltime)
{
  return Field_datetime::get_date(ltime,0);
}

int Field_datetime::cmp(const unsigned char *a_ptr, const unsigned char *b_ptr)
{
  int64_t a,b;
#ifdef WORDS_BIGENDIAN
  if (table && table->s->db_low_byte_first)
  {
    a=sint8korr(a_ptr);
    b=sint8korr(b_ptr);
  }
  else
#endif
  {
    int64_tget(a,a_ptr);
    int64_tget(b,b_ptr);
  }
  return ((uint64_t) a < (uint64_t) b) ? -1 :
    ((uint64_t) a > (uint64_t) b) ? 1 : 0;
}

void Field_datetime::sort_string(unsigned char *to,uint32_t )
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
