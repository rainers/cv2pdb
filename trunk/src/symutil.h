// Convert DMD CodeView debug information to PDB files
// Copyright (c) 2009-2010 by Rainer Schuetze, All Rights Reserved
//
// License for redistribution is given by the Artistic License 2.0
// see file LICENSE for further details

#ifndef __SYMUTIL_H__
#define __SYMUTIL_H__

#include <windows.h>

struct p_string;

int dsym2c(const BYTE* p, int len, char* cname, int maxclen);

int pstrmemlen(const BYTE* p);
int pstrlen(const BYTE* &p);
char* p2c(const BYTE* p, int idx = 0);
char* p2c(const p_string& p, int idx = 0);
int p2ccpy(char* p, const BYTE* s);
int pstrcpy(BYTE* p, const BYTE* s);
int pstrcpy(p_string& p, const p_string& s);
int pstrcmp(const BYTE* p1, const BYTE* p2);
bool p2ccmp(const BYTE* pp, const char* cp);
bool p2ccmp(const p_string& pp, const char* cp);
int pstrcpy_v(bool v3, BYTE* d, const BYTE* s);
int cstrcpy_v(bool v3, BYTE* d, const char* s);
bool dstrcmp(const BYTE* s1, bool cstr1, const BYTE* s2, bool cstr2);

extern char dotReplacementChar;

#endif //__SYMUTIL_H__
