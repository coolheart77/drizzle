/* Copyright (C) 2000-2004, 2006 MySQL AB

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
  Gives a approximated number of how many records there is between two keys.
  Used when optimizing querries.
 */

#include "myisamdef.h"

static ha_rows _mi_record_pos(MI_INFO *, const unsigned char *, key_part_map,
                              enum ha_rkey_function);
static double _mi_search_pos(MI_INFO *,MI_KEYDEF *,unsigned char *, uint,uint,my_off_t);
static uint32_t _mi_keynr(MI_INFO *info,MI_KEYDEF *,unsigned char *, unsigned char *,uint32_t *);

/*
  Estimate how many records there is in a given range

  SYNOPSIS
    mi_records_in_range()
    info		MyISAM handler
    inx			Index to use
    min_key		Min key. Is = 0 if no min range
    max_key		Max key. Is = 0 if no max range

  NOTES
    We should ONLY return 0 if there is no rows in range

  RETURN
    HA_POS_ERROR  error (or we can't estimate number of rows)
    number	  Estimated number of rows
*/

ha_rows mi_records_in_range(MI_INFO *info, int inx,
                            key_range *min_key, key_range *max_key)
{
  ha_rows start_pos,end_pos,res;

  if ((inx = _mi_check_index(info,inx)) < 0)
    return(HA_POS_ERROR);

  if (fast_mi_readinfo(info))
    return(HA_POS_ERROR);
  info->update&= (HA_STATE_CHANGED+HA_STATE_ROW_CHANGED);
  if (info->s->concurrent_insert)
    pthread_rwlock_rdlock(&info->s->key_root_lock[inx]);

  switch(info->s->keyinfo[inx].key_alg){
  case HA_KEY_ALG_BTREE:
  default:
    start_pos= (min_key ?  _mi_record_pos(info, min_key->key,
                                          min_key->keypart_map, min_key->flag)
                        : (ha_rows) 0);
    end_pos=   (max_key ?  _mi_record_pos(info, max_key->key,
                                          max_key->keypart_map, max_key->flag)
                        : info->state->records + (ha_rows) 1);
    res= (end_pos < start_pos ? (ha_rows) 0 :
          (end_pos == start_pos ? (ha_rows) 1 : end_pos-start_pos));
    if (start_pos == HA_POS_ERROR || end_pos == HA_POS_ERROR)
      res=HA_POS_ERROR;
  }

  if (info->s->concurrent_insert)
    pthread_rwlock_unlock(&info->s->key_root_lock[inx]);
  fast_mi_writeinfo(info);

  return(res);
}


	/* Find relative position (in records) for key in index-tree */

static ha_rows _mi_record_pos(MI_INFO *info, const unsigned char *key,
                              key_part_map keypart_map,
			      enum ha_rkey_function search_flag)
{
  uint32_t inx=(uint) info->lastinx, nextflag, key_len;
  MI_KEYDEF *keyinfo=info->s->keyinfo+inx;
  unsigned char *key_buff;
  double pos;

  assert(keypart_map);

  key_buff=info->lastkey+info->s->base.max_key_length;
  key_len=_mi_pack_key(info,inx,key_buff,(unsigned char*) key, keypart_map,
		       (HA_KEYSEG**) 0);
  nextflag=myisam_read_vec[search_flag];
  if (!(nextflag & (SEARCH_FIND | SEARCH_NO_FIND | SEARCH_LAST)))
    key_len=USE_WHOLE_KEY;

  /*
    my_handler.c:ha_compare_text() has a flag 'skip_end_space'.
    This is set in my_handler.c:ha_key_cmp() in dependence on the
    compare flags 'nextflag' and the column type.

    TEXT columns are of type HA_KEYTYPE_VARTEXT. In this case the
    condition is skip_end_space= ((nextflag & (SEARCH_FIND |
    SEARCH_UPDATE)) == SEARCH_FIND).

    SEARCH_FIND is used for an exact key search. The combination
    SEARCH_FIND | SEARCH_UPDATE is used in write/update/delete
    operations with a comment like "Not real duplicates", whatever this
    means. From the condition above we can see that 'skip_end_space' is
    always false for these operations. The result is that trailing space
    counts in key comparison and hence, emtpy strings ('', string length
    zero, but not NULL) compare less that strings starting with control
    characters and these in turn compare less than strings starting with
    blanks.

    When estimating the number of records in a key range, we request an
    exact search for the minimum key. This translates into a plain
    SEARCH_FIND flag. Using this alone would lead to a 'skip_end_space'
    compare. Empty strings would be expected above control characters.
    Their keys would not be found because they are located below control
    characters.

    This is the reason that we add the SEARCH_UPDATE flag here. It makes
    the key estimation compare in the same way like key write operations
    do. Olny so we will find the keys where they have been inserted.

    Adding the flag unconditionally does not hurt as it is used in the
    above mentioned condition only. So it can safely be used together
    with other flags.
  */
  pos=_mi_search_pos(info,keyinfo,key_buff,key_len,
		     nextflag | SEARCH_SAVE_BUFF | SEARCH_UPDATE,
		     info->s->state.key_root[inx]);
  if (pos >= 0.0)
  {
    return((uint32_t) (pos*info->state->records+0.5));
  }
  return(HA_POS_ERROR);
}


	/* This is a modified version of _mi_search */
	/* Returns offset for key in indextable (decimal 0.0 <= x <= 1.0) */

static double _mi_search_pos(register MI_INFO *info,
			     register MI_KEYDEF *keyinfo,
			     unsigned char *key, uint32_t key_len, uint32_t nextflag,
			     register my_off_t pos)
{
  int flag;
  uint32_t nod_flag, keynr, max_keynr= 0;
  bool after_key;
  unsigned char *keypos,*buff;
  double offset;

  if (pos == HA_OFFSET_ERROR)
    return(0.5);

  if (!(buff=_mi_fetch_keypage(info,keyinfo,pos,DFLT_INIT_HITS,info->buff,1)))
    goto err;
  flag=(*keyinfo->bin_search)(info,keyinfo,buff,key,key_len,nextflag,
			      &keypos,info->lastkey, &after_key);
  nod_flag=mi_test_if_nod(buff);
  keynr=_mi_keynr(info,keyinfo,buff,keypos,&max_keynr);

  if (flag)
  {
    if (flag == MI_FOUND_WRONG_KEY)
      return(-1);				/* error */
    /*
      Didn't found match. keypos points at next (bigger) key
      Try to find a smaller, better matching key.
      Matches keynr + [0-1]
    */
    if (flag > 0 && ! nod_flag)
      offset= 1.0;
    else if ((offset=_mi_search_pos(info,keyinfo,key,key_len,nextflag,
				    _mi_kpos(nod_flag,keypos))) < 0)
      return(offset);
  }
  else
  {
    /*
      Found match. Keypos points at the start of the found key
      Matches keynr+1
    */
    offset=1.0;					/* Matches keynr+1 */
    if ((nextflag & SEARCH_FIND) && nod_flag &&
	((keyinfo->flag & (HA_NOSAME | HA_NULL_PART)) != HA_NOSAME ||
	 key_len != USE_WHOLE_KEY))
    {
      /*
        There may be identical keys in the tree. Try to match on of those.
        Matches keynr + [0-1]
      */
      if ((offset=_mi_search_pos(info,keyinfo,key,key_len,SEARCH_FIND,
				 _mi_kpos(nod_flag,keypos))) < 0)
	return(offset);			/* Read error */
    }
  }
  return((keynr+offset)/(max_keynr+1));
err:
  return (-1.0);
}


	/* Get keynummer of current key and max number of keys in nod */

static uint32_t _mi_keynr(MI_INFO *info, register MI_KEYDEF *keyinfo, unsigned char *page,
                      unsigned char *keypos, uint32_t *ret_max_key)
{
  uint32_t nod_flag,keynr,max_key;
  unsigned char t_buff[MI_MAX_KEY_BUFF],*end;

  end= page+mi_getint(page);
  nod_flag=mi_test_if_nod(page);
  page+=2+nod_flag;

  if (!(keyinfo->flag & (HA_VAR_LENGTH_KEY | HA_BINARY_PACK_KEY)))
  {
    *ret_max_key= (uint) (end-page)/(keyinfo->keylength+nod_flag);
    return (uint) (keypos-page)/(keyinfo->keylength+nod_flag);
  }

  max_key=keynr=0;
  t_buff[0]=0;					/* Safety */
  while (page < end)
  {
    if (!(*keyinfo->get_key)(keyinfo,nod_flag,&page,t_buff))
      return 0;					/* Error */
    max_key++;
    if (page == keypos)
      keynr=max_key;
  }
  *ret_max_key=max_key;
  return(keynr);
}