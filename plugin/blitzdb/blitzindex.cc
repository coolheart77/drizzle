/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2009 - 2010 Toru Maesaka
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

#include "ha_blitz.h"

/* Unlike the data dictionary, don't tune the btree by default
   since the default configuration satisfies BlitzDB's default
   performance requirements. Tuning parameters will be made dynamic
   in the upcoming releases. */
int BlitzTree::open(const char *path, const int key_num, int mode) {
  char buf[FN_REFLEN];

  if ((btree = tcbdbnew()) == NULL)
    return HA_ERR_OUT_OF_MEM;

  if ((keybuf = (char *)malloc(BLITZ_MAX_KEY_LEN)) == NULL) {
    tcbdbdel(btree);
    return HA_ERR_OUT_OF_MEM;
  }

  if (!tcbdbsetmutex(btree)) {
    tcbdbdel(btree);
    return HA_ERR_CRASHED_ON_USAGE;
  }

  if (!tcbdbsetcmpfunc(btree, blitz_keycmp_cb, this)) {
    tcbdbdel(btree);
    return HA_ERR_CRASHED_ON_USAGE;
  }

  snprintf(buf, FN_REFLEN, "%s_%02d%s", path, key_num, BLITZ_INDEX_EXT);

  if (!tcbdbopen(btree, buf, mode)) {
    tcbdbdel(btree);
    return HA_ERR_CRASHED_ON_USAGE;
  }

  return 0;
}

/* Similar to UNIX touch(1) but generates a TCBDB file. */
int BlitzTree::create(const char *path, const int key_num) {
  int rv;

  if ((rv = this->open(path, key_num, (BDBOWRITER | BDBOCREAT))) != 0)
    return rv;

  if ((rv = this->close()) != 0)
    return rv;

  return rv;
}

int BlitzTree::drop(const char *path, const int key_num) {
  char buf[FN_REFLEN];
  snprintf(buf, FN_REFLEN, "%s_%02d%s", path, key_num, BLITZ_INDEX_EXT);
  return unlink(buf);
}

int BlitzTree::rename(const char *from, const char *to, const int key_num) {
  char from_buf[FN_REFLEN];
  char to_buf[FN_REFLEN];

  snprintf(from_buf, FN_REFLEN, "%s_%02d%s", from, key_num, BLITZ_INDEX_EXT);
  snprintf(to_buf, FN_REFLEN, "%s_%02d%s", to, key_num, BLITZ_INDEX_EXT);

  return std::rename(from_buf, to_buf);
}

int BlitzTree::close(void) {
  assert(btree);

  if (!tcbdbclose(btree)) {
    tcbdbdel(btree);
    return HA_ERR_CRASHED_ON_USAGE;
  }

  free(keybuf);
  tcbdbdel(btree);
  return 0;
}

int BlitzTree::create_cursor(void) {
  bt_cursor = tcbdbcurnew(btree);
  return (bt_cursor != NULL) ? 0 : HA_ERR_OUT_OF_MEM;
}

void BlitzTree::destroy_cursor(void) {
  tcbdbcurdel(bt_cursor);
  bt_cursor = NULL;
}

int BlitzTree::write(const char *key, const size_t klen,
                     const char *val, const size_t vlen) {
  return (tcbdbput(btree, key, klen, val, vlen)) ? 0 : 1;
}

int BlitzTree::write_unique(const char *key, const size_t klen,
                            const char *val, const size_t vlen) {
  int rv = 0;
  size_t total = klen + sizeof(uint16_t) + vlen;

  if (total > keybuf_len) {
    if ((keybuf = (char *)realloc(keybuf, total)) == NULL) {
      errno = HA_ERR_OUT_OF_MEM;
      return errno;
    }
    keybuf_len = total;
  }

  /* Serialize a key for storing in the B+Tree. We merge the key
     and value so that we only use the internal nodes of the B+Tree.
     This is to avoid reading leaf nodes and thus reduce total IO. */
  char *pos = keybuf;
  memcpy(pos, key, klen);
  pos += klen;
  int2store(pos, (uint16_t)vlen);
  pos += sizeof(uint16_t);
  memcpy(pos, val, vlen);

  if (!tcbdbputkeep(btree, keybuf, total, "", 0)) {
    if (tcbdbecode(btree) == TCEKEEP) {
      errno = HA_ERR_FOUND_DUPP_KEY;
      rv = HA_ERR_FOUND_DUPP_KEY;
    }
  }
  return rv;
}

char *BlitzTree::first_key(int *row_len) {
  char *key;

  /* Means that the index is empty. */
  if (!tcbdbcurfirst(bt_cursor))
    return NULL;

  key = (char *)tcbdbcurkey(bt_cursor, row_len);
  return key;
}

int BlitzTree::delete_all(void) {
  return (tcbdbvanish(btree)) ? 0 : 1;
}

uint64_t BlitzTree::records(void) {
  return tcbdbrnum(btree);
}
