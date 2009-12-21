/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008-2009 Sun Microsystems
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

#include "config.h"
#include "drizzled/session.h"
#include "drizzled/item/uint.h"
#include "drizzled/item/float.h"
#include "drizzled/optimizer/explain_plan.h"
#include "drizzled/optimizer/position.h"
#include "drizzled/optimizer/quick_ror_intersect_select.h"
#include "drizzled/optimizer/range.h"
#include "drizzled/sql_select.h"
#include "drizzled/join.h"

#include <string>
#include <sstream>

using namespace std;
using namespace drizzled;

static const string access_method_str[]=
{
  "UNKNOWN",
  "system",
  "const",
  "eq_ref",
  "ref",
  "MAYBE_REF",
  "ALL",
  "range",
  "index",
  "ref_or_null",
  "unique_subquery",
  "index_subquery",
  "index_merge"
};

void optimizer::ExplainPlan::printPlan()
{
  List<Item> field_list;
  List<Item> item_list;
  Session *session= join->session;
  select_result *result= join->result;
  Item *item_null= new Item_null();
  const CHARSET_INFO * const cs= system_charset_info;
  int quick_type;
  /* Don't log this into the slow query log */
  session->server_status&= ~(SERVER_QUERY_NO_INDEX_USED | SERVER_QUERY_NO_GOOD_INDEX_USED);
  join->unit->offset_limit_cnt= 0;

  /*
   NOTE: the number/types of items pushed into item_list must be in sync with
   EXPLAIN column types as they're "defined" in Session::send_explain_fields()
   */
  if (message)
  {
    item_list.push_back(new Item_int((int32_t)
                        join->select_lex->select_number));
    item_list.push_back(new Item_string(join->select_lex->type.c_str(),
                                        join->select_lex->type.length(),
                                        cs));
    for (uint32_t i= 0; i < 7; i++)
      item_list.push_back(item_null);

    if (join->session->lex->describe & DESCRIBE_EXTENDED)
      item_list.push_back(item_null);

    item_list.push_back(new Item_string(message,strlen(message),cs));
    if (result->send_data(item_list))
      join->error= 1;
  }
  else if (join->select_lex == join->unit->fake_select_lex)
  {
    /*
       here we assume that the query will return at least two rows, so we
       show "filesort" in EXPLAIN. Of course, sometimes we'll be wrong
       and no filesort will be actually done, but executing all selects in
       the UNION to provide precise EXPLAIN information will hardly be
       appreciated :)
     */
    char table_name_buffer[NAME_LEN];
    item_list.empty();
    /* id */
    item_list.push_back(new Item_null);
    /* select_type */
    item_list.push_back(new Item_string(join->select_lex->type.c_str(),
                                        join->select_lex->type.length(),
                                        cs));
    /* table */
    {
      Select_Lex *sl= join->unit->first_select();
      uint32_t len= 6, lastop= 0;
      memcpy(table_name_buffer, STRING_WITH_LEN("<union"));
      for (; sl && len + lastop + 5 < NAME_LEN; sl= sl->next_select())
      {
        len+= lastop;
        lastop= snprintf(table_name_buffer + len, NAME_LEN - len,
            "%u,", sl->select_number);
      }
      if (sl || len + lastop >= NAME_LEN)
      {
        memcpy(table_name_buffer + len, STRING_WITH_LEN("...>") + 1);
        len+= 4;
      }
      else
      {
        len+= lastop;
        table_name_buffer[len - 1]= '>';  // change ',' to '>'
      }
      item_list.push_back(new Item_string(table_name_buffer, len, cs));
    }
    /* type */
    item_list.push_back(new Item_string(access_method_str[AM_ALL].c_str(),
                                        access_method_str[AM_ALL].length(),
                                        cs));
    /* possible_keys */
    item_list.push_back(item_null);
    /* key*/
    item_list.push_back(item_null);
    /* key_len */
    item_list.push_back(item_null);
    /* ref */
    item_list.push_back(item_null);
    /* in_rows */
    if (join->session->lex->describe & DESCRIBE_EXTENDED)
      item_list.push_back(item_null);
    /* rows */
    item_list.push_back(item_null);
    /* extra */
    if (join->unit->global_parameters->order_list.first)
      item_list.push_back(new Item_string("Using filesort",
                                          14, 
                                          cs));
    else
      item_list.push_back(new Item_string("", 0, cs));

    if (result->send_data(item_list))
      join->error= 1;
  }
  else
  {
    table_map used_tables= 0;
    for (uint32_t i= 0; i < join->tables; i++)
    {
      JoinTable *tab= join->join_tab + i;
      Table *table= tab->table;
      char buff[512];
      char buff1[512], buff2[512], buff3[512];
      char keylen_str_buf[64];
      String extra(buff, sizeof(buff),cs);
      char table_name_buffer[NAME_LEN];
      String tmp1(buff1,sizeof(buff1),cs);
      String tmp2(buff2,sizeof(buff2),cs);
      String tmp3(buff3,sizeof(buff3),cs);
      extra.length(0);
      tmp1.length(0);
      tmp2.length(0);
      tmp3.length(0);

      quick_type= -1;
      item_list.empty();
      /* id */
      item_list.push_back(new Item_uint((uint32_t)
            join->select_lex->select_number));
      /* select_type */
      item_list.push_back(new Item_string(join->select_lex->type.c_str(),
                                          join->select_lex->type.length(),
                                          cs));
      if (tab->type == AM_ALL && tab->select && tab->select->quick)
      {
        quick_type= tab->select->quick->get_type();
        if ((quick_type == optimizer::QuickSelectInterface::QS_TYPE_INDEX_MERGE) ||
            (quick_type == optimizer::QuickSelectInterface::QS_TYPE_ROR_INTERSECT) ||
            (quick_type == optimizer::QuickSelectInterface::QS_TYPE_ROR_UNION))
          tab->type = AM_INDEX_MERGE;
        else
          tab->type = AM_RANGE;
      }
      /* table */
      if (table->derived_select_number)
      {
        /* Derived table name generation */
        int len= snprintf(table_name_buffer, 
                          sizeof(table_name_buffer)-1,
                          "<derived%u>",
                          table->derived_select_number);
        item_list.push_back(new Item_string(table_name_buffer, len, cs));
      }
      else
      {
        TableList *real_table= table->pos_in_table_list;
        item_list.push_back(new Item_string(real_table->alias,
                                            strlen(real_table->alias),
                                            cs));
      }
      /* "type" column */
      item_list.push_back(new Item_string(access_method_str[tab->type].c_str(),
                                          access_method_str[tab->type].length(),
                                          cs));
      /* Build "possible_keys" value and add it to item_list */
      if (tab->keys.any())
      {
        for (uint32_t j= 0; j < table->s->keys; j++)
        {
          if (tab->keys.test(j))
          {
            if (tmp1.length())
              tmp1.append(',');
            tmp1.append(table->key_info[j].name,
                        strlen(table->key_info[j].name),
                        system_charset_info);
          }
        }
      }
      if (tmp1.length())
        item_list.push_back(new Item_string(tmp1.ptr(),tmp1.length(),cs));
      else
        item_list.push_back(item_null);

      /* Build "key", "key_len", and "ref" values and add them to item_list */
      if (tab->ref.key_parts)
      {
        KEY *key_info= table->key_info+ tab->ref.key;
        item_list.push_back(new Item_string(key_info->name,
                                            strlen(key_info->name),
                                            system_charset_info));
        uint32_t length= int64_t2str(tab->ref.key_length, keylen_str_buf, 10) -
                                     keylen_str_buf;
        item_list.push_back(new Item_string(keylen_str_buf, 
                                            length,
                                            system_charset_info));
        for (StoredKey **ref= tab->ref.key_copy; *ref; ref++)
        {
          if (tmp2.length())
            tmp2.append(',');
          tmp2.append((*ref)->name(), 
                       strlen((*ref)->name()),
                       system_charset_info);
        }
        item_list.push_back(new Item_string(tmp2.ptr(),tmp2.length(),cs));
      }
      else if (tab->type == AM_NEXT)
      {
        KEY *key_info=table->key_info+ tab->index;
        item_list.push_back(new Item_string(key_info->name,
              strlen(key_info->name),cs));
        uint32_t length= int64_t2str(key_info->key_length, keylen_str_buf, 10) -
                                     keylen_str_buf;
        item_list.push_back(new Item_string(keylen_str_buf,
                                            length,
                                            system_charset_info));
        item_list.push_back(item_null);
      }
      else if (tab->select && tab->select->quick)
      {
        tab->select->quick->add_keys_and_lengths(&tmp2, &tmp3);
        item_list.push_back(new Item_string(tmp2.ptr(),tmp2.length(),cs));
        item_list.push_back(new Item_string(tmp3.ptr(),tmp3.length(),cs));
        item_list.push_back(item_null);
      }
      else
      {
        item_list.push_back(item_null);
        item_list.push_back(item_null);
        item_list.push_back(item_null);
      }

      /* Add "rows" field to item_list. */
      double examined_rows;
      if (tab->select && tab->select->quick)
      {
        examined_rows= rows2double(tab->select->quick->records);
      }
      else if (tab->type == AM_NEXT || tab->type == AM_ALL)
      {
        examined_rows= rows2double(tab->limit ? tab->limit :
                                                tab->table->cursor->records());
      }
      else
      {
        optimizer::Position cur_pos= join->getPosFromOptimalPlan(i);
        examined_rows= cur_pos.getFanout();
      }

      item_list.push_back(new Item_int((int64_t) (uint64_t) examined_rows,
                                       MY_INT64_NUM_DECIMAL_DIGITS));

      /* Add "filtered" field to item_list. */
      if (join->session->lex->describe & DESCRIBE_EXTENDED)
      {
        float f= 0.0;
        if (examined_rows)
        {
          optimizer::Position cur_pos= join->getPosFromOptimalPlan(i);
          f= static_cast<float>(100.0 * cur_pos.getFanout() / examined_rows);
        }
        item_list.push_back(new Item_float(f, 2));
      }

      /* Build "Extra" field and add it to item_list. */
      bool key_read= table->key_read;
      if ((tab->type == AM_NEXT || tab->type == AM_CONST) &&
          table->covering_keys.test(tab->index))
        key_read= 1;
      if (quick_type == optimizer::QuickSelectInterface::QS_TYPE_ROR_INTERSECT &&
          ! ((optimizer::QuickRorIntersectSelect *) tab->select->quick)->need_to_fetch_row)
        key_read= 1;

      if (tab->info)
        item_list.push_back(new Item_string(tab->info,strlen(tab->info),cs));
      else if (tab->packed_info & TAB_INFO_HAVE_VALUE)
      {
        if (tab->packed_info & TAB_INFO_USING_INDEX)
          extra.append(STRING_WITH_LEN("; Using index"));
        if (tab->packed_info & TAB_INFO_USING_WHERE)
          extra.append(STRING_WITH_LEN("; Using where"));
        if (tab->packed_info & TAB_INFO_FULL_SCAN_ON_NULL)
          extra.append(STRING_WITH_LEN("; Full scan on NULL key"));
        /* Skip initial "; "*/
        const char *str= extra.ptr();
        uint32_t len= extra.length();
        if (len)
        {
          str += 2;
          len -= 2;
        }
        item_list.push_back(new Item_string(str, len, cs));
      }
      else
      {
        uint32_t keyno= MAX_KEY;
        if (tab->ref.key_parts)
          keyno= tab->ref.key;
        else if (tab->select && tab->select->quick)
          keyno = tab->select->quick->index;

        if (quick_type == optimizer::QuickSelectInterface::QS_TYPE_ROR_UNION ||
            quick_type == optimizer::QuickSelectInterface::QS_TYPE_ROR_INTERSECT ||
            quick_type == optimizer::QuickSelectInterface::QS_TYPE_INDEX_MERGE)
        {
          extra.append(STRING_WITH_LEN("; Using "));
          tab->select->quick->add_info_string(&extra);
        }
        if (tab->select)
        {
          if (tab->use_quick == 2)
          {
            /*
             * To print out the bitset in tab->keys, we go through
             * it 32 bits at a time. We need to do this to ensure
             * that the to_ulong() method will not throw an
             * out_of_range exception at runtime which would happen
             * if the bitset we were working with was larger than 64
             * bits on a 64-bit platform (for example).
             */
            stringstream s, w;
            string str;
            w << tab->keys;
            w >> str;
            for (uint32_t pos= 0; pos < tab->keys.size(); pos+= 32)
            {
              bitset<32> tmp(str, pos, 32);
              if (tmp.any())
                s << uppercase << hex << tmp.to_ulong();
            }
            extra.append(STRING_WITH_LEN("; Range checked for each "
                  "record (index map: 0x"));
            extra.append(s.str().c_str());
            extra.append(')');
          }
          else if (tab->select->cond)
          {
            extra.append(STRING_WITH_LEN("; Using where"));
          }
        }
        if (key_read)
        {
          if (quick_type == optimizer::QuickSelectInterface::QS_TYPE_GROUP_MIN_MAX)
            extra.append(STRING_WITH_LEN("; Using index for group-by"));
          else
            extra.append(STRING_WITH_LEN("; Using index"));
        }
        if (table->reginfo.not_exists_optimize)
          extra.append(STRING_WITH_LEN("; Not exists"));

        if (need_tmp_table)
        {
          need_tmp_table=0;
          extra.append(STRING_WITH_LEN("; Using temporary"));
        }
        if (need_order)
        {
          need_order=0;
          extra.append(STRING_WITH_LEN("; Using filesort"));
        }
        if (distinct & test_all_bits(used_tables,session->used_tables))
          extra.append(STRING_WITH_LEN("; Distinct"));

        if (tab->insideout_match_tab)
        {
          extra.append(STRING_WITH_LEN("; LooseScan"));
        }

        for (uint32_t part= 0; part < tab->ref.key_parts; part++)
        {
          if (tab->ref.cond_guards[part])
          {
            extra.append(STRING_WITH_LEN("; Full scan on NULL key"));
            break;
          }
        }

        if (i > 0 && tab[-1].next_select == sub_select_cache)
          extra.append(STRING_WITH_LEN("; Using join buffer"));

        /* Skip initial "; "*/
        const char *str= extra.ptr();
        uint32_t len= extra.length();
        if (len)
        {
          str += 2;
          len -= 2;
        }
        item_list.push_back(new Item_string(str, len, cs));
      }
      // For next iteration
      used_tables|=table->map;
      if (result->send_data(item_list))
        join->error= 1;
    }
  }
  for (Select_Lex_Unit *unit= join->select_lex->first_inner_unit();
      unit;
      unit= unit->next_unit())
  {
    if (explainUnion(session, unit, result))
      return;
  }
  return;
}

bool optimizer::ExplainPlan::explainUnion(Session *session,
                                          Select_Lex_Unit *unit,
                                          select_result *result)
{
  bool res= false;
  Select_Lex *first= unit->first_select();

  for (Select_Lex *sl= first;
       sl;
       sl= sl->next_select())
  {
    // drop UNCACHEABLE_EXPLAIN, because it is for internal usage only
    uint8_t uncacheable= (sl->uncacheable & ~UNCACHEABLE_EXPLAIN);
    if (&session->lex->select_lex == sl)
    {
      if (sl->first_inner_unit() || sl->next_select())
      {
        sl->type.assign("PRIMARY");
      }
      else
      {
        sl->type.assign("SIMPLE");
      }
    }
    else
    {
      if (sl == first)
      {
        if (sl->linkage == DERIVED_TABLE_TYPE)
        {
          sl->type.assign("DERIVED");
        }
        else
        {
          if (uncacheable & UNCACHEABLE_DEPENDENT)
          {
            sl->type.assign("DEPENDENT SUBQUERY");
          }
          else
          {
            if (uncacheable)
            {
              sl->type.assign("UNCACHEABLE SUBQUERY");
            }
            else
            {
              sl->type.assign("SUBQUERY");
            }
          }
        }
      }
      else
      {
        if (uncacheable & UNCACHEABLE_DEPENDENT)
        {
          sl->type.assign("DEPENDENT UNION");
        }
        else
        {
          if (uncacheable)
          {
            sl->type.assign("UNCACHEABLE_UNION");
          }
          else
          {
            sl->type.assign("UNION");
          }
        }
      }
    }
    sl->options|= SELECT_DESCRIBE;
  }

  if (unit->is_union())
  {
    unit->fake_select_lex->select_number= UINT_MAX; // just for initialization
    unit->fake_select_lex->type.assign("UNION RESULT");
    unit->fake_select_lex->options|= SELECT_DESCRIBE;
    if (! (res= unit->prepare(session, result, SELECT_NO_UNLOCK | SELECT_DESCRIBE)))
    {
      res= unit->exec();
    }
    res|= unit->cleanup();
  }
  else
  {
    session->lex->current_select= first;
    unit->set_limit(unit->global_parameters);
    res= mysql_select(session, 
                      &first->ref_pointer_array,
                      (TableList*) first->table_list.first,
                      first->with_wild, 
                      first->item_list,
                      first->where,
                      first->order_list.elements + first->group_list.elements,
                      (order_st*) first->order_list.first,
                      (order_st*) first->group_list.first,
                      first->having,
                      first->options | session->options | SELECT_DESCRIBE,
                      result, 
                      unit, 
                      first);
  }
  return (res || session->is_error());
}
