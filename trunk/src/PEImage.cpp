// Convert DMD CodeView debug information to PDB files
// Copyright (c) 2009 by Rainer Schuetze, All Rights Reserved
//
// License for redistribution is given by the Artistic License 2.0
// see file LICENSE for further details

#include "PEImage.h"

extern "C" {
#include "mscvpdb.h"
}

#include <io.h>
#include <fcntl.h>
#include <ctype.h>
#include <direct.h>
#include <sys/stat.h>

///////////////////////////////////////////////////////////////////////
PEImage::PEImage(const char* iname)
: dump_base(0)
, dump_total_len(0)
, dirHeader(0)
, fd(-1)
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

	fd = open(iname, O_RDONLY | O_BINARY);
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
	return initPtr();
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
bool PEImage::replaceDebugSection (const void* data, int datalen)
{
	int align = hdr->OptionalHeader.FileAlignment;
	int align_len = datalen;
	int fill = 0;
	if (align > 0)
	{
		fill = (align - (dump_total_len % align)) % align;
		align_len = ((datalen + align - 1) / align) * align;
	}
	char* newdata = (char*) alloc_aligned(dump_total_len + fill + datalen, 0x1000);
	if(!newdata)
		return setError("cannot alloc new image");

	// assume there is place for another section because of section alignment
	int s;
	DWORD lastVirtualAddress = 0;
	for(s = 0; s < hdr->FileHeader.NumberOfSections; s++)
	{
		if (strcmp ((char*) sec [s].Name, ".debug") == 0)
			strcpy ((char*) sec [s].Name, ".ddebug");
		lastVirtualAddress = sec [s].VirtualAddress + sec[s].Misc.VirtualSize;
	}

	int salign_len = datalen;
	align = hdr->OptionalHeader.SectionAlignment;
	if (align > 0)
	{
		lastVirtualAddress = ((lastVirtualAddress + align - 1) / align) * align;
		salign_len = ((datalen + align - 1) / align) * align;
	}

	strcpy((char*) sec[s].Name, ".debug");
	sec[s].Misc.VirtualSize = align_len; // union with PhysicalAddress;
	sec[s].VirtualAddress = lastVirtualAddress;
	sec[s].SizeOfRawData = datalen;
	sec[s].PointerToRawData = dump_total_len + fill;
	sec[s].PointerToRelocations = 0;
	sec[s].PointerToLinenumbers = 0;
	sec[s].NumberOfRelocations = 0;
	sec[s].NumberOfLinenumbers = 0;
	sec[s].Characteristics = IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_DISCARDABLE | IMAGE_SCN_CNT_INITIALIZED_DATA;

	hdr->FileHeader.NumberOfSections++;
	hdr->OptionalHeader.SizeOfImage += salign_len;

	dbgDir->PointerToRawData = sec[s].PointerToRawData;
	dbgDir->AddressOfRawData = sec[s].PointerToRawData;
	dbgDir->SizeOfData = sec[s].SizeOfRawData;

	// append debug data chunk to existing file image
	memcpy(newdata, dump_base, dump_total_len);
	memset(newdata + dump_total_len, 0, fill);
	memcpy(newdata + dump_total_len + fill, data, datalen);

	free_aligned(dump_base);
	dump_base = newdata;
	dump_total_len += fill + datalen;

	return initPtr();
}

///////////////////////////////////////////////////////////////////////
bool PEImage::initPtr()
{
	dos = DPV<IMAGE_DOS_HEADER> (0);
	if(!dos)
		return setError("file too small for DOS header");
	if(dos->e_magic != IMAGE_DOS_SIGNATURE)
		return setError("this is not a DOS executable");

	hdr = DPV<IMAGE_NT_HEADERS32> (dos->e_lfanew);
	if(!hdr)
		return setError("no optional header found");

	if(hdr->Signature != IMAGE_NT_SIGNATURE)
		return setError("optional header does not have PE signature");
	if(hdr->FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER32))
		return setError("optional header too small");

	sec = IMAGE_FIRST_SECTION(hdr);

	if(hdr->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_DEBUG)
		return setError("too few entries in data directory");

	if(hdr->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].Size != 0x1c)
		return setError("unexpected size of DEBUG data directory entry");

	int off = hdr->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].VirtualAddress;
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
