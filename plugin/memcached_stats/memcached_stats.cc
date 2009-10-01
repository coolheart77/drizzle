/* 
 * Copyright (c) 2009, Padraig O'Sullivan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *   * Neither the name of Patrick Galbraith nor the names of its contributors
 *     may be used to endorse or promote products derived from this software
 *     without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "drizzled/server_includes.h"
#include "drizzled/show.h"
#include "drizzled/gettext.h"
#include "drizzled/info_schema.h"

#include "stats_table.h"
#include "analysis_table.h"
#include "sysvar_holder.h"

#include <string>
#include <map>

using namespace std;
using namespace drizzled;

/*
 * Vectors of columns for I_S tables.
 */
static vector<const ColumnInfo *> memcached_stats_columns;
static vector<const ColumnInfo *> memcached_analysis_columns;

/*
 * Methods for I_S tables.
 */
static InfoSchemaMethods *memcached_stats_methods= NULL;
static InfoSchemaMethods *memcached_analysis_methods= NULL;

/*
 * I_S tables.
 */
static InfoSchemaTable *memcached_stats_table= NULL;
static InfoSchemaTable *memcached_analysis_table= NULL;

/*
 * System variable related variables.
 */
static char *sysvar_memcached_servers= NULL;

/**
 * Populate the vectors of columns for each I_S table.
 *
 * @return false on success; true on failure.
 */
static bool initColumns()
{
  if (createMemcachedStatsColumns(memcached_stats_columns))
  {
    return true;
  }

  if (createMemcachedAnalysisColumns(memcached_analysis_columns))
  {
    return true;
  }

  return false;
}

/**
 * Clear the vectors of columns for each I_S table.
 */
static void cleanupColumns()
{
  clearMemcachedColumns(memcached_stats_columns);
  clearMemcachedColumns(memcached_analysis_columns);
}

/**
 * Initialize the methods for each I_S table.
 *
 * @return false on success; true on failure
 */
static bool initMethods()
{
  memcached_stats_methods= new(std::nothrow) 
    MemcachedStatsISMethods();
  if (! memcached_stats_methods)
  {
    return true;
  }

  memcached_analysis_methods= new(std::nothrow) 
    MemcachedAnalysisISMethods();
  if (! memcached_analysis_methods)
  {
    return true;
  }

  return false;
}

/**
 * Delete memory allocated for the I_S table methods.
 */
static void cleanupMethods()
{
  delete memcached_stats_methods;
  delete memcached_analysis_methods;
}

/**
 * Initialize the I_S tables related to memcached.
 *
 * @return false on success; true on failure
 */
static bool initMemcachedTables()
{
  memcached_stats_table= new(std::nothrow) InfoSchemaTable("MEMCACHED_STATS",
                                                           memcached_stats_columns,
                                                           -1, -1, false, false, 0,
                                                           memcached_stats_methods);
  if (! memcached_stats_table)
  {
    return true;
  }

  memcached_analysis_table= 
    new(std::nothrow) InfoSchemaTable("MEMCACHED_ANALYSIS",
                                      memcached_analysis_columns,
                                      -1, -1, false, false, 0,
                                      memcached_analysis_methods);
  if (! memcached_analysis_table)
  {
    return true;
  }

  return false;
}

/**
 * Delete memory allocated for the I_S tables.
 */
static void cleanupMemcachedTables()
{
  delete memcached_stats_table;
  delete memcached_analysis_table;
}

/**
 * Initialize the memcached stats plugin.
 *
 * @param[in] registry the drizzled::plugin::Registry singleton
 * @return false on success; true on failure.
 */
static int init(plugin::Registry &registry)
{
  if (initMethods())
  {
    return true;
  }

  if (initColumns())
  {
    return true;
  }

  if (initMemcachedTables())
  {
    return true;
  }

  SysvarHolder &sysvar_holder= SysvarHolder::singleton();
  sysvar_holder.setServersString(sysvar_memcached_servers);

  /* we are good to go */
  registry.add(memcached_stats_table);
  registry.add(memcached_analysis_table);

  return false;
}

/**
 * Clean up the memcached stats plugin.
 *
 * @param[in] registry the drizzled::plugin::Registry singleton
 * @return false on success; true on failure
 */
static int deinit(plugin::Registry &registry)
{
  registry.remove(memcached_stats_table);
  registry.remove(memcached_analysis_table);

  cleanupMethods();
  cleanupColumns();
  cleanupMemcachedTables();

  return false;
}

static int check_memc_servers(Session *,
                              struct st_mysql_sys_var *,
                              void *,
                              struct st_mysql_value *value)
{
  char buff[STRING_BUFFER_USUAL_SIZE];
  int len= sizeof(buff);
  const char *input= value->val_str(value, buff, &len);

  if (input)
  {
    SysvarHolder &sysvar_holder= SysvarHolder::singleton();
    sysvar_holder.setServersStringVar(input);
    return 0;
  }

  return 1;
}

static void set_memc_servers(Session *,
                             struct st_mysql_sys_var *,
                             void *var_ptr,
                             const void *save)
{
  if (*(bool *) save != true)
  {
    SysvarHolder &sysvar_holder= SysvarHolder::singleton();
    sysvar_holder.updateServersSysvar((const char **) var_ptr);
  }
}

static DRIZZLE_SYSVAR_STR(servers,
                          sysvar_memcached_servers,
                          PLUGIN_VAR_OPCMDARG,
                          N_("List of memcached servers."),
                          check_memc_servers, /* check func */
                          set_memc_servers, /* update func */
                          ""); /* default value */

static struct st_mysql_sys_var *system_variables[]=
{
  DRIZZLE_SYSVAR(servers),
  NULL
};

drizzle_declare_plugin(memcached_stats)
{
  "memcached_stats",
  "1.0",
  "Padraig O'Sullivan",
  N_("Memcached Stats as I_S tables"),
  PLUGIN_LICENSE_BSD,
  init,   /* Plugin Init      */
  deinit, /* Plugin Deinit    */
  NULL,   /* status variables */
  system_variables, /* system variables */
  NULL    /* config options   */
}
drizzle_declare_plugin_end;
