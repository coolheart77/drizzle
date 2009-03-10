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

#ifndef DRIZZLED_SQL_SELECT_H
#define DRIZZLED_SQL_SELECT_H

#include <drizzled/cached_item.h>
#include <drizzled/session.h>
#include <drizzled/field/varstring.h>
#include <drizzled/item/null.h>

class select_result;

/**
  @file

  @brief
  classes to use when handling where clause
*/


/* PREV_BITS only used in sql_select.cc */
#define PREV_BITS(type,A)	((type) (((type) 1 << (A)) -1))

#include <storage/myisam/myisam.h>
#include <drizzled/sql_array.h>

/* Values in optimize */
#define KEY_OPTIMIZE_EXISTS		1
#define KEY_OPTIMIZE_REF_OR_NULL	2

typedef struct keyuse_t {
  Table *table;
  Item	*val;				/**< or value if no field */
  table_map used_tables;
  uint	key, keypart;
  uint32_t optimize; // 0, or KEY_OPTIMIZE_*
  key_part_map keypart_map;
  ha_rows      ref_table_rows;
  /**
    If true, the comparison this value was created from will not be
    satisfied if val has NULL 'value'.
  */
  bool null_rejecting;
  /*
    !NULL - This KEYUSE was created from an equality that was wrapped into
            an Item_func_trig_cond. This means the equality (and validity of
            this KEYUSE element) can be turned on and off. The on/off state
            is indicted by the pointed value:
              *cond_guard == true <=> equality condition is on
              *cond_guard == false <=> equality condition is off

    NULL  - Otherwise (the source equality can't be turned off)
  */
  bool *cond_guard;
  /*
     0..64    <=> This was created from semi-join IN-equality # sj_pred_no.
     MAX_UINT  Otherwise
  */
  uint32_t         sj_pred_no;
} KEYUSE;

class store_key;

typedef struct st_table_ref
{
  bool		key_err;
  uint32_t      key_parts;                ///< num of ...
  uint32_t      key_length;               ///< length of key_buff
  int32_t       key;                      ///< key no
  unsigned char *key_buff;                ///< value to look for with key
  unsigned char *key_buff2;               ///< key_buff+key_length
  store_key     **key_copy;               //
  Item          **items;                  ///< val()'s for each keypart
  /*
    Array of pointers to trigger variables. Some/all of the pointers may be
    NULL.  The ref access can be used iff

      for each used key part i, (!cond_guards[i] || *cond_guards[i])

    This array is used by subquery code. The subquery code may inject
    triggered conditions, i.e. conditions that can be 'switched off'. A ref
    access created from such condition is not valid when at least one of the
    underlying conditions is switched off (see subquery code for more details)
  */
  bool          **cond_guards;
  /**
    (null_rejecting & (1<<i)) means the condition is '=' and no matching
    rows will be produced if items[i] IS NULL (see add_not_null_conds())
  */
  key_part_map  null_rejecting;
  table_map	depend_map;		  ///< Table depends on these tables.
  /* null byte position in the key_buf. Used for REF_OR_NULL optimization */
  unsigned char *null_ref_key;

  /*
    true <=> disable the "cache" as doing lookup with the same key value may
    produce different results (because of Index Condition Pushdown)
  */
  bool          disable_cache;
} TABLE_REF;


/**
  CACHE_FIELD and JOIN_CACHE is used on full join to cache records in outer
  table
*/

typedef struct st_cache_field {
  /*
    Where source data is located (i.e. this points to somewhere in
    tableX->record[0])
  */
  unsigned char *str;
  uint32_t length; /* Length of data at *str, in bytes */
  uint32_t blob_length; /* Valid IFF blob_field != 0 */
  Field_blob *blob_field;
  bool strip; /* true <=> Strip endspaces ?? */

  Table *get_rowid; /* _ != NULL <=> */
} CACHE_FIELD;


typedef struct st_join_cache
{
  unsigned char *buff;
  unsigned char *pos;    /* Start of free space in the buffer */
  unsigned char *end;
  uint32_t records;  /* # of row cominations currently stored in the cache */
  uint32_t record_nr;
  uint32_t ptr_record;
  /*
    Number of fields (i.e. cache_field objects). Those correspond to table
    columns, and there are also special fields for
     - table's column null bits
     - table's null-complementation byte
     - [new] table's rowid.
  */
  uint32_t fields;
  uint32_t length;
  uint32_t blobs;
  CACHE_FIELD *field;
  CACHE_FIELD **blob_ptr;
  SQL_SELECT *select;
} JOIN_CACHE;


/*
  The structs which holds the join connections and join states
*/
enum join_type { JT_UNKNOWN,JT_SYSTEM,JT_CONST,JT_EQ_REF,JT_REF,JT_MAYBE_REF,
		 JT_ALL, JT_RANGE, JT_NEXT, JT_REF_OR_NULL,
		 JT_UNIQUE_SUBQUERY, JT_INDEX_SUBQUERY, JT_INDEX_MERGE};

class JOIN;

enum enum_nested_loop_state
{
  NESTED_LOOP_KILLED= -2, NESTED_LOOP_ERROR= -1,
  NESTED_LOOP_OK= 0, NESTED_LOOP_NO_MORE_ROWS= 1,
  NESTED_LOOP_QUERY_LIMIT= 3, NESTED_LOOP_CURSOR_LIMIT= 4
};


/* Values for JOIN_TAB::packed_info */
#define TAB_INFO_HAVE_VALUE 1
#define TAB_INFO_USING_INDEX 2
#define TAB_INFO_USING_WHERE 4
#define TAB_INFO_FULL_SCAN_ON_NULL 8

class SJ_TMP_TABLE;

typedef enum_nested_loop_state
(*Next_select_func)(JOIN *, struct st_join_table *, bool);
typedef int (*Read_record_func)(struct st_join_table *tab);
Next_select_func setup_end_select_func(JOIN *join);


typedef struct st_join_table {
  st_join_table() {}                          /* Remove gcc warning */
  Table		*table;
  KEYUSE	*keyuse;			/**< pointer to first used key */
  SQL_SELECT	*select;
  COND		*select_cond;
  QUICK_SELECT_I *quick;
  /*
    The value of select_cond before we've attempted to do Index Condition
    Pushdown. We may need to restore everything back if we first choose one
    index but then reconsider (see test_if_skip_sort_order() for such
    scenarios).
    NULL means no index condition pushdown was performed.
  */
  Item          *pre_idx_push_select_cond;
  Item	       **on_expr_ref;   /**< pointer to the associated on expression   */
  COND_EQUAL    *cond_equal;    /**< multiple equalities for the on expression */
  st_join_table *first_inner;   /**< first inner table for including outerjoin */
  bool           found;         /**< true after all matches or null complement */
  bool           not_null_compl;/**< true before null complement is added      */
  st_join_table *last_inner;    /**< last table table for embedding outer join */
  st_join_table *first_upper;  /**< first inner table for embedding outer join */
  st_join_table *first_unmatched; /**< used for optimization purposes only     */

  /* Special content for EXPLAIN 'Extra' column or NULL if none */
  const char	*info;
  /*
    Bitmap of TAB_INFO_* bits that encodes special line for EXPLAIN 'Extra'
    column, or 0 if there is no info.
  */
  uint32_t          packed_info;

  Read_record_func read_first_record;
  Next_select_func next_select;
  READ_RECORD	read_record;
  /*
    Currently the following two fields are used only for a [NOT] IN subquery
    if it is executed by an alternative full table scan when the left operand of
    the subquery predicate is evaluated to NULL.
  */
  Read_record_func save_read_first_record;/* to save read_first_record */
  int (*save_read_record) (READ_RECORD *);/* to save read_record.read_record */
  double	worst_seeks;
  key_map	const_keys;			/**< Keys with constant part */
  key_map	checked_keys;			/**< Keys checked in find_best */
  key_map	needed_reg;
  key_map       keys;                           /**< all keys with can be used */

  /* Either #rows in the table or 1 for const table.  */
  ha_rows	records;
  /*
    Number of records that will be scanned (yes scanned, not returned) by the
    best 'independent' access method, i.e. table scan or QUICK_*_SELECT)
  */
  ha_rows       found_records;
  /*
    Cost of accessing the table using "ALL" or range/index_merge access
    method (but not 'index' for some reason), i.e. this matches method which
    E(#records) is in found_records.
  */
  ha_rows       read_time;

  table_map	dependent,key_dependent;
  uint		use_quick,index;
  uint		status;				///< Save status for cache
  uint		used_fields,used_fieldlength,used_blobs;
  enum join_type type;
  bool		cached_eq_ref_table,eq_ref_table,not_used_in_distinct;
  /* true <=> index-based access method must return records in order */
  bool		sorted;
  /*
    If it's not 0 the number stored this field indicates that the index
    scan has been chosen to access the table data and we expect to scan
    this number of rows for the table.
  */
  ha_rows       limit;
  TABLE_REF	ref;
  JOIN_CACHE	cache;
  JOIN		*join;
  /** Bitmap of nested joins this table is part of */

  /* SemiJoinDuplicateElimination variables: */
  /*
    Embedding SJ-nest (may be not the direct parent), or NULL if none.
    This variable holds the result of table pullout.
  */
  TableList    *emb_sj_nest;

  /* Variables for semi-join duplicate elimination */
  SJ_TMP_TABLE  *flush_weedout_table;
  SJ_TMP_TABLE  *check_weed_out_table;
  struct st_join_table  *do_firstmatch;

  /*
     ptr  - this join tab should do an InsideOut scan. Points
            to the tab for which we'll need to check tab->found_match.

     NULL - Not an insideout scan.
  */
  struct st_join_table *insideout_match_tab;
  unsigned char *insideout_buf; // Buffer to save index tuple to be able to skip dups

  /* Used by InsideOut scan. Just set to true when have found a row. */
  bool found_match;

  enum {
    /* If set, the rowid of this table must be put into the temptable. */
    KEEP_ROWID=1,
    /*
      If set, one should call h->position() to obtain the rowid,
      otherwise, the rowid is assumed to already be in h->ref
      (this is because join caching and filesort() save the rowid and then
      put it back into h->ref)
    */
    CALL_POSITION=2
  };
  /* A set of flags from the above enum */
  int  rowid_keep_flags;


  /* NestedOuterJoins: Bitmap of nested joins this table is part of */
  nested_join_map embedding_map;

  void cleanup();
  inline bool is_using_loose_index_scan()
  {
    return (select && select->quick &&
            (select->quick->get_type() ==
             QUICK_SELECT_I::QS_TYPE_GROUP_MIN_MAX));
  }
} JOIN_TAB;

enum_nested_loop_state sub_select_cache(JOIN *join, JOIN_TAB *join_tab, bool
                                        end_of_records);
enum_nested_loop_state sub_select(JOIN *join,JOIN_TAB *join_tab, bool
                                  end_of_records);
enum_nested_loop_state end_send_group(JOIN *join, JOIN_TAB *join_tab,
                                      bool end_of_records);
enum_nested_loop_state end_write_group(JOIN *join, JOIN_TAB *join_tab,
                                       bool end_of_records);

/**
  Information about a position of table within a join order. Used in join
  optimization.
*/
typedef struct st_position
{
  /*
    The "fanout": number of output rows that will be produced (after
    pushed down selection condition is applied) per each row combination of
    previous tables.
  */
  double records_read;

  /*
    Cost accessing the table in course of the entire complete join execution,
    i.e. cost of one access method use (e.g. 'range' or 'ref' scan ) times
    number the access method will be invoked.
  */
  double read_time;
  JOIN_TAB *table;

  /*
    NULL  -  'index' or 'range' or 'index_merge' or 'ALL' access is used.
    Other - [eq_]ref[_or_null] access is used. Pointer to {t.keypart1 = expr}
  */
  KEYUSE *key;

  /* If ref-based access is used: bitmap of tables this table depends on  */
  table_map ref_depend_map;

  bool use_insideout_scan;
} POSITION;


typedef struct st_rollup
{
  enum State { STATE_NONE, STATE_INITED, STATE_READY };
  State state;
  Item_null_result **null_items;
  Item ***ref_pointer_arrays;
  List<Item> *fields;
} ROLLUP;


class JOIN :public Sql_alloc
{
  JOIN(const JOIN &rhs);                        /**< not implemented */
  JOIN& operator=(const JOIN &rhs);             /**< not implemented */
public:
  JOIN_TAB *join_tab,**best_ref;
  JOIN_TAB **map2table;    ///< mapping between table indexes and JOIN_TABs
  JOIN_TAB *join_tab_save; ///< saved join_tab for subquery reexecution
  Table    **table,**all_tables;
  /**
    The table which has an index that allows to produce the requried ordering.
    A special value of 0x1 means that the ordering will be produced by
    passing 1st non-const table to filesort(). NULL means no such table exists.
  */
  Table    *sort_by_table;
  uint	   tables;        /**< Number of tables in the join */
  uint32_t     outer_tables;  /**< Number of tables that are not inside semijoin */
  uint32_t     const_tables;
  uint	   send_group_parts;
  bool	   sort_and_group,first_record,full_join,group, no_field_update;
  bool	   do_send_rows;
  /**
    true when we want to resume nested loop iterations when
    fetching data from a cursor
  */
  bool     resume_nested_loop;
  table_map const_table_map,found_const_table_map,outer_join;
  ha_rows  send_records,found_records,examined_rows,row_limit, select_limit;
  /**
    Used to fetch no more than given amount of rows per one
    fetch operation of server side cursor.
    The value is checked in end_send and end_send_group in fashion, similar
    to offset_limit_cnt:
      - fetch_limit= HA_POS_ERROR if there is no cursor.
      - when we open a cursor, we set fetch_limit to 0,
      - on each fetch iteration we add num_rows to fetch to fetch_limit
  */
  ha_rows  fetch_limit;
  POSITION positions[MAX_TABLES+1],best_positions[MAX_TABLES+1];

  /* *
    Bitmap of nested joins embedding the position at the end of the current
    partial join (valid only during join optimizer run).
  */
  nested_join_map cur_embedding_map;

  double   best_read;
  List<Item> *fields;
  List<Cached_item> group_fields, group_fields_cache;
  Table    *tmp_table;
  /// used to store 2 possible tmp table of SELECT
  Table    *exec_tmp_table1, *exec_tmp_table2;
  Session	   *session;
  Item_sum  **sum_funcs, ***sum_funcs_end;
  /** second copy of sumfuncs (for queries with 2 temporary tables */
  Item_sum  **sum_funcs2, ***sum_funcs_end2;
  Item	    *having;
  Item      *tmp_having; ///< To store having when processed temporary table
  Item      *having_history; ///< Store having for explain
  uint64_t  select_options;
  select_result *result;
  Tmp_Table_Param tmp_table_param;
  DRIZZLE_LOCK *lock;
  /// unit structure (with global parameters) for this select
  Select_Lex_Unit *unit;
  /// select that processed
  Select_Lex *select_lex;
  /**
    true <=> optimizer must not mark any table as a constant table.
    This is needed for subqueries in form "a IN (SELECT .. UNION SELECT ..):
    when we optimize the select that reads the results of the union from a
    temporary table, we must not mark the temp. table as constant because
    the number of rows in it may vary from one subquery execution to another.
  */
  bool no_const_tables;

  JOIN *tmp_join; ///< copy of this JOIN to be used with temporary tables
  ROLLUP rollup;				///< Used with rollup

  bool select_distinct;				///< Set if SELECT DISTINCT
  /**
    If we have the GROUP BY statement in the query,
    but the group_list was emptied by optimizer, this
    flag is true.
    It happens when fields in the GROUP BY are from
    constant table
  */
  bool group_optimized_away;

  /*
    simple_xxxxx is set if order_st/GROUP BY doesn't include any references
    to other tables than the first non-constant table in the JOIN.
    It's also set if order_st/GROUP BY is empty.
  */
  bool simple_order, simple_group;
  /**
    Is set only in case if we have a GROUP BY clause
    and no order_st BY after constant elimination of 'order'.
  */
  bool no_order;
  /** Is set if we have a GROUP BY and we have order_st BY on a constant. */
  bool          skip_sort_order;

  bool need_tmp, hidden_group_fields;
  DYNAMIC_ARRAY keyuse;
  Item::cond_result cond_value, having_value;
  List<Item> all_fields; ///< to store all fields that used in query
  ///Above list changed to use temporary table
  List<Item> tmp_all_fields1, tmp_all_fields2, tmp_all_fields3;
  ///Part, shared with list above, emulate following list
  List<Item> tmp_fields_list1, tmp_fields_list2, tmp_fields_list3;
  List<Item> &fields_list; ///< hold field list passed to mysql_select
  int error;

  order_st *order, *group_list, *proc_param; //hold parameters of mysql_select
  COND *conds;                            // ---"---
  Item *conds_history;                    // store WHERE for explain
  TableList *tables_list;           ///<hold 'tables' parameter of mysql_select
  List<TableList> *join_list;       ///< list of joined tables in reverse order
  COND_EQUAL *cond_equal;
  SQL_SELECT *select;                ///<created in optimisation phase
  JOIN_TAB *return_tab;              ///<used only for outer joins
  Item **ref_pointer_array; ///<used pointer reference for this select
  // Copy of above to be used with different lists
  Item **items0, **items1, **items2, **items3, **current_ref_pointer_array;
  uint32_t ref_pointer_array_size; ///< size of above in bytes
  const char *zero_result_cause; ///< not 0 if exec must return zero result

  bool union_part; ///< this subselect is part of union
  bool optimized; ///< flag to avoid double optimization in EXPLAIN

  Array<Item_in_subselect> sj_subselects;

  /* Descriptions of temporary tables used to weed-out semi-join duplicates */
  SJ_TMP_TABLE  *sj_tmp_tables;

  table_map cur_emb_sj_nests;

  /*
    storage for caching buffers allocated during query execution.
    These buffers allocations need to be cached as the thread memory pool is
    cleared only at the end of the execution of the whole query and not caching
    allocations that occur in repetition at execution time will result in
    excessive memory usage.
  */
  SORT_FIELD *sortorder;                        // make_unireg_sortorder()
  Table **table_reexec;                         // make_simple_join()
  JOIN_TAB *join_tab_reexec;                    // make_simple_join()
  /* end of allocation caching storage */

  JOIN(Session *session_arg, List<Item> &fields_arg, uint64_t select_options_arg,
       select_result *result_arg)
    :fields_list(fields_arg), sj_subselects(session_arg->mem_root, 4)
  {
    init(session_arg, fields_arg, select_options_arg, result_arg);
  }

  void init(Session *session_arg, List<Item> &fields_arg, uint64_t select_options_arg,
       select_result *result_arg)
  {
    join_tab= join_tab_save= 0;
    table= 0;
    tables= 0;
    const_tables= 0;
    join_list= 0;
    sort_and_group= 0;
    first_record= 0;
    do_send_rows= 1;
    resume_nested_loop= false;
    send_records= 0;
    found_records= 0;
    fetch_limit= HA_POS_ERROR;
    examined_rows= 0;
    exec_tmp_table1= 0;
    exec_tmp_table2= 0;
    sortorder= 0;
    table_reexec= 0;
    join_tab_reexec= 0;
    session= session_arg;
    sum_funcs= sum_funcs2= 0;
    having= tmp_having= having_history= 0;
    select_options= select_options_arg;
    result= result_arg;
    lock= session_arg->lock;
    select_lex= 0; //for safety
    tmp_join= 0;
    select_distinct= test(select_options & SELECT_DISTINCT);
    no_order= 0;
    simple_order= 0;
    simple_group= 0;
    skip_sort_order= 0;
    need_tmp= 0;
    hidden_group_fields= 0; /*safety*/
    error= 0;
    select= 0;
    return_tab= 0;
    ref_pointer_array= items0= items1= items2= items3= 0;
    ref_pointer_array_size= 0;
    zero_result_cause= 0;
    optimized= 0;
    cond_equal= 0;
    group_optimized_away= 0;

    all_fields= fields_arg;
    if (&fields_list != &fields_arg) /* only copy if not same*/
      fields_list= fields_arg;
    memset(&keyuse, 0, sizeof(keyuse));
    tmp_table_param.init();
    tmp_table_param.end_write_records= HA_POS_ERROR;
    rollup.state= ROLLUP::STATE_NONE;
    sj_tmp_tables= NULL;

    no_const_tables= false;
  }

  int prepare(Item ***rref_pointer_array, TableList *tables, uint32_t wind_num,
	      COND *conds, uint32_t og_num, order_st *order, order_st *group,
	      Item *having, order_st *proc_param, Select_Lex *select,
	      Select_Lex_Unit *unit);
  int optimize();
  int reinit();
  void exec();
  int destroy();
  void restore_tmp();
  bool alloc_func_list();
  bool flatten_subqueries();
  bool setup_subquery_materialization();
  bool make_sum_func_list(List<Item> &all_fields, List<Item> &send_fields,
			  bool before_group_by, bool recompute= false);

  inline void set_items_ref_array(Item **ptr)
  {
    memcpy(ref_pointer_array, ptr, ref_pointer_array_size);
    current_ref_pointer_array= ptr;
  }
  inline void init_items_ref_array()
  {
    items0= ref_pointer_array + all_fields.elements;
    memcpy(items0, ref_pointer_array, ref_pointer_array_size);
    current_ref_pointer_array= items0;
  }

  bool rollup_init();
  bool rollup_make_fields(List<Item> &all_fields, List<Item> &fields,
			  Item_sum ***func);
  int rollup_send_data(uint32_t idx);
  int rollup_write_data(uint32_t idx, Table *table);
  void remove_subq_pushed_predicates(Item **where);
  /**
    Release memory and, if possible, the open tables held by this execution
    plan (and nested plans). It's used to release some tables before
    the end of execution in order to increase concurrency and reduce
    memory consumption.
  */
  void join_free();
  /** Cleanup this JOIN, possibly for reuse */
  void cleanup(bool full);
  void clear();
  bool save_join_tab();
  bool init_save_join_tab();
  bool send_row_on_empty_set()
  {
    return (do_send_rows && tmp_table_param.sum_func_count != 0 &&
	    !group_list);
  }
  bool change_result(select_result *result);
  bool is_top_level_join() const
  {
    return (unit == &session->lex->unit && (unit->fake_select_lex == 0 ||
                                        select_lex == unit->fake_select_lex));
  }
};


typedef struct st_select_check {
  uint32_t const_ref,reg_ref;
} SELECT_CHECK;

extern const char *join_type_str[];
void TEST_join(JOIN *join);

/* Extern functions in sql_select.cc */
bool store_val_in_field(Field *field, Item *val, enum_check_fields check_flag);
Table *create_tmp_table(Session *session,Tmp_Table_Param *param,List<Item> &fields,
			order_st *group, bool distinct, bool save_sum_fields,
			uint64_t select_options, ha_rows rows_limit,
			char* alias);
void free_tmp_table(Session *session, Table *entry);
void count_field_types(Select_Lex *select_lex, Tmp_Table_Param *param,
                       List<Item> &fields, bool reset_with_sum_func);
bool setup_copy_fields(Session *session, Tmp_Table_Param *param,
		       Item **ref_pointer_array,
		       List<Item> &new_list1, List<Item> &new_list2,
		       uint32_t elements, List<Item> &fields);
void copy_fields(Tmp_Table_Param *param);
void copy_funcs(Item **func_ptr);
Field* create_tmp_field_from_field(Session *session, Field* org_field,
                                   const char *name, Table *table,
                                   Item_field *item, uint32_t convert_blob_length);

/* functions from opt_sum.cc */
bool simple_pred(Item_func *func_item, Item **args, bool *inv_order);
int opt_sum_query(TableList *tables, List<Item> &all_fields,COND *conds);

/* from sql_delete.cc, used by opt_range.cc */
extern "C" int refpos_order_cmp(void* arg, const void *a,const void *b);

/** class to copying an field/item to a key struct */

class store_key :public Sql_alloc
{
public:
  bool null_key; /* true <=> the value of the key has a null part */
  enum store_key_result { STORE_KEY_OK, STORE_KEY_FATAL, STORE_KEY_CONV };
  store_key(Session *session, Field *field_arg, unsigned char *ptr, unsigned char *null, uint32_t length)
    :null_key(0), null_ptr(null), err(0)
  {
    if (field_arg->type() == DRIZZLE_TYPE_BLOB)
    {
      /*
        Key segments are always packed with a 2 byte length prefix.
        See mi_rkey for details.
      */
      to_field= new Field_varstring(ptr, length, 2, null, 1,
                                    Field::NONE, field_arg->field_name,
                                    field_arg->table->s, field_arg->charset());
      to_field->init(field_arg->table);
    }
    else
      to_field=field_arg->new_key_field(session->mem_root, field_arg->table,
                                        ptr, null, 1);
  }
  virtual ~store_key() {}			/** Not actually needed */
  virtual const char *name() const=0;

  /**
    @brief sets ignore truncation warnings mode and calls the real copy method

    @details this function makes sure truncation warnings when preparing the
    key buffers don't end up as errors (because of an enclosing INSERT/UPDATE).
  */
  enum store_key_result copy()
  {
    enum store_key_result result;
    Session *session= to_field->table->in_use;
    enum_check_fields saved_count_cuted_fields= session->count_cuted_fields;

    session->count_cuted_fields= CHECK_FIELD_IGNORE;

    result= copy_inner();

    session->count_cuted_fields= saved_count_cuted_fields;

    return result;
  }

 protected:
  Field *to_field;				// Store data here
  unsigned char *null_ptr;
  unsigned char err;

  virtual enum store_key_result copy_inner()=0;
};


class store_key_field: public store_key
{
  Copy_field copy_field;
  const char *field_name;
 public:
  store_key_field(Session *session, Field *to_field_arg, unsigned char *ptr,
                  unsigned char *null_ptr_arg,
		  uint32_t length, Field *from_field, const char *name_arg)
    :store_key(session, to_field_arg,ptr,
	       null_ptr_arg ? null_ptr_arg : from_field->maybe_null() ? &err
	       : (unsigned char*) 0, length), field_name(name_arg)
  {
    if (to_field)
    {
      copy_field.set(to_field,from_field,0);
    }
  }
  const char *name() const { return field_name; }

 protected:
  enum store_key_result copy_inner()
  {
    copy_field.do_copy(&copy_field);
    null_key= to_field->is_null();
    return err != 0 ? STORE_KEY_FATAL : STORE_KEY_OK;
  }
};


class store_key_item :public store_key
{
 protected:
  Item *item;
public:
  store_key_item(Session *session, Field *to_field_arg, unsigned char *ptr,
                 unsigned char *null_ptr_arg, uint32_t length, Item *item_arg)
    :store_key(session, to_field_arg, ptr,
	       null_ptr_arg ? null_ptr_arg : item_arg->maybe_null ?
	       &err : (unsigned char*) 0, length), item(item_arg)
  {}
  const char *name() const { return "func"; }

 protected:
  enum store_key_result copy_inner()
  {
    int res= item->save_in_field(to_field, 1);
    null_key= to_field->is_null() || item->null_value;
    return (err != 0 || res > 2 ? STORE_KEY_FATAL : (store_key_result) res);
  }
};


class store_key_const_item :public store_key_item
{
  bool inited;
public:
  store_key_const_item(Session *session, Field *to_field_arg, unsigned char *ptr,
		       unsigned char *null_ptr_arg, uint32_t length,
		       Item *item_arg)
    :store_key_item(session, to_field_arg,ptr,
		    null_ptr_arg ? null_ptr_arg : item_arg->maybe_null ?
		    &err : (unsigned char*) 0, length, item_arg), inited(0)
  {
  }
  const char *name() const { return "const"; }

protected:
  enum store_key_result copy_inner()
  {
    int res;
    if (!inited)
    {
      inited=1;
      if ((res= item->save_in_field(to_field, 1)))
      {
        if (!err)
          err= res;
      }
    }
    null_key= to_field->is_null() || item->null_value;
    return (err > 2 ?  STORE_KEY_FATAL : (store_key_result) err);
  }
};

bool cp_buffer_from_ref(Session *session, TABLE_REF *ref);
bool error_if_full_join(JOIN *join);
int safe_index_read(JOIN_TAB *tab);
COND *remove_eq_conds(Session *session, COND *cond, Item::cond_result *cond_value);
int test_if_item_cache_changed(List<Cached_item> &list);

#endif /* DRIZZLED_SQL_SELECT_H */