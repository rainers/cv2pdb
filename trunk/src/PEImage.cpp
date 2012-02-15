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

///////////////////////////////////////////////////////////////////////
PEImage::PEImage(const char* iname)
: dump_base(0)
, dump_total_len(0)
, dirHeader(0)
, hdr32(0)
, hdr64(0)
, fd(-1)
, debug_aranges(0)
, debug_pubnames(0)
, debug_pubtypes(0)
, debug_info(0)
, debug_abbrev(0)
, debug_line(0)
, debug_frame(0)
, debug_str(0)
, debug_loc(0)
, debug_ranges(0)
, codeSegment(0)
{
	if(iname)
		load(iname);
}

PEImage::~PEImage()
{
	if(fd != -1)
		close(fd);
	if(dump_base)
		free_aligned(dump_base);
}

///////////////////////////////////////////////////////////////////////
bool PEImage::load(const char* iname)
{
	if (fd != -1)
		return setError("file already open");

	fd = sopen(iname, O_RDONLY | O_BINARY, SH_DENYWR);
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
	return initCVPtr(true) || initDWARFPtr(true);
}

///////////////////////////////////////////////////////////////////////
bool PEImage::save(const char* oname)
{
	if (fd != -1)
		return setError("file already open");

	if (!dump_base)
		return setError("no data to dump");

	fd = open(oname, O_WRONLY | O_CREAT | O_BINARY | O_TRUNC, S_IREAD | S_IWRITE | S_IEXEC);
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
        memset(&debugdir, 0, sizeof(debugdir));
	int xdatalen = datalen + sizeof(debugdir);

	// assume there is place for another section because of section alignment
	int s;
	DWORD lastVirtualAddress = 0;
    int cntSections = countSections();
	for(s = 0; s < cntSections; s++)
	{
		if (strcmp ((char*) sec [s].Name, ".debug") == 0)
		{
			if (s == cntSections - 1)
			{
				dump_total_len = sec[s].PointerToRawData;
				break;
			}
			strcpy ((char*) sec [s].Name, ".ddebug");
			printf("warning: .debug not last section, cannot remove section\n");
		}
		lastVirtualAddress = sec [s].VirtualAddress + sec[s].Misc.VirtualSize;
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

	// append debug data chunk to existing file image
	memcpy(newdata, dump_base, dump_total_len);
	memset(newdata + dump_total_len, 0, fill);
	memcpy(newdata + dump_total_len + fill, data, datalen);

    if(!dbgDir)
    {
        debugdir.Type = 2;
    }
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

    if(IMGHDR(Signature) != IMAGE_NT_SIGNATURE)
	    return setError("optional header does not have PE signature");
    if(IMGHDR(FileHeader.SizeOfOptionalHeader) < sizeof(IMAGE_OPTIONAL_HEADER32))
	    return setError("optional header too small");

    sec = hdr32 ? IMAGE_FIRST_SECTION(hdr32) : IMAGE_FIRST_SECTION(hdr64);

    if(IMGHDR(OptionalHeader.NumberOfRvaAndSizes) <= IMAGE_DIRECTORY_ENTRY_DEBUG)
	    return setError("too few entries in data directory");

    if(IMGHDR(OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].Size) != 0x1c)
	    return setError("unexpected size of DEBUG data directory entry");

    int off = IMGHDR(OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].VirtualAddress);
	dbgDir = RVA<IMAGE_DEBUG_DIRECTORY>(off, 0x1c);
	if (!dbgDir)
		return setError("debug directory not placed in image");
	if (dbgDir->Type != IMAGE_DEBUG_TYPE_CODEVIEW)
		return setError("debug directory not of type CodeView");

	cv_base = dbgDir->PointerToRawData;
	OMFSignature* sig = DPV<OMFSignature>(cv_base, dbgDir->SizeOfData);
	if (!sig)
		return setError("invalid debug data base address and size");
	if (memcmp(sig->Signature, "NB09", 4) != 0 && memcmp(sig->Signature, "NB11", 4) != 0)
	{
		// return setError("can only handle debug info of type NB09 and NB11");
		dirHeader = 0;
		dirEntry = 0;
		return true;
	}
	dirHeader = CVP<OMFDirHeader>(sig->filepos);
	if (!dirHeader)
		return setError("invalid cv dir header data base address");
	dirEntry = CVP<OMFDirEntry>(sig->filepos + dirHeader->cbDirHeader);
	if (!dirEntry)
		return setError("cv debug dir entries invalid");

	//if (dirHeader->cDir == 0)
	//	return setError("cv debug dir has no entries");

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

    if(IMGHDR(Signature) != IMAGE_NT_SIGNATURE)
	    return setError("optional header does not have PE signature");
    if(IMGHDR(FileHeader.SizeOfOptionalHeader) < sizeof(IMAGE_OPTIONAL_HEADER32))
	    return setError("optional header too small");

    dbgDir = 0;
    sec = hdr32 ? IMAGE_FIRST_SECTION(hdr32) : IMAGE_FIRST_SECTION(hdr64);
    int nsec = IMGHDR(FileHeader.NumberOfSections);
    const char* strtable = DPV<char>(IMGHDR(FileHeader.PointerToSymbolTable) + IMGHDR(FileHeader.NumberOfSymbols) * IMAGE_SIZEOF_SYMBOL);
    for(int s = 0; s < nsec; s++)
    {
        const char* name = (const char*) sec[s].Name;
        if(name[0] == '/')
        {
            int off = strtol(name + 1, 0, 10);
            name = strtable + off;
        }
        if(strcmp(name, ".debug_aranges") == 0)
            debug_aranges = DPV<char>(sec[s].PointerToRawData, sec[s].SizeOfRawData);
        if(strcmp(name, ".debug_pubnames") == 0)
            debug_pubnames = DPV<char>(sec[s].PointerToRawData, sec[s].SizeOfRawData);
        if(strcmp(name, ".debug_pubtypes") == 0)
            debug_pubtypes = DPV<char>(sec[s].PointerToRawData, sec[s].SizeOfRawData);
        if(strcmp(name, ".debug_info") == 0)
            debug_info = DPV<char>(sec[s].PointerToRawData, debug_info_length = sec[s].Misc.VirtualSize);
        if(strcmp(name, ".debug_abbrev") == 0)
            debug_abbrev = DPV<char>(sec[s].PointerToRawData, debug_abbrev_length = sec[s].Misc.VirtualSize);
        if(strcmp(name, ".debug_line") == 0)
            debug_line = DPV<char>(sec[s].PointerToRawData, debug_line_length = sec[s].Misc.VirtualSize);
        if(strcmp(name, ".debug_frame") == 0)
            debug_frame = DPV<char>(sec[s].PointerToRawData, sec[s].SizeOfRawData);
        if(strcmp(name, ".debug_str") == 0)
            debug_str = DPV<char>(sec[s].PointerToRawData, sec[s].SizeOfRawData);
        if(strcmp(name, ".debug_loc") == 0)
            debug_loc = DPV<char>(sec[s].PointerToRawData, sec[s].SizeOfRawData);
        if(strcmp(name, ".debug_ranges") == 0)
            debug_ranges = DPV<char>(sec[s].PointerToRawData, debug_ranges_length = sec[s].Misc.VirtualSize);
        if(strcmp(name, ".reloc") == 0)
            reloc = DPV<char>(sec[s].PointerToRawData, reloc_length = sec[s].Misc.VirtualSize);
        if(strcmp(name, ".text") == 0)
            codeSegment = s;
    }

    setError(0);

    return true;
}

int PEImage::findSection(unsigned int off) const
{
    off -= IMGHDR(OptionalHeader.ImageBase);
    int nsec = IMGHDR(FileHeader.NumberOfSections);
	for(int s = 0; s < nsec; s++)
        if(sec[s].VirtualAddress <= off && off < sec[s].VirtualAddress + sec[s].Misc.VirtualSize)
            return s;
    return -1;
}

int PEImage::findSymbol(const char* name, unsigned long& off) const
{
    IMAGE_SYMBOL* symtable = DPV<IMAGE_SYMBOL>(IMGHDR(FileHeader.PointerToSymbolTable));
    int syms = IMGHDR(FileHeader.NumberOfSymbols);
    const char* strtable = (const char*) (symtable + syms);
    for(int i = 0; i < syms; i++)
    {
        IMAGE_SYMBOL* sym = symtable + i;
        const char* symname = sym->N.Name.Short == 0 ? strtable + sym->N.Name.Long : (char*)sym->N.ShortName;
        if(strcmp(symname, name) == 0 || (symname[0] == '_' && strcmp(symname + 1, name) == 0))
        {
            off = sym->Value;
            return sym->SectionNumber;
        }
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
