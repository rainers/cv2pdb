// Convert DMD CodeView debug information to PDB files
// Copyright (c) 2009-2010 by Rainer Schuetze, All Rights Reserved
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
bool demangleSymbols = true;

int dsym2c(const BYTE* p, int len, char* cname, int maxclen)
{
	const BYTE* end = p + len;
	int zlen, zpos, cpos = 0;

	// decompress symbol
	while (p < end)
	{
		int ch = *p++;
		if(ch == 0)
			break;
		if ((ch & 0xc0) == 0xc0)
		{
			zlen = (ch & 0x7) + 1;
			zpos = ((ch >> 3) & 7) + 1; // + zlen;
			if (zpos > cpos)
				break;
			for (int z = 0; z < zlen; z++)
				cname[cpos + z] = cname[cpos - zpos + z];
			cpos += zlen;
		}
		else if (ch >= 0x80)
		{
			if (p >= end)
				break;
			int ch2 = *p++;
			zlen = (ch2 & 0x7f) | ((ch & 0x38) << 4);
			if (p >= end)
				break;
			int ch3 = *p++;
			zpos = (ch3 & 0x7f) | ((ch & 7) << 7);
			if (zpos > cpos)
				break;
			for(int z = 0; z < zlen; z++)
				cname[cpos + z] = cname[cpos - zpos + z];
			cpos += zlen;
		}
#if 0
		if (ch == 0x80)
		{
			if (p >= end)
				break;
			zlen = *p++ & 0x7f;
			if (p >= end)
				break;
			zpos = *p++ & 0x7f;
			if (zpos > cpos)
				break;
			for(int z = 0; z < zlen; z++)
				cname[cpos + z] = cname[cpos - zpos + z];
			cpos += zlen;
		} 
		else if (ch > 0x80)
		{
			zlen = (ch & 0x7) + 1;
			zpos = ((ch >> 3) & 0xf) - 7; // + zlen;
			for(int z = 0; z < zlen; z++)
				cname[cpos + z] = cname[cpos - zpos + z];
			cpos += zlen;
		}
#endif
		else
			cname[cpos++] = ch;
	}

	cname[cpos] = 0;
	if(demangleSymbols)
		if (cname[0] == '_' && cname[1] == 'D' && isdigit(cname[2]))
			d_demangle(cname, cname, maxclen, true);

#if 1
	for(int i = 0; i < cpos; i++)
		if (cname[i] == '.')
			cname[i] = dotReplacementChar;
#endif

	return cpos;
}

int pstrlen(const BYTE* &p)
{
	int len = *p++;
	if(len == 0xff && *p == 0)
	{
		len = p[1] | (p[2] << 8);
		p += 3;
	}
	return len;
}

int pstrmemlen(const BYTE* p)
{
    const BYTE* q = p;
    int len = pstrlen(p);
    return len + (p - q);
}

int dstrlen(const BYTE* &p, bool cstr)
{
	if(cstr)
		return strlen((const char*)p);
	return pstrlen(p);
}

char* p2c(const BYTE* p, int idx)
{
	static char cname[4][2560];
	int len = pstrlen(p);

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
	int len = pstrlen(s);
	memcpy(p, s, len);
	p[len] = 0;
	return len + 1;
}

int pstrcpy(BYTE* p, const BYTE* s)
{
	const BYTE* src = s;
	int len = pstrlen(s);
	for(int i = 0; i <= s - src; i++)
		*p++ = src[i];

	for(int i = 0; i < len; i++)
		if (s[i] == '.')
		{
			//p[i++] = ':';
			p[i] = dotReplacementChar;
		}
		else
			p[i] = s[i];
	return len + src - s; // *(BYTE*) memcpy (p, s, *s + 1) + 1;
}

int dmemcmp(const void* v1, const void* v2, int len)
{
	const BYTE* p1 = (const BYTE*) v1;
	const BYTE* p2 = (const BYTE*) v2;
	for(int i = 0; i < len; i++)
	{
		int b1 = p1[i];
		int b2 = p2[i];
		if(b1 == '.')
			b1 = dotReplacementChar;
		if(b2 == '.')
			b2 = dotReplacementChar;
		if(b1 != b2)
			return b2 - b1;
	}
	return 0;
}

int pstrcpy(p_string& p, const p_string& s)
{
	return *(BYTE*) memcpy (&p, &s, s.namelen + 1) + 1;
}

int pstrcmp(const BYTE* p1, const BYTE* p2)
{
	int len1 = pstrlen(p1);
	int len2 = pstrlen(p2);
	if (len1 != len2)
		return len2 - len1;
	return dmemcmp(p1, p2, len1);
}

bool p2ccmp(const BYTE* pp, const char* cp)
{
	int len = strlen(cp);
	int plen = pstrlen(pp);
	if (len != plen)
		return false;
	return dmemcmp(pp, cp, len) == 0;
}

bool p2ccmp(const p_string& pp, const char* cp)
{
	return p2ccmp(&pp.namelen, cp);
}

bool dstrcmp(const BYTE* s1, bool cstr1, const BYTE* s2, bool cstr2)
{
	int len1 = dstrlen(s1, cstr1);
	int len2 = dstrlen(s2, cstr2);
	if(len1 != len2)
		return false;
	return dmemcmp(s1, s2, len1) == 0;
}

int pstrcpy_v(bool v3, BYTE* d, const BYTE* s)
{
	if (!v3)
		return pstrcpy(d, s);

	int len = pstrlen(s);
	int clen = dsym2c(s, len, (char*) d, kMaxNameLen) + 1;

	return clen;
}

int cstrcpy_v(bool v3, BYTE* d, const char* s)
{
	int len = strlen(s);
	if(!v3)
	{
		assert(len < 256);
		*d++ = len;
	}

	memcpy(d, s, len + 1);
	return len + 1;
}

