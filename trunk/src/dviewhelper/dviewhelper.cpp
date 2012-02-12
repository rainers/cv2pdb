///////////////////////////////////////////////////////////////////////////////
//
// DViewHelper - Expression Evaluator for the D string and object class
// Copyright (c) 2009-2010 by Rainer Schuetze, All Rights Reserved
//
// License for redistribution is given by the Artistic License 2.0
// see file LICENSE for further details
//
// Compile the DLL and add the following lines to AUTOEXP.DAT in section [AutoExpand]
// string_viewhelper=$ADDIN(<path to the DLL>\dviewhelper.dll,_DStringView@28)
// wstring_viewhelper=$ADDIN(<path to the DLL>\dviewhelper.dll,_DWStringView@28)
// dstring_viewhelper=$ADDIN(<path to the DLL>\dviewhelper.dll,_DDStringView@28)
// object_viewhelper=$ADDIN(<path to the DLL>\dviewhelper.dll,_DObjectView@28)
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
	HRESULT   (WINAPI *ReadDebuggeeMemory)(DEBUGHELPER *pThis, DWORD dwAddr, DWORD nWant, VOID* pWhere, DWORD *nGot);
	// from here only when dwVersion >= 0x20000
	DWORDLONG (WINAPI *GetRealAddress)(DEBUGHELPER *pThis);
	HRESULT   (WINAPI *ReadDebuggeeMemoryEx)(DEBUGHELPER *pThis, DWORDLONG qwAddr, DWORD nWant, VOID* pWhere, DWORD *nGot);
	int       (WINAPI *GetProcessorType)(DEBUGHELPER *pThis);
};

// possible processor types
typedef enum _MPT {
    mptix86 = 0,    // Intel X86
    mptia64 = 1,   // Intel Merced
    mptamd64 = 2,   // AMD64
    mptUnknown = 3   // Unknown
} MPT;

HRESULT readMem(DEBUGHELPER *pHelper, DWORDLONG qwAddr, DWORD nWant, VOID* pWhere, DWORD *nGot)
{
    if(pHelper->dwVersion < 0x20000)
        return pHelper->ReadDebuggeeMemory(pHelper, (DWORD)qwAddr, nWant, pWhere, nGot);
    return pHelper->ReadDebuggeeMemoryEx(pHelper, qwAddr, nWant, pWhere, nGot);
}

struct DString
{
    DWORD length;
    DWORD data;
};

///////////////////////////////////////////////////////////////////////////////

HRESULT WINAPI StringView(DWORD dwAddress, DEBUGHELPER *pHelper, int nBase, BOOL bUniStrings, 
                          char *pResult, size_t max, DWORD sizePerChar)
{
    DWORDLONG qwAddress = dwAddress; 
    if(pHelper->dwVersion >= 0x20000)
        qwAddress = pHelper->GetRealAddress(pHelper);

    int proc = 0;
    if(pHelper->dwVersion >= 0x20000)
        proc = pHelper->GetProcessorType(pHelper);
    int sizeOfPtr = proc == 0 ? 4 : 8;

	// Get the string struct
    char strdata[16];
	DWORD read;
	if (readMem(pHelper, qwAddress, 2*sizeOfPtr, strdata, &read) != S_OK) 
	{
	    strncpy(pResult,"Cannot access struct", max);
	    return S_OK;
	}
    DWORDLONG length, data;
    if(sizeOfPtr > 4)
    {
        length = *(DWORDLONG*) strdata;
        data = *(DWORDLONG*) (strdata + sizeOfPtr);
    }
    else
    {
        length = *(DWORD*) strdata;
        data = *(DWORD*) (strdata + sizeOfPtr);
    }
	if (length == 0) 
	{
		strncpy(pResult,"\"\"", max);
		return S_OK;
	}

	char* pData = pResult + 1;
	DWORD cnt = (length < max - 3 ? (DWORD)length : max - 3);
	if (sizePerChar * cnt > max)
		pData = new char[sizePerChar * cnt];

	if (readMem(pHelper, data, sizePerChar * cnt, pData, &read) != S_OK) 
	{
		strncpy(pResult,"Cannot access data", max);
	}
	else
	{
		//! @todo: proper utf8/16/32 translation
		for (DWORD p = 0; p < cnt; p++)
		{
			int ch;
			if (sizePerChar == 4)
				ch = ((long*) pData) [p];
			else if (sizePerChar == 2)
				ch = ((short*) pData) [p];
			else
				ch = pData [p];

			if (ch >= 128 && ch < -128)
				pResult[p + 1] = -1;
			else
				pResult[p + 1] = (char) ch;
		}
		pResult[0] = '\"';
		pResult[cnt+1] = '\"';
		pResult[cnt+2] = 0;
	}

	if(pData != pResult + 1)
		delete [] pData;
	return S_OK;
}


__declspec(dllexport)
HRESULT WINAPI DStringView(DWORD dwAddress, DEBUGHELPER *pHelper, int nBase, BOOL bUniStrings, 
                           char *pResult, size_t max, DWORD reserved)
{
	return StringView(dwAddress, pHelper, nBase, bUniStrings, pResult, max, 1);
}

__declspec(dllexport)
HRESULT WINAPI DWStringView(DWORD dwAddress, DEBUGHELPER *pHelper, int nBase, BOOL bUniStrings, 
                            char *pResult, size_t max, DWORD reserved)
{
	return StringView(dwAddress, pHelper, nBase, bUniStrings, pResult, max, 2);
}

__declspec(dllexport)
HRESULT WINAPI DDStringView(DWORD dwAddress, DEBUGHELPER *pHelper, int nBase, BOOL bUniStrings, 
                            char *pResult, size_t max, DWORD reserved)
{
	return StringView(dwAddress, pHelper, nBase, bUniStrings, pResult, max, 4);
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
