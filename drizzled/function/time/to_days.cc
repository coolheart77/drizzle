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

#include "drizzled/server_includes.h"
#include CSTDINT_H
#include "drizzled/function/time/to_days.h"
#include "drizzled/error.h"
#include "drizzled/temporal.h"

/* 
 * We intepret the first argument as a DateTime and then convert
 * it to a Julian Day Number and return it.
 */
int64_t Item_func_to_days::val_int()
{
  assert(fixed);

  /* We return NULL from FROM_DAYS() only when supplied a NULL argument */
  if (args[0]->null_value)
  {
    null_value= true;
    return false;
  }

  /*
   * We attempt to convert the first argument into a
   * temporal value.  If the conversion is successful, 
   * we know that a conversion to a Julian Day Number
   * is always possible.  Upon successful conversion, 
   * we return the Julian Day Number.  If no conversion
   * was possible into a temporal value, we throw an 
   * error and return 0, setting the null_value flag to true.
   */
  /* Grab the first argument as a DateTime object */
  drizzled::DateTime temporal;
  Item_result arg0_result_type= args[0]->result_type();
  
  switch (arg0_result_type)
  {
    case REAL_RESULT:
    case DECIMAL_RESULT: 
      /* 
       * For doubles supplied, interpret the arg as a string, 
       * so intentionally fall-through here...
       * This allows us to accept double parameters like 
       * 19971231235959.01 and interpret it the way MySQL does:
       * as a TIMESTAMP-like thing with a microsecond component.
       * Ugh, but need to keep backwards-compat.
       */
    case STRING_RESULT:
      {
        char buff[DRIZZLE_MAX_LENGTH_DATETIME_AS_STRING];
        String tmp(buff,sizeof(buff), &my_charset_utf8_bin);
        String *res= args[0]->val_str(&tmp);

        if (! res)
        {
          /* 
           * Likely a nested function issue where the nested
           * function had bad input.  We rely on the nested
           * function my_error() and simply return false here.
           */
          return false;
        }

        if (! temporal.from_string(res->c_ptr(), res->length()))
        {
          /* 
          * Could not interpret the function argument as a temporal value, 
          * so throw an error and return 0
          */
          my_error(ER_INVALID_DATETIME_VALUE, MYF(0), res->c_ptr());
          return 0;
        }
      }
      break;
    case INT_RESULT:
      if (temporal.from_int64_t(args[0]->val_int()))
        break;
      /* Intentionally fall-through on invalid conversion from integer */
    default:
      {
        /* 
        * Could not interpret the function argument as a temporal value, 
        * so throw an error and return 0
        */
        null_value= true;
        char buff[DRIZZLE_MAX_LENGTH_DATETIME_AS_STRING];
        String tmp(buff,sizeof(buff), &my_charset_utf8_bin);
        String *res;

        res= args[0]->val_str(&tmp);

        if (! res)
        {
          /* 
           * Likely a nested function issue where the nested
           * function had bad input.  We rely on the nested
           * function my_error() and simply return false here.
           */
          return false;
        }

        my_error(ER_INVALID_DATETIME_VALUE, MYF(0), res->c_ptr());
        return 0;
      }
  }
  int64_t julian_day_number;
  temporal.to_julian_day_number(&julian_day_number);
  return julian_day_number;
}

/*
  Get information about this Item tree monotonicity

  SYNOPSIS
    Item_func_to_days::get_monotonicity_info()

  DESCRIPTION
  Get information about monotonicity of the function represented by this item
  tree.

  RETURN
    See enum_monotonicity_info.
*/
enum_monotonicity_info Item_func_to_days::get_monotonicity_info() const
{
  if (args[0]->type() == Item::FIELD_ITEM)
  {
    if (args[0]->field_type() == DRIZZLE_TYPE_DATE)
      return MONOTONIC_STRICT_INCREASING;
    if (args[0]->field_type() == DRIZZLE_TYPE_DATETIME)
      return MONOTONIC_INCREASING;
  }
  return NON_MONOTONIC;
}

int64_t Item_func_to_days::val_int_endpoint(bool left_endp, bool *incl_endp)
{
  assert(fixed);

  /*
   * We attempt to convert the first argument into a
   * temporal value.  If the conversion is successful, 
   * we know that a conversion to a Julian Day Number
   * is always possible. Depending on whether the 
   * first argument is a Date, or a DateTime with no
   * time-portion, we return the Julian Day Number or
   * the appropriate end-point integer.
   */
  /* Grab the first argument as a DateTime object */
  drizzled::DateTime temporal;
  Item_result arg0_result_type= args[0]->result_type();
  
  switch (arg0_result_type)
  {
    case REAL_RESULT:
    case DECIMAL_RESULT: 
      /* 
       * For doubles supplied, interpret the arg as a string, 
       * so intentionally fall-through here...
       * This allows us to accept double parameters like 
       * 19971231235959.01 and interpret it the way MySQL does:
       * as a TIMESTAMP-like thing with a microsecond component.
       * Ugh, but need to keep backwards-compat.
       */
    case STRING_RESULT:
      {
        char buff[DRIZZLE_MAX_LENGTH_DATETIME_AS_STRING];
        String tmp(buff,sizeof(buff), &my_charset_utf8_bin);
        String *res= args[0]->val_str(&tmp);

        if (! res)
        {
          /* 
           * Likely a nested function issue where the nested
           * function had bad input.  We rely on the nested
           * function my_error() and simply return false here.
           */
          return 0;
        }

        if (! temporal.from_string(res->c_ptr(), res->length()))
        {
          /* 
          * Could not interpret the function argument as a temporal value, 
          * so throw an error and return 0
          */
          my_error(ER_INVALID_DATETIME_VALUE, MYF(0), res->c_ptr());
          return 0;
        }
      }
      break;
    case INT_RESULT:
      if (temporal.from_int64_t(args[0]->val_int()))
        break;
      /* Intentionally fall-through on invalid conversion from integer */
    default:
      {
        /* 
        * Could not interpret the function argument as a temporal value, 
        * so throw an error and return 0
        */
        null_value= true;
        char buff[DRIZZLE_MAX_LENGTH_DATETIME_AS_STRING];
        String tmp(buff,sizeof(buff), &my_charset_utf8_bin);
        String *res;

        res= args[0]->val_str(&tmp);

        if (! res)
        {
          /* 
           * Likely a nested function issue where the nested
           * function had bad input.  We rely on the nested
           * function my_error() and simply return false here.
           */
          return 0;
        }

        my_error(ER_INVALID_DATETIME_VALUE, MYF(0), res->c_ptr());
        return 0;
      }
  }

  if (null_value == true)
  {
    /* got NULL, leave the incl_endp intact */
    return INT64_MIN;
  }

  int64_t julian_day_number;
  temporal.to_julian_day_number(&julian_day_number);

  if (args[0]->field_type() == DRIZZLE_TYPE_DATE)
  {
    /* TO_DAYS() is strictly monotonic for dates, leave incl_endp intact */
    return julian_day_number;
  }

  /*
    Handle the special but practically useful case of datetime values that
    point to day bound ("strictly less" comparison stays intact):

      col < '2007-09-15 00:00:00'  -> TO_DAYS(col) <  TO_DAYS('2007-09-15')

    which is different from the general case ("strictly less" changes to
    "less or equal"):

      col < '2007-09-15 12:34:56'  -> TO_DAYS(col) <= TO_DAYS('2007-09-15')
  */
  if (!left_endp && ! (
                    temporal.hours() 
                    || temporal.minutes()
                    || temporal.seconds() 
                    || temporal.useconds()
                    || temporal.nseconds()
                    )
                    )
    ; /* do nothing */
  else
    *incl_endp= true;
  return julian_day_number;
}