///////////////////////////////////////////////////////////////////////////////
//
// DViewHelper - Expression Evaluator for the D string and object class
// Copyright (c) 2009 by Rainer Schuetze, All Rights Reserved
//
// License for redistribution is given by the Artistic License 2.0
// see file LICENSE for further details
//
// Compile the DLL and add this to AUTOEXP.DAT in section [AutoExpand]
// string=$ADDIN(<path to the DLL>\dviewhelper.dll,_DStringView@28)
//
///////////////////////////////////////////////////////////////////////////////

#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <stdio.h>

extern "C" {

// Copied from MSDN
struct DEBUGHELPER
{
	DWORD dwVersion;
	BOOL      (WINAPI *ReadDebuggeeMemory)(DEBUGHELPER *pThis, DWORD dwAddr, DWORD nWant, VOID* pWhere, DWORD *nGot);
	// from here only when dwVersion >= 0x20000
	DWORDLONG (WINAPI *GetRealAddress)(DEBUGHELPER *pThis);
	BOOL      (WINAPI *ReadDebuggeeMemoryEx)(DEBUGHELPER *pThis, DWORDLONG qwAddr, DWORD nWant, VOID* pWhere, DWORD *nGot);
	int       (WINAPI *GetProcessorType)(DEBUGHELPER *pThis);
};

struct DString
{
    DWORD length;
    DWORD data;
};

__declspec(dllexport)
HRESULT WINAPI DStringView(DWORD dwAddress, DEBUGHELPER *pHelper, int nBase, BOOL bUniStrings, 
                           char *pResult, size_t max, DWORD reserved)
{
	// Get the string struct
	DString dstr;
	DWORD read;
	if (pHelper->ReadDebuggeeMemory(pHelper, dwAddress, sizeof(dstr), &dstr, &read) != S_OK) 
	{
		strncpy(pResult,"Cannot access struct", max);
		return S_OK;
	}
	if (dstr.length == 0) 
	{
		strncpy(pResult,"\"\"", max);
		return S_OK;
	}

	DWORD cnt = (dstr.length < max - 3 ? dstr.length : max - 3);
	if (pHelper->ReadDebuggeeMemory(pHelper, dstr.data, cnt, pResult + 1, &read) != S_OK) 
	{
		strncpy(pResult,"Cannot access data", max);
		return S_OK;
	}

	pResult[0] = '\"';
	pResult[cnt+1] = '\"';
	pResult[cnt+2] = 0;
	return S_OK;
}

__declspec(dllexport)
HRESULT WINAPI DObjectView(DWORD dwAddress, DEBUGHELPER *pHelper, int nBase, BOOL bUniStrings, 
                           char *pResult, size_t max, DWORD reserved)
{
	if(dwAddress == 0)
	{
		strncpy(pResult,"null", max);
		return S_OK;
	}

	DWORD read;
	DWORD vtablePtr;
	if (pHelper->ReadDebuggeeMemory(pHelper, dwAddress, sizeof(vtablePtr), &vtablePtr, &read) != S_OK) 
	{
		strncpy(pResult,"Cannot access object", max);
		return S_OK;
	}
	DWORD classinfoPtr;
	if (pHelper->ReadDebuggeeMemory(pHelper, vtablePtr, sizeof(vtablePtr), &classinfoPtr, &read) != S_OK) 
	{
		strncpy(pResult,"Cannot access vtable", max);
		return S_OK;
	}
	DString dstr;
	if (pHelper->ReadDebuggeeMemory(pHelper, classinfoPtr + 16, sizeof(dstr), &dstr, &read) != S_OK) 
	{
		strncpy(pResult,"Cannot access class info", max);
		return S_OK;
	}

	DWORD cnt = (dstr.length < max - 1 ? dstr.length : max - 1);
	if (pHelper->ReadDebuggeeMemory(pHelper, dstr.data, cnt, pResult, &read) != S_OK) 
	{
		strncpy(pResult,"Cannot access name data", max);
		return S_OK;
	}

	pResult[cnt] = 0;
	return S_OK;
}

} // extern "C"