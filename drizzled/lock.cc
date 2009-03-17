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

  Locking functions for mysql.

  Because of the new concurrent inserts, we must first get external locks
  before getting internal locks.  If we do it in the other order, the status
  information is not up to date when called from the lock handler.

  GENERAL DESCRIPTION OF LOCKING

  When not using LOCK TABLES:

  - For each SQL statement mysql_lock_tables() is called for all involved
    tables.
    - mysql_lock_tables() will call
      table_handler->external_lock(session,locktype) for each table.
      This is followed by a call to thr_multi_lock() for all tables.

  - When statement is done, we call mysql_unlock_tables().
    This will call thr_multi_unlock() followed by
    table_handler->external_lock(session, F_UNLCK) for each table.

  - Note that mysql_unlock_tables() may be called several times as
    MySQL in some cases can free some tables earlier than others.

  - The above is true both for normal and temporary tables.

  - Temporary non transactional tables are never passed to thr_multi_lock()
    and we never call external_lock(session, F_UNLOCK) on these.

  When using LOCK TABLES:

  - LOCK Table will call mysql_lock_tables() for all tables.
    mysql_lock_tables() will call
    table_handler->external_lock(session,locktype) for each table.
    This is followed by a call to thr_multi_lock() for all tables.

  - For each statement, we will call table_handler->start_stmt(Session)
    to inform the table handler that we are using the table.

    The tables used can only be tables used in LOCK TABLES or a
    temporary table.

  - When statement is done, we will call ha_commit_stmt(session);

  - When calling UNLOCK TABLES we call mysql_unlock_tables() for all
    tables used in LOCK TABLES

  If table_handler->external_lock(session, locktype) fails, we call
  table_handler->external_lock(session, F_UNLCK) for each table that was locked,
  excluding one that caused failure. That means handler must cleanup itself
  in case external_lock() fails.

  @todo
  Change to use malloc() ONLY when using LOCK TABLES command or when
  we are forced to use mysql_lock_merge.
*/
#include <drizzled/server_includes.h>
#include <drizzled/error.h>
#include <mysys/hash.h>
#include <mysys/thr_lock.h>
#include <drizzled/session.h>
#include <drizzled/sql_base.h>
#include <drizzled/lock.h>

/**
  @defgroup Locking Locking
  @{
*/

extern HASH open_cache;

/* flags for get_lock_data */
#define GET_LOCK_UNLOCK         1
#define GET_LOCK_STORE_LOCKS    2

static DRIZZLE_LOCK *get_lock_data(Session *session, Table **table,
                                   uint32_t count,
                                   uint32_t flags, Table **write_locked);
static int lock_external(Session *session, Table **table,uint32_t count);
static int unlock_external(Session *session, Table **table,uint32_t count);
static void print_lock_error(int error, const char *);

/*
  Lock tables.

  SYNOPSIS
    mysql_lock_tables()
    session                         The current thread.
    tables                      An array of pointers to the tables to lock.
    count                       The number of tables to lock.
    flags                       Options:
      DRIZZLE_LOCK_IGNORE_GLOBAL_READ_LOCK      Ignore a global read lock
      DRIZZLE_LOCK_IGNORE_FLUSH                 Ignore a flush tables.
      DRIZZLE_LOCK_NOTIFY_IF_NEED_REOPEN        Instead of reopening altered
                                              or dropped tables by itself,
                                              mysql_lock_tables() should
                                              notify upper level and rely
                                              on caller doing this.
    need_reopen                 Out parameter, TRUE if some tables were altered
                                or deleted and should be reopened by caller.

  RETURN
    A lock structure pointer on success.
    NULL on error or if some tables should be reopen.
*/

/* Map the return value of thr_lock to an error from errmsg.txt */
static int thr_lock_errno_to_mysql[]=
{ 0, 1, ER_LOCK_WAIT_TIMEOUT, ER_LOCK_DEADLOCK };


/**
  Reset lock type in lock data and free.

  @param mysql_lock Lock structures to reset.

  @note After a locking error we want to quit the locking of the table(s).
        The test case in the bug report for Bug #18544 has the following
        cases: 1. Locking error in lock_external() due to InnoDB timeout.
        2. Locking error in get_lock_data() due to missing write permission.
        3. Locking error in wait_if_global_read_lock() due to lock conflict.

  @note In all these cases we have already set the lock type into the lock
        data of the open table(s). If the table(s) are in the open table
        cache, they could be reused with the non-zero lock type set. This
        could lead to ignoring a different lock type with the next lock.

  @note Clear the lock type of all lock data. This ensures that the next
        lock request will set its lock type properly.
*/

static void reset_lock_data_and_free(DRIZZLE_LOCK **mysql_lock)
{
  DRIZZLE_LOCK *sql_lock= *mysql_lock;
  THR_LOCK_DATA **ldata, **ldata_end;

  /* Clear the lock type of all lock data to avoid reusage. */
  for (ldata= sql_lock->locks, ldata_end= ldata + sql_lock->lock_count;
       ldata < ldata_end;
       ldata++)
  {
    /* Reset lock type. */
    (*ldata)->type= TL_UNLOCK;
  }
  free((unsigned char*) sql_lock);
  *mysql_lock= 0;
}


DRIZZLE_LOCK *mysql_lock_tables(Session *session, Table **tables, uint32_t count,
                              uint32_t flags, bool *need_reopen)
{
  DRIZZLE_LOCK *sql_lock;
  Table *write_lock_used;
  int rc;

  *need_reopen= false;

  for (;;)
  {
    if (! (sql_lock= get_lock_data(session, tables, count, GET_LOCK_STORE_LOCKS,
                                   &write_lock_used)))
      break;

    if (global_read_lock && write_lock_used &&
        ! (flags & DRIZZLE_LOCK_IGNORE_GLOBAL_READ_LOCK))
    {
      /*
	Someone has issued LOCK ALL TABLES FOR READ and we want a write lock
	Wait until the lock is gone
      */
      if (wait_if_global_read_lock(session, 1, 1))
      {
        /* Clear the lock type of all lock data to avoid reusage. */
        reset_lock_data_and_free(&sql_lock);
	break;
      }
      if (session->version != refresh_version)
      {
        /* Clear the lock type of all lock data to avoid reusage. */
        reset_lock_data_and_free(&sql_lock);
	goto retry;
      }
    }

    session->set_proc_info("System lock");
    if (sql_lock->table_count && lock_external(session, sql_lock->table,
                                               sql_lock->table_count))
    {
      /* Clear the lock type of all lock data to avoid reusage. */
      reset_lock_data_and_free(&sql_lock);
      break;
    }
    session->set_proc_info("Table lock");
    /* Copy the lock data array. thr_multi_lock() reorders its contens. */
    memcpy(sql_lock->locks + sql_lock->lock_count, sql_lock->locks,
           sql_lock->lock_count * sizeof(*sql_lock->locks));
    /* Lock on the copied half of the lock data array. */
    rc= thr_lock_errno_to_mysql[(int) thr_multi_lock(sql_lock->locks +
                                                     sql_lock->lock_count,
                                                     sql_lock->lock_count,
                                                     session->lock_id)];
    if (rc > 1)                                 /* a timeout or a deadlock */
    {
      if (sql_lock->table_count)
        unlock_external(session, sql_lock->table, sql_lock->table_count);
      reset_lock_data_and_free(&sql_lock);
      my_error(rc, MYF(0));
      break;
    }
    else if (rc == 1)                           /* aborted */
    {
      session->some_tables_deleted=1;		// Try again
      sql_lock->lock_count= 0;                  // Locks are already freed
      // Fall through: unlock, reset lock data, free and retry
    }
    else if (!session->some_tables_deleted || (flags & DRIZZLE_LOCK_IGNORE_FLUSH))
    {
      /*
        Thread was killed or lock aborted. Let upper level close all
        used tables and retry or give error.
      */
      break;
    }
    else if (!session->open_tables)
    {
      // Only using temporary tables, no need to unlock
      session->some_tables_deleted=0;
      break;
    }
    session->set_proc_info(0);

    /* going to retry, unlock all tables */
    if (sql_lock->lock_count)
        thr_multi_unlock(sql_lock->locks, sql_lock->lock_count);

    if (sql_lock->table_count)
      unlock_external(session, sql_lock->table, sql_lock->table_count);

    /*
      If thr_multi_lock fails it resets lock type for tables, which
      were locked before (and including) one that caused error. Lock
      type for other tables preserved.
    */
    reset_lock_data_and_free(&sql_lock);
retry:
    if (flags & DRIZZLE_LOCK_NOTIFY_IF_NEED_REOPEN)
    {
      *need_reopen= true;
      break;
    }
    if (wait_for_tables(session))
      break;					// Couldn't open tables
  }
  session->set_proc_info(0);
  if (session->killed)
  {
    session->send_kill_message();
    if (sql_lock)
    {
      mysql_unlock_tables(session,sql_lock);
      sql_lock=0;
    }
  }

  session->set_time_after_lock();
  return (sql_lock);
}


static int lock_external(Session *session, Table **tables, uint32_t count)
{
  register uint32_t i;
  int lock_type,error;
  for (i=1 ; i <= count ; i++, tables++)
  {
    assert((*tables)->reginfo.lock_type >= TL_READ);
    lock_type=F_WRLCK;				/* Lock exclusive */
    if ((*tables)->db_stat & HA_READ_ONLY ||
	((*tables)->reginfo.lock_type >= TL_READ &&
	 (*tables)->reginfo.lock_type <= TL_READ_NO_INSERT))
      lock_type=F_RDLCK;

    if ((error=(*tables)->file->ha_external_lock(session,lock_type)))
    {
      print_lock_error(error, (*tables)->file->table_type());
      while (--i)
      {
        tables--;
	(*tables)->file->ha_external_lock(session, F_UNLCK);
	(*tables)->current_lock=F_UNLCK;
      }
      return(error);
    }
    else
    {
      (*tables)->db_stat &= ~ HA_BLOCK_LOCK;
      (*tables)->current_lock= lock_type;
    }
  }
  return(0);
}


void mysql_unlock_tables(Session *session, DRIZZLE_LOCK *sql_lock)
{
  if (sql_lock->lock_count)
    thr_multi_unlock(sql_lock->locks,sql_lock->lock_count);
  if (sql_lock->table_count)
    unlock_external(session,sql_lock->table,sql_lock->table_count);
  free((unsigned char*) sql_lock);
  return;
}

/**
  Unlock some of the tables locked by mysql_lock_tables.

  This will work even if get_lock_data fails (next unlock will free all)
*/

void mysql_unlock_some_tables(Session *session, Table **table,uint32_t count)
{
  DRIZZLE_LOCK *sql_lock;
  Table *write_lock_used;
  if ((sql_lock= get_lock_data(session, table, count, GET_LOCK_UNLOCK,
                               &write_lock_used)))
    mysql_unlock_tables(session, sql_lock);
}


/**
  unlock all tables locked for read.
*/

void mysql_unlock_read_tables(Session *session, DRIZZLE_LOCK *sql_lock)
{
  uint32_t i,found;

  /* Move all write locks first */
  THR_LOCK_DATA **lock=sql_lock->locks;
  for (i=found=0 ; i < sql_lock->lock_count ; i++)
  {
    if (sql_lock->locks[i]->type >= TL_WRITE_ALLOW_READ)
    {
      std::swap(*lock, sql_lock->locks[i]);
      lock++;
      found++;
    }
  }
  /* unlock the read locked tables */
  if (i != found)
  {
    thr_multi_unlock(lock,i-found);
    sql_lock->lock_count= found;
  }

  /* Then do the same for the external locks */
  /* Move all write locked tables first */
  Table **table=sql_lock->table;
  for (i=found=0 ; i < sql_lock->table_count ; i++)
  {
    assert(sql_lock->table[i]->lock_position == i);
    if ((uint32_t) sql_lock->table[i]->reginfo.lock_type >= TL_WRITE_ALLOW_READ)
    {
      std::swap(*table, sql_lock->table[i]);
      table++;
      found++;
    }
  }
  /* Unlock all read locked tables */
  if (i != found)
  {
    unlock_external(session,table,i-found);
    sql_lock->table_count=found;
  }
  /* Fix the lock positions in Table */
  table= sql_lock->table;
  found= 0;
  for (i= 0; i < sql_lock->table_count; i++)
  {
    Table *tbl= *table;
    tbl->lock_position= table - sql_lock->table;
    tbl->lock_data_start= found;
    found+= tbl->lock_count;
    table++;
  }
  return;
}


/**
  Try to find the table in the list of locked tables.
  In case of success, unlock the table and remove it from this list.

  @note This function has a legacy side effect: the table is
  unlocked even if it is not found in the locked list.
  It's not clear if this side effect is intentional or still
  desirable. It might lead to unmatched calls to
  unlock_external(). Moreover, a discrepancy can be left
  unnoticed by the storage engine, because in
  unlock_external() we call handler::external_lock(F_UNLCK) only
  if table->current_lock is not F_UNLCK.

  @param  session             thread context
  @param  locked          list of locked tables
  @param  table           the table to unlock
  @param  always_unlock   specify explicitly if the legacy side
                          effect is desired.
*/

void mysql_lock_remove(Session *session, DRIZZLE_LOCK *locked,Table *table,
                       bool always_unlock)
{
  if (always_unlock == true)
    mysql_unlock_some_tables(session, &table, /* table count */ 1);
  if (locked)
  {
    register uint32_t i;
    for (i=0; i < locked->table_count; i++)
    {
      if (locked->table[i] == table)
      {
        uint32_t  j, removed_locks, old_tables;
        Table *tbl;
        uint32_t lock_data_end;

        assert(table->lock_position == i);

        /* Unlock if not yet unlocked */
        if (always_unlock == false)
          mysql_unlock_some_tables(session, &table, /* table count */ 1);

        /* Decrement table_count in advance, making below expressions easier */
        old_tables= --locked->table_count;

        /* The table has 'removed_locks' lock data elements in locked->locks */
        removed_locks= table->lock_count;

        /* Move down all table pointers above 'i'. */
        memmove((locked->table+i), (locked->table+i+1),
                (old_tables - i) * sizeof(Table*));

        lock_data_end= table->lock_data_start + table->lock_count;
        /* Move down all lock data pointers above 'table->lock_data_end-1' */
        memmove((locked->locks + table->lock_data_start),
                (locked->locks + lock_data_end),
                (locked->lock_count - lock_data_end) *
                sizeof(THR_LOCK_DATA*));

        /*
          Fix moved table elements.
          lock_position is the index in the 'locked->table' array,
          it must be fixed by one.
          table->lock_data_start is pointer to the lock data for this table
          in the 'locked->locks' array, they must be fixed by 'removed_locks',
          the lock data count of the removed table.
        */
        for (j= i ; j < old_tables; j++)
        {
          tbl= locked->table[j];
          tbl->lock_position--;
          assert(tbl->lock_position == j);
          tbl->lock_data_start-= removed_locks;
        }

        /* Finally adjust lock_count. */
        locked->lock_count-= removed_locks;
	break;
      }
    }
  }
}

/* Downgrade all locks on a table to new WRITE level from WRITE_ONLY */

void mysql_lock_downgrade_write(Session *session, Table *table,
                                thr_lock_type new_lock_type)
{
  DRIZZLE_LOCK *locked;
  Table *write_lock_used;
  if ((locked = get_lock_data(session, &table, 1, GET_LOCK_UNLOCK,
                              &write_lock_used)))
  {
    for (uint32_t i=0; i < locked->lock_count; i++)
      thr_downgrade_write_lock(locked->locks[i], new_lock_type);
    free((unsigned char*) locked);
  }
}


/** Abort all other threads waiting to get lock in table. */

void mysql_lock_abort(Session *session, Table *table, bool upgrade_lock)
{
  DRIZZLE_LOCK *locked;
  Table *write_lock_used;

  if ((locked= get_lock_data(session, &table, 1, GET_LOCK_UNLOCK,
                             &write_lock_used)))
  {
    for (uint32_t i=0; i < locked->lock_count; i++)
      thr_abort_locks(locked->locks[i]->lock, upgrade_lock);
    free((unsigned char*) locked);
  }
  return;
}


/**
  Abort one thread / table combination.

  @param session	   Thread handler
  @param table	   Table that should be removed from lock queue

  @retval
    0  Table was not locked by another thread
  @retval
    1  Table was locked by at least one other thread
*/

bool mysql_lock_abort_for_thread(Session *session, Table *table)
{
  DRIZZLE_LOCK *locked;
  Table *write_lock_used;
  bool result= false;

  if ((locked= get_lock_data(session, &table, 1, GET_LOCK_UNLOCK,
                             &write_lock_used)))
  {
    for (uint32_t i=0; i < locked->lock_count; i++)
    {
      if (thr_abort_locks_for_thread(locked->locks[i]->lock,
                                     table->in_use->thread_id))
        result= true;
    }
    free((unsigned char*) locked);
  }
  return(result);
}


DRIZZLE_LOCK *mysql_lock_merge(DRIZZLE_LOCK *a,DRIZZLE_LOCK *b)
{
  DRIZZLE_LOCK *sql_lock;
  Table **table, **end_table;

  if (!(sql_lock= (DRIZZLE_LOCK*)
	malloc(sizeof(*sql_lock)+
               sizeof(THR_LOCK_DATA*)*(a->lock_count+b->lock_count)+
               sizeof(Table*)*(a->table_count+b->table_count))))
    return(0);				// Fatal error
  sql_lock->lock_count=a->lock_count+b->lock_count;
  sql_lock->table_count=a->table_count+b->table_count;
  sql_lock->locks=(THR_LOCK_DATA**) (sql_lock+1);
  sql_lock->table=(Table**) (sql_lock->locks+sql_lock->lock_count);
  memcpy(sql_lock->locks,a->locks,a->lock_count*sizeof(*a->locks));
  memcpy(sql_lock->locks+a->lock_count,b->locks,
	 b->lock_count*sizeof(*b->locks));
  memcpy(sql_lock->table,a->table,a->table_count*sizeof(*a->table));
  memcpy(sql_lock->table+a->table_count,b->table,
         b->table_count*sizeof(*b->table));

  /*
    Now adjust lock_position and lock_data_start for all objects that was
    moved in 'b' (as there is now all objects in 'a' before these).
  */
  for (table= sql_lock->table + a->table_count,
         end_table= table + b->table_count;
       table < end_table;
       table++)
  {
    (*table)->lock_position+=   a->table_count;
    (*table)->lock_data_start+= a->lock_count;
  }

  /* Delete old, not needed locks */
  free((unsigned char*) a);
  free((unsigned char*) b);
  return(sql_lock);
}


/**
  Find duplicate lock in tables.

  Temporary tables are ignored here like they are ignored in
  get_lock_data(). If we allow two opens on temporary tables later,
  both functions should be checked.

  @param session                 The current thread.
  @param needle              The table to check for duplicate lock.
  @param haystack            The list of tables to search for the dup lock.

  @note
    This is mainly meant for MERGE tables in INSERT ... SELECT
    situations. The 'real', underlying tables can be found only after
    the MERGE tables are opened. This function assumes that the tables are
    already locked.

  @retval
    NULL    No duplicate lock found.
  @retval
    !NULL   First table from 'haystack' that matches a lock on 'needle'.
*/

TableList *mysql_lock_have_duplicate(Session *session, TableList *needle,
                                      TableList *haystack)
{
  DRIZZLE_LOCK            *mylock;
  Table                 **lock_tables;
  Table                 *table;
  Table                 *table2;
  THR_LOCK_DATA         **lock_locks;
  THR_LOCK_DATA         **table_lock_data;
  THR_LOCK_DATA         **end_data;
  THR_LOCK_DATA         **lock_data2;
  THR_LOCK_DATA         **end_data2;

  /*
    Table may not be defined for derived or view tables.
    Table may not be part of a lock for delayed operations.
  */
  if (! (table= needle->table) || ! table->lock_count)
    goto end;

  /* A temporary table does not have locks. */
  if (table->s->tmp_table == NON_TRANSACTIONAL_TMP_TABLE)
    goto end;

  /* Get command lock or LOCK TABLES lock. Maybe empty for INSERT DELAYED. */
  if (! (mylock= session->lock ? session->lock : session->locked_tables))
    goto end;

  /* If we have less than two tables, we cannot have duplicates. */
  if (mylock->table_count < 2)
    goto end;

  lock_locks=  mylock->locks;
  lock_tables= mylock->table;

  /* Prepare table related variables that don't change in loop. */
  assert((table->lock_position < mylock->table_count) &&
              (table == lock_tables[table->lock_position]));
  table_lock_data= lock_locks + table->lock_data_start;
  end_data= table_lock_data + table->lock_count;

  for (; haystack; haystack= haystack->next_global)
  {
    if (haystack->placeholder())
      continue;
    table2= haystack->table;
    if (table2->s->tmp_table == NON_TRANSACTIONAL_TMP_TABLE)
      continue;

    /* All tables in list must be in lock. */
    assert((table2->lock_position < mylock->table_count) &&
                (table2 == lock_tables[table2->lock_position]));

    for (lock_data2=  lock_locks + table2->lock_data_start,
           end_data2= lock_data2 + table2->lock_count;
         lock_data2 < end_data2;
         lock_data2++)
    {
      THR_LOCK_DATA **lock_data;
      THR_LOCK *lock2= (*lock_data2)->lock;

      for (lock_data= table_lock_data;
           lock_data < end_data;
           lock_data++)
      {
        if ((*lock_data)->lock == lock2)
        {
          return(haystack);
        }
      }
    }
  }

 end:
  return(NULL);
}


/** Unlock a set of external. */

static int unlock_external(Session *session, Table **table,uint32_t count)
{
  int error,error_code;

  error_code=0;
  do
  {
    if ((*table)->current_lock != F_UNLCK)
    {
      (*table)->current_lock = F_UNLCK;
      if ((error=(*table)->file->ha_external_lock(session, F_UNLCK)))
      {
	error_code=error;
	print_lock_error(error_code, (*table)->file->table_type());
      }
    }
    table++;
  } while (--count);
  return(error_code);
}


/**
  Get lock structures from table structs and initialize locks.

  @param session		    Thread handler
  @param table_ptr	    Pointer to tables that should be locks
  @param flags		    One of:
           - GET_LOCK_UNLOCK      : If we should send TL_IGNORE to store lock
           - GET_LOCK_STORE_LOCKS : Store lock info in Table
  @param write_lock_used   Store pointer to last table with WRITE_ALLOW_WRITE
*/

static DRIZZLE_LOCK *get_lock_data(Session *session, Table **table_ptr, uint32_t count,
				 uint32_t flags, Table **write_lock_used)
{
  uint32_t i,tables,lock_count;
  DRIZZLE_LOCK *sql_lock;
  THR_LOCK_DATA **locks, **locks_buf, **locks_start;
  Table **to, **table_buf;

  assert((flags == GET_LOCK_UNLOCK) || (flags == GET_LOCK_STORE_LOCKS));

  *write_lock_used=0;
  for (i=tables=lock_count=0 ; i < count ; i++)
  {
    Table *t= table_ptr[i];

    if (t->s->tmp_table != NON_TRANSACTIONAL_TMP_TABLE)
    {
      tables+= t->file->lock_count();
      lock_count++;
    }
  }

  /*
    Allocating twice the number of pointers for lock data for use in
    thr_mulit_lock(). This function reorders the lock data, but cannot
    update the table values. So the second part of the array is copied
    from the first part immediately before calling thr_multi_lock().
  */
  if (!(sql_lock= (DRIZZLE_LOCK*)
	malloc(sizeof(*sql_lock) +
               sizeof(THR_LOCK_DATA*) * tables * 2 +
               sizeof(table_ptr) * lock_count)))
    return(0);
  locks= locks_buf= sql_lock->locks= (THR_LOCK_DATA**) (sql_lock + 1);
  to= table_buf= sql_lock->table= (Table**) (locks + tables * 2);
  sql_lock->table_count=lock_count;

  for (i=0 ; i < count ; i++)
  {
    Table *table;
    enum thr_lock_type lock_type;

    if ((table=table_ptr[i])->s->tmp_table == NON_TRANSACTIONAL_TMP_TABLE)
      continue;
    lock_type= table->reginfo.lock_type;
    assert (lock_type != TL_WRITE_DEFAULT);
    if (lock_type >= TL_WRITE_ALLOW_WRITE)
    {
      *write_lock_used=table;
      if (table->db_stat & HA_READ_ONLY)
      {
	my_error(ER_OPEN_AS_READONLY,MYF(0),table->alias);
        /* Clear the lock type of the lock data that are stored already. */
        sql_lock->lock_count= locks - sql_lock->locks;
        reset_lock_data_and_free(&sql_lock);
	return(0);
      }
    }
    locks_start= locks;
    locks= table->file->store_lock(session, locks,
                                   (flags & GET_LOCK_UNLOCK) ? TL_IGNORE :
                                   lock_type);
    if (flags & GET_LOCK_STORE_LOCKS)
    {
      table->lock_position=   (uint32_t) (to - table_buf);
      table->lock_data_start= (uint32_t) (locks_start - locks_buf);
      table->lock_count=      (uint32_t) (locks - locks_start);
    }
    *to++= table;
  }
  /*
    We do not use 'tables', because there are cases where store_lock()
    returns less locks than lock_count() claimed. This can happen when
    a FLUSH TABLES tries to abort locks from a MERGE table of another
    thread. When that thread has just opened the table, but not yet
    attached its children, it cannot return the locks. lock_count()
    always returns the number of locks that an attached table has.
    This is done to avoid the reverse situation: If lock_count() would
    return 0 for a non-attached MERGE table, and that table becomes
    attached between the calls to lock_count() and store_lock(), then
    we would have allocated too little memory for the lock data. Now
    we may allocate too much, but better safe than memory overrun.
    And in the FLUSH case, the memory is released quickly anyway.
  */
  sql_lock->lock_count= locks - locks_buf;
  return(sql_lock);
}


/*****************************************************************************
  Lock table based on the name.
  This is used when we need total access to a closed, not open table
*****************************************************************************/

/**
  Lock and wait for the named lock.

  @param session			Thread handler
  @param table_list		Lock first table in this list


  @note
    Works together with global read lock.

  @retval
    0	ok
  @retval
    1	error
*/

int lock_and_wait_for_table_name(Session *session, TableList *table_list)
{
  int lock_retcode;
  int error= -1;

  if (wait_if_global_read_lock(session, 0, 1))
    return(1);
  pthread_mutex_lock(&LOCK_open);
  if ((lock_retcode = lock_table_name(session, table_list, true)) < 0)
    goto end;
  if (lock_retcode && wait_for_locked_table_names(session, table_list))
  {
    unlock_table_name(session, table_list);
    goto end;
  }
  error=0;

end:
  pthread_mutex_unlock(&LOCK_open);
  start_waiting_global_read_lock(session);
  return(error);
}


/**
  Put a not open table with an old refresh version in the table cache.

  @param session			Thread handler
  @param table_list		Lock first table in this list
  @param check_in_use           Do we need to check if table already in use by us

  @note
    One must have a lock on LOCK_open!

  @warning
    If you are going to update the table, you should use
    lock_and_wait_for_table_name instead of this function as this works
    together with 'FLUSH TABLES WITH READ LOCK'

  @note
    This will force any other threads that uses the table to release it
    as soon as possible.

  @return
    < 0 error
  @return
    == 0 table locked
  @return
    > 0  table locked, but someone is using it
*/

int lock_table_name(Session *session, TableList *table_list, bool check_in_use)
{
  Table *table;
  char  key[MAX_DBKEY_LENGTH];
  char *db= table_list->db;
  uint32_t  key_length;
  bool  found_locked_table= false;
  HASH_SEARCH_STATE state;

  key_length= create_table_def_key(session, key, table_list, 0);

  if (check_in_use)
  {
    /* Only insert the table if we haven't insert it already */
    for (table=(Table*) hash_first(&open_cache, (unsigned char*)key,
                                   key_length, &state);
         table ;
         table = (Table*) hash_next(&open_cache,(unsigned char*) key,
                                    key_length, &state))
    {
      if (table->reginfo.lock_type < TL_WRITE)
      {
        if (table->in_use == session)
          found_locked_table= true;
        continue;
      }

      if (table->in_use == session)
      {
        table->s->version= 0;                  // Ensure no one can use this
        table->locked_by_name= 1;
        return(0);
      }
    }
  }

  if (session->locked_tables && session->locked_tables->table_count &&
      ! find_temporary_table(session, table_list->db, table_list->table_name))
  {
    if (found_locked_table)
      my_error(ER_TABLE_NOT_LOCKED_FOR_WRITE, MYF(0), table_list->alias);
    else
      my_error(ER_TABLE_NOT_LOCKED, MYF(0), table_list->alias);

    return(-1);
  }

  if (!(table= table_cache_insert_placeholder(session, key, key_length)))
    return(-1);

  table_list->table=table;

  /* Return 1 if table is in use */
  return(test(remove_table_from_cache(session, db, table_list->table_name,
             check_in_use ? RTFC_NO_FLAG : RTFC_WAIT_OTHER_THREAD_FLAG)));
}


void unlock_table_name(Session *,
                       TableList *table_list)
{
  if (table_list->table)
  {
    hash_delete(&open_cache, (unsigned char*) table_list->table);
    broadcast_refresh();
  }
}


static bool locked_named_table(Session *,
                               TableList *table_list)
{
  for (; table_list ; table_list=table_list->next_local)
  {
    Table *table= table_list->table;
    if (table)
    {
      Table *save_next= table->next;
      bool result;
      table->next= 0;
      result= table_is_used(table_list->table, 0);
      table->next= save_next;
      if (result)
        return 1;
    }
  }
  return 0;					// All tables are locked
}


bool wait_for_locked_table_names(Session *session, TableList *table_list)
{
  bool result=0;

  safe_mutex_assert_owner(&LOCK_open);

  while (locked_named_table(session,table_list))
  {
    if (session->killed)
    {
      result=1;
      break;
    }
    wait_for_condition(session, &LOCK_open, &COND_refresh);
    pthread_mutex_lock(&LOCK_open);
  }
  return(result);
}


/**
  Lock all tables in list with a name lock.

  REQUIREMENTS
  - One must have a lock on LOCK_open when calling this

  @param session			Thread handle
  @param table_list		Names of tables to lock

  @note
    If you are just locking one table, you should use
    lock_and_wait_for_table_name().

  @retval
    0	ok
  @retval
    1	Fatal error (end of memory ?)
*/

bool lock_table_names(Session *session, TableList *table_list)
{
  bool got_all_locks=1;
  TableList *lock_table;

  for (lock_table= table_list; lock_table; lock_table= lock_table->next_local)
  {
    int got_lock;
    if ((got_lock=lock_table_name(session,lock_table, true)) < 0)
      goto end;					// Fatal error
    if (got_lock)
      got_all_locks=0;				// Someone is using table
  }

  /* If some table was in use, wait until we got the lock */
  if (!got_all_locks && wait_for_locked_table_names(session, table_list))
    goto end;
  return 0;

end:
  unlock_table_names(session, table_list, lock_table);
  return 1;
}


/**
  Unlock all tables in list with a name lock.

  @param session        Thread handle.
  @param table_list Names of tables to lock.

  @note
    This function needs to be protected by LOCK_open. If we're
    under LOCK TABLES, this function does not work as advertised. Namely,
    it does not exclude other threads from using this table and does not
    put an exclusive name lock on this table into the table cache.

  @see lock_table_names
  @see unlock_table_names

  @retval TRUE An error occured.
  @retval FALSE Name lock successfully acquired.
*/

bool lock_table_names_exclusively(Session *session, TableList *table_list)
{
  if (lock_table_names(session, table_list))
    return true;

  /*
    Upgrade the table name locks from semi-exclusive to exclusive locks.
  */
  for (TableList *table= table_list; table; table= table->next_global)
  {
    if (table->table)
      table->table->open_placeholder= 1;
  }
  return false;
}


/**
  Test is 'table' is protected by an exclusive name lock.

  @param[in] session        The current thread handler
  @param[in] table_list Table container containing the single table to be
                        tested

  @note Needs to be protected by LOCK_open mutex.

  @return Error status code
    @retval TRUE Table is protected
    @retval FALSE Table is not protected
*/

bool
is_table_name_exclusively_locked_by_this_thread(Session *session,
                                                TableList *table_list)
{
  char  key[MAX_DBKEY_LENGTH];
  uint32_t  key_length;

  key_length= create_table_def_key(session, key, table_list, 0);

  return is_table_name_exclusively_locked_by_this_thread(session, (unsigned char *)key,
                                                         key_length);
}


/**
  Test is 'table key' is protected by an exclusive name lock.

  @param[in] session        The current thread handler.
  @param[in] key
  @param[in] key_length

  @note Needs to be protected by LOCK_open mutex

  @retval TRUE Table is protected
  @retval FALSE Table is not protected
 */

bool
is_table_name_exclusively_locked_by_this_thread(Session *session, unsigned char *key,
                                                int key_length)
{
  HASH_SEARCH_STATE state;
  Table *table;

  for (table= (Table*) hash_first(&open_cache, key,
                                  key_length, &state);
       table ;
       table= (Table*) hash_next(&open_cache, key,
                                 key_length, &state))
  {
    if (table->in_use == session &&
        table->open_placeholder == 1 &&
        table->s->version == 0)
      return true;
  }

  return false;
}

/**
  Unlock all tables in list with a name lock.

  @param
    session			Thread handle
  @param
    table_list		Names of tables to unlock
  @param
    last_table		Don't unlock any tables after this one.
			        (default 0, which will unlock all tables)

  @note
    One must have a lock on LOCK_open when calling this.

  @note
    This function will broadcast refresh signals to inform other threads
    that the name locks are removed.

  @retval
    0	ok
  @retval
    1	Fatal error (end of memory ?)
*/

void unlock_table_names(Session *session, TableList *table_list,
			TableList *last_table)
{
  for (TableList *table= table_list;
       table != last_table;
       table= table->next_local)
    unlock_table_name(session,table);
  broadcast_refresh();
  return;
}


static void print_lock_error(int error, const char *table)
{
  int textno;

  switch (error) {
  case HA_ERR_LOCK_WAIT_TIMEOUT:
    textno=ER_LOCK_WAIT_TIMEOUT;
    break;
  case HA_ERR_READ_ONLY_TRANSACTION:
    textno=ER_READ_ONLY_TRANSACTION;
    break;
  case HA_ERR_LOCK_DEADLOCK:
    textno=ER_LOCK_DEADLOCK;
    break;
  case HA_ERR_WRONG_COMMAND:
    textno=ER_ILLEGAL_HA;
    break;
  default:
    textno=ER_CANT_LOCK;
    break;
  }

  if ( textno == ER_ILLEGAL_HA )
    my_error(textno, MYF(ME_BELL+ME_OLDWIN+ME_WAITTANG), table);
  else
    my_error(textno, MYF(ME_BELL+ME_OLDWIN+ME_WAITTANG), error);

  return;
}


/****************************************************************************
  Handling of global read locks

  Taking the global read lock is TWO steps (2nd step is optional; without
  it, COMMIT of existing transactions will be allowed):
  lock_global_read_lock() THEN make_global_read_lock_block_commit().

  The global locks are handled through the global variables:
  global_read_lock
    count of threads which have the global read lock (i.e. have completed at
    least the first step above)
  global_read_lock_blocks_commit
    count of threads which have the global read lock and block
    commits (i.e. are in or have completed the second step above)
  waiting_for_read_lock
    count of threads which want to take a global read lock but cannot
  protect_against_global_read_lock
    count of threads which have set protection against global read lock.

  access to them is protected with a mutex LOCK_global_read_lock

  (XXX: one should never take LOCK_open if LOCK_global_read_lock is
  taken, otherwise a deadlock may occur. Other mutexes could be a
  problem too - grep the code for global_read_lock if you want to use
  any other mutex here) Also one must not hold LOCK_open when calling
  wait_if_global_read_lock(). When the thread with the global read lock
  tries to close its tables, it needs to take LOCK_open in
  close_thread_table().

  How blocking of threads by global read lock is achieved: that's
  advisory. Any piece of code which should be blocked by global read lock must
  be designed like this:
  - call to wait_if_global_read_lock(). When this returns 0, no global read
  lock is owned; if argument abort_on_refresh was 0, none can be obtained.
  - job
  - if abort_on_refresh was 0, call to start_waiting_global_read_lock() to
  allow other threads to get the global read lock. I.e. removal of the
  protection.
  (Note: it's a bit like an implementation of rwlock).

  [ I am sorry to mention some SQL syntaxes below I know I shouldn't but found
  no better descriptive way ]

  Why does FLUSH TABLES WITH READ LOCK need to block COMMIT: because it's used
  to read a non-moving SHOW MASTER STATUS, and a COMMIT writes to the binary
  log.

  Why getting the global read lock is two steps and not one. Because FLUSH
  TABLES WITH READ LOCK needs to insert one other step between the two:
  flushing tables. So the order is
  1) lock_global_read_lock() (prevents any new table write locks, i.e. stalls
  all new updates)
  2) close_cached_tables() (the FLUSH TABLES), which will wait for tables
  currently opened and being updated to close (so it's possible that there is
  a moment where all new updates of server are stalled *and* FLUSH TABLES WITH
  READ LOCK is, too).
  3) make_global_read_lock_block_commit().
  If we have merged 1) and 3) into 1), we would have had this deadlock:
  imagine thread 1 and 2, in non-autocommit mode, thread 3, and an InnoDB
  table t.
  session1: SELECT * FROM t FOR UPDATE;
  session2: UPDATE t SET a=1; # blocked by row-level locks of session1
  session3: FLUSH TABLES WITH READ LOCK; # blocked in close_cached_tables() by the
  table instance of session2
  session1: COMMIT; # blocked by session3.
  session1 blocks session2 which blocks session3 which blocks session1: deadlock.

  Note that we need to support that one thread does
  FLUSH TABLES WITH READ LOCK; and then COMMIT;
  (that's what innobackup does, for some good reason).
  So in this exceptional case the COMMIT should not be blocked by the FLUSH
  TABLES WITH READ LOCK.

****************************************************************************/

volatile uint32_t global_read_lock=0;
volatile uint32_t global_read_lock_blocks_commit=0;
static volatile uint32_t protect_against_global_read_lock=0;
static volatile uint32_t waiting_for_read_lock=0;

#define GOT_GLOBAL_READ_LOCK               1
#define MADE_GLOBAL_READ_LOCK_BLOCK_COMMIT 2

bool lock_global_read_lock(Session *session)
{
  if (!session->global_read_lock)
  {
    const char *old_message;
    (void) pthread_mutex_lock(&LOCK_global_read_lock);
    old_message=session->enter_cond(&COND_global_read_lock, &LOCK_global_read_lock,
                                "Waiting to get readlock");

    waiting_for_read_lock++;
    while (protect_against_global_read_lock && !session->killed)
      pthread_cond_wait(&COND_global_read_lock, &LOCK_global_read_lock);
    waiting_for_read_lock--;
    if (session->killed)
    {
      session->exit_cond(old_message);
      return(1);
    }
    session->global_read_lock= GOT_GLOBAL_READ_LOCK;
    global_read_lock++;
    session->exit_cond(old_message); // this unlocks LOCK_global_read_lock
  }
  /*
    We DON'T set global_read_lock_blocks_commit now, it will be set after
    tables are flushed (as the present function serves for FLUSH TABLES WITH
    READ LOCK only). Doing things in this order is necessary to avoid
    deadlocks (we must allow COMMIT until all tables are closed; we should not
    forbid it before, or we can have a 3-thread deadlock if 2 do SELECT FOR
    UPDATE and one does FLUSH TABLES WITH READ LOCK).
  */
  return(0);
}


void unlock_global_read_lock(Session *session)
{
  uint32_t tmp;

  pthread_mutex_lock(&LOCK_global_read_lock);
  tmp= --global_read_lock;
  if (session->global_read_lock == MADE_GLOBAL_READ_LOCK_BLOCK_COMMIT)
    --global_read_lock_blocks_commit;
  pthread_mutex_unlock(&LOCK_global_read_lock);
  /* Send the signal outside the mutex to avoid a context switch */
  if (!tmp)
  {
    pthread_cond_broadcast(&COND_global_read_lock);
  }
  session->global_read_lock= 0;

  return;
}

#define must_wait (global_read_lock &&                             \
                   (is_not_commit ||                               \
                    global_read_lock_blocks_commit))

bool wait_if_global_read_lock(Session *session, bool abort_on_refresh,
                              bool is_not_commit)
{
  const char *old_message= NULL;
  bool result= 0, need_exit_cond;

  /*
    Assert that we do not own LOCK_open. If we would own it, other
    threads could not close their tables. This would make a pretty
    deadlock.
  */
  safe_mutex_assert_not_owner(&LOCK_open);

  (void) pthread_mutex_lock(&LOCK_global_read_lock);
  if ((need_exit_cond= must_wait))
  {
    if (session->global_read_lock)		// This thread had the read locks
    {
      if (is_not_commit)
        my_message(ER_CANT_UPDATE_WITH_READLOCK,
                   ER(ER_CANT_UPDATE_WITH_READLOCK), MYF(0));
      (void) pthread_mutex_unlock(&LOCK_global_read_lock);
      /*
        We allow FLUSHer to COMMIT; we assume FLUSHer knows what it does.
        This allowance is needed to not break existing versions of innobackup
        which do a BEGIN; INSERT; FLUSH TABLES WITH READ LOCK; COMMIT.
      */
      return(is_not_commit);
    }
    old_message=session->enter_cond(&COND_global_read_lock, &LOCK_global_read_lock,
				"Waiting for release of readlock");
    while (must_wait && ! session->killed &&
	   (!abort_on_refresh || session->version == refresh_version))
    {
      (void) pthread_cond_wait(&COND_global_read_lock, &LOCK_global_read_lock);
    }
    if (session->killed)
      result=1;
  }
  if (!abort_on_refresh && !result)
    protect_against_global_read_lock++;
  /*
    The following is only true in case of a global read locks (which is rare)
    and if old_message is set
  */
  if (unlikely(need_exit_cond))
    session->exit_cond(old_message); // this unlocks LOCK_global_read_lock
  else
    pthread_mutex_unlock(&LOCK_global_read_lock);
  return(result);
}


void start_waiting_global_read_lock(Session *session)
{
  bool tmp;
  if (unlikely(session->global_read_lock))
    return;
  (void) pthread_mutex_lock(&LOCK_global_read_lock);
  tmp= (!--protect_against_global_read_lock &&
        (waiting_for_read_lock || global_read_lock_blocks_commit));
  (void) pthread_mutex_unlock(&LOCK_global_read_lock);
  if (tmp)
    pthread_cond_broadcast(&COND_global_read_lock);
  return;
}


bool make_global_read_lock_block_commit(Session *session)
{
  bool error;
  const char *old_message;
  /*
    If we didn't succeed lock_global_read_lock(), or if we already suceeded
    make_global_read_lock_block_commit(), do nothing.
  */
  if (session->global_read_lock != GOT_GLOBAL_READ_LOCK)
    return(0);
  pthread_mutex_lock(&LOCK_global_read_lock);
  /* increment this BEFORE waiting on cond (otherwise race cond) */
  global_read_lock_blocks_commit++;
  old_message= session->enter_cond(&COND_global_read_lock, &LOCK_global_read_lock,
                               "Waiting for all running commits to finish");
  while (protect_against_global_read_lock && !session->killed)
    pthread_cond_wait(&COND_global_read_lock, &LOCK_global_read_lock);
  if ((error= test(session->killed)))
    global_read_lock_blocks_commit--; // undo what we did
  else
    session->global_read_lock= MADE_GLOBAL_READ_LOCK_BLOCK_COMMIT;
  session->exit_cond(old_message); // this unlocks LOCK_global_read_lock
  return(error);
}


/**
  Broadcast COND_refresh and COND_global_read_lock.

    Due to a bug in a threading library it could happen that a signal
    did not reach its target. A condition for this was that the same
    condition variable was used with different mutexes in
    pthread_cond_wait(). Some time ago we changed LOCK_open to
    LOCK_global_read_lock in global read lock handling. So COND_refresh
    was used with LOCK_open and LOCK_global_read_lock.

    We did now also change from COND_refresh to COND_global_read_lock
    in global read lock handling. But now it is necessary to signal
    both conditions at the same time.

  @note
    When signalling COND_global_read_lock within the global read lock
    handling, it is not necessary to also signal COND_refresh.
*/

void broadcast_refresh(void)
{
  pthread_cond_broadcast(&COND_refresh);
  pthread_cond_broadcast(&COND_global_read_lock);
}


/*
  Try to get transactional table locks for the tables in the list.

  SYNOPSIS
    try_transactional_lock()
      session                       Thread handle
      table_list                List of tables to lock

  DESCRIPTION
    This is called if transactional table locks are requested for all
    tables in table_list and no non-transactional locks pre-exist.

  RETURN
    0                   OK. All tables are transactional locked.
    1                   Error: must fall back to non-transactional locks.
    -1                  Error: no recovery possible.
*/

int try_transactional_lock(Session *session, TableList *table_list)
{
  uint32_t          dummy_counter;
  int           error;
  int           result= 0;

  /* Need to open the tables to be able to access engine methods. */
  if (open_tables(session, &table_list, &dummy_counter, 0))
  {
    /* purecov: begin tested */
    return(-1);
    /* purecov: end */
  }

  /* Required by InnoDB. */
  session->in_lock_tables= true;

  if ((error= set_handler_table_locks(session, table_list, true)))
  {
    /*
      Not all transactional locks could be taken. If the error was
      something else but "unsupported by storage engine", abort the
      execution of this statement.
    */
    if (error != HA_ERR_WRONG_COMMAND)
    {
      result= -1;
      goto err;
    }
    /*
      Fall back to non-transactional locks because transactional locks
      are unsupported by a storage engine. No need to unlock the
      successfully taken transactional locks. They go away at end of
      transaction anyway.
    */
    result= 1;
  }

 err:
  /* We need to explicitly commit if autocommit mode is active. */
  (void) ha_autocommit_or_rollback(session, 0);
  /* Close the tables. The locks (if taken) persist in the storage engines. */
  close_tables_for_reopen(session, &table_list);
  session->in_lock_tables= false;
  return(result);
}


/*
  Check if lock method conversion was done and was allowed.

  SYNOPSIS
    check_transactional_lock()
      session                       Thread handle
      table_list                List of tables to lock

  DESCRIPTION

    Lock method conversion can be done during parsing if one of the
    locks is non-transactional. It can also happen if non-transactional
    table locks exist when the statement is executed or if a storage
    engine does not support transactional table locks.

    Check if transactional table locks have been converted to
    non-transactional and if this was allowed. In a running transaction
    or in strict mode lock method conversion is not allowed - report an
    error. Otherwise it is allowed - issue a warning.

  RETURN
    0                   OK. Proceed with non-transactional locks.
    -1                  Error: Lock conversion is prohibited.
*/

int check_transactional_lock(Session *, TableList *table_list)
{
  TableList    *tlist;
  int           result= 0;

  for (tlist= table_list; tlist; tlist= tlist->next_global)
  {

    /*
      Unfortunately we cannot use tlist->placeholder() here. This method
      returns TRUE if the table is not open, which is always the case
      here. Whenever the definition of TableList::placeholder() is
      changed, probably this condition needs to be changed too.
    */
    if (tlist->derived || tlist->schema_table || !tlist->lock_transactional)
    {
      continue;
    }

    /* We must not convert the lock method in strict mode. */
    {
      my_error(ER_NO_AUTO_CONVERT_LOCK_STRICT, MYF(0),
               tlist->alias ? tlist->alias : tlist->table_name);
      result= -1;
      continue;
    }

  }

  return(result);
}


/*
  Set table locks in the table handler.

  SYNOPSIS
    set_handler_table_locks()
      session                       Thread handle
      table_list                List of tables to lock
      transactional             If to lock transactional or non-transactional

  RETURN
    0                   OK.
    != 0                Error code from handler::lock_table().
*/

int set_handler_table_locks(Session *session, TableList *table_list,
                            bool transactional)
{
  TableList    *tlist;
  int           error= 0;

  for (tlist= table_list; tlist; tlist= tlist->next_global)
  {
    int lock_type;
    int lock_timeout= -1; /* Use default for non-transactional locks. */

    if (tlist->placeholder())
      continue;

    assert((tlist->lock_type == TL_READ) ||
                (tlist->lock_type == TL_READ_NO_INSERT) ||
                (tlist->lock_type == TL_WRITE_DEFAULT) ||
                (tlist->lock_type == TL_WRITE) ||
                (tlist->lock_type == TL_WRITE_LOW_PRIORITY));

    /*
      Every tlist object has a proper lock_type set. Even if it came in
      the list as a base table from a view only.
    */
    lock_type= ((tlist->lock_type <= TL_READ_NO_INSERT) ?
                HA_LOCK_IN_SHARE_MODE : HA_LOCK_IN_EXCLUSIVE_MODE);

    if (transactional)
    {
      /*
        The lock timeout is not set if this table belongs to a view. We
        need to take it from the top-level view. After this loop
        iteration, lock_timeout is not needed any more. Not even if the
        locks are converted to non-transactional locks later.
        Non-transactional locks do not support a lock_timeout.
      */
      lock_timeout= tlist->top_table()->lock_timeout;

      /*
        For warning/error reporting we need to set the intended lock
        method in the TableList object. It will be used later by
        check_transactional_lock(). The lock method is not set if this
        table belongs to a view. We can safely set it to transactional
        locking here. Even for non-view tables. This function is not
        called if non-transactional locking was requested for any
        object.
      */
      tlist->lock_transactional= true;
    }

    /*
      Because we need to set the lock method (see above) for all
      involved tables, we cannot break the loop on an error.
      But we do not try more locks after the first error.
      However, for non-transactional locking handler::lock_table() is
      a hint only. So we continue to call it for other tables.
    */
    if (!error || !transactional)
    {
      error= tlist->table->file->lock_table(session, lock_type, lock_timeout);
      if (error && transactional && (error != HA_ERR_WRONG_COMMAND))
        tlist->table->file->print_error(error, MYF(0));
    }
  }

  return(error);
}


/**
  @} (end of group Locking)
*/