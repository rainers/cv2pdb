// Convert DMD CodeView debug information to PDB files
// Copyright (c) 2009-2010 by Rainer Schuetze, All Rights Reserved
//
// License for redistribution is given by the Artistic License 2.0
// see file LICENSE for further details

#include "PEImage.h"
#include "cv2pdb.h"
#include "symutil.h"

#include <direct.h>

double
#include "../VERSION"
;

#ifdef UNICODE
#define T_toupper	towupper
#define T_getdcwd	_wgetdcwd
#define T_strlen	wcslen
#define T_strcpy	wcscpy
#define T_strncpy	wcsncpy
#define T_strcat	wcscat
#define T_strstr	wcsstr
#define T_strncmp	wcsncmp
#define T_strtoul	wcstoul
#define T_strtod	wcstod
#define T_strrchr	wcsrchr
#define T_unlink	_wremove
#define T_main		wmain
#define SARG		"%S"
#define T_stat		_wstat
#else
#define T_toupper	toupper
#define T_getdcwd	_getdcwd
#define T_strlen	strlen
#define T_strcpy	strcpy
#define T_strncpy	strncpy
#define T_strcat	strcat
#define T_strstr	strstr
#define T_strncmp	strncmp
#define T_strtoul	strtoul
#define T_strtod	strtod
#define T_strrchr	strrchr
#define T_unlink	unlink
#define T_main		main
#define SARG		"%s"
#define T_stat		stat
#endif

void fatal(const char *message, ...)
{
	va_list argptr;
	va_start(argptr, message);
	vprintf(message, argptr);
	va_end(argptr);
	printf("\n");
	exit(1);
}

bool makefullpath(TCHAR* pdbname, const TCHAR* basename = NULL)
{
	TCHAR* pdbstart = pdbname;
	TCHAR fullname[260];
	TCHAR* pfullname = fullname;

	if (!pdbname || T_strlen(pdbname) < 2)
	{
		return false;
	}
	// If the path starts with "\\\\", it is considered to be a full path, such as UNC path, VolumeGUID path: "\\\\?\\Volume"
	if (pdbname[0] == '\\' && pdbname[1] == '\\')
	{
		return false;
	}

	int drive = 0;
	if (pdbname[0] && pdbname[1] == ':')
	{
		if (pdbname[2] == '\\' || pdbname[2] == '/')
			return false;
		drive = T_toupper (pdbname[0]) - 'A' + 1;
		pdbname += 2;
	}
	else
	{
		drive = _getdrive();
	}

	if (*pdbname != '\\' && *pdbname != '/')
	{
		if (basename) {
			const TCHAR* pPathEnd = T_strrchr(basename, '/');
			if (!pPathEnd)
				pPathEnd = T_strrchr(basename, '\\');
			if (pPathEnd) {
				auto len = pPathEnd - basename + 1;
				T_strncpy(pfullname, basename, len);
				pfullname += len;
				pfullname[0] = TEXT('\0');
			}
		}
		if (pfullname == fullname) {
			T_getdcwd(drive, pfullname, sizeof(fullname) / sizeof(fullname[0]) - 2);
			pfullname += T_strlen(pfullname);
			if (pfullname[-1] != '\\')
				*pfullname++ = '\\';
		}
	}
	else
	{
		*pfullname++ = 'a' - 1 + drive;
		*pfullname++ = ':';
	}
	T_strcpy(pfullname, pdbname);
	T_strcpy(pdbstart, fullname);

	for(TCHAR*p = pdbstart; *p; p++)
		if (*p == '/')
			*p = '\\';

	// remove relative parts "./" and "../"
	while (TCHAR* p = T_strstr (pdbstart, TEXT("\\.\\")))
		T_strcpy(p, p + 2);

	while (TCHAR* p = T_strstr (pdbstart, TEXT("\\..\\")))
	{
		for (TCHAR* q = p - 1; q >= pdbstart; q--)
			if (*q == '\\')
			{
				T_strcpy(q, p + 3);
				break;
			}
	}
	return true;
}

TCHAR* changeExtension(TCHAR* dbgname, const TCHAR* exename, const TCHAR* ext)
{
	T_strcpy(dbgname, exename);
	TCHAR *pDot = T_strrchr(dbgname, '.');
	if (!pDot || pDot <= T_strrchr(dbgname, '/') || pDot <= T_strrchr(dbgname, '\\'))
		T_strcat(dbgname, ext);
	else
		T_strcpy(pDot, ext);
	return dbgname;
}

TCHAR* extractDebugLink(const PEImage& img, TCHAR* dbgname, const TCHAR* exename, const TCHAR* debug_link = NULL)
{
	if (debug_link)
	{
		T_strcpy(dbgname, debug_link);
	}
	else
	{
#ifdef UNICODE
		auto copied = MultiByteToWideChar(CP_UTF8, 0, (const char*)img.gnu_debuglink.startByte(), img.gnu_debuglink.length, dbgname, MAX_PATH);
		if (copied < MAX_PATH)
			dbgname[copied] = L'\0';
		else
			dbgname[0] = L'\0';
#else
		if (exe.gnu_debuglink.length < MAX_PATH) {
			strncpy((char*)dbgname, (const char*)exe.gnu_debuglink.startByte(), exe.gnu_debuglink.length);
			dbgname[exe.gnu_debuglink.length] = '\0';
		}
		else
			dbgname[0] = '\0';
#endif
	}

	struct _stat buffer;
	TCHAR fulldbgname[MAX_PATH];
	T_strcpy(fulldbgname, dbgname);
	if (makefullpath(fulldbgname, exename) && T_stat(fulldbgname, &buffer) != 0)
	{
		// If path conversion took place and file does not exist, prepend ".debug\" and try again
		T_strcpy(fulldbgname, TEXT(".debug\\"));
		T_strcat(fulldbgname, dbgname);
		makefullpath(fulldbgname, exename);
	}
	T_strcpy(dbgname, fulldbgname);
	return dbgname;
}

int T_main(int argc, TCHAR* argv[])
{
	double Dversion = 2.072;
	const TCHAR* pdbref = 0;
	const TCHAR* debug_link = 0;
	DebugLevel debug = DebugLevel{};

	CoInitialize(nullptr);

	while (argc > 1 && argv[1][0] == '-')
	{
		argv++;
		argc--;
		if (argv[0][1] == '-')
			break;
		if (argv[0][1] == 'D')
			Dversion = T_strtod(argv[0] + 2, 0);
		else if (argv[0][1] == 'C')
			Dversion = 0;
		else if (argv[0][1] == 'n')
			demangleSymbols = false;
		else if (argv[0][1] == 'e')
			useTypedefEnum = true;
		else if (!T_strncmp(&argv[0][1], TEXT("debug"), 5)) // debug[level]
		{
			debug = (DebugLevel)T_strtoul(&argv[0][6], 0, 0);
			if (!debug) {
				debug = DbgBasic;
			}

			fprintf(stderr, "Debug set to %x\n", debug);
		}
		else if (argv[0][1] == 's' && argv[0][2])
			dotReplacementChar = (char)argv[0][2];
		else if (argv[0][1] == 'p' && argv[0][2])
			pdbref = argv[0] + 2;
		else if (argv[0][1] == 'l' && argv[0][2])
			debug_link = argv[0] + 2;
		else
			fatal("unknown option: " SARG, argv[0]);
	}

	if (argc < 2)
	{
		printf("Convert DMD CodeView/DWARF debug information to PDB files, Version %.02f\n", VERSION);
		printf("Copyright (c) 2009-2012 by Rainer Schuetze, All Rights Reserved\n");
		printf("\n");
		printf("License for redistribution is given by the Artistic License 2.0\n");
		printf("see file LICENSE for further details\n");
		printf("\n");
		printf("usage: " SARG " [-D<version>|-C|-n|-e|-s<C>|-p<embedded-pdb>|-l<debug-link>] <exe-file> [new-exe-file] [pdb-file]\n", argv[0]);
		return -1;
	}

	PEImage exe, dbg, *img = NULL;
	TCHAR dbgname[MAX_PATH];

	if (!exe.loadExe(argv[1]))
		fatal(SARG ": %s", argv[1], exe.getLastError());
	if (exe.countCVEntries() || exe.hasDWARF())
		img = &exe;
	else
	{
		struct _stat buffer;

		if (debug_link || exe.hasDebugLink())
		{
			img = &exe;
			extractDebugLink(exe, dbgname, argv[1], debug_link);
		}
		else {
			img = &dbg;
			changeExtension(dbgname, argv[1], TEXT(".dbg"));
		}
		// try separate debug file
		if (T_stat(dbgname, &buffer) != 0)
			fatal(SARG ": no debug entries found", argv[1]);
		if (!dbg.loadExe(dbgname))
			fatal(SARG ": %s", dbgname, dbg.getLastError());
		if (dbg.countCVEntries() == 0 && !dbg.hasDWARF())
			fatal(SARG ": no debug entries found", dbgname);
	}

	CV2PDB cv2pdb(*img, dbg.hasDWARF() ? &dbg : NULL, debug);
	cv2pdb.Dversion = Dversion;
	cv2pdb.initLibraries();

	TCHAR* outname = argv[1];
	if (argc > 2 && argv[2][0])
		outname = argv[2];

	TCHAR pdbname[260];
	if (argc > 3)
		T_strcpy (pdbname, argv[3]);
	else
	{
		T_strcpy (pdbname, outname);
		TCHAR *pDot = T_strrchr (pdbname, '.');
		if (!pDot || pDot <= T_strrchr (pdbname, '/') || pDot <= T_strrchr (pdbname, '\\'))
			T_strcat (pdbname, TEXT(".pdb"));
		else
			T_strcpy (pDot, TEXT(".pdb"));
	}
	makefullpath(pdbname);

	T_unlink(pdbname);

	if(!cv2pdb.openPDB(pdbname, pdbref))
		fatal(SARG ": %s", pdbname, cv2pdb.getLastError());

	if(exe.hasDWARF() || dbg.hasDWARF())
	{
		if(!exe.relocateDebugLineInfo(0x400000))
			fatal(SARG ": %s", argv[1], cv2pdb.getLastError());

		if(!cv2pdb.createDWARFModules())
			fatal(SARG ": %s", pdbname, cv2pdb.getLastError());

		if(!cv2pdb.addDWARFSymbols())
			fatal(SARG ": %s", pdbname, cv2pdb.getLastError());

		if(!cv2pdb.addDWARFLines())
			fatal(SARG ": %s", pdbname, cv2pdb.getLastError());

		if (!cv2pdb.addDWARFPublics())
			fatal(SARG ": %s", pdbname, cv2pdb.getLastError());

		if (!cv2pdb.writeDWARFImage(outname))
			fatal(SARG ": %s", outname, cv2pdb.getLastError());
	}
	else
	{
		if (!cv2pdb.initSegMap())
			fatal(SARG ": %s", argv[1], cv2pdb.getLastError());

		if (!cv2pdb.initGlobalSymbols())
			fatal(SARG ": %s", argv[1], cv2pdb.getLastError());

		if (!cv2pdb.initGlobalTypes())
			fatal(SARG ": %s", argv[1], cv2pdb.getLastError());

		if (!cv2pdb.createModules())
			fatal(SARG ": %s", pdbname, cv2pdb.getLastError());

		if (!cv2pdb.addTypes())
			fatal(SARG ": %s", pdbname, cv2pdb.getLastError());

		if (!cv2pdb.addSymbols())
			fatal(SARG ": %s", pdbname, cv2pdb.getLastError());

		if (!cv2pdb.addSrcLines())
			fatal(SARG ": %s", pdbname, cv2pdb.getLastError());

		if (!cv2pdb.addPublics())
			fatal(SARG ": %s", pdbname, cv2pdb.getLastError());

		if (!exe.isDBG())
			if (!cv2pdb.writeImage(outname, exe))
				fatal(SARG ": %s", outname, cv2pdb.getLastError());
	}

	return 0;
}
