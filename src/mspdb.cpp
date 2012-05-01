// Convert DMD CodeView debug information to PDB files
// Copyright (c) 2009-2010 by Rainer Schuetze, All Rights Reserved
//
// License for redistribution is given by the Artistic License 2.0
// see file LICENSE for further details

#include "mspdb.h"

#include <windows.h>

#pragma comment(lib, "rpcrt4.lib")

HMODULE modMsPdb;
mspdb::fnPDBOpen2W *pPDBOpen2W;

char* mspdb80_dll = "mspdb80.dll";
char* mspdb100_dll = "mspdb100.dll";

bool mspdb::DBI::isVS10 = false;

bool getInstallDir(const char* version, char* installDir, DWORD size)
{
	char key[260] = "SOFTWARE\\Microsoft\\";
	strcat(key, version);

	HKEY hkey;
	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, key, 0, KEY_QUERY_VALUE, &hkey) != ERROR_SUCCESS)
		return false;
	
	bool rc = RegQueryValueExA(hkey, "InstallDir", 0, 0, (LPBYTE)installDir, &size) == ERROR_SUCCESS;
	RegCloseKey(hkey);
	return rc;
}

bool tryLoadMsPdb(const char* version, const char* mspdb)
{
	char installDir[260];
	if (!getInstallDir(version, installDir, sizeof(installDir)))
		return false;
	char* p = installDir + strlen(installDir);
	if (p[-1] != '\\' && p[-1] != '/')
		*p++ = '\\';
	strcpy(p, mspdb);

	modMsPdb = LoadLibraryA(installDir);
	return modMsPdb != 0;
}

bool initMsPdb()
{
	if (!modMsPdb)
		modMsPdb = LoadLibraryA(mspdb80_dll);

	if (!modMsPdb)
		tryLoadMsPdb("VisualStudio\\9.0", mspdb80_dll);
	if (!modMsPdb)
		tryLoadMsPdb("VisualStudio\\8.0", mspdb80_dll);
	if (!modMsPdb)
		tryLoadMsPdb("VCExpress\\9.0", mspdb80_dll);
	if (!modMsPdb)
		tryLoadMsPdb("VCExpress\\8.0", mspdb80_dll);

#if 1
	if (!modMsPdb)
	{
		modMsPdb = LoadLibraryA(mspdb100_dll);
		if (!modMsPdb)
			tryLoadMsPdb("VisualStudio\\10.0", mspdb100_dll);
		if (!modMsPdb)
			tryLoadMsPdb("VCExpress\\10.0", mspdb100_dll);
		if (modMsPdb)
			mspdb::DBI::isVS10 = true;
	}
#endif

	if (!modMsPdb)
		return false;

	if (!pPDBOpen2W)
		pPDBOpen2W = (mspdb::fnPDBOpen2W*) GetProcAddress(modMsPdb, "PDBOpen2W");
	if (!pPDBOpen2W)
		return false;

	return true;
}

bool exitMsPdb()
{
	pPDBOpen2W = 0;
	if (modMsPdb)
		FreeLibrary(modMsPdb);
	modMsPdb = 0;
	return true;
}

mspdb::PDB* CreatePDB(const wchar_t* pdbname)
{
	if (!initMsPdb ())
		return 0;

	mspdb::PDB* pdb = 0;
	long data[194] = { 193, 0 };
	wchar_t ext[256] = L".exe";
	if (!(*pPDBOpen2W) (pdbname, "wf", data, ext, 0x400, &pdb))
		return 0;

	return pdb;
}
