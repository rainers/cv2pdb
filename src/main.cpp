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
#define T_strcat	wcscat
#define T_strstr	wcsstr
#define T_strtod	wcstod
#define T_strrchr	wcsrchr
#define T_unlink	_wremove
#define T_main		wmain
#define SARG		"%S"
#else
#define T_toupper	toupper
#define T_getdcwd	_getdcwd
#define T_strlen	strlen
#define T_strcpy	strcpy
#define T_strcat	strcat
#define T_strstr	strstr
#define T_strtod	strtod
#define T_strrchr	strrchr
#define T_unlink	unlink
#define T_main		main
#define SARG		"%s"
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

void makefullpath(TCHAR* pdbname)
{
	TCHAR* pdbstart = pdbname;
	TCHAR fullname[260];
	TCHAR* pfullname = fullname;

	int drive = 0;
	if (pdbname[0] && pdbname[1] == ':')
	{
		if (pdbname[2] == '\\' || pdbname[2] == '/')
			return;
		drive = T_toupper (pdbname[0]);
		pdbname += 2;
	}
	else
	{
		drive = _getdrive();
	}

	if (*pdbname != '\\' && *pdbname != '/')
	{
		T_getdcwd(drive, pfullname, sizeof(fullname)/sizeof(fullname[0]) - 2);
		pfullname += T_strlen(pfullname);
		if (pfullname[-1] != '\\')
			*pfullname++ = '\\';
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
}

int T_main(int argc, TCHAR* argv[])
{
	PEImage img;
	double Dversion = 2.043;

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
		else if (argv[0][1] == 's' && argv[0][2])
			dotReplacementChar = (char)argv[0][2];
		else
			fatal("unknown option: " SARG, argv[0]);
	}

	if (argc < 2)
	{
		printf("Convert DMD CodeView/DWARF debug information to PDB files, Version %g\n", VERSION);
		printf("Copyright (c) 2009-2012 by Rainer Schuetze, All Rights Reserved\n");
		printf("\n");
		printf("License for redistribution is given by the Artistic License 2.0\n");
		printf("see file LICENSE for further details\n");
		printf("\n");
		printf("usage: " SARG " [-Dversion|-C|-n|-sC] <exe-file> [new-exe-file] [pdb-file]\n", argv[0]);
		return -1;
	}

	if (!img.load(argv[1]))
		fatal(SARG ": %s", argv[1], img.getLastError());
	if (img.countCVEntries() == 0 && !img.hasDWARF())
		fatal(SARG ": no codeview debug entries found", argv[1]);

	CV2PDB cv2pdb(img);
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

	if(!cv2pdb.openPDB(pdbname))
		fatal(SARG ": %s", pdbname, cv2pdb.getLastError());

	if(img.hasDWARF())
	{
		if(!cv2pdb.relocateDebugLineInfo())
			fatal(SARG ": %s", argv[1], cv2pdb.getLastError());

		if(!cv2pdb.createDWARFModules())
			fatal(SARG ": %s", pdbname, cv2pdb.getLastError());

		if(!cv2pdb.addDWARFTypes())
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

		if (!cv2pdb.writeImage(outname))
			fatal(SARG ": %s", outname, cv2pdb.getLastError());
	}

	return 0;
}
