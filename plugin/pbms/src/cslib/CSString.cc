/* Copyright (c) 2008 PrimeBase Technologies GmbH, Germany
 *
 * PrimeBase Media Stream for MySQL
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Original author: Paul McCullagh (H&G2JCtL)
 * Continued development: Barry Leslie
 *
 * 2007-06-15
 *
 * CORE SYSTEM STORAGE
 * Basic storage structures.
 *
 */

#include "CSConfig.h"
#include <inttypes.h>

#include <assert.h>
#include <string.h>
#include <ctype.h>

#include "CSGlobal.h"
#include "CSUTF8.h"
#include "CSStorage.h"
#include "CSMemory.h"
#include "CSString.h"
#include "CSStrUtil.h"
#include "CSGlobal.h"


/*
 * ---------------------------------------------------------------
 * Core System String Buffers
 */

CSStringBuffer_::CSStringBuffer_():
iBuffer(NULL),
iGrow(0),
iSize(0),
myStrLen(0)
{
	iGrow = 20;
}

CSStringBuffer_::CSStringBuffer_(uint32_t grow):
iBuffer(NULL),
iGrow(0),
iSize(0),
myStrLen(0)
{
	iGrow = grow;
}

CSStringBuffer_::~CSStringBuffer_()
{
	clear();
}

void CSStringBuffer_::clear()
{
	if (iBuffer)
		cs_free(iBuffer);
	iBuffer = NULL;
	iSize = 0;
	myStrLen = 0; 
}

void CSStringBuffer_::append(char ch)
{
	if (iSize == myStrLen) {
		cs_realloc((void **) &iBuffer, iSize + iGrow);
		iSize += iGrow;
	}
	iBuffer[myStrLen] = ch;
	myStrLen++;
}

void CSStringBuffer_::append(const char *str, size_t len)
{
	if (myStrLen + len > iSize) {
		size_t add = len;
		
		if (add < iGrow)
			add = iGrow;
		cs_realloc((void **) &iBuffer, iSize + add);
		iSize += add;
	}
	memcpy(iBuffer + myStrLen, str, len);
	myStrLen += len;
}

void CSStringBuffer_::append(int value)
{
	char buffer[100];

	snprintf(buffer, 100, "%"PRId32"", value);
	append(buffer);
}

void CSStringBuffer_::append(uint32_t value)
{
	char buffer[100];

	snprintf(buffer, 100, "%"PRIu32"", value);
	append(buffer);
}

char *CSStringBuffer_::getCString()
{
	if (iSize == myStrLen) {
		cs_realloc((void **) &iBuffer, iSize + 1);
		iSize++;
	}
	iBuffer[myStrLen] = 0;
	return iBuffer;
}

char *CSStringBuffer_::take()
{
	char *buf;

	cs_realloc((void **) &iBuffer, myStrLen + 1);
	iSize = myStrLen + 1;
	iBuffer[myStrLen] = 0;

	buf = iBuffer;
	iBuffer = NULL;
	iSize = 0;
	myStrLen = 0; 
	return buf;
}

void CSStringBuffer_::setLength(uint32_t len)
{
	if (len > iSize) {
		cs_realloc((void **) &iBuffer, len + 1);
		iSize = len+1;
	}
	myStrLen = len;
}

uint32_t CSStringBuffer_::ignore(uint32_t pos, char ch)
{
	while (pos < myStrLen && iBuffer[pos] == ch)
		pos++;
	return pos;
}

uint32_t CSStringBuffer_::find(uint32_t pos, char ch)
{
	while (pos < myStrLen && iBuffer[pos] != ch)
		pos++;
	return pos;
}

uint32_t CSStringBuffer_::trim(uint32_t pos, char ch)
{
	while (pos > 0 && iBuffer[pos-1] == ch)
		pos--;
	return pos;
}

CSString *CSStringBuffer_::substr(uint32_t pos, uint32_t len)
{
	CSString *s = CSString::newString(iBuffer + pos, len);

	return s;
}

/*
 * ---------------------------------------------------------------
 * Generic Strings
 */

CSString *CSString::newString(const char *cstr)
{
	return CSCString::newString(cstr);
}

CSString *CSString::newString(const char *bytes, uint32_t len)
{
	return CSCString::newString(bytes, len);
}

CSString *CSString::newString(CSStringBuffer *sb)
{
	return CSCString::newString(sb);
}

CSString *CSString::concat(CSString *cat_str)
{
	CSString *new_str = NULL;
	uint32_t len_a, len_b;
	
	enter_();
	len_a = length();
	len_b = cat_str->length();
	new_str = clone(len_a + len_b);
	push_(new_str);
	
	for (uint32_t i=0; i<len_b; i++)
		new_str->setCharAt(len_a+i, cat_str->charAt(i));

	pop_(new_str);
	return_(new_str);
}

CSString *CSString::concat(const char *cat_str)
{
	CSString *new_str = NULL;
	uint32_t len_a, len_b;
	
	enter_();
	len_a = length();
	len_b = strlen(cat_str);
	new_str = clone(len_a + len_b);
	push_(new_str);
	
	for (uint32_t i=0; i<len_b; i++)
		new_str->setCharAt(len_a+i, cat_str[i]);

	pop_(new_str);
	return_(new_str);
}

CSString *CSString::toUpper()
{
	CSString *new_str = NULL;
	uint32_t len;

	enter_();
	new_str = clone();
	push_(new_str);
	
	len = new_str->length();
	for (uint32_t i=0; i<len; i++)
		new_str->setCharAt(i, upperCharAt(i));

	pop_(new_str);
	return_(new_str);
}

uint32_t CSString::hashKey()
{
	register uint32_t h = 0, g;
	
	for (uint32_t i=0; i<length(); i++) {
		h = (h << 4) + (uint32_t) upperCharAt(i);
		if ((g = (h & 0xF0000000)))
			h = (h ^ (g >> 24)) ^ g;
	}

	return (h);
}

uint32_t CSString::locate(const char *w_cstr, s_int count)
{
	s_int len = length();
	s_int i;

	if (count >= 0) {
		i = 0;
		while (i < len) {
			if (startsWith((uint32_t) i, w_cstr)) {
				count--;
				if (!count)
					return i;
			}
			i++;
		}
	}
	else {
		count = -count;
		i = len - (s_int) strlen(w_cstr);
		while (i >= 0) {
			if (startsWith((uint32_t) i, w_cstr)) {
				count--;
				if (!count)
					return i;
			}
			i--;
		}
	}
	return i;
}

uint32_t CSString::locate(uint32_t pos, const char *w_cstr)
{
	uint32_t len = length();
	uint32_t i;

	if (pos > len)
		return len;
	i = pos;
	while (i < len) {
		if (startsWith(i, w_cstr))
			return i;
		i++;
	}
	return i;
}

uint32_t CSString::locate(uint32_t pos, CS_CHAR ch)
{
	uint32_t len = length();
	uint32_t i;

	if (pos > len)
		return len;
	i = pos;
	while (i < len) {
		if (charAt(i) == ch)
			return i;
		i++;
	}
	return i;
}

uint32_t CSString::skip(uint32_t pos, CS_CHAR ch)
{
	uint32_t len = length();
	uint32_t i;

	if (pos > len)
		return len;
	i = pos;
	while (i < len) {
		if (charAt(i) != ch)
			return i;
		i++;
	}
	return i;
}

CSString *CSString::substr(uint32_t index, uint32_t size)
{
	return clone(index, size);
}

CSString *CSString::substr(uint32_t index)
{
	return clone(index, length() - index);
	
}

CSString *CSString::left(const char *w_cstr, s_int count)
{
	uint32_t idx = locate(w_cstr, count);

	if (idx == (uint32_t)-1)
		return CSCString::newString("");
	return substr(0, idx);
}

CSString *CSString::left(const char *w_cstr)
{
	return left(w_cstr, 1);
}

CSString *CSString::right(const char *w_cstr, s_int count)
{
	uint32_t idx = locate(w_cstr, count);

	if (idx == (uint32_t)-1) {
		this->retain();
		return this;
	}
	
	if (idx == length())
		return newString("");
		
	return substr(idx + strlen(w_cstr));
}

CSString *CSString::right(const char *w_cstr)
{
	return right(w_cstr, 1);
}

bool CSString::startsWith(const char *w_str)
{
	return startsWith(0, w_str);
}

bool CSString::endsWith(const char *w_str)
{
	return startsWith(length() - strlen(w_str), w_str);
}

uint32_t CSString::nextPos(uint32_t pos)
{
	if (pos >= length())
		return length();
	return pos + 1;
}

CSString *CSString::clone(uint32_t len)
{
	return clone(0, len);
}

CSString *CSString::clone()
{
	return clone(0, length());
}

bool CSString::equals(const char *str)
{
	uint32_t len = length();
	uint32_t i;
	
	for (i=0; i<len && *str; i++) {
		if (charAt(i) != *str)
			return false;
		str++;
	}
	return i==len && !*str;
}

/*
 * ---------------------------------------------------------------
 * Standard C String
 */

CSCString::CSCString():
CSString(),
myCString(NULL),
myStrLen(0)
{
}

CSCString::~CSCString()
{
	if (myCString)
		cs_free(myCString);
}

const char *CSCString::getCString()
{
	return myCString;
}

CS_CHAR CSCString::charAt(uint32_t pos)
{
	if (pos < myStrLen)
		return (CS_CHAR) (unsigned char) myCString[pos];
	return (CS_CHAR) 0;
}

CS_CHAR CSCString::upperCharAt(uint32_t pos)
{
	if (pos < myStrLen)
		return (CS_CHAR) (unsigned char) toupper(myCString[pos]);
	return (CS_CHAR) 0;
}

void CSCString::setCharAt(uint32_t pos, CS_CHAR ch)
{
	if (pos < myStrLen)
		myCString[pos] = (unsigned char) ch;
}

int CSCString::compare(const char *val, uint32_t len)
{
	const char *pa = myCString, *pb = val;
	int r = 0;
	
	enter_();
	
	if (pa && pb) {
		while (*pa && *pb && len) {
			r = toupper(*pa) - toupper(*pb);
			if (r != 0)
				break;
			pa++;
			pb++;
			len--;
		}
		if (len)
			r = toupper(*pa) - toupper(*pb);
	}

	return_(r);
}

int CSCString::compare(CSString *val)
{
	return compare(val->getCString(), (uint32_t)-1);
}

bool CSCString::startsWith(uint32_t index, const char *w_str)
{
	uint32_t len = strlen(w_str);
	char *str;
	
	if (index > myStrLen)
		index = myStrLen;
	str = myCString + index;
	for (uint32_t i=0; i<len && *str; i++) {
		if (*str != *w_str)
			return false;
		str++;
		w_str++;
	}
	return (*w_str == 0);
}

void CSCString::setLength(uint32_t len)
{
	cs_realloc((void **) &myCString, len+1);
	myCString[len] = 0;
	myStrLen = len;
}

CSString *CSCString::clone(uint32_t pos, uint32_t len)
{
	CSCString *str = NULL;
	
	enter_();
	new_(str, CSCString());
	push_(str);
	
	str->myCString = (char *) cs_malloc(len + 1);
	str->myStrLen = len;
	if (pos > myStrLen)
		pos = myStrLen;
	if (len > myStrLen - pos) {
		/* More space has been allocated than required.
		 * It may be that this space will be used up.
		 * Set the zero terminator at the end
		 * of the space!
		 */
		str->myCString[len] = 0;
		len = myStrLen - pos;
	}
	memcpy(str->myCString, myCString+pos, len);
	str->myCString[len] = 0;

	pop_(str);
	return_(str);
}

CSObject *CSCString::getKey()
{
	return (CSObject *) this;
}

int CSCString::compareKey(CSObject *key)
{
	return compare((CSString *) key);
}

CSCString *CSCString::newString(const char *cstr)
{
	CSCString *str;
	
	enter_();
	new_(str, CSCString());
	push_(str);
	str->myCString = cs_strdup(cstr);
	str->myStrLen = strlen(cstr);
	pop_(str);
	return_(str);
}

CSCString *CSCString::newString(const char *bytes, uint32_t len)
{
	CSCString *str;
	
	enter_();
	new_(str, CSCString());
	push_(str);
	str->myStrLen = len;
	str->myCString = (char *) cs_malloc(len+1);
	memcpy(str->myCString, bytes, len);
	str->myCString[len] = 0;
	pop_(str);
	return_(str);
}

CSCString *CSCString::newString(CSStringBuffer *sb)
{
	CSCString *str;
	
	enter_();
	push_(sb);
	new_(str, CSCString());
	push_(str);
	str->myCString = sb->take();
	str->myStrLen = sb->length();
	pop_(str);
	release_(sb);
	return_(str);
}

