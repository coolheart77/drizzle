/************************************************************************
Record manager global types

(c) 1994-1996 Innobase Oy

Created 5/30/1994 Heikki Tuuri
*************************************************************************/

#ifndef rem0types_h
#define rem0types_h

/* We define the physical record simply as an array of bytes */
typedef byte	rec_t;

/* Maximum values for various fields (for non-blob tuples) */
#define REC_MAX_N_FIELDS	(1024 - 1)
#define REC_MAX_HEAP_NO		(2 * 8192 - 1)
#define REC_MAX_N_OWNED		(16 - 1)

/* REC_MAX_INDEX_COL_LEN is measured in bytes and is the maximum
indexed column length (or indexed prefix length). It is set to 3*256,
so that one can create a column prefix index on 256 characters of a
TEXT or VARCHAR column also in the UTF-8 charset. In that charset,
a character may take at most 3 bytes.
This constant MUST NOT BE CHANGED, or the compatibility of InnoDB data
files would be at risk! */
#define REC_MAX_INDEX_COL_LEN	768

#endif