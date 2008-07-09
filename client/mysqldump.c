/* Copyright (C) 2000 MySQL AB

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

/* mysqldump.c  - Dump a tables contents and format to an ASCII file
**
** The author's original notes follow :-
**
** AUTHOR: Igor Romanenko (igor@frog.kiev.ua)
** DATE:   December 3, 1994
** WARRANTY: None, expressed, impressed, implied
**          or other
** STATUS: Public domain
** Adapted and optimized for MySQL by
** Michael Widenius, Sinisa Milivojevic, Jani Tolonen
** -w --where added 9/10/98 by Jim Faucette
** slave code by David Saez Padros <david@ols.es>
** master/autocommit code by Brian Aker <brian@tangent.org>
** SSL by
** Andrei Errapart <andreie@no.spam.ee>
** TÃµnu Samuel  <tonu@please.do.not.remove.this.spam.ee>
** XML by Gary Huntress <ghuntress@mediaone.net> 10/10/01, cleaned up
** and adapted to mysqldump 05/11/01 by Jani Tolonen
** Added --single-transaction option 06/06/2002 by Peter Zaitsev
** 10 Jun 2003: SET NAMES and --no-set-names by Alexander Barkov
*/

#define DUMP_VERSION "10.13"

#include <my_global.h>
#include <my_sys.h>
#include <m_string.h>
#include <m_ctype.h>
#include <hash.h>
#include <stdarg.h>

#include "client_priv.h"
#include "mysql.h"
#include "mysql_version.h"
#include "mysqld_error.h"

/* Exit codes */

#define EX_USAGE 1
#define EX_MYSQLERR 2
#define EX_CONSCHECK 3
#define EX_EOM 4
#define EX_EOF 5 /* ferror for output file was got */
#define EX_ILLEGAL_TABLE 6

/* index into 'show fields from table' */

#define SHOW_FIELDNAME  0
#define SHOW_TYPE  1
#define SHOW_NULL  2
#define SHOW_DEFAULT  4
#define SHOW_EXTRA  5

/* Size of buffer for dump's select query */
#define QUERY_LENGTH 1536

/* ignore table flags */
#define IGNORE_NONE 0x00 /* no ignore */
#define IGNORE_DATA 0x01 /* don't dump data for this table */
#define IGNORE_INSERT_DELAYED 0x02 /* table doesn't support INSERT DELAYED */

static void add_load_option(DYNAMIC_STRING *str, const char *option,
                             const char *option_value);
static ulong find_set(TYPELIB *lib, const char *x, uint length,
                      char **err_pos, uint *err_len);

static void field_escape(DYNAMIC_STRING* in, const char *from);
static my_bool  verbose= 0, opt_no_create_info= 0, opt_no_data= 0,
                quick= 1, extended_insert= 1,
                lock_tables=1,ignore_errors=0,flush_logs=0,flush_privileges=0,
                opt_drop=1,opt_keywords=0,opt_lock=1,opt_compress=0,
                opt_delayed=0,create_options=1,opt_quoted=0,opt_databases=0,
                opt_alldbs=0,opt_create_db=0,opt_lock_all_tables=0,
                opt_set_charset=0, opt_dump_date=1,
                opt_autocommit=0,opt_disable_keys=1,opt_xml=0,
                opt_delete_master_logs=0, tty_password=0,
                opt_single_transaction=0, opt_comments= 0, opt_compact= 0,
                opt_hex_blob=0, opt_order_by_primary=0, opt_ignore=0,
                opt_complete_insert= 0, opt_drop_database= 0,
                opt_replace_into= 0,
                opt_routines=0, opt_tz_utc=1,
                opt_slave_apply= 0, 
                opt_include_master_host_port= 0,
                opt_events= 0,
                opt_alltspcs=0, opt_notspcs= 0;
static my_bool insert_pat_inited= 0, debug_info_flag= 0, debug_check_flag= 0;
static ulong opt_max_allowed_packet, opt_net_buffer_length;
static MYSQL mysql_connection,*mysql=0;
static DYNAMIC_STRING insert_pat;
static char  *opt_password=0,*current_user=0,
             *current_host=0,*path=0,*fields_terminated=0,
             *lines_terminated=0, *enclosed=0, *opt_enclosed=0, *escaped=0,
             *where=0, *order_by=0,
             *opt_compatible_mode_str= 0,
             *err_ptr= 0,
             *log_error_file= NULL;
static char **defaults_argv= 0;
static char compatible_mode_normal_str[255];
/* Server supports character_set_results session variable? */
static my_bool server_supports_switching_charsets= TRUE;
static ulong opt_compatible_mode= 0;
#define MYSQL_OPT_MASTER_DATA_EFFECTIVE_SQL 1
#define MYSQL_OPT_MASTER_DATA_COMMENTED_SQL 2
#define MYSQL_OPT_SLAVE_DATA_EFFECTIVE_SQL 1
#define MYSQL_OPT_SLAVE_DATA_COMMENTED_SQL 2
static uint opt_mysql_port= 0, opt_master_data;
static uint opt_slave_data;
static uint my_end_arg;
static int   first_error=0;
static DYNAMIC_STRING extended_row;
FILE *md_result_file= 0;
FILE *stderror_file=0;

#ifdef HAVE_SMEM
static char *shared_memory_base_name=0;
#endif
static uint opt_protocol= MYSQL_PROTOCOL_TCP;

/*
Dynamic_string wrapper functions. In this file use these
wrappers, they will terminate the process if there is
an allocation failure.
*/
static void init_dynamic_string_checked(DYNAMIC_STRING *str, const char *init_str,
			    uint init_alloc, uint alloc_increment);
static void dynstr_append_checked(DYNAMIC_STRING* dest, const char* src);
static void dynstr_set_checked(DYNAMIC_STRING *str, const char *init_str);
static void dynstr_append_mem_checked(DYNAMIC_STRING *str, const char *append,
			  uint length);
static void dynstr_realloc_checked(DYNAMIC_STRING *str, ulong additional_size);
/*
  Constant for detection of default value of default_charset.
  If default_charset is equal to mysql_universal_client_charset, then
  it is the default value which assigned at the very beginning of main().
*/
static const char *mysql_universal_client_charset=
  MYSQL_UNIVERSAL_CLIENT_CHARSET;
static char *default_charset;
static CHARSET_INFO *charset_info= &my_charset_latin1;
const char *default_dbug_option="d:t:o,/tmp/mysqldump.trace";
/* have we seen any VIEWs during table scanning? */
my_bool seen_views= 0;
const char *compatible_mode_names[]=
{
  "MYSQL323", "MYSQL40", "POSTGRESQL", "ORACLE", "MSSQL", "DB2",
  "MAXDB", "NO_KEY_OPTIONS", "NO_TABLE_OPTIONS", "NO_FIELD_OPTIONS",
  "ANSI",
  NullS
};
#define MASK_ANSI_QUOTES \
(\
 (1<<2)  | /* POSTGRESQL */\
 (1<<3)  | /* ORACLE     */\
 (1<<4)  | /* MSSQL      */\
 (1<<5)  | /* DB2        */\
 (1<<6)  | /* MAXDB      */\
 (1<<10)   /* ANSI       */\
)
TYPELIB compatible_mode_typelib= {array_elements(compatible_mode_names) - 1,
                                  "", compatible_mode_names, NULL};

HASH ignore_table;

static struct my_option my_long_options[] =
{
  {"all", 'a', "Deprecated. Use --create-options instead.",
   (uchar**) &create_options, (uchar**) &create_options, 0, GET_BOOL, NO_ARG, 1,
   0, 0, 0, 0, 0},
  {"all-databases", 'A',
   "Dump all the databases. This will be same as --databases with all databases selected.",
   (uchar**) &opt_alldbs, (uchar**) &opt_alldbs, 0, GET_BOOL, NO_ARG, 0, 0, 0, 0,
   0, 0},
  {"all-tablespaces", 'Y',
   "Dump all the tablespaces.",
   (uchar**) &opt_alltspcs, (uchar**) &opt_alltspcs, 0, GET_BOOL, NO_ARG, 0, 0, 0, 0,
   0, 0},
  {"no-tablespaces", 'y',
   "Do not dump any tablespace information.",
   (uchar**) &opt_notspcs, (uchar**) &opt_notspcs, 0, GET_BOOL, NO_ARG, 0, 0, 0, 0,
   0, 0},
  {"add-drop-database", OPT_DROP_DATABASE, "Add a 'DROP DATABASE' before each create.",
   (uchar**) &opt_drop_database, (uchar**) &opt_drop_database, 0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0,
   0},
  {"add-drop-table", OPT_DROP, "Add a 'drop table' before each create.",
   (uchar**) &opt_drop, (uchar**) &opt_drop, 0, GET_BOOL, NO_ARG, 1, 0, 0, 0, 0,
   0},
  {"add-locks", OPT_LOCKS, "Add locks around insert statements.",
   (uchar**) &opt_lock, (uchar**) &opt_lock, 0, GET_BOOL, NO_ARG, 1, 0, 0, 0, 0,
   0},
  {"allow-keywords", OPT_KEYWORDS,
   "Allow creation of column names that are keywords.", (uchar**) &opt_keywords,
   (uchar**) &opt_keywords, 0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"apply-slave-statements", OPT_MYSQLDUMP_SLAVE_APPLY,
   "Adds 'STOP SLAVE' prior to 'CHANGE MASTER' and 'START SLAVE' to bottom of dump.",
   (uchar**) &opt_slave_apply, (uchar**) &opt_slave_apply, 0, GET_BOOL, NO_ARG,
   0, 0, 0, 0, 0, 0},
  {"character-sets-dir", OPT_CHARSETS_DIR,
   "Directory where character sets are.", (uchar**) &charsets_dir,
   (uchar**) &charsets_dir, 0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"comments", 'i', "Write additional information.",
   (uchar**) &opt_comments, (uchar**) &opt_comments, 0, GET_BOOL, NO_ARG,
   1, 0, 0, 0, 0, 0},
  {"compatible", OPT_COMPATIBLE,
   "Change the dump to be compatible with a given mode. By default tables are dumped in a format optimized for MySQL. Legal modes are: ansi, mysql323, mysql40, postgresql, oracle, mssql, db2, maxdb, no_key_options, no_table_options, no_field_options. One can use several modes separated by commas. Note: Requires MySQL server version 4.1.0 or higher. This option is ignored with earlier server versions.",
   (uchar**) &opt_compatible_mode_str, (uchar**) &opt_compatible_mode_str, 0,
   GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"compact", OPT_COMPACT,
   "Give less verbose output (useful for debugging). Disables structure comments and header/footer constructs.  Enables options --skip-add-drop-table --no-set-names --skip-disable-keys --skip-add-locks",
   (uchar**) &opt_compact, (uchar**) &opt_compact, 0, GET_BOOL, NO_ARG, 0, 0, 0, 0,
   0, 0},
  {"complete-insert", 'c', "Use complete insert statements.",
   (uchar**) &opt_complete_insert, (uchar**) &opt_complete_insert, 0, GET_BOOL,
   NO_ARG, 0, 0, 0, 0, 0, 0},
  {"compress", 'C', "Use compression in server/client protocol.",
   (uchar**) &opt_compress, (uchar**) &opt_compress, 0, GET_BOOL, NO_ARG, 0, 0, 0,
   0, 0, 0},
  {"create-options", OPT_CREATE_OPTIONS,
   "Include all MySQL specific create options.",
   (uchar**) &create_options, (uchar**) &create_options, 0, GET_BOOL, NO_ARG, 1,
   0, 0, 0, 0, 0},
  {"databases", 'B',
   "To dump several databases. Note the difference in usage; In this case no tables are given. All name arguments are regarded as databasenames. 'USE db_name;' will be included in the output.",
   (uchar**) &opt_databases, (uchar**) &opt_databases, 0, GET_BOOL, NO_ARG, 0, 0,
   0, 0, 0, 0},
#ifdef DBUG_OFF
  {"debug", '#', "This is a non-debug version. Catch this and exit",
   0,0, 0, GET_DISABLED, OPT_ARG, 0, 0, 0, 0, 0, 0},
#else
  {"debug", '#', "Output debug log", (uchar**) &default_dbug_option,
   (uchar**) &default_dbug_option, 0, GET_STR, OPT_ARG, 0, 0, 0, 0, 0, 0},
#endif
  {"debug-check", OPT_DEBUG_CHECK, "Check memory and open file usage at exit.",
   (uchar**) &debug_check_flag, (uchar**) &debug_check_flag, 0,
   GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"debug-info", OPT_DEBUG_INFO, "Print some debug info at exit.",
   (uchar**) &debug_info_flag, (uchar**) &debug_info_flag,
   0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"default-character-set", OPT_DEFAULT_CHARSET,
   "Set the default character set.", (uchar**) &default_charset,
   (uchar**) &default_charset, 0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"delayed-insert", OPT_DELAYED, "Insert rows with INSERT DELAYED; ",
   (uchar**) &opt_delayed, (uchar**) &opt_delayed, 0, GET_BOOL, NO_ARG, 0, 0, 0, 0,
   0, 0},
  {"delete-master-logs", OPT_DELETE_MASTER_LOGS,
   "Delete logs on master after backup. This automatically enables --master-data.",
   (uchar**) &opt_delete_master_logs, (uchar**) &opt_delete_master_logs, 0,
   GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"disable-keys", 'K',
   "'/*!40000 ALTER TABLE tb_name DISABLE KEYS */; and '/*!40000 ALTER TABLE tb_name ENABLE KEYS */; will be put in the output.", (uchar**) &opt_disable_keys,
   (uchar**) &opt_disable_keys, 0, GET_BOOL, NO_ARG, 1, 0, 0, 0, 0, 0},
  {"dump-slave", OPT_MYSQLDUMP_SLAVE_DATA,
   "This causes the binary log position and filename of the master to be "
   "appended to the dumped data output. Setting the value to 1, will print"
   "it as a CHANGE MASTER command in the dumped data output; if equal"
   " to 2, that command will be prefixed with a comment symbol. "
   "This option will turn --lock-all-tables on, unless "
   "--single-transaction is specified too (in which case a "
   "global read lock is only taken a short time at the beginning of the dump "
   "- don't forget to read about --single-transaction below). In all cases "
   "any action on logs will happen at the exact moment of the dump."
   "Option automatically turns --lock-tables off.",
   (uchar**) &opt_slave_data, (uchar**) &opt_slave_data, 0,
   GET_UINT, OPT_ARG, 0, 0, MYSQL_OPT_SLAVE_DATA_COMMENTED_SQL, 0, 0, 0},
  {"events", 'E', "Dump events.",
     (uchar**) &opt_events, (uchar**) &opt_events, 0, GET_BOOL,
     NO_ARG, 0, 0, 0, 0, 0, 0},
  {"extended-insert", 'e',
   "Allows utilization of the new, much faster INSERT syntax.",
   (uchar**) &extended_insert, (uchar**) &extended_insert, 0, GET_BOOL, NO_ARG,
   1, 0, 0, 0, 0, 0},
  {"fields-terminated-by", OPT_FTB,
   "Fields in the textfile are terminated by ...", (uchar**) &fields_terminated,
   (uchar**) &fields_terminated, 0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"fields-enclosed-by", OPT_ENC,
   "Fields in the importfile are enclosed by ...", (uchar**) &enclosed,
   (uchar**) &enclosed, 0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0 ,0, 0},
  {"fields-optionally-enclosed-by", OPT_O_ENC,
   "Fields in the i.file are opt. enclosed by ...", (uchar**) &opt_enclosed,
   (uchar**) &opt_enclosed, 0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0 ,0, 0},
  {"fields-escaped-by", OPT_ESC, "Fields in the i.file are escaped by ...",
   (uchar**) &escaped, (uchar**) &escaped, 0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"first-slave", 'x', "Deprecated, renamed to --lock-all-tables.",
   (uchar**) &opt_lock_all_tables, (uchar**) &opt_lock_all_tables, 0, GET_BOOL, NO_ARG,
   0, 0, 0, 0, 0, 0},
  {"flush-logs", 'F', "Flush logs file in server before starting dump. "
   "Note that if you dump many databases at once (using the option "
   "--databases= or --all-databases), the logs will be flushed for "
   "each database dumped. The exception is when using --lock-all-tables "
   "or --master-data: "
   "in this case the logs will be flushed only once, corresponding "
   "to the moment all tables are locked. So if you want your dump and "
   "the log flush to happen at the same exact moment you should use "
   "--lock-all-tables or --master-data with --flush-logs",
   (uchar**) &flush_logs, (uchar**) &flush_logs, 0, GET_BOOL, NO_ARG, 0, 0, 0, 0,
   0, 0},
  {"flush-privileges", OPT_ESC, "Emit a FLUSH PRIVILEGES statement "
   "after dumping the mysql database.  This option should be used any "
   "time the dump contains the mysql database and any other database "
   "that depends on the data in the mysql database for proper restore. ",
   (uchar**) &flush_privileges, (uchar**) &flush_privileges, 0, GET_BOOL, NO_ARG, 0, 0, 0, 0,
   0, 0},
  {"force", 'f', "Continue even if we get an sql-error.",
   (uchar**) &ignore_errors, (uchar**) &ignore_errors, 0, GET_BOOL, NO_ARG,
   0, 0, 0, 0, 0, 0},
  {"help", '?', "Display this help message and exit.", 0, 0, 0, GET_NO_ARG,
   NO_ARG, 0, 0, 0, 0, 0, 0},
  {"hex-blob", OPT_HEXBLOB, "Dump binary strings (BINARY, "
    "VARBINARY, BLOB) in hexadecimal format.",
   (uchar**) &opt_hex_blob, (uchar**) &opt_hex_blob, 0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"host", 'h', "Connect to host.", (uchar**) &current_host,
   (uchar**) &current_host, 0, GET_STR_ALLOC, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"ignore-table", OPT_IGNORE_TABLE,
   "Do not dump the specified table. To specify more than one table to ignore, "
   "use the directive multiple times, once for each table.  Each table must "
   "be specified with both database and table names, e.g. --ignore-table=database.table",
   0, 0, 0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"include-master-host-port", OPT_MYSQLDUMP_INCLUDE_MASTER_HOST_PORT,
   "Adds 'MASTER_HOST=<host>, MASTER_PORT=<port>' to 'CHANGE MASTER TO..' in dump produced with --dump-slave.",
   (uchar**) &opt_include_master_host_port, 
   (uchar**) &opt_include_master_host_port, 
   0, GET_BOOL, NO_ARG,
   0, 0, 0, 0, 0, 0},
  {"insert-ignore", OPT_INSERT_IGNORE, "Insert rows with INSERT IGNORE.",
   (uchar**) &opt_ignore, (uchar**) &opt_ignore, 0, GET_BOOL, NO_ARG, 0, 0, 0, 0,
   0, 0},
  {"lines-terminated-by", OPT_LTB, "Lines in the i.file are terminated by ...",
   (uchar**) &lines_terminated, (uchar**) &lines_terminated, 0, GET_STR,
   REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"lock-all-tables", 'x', "Locks all tables across all databases. This "
   "is achieved by taking a global read lock for the duration of the whole "
   "dump. Automatically turns --single-transaction and --lock-tables off.",
   (uchar**) &opt_lock_all_tables, (uchar**) &opt_lock_all_tables, 0, GET_BOOL, NO_ARG,
   0, 0, 0, 0, 0, 0},
  {"lock-tables", 'l', "Lock all tables for read.", (uchar**) &lock_tables,
   (uchar**) &lock_tables, 0, GET_BOOL, NO_ARG, 1, 0, 0, 0, 0, 0},
  {"log-error", OPT_ERROR_LOG_FILE, "Append warnings and errors to given file.",
   (uchar**) &log_error_file, (uchar**) &log_error_file, 0, GET_STR,
   REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"master-data", OPT_MASTER_DATA,
   "This causes the binary log position and filename to be appended to the "
   "output. If equal to 1, will print it as a CHANGE MASTER command; if equal"
   " to 2, that command will be prefixed with a comment symbol. "
   "This option will turn --lock-all-tables on, unless "
   "--single-transaction is specified too (in which case a "
   "global read lock is only taken a short time at the beginning of the dump "
   "- don't forget to read about --single-transaction below). In all cases "
   "any action on logs will happen at the exact moment of the dump."
   "Option automatically turns --lock-tables off.",
   (uchar**) &opt_master_data, (uchar**) &opt_master_data, 0,
   GET_UINT, OPT_ARG, 0, 0, MYSQL_OPT_MASTER_DATA_COMMENTED_SQL, 0, 0, 0},
  {"max_allowed_packet", OPT_MAX_ALLOWED_PACKET, "",
    (uchar**) &opt_max_allowed_packet, (uchar**) &opt_max_allowed_packet, 0,
    GET_ULONG, REQUIRED_ARG, 24*1024*1024, 4096,
   (longlong) 2L*1024L*1024L*1024L, MALLOC_OVERHEAD, 1024, 0},
  {"net_buffer_length", OPT_NET_BUFFER_LENGTH, "",
    (uchar**) &opt_net_buffer_length, (uchar**) &opt_net_buffer_length, 0,
    GET_ULONG, REQUIRED_ARG, 1024*1024L-1025, 4096, 16*1024L*1024L,
   MALLOC_OVERHEAD-1024, 1024, 0},
  {"no-autocommit", OPT_AUTOCOMMIT,
   "Wrap tables with autocommit/commit statements.",
   (uchar**) &opt_autocommit, (uchar**) &opt_autocommit, 0, GET_BOOL, NO_ARG,
   0, 0, 0, 0, 0, 0},
  {"no-create-db", 'n',
   "'CREATE DATABASE /*!32312 IF NOT EXISTS*/ db_name;' will not be put in the output. The above line will be added otherwise, if --databases or --all-databases option was given.}.",
   (uchar**) &opt_create_db, (uchar**) &opt_create_db, 0, GET_BOOL, NO_ARG, 0, 0,
   0, 0, 0, 0},
  {"no-create-info", 't', "Don't write table creation info.",
   (uchar**) &opt_no_create_info, (uchar**) &opt_no_create_info, 0, GET_BOOL,
   NO_ARG, 0, 0, 0, 0, 0, 0},
  {"no-data", 'd', "No row information.", (uchar**) &opt_no_data,
   (uchar**) &opt_no_data, 0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"no-set-names", 'N',
   "Deprecated. Use --skip-set-charset instead.",
   0, 0, 0, GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"opt", OPT_OPTIMIZE,
   "Same as --add-drop-table, --add-locks, --create-options, --quick, --extended-insert, --lock-tables, --set-charset, and --disable-keys. Enabled by default, disable with --skip-opt.",
   0, 0, 0, GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"order-by-primary", OPT_ORDER_BY_PRIMARY,
   "Sorts each table's rows by primary key, or first unique key, if such a key exists.  Useful when dumping a MyISAM table to be loaded into an InnoDB table, but will make the dump itself take considerably longer.",
   (uchar**) &opt_order_by_primary, (uchar**) &opt_order_by_primary, 0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"password", 'p',
   "Password to use when connecting to server. If password is not given it's solicited on the tty.",
   0, 0, 0, GET_STR, OPT_ARG, 0, 0, 0, 0, 0, 0},
  {"port", 'P', "Port number to use for connection.", (uchar**) &opt_mysql_port,
   (uchar**) &opt_mysql_port, 0, GET_UINT, REQUIRED_ARG, 0, 0, 0, 0, 0,
   0},
  {"quick", 'q', "Don't buffer query, dump directly to stdout.",
   (uchar**) &quick, (uchar**) &quick, 0, GET_BOOL, NO_ARG, 1, 0, 0, 0, 0, 0},
  {"quote-names",'Q', "Quote table and column names with backticks (`).",
   (uchar**) &opt_quoted, (uchar**) &opt_quoted, 0, GET_BOOL, NO_ARG, 1, 0, 0, 0,
   0, 0},
  {"replace", OPT_MYSQL_REPLACE_INTO, "Use REPLACE INTO instead of INSERT INTO.",
   (uchar**) &opt_replace_into, (uchar**) &opt_replace_into, 0, GET_BOOL, NO_ARG, 0, 0, 0, 0,
   0, 0},
  {"result-file", 'r',
   "Direct output to a given file. This option should be used in MSDOS, because it prevents new line '\\n' from being converted to '\\r\\n' (carriage return + line feed).",
   0, 0, 0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"routines", 'R', "Dump stored routines (functions and procedures).",
     (uchar**) &opt_routines, (uchar**) &opt_routines, 0, GET_BOOL,
     NO_ARG, 0, 0, 0, 0, 0, 0},
  {"set-charset", OPT_SET_CHARSET,
   "Add 'SET NAMES default_character_set' to the output.",
   (uchar**) &opt_set_charset, (uchar**) &opt_set_charset, 0, GET_BOOL, NO_ARG, 1,
   0, 0, 0, 0, 0},
  {"set-variable", 'O',
   "Change the value of a variable. Please note that this option is deprecated; you can set variables directly with --variable-name=value.",
   0, 0, 0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
#ifdef HAVE_SMEM
  {"shared-memory-base-name", OPT_SHARED_MEMORY_BASE_NAME,
   "Base name of shared memory.", (uchar**) &shared_memory_base_name, (uchar**) &shared_memory_base_name,
   0, GET_STR_ALLOC, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
#endif
  /*
    Note that the combination --single-transaction --master-data
    will give bullet-proof binlog position only if server >=4.1.3. That's the
    old "FLUSH TABLES WITH READ LOCK does not block commit" fixed bug.
  */
  {"single-transaction", OPT_TRANSACTION,
   "Creates a consistent snapshot by dumping all tables in a single "
   "transaction. Works ONLY for tables stored in storage engines which "
   "support multiversioning (currently only InnoDB does); the dump is NOT "
   "guaranteed to be consistent for other storage engines. "
   "While a --single-transaction dump is in process, to ensure a valid "
   "dump file (correct table contents and binary log position), no other "
   "connection should use the following statements: ALTER TABLE, DROP "
   "TABLE, RENAME TABLE, TRUNCATE TABLE, as consistent snapshot is not "
   "isolated from them. Option automatically turns off --lock-tables.",
   (uchar**) &opt_single_transaction, (uchar**) &opt_single_transaction, 0,
   GET_BOOL, NO_ARG,  0, 0, 0, 0, 0, 0},
  {"dump-date", OPT_DUMP_DATE, "Put a dump date to the end of the output.",
   (uchar**) &opt_dump_date, (uchar**) &opt_dump_date, 0,
   GET_BOOL, NO_ARG, 1, 0, 0, 0, 0, 0},
  {"skip-opt", OPT_SKIP_OPTIMIZATION,
   "Disable --opt. Disables --add-drop-table, --add-locks, --create-options, --quick, --extended-insert, --lock-tables, --set-charset, and --disable-keys.",
   0, 0, 0, GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"tab",'T',
   "Creates tab separated textfile for each table to given path. (creates .sql and .txt files). NOTE: This only works if mysqldump is run on the same machine as the mysqld daemon.",
   (uchar**) &path, (uchar**) &path, 0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"tables", OPT_TABLES, "Overrides option --databases (-B).",
   0, 0, 0, GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"tz-utc", OPT_TZ_UTC,
    "SET TIME_ZONE='+00:00' at top of dump to allow dumping of TIMESTAMP data when a server has data in different time zones or data is being moved between servers with different time zones.",
    (uchar**) &opt_tz_utc, (uchar**) &opt_tz_utc, 0, GET_BOOL, NO_ARG, 1, 0, 0, 0, 0, 0},
#ifndef DONT_ALLOW_USER_CHANGE
  {"user", 'u', "User for login if not current user.",
   (uchar**) &current_user, (uchar**) &current_user, 0, GET_STR, REQUIRED_ARG,
   0, 0, 0, 0, 0, 0},
#endif
  {"verbose", 'v', "Print info about the various stages.",
   (uchar**) &verbose, (uchar**) &verbose, 0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"version",'V', "Output version information and exit.", 0, 0, 0,
   GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"where", 'w', "Dump only selected records; QUOTES mandatory!",
   (uchar**) &where, (uchar**) &where, 0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"xml", 'X', "Dump a database as well formed XML.", 0, 0, 0, GET_NO_ARG,
   NO_ARG, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0}
};

static const char *load_default_groups[]= { "mysqldump","client",0 };

static void maybe_exit(int error);
static void die(int error, const char* reason, ...);
static void maybe_die(int error, const char* reason, ...);
static void write_header(FILE *sql_file, char *db_name);
static void print_value(FILE *file, MYSQL_RES  *result, MYSQL_ROW row,
                        const char *prefix,const char *name,
                        int string_value);
static int dump_selected_tables(char *db, char **table_names, int tables);
static int dump_all_tables_in_db(char *db);
static int init_dumping_tables(char *);
static int init_dumping(char *, int init_func(char*));
static int dump_databases(char **);
static int dump_all_databases(void);
static char *quote_name(const char *name, char *buff, my_bool force);
char check_if_ignore_table(const char *table_name, char *table_type);
static char *primary_key_fields(const char *table_name);

#include <help_start.h>

/*
  Print the supplied message if in verbose mode

  SYNOPSIS
    verbose_msg()
    fmt   format specifier
    ...   variable number of parameters
*/

static void verbose_msg(const char *fmt, ...)
{
  va_list args;
  DBUG_ENTER("verbose_msg");

  if (!verbose)
    DBUG_VOID_RETURN;

  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);

  DBUG_VOID_RETURN;
}

/*
  exit with message if ferror(file)

  SYNOPSIS
    check_io()
    file        - checked file
*/

static void check_io(FILE *file)
{
  if (ferror(file))
    die(EX_EOF, "Got errno %d on write", errno);
}

static void print_version(void)
{
  printf("%s  Ver %s Distrib %s, for %s (%s)\n",my_progname,DUMP_VERSION,
         MYSQL_SERVER_VERSION,SYSTEM_TYPE,MACHINE_TYPE);
} /* print_version */


static void short_usage_sub(void)
{
  printf("Usage: %s [OPTIONS] database [tables]\n", my_progname);
  printf("OR     %s [OPTIONS] --databases [OPTIONS] DB1 [DB2 DB3...]\n",
         my_progname);
  printf("OR     %s [OPTIONS] --all-databases [OPTIONS]\n", my_progname);
}


static void usage(void)
{
  print_version();
  puts("By Igor Romanenko, Monty, Jani & Sinisa");
  puts("This software comes with ABSOLUTELY NO WARRANTY. This is free software,\nand you are welcome to modify and redistribute it under the GPL license\n");
  puts("Dumping definition and data mysql database or table");
  short_usage_sub();
  print_defaults("my",load_default_groups);
  my_print_help(my_long_options);
  my_print_variables(my_long_options);
} /* usage */


static void short_usage(void)
{
  short_usage_sub();
  printf("For more options, use %s --help\n", my_progname);
}

#include <help_end.h>


static void write_header(FILE *sql_file, char *db_name)
{
  if (opt_xml)
  {
    fputs("<?xml version=\"1.0\"?>\n", sql_file);
    /*
      Schema reference.  Allows use of xsi:nil for NULL values and 
      xsi:type to define an element's data type.
    */
    fputs("<mysqldump ", sql_file);
    fputs("xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"",
          sql_file);
    fputs(">\n", sql_file);
    check_io(sql_file);
  }
  else if (!opt_compact)
  {
    if (opt_comments)
    {
      fprintf(sql_file,
              "-- MySQL dump %s  Distrib %s, for %s (%s)\n--\n",
              DUMP_VERSION, MYSQL_SERVER_VERSION, SYSTEM_TYPE, MACHINE_TYPE);
      fprintf(sql_file, "-- Host: %s    Database: %s\n",
              current_host ? current_host : "localhost", db_name ? db_name :
              "");
      fputs("-- ------------------------------------------------------\n",
            sql_file);
      fprintf(sql_file, "-- Server version\t%s\n",
              mysql_get_server_info(&mysql_connection));
    }
    if (opt_set_charset)
      fprintf(sql_file,
"\n/*!40101 SET @OLD_CHARACTER_SET_CLIENT=@@CHARACTER_SET_CLIENT */;"
"\n/*!40101 SET @OLD_CHARACTER_SET_RESULTS=@@CHARACTER_SET_RESULTS */;"
"\n/*!40101 SET @OLD_COLLATION_CONNECTION=@@COLLATION_CONNECTION */;"
"\n/*!40101 SET NAMES %s */;\n",default_charset);

    if (opt_tz_utc)
    {
      fprintf(sql_file, "/*!40103 SET @OLD_TIME_ZONE=@@TIME_ZONE */;\n");
      fprintf(sql_file, "/*!40103 SET TIME_ZONE='+00:00' */;\n");
    }

    if (!path)
    {
      fprintf(md_result_file,"\
/*!40014 SET @OLD_UNIQUE_CHECKS=@@UNIQUE_CHECKS, UNIQUE_CHECKS=0 */;\n\
/*!40014 SET @OLD_FOREIGN_KEY_CHECKS=@@FOREIGN_KEY_CHECKS, FOREIGN_KEY_CHECKS=0 */;\n\
");
    }
    check_io(sql_file);
  }
} /* write_header */


static void write_footer(FILE *sql_file)
{
  if (opt_xml)
  {
    fputs("</mysqldump>\n", sql_file);
    check_io(sql_file);
  }
  else if (!opt_compact)
  {
    if (opt_tz_utc)
      fprintf(sql_file,"/*!40103 SET TIME_ZONE=@OLD_TIME_ZONE */;\n");

    if (!path)
    {
      fprintf(md_result_file,"\
/*!40014 SET FOREIGN_KEY_CHECKS=@OLD_FOREIGN_KEY_CHECKS */;\n\
/*!40014 SET UNIQUE_CHECKS=@OLD_UNIQUE_CHECKS */;\n");
    }
    if (opt_set_charset)
      fprintf(sql_file,
"/*!40101 SET CHARACTER_SET_CLIENT=@OLD_CHARACTER_SET_CLIENT */;\n"
"/*!40101 SET CHARACTER_SET_RESULTS=@OLD_CHARACTER_SET_RESULTS */;\n"
"/*!40101 SET COLLATION_CONNECTION=@OLD_COLLATION_CONNECTION */;\n");
    fprintf(sql_file,
            "/*!40111 SET SQL_NOTES=@OLD_SQL_NOTES */;\n");
    fputs("\n", sql_file);
    if (opt_comments)
    {
      if (opt_dump_date)
      {
        char time_str[20];
        get_date(time_str, GETDATE_DATE_TIME, 0);
        fprintf(sql_file, "-- Dump completed on %s\n",
                time_str);
      }
      else
        fprintf(sql_file, "-- Dump completed\n");
    }
    check_io(sql_file);
  }
} /* write_footer */


static void free_table_ent(char *key)
{
  my_free(key, MYF(0));
}


static uchar* get_table_key(const char *entry, size_t *length,
                            my_bool not_used __attribute__((unused)))
{
  *length= strlen(entry);
  return (uchar*) entry;
}


static my_bool
get_one_option(int optid, const struct my_option *opt __attribute__((unused)),
               char *argument)
{
  switch (optid) {
  case 'p':
    if (argument)
    {
      char *start=argument;
      my_free(opt_password,MYF(MY_ALLOW_ZERO_PTR));
      opt_password=my_strdup(argument,MYF(MY_FAE));
      while (*argument) *argument++= 'x';               /* Destroy argument */
      if (*start)
        start[1]=0;                             /* Cut length of argument */
      tty_password= 0;
    }
    else
      tty_password=1;
    break;
  case 'r':
    if (!(md_result_file= my_fopen(argument, O_WRONLY | FILE_BINARY,
                                    MYF(MY_WME))))
      exit(1);
    break;
  case 'N':
    opt_set_charset= 0;
    break;
  case 'T':
    opt_disable_keys=0;

    if (strlen(argument) >= FN_REFLEN)
    {
      /*
        This check is made because the some the file functions below
        have FN_REFLEN sized stack allocated buffers and will cause
        a crash even if the input destination buffer is large enough
        to hold the output.
      */
      die(EX_USAGE, "Input filename too long: %s", argument);
    }

    break;
  case '#':
    DBUG_PUSH(argument ? argument : default_dbug_option);
    debug_check_flag= 1;
    break;
  case 'V': print_version(); exit(0);
  case 'X':
    opt_xml= 1;
    extended_insert= opt_drop= opt_lock=
      opt_disable_keys= opt_autocommit= opt_create_db= 0;
    break;
  case 'I':
  case '?':
    usage();
    exit(0);
  case (int) OPT_MASTER_DATA:
    if (!argument) /* work like in old versions */
      opt_master_data= MYSQL_OPT_MASTER_DATA_EFFECTIVE_SQL;
    break;
  case (int) OPT_MYSQLDUMP_SLAVE_DATA:
    if (!argument) /* work like in old versions */
      opt_slave_data= MYSQL_OPT_SLAVE_DATA_EFFECTIVE_SQL;
    break;
  case (int) OPT_OPTIMIZE:
    extended_insert= opt_drop= opt_lock= quick= create_options=
      opt_disable_keys= lock_tables= opt_set_charset= 1;
    break;
  case (int) OPT_SKIP_OPTIMIZATION:
    extended_insert= opt_drop= opt_lock= quick= create_options=
      opt_disable_keys= lock_tables= opt_set_charset= 0;
    break;
  case (int) OPT_COMPACT:
  if (opt_compact)
  {
    opt_comments= opt_drop= opt_disable_keys= opt_lock= 0;
    opt_set_charset= 0;
  }
  case (int) OPT_TABLES:
    opt_databases=0;
    break;
  case (int) OPT_IGNORE_TABLE:
  {
    if (!strchr(argument, '.'))
    {
      fprintf(stderr, "Illegal use of option --ignore-table=<database>.<table>\n");
      exit(1);
    }
    if (my_hash_insert(&ignore_table, (uchar*)my_strdup(argument, MYF(0))))
      exit(EX_EOM);
    break;
  }
  case (int) OPT_COMPATIBLE:
    {
      char buff[255];
      char *end= compatible_mode_normal_str;
      int i;
      ulong mode;
      uint err_len;

      opt_quoted= 1;
      opt_set_charset= 0;
      opt_compatible_mode_str= argument;
      opt_compatible_mode= find_set(&compatible_mode_typelib,
                                    argument, strlen(argument),
                                    &err_ptr, &err_len);
      if (err_len)
      {
        strmake(buff, err_ptr, min(sizeof(buff), err_len));
        fprintf(stderr, "Invalid mode to --compatible: %s\n", buff);
        exit(1);
      }
#if !defined(DBUG_OFF)
      {
        uint size_for_sql_mode= 0;
        const char **ptr;
        for (ptr= compatible_mode_names; *ptr; ptr++)
          size_for_sql_mode+= strlen(*ptr);
        size_for_sql_mode+= sizeof(compatible_mode_names)-1;
        DBUG_ASSERT(sizeof(compatible_mode_normal_str)>=size_for_sql_mode);
      }
#endif
      mode= opt_compatible_mode;
      for (i= 0, mode= opt_compatible_mode; mode; mode>>= 1, i++)
      {
        if (mode & 1)
        {
          end= strmov(end, compatible_mode_names[i]);
          end= strmov(end, ",");
        }
      }
      if (end!=compatible_mode_normal_str)
        end[-1]= 0;
      /*
        Set charset to the default compiled value if it hasn't
        been reset yet by --default-character-set=xxx.
      */
      if (default_charset == mysql_universal_client_charset)
        default_charset= (char*) MYSQL_DEFAULT_CHARSET_NAME;
      break;
    }
  }
  return 0;
}

static int get_options(int *argc, char ***argv)
{
  int ho_error;
  MYSQL_PARAMETERS *mysql_params= mysql_get_parameters();

  opt_max_allowed_packet= *mysql_params->p_max_allowed_packet;
  opt_net_buffer_length= *mysql_params->p_net_buffer_length;

  md_result_file= stdout;
  load_defaults("my",load_default_groups,argc,argv);
  defaults_argv= *argv;

  if (hash_init(&ignore_table, charset_info, 16, 0, 0,
                (hash_get_key) get_table_key,
                (hash_free_key) free_table_ent, 0))
    return(EX_EOM);
  /* Don't copy internal log tables */
  if (my_hash_insert(&ignore_table,
                     (uchar*) my_strdup("mysql.apply_status", MYF(MY_WME))) ||
      my_hash_insert(&ignore_table,
                     (uchar*) my_strdup("mysql.schema", MYF(MY_WME))) ||
      my_hash_insert(&ignore_table,
                     (uchar*) my_strdup("mysql.general_log", MYF(MY_WME))) ||
      my_hash_insert(&ignore_table,
                     (uchar*) my_strdup("mysql.slow_log", MYF(MY_WME))) ||
      my_hash_insert(&ignore_table,
                     (uchar*) my_strdup("mysql.online_backup", MYF(MY_WME))) ||
      my_hash_insert(&ignore_table,
                     (uchar*) my_strdup("mysql.online_backup_progress", MYF(MY_WME))))
    return(EX_EOM);

  if ((ho_error= handle_options(argc, argv, my_long_options, get_one_option)))
    return(ho_error);

  *mysql_params->p_max_allowed_packet= opt_max_allowed_packet;
  *mysql_params->p_net_buffer_length= opt_net_buffer_length;
  if (debug_info_flag)
    my_end_arg= MY_CHECK_ERROR | MY_GIVE_INFO;
  if (debug_check_flag)
    my_end_arg= MY_CHECK_ERROR;

  if (opt_delayed)
    opt_lock=0;                         /* Can't have lock with delayed */
  if (!path && (enclosed || opt_enclosed || escaped || lines_terminated ||
                fields_terminated))
  {
    fprintf(stderr,
            "%s: You must use option --tab with --fields-...\n", my_progname);
    return(EX_USAGE);
  }

  /* We don't delete master logs if slave data option */
  if (opt_slave_data)
  {
    opt_lock_all_tables= !opt_single_transaction;
    opt_master_data= 0;
    opt_delete_master_logs= 0;
  }

  /* Ensure consistency of the set of binlog & locking options */
  if (opt_delete_master_logs && !opt_master_data)
    opt_master_data= MYSQL_OPT_MASTER_DATA_COMMENTED_SQL;
  if (opt_single_transaction && opt_lock_all_tables)
  {
    fprintf(stderr, "%s: You can't use --single-transaction and "
            "--lock-all-tables at the same time.\n", my_progname);
    return(EX_USAGE);
  }
  if (opt_master_data)
  {
    opt_lock_all_tables= !opt_single_transaction;
    opt_slave_data= 0;
  }
  if (opt_single_transaction || opt_lock_all_tables)
    lock_tables= 0;
  if (enclosed && opt_enclosed)
  {
    fprintf(stderr, "%s: You can't use ..enclosed.. and ..optionally-enclosed.. at the same time.\n", my_progname);
    return(EX_USAGE);
  }
  if ((opt_databases || opt_alldbs) && path)
  {
    fprintf(stderr,
            "%s: --databases or --all-databases can't be used with --tab.\n",
            my_progname);
    return(EX_USAGE);
  }
  if (strcmp(default_charset, charset_info->csname) &&
      !(charset_info= get_charset_by_csname(default_charset,
                                            MY_CS_PRIMARY, MYF(MY_WME))))
    exit(1);
  if ((*argc < 1 && !opt_alldbs) || (*argc > 0 && opt_alldbs))
  {
    short_usage();
    return EX_USAGE;
  }
  if (tty_password)
    opt_password=get_tty_password(NullS);
  return(0);
} /* get_options */


/*
** DB_error -- prints mysql error message and exits the program.
*/
static void DB_error(MYSQL *mysql_arg, const char *when)
{
  DBUG_ENTER("DB_error");
  maybe_die(EX_MYSQLERR, "Got error: %d: %s %s",
          mysql_errno(mysql_arg), mysql_error(mysql_arg), when);
  DBUG_VOID_RETURN;
}



/*
  Prints out an error message and kills the process.

  SYNOPSIS
    die()
    error_num   - process return value
    fmt_reason  - a format string for use by vsnprintf.
    ...         - variable arguments for above fmt_reason string
  
  DESCRIPTION
    This call prints out the formatted error message to stderr and then
    terminates the process.
*/
static void die(int error_num, const char* fmt_reason, ...)
{
  char buffer[1000];
  va_list args;
  va_start(args,fmt_reason);
  vsnprintf(buffer, sizeof(buffer), fmt_reason, args);
  va_end(args);

  fprintf(stderr, "%s: %s\n", my_progname, buffer);
  fflush(stderr);

  ignore_errors= 0; /* force the exit */
  maybe_exit(error_num);
}


/*
  Prints out an error message and maybe kills the process.

  SYNOPSIS
    maybe_die()
    error_num   - process return value
    fmt_reason  - a format string for use by vsnprintf.
    ...         - variable arguments for above fmt_reason string
  
  DESCRIPTION
    This call prints out the formatted error message to stderr and then
    terminates the process, unless the --force command line option is used.
    
    This call should be used for non-fatal errors (such as database
    errors) that the code may still be able to continue to the next unit
    of work.
    
*/
static void maybe_die(int error_num, const char* fmt_reason, ...)
{
  char buffer[1000];
  va_list args;
  va_start(args,fmt_reason);
  vsnprintf(buffer, sizeof(buffer), fmt_reason, args);
  va_end(args);

  fprintf(stderr, "%s: %s\n", my_progname, buffer);
  fflush(stderr);

  maybe_exit(error_num);
}



/*
  Sends a query to server, optionally reads result, prints error message if
  some.

  SYNOPSIS
    mysql_query_with_error_report()
    mysql_con       connection to use
    res             if non zero, result will be put there with
                    mysql_store_result()
    query           query to send to server

  RETURN VALUES
    0               query sending and (if res!=0) result reading went ok
    1               error
*/

static int mysql_query_with_error_report(MYSQL *mysql_con, MYSQL_RES **res,
                                         const char *query)
{
  if (mysql_query(mysql_con, query) ||
      (res && !((*res)= mysql_store_result(mysql_con))))
  {
    maybe_die(EX_MYSQLERR, "Couldn't execute '%s': %s (%d)",
            query, mysql_error(mysql_con), mysql_errno(mysql_con));
    return 1;
  }
  return 0;
}


/**
  Switch charset for results to some specified charset.  If the server does not
  support character_set_results variable, nothing can be done here.  As for
  whether something should be done here, future new callers of this function
  should be aware that the server lacking the facility of switching charsets is
  treated as success.

  @note  If the server lacks support, then nothing is changed and no error
         condition is returned.

  @returns  whether there was an error or not
*/
static int switch_character_set_results(MYSQL *mysql, const char *cs_name)
{
  char query_buffer[QUERY_LENGTH];
  size_t query_length;

  /* Server lacks facility.  This is not an error, by arbitrary decision . */
  if (!server_supports_switching_charsets)
    return FALSE;

  query_length= snprintf(query_buffer,
                         sizeof (query_buffer),
                         "SET SESSION character_set_results = '%s'",
                         (const char *) cs_name);

  return mysql_real_query(mysql, query_buffer, query_length);
}

/*
  Open a new .sql file to dump the table or view into

  SYNOPSIS
    open_sql_file_for_table
    name      name of the table or view

  RETURN VALUES
    0        Failed to open file
    > 0      Handle of the open file
*/
static FILE* open_sql_file_for_table(const char* table)
{
  FILE* res;
  char filename[FN_REFLEN], tmp_path[FN_REFLEN];
  convert_dirname(tmp_path,path,NullS);
  res= my_fopen(fn_format(filename, table, tmp_path, ".sql", 4),
                O_WRONLY, MYF(MY_WME));
  return res;
}


static void free_resources(void)
{
  if (md_result_file && md_result_file != stdout)
    my_fclose(md_result_file, MYF(0));
  my_free(opt_password, MYF(MY_ALLOW_ZERO_PTR));
  if (hash_inited(&ignore_table))
    hash_free(&ignore_table);
  if (extended_insert)
    dynstr_free(&extended_row);
  if (insert_pat_inited)
    dynstr_free(&insert_pat);
  if (defaults_argv)
    free_defaults(defaults_argv);
  my_end(my_end_arg);
}


static void maybe_exit(int error)
{
  if (!first_error)
    first_error= error;
  if (ignore_errors)
    return;
  if (mysql)
    mysql_close(mysql);
  free_resources();
  exit(error);
}


/*
  db_connect -- connects to the host and selects DB.
*/

static int connect_to_db(char *host, char *user,char *passwd)
{
  char buff[20+FN_REFLEN];
  DBUG_ENTER("connect_to_db");

  verbose_msg("-- Connecting to %s...\n", host ? host : "localhost");
  mysql_init(&mysql_connection);
  if (opt_compress)
    mysql_options(&mysql_connection,MYSQL_OPT_COMPRESS,NullS);
  if (opt_protocol)
    mysql_options(&mysql_connection,MYSQL_OPT_PROTOCOL,(char*)&opt_protocol);
#ifdef HAVE_SMEM
  if (shared_memory_base_name)
    mysql_options(&mysql_connection,MYSQL_SHARED_MEMORY_BASE_NAME,shared_memory_base_name);
#endif
  mysql_options(&mysql_connection, MYSQL_SET_CHARSET_NAME, default_charset);
  if (!(mysql= mysql_real_connect(&mysql_connection,host,user,passwd,
                                  NULL,opt_mysql_port, NULL,
                                  0)))
  {
    DB_error(&mysql_connection, "when trying to connect");
    DBUG_RETURN(1);
  }
  if (mysql_get_server_version(&mysql_connection) < 40100)
  {
    /* Don't dump SET NAMES with a pre-4.1 server (bug#7997).  */
    opt_set_charset= 0;

    /* Don't switch charsets for 4.1 and earlier.  (bug#34192). */
    server_supports_switching_charsets= FALSE;
  } 
  /*
    set time_zone to UTC to allow dumping date types between servers with
    different time zone settings
  */
  if (opt_tz_utc)
  {
    snprintf(buff, sizeof(buff), "/*!40103 SET TIME_ZONE='+00:00' */");
    if (mysql_query_with_error_report(mysql, 0, buff))
      DBUG_RETURN(1);
  }
  DBUG_RETURN(0);
} /* connect_to_db */


/*
** dbDisconnect -- disconnects from the host.
*/
static void dbDisconnect(char *host)
{
  verbose_msg("-- Disconnecting from %s...\n", host ? host : "localhost");
  mysql_close(mysql);
} /* dbDisconnect */


static void unescape(FILE *file,char *pos,uint length)
{
  char *tmp;
  DBUG_ENTER("unescape");
  if (!(tmp=(char*) my_malloc(length*2+1, MYF(MY_WME))))
    die(EX_MYSQLERR, "Couldn't allocate memory");

  mysql_real_escape_string(&mysql_connection, tmp, pos, length);
  fputc('\'', file);
  fputs(tmp, file);
  fputc('\'', file);
  check_io(file);
  my_free(tmp, MYF(MY_WME));
  DBUG_VOID_RETURN;
} /* unescape */


static my_bool test_if_special_chars(const char *str)
{
#if MYSQL_VERSION_ID >= 32300
  for ( ; *str ; str++)
    if (!my_isvar(charset_info,*str) && *str != '$')
      return 1;
#endif
  return 0;
} /* test_if_special_chars */



/*
  quote_name(name, buff, force)

  Quotes char string, taking into account compatible mode

  Args

  name                 Unquoted string containing that which will be quoted
  buff                 The buffer that contains the quoted value, also returned
  force                Flag to make it ignore 'test_if_special_chars'

  Returns

  buff                 quoted string

*/
static char *quote_name(const char *name, char *buff, my_bool force)
{
  char *to= buff;
  char qtype= (opt_compatible_mode & MASK_ANSI_QUOTES) ? '\"' : '`';

  if (!force && !opt_quoted && !test_if_special_chars(name))
    return (char*) name;
  *to++= qtype;
  while (*name)
  {
    if (*name == qtype)
      *to++= qtype;
    *to++= *name++;
  }
  to[0]= qtype;
  to[1]= 0;
  return buff;
} /* quote_name */


/*
  Quote a table name so it can be used in "SHOW TABLES LIKE <tabname>"

  SYNOPSIS
    quote_for_like()
    name     name of the table
    buff     quoted name of the table

  DESCRIPTION
    Quote \, _, ' and % characters

    Note: Because MySQL uses the C escape syntax in strings
    (for example, '\n' to represent newline), you must double
    any '\' that you use in your LIKE  strings. For example, to
    search for '\n', specify it as '\\n'. To search for '\', specify
    it as '\\\\' (the backslashes are stripped once by the parser
    and another time when the pattern match is done, leaving a
    single backslash to be matched).

    Example: "t\1" => "t\\\\1"

*/
static char *quote_for_like(const char *name, char *buff)
{
  char *to= buff;
  *to++= '\'';
  while (*name)
  {
    if (*name == '\\')
    {
      *to++='\\';
      *to++='\\';
      *to++='\\';
    }
    else if (*name == '\'' || *name == '_'  || *name == '%')
      *to++= '\\';
    *to++= *name++;
  }
  to[0]= '\'';
  to[1]= 0;
  return buff;
}


/*
  Quote and print a string.

  SYNOPSIS
    print_quoted_xml()
    xml_file    - output file
    str         - string to print
    len         - its length

  DESCRIPTION
    Quote '<' '>' '&' '\"' chars and print a string to the xml_file.
*/

static void print_quoted_xml(FILE *xml_file, const char *str, ulong len)
{
  const char *end;

  for (end= str + len; str != end; str++)
  {
    switch (*str) {
    case '<':
      fputs("&lt;", xml_file);
      break;
    case '>':
      fputs("&gt;", xml_file);
      break;
    case '&':
      fputs("&amp;", xml_file);
      break;
    case '\"':
      fputs("&quot;", xml_file);
      break;
    default:
      fputc(*str, xml_file);
      break;
    }
  }
  check_io(xml_file);
}


/*
  Print xml tag. Optionally add attribute(s).

  SYNOPSIS
    print_xml_tag(xml_file, sbeg, send, tag_name, first_attribute_name, 
                    ..., attribute_name_n, attribute_value_n, NullS)
    xml_file              - output file
    sbeg                  - line beginning
    line_end              - line ending
    tag_name              - XML tag name.
    first_attribute_name  - tag and first attribute
    first_attribute_value - (Implied) value of first attribute
    attribute_name_n      - attribute n
    attribute_value_n     - value of attribute n

  DESCRIPTION
    Print XML tag with any number of attribute="value" pairs to the xml_file.

    Format is:
      sbeg<tag_name first_attribute_name="first_attribute_value" ... 
      attribute_name_n="attribute_value_n">send
  NOTE
    Additional arguments must be present in attribute/value pairs.
    The last argument should be the null character pointer.
    All attribute_value arguments MUST be NULL terminated strings.
    All attribute_value arguments will be quoted before output.
*/

static void print_xml_tag(FILE * xml_file, const char* sbeg,
                          const char* line_end, 
                          const char* tag_name, 
                          const char* first_attribute_name, ...)
{
  va_list arg_list;
  const char *attribute_name, *attribute_value;

  fputs(sbeg, xml_file);
  fputc('<', xml_file);
  fputs(tag_name, xml_file);  

  va_start(arg_list, first_attribute_name);
  attribute_name= first_attribute_name;
  while (attribute_name != NullS)
  {
    attribute_value= va_arg(arg_list, char *);
    DBUG_ASSERT(attribute_value != NullS);

    fputc(' ', xml_file);
    fputs(attribute_name, xml_file);    
    fputc('\"', xml_file);
    
    print_quoted_xml(xml_file, attribute_value, strlen(attribute_value));
    fputc('\"', xml_file);

    attribute_name= va_arg(arg_list, char *);
  }
  va_end(arg_list);

  fputc('>', xml_file);
  fputs(line_end, xml_file);
  check_io(xml_file);
}


/*
  Print xml tag with for a field that is null

  SYNOPSIS
    print_xml_null_tag()
    xml_file    - output file
    sbeg        - line beginning
    stag_atr    - tag and attribute
    sval        - value of attribute
    line_end        - line ending

  DESCRIPTION
    Print tag with one attribute to the xml_file. Format is:
      <stag_atr="sval" xsi:nil="true"/>
  NOTE
    sval MUST be a NULL terminated string.
    sval string will be qouted before output.
*/

static void print_xml_null_tag(FILE * xml_file, const char* sbeg,
                               const char* stag_atr, const char* sval,
                               const char* line_end)
{
  fputs(sbeg, xml_file);
  fputs("<", xml_file);
  fputs(stag_atr, xml_file);
  fputs("\"", xml_file);
  print_quoted_xml(xml_file, sval, strlen(sval));
  fputs("\" xsi:nil=\"true\" />", xml_file);
  fputs(line_end, xml_file);
  check_io(xml_file);
}


/*
  Print xml tag with many attributes.

  SYNOPSIS
    print_xml_row()
    xml_file    - output file
    row_name    - xml tag name
    tableRes    - query result
    row         - result row

  DESCRIPTION
    Print tag with many attribute to the xml_file. Format is:
      \t\t<row_name Atr1="Val1" Atr2="Val2"... />
  NOTE
    All atributes and values will be quoted before output.
*/

static void print_xml_row(FILE *xml_file, const char *row_name,
                          MYSQL_RES *tableRes, MYSQL_ROW *row)
{
  uint i;
  MYSQL_FIELD *field;
  ulong *lengths= mysql_fetch_lengths(tableRes);

  fprintf(xml_file, "\t\t<%s", row_name);
  check_io(xml_file);
  mysql_field_seek(tableRes, 0);
  for (i= 0; (field= mysql_fetch_field(tableRes)); i++)
  {
    if ((*row)[i])
    {
      fputc(' ', xml_file);
      print_quoted_xml(xml_file, field->name, field->name_length);
      fputs("=\"", xml_file);
      print_quoted_xml(xml_file, (*row)[i], lengths[i]);
      fputc('"', xml_file);
      check_io(xml_file);
    }
  }
  fputs(" />\n", xml_file);
  check_io(xml_file);
}


/*
  Print hex value for blob data.

  SYNOPSIS
    print_blob_as_hex()
    output_file         - output file
    str                 - string to print
    len                 - its length

  DESCRIPTION
    Print hex value for blob data.
*/

static void print_blob_as_hex(FILE *output_file, const char *str, ulong len)
{
    /* sakaik got the idea to to provide blob's in hex notation. */
    const char *ptr= str, *end= ptr + len;
    for (; ptr < end ; ptr++)
      fprintf(output_file, "%02X", *((uchar *)ptr));
    check_io(output_file);
}

/*
  get_table_structure -- retrievs database structure, prints out corresponding
  CREATE statement and fills out insert_pat if the table is the type we will
  be dumping.

  ARGS
    table       - table name
    db          - db name
    table_type  - table type, e.g. "MyISAM" or "InnoDB", but also "VIEW"
    ignore_flag - what we must particularly ignore - see IGNORE_ defines above

  RETURN
    number of fields in table, 0 if error
*/

static uint get_table_structure(char *table, char *db, char *table_type,
                                char *ignore_flag)
{
  my_bool    init=0, delayed, write_data, complete_insert;
  my_ulonglong num_fields;
  char       *result_table, *opt_quoted_table;
  const char *insert_option;
  char	     name_buff[NAME_LEN+3],table_buff[NAME_LEN*2+3];
  char       table_buff2[NAME_LEN*2+3], query_buff[QUERY_LENGTH];
  FILE       *sql_file= md_result_file;
  int        len;
  MYSQL_RES  *result;
  MYSQL_ROW  row;
  DBUG_ENTER("get_table_structure");
  DBUG_PRINT("enter", ("db: %s  table: %s", db, table));

  *ignore_flag= check_if_ignore_table(table, table_type);

  delayed= opt_delayed;
  if (delayed && (*ignore_flag & IGNORE_INSERT_DELAYED))
  {
    delayed= 0;
    verbose_msg("-- Warning: Unable to use delayed inserts for table '%s' "
                "because it's of type %s\n", table, table_type);
  }

  complete_insert= 0;
  if ((write_data= !(*ignore_flag & IGNORE_DATA)))
  {
    complete_insert= opt_complete_insert;
    if (!insert_pat_inited)
    {
      insert_pat_inited= 1;
      init_dynamic_string_checked(&insert_pat, "", 1024, 1024);
    }
    else
      dynstr_set_checked(&insert_pat, "");
  }

  insert_option= ((delayed && opt_ignore) ? " DELAYED IGNORE " :
                  delayed ? " DELAYED " : opt_ignore ? " IGNORE " : "");

  verbose_msg("-- Retrieving table structure for table %s...\n", table);

  len= snprintf(query_buff, sizeof(query_buff),
                "SET OPTION SQL_QUOTE_SHOW_CREATE=%d",
                (opt_quoted || opt_keywords));

  result_table=     quote_name(table, table_buff, 1);
  opt_quoted_table= quote_name(table, table_buff2, 0);

  if (opt_order_by_primary)
  {
    my_free(order_by, MYF(MY_ALLOW_ZERO_PTR));
    order_by= primary_key_fields(result_table);
  }

  if (!opt_xml && !mysql_query_with_error_report(mysql, 0, query_buff))
  {
    /* using SHOW CREATE statement */
    if (!opt_no_create_info)
    {
      /* Make an sql-file, if path was given iow. option -T was given */
      char buff[20+FN_REFLEN];
      MYSQL_FIELD *field;

      snprintf(buff, sizeof(buff), "show create table %s", result_table);

      if (switch_character_set_results(mysql, "binary") ||
          mysql_query_with_error_report(mysql, &result, buff) ||
          switch_character_set_results(mysql, default_charset))
        DBUG_RETURN(0);

      if (path)
      {
        if (!(sql_file= open_sql_file_for_table(table)))
          DBUG_RETURN(0);

        write_header(sql_file, db);
      }
      if (!opt_xml && opt_comments)
      {
      if (strcmp (table_type, "VIEW") == 0)         /* view */
        fprintf(sql_file, "\n--\n-- Temporary table structure for view %s\n--\n\n",
                result_table);
      else
        fprintf(sql_file, "\n--\n-- Table structure for table %s\n--\n\n",
                result_table);
        check_io(sql_file);
      }
      if (opt_drop)
      {
      /*
        Even if the "table" is a view, we do a DROP TABLE here.  The
        view-specific code below fills in the DROP VIEW.
       */
        fprintf(sql_file, "DROP TABLE IF EXISTS %s;\n",
                opt_quoted_table);
        check_io(sql_file);
      }

      field= mysql_fetch_field_direct(result, 0);
      if (strcmp(field->name, "View") == 0)
      {
        char *scv_buff= NULL;

        verbose_msg("-- It's a view, create dummy table for view\n");

        /* save "show create" statement for later */
        if ((row= mysql_fetch_row(result)) && (scv_buff=row[1]))
          scv_buff= my_strdup(scv_buff, MYF(0));

        mysql_free_result(result);

        /*
          Create a table with the same name as the view and with columns of
          the same name in order to satisfy views that depend on this view.
          The table will be removed when the actual view is created.

          The properties of each column, aside from the data type, are not
          preserved in this temporary table, because they are not necessary.

          This will not be necessary once we can determine dependencies
          between views and can simply dump them in the appropriate order.
        */
        snprintf(query_buff, sizeof(query_buff),
                 "SHOW FIELDS FROM %s", result_table);
        if (switch_character_set_results(mysql, "binary") ||
            mysql_query_with_error_report(mysql, &result, query_buff) ||
            switch_character_set_results(mysql, default_charset))
        {
          /*
            View references invalid or privileged table/col/fun (err 1356),
            so we cannot create a stand-in table.  Be defensive and dump
            a comment with the view's 'show create' statement. (Bug #17371)
          */

          if (mysql_errno(mysql) == ER_VIEW_INVALID)
            fprintf(sql_file, "\n-- failed on view %s: %s\n\n", result_table, scv_buff ? scv_buff : "");

          my_free(scv_buff, MYF(MY_ALLOW_ZERO_PTR));

          DBUG_RETURN(0);
        }
        else
          my_free(scv_buff, MYF(MY_ALLOW_ZERO_PTR));

        if (mysql_num_rows(result))
        {
          if (opt_drop)
          {
            /*
              We have already dropped any table of the same name above, so
              here we just drop the view.
            */

            fprintf(sql_file, "/*!50001 DROP VIEW IF EXISTS %s*/;\n",
                    opt_quoted_table);
            check_io(sql_file);
          }

          fprintf(sql_file,
                  "SET @saved_cs_client     = @@character_set_client;\n"
                  "SET character_set_client = utf8;\n"
                  "/*!50001 CREATE TABLE %s (\n",
                  result_table);

          /*
            Get first row, following loop will prepend comma - keeps from
            having to know if the row being printed is last to determine if
            there should be a _trailing_ comma.
          */

          row= mysql_fetch_row(result);

          fprintf(sql_file, "  %s %s", quote_name(row[0], name_buff, 0),
                  row[1]);

          while((row= mysql_fetch_row(result)))
          {
            /* col name, col type */
            fprintf(sql_file, ",\n  %s %s",
                    quote_name(row[0], name_buff, 0), row[1]);
          }
          fprintf(sql_file,
                  "\n) */;\n"
                  "SET character_set_client = @saved_cs_client;\n");

          check_io(sql_file);
        }

        mysql_free_result(result);

        if (path)
          my_fclose(sql_file, MYF(MY_WME));

        seen_views= 1;
        DBUG_RETURN(0);
      }

      row= mysql_fetch_row(result);

      fprintf(sql_file,
              "SET @saved_cs_client     = @@character_set_client;\n"
              "SET character_set_client = utf8;\n"
              "%s;\n"
              "SET character_set_client = @saved_cs_client;\n",
              row[1]);

      check_io(sql_file);
      mysql_free_result(result);
    }
    snprintf(query_buff, sizeof(query_buff), "show fields from %s",
             result_table);
    if (mysql_query_with_error_report(mysql, &result, query_buff))
    {
      if (path)
        my_fclose(sql_file, MYF(MY_WME));
      DBUG_RETURN(0);
    }

    /*
      If write_data is true, then we build up insert statements for
      the table's data. Note: in subsequent lines of code, this test
      will have to be performed each time we are appending to
      insert_pat.
    */
    if (write_data)
    {
      if (opt_replace_into)
        dynstr_append_checked(&insert_pat, "REPLACE ");
      else
        dynstr_append_checked(&insert_pat, "INSERT ");
      dynstr_append_checked(&insert_pat, insert_option);
      dynstr_append_checked(&insert_pat, "INTO ");
      dynstr_append_checked(&insert_pat, opt_quoted_table);
      if (complete_insert)
      {
        dynstr_append_checked(&insert_pat, " (");
      }
      else
      {
        dynstr_append_checked(&insert_pat, " VALUES ");
        if (!extended_insert)
          dynstr_append_checked(&insert_pat, "(");
      }
    }

    while ((row= mysql_fetch_row(result)))
    {
      if (complete_insert)
      {
        if (init)
        {
          dynstr_append_checked(&insert_pat, ", ");
        }
        init=1;
        dynstr_append_checked(&insert_pat,
                      quote_name(row[SHOW_FIELDNAME], name_buff, 0));
      }
    }
    num_fields= mysql_num_rows(result);
    mysql_free_result(result);
  }
  else
  {
    verbose_msg("%s: Warning: Can't set SQL_QUOTE_SHOW_CREATE option (%s)\n",
                my_progname, mysql_error(mysql));

    snprintf(query_buff, sizeof(query_buff), "show fields from %s",
             result_table);
    if (mysql_query_with_error_report(mysql, &result, query_buff))
      DBUG_RETURN(0);

    /* Make an sql-file, if path was given iow. option -T was given */
    if (!opt_no_create_info)
    {
      if (path)
      {
        if (!(sql_file= open_sql_file_for_table(table)))
          DBUG_RETURN(0);
        write_header(sql_file, db);
      }
      if (!opt_xml && opt_comments)
        fprintf(sql_file, "\n--\n-- Table structure for table %s\n--\n\n",
                result_table);
      if (opt_drop)
        fprintf(sql_file, "DROP TABLE IF EXISTS %s;\n", result_table);
      if (!opt_xml)
        fprintf(sql_file, "CREATE TABLE %s (\n", result_table);
      else
        print_xml_tag(sql_file, "\t", "\n", "table_structure", "name=", table, 
                NullS);
      check_io(sql_file);
    }

    if (write_data)
    {
      if (opt_replace_into)
        dynstr_append_checked(&insert_pat, "REPLACE ");
      else
        dynstr_append_checked(&insert_pat, "INSERT ");
      dynstr_append_checked(&insert_pat, insert_option);
      dynstr_append_checked(&insert_pat, "INTO ");
      dynstr_append_checked(&insert_pat, result_table);
      if (complete_insert)
        dynstr_append_checked(&insert_pat, " (");
      else
      {
        dynstr_append_checked(&insert_pat, " VALUES ");
        if (!extended_insert)
          dynstr_append_checked(&insert_pat, "(");
      }
    }

    while ((row= mysql_fetch_row(result)))
    {
      ulong *lengths= mysql_fetch_lengths(result);
      if (init)
      {
        if (!opt_xml && !opt_no_create_info)
        {
          fputs(",\n",sql_file);
          check_io(sql_file);
        }
        if (complete_insert)
          dynstr_append_checked(&insert_pat, ", ");
      }
      init=1;
      if (complete_insert)
        dynstr_append_checked(&insert_pat,
                      quote_name(row[SHOW_FIELDNAME], name_buff, 0));
      if (!opt_no_create_info)
      {
        if (opt_xml)
        {
          print_xml_row(sql_file, "field", result, &row);
          continue;
        }

        if (opt_keywords)
          fprintf(sql_file, "  %s.%s %s", result_table,
                  quote_name(row[SHOW_FIELDNAME],name_buff, 0),
                  row[SHOW_TYPE]);
        else
          fprintf(sql_file, "  %s %s", quote_name(row[SHOW_FIELDNAME],
                                                  name_buff, 0),
                  row[SHOW_TYPE]);
        if (row[SHOW_DEFAULT])
        {
          fputs(" DEFAULT ", sql_file);
          unescape(sql_file, row[SHOW_DEFAULT], lengths[SHOW_DEFAULT]);
        }
        if (!row[SHOW_NULL][0])
          fputs(" NOT NULL", sql_file);
        if (row[SHOW_EXTRA][0])
          fprintf(sql_file, " %s",row[SHOW_EXTRA]);
        check_io(sql_file);
      }
    }
    num_fields= mysql_num_rows(result);
    mysql_free_result(result);
    if (!opt_no_create_info)
    {
      /* Make an sql-file, if path was given iow. option -T was given */
      char buff[20+FN_REFLEN];
      uint keynr,primary_key;
      snprintf(buff, sizeof(buff), "show keys from %s", result_table);
      if (mysql_query_with_error_report(mysql, &result, buff))
      {
        if (mysql_errno(mysql) == ER_WRONG_OBJECT)
        {
          /* it is VIEW */
          fputs("\t\t<options Comment=\"view\" />\n", sql_file);
          goto continue_xml;
        }
        fprintf(stderr, "%s: Can't get keys for table %s (%s)\n",
                my_progname, result_table, mysql_error(mysql));
        if (path)
          my_fclose(sql_file, MYF(MY_WME));
        DBUG_RETURN(0);
      }

      /* Find first which key is primary key */
      keynr=0;
      primary_key=INT_MAX;
      while ((row= mysql_fetch_row(result)))
      {
        if (atoi(row[3]) == 1)
        {
          keynr++;
#ifdef FORCE_PRIMARY_KEY
          if (atoi(row[1]) == 0 && primary_key == INT_MAX)
            primary_key=keynr;
#endif
          if (!strcmp(row[2],"PRIMARY"))
          {
            primary_key=keynr;
            break;
          }
        }
      }
      mysql_data_seek(result,0);
      keynr=0;
      while ((row= mysql_fetch_row(result)))
      {
        if (opt_xml)
        {
          print_xml_row(sql_file, "key", result, &row);
          continue;
        }

        if (atoi(row[3]) == 1)
        {
          if (keynr++)
            putc(')', sql_file);
          if (atoi(row[1]))       /* Test if duplicate key */
            /* Duplicate allowed */
            fprintf(sql_file, ",\n  KEY %s (",quote_name(row[2],name_buff,0));
          else if (keynr == primary_key)
            fputs(",\n  PRIMARY KEY (",sql_file); /* First UNIQUE is primary */
          else
            fprintf(sql_file, ",\n  UNIQUE %s (",quote_name(row[2],name_buff,
                                                            0));
        }
        else
          putc(',', sql_file);
        fputs(quote_name(row[4], name_buff, 0), sql_file);
        if (row[7])
          fprintf(sql_file, " (%s)",row[7]);      /* Sub key */
        check_io(sql_file);
      }
      mysql_free_result(result);
      if (!opt_xml)
      {
        if (keynr)
          putc(')', sql_file);
        fputs("\n)",sql_file);
        check_io(sql_file);
      }

      /* Get MySQL specific create options */
      if (create_options)
      {
        char show_name_buff[NAME_LEN*2+2+24];

        /* Check memory for quote_for_like() */
        snprintf(buff, sizeof(buff), "show table status like %s",
                 quote_for_like(table, show_name_buff));

        if (mysql_query_with_error_report(mysql, &result, buff))
        {
          if (mysql_errno(mysql) != ER_PARSE_ERROR)
          {                                     /* If old MySQL version */
            verbose_msg("-- Warning: Couldn't get status information for " \
                        "table %s (%s)\n", result_table,mysql_error(mysql));
          }
        }
        else if (!(row= mysql_fetch_row(result)))
        {
          fprintf(stderr,
                  "Error: Couldn't read status information for table %s (%s)\n",
                  result_table,mysql_error(mysql));
        }
        else
        {
          if (opt_xml)
            print_xml_row(sql_file, "options", result, &row);
          else
          {
            fputs("/*!",sql_file);
            print_value(sql_file,result,row,"engine=","Engine",0);
            print_value(sql_file,result,row,"","Create_options",0);
            print_value(sql_file,result,row,"comment=","Comment",1);
            fputs(" */",sql_file);
            check_io(sql_file);
          }
        }
        mysql_free_result(result);              /* Is always safe to free */
      }
continue_xml:
      if (!opt_xml)
        fputs(";\n", sql_file);
      else
        fputs("\t</table_structure>\n", sql_file);
      check_io(sql_file);
    }
  }
  if (complete_insert)
  {
    dynstr_append_checked(&insert_pat, ") VALUES ");
    if (!extended_insert)
      dynstr_append_checked(&insert_pat, "(");
  }
  if (sql_file != md_result_file)
  {
    fputs("\n", sql_file);
    write_footer(sql_file);
    my_fclose(sql_file, MYF(MY_WME));
  }
  DBUG_RETURN((uint) num_fields);
} /* get_table_structure */

static void add_load_option(DYNAMIC_STRING *str, const char *option,
                             const char *option_value)
{
  if (!option_value)
  {
    /* Null value means we don't add this option. */
    return;
  }

  dynstr_append_checked(str, option);
  
  if (strncmp(option_value, "0x", sizeof("0x")-1) == 0)
  {
    /* It's a hex constant, don't escape */
    dynstr_append_checked(str, option_value);
  }
  else
  {
    /* char constant; escape */
    field_escape(str, option_value);
  }
}


/*
  Allow the user to specify field terminator strings like:
  "'", "\", "\\" (escaped backslash), "\t" (tab), "\n" (newline)
  This is done by doubling ' and add a end -\ if needed to avoid
  syntax errors from the SQL parser.
*/

static void field_escape(DYNAMIC_STRING* in, const char *from)
{
  uint end_backslashes= 0; 

  dynstr_append_checked(in, "'");

  while (*from)
  {
    dynstr_append_mem_checked(in, from, 1);

    if (*from == '\\')
      end_backslashes^=1;    /* find odd number of backslashes */
    else
    {
      if (*from == '\'' && !end_backslashes)
      {
        /* We want a duplicate of "'" for MySQL */
        dynstr_append_checked(in, "\'");
      }
      end_backslashes=0;
    }
    from++;
  }
  /* Add missing backslashes if user has specified odd number of backs.*/
  if (end_backslashes)
    dynstr_append_checked(in, "\\");
  
  dynstr_append_checked(in, "'");
}



/*

 SYNOPSIS
  dump_table()

  dump_table saves database contents as a series of INSERT statements.

  ARGS
   table - table name
   db    - db name

   RETURNS
    void
*/


static void dump_table(char *table, char *db)
{
  char ignore_flag;
  char buf[200], table_buff[NAME_LEN+3];
  DYNAMIC_STRING query_string;
  char table_type[NAME_LEN];
  char *result_table, table_buff2[NAME_LEN*2+3], *opt_quoted_table;
  int error= 0;
  ulong         rownr, row_break, total_length, init_length;
  uint num_fields;
  MYSQL_RES     *res;
  MYSQL_FIELD   *field;
  MYSQL_ROW     row;
  DBUG_ENTER("dump_table");

  /*
    Make sure you get the create table info before the following check for
    --no-data flag below. Otherwise, the create table info won't be printed.
  */
  num_fields= get_table_structure(table, db, table_type, &ignore_flag);

  /*
    The "table" could be a view.  If so, we don't do anything here.
  */
  if (strcmp(table_type, "VIEW") == 0)
    DBUG_VOID_RETURN;

  /* Check --no-data flag */
  if (opt_no_data)
  {
    verbose_msg("-- Skipping dump data for table '%s', --no-data was used\n",
                table);
    DBUG_VOID_RETURN;
  }

  DBUG_PRINT("info",
             ("ignore_flag: %x  num_fields: %d", (int) ignore_flag,
              num_fields));
  /*
    If the table type is a merge table or any type that has to be
     _completely_ ignored and no data dumped
  */
  if (ignore_flag & IGNORE_DATA)
  {
    verbose_msg("-- Warning: Skipping data for table '%s' because " \
                "it's of type %s\n", table, table_type);
    DBUG_VOID_RETURN;
  }
  /* Check that there are any fields in the table */
  if (num_fields == 0)
  {
    verbose_msg("-- Skipping dump data for table '%s', it has no fields\n",
                table);
    DBUG_VOID_RETURN;
  }

  /*
     Check --skip-events flag: it is not enough to skip creation of events
     discarding SHOW CREATE EVENT statements generation. The myslq.event
     table data should be skipped too.
  */
  if (!opt_events && !my_strcasecmp(&my_charset_latin1, db, "mysql") &&
      !my_strcasecmp(&my_charset_latin1, table, "event"))
  {
    verbose_msg("-- Skipping data table mysql.event, --skip-events was used\n");
    DBUG_VOID_RETURN;
  }

  result_table= quote_name(table,table_buff, 1);
  opt_quoted_table= quote_name(table, table_buff2, 0);

  verbose_msg("-- Sending SELECT query...\n");

  init_dynamic_string_checked(&query_string, "", 1024, 1024);

  if (path)
  {
    char filename[FN_REFLEN], tmp_path[FN_REFLEN];

    /*
      Convert the path to native os format
      and resolve to the full filepath.
    */
    convert_dirname(tmp_path,path,NullS);    
    my_load_path(tmp_path, tmp_path, NULL);
    fn_format(filename, table, tmp_path, ".txt", MYF(MY_UNPACK_FILENAME));

    /* Must delete the file that 'INTO OUTFILE' will write to */
    my_delete(filename, MYF(0));

    /* convert to a unix path name to stick into the query */
    to_unix_path(filename);

    /* now build the query string */

    dynstr_append_checked(&query_string, "SELECT * INTO OUTFILE '");
    dynstr_append_checked(&query_string, filename);
    dynstr_append_checked(&query_string, "'");

    if (fields_terminated || enclosed || opt_enclosed || escaped)
      dynstr_append_checked(&query_string, " FIELDS");
    
    add_load_option(&query_string, " TERMINATED BY ", fields_terminated);
    add_load_option(&query_string, " ENCLOSED BY ", enclosed);
    add_load_option(&query_string, " OPTIONALLY ENCLOSED BY ", opt_enclosed);
    add_load_option(&query_string, " ESCAPED BY ", escaped);
    add_load_option(&query_string, " LINES TERMINATED BY ", lines_terminated);

    dynstr_append_checked(&query_string, " FROM ");
    dynstr_append_checked(&query_string, result_table);

    if (where)
    {
      dynstr_append_checked(&query_string, " WHERE ");
      dynstr_append_checked(&query_string, where);
    }

    if (order_by)
    {
      dynstr_append_checked(&query_string, " ORDER BY ");
      dynstr_append_checked(&query_string, order_by);
    }

    if (mysql_real_query(mysql, query_string.str, query_string.length))
    {
      DB_error(mysql, "when executing 'SELECT INTO OUTFILE'");
      dynstr_free(&query_string);
      DBUG_VOID_RETURN;
    }
  }
  else
  {
    if (!opt_xml && opt_comments)
    {
      fprintf(md_result_file,"\n--\n-- Dumping data for table %s\n--\n",
              result_table);
      check_io(md_result_file);
    }
    
    dynstr_append_checked(&query_string, "SELECT * FROM ");
    dynstr_append_checked(&query_string, result_table);

    if (where)
    {
      if (!opt_xml && opt_comments)
      {
        fprintf(md_result_file, "-- WHERE:  %s\n", where);
        check_io(md_result_file);
      }
      
      dynstr_append_checked(&query_string, " WHERE ");
      dynstr_append_checked(&query_string, where);
    }
    if (order_by)
    {
      if (!opt_xml && opt_comments)
      {
        fprintf(md_result_file, "-- ORDER BY:  %s\n", order_by);
        check_io(md_result_file);
      }
      dynstr_append_checked(&query_string, " ORDER BY ");
      dynstr_append_checked(&query_string, order_by);
    }

    if (!opt_xml && !opt_compact)
    {
      fputs("\n", md_result_file);
      check_io(md_result_file);
    }
    if (mysql_query_with_error_report(mysql, 0, query_string.str))
    {
      DB_error(mysql, "when retrieving data from server");
      goto err;
    }
    if (quick)
      res=mysql_use_result(mysql);
    else
      res=mysql_store_result(mysql);
    if (!res)
    {
      DB_error(mysql, "when retrieving data from server");
      goto err;
    }

    verbose_msg("-- Retrieving rows...\n");
    if (mysql_num_fields(res) != num_fields)
    {
      fprintf(stderr,"%s: Error in field count for table: %s !  Aborting.\n",
              my_progname, result_table);
      error= EX_CONSCHECK;
      goto err;
    }

    if (opt_lock)
    {
      fprintf(md_result_file,"LOCK TABLES %s WRITE;\n", opt_quoted_table);
      check_io(md_result_file);
    }
    /* Moved disable keys to after lock per bug 15977 */
    if (opt_disable_keys)
    {
      fprintf(md_result_file, "/*!40000 ALTER TABLE %s DISABLE KEYS */;\n",
	      opt_quoted_table);
      check_io(md_result_file);
    }

    total_length= opt_net_buffer_length;                /* Force row break */
    row_break=0;
    rownr=0;
    init_length=(uint) insert_pat.length+4;
    if (opt_xml)
      print_xml_tag(md_result_file, "\t", "\n", "table_data", "name=", table,
              NullS);
    if (opt_autocommit)
    {
      fprintf(md_result_file, "set autocommit=0;\n");
      check_io(md_result_file);
    }

    while ((row= mysql_fetch_row(res)))
    {
      uint i;
      ulong *lengths= mysql_fetch_lengths(res);
      rownr++;
      if (!extended_insert && !opt_xml)
      {
        fputs(insert_pat.str,md_result_file);
        check_io(md_result_file);
      }
      mysql_field_seek(res,0);

      if (opt_xml)
      {
        fputs("\t<row>\n", md_result_file);
        check_io(md_result_file);
      }

      for (i= 0; i < mysql_num_fields(res); i++)
      {
        int is_blob;
        ulong length= lengths[i];

        if (!(field= mysql_fetch_field(res)))
          die(EX_CONSCHECK,
                      "Not enough fields from table %s! Aborting.\n",
                      result_table);

        /*
           63 is my_charset_bin. If charsetnr is not 63,
           we have not a BLOB but a TEXT column.
           we'll dump in hex only BLOB columns.
        */
        is_blob= (opt_hex_blob && field->charsetnr == 63 &&
                  (field->type == MYSQL_TYPE_STRING ||
                   field->type == MYSQL_TYPE_VARCHAR ||
                   field->type == MYSQL_TYPE_BLOB ||
                   field->type == MYSQL_TYPE_LONG_BLOB ||
                   field->type == MYSQL_TYPE_MEDIUM_BLOB ||
                   field->type == MYSQL_TYPE_TINY_BLOB)) ? 1 : 0;
        if (extended_insert && !opt_xml)
        {
          if (i == 0)
            dynstr_set_checked(&extended_row,"(");
          else
            dynstr_append_checked(&extended_row,",");

          if (row[i])
          {
            if (length)
            {
              if (!IS_NUM_FIELD(field))
              {
                /*
                  "length * 2 + 2" is OK for both HEX and non-HEX modes:
                  - In HEX mode we need exactly 2 bytes per character
                  plus 2 bytes for '0x' prefix.
                  - In non-HEX mode we need up to 2 bytes per character,
                  plus 2 bytes for leading and trailing '\'' characters.
                  Also we need to reserve 1 byte for terminating '\0'.
                */
                dynstr_realloc_checked(&extended_row,length * 2 + 2 + 1);
                if (opt_hex_blob && is_blob)
                {
                  dynstr_append_checked(&extended_row, "0x");
                  extended_row.length+= mysql_hex_string(extended_row.str +
                                                         extended_row.length,
                                                         row[i], length);
                  DBUG_ASSERT(extended_row.length+1 <= extended_row.max_length);
                  /* mysql_hex_string() already terminated string by '\0' */
                  DBUG_ASSERT(extended_row.str[extended_row.length] == '\0');
                }
                else
                {
                  dynstr_append_checked(&extended_row,"'");
                  extended_row.length +=
                  mysql_real_escape_string(&mysql_connection,
                                           &extended_row.str[extended_row.length],
                                           row[i],length);
                  extended_row.str[extended_row.length]='\0';
                  dynstr_append_checked(&extended_row,"'");
                }
              }
              else
              {
                /* change any strings ("inf", "-inf", "nan") into NULL */
                char *ptr= row[i];
                if (my_isalpha(charset_info, *ptr) || (*ptr == '-' &&
                    my_isalpha(charset_info, ptr[1])))
                  dynstr_append_checked(&extended_row, "NULL");
                else
                {
                  dynstr_append_checked(&extended_row, ptr);
                }
              }
            }
            else
              dynstr_append_checked(&extended_row,"''");
          }
          else
            dynstr_append_checked(&extended_row,"NULL");
        }
        else
        {
          if (i && !opt_xml)
          {
            fputc(',', md_result_file);
            check_io(md_result_file);
          }
          if (row[i])
          {
            if (!IS_NUM_FIELD(field))
            {
              if (opt_xml)
              {
                if (opt_hex_blob && is_blob && length)
                {
                  /* Define xsi:type="xs:hexBinary" for hex encoded data */
                  print_xml_tag(md_result_file, "\t\t", "", "field", "name=",
                                field->name, "xsi:type=", "xs:hexBinary", NullS);
                  print_blob_as_hex(md_result_file, row[i], length);
                }
                else
                {
                  print_xml_tag(md_result_file, "\t\t", "", "field", "name=", 
                                field->name, NullS);
                  print_quoted_xml(md_result_file, row[i], length);
                }
                fputs("</field>\n", md_result_file);
              }
              else if (opt_hex_blob && is_blob && length)
              {
                fputs("0x", md_result_file);
                print_blob_as_hex(md_result_file, row[i], length);
              }
              else
                unescape(md_result_file, row[i], length);
            }
            else
            {
              /* change any strings ("inf", "-inf", "nan") into NULL */
              char *ptr= row[i];
              if (opt_xml)
              {
                print_xml_tag(md_result_file, "\t\t", "", "field", "name=",
                        field->name, NullS);
                fputs(!my_isalpha(charset_info, *ptr) ? ptr: "NULL",
                      md_result_file);
                fputs("</field>\n", md_result_file);
              }
              else if (my_isalpha(charset_info, *ptr) ||
                       (*ptr == '-' && my_isalpha(charset_info, ptr[1])))
                fputs("NULL", md_result_file);
              else
                fputs(ptr, md_result_file);
            }
          }
          else
          {
            /* The field value is NULL */
            if (!opt_xml)
              fputs("NULL", md_result_file);
            else
              print_xml_null_tag(md_result_file, "\t\t", "field name=",
                                 field->name, "\n");
          }
          check_io(md_result_file);
        }
      }

      if (opt_xml)
      {
        fputs("\t</row>\n", md_result_file);
        check_io(md_result_file);
      }

      if (extended_insert)
      {
        ulong row_length;
        dynstr_append_checked(&extended_row,")");
        row_length= 2 + extended_row.length;
        if (total_length + row_length < opt_net_buffer_length)
        {
          total_length+= row_length;
          fputc(',',md_result_file);            /* Always row break */
          fputs(extended_row.str,md_result_file);
        }
        else
        {
          if (row_break)
            fputs(";\n", md_result_file);
          row_break=1;                          /* This is first row */

          fputs(insert_pat.str,md_result_file);
          fputs(extended_row.str,md_result_file);
          total_length= row_length+init_length;
        }
        check_io(md_result_file);
      }
      else if (!opt_xml)
      {
        fputs(");\n", md_result_file);
        check_io(md_result_file);
      }
    }

    /* XML - close table tag and supress regular output */
    if (opt_xml)
        fputs("\t</table_data>\n", md_result_file);
    else if (extended_insert && row_break)
      fputs(";\n", md_result_file);             /* If not empty table */
    fflush(md_result_file);
    check_io(md_result_file);
    if (mysql_errno(mysql))
    {
      snprintf(buf, sizeof(buf),
               "%s: Error %d: %s when dumping table %s at row: %ld\n",
               my_progname,
               mysql_errno(mysql),
               mysql_error(mysql),
               result_table,
               rownr);
      fputs(buf,stderr);
      error= EX_CONSCHECK;
      goto err;
    }

    /* Moved enable keys to before unlock per bug 15977 */
    if (opt_disable_keys)
    {
      fprintf(md_result_file,"/*!40000 ALTER TABLE %s ENABLE KEYS */;\n",
              opt_quoted_table);
      check_io(md_result_file);
    }
    if (opt_lock)
    {
      fputs("UNLOCK TABLES;\n", md_result_file);
      check_io(md_result_file);
    }
    if (opt_autocommit)
    {
      fprintf(md_result_file, "commit;\n");
      check_io(md_result_file);
    }
    mysql_free_result(res);
  }
  dynstr_free(&query_string);
  DBUG_VOID_RETURN;

err:
  dynstr_free(&query_string);
  maybe_exit(error);
  DBUG_VOID_RETURN;
} /* dump_table */


static char *getTableName(int reset)
{
  static MYSQL_RES *res= NULL;
  MYSQL_ROW    row;

  if (!res)
  {
    if (!(res= mysql_list_tables(mysql,NullS)))
      return(NULL);
  }
  if ((row= mysql_fetch_row(res)))
    return((char*) row[0]);

  if (reset)
    mysql_data_seek(res,0);      /* We want to read again */
  else
  {
    mysql_free_result(res);
    res= NULL;
  }
  return(NULL);
} /* getTableName */


static int dump_all_databases()
{
  MYSQL_ROW row;
  MYSQL_RES *tableres;
  int result=0;

  if (mysql_query_with_error_report(mysql, &tableres, "SHOW DATABASES"))
    return 1;
  while ((row= mysql_fetch_row(tableres)))
  {
    if (dump_all_tables_in_db(row[0]))
      result=1;
  }
  return result;
}
/* dump_all_databases */


static int dump_databases(char **db_names)
{
  int result=0;
  char **db;
  DBUG_ENTER("dump_databases");

  for (db= db_names ; *db ; db++)
  {
    if (dump_all_tables_in_db(*db))
      result=1;
  }
  DBUG_RETURN(result);
} /* dump_databases */


/*
Table Specific database initalization.

SYNOPSIS
  init_dumping_tables
  qdatabase      quoted name of the database

RETURN VALUES
  0        Success.
  1        Failure.
*/

int init_dumping_tables(char *qdatabase)
{
  DBUG_ENTER("init_dumping_tables");

  if (!opt_create_db)
  {
    char qbuf[256];
    MYSQL_ROW row;
    MYSQL_RES *dbinfo;

    snprintf(qbuf, sizeof(qbuf),
             "SHOW CREATE DATABASE IF NOT EXISTS %s",
             qdatabase);

    if (mysql_query(mysql, qbuf) || !(dbinfo = mysql_store_result(mysql)))
    {
      /* Old server version, dump generic CREATE DATABASE */
      if (opt_drop_database)
        fprintf(md_result_file,
                "\n/*!40000 DROP DATABASE IF EXISTS %s*/;\n",
                qdatabase);
      fprintf(md_result_file,
              "\nCREATE DATABASE /*!32312 IF NOT EXISTS*/ %s;\n",
              qdatabase);
    }
    else
    {
      if (opt_drop_database)
        fprintf(md_result_file,
                "\n/*!40000 DROP DATABASE IF EXISTS %s*/;\n",
                qdatabase);
      row = mysql_fetch_row(dbinfo);
      if (row[1])
      {
        fprintf(md_result_file,"\n%s;\n",row[1]);
      }
      mysql_free_result(dbinfo);
    }
  }
  DBUG_RETURN(0);
} /* init_dumping_tables */


static int init_dumping(char *database, int init_func(char*))
{
  if (mysql_get_server_version(mysql) >= 50003 &&
      !my_strcasecmp(&my_charset_latin1, database, "information_schema"))
    return 1;

  if (mysql_select_db(mysql, database))
  {
    DB_error(mysql, "when selecting the database");
    return 1;                   /* If --force */
  }
  if (!path && !opt_xml)
  {
    if (opt_databases || opt_alldbs)
    {
      /*
        length of table name * 2 (if name contains quotes), 2 quotes and 0
      */
      char quoted_database_buf[NAME_LEN*2+3];
      char *qdatabase= quote_name(database,quoted_database_buf,opt_quoted);
      if (opt_comments)
      {
        fprintf(md_result_file,"\n--\n-- Current Database: %s\n--\n", qdatabase);
        check_io(md_result_file);
      }

      /* Call the view or table specific function */
      init_func(qdatabase);

      fprintf(md_result_file,"\nUSE %s;\n", qdatabase);
      check_io(md_result_file);
    }
  }
  if (extended_insert)
    init_dynamic_string_checked(&extended_row, "", 1024, 1024);
  return 0;
} /* init_dumping */


/* Return 1 if we should copy the table */

static my_bool include_table(const uchar *hash_key, size_t len)
{
  return !hash_search(&ignore_table, hash_key, len);
}


static int dump_all_tables_in_db(char *database)
{
  char *table;
  uint numrows;
  char table_buff[NAME_LEN*2+3];
  char hash_key[2*NAME_LEN+2];  /* "db.tablename" */
  char *afterdot;
  int using_mysql_db= my_strcasecmp(&my_charset_latin1, database, "mysql");
  DBUG_ENTER("dump_all_tables_in_db");

  afterdot= strmov(hash_key, database);
  *afterdot++= '.';

  if (init_dumping(database, init_dumping_tables))
    DBUG_RETURN(1);
  if (opt_xml)
    print_xml_tag(md_result_file, "", "\n", "database", "name=", database, NullS);
  if (lock_tables)
  {
    DYNAMIC_STRING query;
    init_dynamic_string_checked(&query, "LOCK TABLES ", 256, 1024);
    for (numrows= 0 ; (table= getTableName(1)) ; )
    {
      char *end= strmov(afterdot, table);
      if (include_table((uchar*) hash_key,end - hash_key))
      {
        numrows++;
        dynstr_append_checked(&query, quote_name(table, table_buff, 1));
        dynstr_append_checked(&query, " READ /*!32311 LOCAL */,");
      }
    }
    if (numrows && mysql_real_query(mysql, query.str, query.length-1))
      DB_error(mysql, "when using LOCK TABLES");
            /* We shall continue here, if --force was given */
    dynstr_free(&query);
  }
  if (flush_logs)
  {
    if (mysql_refresh(mysql, REFRESH_LOG))
      DB_error(mysql, "when doing refresh");
           /* We shall continue here, if --force was given */
  }
  while ((table= getTableName(0)))
  {
    char *end= strmov(afterdot, table);
    if (include_table((uchar*) hash_key, end - hash_key))
    {
      dump_table(table,database);
      my_free(order_by, MYF(MY_ALLOW_ZERO_PTR));
      order_by= 0;
    }
  }
  if (opt_xml)
  {
    fputs("</database>\n", md_result_file);
    check_io(md_result_file);
  }
  if (lock_tables)
    VOID(mysql_query_with_error_report(mysql, 0, "UNLOCK TABLES"));
  if (flush_privileges && using_mysql_db == 0)
  {
    fprintf(md_result_file,"\n--\n-- Flush Grant Tables \n--\n");
    fprintf(md_result_file,"\n/*! FLUSH PRIVILEGES */;\n");
  }
  DBUG_RETURN(0);
} /* dump_all_tables_in_db */


/*
  get_actual_table_name -- executes a SHOW TABLES LIKE '%s' to get the actual
  table name from the server for the table name given on the command line.
  we do this because the table name given on the command line may be a
  different case (e.g.  T1 vs t1)

  RETURN
    pointer to the table name
    0 if error
*/

static char *get_actual_table_name(const char *old_table_name, MEM_ROOT *root)
{
  char *name= 0;
  MYSQL_RES  *table_res;
  MYSQL_ROW  row;
  char query[50 + 2*NAME_LEN];
  char show_name_buff[FN_REFLEN];
  DBUG_ENTER("get_actual_table_name");

  /* Check memory for quote_for_like() */
  DBUG_ASSERT(2*sizeof(old_table_name) < sizeof(show_name_buff));
  snprintf(query, sizeof(query), "SHOW TABLES LIKE %s",
           quote_for_like(old_table_name, show_name_buff));

  if (mysql_query_with_error_report(mysql, 0, query))
    return NullS;

  if ((table_res= mysql_store_result(mysql)))
  {
    my_ulonglong num_rows= mysql_num_rows(table_res);
    if (num_rows > 0)
    {
      ulong *lengths;
      /*
        Return first row
        TODO: Return all matching rows
      */
      row= mysql_fetch_row(table_res);
      lengths= mysql_fetch_lengths(table_res);
      name= strmake_root(root, row[0], lengths[0]);
    }
    mysql_free_result(table_res);
  }
  DBUG_PRINT("exit", ("new_table_name: %s", name));
  DBUG_RETURN(name);
}


static int dump_selected_tables(char *db, char **table_names, int tables)
{
  char table_buff[NAME_LEN*2+3];
  DYNAMIC_STRING lock_tables_query;
  MEM_ROOT root;
  char **dump_tables, **pos, **end;
  DBUG_ENTER("dump_selected_tables");

  if (init_dumping(db, init_dumping_tables))
    DBUG_RETURN(1);

  init_alloc_root(&root, 8192, 0);
  if (!(dump_tables= pos= (char**) alloc_root(&root, tables * sizeof(char *))))
     die(EX_EOM, "alloc_root failure.");

  init_dynamic_string_checked(&lock_tables_query, "LOCK TABLES ", 256, 1024);
  for (; tables > 0 ; tables-- , table_names++)
  {
    /* the table name passed on commandline may be wrong case */
    if ((*pos= get_actual_table_name(*table_names, &root)))
    {
      /* Add found table name to lock_tables_query */
      if (lock_tables)
      {
        dynstr_append_checked(&lock_tables_query, quote_name(*pos, table_buff, 1));
        dynstr_append_checked(&lock_tables_query, " READ /*!32311 LOCAL */,");
      }
      pos++;
    }
    else
    {
      if (!ignore_errors)
      {
        dynstr_free(&lock_tables_query);
        free_root(&root, MYF(0));
      }
      maybe_die(EX_ILLEGAL_TABLE, "Couldn't find table: \"%s\"", *table_names);
      /* We shall countinue here, if --force was given */
    }
  }
  end= pos;

  if (lock_tables)
  {
    if (mysql_real_query(mysql, lock_tables_query.str,
                         lock_tables_query.length-1))
    {
      if (!ignore_errors)
      {
        dynstr_free(&lock_tables_query);
        free_root(&root, MYF(0));
      }
      DB_error(mysql, "when doing LOCK TABLES");
       /* We shall countinue here, if --force was given */
    }
  }
  dynstr_free(&lock_tables_query);
  if (flush_logs)
  {
    if (mysql_refresh(mysql, REFRESH_LOG))
    {
      if (!ignore_errors)
        free_root(&root, MYF(0));
      DB_error(mysql, "when doing refresh");
    }
     /* We shall countinue here, if --force was given */
  }
  if (opt_xml)
    print_xml_tag(md_result_file, "", "\n", "database", "name=", db, NullS);

  /* Dump each selected table */
  for (pos= dump_tables; pos < end; pos++)
  {
    DBUG_PRINT("info",("Dumping table %s", *pos));
    dump_table(*pos, db);
  }

  free_root(&root, MYF(0));
  my_free(order_by, MYF(MY_ALLOW_ZERO_PTR));
  order_by= 0;
  if (opt_xml)
  {
    fputs("</database>\n", md_result_file);
    check_io(md_result_file);
  }
  if (lock_tables)
    VOID(mysql_query_with_error_report(mysql, 0, "UNLOCK TABLES"));
  DBUG_RETURN(0);
} /* dump_selected_tables */


static int do_show_master_status(MYSQL *mysql_con)
{
  MYSQL_ROW row;
  MYSQL_RES *master;
  const char *comment_prefix=
    (opt_master_data == MYSQL_OPT_MASTER_DATA_COMMENTED_SQL) ? "-- " : "";
  if (mysql_query_with_error_report(mysql_con, &master, "SHOW MASTER STATUS"))
  {
    return 1;
  }
  else
  {
    row= mysql_fetch_row(master);
    if (row && row[0] && row[1])
    {
      /* SHOW MASTER STATUS reports file and position */
      if (opt_comments)
        fprintf(md_result_file,
                "\n--\n-- Position to start replication or point-in-time "
                "recovery from\n--\n\n");
      fprintf(md_result_file,
              "%sCHANGE MASTER TO MASTER_LOG_FILE='%s', MASTER_LOG_POS=%s;\n",
              comment_prefix, row[0], row[1]);
      check_io(md_result_file);
    }
    else if (!ignore_errors)
    {
      /* SHOW MASTER STATUS reports nothing and --force is not enabled */
      my_printf_error(0, "Error: Binlogging on server not active",
                      MYF(0));
      mysql_free_result(master);
      maybe_exit(EX_MYSQLERR);
      return 1;
    }
    mysql_free_result(master);
  }
  return 0;
}

static int do_stop_slave_sql(MYSQL *mysql_con)
{
  MYSQL_RES *slave;
  /* We need to check if the slave sql is running in the first place */
  if (mysql_query_with_error_report(mysql_con, &slave, "SHOW SLAVE STATUS"))
    return(1);
  else
  {
    MYSQL_ROW row= mysql_fetch_row(slave);
    if (row && row[11])
    {
      /* if SLAVE SQL is not running, we don't stop it */
      if (!strcmp(row[11],"No"))
      {
        mysql_free_result(slave);
        /* Silently assume that they don't have the slave running */
        return(0);
      }
    }
  }
  mysql_free_result(slave);

  /* now, stop slave if running */
  if (mysql_query_with_error_report(mysql_con, 0, "STOP SLAVE SQL_THREAD"))
    return(1);

  return(0);
}

static int add_stop_slave(void)
{
  if (opt_comments)
    fprintf(md_result_file,
            "\n--\n-- stop slave statement to make a recovery dump)\n--\n\n");
  fprintf(md_result_file, "STOP SLAVE;\n");
  return(0);
}

static int add_slave_statements(void)
{
  if (opt_comments)
    fprintf(md_result_file,
            "\n--\n-- start slave statement to make a recovery dump)\n--\n\n");
  fprintf(md_result_file, "START SLAVE;\n");
  return(0);
}

static int do_show_slave_status(MYSQL *mysql_con)
{
  MYSQL_RES *slave;
  const char *comment_prefix=
    (opt_slave_data == MYSQL_OPT_SLAVE_DATA_COMMENTED_SQL) ? "-- " : "";
  if (mysql_query_with_error_report(mysql_con, &slave, "SHOW SLAVE STATUS"))
  {
    if (!ignore_errors)
    {
      /* SHOW SLAVE STATUS reports nothing and --force is not enabled */
      my_printf_error(0, "Error: Slave not set up", MYF(0));
    }
    return 1;
  }
  else
  {
    MYSQL_ROW row= mysql_fetch_row(slave);
    if (row && row[9] && row[21])
    {
      /* SHOW MASTER STATUS reports file and position */
      if (opt_comments)
        fprintf(md_result_file,
                "\n--\n-- Position to start replication or point-in-time "
                "recovery from (the master of this slave)\n--\n\n");

      fprintf(md_result_file, "%sCHANGE MASTER TO ", comment_prefix);

      if (opt_include_master_host_port)
      {
        if (row[1])
          fprintf(md_result_file, "MASTER_HOST='%s', ", row[1]);
        if (row[3])
          fprintf(md_result_file, "MASTER_PORT='%s', ", row[3]);
      }
      fprintf(md_result_file,
              "MASTER_LOG_FILE='%s', MASTER_LOG_POS=%s;\n", row[9], row[21]);

      check_io(md_result_file);
    }
    mysql_free_result(slave);
  }
  return 0;
}

static int do_start_slave_sql(MYSQL *mysql_con)
{
  MYSQL_RES *slave;
  /* We need to check if the slave sql is stopped in the first place */
  if (mysql_query_with_error_report(mysql_con, &slave, "SHOW SLAVE STATUS"))
    return(1);
  else
  {
    MYSQL_ROW row= mysql_fetch_row(slave);
    if (row && row[11])
    {
      /* if SLAVE SQL is not running, we don't start it */
      if (!strcmp(row[11],"Yes"))
      {
        mysql_free_result(slave);
        /* Silently assume that they don't have the slave running */
        return(0);
      }
    }
  }
  mysql_free_result(slave);

  /* now, start slave if stopped */
  if (mysql_query_with_error_report(mysql_con, 0, "START SLAVE"))
  {
    my_printf_error(0, "Error: Unable to start slave", MYF(0));
    return 1;
  }
  return(0);
}



static int do_flush_tables_read_lock(MYSQL *mysql_con)
{
  /*
    We do first a FLUSH TABLES. If a long update is running, the FLUSH TABLES
    will wait but will not stall the whole mysqld, and when the long update is
    done the FLUSH TABLES WITH READ LOCK will start and succeed quickly. So,
    FLUSH TABLES is to lower the probability of a stage where both mysqldump
    and most client connections are stalled. Of course, if a second long
    update starts between the two FLUSHes, we have that bad stall.
  */
  return
    ( mysql_query_with_error_report(mysql_con, 0, "FLUSH TABLES") ||
      mysql_query_with_error_report(mysql_con, 0,
                                    "FLUSH TABLES WITH READ LOCK") );
}


static int do_unlock_tables(MYSQL *mysql_con)
{
  return mysql_query_with_error_report(mysql_con, 0, "UNLOCK TABLES");
}

static int get_bin_log_name(MYSQL *mysql_con,
                            char* buff_log_name, uint buff_len)
{
  MYSQL_RES *res;
  MYSQL_ROW row;

  if (mysql_query(mysql_con, "SHOW MASTER STATUS") ||
      !(res= mysql_store_result(mysql)))
    return 1;

  if (!(row= mysql_fetch_row(res)))
  {
    mysql_free_result(res);
    return 1;
  }
  /*
    Only one row is returned, and the first column is the name of the
    active log.
  */
  strmake(buff_log_name, row[0], buff_len - 1);

  mysql_free_result(res);
  return 0;
}

static int purge_bin_logs_to(MYSQL *mysql_con, char* log_name)
{
  DYNAMIC_STRING str;
  int err;
  init_dynamic_string_checked(&str, "PURGE BINARY LOGS TO '", 1024, 1024);
  dynstr_append_checked(&str, log_name);
  dynstr_append_checked(&str, "'");
  err = mysql_query_with_error_report(mysql_con, 0, str.str);
  dynstr_free(&str);
  return err;
}


static int start_transaction(MYSQL *mysql_con)
{
  /*
    We use BEGIN for old servers. --single-transaction --master-data will fail
    on old servers, but that's ok as it was already silently broken (it didn't
    do a consistent read, so better tell people frankly, with the error).

    We want the first consistent read to be used for all tables to dump so we
    need the REPEATABLE READ level (not anything lower, for example READ
    COMMITTED would give one new consistent read per dumped table).
  */
  if ((mysql_get_server_version(mysql_con) < 40100) && opt_master_data)
  {
    fprintf(stderr, "-- %s: the combination of --single-transaction and "
            "--master-data requires a MySQL server version of at least 4.1 "
            "(current server's version is %s). %s\n",
            ignore_errors ? "Warning" : "Error",
            mysql_con->server_version ? mysql_con->server_version : "unknown",
            ignore_errors ? "Continuing due to --force, backup may not be consistent across all tables!" : "Aborting.");
    if (!ignore_errors)
      exit(EX_MYSQLERR);
  }

  return (mysql_query_with_error_report(mysql_con, 0,
                                        "SET SESSION TRANSACTION ISOLATION "
                                        "LEVEL REPEATABLE READ") ||
          mysql_query_with_error_report(mysql_con, 0,
                                        "START TRANSACTION "
                                        "/*!40100 WITH CONSISTENT SNAPSHOT */"));
}


static ulong find_set(TYPELIB *lib, const char *x, uint length,
                      char **err_pos, uint *err_len)
{
  const char *end= x + length;
  ulong found= 0;
  uint find;
  char buff[255];

  *err_pos= 0;                  /* No error yet */
  while (end > x && my_isspace(charset_info, end[-1]))
    end--;

  *err_len= 0;
  if (x != end)
  {
    const char *start= x;
    for (;;)
    {
      const char *pos= start;
      uint var_len;

      for (; pos != end && *pos != ','; pos++) ;
      var_len= (uint) (pos - start);
      strmake(buff, start, min(sizeof(buff), var_len));
      find= find_type(buff, lib, var_len);
      if (!find)
      {
        *err_pos= (char*) start;
        *err_len= var_len;
      }
      else
        found|= ((longlong) 1 << (find - 1));
      if (pos == end)
        break;
      start= pos + 1;
    }
  }
  return found;
}


/* Print a value with a prefix on file */
static void print_value(FILE *file, MYSQL_RES  *result, MYSQL_ROW row,
                        const char *prefix, const char *name,
                        int string_value)
{
  MYSQL_FIELD   *field;
  mysql_field_seek(result, 0);

  for ( ; (field= mysql_fetch_field(result)) ; row++)
  {
    if (!strcmp(field->name,name))
    {
      if (row[0] && row[0][0] && strcmp(row[0],"0")) /* Skip default */
      {
        fputc(' ',file);
        fputs(prefix, file);
        if (string_value)
          unescape(file,row[0],(uint) strlen(row[0]));
        else
          fputs(row[0], file);
        check_io(file);
        return;
      }
    }
  }
  return;                                       /* This shouldn't happen */
} /* print_value */


/*
  SYNOPSIS

  Check if we the table is one of the table types that should be ignored:
  MRG_ISAM, MRG_MYISAM, if opt_delayed, if that table supports delayed inserts.
  If the table should be altogether ignored, it returns a TRUE, FALSE if it
  should not be ignored. If the user has selected to use INSERT DELAYED, it
  sets the value of the bool pointer supports_delayed_inserts to 0 if not
  supported, 1 if it is supported.

  ARGS

    check_if_ignore_table()
    table_name                  Table name to check
    table_type                  Type of table

  GLOBAL VARIABLES
    mysql                       MySQL connection
    verbose                     Write warning messages

  RETURN
    char (bit value)            See IGNORE_ values at top
*/

char check_if_ignore_table(const char *table_name, char *table_type)
{
  char result= IGNORE_NONE;
  char buff[FN_REFLEN+80], show_name_buff[FN_REFLEN];
  MYSQL_RES *res= NULL;
  MYSQL_ROW row;
  DBUG_ENTER("check_if_ignore_table");

  /* Check memory for quote_for_like() */
  DBUG_ASSERT(2*sizeof(table_name) < sizeof(show_name_buff));
  snprintf(buff, sizeof(buff), "show table status like %s",
           quote_for_like(table_name, show_name_buff));
  if (mysql_query_with_error_report(mysql, &res, buff))
  {
    if (mysql_errno(mysql) != ER_PARSE_ERROR)
    {                                   /* If old MySQL version */
      verbose_msg("-- Warning: Couldn't get status information for "
                  "table %s (%s)\n", table_name, mysql_error(mysql));
      DBUG_RETURN(result);                       /* assume table is ok */
    }
  }
  if (!(row= mysql_fetch_row(res)))
  {
    fprintf(stderr,
            "Error: Couldn't read status information for table %s (%s)\n",
            table_name, mysql_error(mysql));
    mysql_free_result(res);
    DBUG_RETURN(result);                         /* assume table is ok */
  }
  if (!(row[1]))
    strmake(table_type, "VIEW", NAME_LEN-1);
  else
  {
    /*
      If the table type matches any of these, we do support delayed inserts.
      Note: we do not want to skip dumping this table if if is not one of
      these types, but we do want to use delayed inserts in the dump if
      the table type is _NOT_ one of these types
    */
    strmake(table_type, row[1], NAME_LEN-1);
    if (opt_delayed)
    {
      if (strcmp(table_type,"MyISAM") &&
          strcmp(table_type,"ISAM") &&
          strcmp(table_type,"ARCHIVE") &&
          strcmp(table_type,"HEAP") &&
          strcmp(table_type,"MEMORY"))
        result= IGNORE_INSERT_DELAYED;
    }

    /*
      If these two types, we do want to skip dumping the table
    */
    if (!opt_no_data &&
        (!my_strcasecmp(&my_charset_latin1, table_type, "MRG_MyISAM") ||
         !strcmp(table_type,"MRG_ISAM")))
      result= IGNORE_DATA;
  }
  mysql_free_result(res);
  DBUG_RETURN(result);
}


/*
  Get string of comma-separated primary key field names

  SYNOPSIS
    char *primary_key_fields(const char *table_name)
    RETURNS     pointer to allocated buffer (must be freed by caller)
    table_name  quoted table name

  DESCRIPTION
    Use SHOW KEYS FROM table_name, allocate a buffer to hold the
    field names, and then build that string and return the pointer
    to that buffer.

    Returns NULL if there is no PRIMARY or UNIQUE key on the table,
    or if there is some failure.  It is better to continue to dump
    the table unsorted, rather than exit without dumping the data.
*/

static char *primary_key_fields(const char *table_name)
{
  MYSQL_RES  *res= NULL;
  MYSQL_ROW  row;
  /* SHOW KEYS FROM + table name * 2 (escaped) + 2 quotes + \0 */
  char show_keys_buff[15 + NAME_LEN * 2 + 3];
  uint result_length= 0;
  char *result= 0;
  char buff[NAME_LEN * 2 + 3];
  char *quoted_field;

  snprintf(show_keys_buff, sizeof(show_keys_buff),
           "SHOW KEYS FROM %s", table_name);
  if (mysql_query(mysql, show_keys_buff) ||
      !(res= mysql_store_result(mysql)))
  {
    fprintf(stderr, "Warning: Couldn't read keys from table %s;"
            " records are NOT sorted (%s)\n",
            table_name, mysql_error(mysql));
    /* Don't exit, because it's better to print out unsorted records */
    goto cleanup;
  }

  /*
   * Figure out the length of the ORDER BY clause result.
   * Note that SHOW KEYS is ordered:  a PRIMARY key is always the first
   * row, and UNIQUE keys come before others.  So we only need to check
   * the first key, not all keys.
   */
  if ((row= mysql_fetch_row(res)) && atoi(row[1]) == 0)
  {
    /* Key is unique */
    do
    {
      quoted_field= quote_name(row[4], buff, 0);
      result_length+= strlen(quoted_field) + 1; /* + 1 for ',' or \0 */
    } while ((row= mysql_fetch_row(res)) && atoi(row[3]) > 1);
  }

  /* Build the ORDER BY clause result */
  if (result_length)
  {
    char *end;
    /* result (terminating \0 is already in result_length) */
    result= my_malloc(result_length + 10, MYF(MY_WME));
    if (!result)
    {
      fprintf(stderr, "Error: Not enough memory to store ORDER BY clause\n");
      goto cleanup;
    }
    mysql_data_seek(res, 0);
    row= mysql_fetch_row(res);
    quoted_field= quote_name(row[4], buff, 0);
    end= strmov(result, quoted_field);
    while ((row= mysql_fetch_row(res)) && atoi(row[3]) > 1)
    {
      quoted_field= quote_name(row[4], buff, 0);
      end= strxmov(end, ",", quoted_field, NullS);
    }
  }

cleanup:
  if (res)
    mysql_free_result(res);

  return result;
}

/*
  The following functions are wrappers for the dynamic string functions
  and if they fail, the wrappers will terminate the current process.
*/

#define DYNAMIC_STR_ERROR_MSG "Couldn't perform DYNAMIC_STRING operation"

static void init_dynamic_string_checked(DYNAMIC_STRING *str, const char *init_str,
			    uint init_alloc, uint alloc_increment)
{
  if (init_dynamic_string(str, init_str, init_alloc, alloc_increment))
    die(EX_MYSQLERR, DYNAMIC_STR_ERROR_MSG);
}

static void dynstr_append_checked(DYNAMIC_STRING* dest, const char* src)
{
  if (dynstr_append(dest, src))
    die(EX_MYSQLERR, DYNAMIC_STR_ERROR_MSG);
}

static void dynstr_set_checked(DYNAMIC_STRING *str, const char *init_str)
{
  if (dynstr_set(str, init_str))
    die(EX_MYSQLERR, DYNAMIC_STR_ERROR_MSG);
}

static void dynstr_append_mem_checked(DYNAMIC_STRING *str, const char *append,
			  uint length)
{
  if (dynstr_append_mem(str, append, length))
    die(EX_MYSQLERR, DYNAMIC_STR_ERROR_MSG);
}

static void dynstr_realloc_checked(DYNAMIC_STRING *str, ulong additional_size)
{
  if (dynstr_realloc(str, additional_size))
    die(EX_MYSQLERR, DYNAMIC_STR_ERROR_MSG);
}


int main(int argc, char **argv)
{
  char bin_log_name[FN_REFLEN];
  int exit_code;
  MY_INIT("mysqldump");

  compatible_mode_normal_str[0]= 0;
  default_charset= (char *)mysql_universal_client_charset;
  bzero((char*) &ignore_table, sizeof(ignore_table));

  exit_code= get_options(&argc, &argv);
  if (exit_code)
  {
    free_resources();
    exit(exit_code);
  }

  if (log_error_file)
  {
    if(!(stderror_file= freopen(log_error_file, "a+", stderr)))
    {
      free_resources();
      exit(EX_MYSQLERR);
    }
  }

  if (connect_to_db(current_host, current_user, opt_password))
  {
    free_resources();
    exit(EX_MYSQLERR);
  }
  if (!path)
    write_header(md_result_file, *argv);

  if (opt_slave_data && do_stop_slave_sql(mysql))
    goto err;

  if ((opt_lock_all_tables || opt_master_data) &&
      do_flush_tables_read_lock(mysql))
    goto err;
  if (opt_single_transaction && start_transaction(mysql))
      goto err;
  if (opt_delete_master_logs)
  {
    if (mysql_refresh(mysql, REFRESH_LOG) ||
        get_bin_log_name(mysql, bin_log_name, sizeof(bin_log_name)))
      goto err;
    flush_logs= 0;
  }
  if (opt_lock_all_tables || opt_master_data)
  {
    if (flush_logs && mysql_refresh(mysql, REFRESH_LOG))
      goto err;
    flush_logs= 0; /* not anymore; that would not be sensible */
  }
  /* Add 'STOP SLAVE to beginning of dump */
  if (opt_slave_apply && add_stop_slave())
    goto err;
  if (opt_master_data && do_show_master_status(mysql))
    goto err;
  if (opt_slave_data && do_show_slave_status(mysql))
    goto err;
  if (opt_single_transaction && do_unlock_tables(mysql)) /* unlock but no commit! */
    goto err;

  if (opt_alldbs)
  {
    dump_all_databases();
  }
  else if (argc > 1 && !opt_databases)
  {
    /* Only one database and selected table(s) */
    dump_selected_tables(*argv, (argv + 1), (argc - 1));
  }
  else
  {
    dump_databases(argv);
  }

  /* if --dump-slave , start the slave sql thread */
  if (opt_slave_data && do_start_slave_sql(mysql))
    goto err;

  /* add 'START SLAVE' to end of dump */
  if (opt_slave_apply && add_slave_statements())
    goto err;

  /* ensure dumped data flushed */
  if (md_result_file && fflush(md_result_file))
  {
    if (!first_error)
      first_error= EX_MYSQLERR;
    goto err;
  }
  /* everything successful, purge the old logs files */
  if (opt_delete_master_logs && purge_bin_logs_to(mysql, bin_log_name))
    goto err;

#ifdef HAVE_SMEM
  my_free(shared_memory_base_name,MYF(MY_ALLOW_ZERO_PTR));
#endif
  /*
    No reason to explicitely COMMIT the transaction, neither to explicitely
    UNLOCK TABLES: these will be automatically be done by the server when we
    disconnect now. Saves some code here, some network trips, adds nothing to
    server.
  */
err:
  dbDisconnect(current_host);
  if (!path)
    write_footer(md_result_file);
  free_resources();

  if (stderror_file)
    fclose(stderror_file);

  return(first_error);
} /* main */
