// Convert DMD CodeView debug information to PDB files
// Copyright (c) 2009-2010 by Rainer Schuetze, All Rights Reserved
//
// License for redistribution is given by the Artistic License 2.0
// see file LICENSE for further details

#include "PEImage.h"

extern "C" {
#include "mscvpdb.h"
}

#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <ctype.h>
#include <direct.h>
#include <share.h>
#include <sys/stat.h>
#include <vector>

#ifdef UNICODE
#define T_sopen	_wsopen
#define T_open	_wopen
#else
#define T_sopen	sopen
#define T_open	open
#endif

///////////////////////////////////////////////////////////////////////
PEImage::PEImage(const TCHAR* iname)
: dump_base(0)
, dump_total_len(0)
, dirHeader(0)
, hdr32(0)
, hdr64(0)
, fd(-1)
, codeSegment(0)
, linesSegment(-1)
, nsec(0)
, nsym(0)
, symtable(0)
, strtable(0)
, bigobj(false)
, dbgfile(false)
, x64(false)
{
	if(iname)
		loadExe(iname);
}

PEImage::~PEImage()
{
	if(fd != -1)
		close(fd);
	if(dump_base)
		free_aligned(dump_base);
}

///////////////////////////////////////////////////////////////////////
bool PEImage::readAll(const TCHAR* iname)
{
	if (fd != -1)
		return setError("file already open");

	fd = T_sopen(iname, O_RDONLY | O_BINARY, SH_DENYWR);
	if (fd == -1)
		return setError("Can't open file");

	struct stat s;
	if (fstat(fd, &s) < 0)
		return setError("Can't get size");
	dump_total_len = s.st_size;

	dump_base = alloc_aligned(dump_total_len, 0x1000);
	if (!dump_base)
		return setError("Out of memory");
	if (read(fd, dump_base, dump_total_len) != dump_total_len)
		return setError("Cannot read file");

	close(fd);
	fd = -1;
	return true;
}

///////////////////////////////////////////////////////////////////////
bool PEImage::loadExe(const TCHAR* iname)
{
    if (!readAll(iname))
        return false;

    return initCVPtr(true) || initDbgPtr(true) || initDWARFPtr(true);
}

///////////////////////////////////////////////////////////////////////
bool PEImage::loadObj(const TCHAR* iname)
{
    if (!readAll(iname))
        return false;

    return initDWARFObject();
}

///////////////////////////////////////////////////////////////////////
bool PEImage::save(const TCHAR* oname)
{
	if (fd != -1)
		return setError("file already open");

	if (!dump_base)
		return setError("no data to dump");

	fd = T_open(oname, O_WRONLY | O_CREAT | O_BINARY | O_TRUNC, S_IREAD | S_IWRITE | S_IEXEC);
	if (fd == -1)
		return setError("Can't create file");

	if (write(fd, dump_base, dump_total_len) != dump_total_len)
		return setError("Cannot write file");

	close(fd);
	fd = -1;
	return true;
}

///////////////////////////////////////////////////////////////////////
bool PEImage::replaceDebugSection (const void* data, int datalen, bool initCV)
{
	// append new debug directory to data
	IMAGE_DEBUG_DIRECTORY debugdir;
	if(dbgDir)
		debugdir = *dbgDir;
	else
	{
		memset(&debugdir, 0, sizeof(debugdir));
		debugdir.Type = IMAGE_DEBUG_TYPE_CODEVIEW;
	}
	int datalenRaw = datalen;
	// Growing the data block to the closest 16-byte boundary to make sure the debug directory is aligned.
	datalen = (datalen + 0xf) & ~0xf;
	int xdatalen = datalen + sizeof(debugdir);

	// assume there is place for another section because of section alignment
	int s;
	DWORD lastVirtualAddress = 0;
    int firstDWARFsection = -1;
	int cntSections = countSections();
	for(s = 0; s < cntSections; s++)
	{
		const char* name = (const char*) sec[s].Name;
		if(name[0] == '/')
		{
			int off = strtol(name + 1, 0, 10);
			name = strtable + off;
		}
		if (strncmp (name, ".debug_", 7) != 0)
			firstDWARFsection = -1;
		else if (firstDWARFsection < 0)
			firstDWARFsection = s;

		if (strcmp (name, ".debug") == 0)
		{
			if (s == cntSections - 1)
			{
				dump_total_len = sec[s].PointerToRawData;
				break;
			}
			strcpy ((char*) sec [s].Name, ".ddebug");
			printf("warning: .debug not last section, cannot remove section\n");
		}
		lastVirtualAddress = sec[s].VirtualAddress + sec[s].Misc.VirtualSize;
	}
    if (firstDWARFsection > 0)
    {
        s = firstDWARFsection;
		dump_total_len = sec[s].PointerToRawData;
		lastVirtualAddress = sec[s-1].VirtualAddress + sec[s-1].Misc.VirtualSize;
    }
	int align = IMGHDR(OptionalHeader.FileAlignment);
	int align_len = xdatalen;
	int fill = 0;

	if (align > 0)
	{
		fill = (align - (dump_total_len % align)) % align;
		align_len = ((xdatalen + align - 1) / align) * align;
	}
	char* newdata = (char*) alloc_aligned(dump_total_len + fill + xdatalen, 0x1000);
	if(!newdata)
		return setError("cannot alloc new image");

	int salign_len = xdatalen;
	align = IMGHDR(OptionalHeader.SectionAlignment);
	if (align > 0)
	{
		lastVirtualAddress = ((lastVirtualAddress + align - 1) / align) * align;
		salign_len = ((xdatalen + align - 1) / align) * align;
	}

	strcpy((char*) sec[s].Name, ".debug");
	sec[s].Misc.VirtualSize = align_len; // union with PhysicalAddress;
	sec[s].VirtualAddress = lastVirtualAddress;
	sec[s].SizeOfRawData = xdatalen;
	sec[s].PointerToRawData = dump_total_len + fill;
	sec[s].PointerToRelocations = 0;
	sec[s].PointerToLinenumbers = 0;
	sec[s].NumberOfRelocations = 0;
	sec[s].NumberOfLinenumbers = 0;
	sec[s].Characteristics = IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_DISCARDABLE | IMAGE_SCN_CNT_INITIALIZED_DATA;

	IMGHDR(FileHeader.NumberOfSections) = s + 1;
	// hdr->OptionalHeader.SizeOfImage += salign_len;
	IMGHDR(OptionalHeader.SizeOfImage) = sec[s].VirtualAddress + salign_len;

	IMGHDR(OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].VirtualAddress) = lastVirtualAddress + datalen;
	IMGHDR(OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].Size) = sizeof(IMAGE_DEBUG_DIRECTORY);

	// invalidate the symbol table pointer if it points outside of the data to be copied
	IMAGE_DOS_HEADER *dos = DPV<IMAGE_DOS_HEADER>(0);
	if(dos && dos->e_magic == IMAGE_DOS_SIGNATURE)
	{
		// The 32-bit and 64-bit headers are identical in the FileHeader part, so we just use the 32-bit one
		IMAGE_NT_HEADERS32* hdr = DPV<IMAGE_NT_HEADERS32>(dos->e_lfanew);
		if(hdr && hdr->FileHeader.PointerToSymbolTable >= dump_total_len)
		{
			hdr->FileHeader.PointerToSymbolTable = 0;
			hdr->FileHeader.NumberOfSymbols = 0;
		}
	}

	// append debug data chunk to existing file image
	memcpy(newdata, dump_base, dump_total_len);
	memset(newdata + dump_total_len, 0, fill);
	memcpy(newdata + dump_total_len + fill, data, datalenRaw);

	dbgDir = (IMAGE_DEBUG_DIRECTORY*) (newdata + dump_total_len + fill + datalen);
	memcpy(dbgDir, &debugdir, sizeof(debugdir));

	dbgDir->PointerToRawData = sec[s].PointerToRawData;
#if 0
	dbgDir->AddressOfRawData = sec[s].PointerToRawData;
	dbgDir->SizeOfData = sec[s].SizeOfRawData;
#else // suggested by Z3N
	dbgDir->AddressOfRawData = sec[s].VirtualAddress;
	dbgDir->SizeOfData = sec[s].SizeOfRawData - sizeof(IMAGE_DEBUG_DIRECTORY);
#endif

	free_aligned(dump_base);
	dump_base = newdata;
	dump_total_len += fill + xdatalen;

	return !initCV || initCVPtr(false);
}

///////////////////////////////////////////////////////////////////////
bool PEImage::initCVPtr(bool initDbgDir)
{
	dos = DPV<IMAGE_DOS_HEADER> (0);
	if(!dos)
		return setError("file too small for DOS header");
	if(dos->e_magic != IMAGE_DOS_SIGNATURE)
		return setError("this is not a DOS executable");

	hdr32 = DPV<IMAGE_NT_HEADERS32> (dos->e_lfanew);
	hdr64 = DPV<IMAGE_NT_HEADERS64> (dos->e_lfanew);
	if(!hdr32)
		return setError("no optional header found");
	if(hdr32->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 ||
	   hdr32->FileHeader.Machine == IMAGE_FILE_MACHINE_IA64)
		hdr32 = 0;
	else
		hdr64 = 0;
	x64 = hdr64 != nullptr;

	if(IMGHDR(Signature) != IMAGE_NT_SIGNATURE)
		return setError("optional header does not have PE signature");
	if(IMGHDR(FileHeader.SizeOfOptionalHeader) < sizeof(IMAGE_OPTIONAL_HEADER32))
		return setError("optional header too small");

	sec = hdr32 ? IMAGE_FIRST_SECTION(hdr32) : IMAGE_FIRST_SECTION(hdr64);
    nsec = IMGHDR(FileHeader.NumberOfSections);

    symtable = DPV<char>(IMGHDR(FileHeader.PointerToSymbolTable));
    nsym = IMGHDR(FileHeader.NumberOfSymbols);
	strtable = symtable + nsym * IMAGE_SIZEOF_SYMBOL;

	if(IMGHDR(OptionalHeader.NumberOfRvaAndSizes) <= IMAGE_DIRECTORY_ENTRY_DEBUG)
		return setError("too few entries in data directory");

	dbgDir = 0;
	dirHeader = 0;
	dirEntry = 0;
	if (!initDbgDir)
		return true;

	unsigned int i;
	int found = false;
	for(i = 0; i < IMGHDR(OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].Size)/sizeof(IMAGE_DEBUG_DIRECTORY); i++)
	{
		int off = IMGHDR(OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].VirtualAddress) + i*sizeof(IMAGE_DEBUG_DIRECTORY);
		auto ddir = RVA<IMAGE_DEBUG_DIRECTORY>(off, sizeof(IMAGE_DEBUG_DIRECTORY));

		if (!ddir)
			continue; //return setError("debug directory not placed in image");
		if (ddir->Type != IMAGE_DEBUG_TYPE_CODEVIEW)
			continue; //return setError("debug directory not of type CodeView");

		return _initFromCVDebugDir(ddir);
	}
	return setError("no CodeView debug info data found");
}

///////////////////////////////////////////////////////////////////////
bool PEImage::initDbgPtr(bool initDbgDir)
{
	auto dbg = DPV<IMAGE_SEPARATE_DEBUG_HEADER> (0);
	if(!dbg)
		return setError("file too small for DBG header");
	if(dbg->Signature != IMAGE_SEPARATE_DEBUG_SIGNATURE)
		return setError("this is not a DBG file");

	sec = (PIMAGE_SECTION_HEADER)(dbg + 1);
	nsec = dbg->NumberOfSections;

	symtable = (char*)(sec + nsec);
	nsym = 0;
	strtable = symtable + nsym;

	if(dbg->DebugDirectorySize <= IMAGE_DIRECTORY_ENTRY_DEBUG)
		return setError("too few entries in data directory");

	x64 = dbg->Machine == IMAGE_FILE_MACHINE_AMD64 || dbg->Machine == IMAGE_FILE_MACHINE_IA64;
	dbgfile = true;
	dbgDir = 0;
	dirHeader = 0;
	dirEntry = 0;
	if (!initDbgDir)
		return true;

	unsigned int dbgDirOff = strtable + dbg->ExportedNamesSize - (char*) dbg;
	unsigned int i;
	int found = false;
	for(i = 0; i < dbg->DebugDirectorySize/sizeof(IMAGE_DEBUG_DIRECTORY); i++)
	{
		int off = dbgDirOff + i*sizeof(IMAGE_DEBUG_DIRECTORY);
		auto ddir = DPV<IMAGE_DEBUG_DIRECTORY>(off, sizeof(IMAGE_DEBUG_DIRECTORY));
		if (!ddir)
			continue; //return setError("debug directory not placed in image");
		if (ddir->Type != IMAGE_DEBUG_TYPE_CODEVIEW)
			continue; //return setError("debug directory not of type CodeView");

		return _initFromCVDebugDir(ddir);
	}
	return setError("no CodeView debug info data found");
}

///////////////////////////////////////////////////////////////////////
bool PEImage::_initFromCVDebugDir(IMAGE_DEBUG_DIRECTORY* ddir)
{
	cv_base = ddir->PointerToRawData;
	OMFSignature* sig = DPV<OMFSignature>(cv_base, ddir->SizeOfData);
	if (!sig)
		return setError("invalid debug data base address and size");
	if (memcmp(sig->Signature, "NB09", 4) != 0 && memcmp(sig->Signature, "NB11", 4) != 0)
	{
		// return setError("can only handle debug info of type NB09 and NB11");
		return false;
	}
	dirHeader = CVP<OMFDirHeader>(sig->filepos);
	if (!dirHeader)
		return setError("invalid CodeView dir header data base address");
	dirEntry = CVP<OMFDirEntry>(sig->filepos + dirHeader->cbDirHeader);
	if (!dirEntry)
		return setError("CodeView debug dir entries invalid");

	dbgDir = ddir;
	return true;
}

///////////////////////////////////////////////////////////////////////
bool PEImage::initDWARFPtr(bool initDbgDir)
{
	dos = DPV<IMAGE_DOS_HEADER> (0);
	if(!dos)
		return setError("file too small for DOS header");
	if(dos->e_magic != IMAGE_DOS_SIGNATURE)
		return setError("this is not a DOS executable");

	hdr32 = DPV<IMAGE_NT_HEADERS32> (dos->e_lfanew);
	hdr64 = DPV<IMAGE_NT_HEADERS64> (dos->e_lfanew);
	if(!hdr32)
		return setError("no optional header found");
	if(hdr32->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 ||
	   hdr32->FileHeader.Machine == IMAGE_FILE_MACHINE_IA64)
		hdr32 = 0;
	else
		hdr64 = 0;
	x64 = hdr64 != nullptr;

	if(IMGHDR(Signature) != IMAGE_NT_SIGNATURE)
		return setError("optional header does not have PE signature");
	if(IMGHDR(FileHeader.SizeOfOptionalHeader) < sizeof(IMAGE_OPTIONAL_HEADER32))
		return setError("optional header too small");

	dbgDir = 0;
	sec = hdr32 ? IMAGE_FIRST_SECTION(hdr32) : IMAGE_FIRST_SECTION(hdr64);
	symtable = DPV<char>(IMGHDR(FileHeader.PointerToSymbolTable));
    nsym = IMGHDR(FileHeader.NumberOfSymbols);
	strtable = symtable + nsym * IMAGE_SIZEOF_SYMBOL;
	initDWARFSegments();

	setError(0);
	return true;
}

bool PEImage::initDWARFObject()
{
	IMAGE_FILE_HEADER* hdr = DPV<IMAGE_FILE_HEADER> (0);
	if(!dos)
		return setError("file too small for COFF header");

	if (hdr->Machine == IMAGE_FILE_MACHINE_UNKNOWN && hdr->NumberOfSections == 0xFFFF)
	{
        static CLSID bigObjClSID = { 0xD1BAA1C7, 0xBAEE, 0x4ba9, { 0xAF, 0x20, 0xFA, 0xF6, 0x6A, 0xA4, 0xDC, 0xB8 } };
		ANON_OBJECT_HEADER_BIGOBJ* bighdr = DPV<ANON_OBJECT_HEADER_BIGOBJ> (0);
		if (!bighdr || bighdr->Version < 2 || bighdr->ClassID != bigObjClSID)
			return setError("invalid big object file COFF header");
		sec = (IMAGE_SECTION_HEADER*)((char*)(bighdr + 1) + bighdr->SizeOfData);
        nsec = bighdr->NumberOfSections;
        bigobj = true;
        symtable = DPV<char>(bighdr->PointerToSymbolTable);
        nsym = bighdr->NumberOfSymbols;
	    strtable = symtable + nsym * sizeof(IMAGE_SYMBOL_EX);
	}
    else if (hdr->Machine != IMAGE_FILE_MACHINE_UNKNOWN)
    {
        sec = (IMAGE_SECTION_HEADER*)(hdr + 1);
        nsec = hdr->NumberOfSections;
        bigobj = false;
        hdr32 = (IMAGE_NT_HEADERS32*)((char*)hdr - 4); // skip back over signature
	    symtable = DPV<char>(IMGHDR(FileHeader.PointerToSymbolTable));
        nsym = IMGHDR(FileHeader.NumberOfSymbols);
	    strtable = symtable + nsym * IMAGE_SIZEOF_SYMBOL;
    }
    else
	    return setError("Unknown object file format");

    if (!symtable || !strtable)
	    return setError("Unknown object file format");

    initDWARFSegments();
    setError(0);
    return true;
}

static DWORD sizeInImage(const IMAGE_SECTION_HEADER& sec)
{
    if (sec.Misc.VirtualSize == 0)
        return sec.SizeOfRawData; // for object files
    return sec.SizeOfRawData < sec.Misc.VirtualSize ? sec.SizeOfRawData : sec.Misc.VirtualSize;
}

void PEImage::initSec(PESection& sec, const IMAGE_SECTION_HEADER& imgSec) const
{
	sec.base = DPV<char>(imgSec.PointerToRawData, sec.length = sizeInImage(imgSec));
}

void PEImage::initDWARFSegments()
{
	for(int s = 0; s < nsec; s++)
	{
		const char* name = (const char*) sec[s].Name;
		if(name[0] == '/')
		{
			int off = strtol(name + 1, 0, 10);
			name = strtable + off;
		}

		if (strcmp(name, ".debug_info") == 0)
			initSec(debug_info, sec[s]);
		if (strcmp(name, ".debug_addr") == 0)
			initSec(debug_addr, sec[s]);
		if (strcmp(name, ".debug_abbrev") == 0)
			initSec(debug_abbrev, sec[s]);
		if (strcmp(name, ".debug_line") == 0)
			initSec(debug_line, sec[linesSegment = s]);
		if (strcmp(name, ".debug_line_str") == 0)
			initSec(debug_line_str, sec[s]);
		if (strcmp(name, ".debug_frame") == 0)
			initSec(debug_frame, sec[s]);
		if (strcmp(name, ".debug_str") == 0)
			initSec(debug_str, sec[s]);
		if (strcmp(name, ".debug_loc") == 0)
			initSec(debug_loc, sec[s]);
		if (strcmp(name, ".debug_loclists") == 0)
			initSec(debug_loclists, sec[s]);
		if (strcmp(name, ".debug_ranges") == 0)
			initSec(debug_ranges, sec[s]);
		if (strcmp(name, ".debug_rnglists") == 0)
			initSec(debug_rnglists, sec[s]);
		if (strcmp(name, ".reloc") == 0)
			initSec(reloc, sec[s]);
		if(strcmp(name, ".text") == 0)
			codeSegment = s;
	}
}

bool PEImage::relocateDebugLineInfo(unsigned int img_base)
{
	if(!reloc.isPresent())
		return true;

	char* relocbase = reloc.base;
	char* relocend = relocbase + reloc.length;
	while(relocbase < relocend)
	{
		unsigned int virtadr = *(unsigned int *) relocbase;
		unsigned int chksize = *(unsigned int *) (relocbase + 4);

		char* p = RVA<char> (virtadr, 1);
		if(debug_line.isPtrInside(p))
		{
			for (unsigned int w = 8; w < chksize; w += 2)
			{
				unsigned short entry = *(unsigned short*)(relocbase + w);
				unsigned short type = (entry >> 12) & 0xf;
				unsigned short off = entry & 0xfff;

				if(type == 3) // HIGHLOW
				{
					*(long*) (p + off) += img_base;
				}
			}
		}
		if(chksize == 0 || chksize >= reloc.length)
			break;
		relocbase += chksize;
	}
	return true;
}

int PEImage::getRelocationInLineSegment(unsigned int offset) const
{
    return getRelocationInSegment(linesSegment, offset);
}

int PEImage::getRelocationInSegment(int segment, unsigned int offset) const
{
    if (segment < 0)
        return -1;

    int cnt = sec[segment].NumberOfRelocations;
    IMAGE_RELOCATION* rel = DPV<IMAGE_RELOCATION>(sec[segment].PointerToRelocations, cnt * sizeof(IMAGE_RELOCATION));
    if (!rel)
        return -1;

    for (int i = 0; i < cnt; i++)
        if (rel[i].VirtualAddress == offset)
        {
            if (bigobj)
            {
                IMAGE_SYMBOL_EX* sym = (IMAGE_SYMBOL_EX*)(symtable + rel[i].SymbolTableIndex * sizeof(IMAGE_SYMBOL_EX));
                if (!sym)
                    return -1;
                return sym->SectionNumber;
            }
            else
            {
                IMAGE_SYMBOL* sym = (IMAGE_SYMBOL*)(symtable + rel[i].SymbolTableIndex * IMAGE_SIZEOF_SYMBOL);
                if (!sym)
                    return -1;
                return sym->SectionNumber;
            }
        }

    return -1;
}

///////////////////////////////////////////////////////////////////////
struct LineInfoDataHeader
{
    int relocoff;
    short segment;
    short flags;
    int size;
};
struct LineInfoData
{
    int nameIndex;
    int numLines;
    int lineInfoSize;
};

struct LineInfoPair
{
    int offset;
    int line;
};

int PEImage::dumpDebugLineInfoCOFF()
{
    char* f3section = 0;
    char* f4section = 0;
	for(int s = 0; s < nsec; s++)
    {
        if (strncmp((char*)sec[s].Name, ".debug$S", 8) == 0)
        {
            DWORD* base = DPV<DWORD>(sec[s].PointerToRawData, sec[s].SizeOfRawData);
            if (!base || *base != 4)
                continue;
            DWORD* end = base + sec[s].SizeOfRawData / 4;
            int lineInfoTotalSize = 0;
            int lineInfoAccumulatedSize = 0;
            for (DWORD* p = base + 1; p < end; p += (p[1] + 3) / 4 + 2)
            {
                if (!f4section && p[0] == 0xf4)
                    f4section = (char*)p + 8;
                if (!f3section && p[0] == 0xf3)
                    f3section = (char*)p + 8;
                if (p[0] != 0xf2)
                    continue;

                lineInfoTotalSize = p[1];
                lineInfoAccumulatedSize = sizeof(LineInfoDataHeader);

                LineInfoDataHeader* pLineInfoDataHeader = (LineInfoDataHeader*) (p + 2);
                int section = getRelocationInSegment(s, (char*)pLineInfoDataHeader - (char*)base);
                const char* secname = findSectionSymbolName(section);
                printf("Sym: %s\n", secname ? secname : "<none>");

                DWORD* pLID = p + 2 + (sizeof(LineInfoDataHeader) / 4);
                while (lineInfoAccumulatedSize < lineInfoTotalSize && pLID < end)
                {
                    LineInfoData* pLineInfoData = (LineInfoData*) (pLID);

                    int* f3off = f4section ? (int*)(f4section + pLineInfoData->nameIndex) : 0;
                    const char* fname = f3off ? f3section + *f3off : "unknown";
                    printf("File: %s\n", fname);

                    LineInfoPair* lineInfoPairs = (LineInfoPair*) (pLineInfoData + 1);
                    for (int i = 0; i < pLineInfoData->numLines; ++i)
                        printf("\tOff 0x%x: Line %d\n", lineInfoPairs[i].offset, lineInfoPairs[i].line & 0x7fffffff);

                    pLID += (pLineInfoData->lineInfoSize / 4);
                    lineInfoAccumulatedSize += pLineInfoData->lineInfoSize;
                }
            }
        }
    }
    return 0;
}

int _pstrlen(const BYTE* &p)
{
	int len = *p++;
	if(len == 0xff && *p == 0)
	{
		len = p[1] | (p[2] << 8);
		p += 3;
	}
	return len;
}

unsigned _getIndex(const BYTE* &p)
{
    if (*p & 0x80)
    {
        p += 2;
        return ((p[-2] << 8) | p[-1]) & 0x7fff;
    }
    return *p++;
}

int PEImage::dumpDebugLineInfoOMF()
{
    std::vector<const unsigned char*> lnames;
    std::vector<const unsigned char*> llnames;
    const unsigned char* fname = 0;
    unsigned char* base = (unsigned char*) dump_base;
    if (*base != 0x80) // assume THEADR record
        return -1;
    unsigned char* end = base + dump_total_len;
    for(unsigned char* p = base; p < end; p += *(unsigned short*)(p + 1) + 3)
    {
        switch(*p)
        {
        case 0x80: // THEADR
            fname = p + 3; // pascal string
            break;
        case 0x96: // LNAMES
        {
            int len = *(unsigned short*)(p + 1);
            for(const unsigned char* q = p + 3; q < p + len + 2; q += _pstrlen (q)) // defined behaviour?
                lnames.push_back(q);
            break;
        }
        case 0xCA: // LLNAMES
        {
            int len = *(unsigned short*)(p + 1);
            for(const unsigned char* q = p + 3; q < p + len + 2; q += _pstrlen (q)) // defined behaviour?
                llnames.push_back(q);
            break;
        }
        case 0x95: // LINNUM
        {
            const unsigned char* q = p + 3;
            int basegrp = _getIndex(q);
            int baseseg = _getIndex(q);
            unsigned num = (p + *(unsigned short*)(p + 1) + 2 - q) / 6;
            const unsigned char* fn = fname;
            int flen = fn ? _pstrlen(fn) : 0;
            printf("File: %.*s, BaseSegment %d\n", flen, fn, baseseg);
            for (int i = 0; i < num; i++)
                printf("\tOff 0x%x: Line %d\n", *(int*)(q + 2 + 6 * i), *(unsigned short*)(p + 6 * i));
            break;
        }
        case 0xc5: // LINSYM
        {
            const unsigned char* q = p + 3;
            unsigned flags = *q++;
            unsigned pubname = _getIndex(q);
            unsigned num = (p + *(unsigned short*)(p + 1) + 2 - q) / 6;
            if (num == 0)
                break;
            const unsigned char* fn = fname;
            int flen = fn ? _pstrlen(fn) : 0;
            const unsigned char* sn = (pubname == 0 || pubname > lnames.size() ? 0 : lnames[pubname-1]);
            int slen = sn ? _pstrlen(sn) : 0;
            printf("Sym: %.*s\n", slen, sn);
            printf("File: %.*s\n", flen, fn);
            for (unsigned i = 0; i < num; i++)
                printf("\tOff 0x%x: Line %d\n", *(int*)(q + 2 + 6 * i), *(unsigned short*)(q + 6 * i));
            break;
        }
        default:
            break;
        }
    }
    return 0;
}

///////////////////////////////////////////////////////////////////////
int PEImage::findSection(unsigned int off) const
{
	off -= IMGHDR(OptionalHeader.ImageBase);
	for(int s = 0; s < nsec; s++)
		if(sec[s].VirtualAddress <= off && off < sec[s].VirtualAddress + sec[s].Misc.VirtualSize)
			return s;
	return -1;
}

template<typename SYM>
const char* PEImage::t_findSectionSymbolName(int s) const
{
	SYM* sym = 0;
	for(int i = 0; i < nsym; i += 1 + sym->NumberOfAuxSymbols)
	{
		sym = (SYM*) symtable + i;
        if (sym->SectionNumber == s && sym->StorageClass == IMAGE_SYM_CLASS_EXTERNAL)
        {
            static char sname[10] = { 0 };

		    if (sym->N.Name.Short == 0)
                return strtable + sym->N.Name.Long;
            return strncpy (sname, (char*)sym->N.ShortName, 8);
        }
	}
    return 0;
}

const char* PEImage::findSectionSymbolName(int s) const
{
    if (s < 0 || s >= nsec)
        return 0;
    if (!(sec[s].Characteristics & IMAGE_SCN_LNK_COMDAT))
        return 0;

    if (bigobj)
        return t_findSectionSymbolName<IMAGE_SYMBOL_EX> (s);
    else
        return t_findSectionSymbolName<IMAGE_SYMBOL> (s);
}

void PEImage::createSymbolCache()
{
	int sizeof_sym = bigobj ? sizeof(IMAGE_SYMBOL_EX) : IMAGE_SIZEOF_SYMBOL;
	for (int i = 0; i < nsym; ++i)
	{
		IMAGE_SYMBOL* sym = (IMAGE_SYMBOL*)(symtable + i * sizeof_sym);
		const char* symname = sym->N.Name.Short == 0 ? strtable + sym->N.Name.Long : (char*)sym->N.ShortName;
		int seg = bigobj ? ((IMAGE_SYMBOL_EX*)sym)->SectionNumber : sym->SectionNumber;
		if (seg)
		{
			unsigned long off = sym->Value;
			std::string key = symname;
			bool dllimport = !key.compare(0, 6, "__imp_"); // symname starts with "__imp_"
			symbolCache[key] = {seg, off, dllimport};
		}
		i += sym->NumberOfAuxSymbols;
	}
}

int PEImage::findSymbol(const char* name, unsigned long& off, bool& dllimport) const
{
	std::string key = name;
	auto it = symbolCache.find(key);
	if (it == symbolCache.end())
		it = symbolCache.find("_" + key);
	if (it == symbolCache.end())
		it = symbolCache.find("__imp_" + key);
	if (it == symbolCache.end())
		it = symbolCache.find("__imp__" + key);
	if (it != symbolCache.end())
	{
		off = it->second.off;
		dllimport = it->second.dllimport;
		return it->second.seg - 1;
	}
	return -1;
}

///////////////////////////////////////////////////////////////////////
int PEImage::countCVEntries() const
{
	return dirHeader ? dirHeader->cDir : 0;
}

OMFDirEntry* PEImage::getCVEntry(int i) const
{
	return dirEntry + i;
}


///////////////////////////////////////////////////////////////////////
// utilities
void* PEImage::alloc_aligned(unsigned int size, unsigned int align, unsigned int alignoff)
{
	if (align & (align - 1))
		return 0;

	unsigned int pad = align + sizeof(void*);
	char* p = (char*) malloc(size + pad);
	unsigned int off = (align + alignoff - sizeof(void*) - (p - (char*) 0)) & (align - 1);
	char* q = p + sizeof(void*) + off;
	((void**) q)[-1] = p;
	return q;
}

///////////////////////////////////////////////////////////////////////
void PEImage::free_aligned(void* p)
{
	void* q = ((void**) p)[-1];
	free(q);
}
