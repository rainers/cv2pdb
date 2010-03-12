// Convert DMD CodeView debug information to PDB files
// Copyright (c) 2009 by Rainer Schuetze, All Rights Reserved
//
// License for redistribution is given by the Artistic License 2.0
// see file LICENSE for further details

#include "PEImage.h"
#include "cv2pdb.h"

#include <direct.h>

double
#include "../VERSION"
;

void fatal(const char *message, ...)
{
	va_list argptr;
	va_start(argptr, message);
	vprintf(message, argptr);
	va_end(argptr);
	printf("\n");
	exit(1);
}

void makefullpath(char* pdbname)
{
	char* pdbstart = pdbname;
	char fullname[260];
	char* pfullname = fullname;

	int drive = 0;
	if (pdbname[0] && pdbname[1] == ':')
	{
		if (pdbname[2] == '\\' || pdbname[2] == '/')
			return;
		drive = toupper (pdbname[0]);
		pdbname += 2;
	}
	else
	{
		drive = _getdrive();
	}

	if (*pdbname != '\\' && *pdbname != '/')
	{
		_getdcwd(drive, pfullname, sizeof(fullname) - 2);
		pfullname += strlen(pfullname);
		if (pfullname[-1] != '\\')
			*pfullname++ = '\\';
	}
	else
	{
		*pfullname++ = 'a' - 1 + drive;
		*pfullname++ = ':';
	}
	strcpy(pfullname, pdbname);
	strcpy(pdbstart, fullname);

	for(char*p = pdbstart; *p; p++)
		if (*p == '/')
			*p = '\\';

	// remove relative parts "./" and "../"
	while (char* p = strstr (pdbstart, "\\.\\"))
		strcpy(p, p + 2);

	while (char* p = strstr (pdbstart, "\\..\\"))
	{
		for (char* q = p - 1; q >= pdbstart; q--)
			if (*q == '\\')
			{
				strcpy(q, p + 3);
				break;
			}
	}
}

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		printf("Convert DMD CodeView debug information to PDB files, Version %g\n", VERSION);
		printf("Copyright (c) 2009 by Rainer Schuetze, All Rights Reserved\n");
		printf("\n");
		printf("License for redistribution is given by the Artistic License 2.0\n");
		printf("see file LICENSE for further details\n");
		printf("\n");
		printf("usage: %s [-Dversion|-C] <exe-file> [new-exe-file] [pdb-file]\n", argv[0]);
		return -1;
	}

	PEImage img;
	double Dversion = 2;

	while (argc > 1 && argv[1][0] == '-')
	{
		argv++;
		argc--;
		if (argv[0][1] == '-')
			break;
		if (argv[0][1] == 'D')
			Dversion = strtod (argv[0] + 2, 0);
		else if (argv[0][1] == 'C')
			Dversion = 0;
		else
			fatal("unknwon option: %s", argv[0]);
	}

	if (!img.load(argv[1]))
		fatal("%s: %s", argv[1], img.getLastError());
	if (img.countCVEntries() == 0)
		fatal("%s: no codeview debug entries found", argv[1]);

	CV2PDB cv2pdb(img);
	cv2pdb.initLibraries();
	cv2pdb.Dversion = Dversion;

	char* outname = argv[1];
	if (argc > 2 && argv[2][0])
		outname = argv[2];

	char pdbname[260];
	if (argc > 3)
		strcpy (pdbname, argv[3]);
	else
	{
		strcpy (pdbname, outname);
		char *pDot = strrchr (pdbname, '.');
		if (!pDot || pDot <= strrchr (pdbname, '/') || pDot <= strrchr (pdbname, '\\'))
			strcat (pdbname, ".pdb");
		else
			strcpy (pDot, ".pdb");
	}
	makefullpath(pdbname);

	unlink(pdbname);

	if(!cv2pdb.openPDB(pdbname))
		fatal("%s: %s", pdbname, cv2pdb.getLastError());

	if (!cv2pdb.initSegMap())
		fatal("%s: %s", argv[1], cv2pdb.getLastError());

	if (!cv2pdb.initGlobalTypes())
		fatal("%s: %s", argv[1], cv2pdb.getLastError());

	if (!cv2pdb.createModules())
		fatal("%s: %s", pdbname, cv2pdb.getLastError());

	if (!cv2pdb.addTypes())
		fatal("%s: %s", pdbname, cv2pdb.getLastError());

	if (!cv2pdb.addSymbols())
		fatal("%s: %s", pdbname, cv2pdb.getLastError());

	if (!cv2pdb.addSrcLines())
		fatal("%s: %s", pdbname, cv2pdb.getLastError());

	if (!cv2pdb.addPublics())
		fatal("%s: %s", pdbname, cv2pdb.getLastError());

	if (!cv2pdb.writeImage(outname))
		fatal("%s: %s", outname, cv2pdb.getLastError());

	return 0;
}
