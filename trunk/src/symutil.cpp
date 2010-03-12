// Convert DMD CodeView debug information to PDB files
// Copyright (c) 2009 by Rainer Schuetze, All Rights Reserved
//
// License for redistribution is given by the Artistic License 2.0
// see file LICENSE for further details

#include "symutil.h"
#include "demangle.h"

extern "C" {
#include "mscvpdb.h"
}

#include <assert.h>

char dotReplacementChar = '@';

int dsym2c(const BYTE* p, BYTE len, char* cname, int maxclen)
{
	int zlen, zpos, cpos = 0;

	// decompress symbol
	while (len-- > 0)
	{
		int ch = *p++;
		if (ch == 0x80)
		{
			if (len-- <= 0)
				break;
			zlen = *p++ & 0x7f;
			if (len-- <= 0)
				break;
			zpos = *p++ & 0x7f;
			if (zpos > cpos)
				break;
			for(int z = 0; z < zlen; z++)
				cname[cpos + z] = cname[cpos - zpos + z];
			cpos += zlen;
			break;
		} 
		else if (ch > 0x80)
		{
			zlen = (ch & 0x7) + 1;
			zpos = ((ch >> 3) & 0xf) - 7; // + zlen;
			for(int z = 0; z < zlen; z++)
				cname[cpos + z] = cname[cpos - zpos + z];
			cpos += zlen;
		}
		else
			cname[cpos++] = ch;
	}

	cname[cpos] = 0;
	if (cname[0] == '_' && cname[1] == 'D' && isdigit(cname[2]))
		d_demangle(cname, cname, maxclen, true);

#if 1
	for(int i = 0; i < cpos; i++)
		if (cname[i] == '.')
			cname[i] = dotReplacementChar;
#endif

	return cpos;
}

char* p2c(const BYTE* p, int idx)
{
	static char cname[4][2560];
	int len = *p++;

#if 1
	memcpy(cname[idx], p, len);
	cname[idx][len] = 0;
#else
	dsym2c(p, len, cname[idx], 2560);
#endif
	return cname[idx];
}

char* p2c(const p_string& p, int idx)
{
	return p2c(&p.namelen, idx);
}

int p2ccpy(char* p, const BYTE* s)
{
	memcpy(p, s + 1, *s);
	p[*s] = 0;
	return *s + 1;
}

int pstrcpy(BYTE* p, const BYTE* s)
{
	int len = *p = *s;
	for(int i = 1; i <= len; i++)
		if (s[i] == '.')
		{
			//p[i++] = ':';
			p[i] = dotReplacementChar;
		}
		else
			p[i] = s[i];
	return len + 1; // *(BYTE*) memcpy (p, s, *s + 1) + 1;
}

int pstrcpy(p_string& p, const p_string& s)
{
	return *(BYTE*) memcpy (&p, &s, s.namelen + 1) + 1;
}

int pstrcmp(const BYTE* p1, const BYTE* p2)
{
	if (*p1 != *p2)
		return *p2 - *p1;
	return memcmp(p1 + 1, p2 + 1, *p1);
}

bool p2ccmp(const BYTE* pp, const char* cp)
{
	int len = strlen(cp);
	if (len != *pp)
		return false;
	return memcmp(pp + 1, cp, len) == 0;
}
bool p2ccmp(const p_string& pp, const char* cp)
{
	return p2ccmp(&pp.namelen, cp);
}

int pstrcpy_v(bool v3, BYTE* d, const BYTE* s)
{
	if (!v3)
		return pstrcpy(d, s);

	int len = *s++;
	int clen = dsym2c(s, len, (char*) d, 1000) + 1;

	return clen;
}

int cstrcpy_v(bool v3, BYTE* d, const char* s)
{
	int len = strlen(s);
	if(!v3)
		*d++ = len;
	else
		assert(len < 256);

	memcpy(d, s, len + 1);
	return len + 1;
}

