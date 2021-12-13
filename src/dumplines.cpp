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

int dumpObjectFile(TCHAR* fname)
{
	PEImage img;
    if (!img.readAll(fname))
		fatal(SARG ": %s", fname, img.getLastError());

    img.initDWARFObject();
	if(img.debug_line.isPresent())
    {
        if (!interpretDWARFLines(img, 0))
	    	fatal(SARG ": cannot dump line numbers", fname);
    }
    else if (img.dumpDebugLineInfoOMF() < 0)
        img.dumpDebugLineInfoCOFF();
    
    return 0;
}

int T_main(int argc, TCHAR* argv[])
{
	if (argc < 2)
	{
		printf("Dump line information for object files in OMF/CV4, COFF/CV8 or COFF/DWARF format, Version %g\n", VERSION);
		printf("Copyright (c) 2015 by Rainer Schuetze, All Rights Reserved\n");
		printf("\n");
		printf("License for redistribution is given by the Artistic License 2.0\n");
		printf("see file LICENSE for further details\n");
		printf("\n");
		printf("usage: " SARG " <obj-file>\n", argv[0]);
		return -1;
	}

    return dumpObjectFile(argv[1]);
}
