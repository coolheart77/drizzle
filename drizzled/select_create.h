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

#ifndef DRIZZLED_SELECT_CREATE_H
#define DRIZZLED_SELECT_CREATE_H

class select_create: public select_insert {
  order_st *group;
  TableList *create_table;
  HA_CREATE_INFO *create_info;
  drizzled::message::Table *table_proto;
  TableList *select_tables;
  AlterInfo *alter_info;
  Field **field;
  /* lock data for tmp table */
  DRIZZLE_LOCK *m_lock;
  /* m_lock or session->extra_lock */
  DRIZZLE_LOCK **m_plock;
public:
  select_create (TableList *table_arg,
		 HA_CREATE_INFO *create_info_par,
                 drizzled::message::Table *proto,
                 AlterInfo *alter_info_arg,
		 List<Item> &select_fields,enum_duplicates duplic, bool ignore,
                 TableList *select_tables_arg)
    :select_insert (NULL, NULL, &select_fields, 0, 0, duplic, ignore),
    create_table(table_arg),
    create_info(create_info_par),
    table_proto(proto),
    select_tables(select_tables_arg),
    alter_info(alter_info_arg),
    m_plock(NULL)
    {}
  int prepare(List<Item> &list, Select_Lex_Unit *u);

  void store_values(List<Item> &values);
  void send_error(uint32_t errcode,const char *err);
  bool send_eof();
  void abort();
  virtual bool can_rollback_data() { return 1; }

  // Needed for access from local class MY_HOOKS in prepare(), since session is proteted.
  const Session *get_session(void) { return session; }
  const HA_CREATE_INFO *get_create_info() { return create_info; };
  int prepare2(void) { return 0; }
};

#endif /* DRIZZLED_SELECT_CREATE_H */
