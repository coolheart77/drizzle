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
  @file

  It is interface module to fixed precision decimals library.

  Most functions use 'uint32_t mask' as parameter, if during operation error
  which fit in this mask is detected then it will be processed automatically
  here. (errors are E_DEC_* constants, see include/decimal.h)

  Most function are just inline wrappers around library calls
*/

#ifndef my_decimal_h
#define my_decimal_h

#ifdef __cplusplus
extern "C" {
#endif

#include <mystrings/decimal.h>
#include <mysys/my_time.h>
#include <drizzled/sql_string.h>

#ifdef __cplusplus
}
#endif


#define DECIMAL_LONGLONG_DIGITS 22
#define DECIMAL_LONG_DIGITS 10
#define DECIMAL_LONG3_DIGITS 8

/** maximum length of buffer in our big digits (uint32_t). */
#define DECIMAL_BUFF_LENGTH 9

/* the number of digits that my_decimal can possibly contain */
#define DECIMAL_MAX_POSSIBLE_PRECISION (DECIMAL_BUFF_LENGTH * 9)


/**
  maximum guaranteed precision of number in decimal digits (number of our
  digits * number of decimal digits in one our big digit - number of decimal
  digits in one our big digit decreased by 1 (because we always put decimal
  point on the border of our big digits))
*/
#define DECIMAL_MAX_PRECISION (DECIMAL_MAX_POSSIBLE_PRECISION - 8*2)
#define DECIMAL_MAX_SCALE 30
#define DECIMAL_NOT_SPECIFIED 31

/**
  maximum length of string representation (number of maximum decimal
  digits + 1 position for sign + 1 position for decimal point)
*/
#define DECIMAL_MAX_STR_LENGTH (DECIMAL_MAX_POSSIBLE_PRECISION + 2)

/**
  maximum size of packet length.
*/
#define DECIMAL_MAX_FIELD_SIZE DECIMAL_MAX_PRECISION


inline uint32_t my_decimal_size(uint32_t precision, uint32_t scale)
{
  /*
    Always allocate more space to allow library to put decimal point
    where it want
  */
  return decimal_size(precision, scale) + 1;
}


inline int my_decimal_int_part(uint32_t precision, uint32_t decimals)
{
  return precision - ((decimals == DECIMAL_NOT_SPECIFIED) ? 0 : decimals);
}


/**
  my_decimal class limits 'decimal_t' type to what we need in MySQL.

  It contains internally all necessary space needed by the instance so
  no extra memory is needed. One should call fix_buffer_pointer() function
  when he moves my_decimal objects in memory.
*/

class my_decimal :public decimal_t
{
  decimal_digit_t buffer[DECIMAL_BUFF_LENGTH];

public:

  void init()
  {
    len= DECIMAL_BUFF_LENGTH;
    buf= buffer;
#if !defined (HAVE_purify)
    /* Set buffer to 'random' value to find wrong buffer usage */
    for (uint32_t i= 0; i < DECIMAL_BUFF_LENGTH; i++)
      buffer[i]= i;
#endif
  }
  my_decimal()
  {
    init();
  }
  void fix_buffer_pointer() { buf= buffer; }

  bool sign() const { return decimal_t::sign; }
  void sign(bool s) { decimal_t::sign= s; }
  uint32_t precision() const { return intg + frac; }
};

int decimal_operation_results(int result);

inline
void max_my_decimal(my_decimal *to, int precision, int frac)
{
  assert((precision <= DECIMAL_MAX_PRECISION)&&
              (frac <= DECIMAL_MAX_SCALE));
  max_decimal(precision, frac, (decimal_t*) to);
}

inline void max_internal_decimal(my_decimal *to)
{
  max_my_decimal(to, DECIMAL_MAX_PRECISION, 0);
}

inline int check_result(uint32_t mask, int result)
{
  if (result & mask)
    decimal_operation_results(result);
  return result;
}

inline int check_result_and_overflow(uint32_t mask, int result, my_decimal *val)
{
  if (check_result(mask, result) & E_DEC_OVERFLOW)
  {
    bool sign= val->sign();
    val->fix_buffer_pointer();
    max_internal_decimal(val);
    val->sign(sign);
  }
  return result;
}

inline uint32_t my_decimal_length_to_precision(uint32_t length, uint32_t scale,
                                           bool unsigned_flag)
{
  return (uint32_t) (length - (scale>0 ? 1:0) - (unsigned_flag ? 0:1));
}

inline uint32_t my_decimal_precision_to_length(uint32_t precision, uint8_t scale,
                                             bool unsigned_flag)
{
  set_if_smaller(precision, DECIMAL_MAX_PRECISION);
  return (uint32_t)(precision + (scale>0 ? 1:0) + (unsigned_flag ? 0:1));
}

inline
int my_decimal_string_length(const my_decimal *d)
{
  return decimal_string_size(d);
}


inline
int my_decimal_max_length(const my_decimal *d)
{
  /* -1 because we do not count \0 */
  return decimal_string_size(d) - 1;
}


inline
int my_decimal_get_binary_size(uint32_t precision, uint32_t scale)
{
  return decimal_bin_size((int)precision, (int)scale);
}


inline
void my_decimal2decimal(const my_decimal *from, my_decimal *to)
{
  *to= *from;
  to->fix_buffer_pointer();
}


int my_decimal2binary(uint32_t mask, const my_decimal *d, unsigned char *bin, int prec,
		      int scale);


inline
int binary2my_decimal(uint32_t mask, const unsigned char *bin, my_decimal *d, int prec,
		      int scale)
{
  return check_result(mask, bin2decimal(bin, (decimal_t*) d, prec, scale));
}


inline
int my_decimal_set_zero(my_decimal *d)
{
  decimal_make_zero(((decimal_t*) d));
  return 0;
}


inline
bool my_decimal_is_zero(const my_decimal *decimal_value)
{
  return decimal_is_zero((decimal_t*) decimal_value);
}


inline
int my_decimal_round(uint32_t mask, const my_decimal *from, int scale,
                     bool truncate, my_decimal *to)
{
  return check_result(mask, decimal_round((decimal_t*) from, to, scale,
                                          (truncate ? TRUNCATE : HALF_UP)));
}


inline
int my_decimal_floor(uint32_t mask, const my_decimal *from, my_decimal *to)
{
  return check_result(mask, decimal_round((decimal_t*) from, to, 0, FLOOR));
}


inline
int my_decimal_ceiling(uint32_t mask, const my_decimal *from, my_decimal *to)
{
  return check_result(mask, decimal_round((decimal_t*) from, to, 0, CEILING));
}


int my_decimal2string(uint32_t mask, const my_decimal *d, uint32_t fixed_prec,
                      uint32_t fixed_dec, char filler, String *str);

inline
int my_decimal2int(uint32_t mask, const my_decimal *d, bool unsigned_flag,
                   int64_t *l)
{
  my_decimal rounded;
  /* decimal_round can return only E_DEC_TRUNCATED */
  decimal_round((decimal_t*)d, &rounded, 0, HALF_UP);
  return check_result(mask, (unsigned_flag ?
			     decimal2uint64_t(&rounded, (uint64_t *)l) :
			     decimal2int64_t(&rounded, l)));
}


inline
int my_decimal2double(uint32_t, const my_decimal *d, double *result)
{
  /* No need to call check_result as this will always succeed */
  return decimal2double((decimal_t*) d, result);
}


inline
int str2my_decimal(uint32_t mask, char *str, my_decimal *d, char **end)
{
  return check_result_and_overflow(mask, string2decimal(str,(decimal_t*)d,end),
                                   d);
}


int str2my_decimal(uint32_t mask, const char *from, uint32_t length,
                   const CHARSET_INFO * charset, my_decimal *decimal_value);

inline
int string2my_decimal(uint32_t mask, const String *str, my_decimal *d)
{
  return str2my_decimal(mask, str->ptr(), str->length(), str->charset(), d);
}


my_decimal *date2my_decimal(DRIZZLE_TIME *ltime, my_decimal *dec);


inline
int double2my_decimal(uint32_t mask, double val, my_decimal *d)
{
  return check_result_and_overflow(mask, double2decimal(val, (decimal_t*)d), d);
}


inline
int int2my_decimal(uint32_t mask, int64_t i, bool unsigned_flag, my_decimal *d)
{
  return check_result(mask, (unsigned_flag ?
			     uint64_t2decimal((uint64_t)i, d) :
			     int64_t2decimal(i, d)));
}


inline
void my_decimal_neg(decimal_t *arg)
{
  if (decimal_is_zero(arg))
  {
    arg->sign= 0;
    return;
  }
  decimal_neg(arg);
}


inline
int my_decimal_add(uint32_t mask, my_decimal *res, const my_decimal *a,
		   const my_decimal *b)
{
  return check_result_and_overflow(mask,
                                   decimal_add((decimal_t*)a,(decimal_t*)b,res),
                                   res);
}


inline
int my_decimal_sub(uint32_t mask, my_decimal *res, const my_decimal *a,
		   const my_decimal *b)
{
  return check_result_and_overflow(mask,
                                   decimal_sub((decimal_t*)a,(decimal_t*)b,res),
                                   res);
}


inline
int my_decimal_mul(uint32_t mask, my_decimal *res, const my_decimal *a,
		   const my_decimal *b)
{
  return check_result_and_overflow(mask,
                                   decimal_mul((decimal_t*)a,(decimal_t*)b,res),
                                   res);
}


inline
int my_decimal_div(uint32_t mask, my_decimal *res, const my_decimal *a,
		   const my_decimal *b, int div_scale_inc)
{
  return check_result_and_overflow(mask,
                                   decimal_div((decimal_t*)a,(decimal_t*)b,res,
                                               div_scale_inc),
                                   res);
}


inline
int my_decimal_mod(uint32_t mask, my_decimal *res, const my_decimal *a,
		   const my_decimal *b)
{
  return check_result_and_overflow(mask,
                                   decimal_mod((decimal_t*)a,(decimal_t*)b,res),
                                   res);
}


/**
  @return
    -1 if a<b, 1 if a>b and 0 if a==b
*/
inline
int my_decimal_cmp(const my_decimal *a, const my_decimal *b)
{
  return decimal_cmp((decimal_t*) a, (decimal_t*) b);
}


inline
int my_decimal_intg(const my_decimal *a)
{
  return decimal_intg((decimal_t*) a);
}


void my_decimal_trim(uint32_t *precision, uint32_t *scale);


#endif /*my_decimal_h*/
