/* Copyright (C) 2000-2003 MySQL AB

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

  Low level functions for storing data to be send to the MySQL client.
  The actual communction is handled by the net_xxx functions in net_serv.cc
*/
#include <drizzled/server_includes.h>
#include <drizzled/error.h>
#include <drizzled/sql_state.h>
#include <libdrizzleclient/pack.h>
#include <drizzled/protocol.h>
#include <drizzled/session.h>

static const unsigned int PACKET_BUFFER_EXTRA_ALLOC= 1024;
/* Declared non-static only because of the embedded library. */
static void net_send_error_packet(Session *session, uint32_t sql_errno, const char *err);
static void write_eof_packet(Session *session, NET *net,
                             uint32_t server_status, uint32_t total_warn_count);

bool Protocol::net_store_data(const unsigned char *from, size_t length)
{
  size_t packet_length= packet->length();
  /*
     The +9 comes from that strings of length longer than 16M require
     9 bytes to be stored (see drizzleclient_net_store_length).
  */
  if (packet_length+9+length > packet->alloced_length() &&
      packet->realloc(packet_length+9+length))
    return 1;
  unsigned char *to= drizzleclient_net_store_length((unsigned char*) packet->ptr()+packet_length, length);
  memcpy(to,from,length);
  packet->length((size_t) (to+length-(unsigned char*) packet->ptr()));
  return 0;
}




/*
  net_store_data() - extended version with character set conversion.

  It is optimized for short strings whose length after
  conversion is garanteed to be less than 251, which accupies
  exactly one byte to store length. It allows not to use
  the "convert" member as a temporary buffer, conversion
  is done directly to the "packet" member.
  The limit 251 is good enough to optimize send_fields()
  because column, table, database names fit into this limit.
*/

bool Protocol::net_store_data(const unsigned char *from, size_t length,
                              const CHARSET_INFO * const from_cs,
							  const CHARSET_INFO * const to_cs)
{
  uint32_t dummy_errors;
  /* Calculate maxumum possible result length */
  uint32_t conv_length= to_cs->mbmaxlen * length / from_cs->mbminlen;
  if (conv_length > 250)
  {
    /*
      For strings with conv_length greater than 250 bytes
      we don't know how many bytes we will need to store length: one or two,
      because we don't know result length until conversion is done.
      For example, when converting from utf8 (mbmaxlen=3) to latin1,
      conv_length=300 means that the result length can vary between 100 to 300.
      length=100 needs one byte, length=300 needs to bytes.

      Thus conversion directly to "packet" is not worthy.
      Let's use "convert" as a temporary buffer.
    */
    return (convert->copy((const char*) from, length, from_cs,
                          to_cs, &dummy_errors) ||
            net_store_data((const unsigned char*) convert->ptr(), convert->length()));
  }

  size_t packet_length= packet->length();
  size_t new_length= packet_length + conv_length + 1;

  if (new_length > packet->alloced_length() && packet->realloc(new_length))
    return 1;

  char *length_pos= (char*) packet->ptr() + packet_length;
  char *to= length_pos + 1;

  to+= copy_and_convert(to, conv_length, to_cs,
                        (const char*) from, length, from_cs, &dummy_errors);

  drizzleclient_net_store_length((unsigned char*) length_pos, to - length_pos - 1);
  packet->length((uint32_t) (to - packet->ptr()));
  return 0;
}


/**
  Send a error string to client.

  Design note:
  net_printf_error and net_send_error are low-level functions
  that shall be used only when a new connection is being
  established or at server startup.

  For SIGNAL/RESIGNAL and GET DIAGNOSTICS functionality it's
  critical that every error that can be intercepted is issued in one
  place only, my_message_sql.
*/
void net_send_error(Session *session, uint32_t sql_errno, const char *err)
{
  assert(sql_errno);
  assert(err && err[0]);

  /*
    It's one case when we can push an error even though there
    is an OK or EOF already.
  */
  session->main_da.can_overwrite_status= true;

  /* Abort multi-result sets */
  session->server_status&= ~SERVER_MORE_RESULTS_EXISTS;

  net_send_error_packet(session, sql_errno, err);

  session->main_da.can_overwrite_status= false;
}

/**
  Return ok to the client.

  The ok packet has the following structure:

  - 0               : Marker (1 byte)
  - affected_rows	: Stored in 1-9 bytes
  - id		: Stored in 1-9 bytes
  - server_status	: Copy of session->server_status;  Can be used by client
  to check if we are inside an transaction.
  New in 4.0 protocol
  - warning_count	: Stored in 2 bytes; New in 4.1 protocol
  - message		: Stored as packed length (1-9 bytes) + message.
  Is not stored if no message.

  @param session		   Thread handler
  @param affected_rows	   Number of rows changed by statement
  @param id		   Auto_increment id for first row (if used)
  @param message	   Message to send to the client (Used by mysql_status)
*/

static void
net_send_ok(Session *session,
            uint32_t server_status, uint32_t total_warn_count,
            ha_rows affected_rows, uint64_t id, const char *message)
{
  NET *net= &session->net;
  unsigned char buff[DRIZZLE_ERRMSG_SIZE+10],*pos;

  if (! net->vio)	// hack for re-parsing queries
  {
    return;
  }

  buff[0]=0;					// No fields
  pos=drizzleclient_net_store_length(buff+1,affected_rows);
  pos=drizzleclient_net_store_length(pos, id);

  int2store(pos, server_status);
  pos+=2;

  /* We can only return up to 65535 warnings in two bytes */
  uint32_t tmp= cmin(total_warn_count, (uint32_t)65535);
  int2store(pos, tmp);
  pos+= 2;

  session->main_da.can_overwrite_status= true;

  if (message && message[0])
    pos= net_store_data(pos, (unsigned char*) message, strlen(message));
  drizzleclient_net_write(net, buff, (size_t) (pos-buff));
  drizzleclient_net_flush(net);

  session->main_da.can_overwrite_status= false;
}

/**
  Send eof (= end of result set) to the client.

  The eof packet has the following structure:

  - 254	(DRIZZLE_PROTOCOL_NO_MORE_DATA)	: Marker (1 byte)
  - warning_count	: Stored in 2 bytes; New in 4.1 protocol
  - status_flag	: Stored in 2 bytes;
  For flags like SERVER_MORE_RESULTS_EXISTS.

  Note that the warning count will not be sent if 'no_flush' is set as
  we don't want to report the warning count until all data is sent to the
  client.

  @param session		Thread handler
  @param no_flush	Set to 1 if there will be more data to the client,
                    like in send_fields().
*/

static void
net_send_eof(Session *session, uint32_t server_status, uint32_t total_warn_count)
{
  NET *net= &session->net;
  /* Set to true if no active vio, to work well in case of --init-file */
  if (net->vio != 0)
  {
    session->main_da.can_overwrite_status= true;
    write_eof_packet(session, net, server_status, total_warn_count);
    drizzleclient_net_flush(net);
    session->main_da.can_overwrite_status= false;
  }
}


/**
  Format EOF packet according to the current protocol and
  write it to the network output buffer.
*/

static void write_eof_packet(Session *session, NET *net,
                             uint32_t server_status,
                             uint32_t total_warn_count)
{
  unsigned char buff[5];
  /*
    Don't send warn count during SP execution, as the warn_list
    is cleared between substatements, and mysqltest gets confused
  */
  uint32_t tmp= cmin(total_warn_count, (uint32_t)65535);
  buff[0]= DRIZZLE_PROTOCOL_NO_MORE_DATA;
  int2store(buff+1, tmp);
  /*
    The following test should never be true, but it's better to do it
    because if 'is_fatal_error' is set the server is not going to execute
    other queries (see the if test in dispatch_command / COM_QUERY)
  */
  if (session->is_fatal_error)
    server_status&= ~SERVER_MORE_RESULTS_EXISTS;
  int2store(buff + 3, server_status);
  drizzleclient_net_write(net, buff, 5);
}

void net_send_error_packet(Session *session, uint32_t sql_errno, const char *err)
{
  NET *net= &session->net;
  uint32_t length;
  /*
    buff[]: sql_errno:2 + ('#':1 + SQLSTATE_LENGTH:5) + DRIZZLE_ERRMSG_SIZE:512
  */
  unsigned char buff[2+1+SQLSTATE_LENGTH+DRIZZLE_ERRMSG_SIZE], *pos;

  if (net->vio == 0)
  {
    return;
  }

  int2store(buff,sql_errno);
  pos= buff+2;

  /* The first # is to make the protocol backward compatible */
  buff[2]= '#';
  pos= (unsigned char*) strcpy((char*) buff+3, drizzleclient_errno_to_sqlstate(sql_errno));
  pos+= strlen(drizzleclient_errno_to_sqlstate(sql_errno));

  char *tmp= strncpy((char*)pos, err, DRIZZLE_ERRMSG_SIZE-1);
  tmp+= strlen((char*)pos);
  tmp[0]= '\0';
  length= (uint32_t)(tmp-(char*)buff);
  err= (char*) buff;

  drizzleclient_net_write_command(net,(unsigned char) 255, (unsigned char*) "", 0, (unsigned char*) err, length);
  return;
}


/**
  Faster drizzleclient_net_store_length when we know that length is less than 65536.
  We keep a separate version for that range because it's widely used in
  libmysql.

  uint32_t is used as agrument type because of MySQL type conventions:
  - uint32_t for 0..65536
  - ulong for 0..4294967296
  - uint64_t for bigger numbers.
*/

static unsigned char *drizzleclient_net_store_length_fast(unsigned char *packet, uint32_t length)
{
  if (length < 251)
  {
    *packet=(unsigned char) length;
    return packet+1;
  }
  *packet++=252;
  int2store(packet,(uint32_t) length);

  return packet+2;
}

/**
  Send the status of the current statement execution over network.

  @param  session   in fact, carries two parameters, NET for the transport and
                Diagnostics_area as the source of status information.

  In MySQL, there are two types of SQL statements: those that return
  a result set and those that return status information only.

  If a statement returns a result set, it consists of 3 parts:
  - result set meta-data
  - variable number of result set rows (can be 0)
  - followed and terminated by EOF or ERROR packet

  Once the  client has seen the meta-data information, it always
  expects an EOF or ERROR to terminate the result set. If ERROR is
  received, the result set rows are normally discarded (this is up
  to the client implementation, libmysql at least does discard them).
  EOF, on the contrary, means "successfully evaluated the entire
  result set". Since we don't know how many rows belong to a result
  set until it's evaluated, EOF/ERROR is the indicator of the end
  of the row stream. Note, that we can not buffer result set rows
  on the server -- there may be an arbitrary number of rows. But
  we do buffer the last packet (EOF/ERROR) in the Diagnostics_area and
  delay sending it till the very end of execution (here), to be able to
  change EOF to an ERROR if commit failed or some other error occurred
  during the last cleanup steps taken after execution.

  A statement that does not return a result set doesn't send result
  set meta-data either. Instead it returns one of:
  - OK packet
  - ERROR packet.
  Similarly to the EOF/ERROR of the previous statement type, OK/ERROR
  packet is "buffered" in the diagnostics area and sent to the client
  in the end of statement.

  @pre  The diagnostics area is assigned or disabled. It can not be empty
        -- we assume that every SQL statement or COM_* command
        generates OK, ERROR, or EOF status.

  @post The status information is encoded to protocol format and sent to the
        client.

  @return We conventionally return void, since the only type of error
          that can happen here is a NET (transport) error, and that one
          will become visible when we attempt to read from the NET the
          next command.
          Diagnostics_area::is_sent is set for debugging purposes only.
*/

void drizzleclient_net_end_statement(Session *session)
{
  assert(! session->main_da.is_sent);

  /* Can not be true, but do not take chances in production. */
  if (session->main_da.is_sent)
    return;

  switch (session->main_da.status()) {
  case Diagnostics_area::DA_ERROR:
    /* The query failed, send error to log and abort bootstrap. */
    net_send_error(session,
                   session->main_da.sql_errno(),
                   session->main_da.message());
    break;
  case Diagnostics_area::DA_EOF:
    net_send_eof(session,
                 session->main_da.server_status(),
                 session->main_da.total_warn_count());
    break;
  case Diagnostics_area::DA_OK:
    net_send_ok(session,
                session->main_da.server_status(),
                session->main_da.total_warn_count(),
                session->main_da.affected_rows(),
                session->main_da.last_insert_id(),
                session->main_da.message());
    break;
  case Diagnostics_area::DA_DISABLED:
    break;
  case Diagnostics_area::DA_EMPTY:
  default:
    //TODO: Something is being masked here by commenting this out
    //  assert(0);
    net_send_ok(session, session->server_status, session->total_warn_count,
                0, 0, NULL);
    break;
  }
  session->main_da.is_sent= true;
}


/****************************************************************************
  Functions used by the protocol functions (like net_send_ok) to store
  strings and numbers in the header result packet.
****************************************************************************/

/* The following will only be used for short strings < 65K */

unsigned char *net_store_data(unsigned char *to, const unsigned char *from, size_t length)
{
  to=drizzleclient_net_store_length_fast(to,length);
  memcpy(to,from,length);
  return to+length;
}

unsigned char *net_store_data(unsigned char *to,int32_t from)
{
  char buff[20];
  uint32_t length=(uint32_t) (int10_to_str(from,buff,10)-buff);
  to=drizzleclient_net_store_length_fast(to,length);
  memcpy(to,buff,length);
  return to+length;
}

unsigned char *net_store_data(unsigned char *to,int64_t from)
{
  char buff[22];
  uint32_t length=(uint32_t) (int64_t10_to_str(from,buff,10)-buff);
  to=drizzleclient_net_store_length_fast(to,length);
  memcpy(to,buff,length);
  return to+length;
}


/*****************************************************************************
  Default Protocol functions
*****************************************************************************/

void Protocol::init(Session *session_arg)
{
  session=session_arg;
  packet= &session->packet;
  convert= &session->convert_buffer;
}


bool Protocol::flush()
{
  return drizzleclient_net_flush(&session->net);
}


/**
  Send name and type of result to client.

  Sum fields has table name empty and field_name.

  @param Session		Thread data object
  @param list	        List of items to send to client
  @param flag	        Bit mask with the following functions:
                        - 1 send number of rows
                        - 2 send default values
                        - 4 don't write eof packet

  @retval
    0	ok
  @retval
    1	Error  (Note that in this case the error is not sent to the
    client)
*/
bool Protocol::send_fields(List<Item> *list, uint32_t flags)
{
  List_iterator_fast<Item> it(*list);
  Item *item;
  unsigned char buff[80];
  String tmp((char*) buff,sizeof(buff),&my_charset_bin);
  Protocol_text prot(session);
  String *local_packet= prot.storage_packet();
  const CHARSET_INFO * const session_charset= default_charset_info;

  if (flags & SEND_NUM_ROWS)
  {				// Packet with number of elements
    unsigned char *pos= drizzleclient_net_store_length(buff, list->elements);
    (void) drizzleclient_net_write(&session->net, buff, (size_t) (pos-buff));
  }

  while ((item=it++))
  {
    char *pos;
    const CHARSET_INFO * const cs= system_charset_info;
    Send_field field;
    item->make_field(&field);

    prot.prepare_for_resend();


    if (prot.store(STRING_WITH_LEN("def"), cs, session_charset) ||
        prot.store(field.db_name, (uint32_t) strlen(field.db_name),
                   cs, session_charset) ||
        prot.store(field.table_name, (uint32_t) strlen(field.table_name),
                   cs, session_charset) ||
        prot.store(field.org_table_name, (uint32_t) strlen(field.org_table_name),
                   cs, session_charset) ||
        prot.store(field.col_name, (uint32_t) strlen(field.col_name),
                   cs, session_charset) ||
        prot.store(field.org_col_name, (uint32_t) strlen(field.org_col_name),
                   cs, session_charset) ||
        local_packet->realloc(local_packet->length()+12))
      goto err;

    /* Store fixed length fields */
    pos= (char*) local_packet->ptr()+local_packet->length();
    *pos++= 12;				// Length of packed fields
    if (item->collation.collation == &my_charset_bin || session_charset == NULL)
    {
      /* No conversion */
      int2store(pos, field.charsetnr);
      int4store(pos+2, field.length);
    }
    else
    {
      /* With conversion */
      uint32_t max_char_len;
      int2store(pos, session_charset->number);
      /*
        For TEXT/BLOB columns, field_length describes the maximum data
        length in bytes. There is no limit to the number of characters
        that a TEXT column can store, as long as the data fits into
        the designated space.
        For the rest of textual columns, field_length is evaluated as
        char_count * mbmaxlen, where character count is taken from the
        definition of the column. In other words, the maximum number
        of characters here is limited by the column definition.
      */
      max_char_len= field.length / item->collation.collation->mbmaxlen;
      int4store(pos+2, max_char_len * session_charset->mbmaxlen);
    }
    pos[6]= field.type;
    int2store(pos+7,field.flags);
    pos[9]= (char) field.decimals;
    pos[10]= 0;				// For the future
    pos[11]= 0;				// For the future
    pos+= 12;

    local_packet->length((uint32_t) (pos - local_packet->ptr()));
    if (flags & SEND_DEFAULTS)
      item->send(&prot, &tmp);			// Send default value
    if (prot.write())
      break;					/* purecov: inspected */
  }

  if (flags & SEND_EOF)
  {
    /*
      Mark the end of meta-data result set, and store session->server_status,
      to show that there is no cursor.
      Send no warning information, as it will be sent at statement end.
    */
    write_eof_packet(session, &session->net, session->server_status, session->total_warn_count);
  }
  return(prepare_for_send(list));

err:
  my_message(ER_OUT_OF_RESOURCES, ER(ER_OUT_OF_RESOURCES),
             MYF(0));	/* purecov: inspected */
  return(1);				/* purecov: inspected */
}


bool Protocol::write()
{
  return(drizzleclient_net_write(&session->net, (unsigned char*) packet->ptr(),
                           packet->length()));
}


/**
  Send \\0 end terminated string.

  @param from	NULL or \\0 terminated string

  @note
    In most cases one should use store(from, length) instead of this function

  @retval
    0		ok
  @retval
    1		error
*/

bool Protocol::store(const char *from, const CHARSET_INFO * const cs)
{
  if (!from)
    return store_null();
  uint32_t length= strlen(from);
  return store(from, length, cs);
}


/**
  Send a set of strings as one long string with ',' in between.
*/

bool Protocol::store(I_List<i_string>* str_list)
{
  char buf[256];
  String tmp(buf, sizeof(buf), &my_charset_bin);
  uint32_t len;
  I_List_iterator<i_string> it(*str_list);
  i_string* s;

  tmp.length(0);
  while ((s=it++))
  {
    tmp.append(s->ptr);
    tmp.append(',');
  }
  if ((len= tmp.length()))
    len--;					// Remove last ','
  return store((char*) tmp.ptr(), len,  tmp.charset());
}


bool Protocol::store(String *str)
{
  return store((char*) str->ptr(), str->length(), str->charset());
}

void Protocol::free()
{
  packet->free();
}


/****************************************************************************
  Functions to handle the simple (default) protocol where everything is
  This protocol is the one that is used by default between the MySQL server
  and client when you are not using prepared statements.

  All data are sent as 'packed-string-length' followed by 'string-data'
****************************************************************************/

void Protocol_text::prepare_for_resend()
{
  packet->length(0);
}

bool Protocol_text::store_null()
{
  char buff[1];
  buff[0]= (char)251;
  return packet->append(buff, sizeof(buff), PACKET_BUFFER_EXTRA_ALLOC);
}


/**
  Auxilary function to convert string to the given character set
  and store in network buffer.
*/

bool Protocol::store_string_aux(const char *from, size_t length,
                                const CHARSET_INFO * const fromcs,
								const CHARSET_INFO * const tocs)
{
  /* 'tocs' is set 0 when client issues SET character_set_results=NULL */
  if (tocs && !my_charset_same(fromcs, tocs) &&
      fromcs != &my_charset_bin &&
      tocs != &my_charset_bin)
  {
    /* Store with conversion */
    return net_store_data((unsigned char*) from, length, fromcs, tocs);
  }
  /* Store without conversion */
  return net_store_data((unsigned char*) from, length);
}


bool Protocol_text::store(const char *from, size_t length,
                          const CHARSET_INFO * const fromcs,
						  const CHARSET_INFO * const tocs)
{
  return store_string_aux(from, length, fromcs, tocs);
}


bool Protocol_text::store(const char *from, size_t length,
                          const CHARSET_INFO * const fromcs)
{
  const CHARSET_INFO * const tocs= default_charset_info;
  return store_string_aux(from, length, fromcs, tocs);
}


bool Protocol_text::store_tiny(int64_t from)
{
  char buff[20];
  return net_store_data((unsigned char*) buff,
			(size_t) (int10_to_str((int) from, buff, -10) - buff));
}


bool Protocol_text::store_short(int64_t from)
{
  char buff[20];
  return net_store_data((unsigned char*) buff,
			(size_t) (int10_to_str((int) from, buff, -10) -
                                  buff));
}


bool Protocol_text::store_long(int64_t from)
{
  char buff[20];
  return net_store_data((unsigned char*) buff,
			(size_t) (int10_to_str((long int)from, buff,
                                               (from <0)?-10:10)-buff));
}


bool Protocol_text::store_int64_t(int64_t from, bool unsigned_flag)
{
  char buff[22];
  return net_store_data((unsigned char*) buff,
			(size_t) (int64_t10_to_str(from,buff,
                                                    unsigned_flag ? 10 : -10)-
                                  buff));
}


bool Protocol_text::store_decimal(const my_decimal *d)
{
  char buff[DECIMAL_MAX_STR_LENGTH];
  String str(buff, sizeof(buff), &my_charset_bin);
  (void) my_decimal2string(E_DEC_FATAL_ERROR, d, 0, 0, 0, &str);
  return net_store_data((unsigned char*) str.ptr(), str.length());
}


bool Protocol_text::store(float from, uint32_t decimals, String *buffer)
{
  buffer->set_real((double) from, decimals, session->charset());
  return net_store_data((unsigned char*) buffer->ptr(), buffer->length());
}


bool Protocol_text::store(double from, uint32_t decimals, String *buffer)
{
  buffer->set_real(from, decimals, session->charset());
  return net_store_data((unsigned char*) buffer->ptr(), buffer->length());
}


bool Protocol_text::store(Field *field)
{
  if (field->is_null())
    return store_null();
  char buff[MAX_FIELD_WIDTH];
  String str(buff,sizeof(buff), &my_charset_bin);
  const CHARSET_INFO * const tocs= default_charset_info;

  field->val_str(&str);

  return store_string_aux(str.ptr(), str.length(), str.charset(), tocs);
}


/**
  @todo
    Second_part format ("%06") needs to change when
    we support 0-6 decimals for time.
*/

bool Protocol_text::store(DRIZZLE_TIME *tm)
{
  char buff[40];
  uint32_t length;
  length= sprintf(buff, "%04d-%02d-%02d %02d:%02d:%02d",
			   (int) tm->year,
			   (int) tm->month,
			   (int) tm->day,
			   (int) tm->hour,
			   (int) tm->minute,
			   (int) tm->second);
  if (tm->second_part)
    length+= sprintf(buff+length, ".%06d",
                                     (int)tm->second_part);
  return net_store_data((unsigned char*) buff, length);
}


bool Protocol_text::store_date(DRIZZLE_TIME *tm)
{
  char buff[MAX_DATE_STRING_REP_LENGTH];
  size_t length= my_date_to_str(tm, buff);
  return net_store_data((unsigned char*) buff, length);
}


/**
  @todo
    Second_part format ("%06") needs to change when
    we support 0-6 decimals for time.
*/

bool Protocol_text::store_time(DRIZZLE_TIME *tm)
{
  char buff[40];
  uint32_t length;
  uint32_t day= (tm->year || tm->month) ? 0 : tm->day;
  length= sprintf(buff, "%s%02ld:%02d:%02d",
			   tm->neg ? "-" : "",
			   (long) day*24L+(long) tm->hour,
			   (int) tm->minute,
			   (int) tm->second);
  if (tm->second_part)
    length+= sprintf(buff+length, ".%06d", (int)tm->second_part);
  return net_store_data((unsigned char*) buff, length);
}
