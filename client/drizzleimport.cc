/* Copyright (C) 2008 Drizzle Open Source Development Team

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

/*
**     drizzleimport.c  - Imports all given files
**          into a table(s).
**
**         *************************
**         *         *
**         * AUTHOR: Monty & Jani  *
**         * DATE:   June 24, 1997 *
**         *         *
**         *************************
*/
#define IMPORT_VERSION "3.7"

#include "client_priv.h"
#include <string>

#include <pthread.h>

/* Added this for string translation. */
#include <drizzled/gettext.h>

using namespace std;

/* Global Thread counter */
uint32_t counter;
pthread_mutex_t counter_mutex;
pthread_cond_t count_threshhold;

static void db_error_with_table(DRIZZLE *drizzle, char *table);
static void db_error(DRIZZLE *drizzle);
static char *field_escape(char *to,const char *from,uint32_t length);
static char *add_load_option(char *ptr,const char *object,
           const char *statement);

static bool verbose= false, lock_tables= false, ignore_errors= false,
            opt_delete= false, opt_replace= false, silent= false,
            ignore= false, opt_compress= false, opt_low_priority= false,
            tty_password= false;
static bool debug_info_flag= false, debug_check_flag= false;
static uint32_t opt_use_threads= 0, opt_local_file= 0, my_end_arg= 0;
static char  *opt_password= NULL, *current_user= NULL,
    *current_host= NULL, *current_db= NULL, *fields_terminated= NULL,
    *lines_terminated= NULL, *enclosed= NULL, *opt_enclosed= NULL,
    *escaped= NULL, *opt_columns= NULL,
    *default_charset= (char*) DRIZZLE_DEFAULT_CHARSET_NAME;
static uint32_t opt_protocol= 0;
static uint32_t opt_drizzle_port= 0;
static char * opt_drizzle_unix_port= 0;
static int64_t opt_ignore_lines= -1;
static const CHARSET_INFO *charset_info= &my_charset_utf8_general_ci;

static struct my_option my_long_options[] =
{
  {"default-character-set", OPT_DEFAULT_CHARSET,
   "Set the default character set.", (char**) &default_charset,
   (char**) &default_charset, 0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"columns", 'c',
   "Use only these columns to import the data to. Give the column names in a comma separated list. This is same as giving columns to LOAD DATA INFILE.",
   (char**) &opt_columns, (char**) &opt_columns, 0, GET_STR, REQUIRED_ARG, 0, 0, 0,
   0, 0, 0},
  {"compress", 'C', "Use compression in server/client protocol.",
   (char**) &opt_compress, (char**) &opt_compress, 0, GET_BOOL, NO_ARG, 0, 0, 0,
   0, 0, 0},
  {"debug",'#', "Output debug log. Often this is 'd:t:o,filename'.", 0, 0, 0,
   GET_STR, OPT_ARG, 0, 0, 0, 0, 0, 0},
  {"debug-check", OPT_DEBUG_CHECK, "Check memory and open file usage at exit.",
   (char**) &debug_check_flag, (char**) &debug_check_flag, 0,
   GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"debug-info", OPT_DEBUG_INFO, "Print some debug info at exit.",
   (char**) &debug_info_flag, (char**) &debug_info_flag,
   0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"delete", 'd', "First delete all rows from table.", (char**) &opt_delete,
   (char**) &opt_delete, 0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"fields-terminated-by", OPT_FTB,
   "Fields in the textfile are terminated by ...", (char**) &fields_terminated,
   (char**) &fields_terminated, 0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"fields-enclosed-by", OPT_ENC,
   "Fields in the importfile are enclosed by ...", (char**) &enclosed,
   (char**) &enclosed, 0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"fields-optionally-enclosed-by", OPT_O_ENC,
   "Fields in the i.file are opt. enclosed by ...", (char**) &opt_enclosed,
   (char**) &opt_enclosed, 0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"fields-escaped-by", OPT_ESC, "Fields in the i.file are escaped by ...",
   (char**) &escaped, (char**) &escaped, 0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0,
   0, 0},
  {"force", 'f', "Continue even if we get an sql-error.",
   (char**) &ignore_errors, (char**) &ignore_errors, 0, GET_BOOL, NO_ARG, 0, 0,
   0, 0, 0, 0},
  {"help", '?', "Displays this help and exits.", 0, 0, 0, GET_NO_ARG, NO_ARG,
   0, 0, 0, 0, 0, 0},
  {"host", 'h', "Connect to host.", (char**) &current_host,
   (char**) &current_host, 0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"ignore", 'i', "If duplicate unique key was found, keep old row.",
   (char**) &ignore, (char**) &ignore, 0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"ignore-lines", OPT_IGN_LINES, "Ignore first n lines of data infile.",
   (char**) &opt_ignore_lines, (char**) &opt_ignore_lines, 0, GET_LL,
   REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"lines-terminated-by", OPT_LTB, "Lines in the i.file are terminated by ...",
   (char**) &lines_terminated, (char**) &lines_terminated, 0, GET_STR,
   REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"local", 'L', "Read all files through the client.", (char**) &opt_local_file,
   (char**) &opt_local_file, 0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"lock-tables", 'l', "Lock all tables for write (this disables threads).",
    (char**) &lock_tables, (char**) &lock_tables, 0, GET_BOOL, NO_ARG,
    0, 0, 0, 0, 0, 0},
  {"low-priority", OPT_LOW_PRIORITY,
   "Use LOW_PRIORITY when updating the table.", (char**) &opt_low_priority,
   (char**) &opt_low_priority, 0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"password", 'P',
   "Password to use when connecting to server. If password is not given it's asked from the tty.",
   0, 0, 0, GET_STR, OPT_ARG, 0, 0, 0, 0, 0, 0},
  {"port", 'p', "Port number to use for connection or 0 for default to, in "
   "order of preference, drizzle.cnf, $DRIZZLE_TCP_PORT, "
   "built-in default (" STRINGIFY_ARG(DRIZZLE_PORT) ").",
   0, 0, 0, GET_UINT, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"protocol", OPT_DRIZZLE_PROTOCOL, "The protocol of connection (tcp,socket,pipe,memory).",
   0, 0, 0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"replace", 'r', "If duplicate unique key was found, replace old row.",
   (char**) &opt_replace, (char**) &opt_replace, 0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"silent", 's', "Be more silent.", (char**) &silent, (char**) &silent, 0,
   GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"socket", 'S', "Socket file to use for connection.",
   (char**) &opt_drizzle_unix_port, (char**) &opt_drizzle_unix_port, 0, GET_STR,
   REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"use-threads", OPT_USE_THREADS,
   "Load files in parallel. The argument is the number "
   "of threads to use for loading data.",
   (char**) &opt_use_threads, (char**) &opt_use_threads, 0,
   GET_UINT, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
#ifndef DONT_ALLOW_USER_CHANGE
  {"user", 'u', "User for login if not current user.", (char**) &current_user,
   (char**) &current_user, 0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
#endif
  {"verbose", 'v', "Print info about the various stages.", (char**) &verbose,
   (char**) &verbose, 0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"version", 'V', "Output version information and exit.", 0, 0, 0, GET_NO_ARG,
   NO_ARG, 0, 0, 0, 0, 0, 0},
  { 0, 0, 0, 0, 0, 0, GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0}
};


static const char *load_default_groups[]= { "drizzleimport","client",0 };

static void print_version(void)
{
  printf("%s  Ver %s Distrib %s, for %s (%s)\n" ,my_progname,
    IMPORT_VERSION, drizzleclient_get_client_info(),SYSTEM_TYPE,MACHINE_TYPE);
}


static void usage(void)
{
  print_version();
  puts("Copyright (C) 2008 Drizzle Open Source Development Team");
  puts("This software comes with ABSOLUTELY NO WARRANTY. This is free software,\nand you are welcome to modify and redistribute it under the GPL license\n");
  printf("\
Loads tables from text files in various formats.  The base name of the\n\
text file must be the name of the table that should be used.\n\
If one uses sockets to connect to the Drizzle server, the server will open and\n\
read the text file directly. In other cases the client will open the text\n\
file. The SQL command 'LOAD DATA INFILE' is used to import the rows.\n");

  printf("\nUsage: %s [OPTIONS] database textfile...",my_progname);
  print_defaults("drizzle",load_default_groups);
  my_print_help(my_long_options);
  my_print_variables(my_long_options);
}

extern "C"
bool get_one_option(int optid, const struct my_option *, char *argument)
{
  char *endchar= NULL;
  uint64_t temp_drizzle_port= 0;

  switch(optid) {
  case 'p':
    temp_drizzle_port= (uint64_t) strtoul(argument, &endchar, 10);
    /* if there is an alpha character this is not a valid port */
    if (strlen(endchar) != 0)
    {
      fprintf(stderr, _("Non-integer value supplied for port.  If you are trying to enter a password please use --password instead.\n"));
      exit(1);
    }
    /* If the port number is > 65535 it is not a valid port
       This also helps with potential data loss casting unsigned long to a
       uint32_t. */
    if ((temp_drizzle_port == 0) || (temp_drizzle_port > 65535))
    {
      fprintf(stderr, _("Value supplied for port is not valid.\n"));
      exit(1);
    }
    else
    {
      opt_drizzle_port= (uint32_t) temp_drizzle_port;
    }
    break;
  case 'P':
    if (argument)
    {
      char *start=argument;
      if (opt_password)
        free(opt_password);
      opt_password = strdup(argument);
      if (opt_password == NULL)
      {
        fprintf(stderr, "Memory allocation error while copying password. "
                        "Aborting.\n");
        exit(ENOMEM);
      }
      while (*argument)
      {
        /* Overwriting password with 'x' */
        *argument++= 'x';
      }
      if (*start)
      {
        /* Cut length of argument */
        start[1]= 0;
      }
      tty_password= 0;
    }
    else
      tty_password= 1;
    break;
  case OPT_DRIZZLE_PROTOCOL:
    break;
  case 'V': print_version(); exit(0);
  case 'I':
  case '?':
    usage();
    exit(0);
  }
  return 0;
}


static int get_options(int *argc, char ***argv)
{
  int ho_error;

  if ((ho_error=handle_options(argc, argv, my_long_options, get_one_option)))
    exit(ho_error);
  if (debug_info_flag)
    my_end_arg= MY_CHECK_ERROR | MY_GIVE_INFO;
  if (debug_check_flag)
    my_end_arg= MY_CHECK_ERROR;

  if (enclosed && opt_enclosed)
  {
    fprintf(stderr, "You can't use ..enclosed.. and ..optionally-enclosed.. at the same time.\n");
    return(1);
  }
  if (opt_replace && ignore)
  {
    fprintf(stderr, "You can't use --ignore (-i) and --replace (-r) at the same time.\n");
    return(1);
  }
  if (strcmp(default_charset, charset_info->csname) &&
      !(charset_info= get_charset_by_csname(default_charset, MY_CS_PRIMARY)))
    exit(1);
  if (*argc < 2)
  {
    usage();
    return 1;
  }
  current_db= *((*argv)++);
  (*argc)--;
  if (tty_password)
    opt_password=drizzleclient_get_tty_password(NULL);
  return(0);
}



static int write_to_table(char *filename, DRIZZLE *drizzle)
{
  char tablename[FN_REFLEN], hard_path[FN_REFLEN],
       sql_statement[FN_REFLEN*16+256], *end;

  fn_format(tablename, filename, "", "", 1 | 2); /* removes path & ext. */
  if (!opt_local_file)
    strcpy(hard_path,filename);
  else
    my_load_path(hard_path, filename, NULL); /* filename includes the path */

  if (opt_delete)
  {
    if (verbose)
      fprintf(stdout, "Deleting the old data from table %s\n", tablename);
#ifdef HAVE_SNPRINTF
    snprintf(sql_statement, FN_REFLEN*16+256, "DELETE FROM %s", tablename);
#else
    sprintf(sql_statement, "DELETE FROM %s", tablename);
#endif
    if (drizzleclient_query(drizzle, sql_statement))
    {
      db_error_with_table(drizzle, tablename);
      return(1);
    }
  }
  if (verbose)
  {
    if (opt_local_file)
      fprintf(stdout, "Loading data from LOCAL file: %s into %s\n",
        hard_path, tablename);
    else
      fprintf(stdout, "Loading data from SERVER file: %s into %s\n",
        hard_path, tablename);
  }
  sprintf(sql_statement, "LOAD DATA %s %s INFILE '%s'",
    opt_low_priority ? "LOW_PRIORITY" : "",
    opt_local_file ? "LOCAL" : "", hard_path);
  end= strchr(sql_statement, '\0');
  if (opt_replace)
    end= strcpy(end, " REPLACE")+8;
  if (ignore)
    end= strcpy(end, " IGNORE")+7;

  end+= sprintf(end, " INTO TABLE %s", tablename);

  if (fields_terminated || enclosed || opt_enclosed || escaped)
      end= strcpy(end, " FIELDS")+7;
  end= add_load_option(end, fields_terminated, " TERMINATED BY");
  end= add_load_option(end, enclosed, " ENCLOSED BY");
  end= add_load_option(end, opt_enclosed,
           " OPTIONALLY ENCLOSED BY");
  end= add_load_option(end, escaped, " ESCAPED BY");
  end= add_load_option(end, lines_terminated, " LINES TERMINATED BY");
  if (opt_ignore_lines >= 0)
  {
    end= strcpy(end, " IGNORE ")+8;
    end= int64_t2str(opt_ignore_lines, end, 10);
    end= strcpy(end, " LINES")+6;
  }
  if (opt_columns)
  {
    end= strcpy(end, " (")+2;
    end= strcpy(end, opt_columns)+strlen(opt_columns);
    end= strcpy(end, ")")+1;
  }
  *end= '\0';

  if (drizzleclient_query(drizzle, sql_statement))
  {
    db_error_with_table(drizzle, tablename);
    return(1);
  }
  if (!silent)
  {
    if (drizzleclient_info(drizzle)) /* If NULL-pointer, print nothing */
    {
      fprintf(stdout, "%s.%s: %s\n", current_db, tablename,
        drizzleclient_info(drizzle));
    }
  }
  return(0);
}



static void lock_table(DRIZZLE *drizzle, int tablecount, char **raw_tablename)
{
  string query;
  int i;
  char tablename[FN_REFLEN];

  if (verbose)
    fprintf(stdout, "Locking tables for write\n");
  query.append("LOCK TABLES ");
  for (i=0 ; i < tablecount ; i++)
  {
    fn_format(tablename, raw_tablename[i], "", "", 1 | 2);
    query.append(tablename);
    query.append(" WRITE,");
  }
  if (drizzleclient_real_query(drizzle, query.c_str(), query.length()-1))
    db_error(drizzle); /* We shall countinue here, if --force was given */
}




static DRIZZLE *db_connect(char *host, char *database,
                         char *user, char *passwd)
{
  DRIZZLE *drizzle;
  if (verbose)
    fprintf(stdout, "Connecting to %s\n", host ? host : "localhost");
  if (!(drizzle= drizzleclient_create(NULL)))
    return 0;
  if (opt_compress)
    drizzleclient_options(drizzle,DRIZZLE_OPT_COMPRESS,NULL);
  if (opt_protocol)
    drizzleclient_options(drizzle,DRIZZLE_OPT_PROTOCOL,(char*)&opt_protocol);
  if (!(drizzleclient_connect(drizzle,host,user,passwd,
                           database,opt_drizzle_port,opt_drizzle_unix_port,
                           0)))
  {
    ignore_errors=0;    /* NO RETURN FROM db_error */
    db_error(drizzle);
  }
  drizzle->reconnect= 0;
  if (verbose)
    fprintf(stdout, "Selecting database %s\n", database);
  if (drizzleclient_select_db(drizzle, database))
  {
    ignore_errors=0;
    db_error(drizzle);
  }
  return drizzle;
}



static void db_disconnect(char *host, DRIZZLE *drizzle)
{
  if (verbose)
    fprintf(stdout, "Disconnecting from %s\n", host ? host : "localhost");
  drizzleclient_close(drizzle);
}



static void safe_exit(int error, DRIZZLE *drizzle)
{
  if (ignore_errors)
    return;
  if (drizzle)
    drizzleclient_close(drizzle);
  exit(error);
}



static void db_error_with_table(DRIZZLE *drizzle, char *table)
{
  my_printf_error(0,"Error: %d, %s, when using table: %s",
      MYF(0), drizzleclient_errno(drizzle), drizzleclient_error(drizzle), table);
  safe_exit(1, drizzle);
}



static void db_error(DRIZZLE *drizzle)
{
  my_printf_error(0,"Error: %d %s", MYF(0), drizzleclient_errno(drizzle), drizzleclient_error(drizzle));
  safe_exit(1, drizzle);
}


static char *add_load_option(char *ptr, const char *object,
           const char *statement)
{
  if (object)
  {
    /* Don't escape hex constants */
    if (object[0] == '0' && (object[1] == 'x' || object[1] == 'X'))
      ptr+= sprintf(ptr, " %s %s", statement, object);
    else
    {
      /* char constant; escape */
      ptr+= sprintf(ptr, " %s '", statement); 
      ptr= field_escape(ptr,object,(uint32_t) strlen(object));
      *ptr++= '\'';
    }
  }
  return ptr;
}

/*
** Allow the user to specify field terminator strings like:
** "'", "\", "\\" (escaped backslash), "\t" (tab), "\n" (newline)
** This is done by doubleing ' and add a end -\ if needed to avoid
** syntax errors from the SQL parser.
*/

static char *field_escape(char *to,const char *from,uint32_t length)
{
  const char *end;
  uint32_t end_backslashes=0;

  for (end= from+length; from != end; from++)
  {
    *to++= *from;
    if (*from == '\\')
      end_backslashes^=1;    /* find odd number of backslashes */
    else
    {
      if (*from == '\'' && !end_backslashes)
  *to++= *from;      /* We want a dublicate of "'" for DRIZZLE */
      end_backslashes=0;
    }
  }
  /* Add missing backslashes if user has specified odd number of backs.*/
  if (end_backslashes)
    *to++= '\\';
  return to;
}

int exitcode= 0;

extern "C"
void * worker_thread(void *arg)
{
  int error;
  char *raw_table_name= (char *)arg;
  DRIZZLE *drizzle= 0;

  if (!(drizzle= db_connect(current_host,current_db,current_user,opt_password)))
  {
    goto error;
  }

  if (drizzleclient_query(drizzle, "/*!40101 set @@character_set_database=binary */;"))
  {
    db_error(drizzle); /* We shall countinue here, if --force was given */
    goto error;
  }

  /*
    We are not currently catching the error here.
  */
  if((error= write_to_table(raw_table_name, drizzle)))
    if (exitcode == 0)
      exitcode= error;

error:
  if (drizzle)
    db_disconnect(current_host, drizzle);

  pthread_mutex_lock(&counter_mutex);
  counter--;
  pthread_cond_signal(&count_threshhold);
  pthread_mutex_unlock(&counter_mutex);
  my_thread_end();

  return 0;
}


int main(int argc, char **argv)
{
  int error=0;
  char **argv_to_free;
  MY_INIT(argv[0]);

  load_defaults("drizzle",load_default_groups,&argc,&argv);
  /* argv is changed in the program */
  argv_to_free= argv;
  if (get_options(&argc, &argv))
  {
    free_defaults(argv_to_free);
    return(1);
  }

#ifdef HAVE_LIBPTHREAD
  if (opt_use_threads && !lock_tables)
  {
    pthread_t mainthread;            /* Thread descriptor */
    pthread_attr_t attr;          /* Thread attributes */
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr,
                                PTHREAD_CREATE_DETACHED);

    pthread_mutex_init(&counter_mutex, NULL);
    pthread_cond_init(&count_threshhold, NULL);

    for (counter= 0; *argv != NULL; argv++) /* Loop through tables */
    {
      pthread_mutex_lock(&counter_mutex);
      while (counter == opt_use_threads)
      {
        struct timespec abstime;

        set_timespec(abstime, 3);
        pthread_cond_timedwait(&count_threshhold, &counter_mutex, &abstime);
      }
      /* Before exiting the lock we set ourselves up for the next thread */
      counter++;
      pthread_mutex_unlock(&counter_mutex);
      /* now create the thread */
      if (pthread_create(&mainthread, &attr, worker_thread,
                         (void *)*argv) != 0)
      {
        pthread_mutex_lock(&counter_mutex);
        counter--;
        pthread_mutex_unlock(&counter_mutex);
        fprintf(stderr,"%s: Could not create thread\n",
                my_progname);
      }
    }

    /*
      We loop until we know that all children have cleaned up.
    */
    pthread_mutex_lock(&counter_mutex);
    while (counter)
    {
      struct timespec abstime;

      set_timespec(abstime, 3);
      pthread_cond_timedwait(&count_threshhold, &counter_mutex, &abstime);
    }
    pthread_mutex_unlock(&counter_mutex);
    pthread_mutex_destroy(&counter_mutex);
    pthread_cond_destroy(&count_threshhold);
    pthread_attr_destroy(&attr);
  }
  else
#endif
  {
    DRIZZLE *drizzle= 0;
    if (!(drizzle= db_connect(current_host,current_db,current_user,opt_password)))
    {
      free_defaults(argv_to_free);
      return(1); /* purecov: deadcode */
    }

    if (drizzleclient_query(drizzle, "/*!40101 set @@character_set_database=binary */;"))
    {
      db_error(drizzle); /* We shall countinue here, if --force was given */
      return(1);
    }

    if (lock_tables)
      lock_table(drizzle, argc, argv);
    for (; *argv != NULL; argv++)
      if ((error= write_to_table(*argv, drizzle)))
        if (exitcode == 0)
          exitcode= error;
    db_disconnect(current_host, drizzle);
  }
  free(opt_password);
  free_defaults(argv_to_free);
  my_end(my_end_arg);
  return(exitcode);
}