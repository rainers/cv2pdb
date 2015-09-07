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
char* mspdb110_dll = "mspdb110.dll";
char* mspdb120_dll = "mspdb120.dll";
char* mspdb140_dll = "mspdb140.dll";
// char* mspdb110shell_dll = "mspdbst.dll"; // the VS 2012 Shell uses this file instead of mspdb110.dll, but is missing mspdbsrv.exe

int mspdb::vsVersion = 8;

// verify mspdbsrv.exe is found in the same path
void tryLoadLibrary(const char* mspdb)
{
	if (modMsPdb)
		return;
	modMsPdb = LoadLibraryA(mspdb);
	if (!modMsPdb)
		return;

	char modpath[260];
	if(GetModuleFileNameA(modMsPdb, modpath, 260) < 260)
	{
		char* p = modpath + strlen(modpath);
		while(p > modpath && p[-1] != '\\')
			p--;
		strcpy(p, "mspdbsrv.exe");
		if(GetFileAttributesA(modpath) != INVALID_FILE_ATTRIBUTES)
			return;
	}
	FreeLibrary(modMsPdb);
	modMsPdb = NULL;
}

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

bool tryLoadMsPdb(const char* version, const char* mspdb, const char* path = 0)
{
	char installDir[260];
	if (!getInstallDir(version, installDir, sizeof(installDir)))
		return false;
	char* p = installDir + strlen(installDir);
	if (p[-1] != '\\' && p[-1] != '/')
		*p++ = '\\';
	if(path)
		p += strlen(strcpy(p, path));
	strcpy(p, mspdb);

	tryLoadLibrary(installDir);
	return modMsPdb != 0;
}

void tryLoadMsPdb80(bool throughPath)
{
	if (!modMsPdb && throughPath)
		tryLoadLibrary(mspdb80_dll);

	if (!modMsPdb && !throughPath)
		tryLoadMsPdb("VisualStudio\\9.0", mspdb80_dll);
	if (!modMsPdb && !throughPath)
		tryLoadMsPdb("VisualStudio\\8.0", mspdb80_dll);
	if (!modMsPdb && !throughPath)
		tryLoadMsPdb("VCExpress\\9.0", mspdb80_dll);
	if (!modMsPdb && !throughPath)
		tryLoadMsPdb("VCExpress\\8.0", mspdb80_dll);
}

void tryLoadMsPdb100(bool throughPath)
{
	if (!modMsPdb)
	{
		if(throughPath)
			modMsPdb = LoadLibraryA(mspdb100_dll);
		if (!modMsPdb && !throughPath)
			tryLoadMsPdb("VisualStudio\\10.0", mspdb100_dll);
		if (!modMsPdb && !throughPath)
			tryLoadMsPdb("VCExpress\\10.0", mspdb100_dll);
		if (modMsPdb)
			mspdb::vsVersion = 10;
	}
}

void tryLoadMsPdb110(bool throughPath)
{
	if (!modMsPdb)
	{
		if (throughPath)
			modMsPdb = LoadLibraryA(mspdb110_dll);
		if (!modMsPdb && !throughPath)
			tryLoadMsPdb("VisualStudio\\11.0", mspdb110_dll);
		if (!modMsPdb && !throughPath)
			tryLoadMsPdb("VSWinExpress\\11.0", mspdb110_dll);
		if (modMsPdb)
			mspdb::vsVersion = 11;
	}
}

void tryLoadMsPdb120(bool throughPath)
{
	if (!modMsPdb)
	{
		if(throughPath)
			modMsPdb = LoadLibraryA(mspdb120_dll);
		if (!modMsPdb && !throughPath)
			tryLoadMsPdb("VisualStudio\\12.0", mspdb120_dll, "..\\..\\VC\\bin\\");
		if (!modMsPdb && !throughPath)
			tryLoadMsPdb("VSWinExpress\\12.0", mspdb120_dll, "..\\..\\VC\\bin\\");
		if (modMsPdb)
			mspdb::vsVersion = 12;
	}
}

void tryLoadMsPdb140(bool throughPath)
{
	if (!modMsPdb)
	{
		if(throughPath)
			modMsPdb = LoadLibraryA(mspdb140_dll);
		if (!modMsPdb && !throughPath)
			tryLoadMsPdb("VisualStudio\\14.0", mspdb140_dll, "..\\..\\VC\\bin\\");
		if (!modMsPdb && !throughPath)
			tryLoadMsPdb("VSWinExpress\\14.0", mspdb140_dll, "..\\..\\VC\\bin\\");
		if (modMsPdb)
			mspdb::vsVersion = 14;
	}
}

bool initMsPdb()
{
#if 0 // might cause problems when combining VS Shell 2010 with VS 2008 or similar
	if(const char* p = getenv("VisualStudioDir"))
	{
		// guess from environment variable from which version of VS we are invoked and prefer a correspondig mspdb DLL
		if (strstr(p, "2010"))
			tryLoadMsPdb100();
		if (strstr(p, "2012"))
			tryLoadMsPdb110();
		// VS2008 tried next anyway
	}
#endif

	// try loading through the PATH first to best match current setup
	tryLoadMsPdb140(true);
	tryLoadMsPdb120(true);
	tryLoadMsPdb110(true);
	tryLoadMsPdb100(true);
	tryLoadMsPdb80(true);

	tryLoadMsPdb140(false);
	tryLoadMsPdb120(false);
	tryLoadMsPdb110(false);
	tryLoadMsPdb100(false);
	tryLoadMsPdb80(false);

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
	if (!((*pPDBOpen2W) (pdbname, "wf", data, ext, 0x400, &pdb)))
		return 0;

	return pdb;
}
