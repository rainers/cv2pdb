// Convert DMD CodeView debug information to PDB files
// Copyright (c) 2009-2010 by Rainer Schuetze, All Rights Reserved
//
// License for redistribution is given by the Artistic License 2.0
// see file LICENSE for further details

#ifndef __MSPDB_H__
#define __MSPDB_H__

#include <stdio.h>

namespace mspdb
{

struct MREUtil;
struct MREFile;
struct MREBag;
struct BufferDefaultAllocator;
struct EnumSC;
struct Stream;
struct EnumThunk;
struct EnumSyms;
struct EnumLines;
struct Dbg;
struct EnumSrc;
struct MREDrv;
struct MREngine;
struct EnumNameMap_Special;
struct MRECmp2;
struct PDB;
struct Src;
struct Mod;
struct StreamCached;
struct GSI;
struct TPI;
struct NameMap;
struct EnumNameMap;

#define MRECmp MRECmp2
#define PDBCommon PDB
#define SrcCommon Src
#define ModCommon Mod

#define MREUtil2 MREUtil
#define MREFile2 MREFile
#define MREBag2 MREBag
#define Mod2 Mod
#define GSI2 GSI
#define TPI2 TPI
#define NameMap2 NameMap
#define EnumNameMap2 EnumNameMap

struct DBI;
/*
#define DBICommon DBI
#define DBI2 DBI
*/

struct MREUtil {
public: virtual int MREUtil::FRelease(void);
public: virtual void MREUtil::EnumSrcFiles(int (__stdcall*)(struct MREUtil *,struct EnumFile &,enum EnumType),unsigned short const *,void *);
public: virtual void MREUtil::EnumDepFiles(struct EnumFile &,int (__stdcall*)(struct MREUtil *,struct EnumFile &,enum EnumType));
public: virtual void MREUtil::EnumAllFiles(int (__stdcall*)(struct MREUtil *,struct EnumFile &),unsigned short const *,void *);
public: virtual void MREUtil::Enumstructes(int (__stdcall*)(struct MREUtil *,struct Enumstruct &),unsigned short const *,void *);
public: virtual void MREUtil::SummaryStats(struct MreStats &);
};

struct MREFile {
public: virtual int MREFile::FOpenBag(struct MREBag * *,unsigned long);
public: virtual int MREFile::FnoteEndInclude(unsigned long);
public: virtual int MREFile::FnotestructMod(unsigned long,unsigned long);
public: virtual int MREFile::FnoteInlineMethodMod(unsigned long,char const *,unsigned long);
public: virtual int MREFile::FnoteLineDelta(unsigned long,int);
public: virtual void MREFile::EnumerateChangedstructes(int (__cdecl*)(unsigned long,struct MREFile *,int (MREFile::*)(unsigned long,unsigned long)));
public: virtual int MREFile::FnotestructTI(unsigned long,unsigned long);
public: virtual int MREFile::FIsBoring(void);
public: virtual int MREFile::FnotePchCreateUse(unsigned short const *,unsigned short const *);
};

struct MREBag {
public: virtual int MREBag::FAddDep(unsigned long,unsigned long,char const *,enum DEPON,unsigned long);
public: virtual int MREBag::FClose(void);
};

struct BufferDefaultAllocator {
public: virtual unsigned char * BufferDefaultAllocator::Alloc(long);
public: virtual unsigned char * BufferDefaultAllocator::AllocZeroed(long);
public: virtual void BufferDefaultAllocator::DeAlloc(unsigned char *);
};


struct EnumSC {
public: virtual int EnumSC::next(void);
public: virtual void EnumSC::get(unsigned short *,unsigned short *,long *,long *,unsigned long *);
public: virtual void EnumSC::getCrcs(unsigned long *,unsigned long *);
public: virtual bool EnumSC::fUpdate(long,long);
public: virtual int EnumSC::prev(void);
public: virtual int EnumSC::clone(struct EnumContrib * *);
public: virtual int EnumSC::locate(long,long);
};

struct Stream {
public: virtual long Stream::QueryCb(void);
public: virtual int Stream::Read(long,void *,long *);
public: virtual int Stream::Write(long,void *,long);
public: virtual int Stream::Replace(void *,long);
public: virtual int Stream::Append(void *,long);
public: virtual int Stream::Delete(void);
public: virtual int Stream::Release(void);
public: virtual int Stream::Read2(long,void *,long);
public: virtual int Stream::Truncate(long);
};

struct EnumThunk {
public: virtual void EnumThunk::release(void);
public: virtual void EnumThunk::reset(void);
public: virtual int EnumThunk::next(void);
public: virtual void EnumThunk::get(unsigned short *,long *,long *);
};

struct EnumSyms {
public: virtual void EnumSyms::release(void);
public: virtual void EnumSyms::reset(void);
public: virtual int EnumSyms::next(void);
public: virtual void EnumSyms::get(unsigned char * *);
public: virtual int EnumSyms::prev(void);
public: virtual int EnumSyms::clone(struct EnumSyms * *);
public: virtual int EnumSyms::locate(long,long);
};

struct EnumLines {
public: virtual void EnumLines::release(void);
public: virtual void EnumLines::reset(void);
public: virtual int EnumLines::next(void);
public: virtual bool EnumLines::getLines(unsigned long *,unsigned long *,unsigned short *,unsigned long *,unsigned long *,struct CV_Line_t *);
public: virtual bool EnumLines::getLinesColumns(unsigned long *,unsigned long *,unsigned short *,unsigned long *,unsigned long *,struct CV_Line_t *,struct CV_Column_t *);
public: virtual bool EnumLines::clone(struct EnumLines * *);
};

struct Dbg {
public: virtual int Dbg::Close(void);
public: virtual long Dbg::QuerySize(void);
public: virtual void Dbg::Reset(void);
public: virtual int Dbg::Skip(unsigned long);
public: virtual int Dbg::QueryNext(unsigned long,void *);
public: virtual int Dbg::Find(void *);
public: virtual int Dbg::Clear(void);
public: virtual int Dbg::Append(unsigned long,void const *);
public: virtual int Dbg::ReplaceNext(unsigned long,void const *);
public: virtual int Dbg::Clone(struct Dbg * *);
public: virtual long Dbg::QueryElementSize(void);
};

struct EnumSrc {
public: virtual void EnumSrc::release(void);
public: virtual void EnumSrc::reset(void);
public: virtual int EnumSrc::next(void);
public: virtual void EnumSrc::get(struct SrcHeaderOut const * *);
};

struct MREDrv {
public: virtual int MREDrv::FRelease(void);
public: virtual int MREDrv::FRefreshFileSysInfo(void);
public: virtual int MREDrv::FSuccessfulCompile(int,unsigned short const *,unsigned short const *);
public: virtual enum YNM MREDrv::YnmFileOutOfDate(struct SRCTARG &);
public: virtual int MREDrv::FFilesOutOfDate(struct CAList *);
public: virtual int MREDrv::FUpdateTargetFile(unsigned short const *,enum TrgType);
public: virtual void MREDrv::OneTimeInit(void);
};

struct MREngine {
public: virtual int MREngine::FDelete(void);
public: virtual int MREngine::FClose(int);
public: virtual void MREngine::QueryPdbApi(struct PDB * &,struct NameMap * &);
public: virtual void MREngine::_Reserved_was_QueryMreLog(void);
public: virtual void MREngine::QueryMreDrv(struct MREDrv * &);
public: virtual void MREngine::QueryMreCmp(struct MRECmp * &,struct TPI *);
public: virtual void MREngine::QueryMreUtil(struct MREUtil * &);
public: virtual int MREngine::FCommit(void);
};

struct MRECmp2 {
public: virtual int MRECmp2::FRelease(void);
public: virtual int MRECmp2::FOpenCompiland(struct MREFile * *,unsigned short const *,unsigned short const *);
public: virtual int MRECmp2::FCloseCompiland(struct MREFile *,int);
public: virtual int MRECmp2::FPushFile(struct MREFile * *,unsigned short const *,void *);
public: virtual struct MREFile * MRECmp2::PmrefilePopFile(void);
public: virtual int MRECmp::FStoreDepData(struct DepData *);
public: virtual int MRECmp::FRestoreDepData(struct DepData *);
public: virtual void MRECmp::structIsBoring(unsigned long);
};

//public: virtual void * Pool<16384>::AllocBytes(unsigned int);
//public: virtual void EnumSyms::get(unsigned char * *);
//public: virtual void * Pool<65536>::AllocBytes(unsigned int);
//public: virtual void EnumSyms::get(unsigned char * *);

typedef int __cdecl fnPDBOpen2W(unsigned short const *path,char const *mode,long *p,
				unsigned short *ext,unsigned int flags,struct PDB **pPDB);

struct PDB {
public: static int __cdecl PDBCommon::Open2W(unsigned short const *path,char const *mode,long *p,unsigned short *ext,unsigned int flags,struct PDB **pPDB);

public: virtual unsigned long PDB::QueryInterfaceVersion(void);
public: virtual unsigned long PDB::QueryImplementationVersion(void);
public: virtual long PDBCommon::QueryLastError(char * const);
public: virtual char * PDB::QueryPDBName(char * const);
public: virtual unsigned long PDB::QuerySignature(void);
public: virtual unsigned long PDB::QueryAge(void);
public: virtual int PDB::CreateDBI(char const *,struct DBI * *);
public: virtual int PDB::OpenDBI(char const *,char const *,struct DBI * *);
public: virtual int PDB::OpenTpi(char const *,struct TPI * *);
public: virtual int PDB::Commit(void);
public: virtual int PDB::Close(void);
public: virtual int PDBCommon::OpenStreamW(unsigned short const *,struct Stream * *);
public: virtual int PDB::GetEnumStreamNameMap(struct Enum * *);
public: virtual int PDB::GetRawBytes(int (__cdecl*)(void const *,long));
public: virtual unsigned long PDB::QueryPdbImplementationVersion(void);
public: virtual int PDB::OpenDBIEx(char const *,char const *,struct DBI * *,int (__stdcall*)(struct _tagSEARCHDEBUGINFO *));
public: virtual int PDBCommon::CopyTo(char const *,unsigned long,unsigned long);
public: virtual int PDB::OpenSrc(struct Src * *);
public: virtual long PDB::QueryLastErrorExW(unsigned short *,unsigned int);
public: virtual unsigned short * PDB::QueryPDBNameExW(unsigned short *,unsigned int);
public: virtual int PDB::QuerySignature2(struct _GUID *);
public: virtual int PDBCommon::CopyToW(unsigned short const *,unsigned long,unsigned long);
public: virtual int PDB::fIsSZPDB(void)const ;
public: virtual int PDB::containsW(unsigned short const *,unsigned long *);
public: virtual int PDB::CopyToW2(unsigned short const *,unsigned long,int (__cdecl*(__cdecl*)(void *,enum PCC))(void),void *);
public: virtual int PDB::OpenStreamEx(char const *,char const *,struct Stream * *);
};

struct Src {
public: virtual bool Src::Close(void);
public: virtual bool SrcCommon::Add(struct SrcHeader const *,void const *);
public: virtual bool Src::Remove(char const *);
public: virtual bool SrcCommon::QueryByName(char const *,struct SrcHeaderOut *)const ;
public: virtual bool Src::GetData(struct SrcHeaderOut const *,void *)const ;
public: virtual bool Src::GetEnum(struct EnumSrc * *)const ;
public: virtual bool Src::GetHeaderBlock(struct SrcHeaderBlock &)const ;
public: virtual bool Src::RemoveW(unsigned short *);
public: virtual bool Src::QueryByNameW(unsigned short *,struct SrcHeaderOut *)const ;
public: virtual bool Src::AddW(struct SrcHeaderW const *,void const *);
};

#include "pshpack1.h"

struct LineInfoEntry
{
	unsigned int offset;
	unsigned short line;
};

struct LineInfo
{
	unsigned int cntEntries;
	unsigned short unknown;
	LineInfoEntry entries[1]; // first entry { 0, 0x7fff }
};

struct SymbolChunk
{
	unsigned int chunkType; // seen 0xf1 (symbols), f2(??) f3 (FPO), f4 (MD5?), f5 (NEWFPO)
	unsigned int chunkSize; // 0x18a: size of compiler symbols

	// symbol entries
	// S_COMPILER_V4
	// S_MSTOOL_V4
};

struct SymbolData
{
	unsigned int magic; // 4: version? sizeof header?
	// followed by SymbolChunks
};

struct TypeChunk
{
	// see also codeview_type

	unsigned short len;
	unsigned short type;

	union
	{
		struct _refpdb // type 0x1515
		{
			unsigned int md5[4];
			unsigned int unknown;
			unsigned pdbname[1];
		} refpdb;
	};
};

struct TypeData
{
	unsigned int magic; // 4: version? sizeof header?
	// followed by TypeChunks
};

#include "poppack.h"

struct Mod {
public: virtual unsigned long Mod::QueryInterfaceVersion(void);
public: virtual unsigned long Mod::QueryImplementationVersion(void);
public: virtual int Mod::AddTypes(unsigned char *pTypeData,long cbTypeData);
public: virtual int Mod::AddSymbols(unsigned char *pSymbolData,long cbSymbolData);
public: virtual int Mod2::AddPublic(char const *,unsigned short,long); // forwards to AddPublic2(...,0)
public: virtual int ModCommon::AddLines(char const *fname,unsigned short sec,long off,long size,long off2,unsigned short firstline,unsigned char *pLineInfo,long cbLineInfo); // forwards to AddLinesW
public: virtual int Mod2::AddSecContrib(unsigned short sec,long off,long size,unsigned long secflags); // forwards to Mod2::AddSecContribEx(..., 0, 0)
public: virtual int ModCommon::QueryCBName(long *);
public: virtual int ModCommon::QueryName(char * const,long *);
public: virtual int Mod::QuerySymbols(unsigned char *,long *);
public: virtual int Mod::QueryLines(unsigned char *,long *);
public: virtual int Mod2::SetPvClient(void *);
public: virtual int Mod2::GetPvClient(void * *);
public: virtual int Mod2::QueryFirstCodeSecContrib(unsigned short *,long *,long *,unsigned long *);
public: virtual int Mod2::QueryImod(unsigned short *);
public: virtual int Mod2::QueryDBI(struct DBI * *);
public: virtual int Mod2::Close(void);
public: virtual int ModCommon::QueryCBFile(long *);
public: virtual int ModCommon::QueryFile(char * const,long *);
public: virtual int Mod::QueryTpi(struct TPI * *);
public: virtual int Mod2::AddSecContribEx(unsigned short sec,long off,long size,unsigned long secflags,unsigned long crc/*???*/,unsigned long);
public: virtual int Mod::QueryItsm(unsigned short *);
public: virtual int ModCommon::QuerySrcFile(char * const,long *);
public: virtual int Mod::QuerySupportsEC(void);
public: virtual int ModCommon::QueryPdbFile(char * const,long *);
public: virtual int Mod::ReplaceLines(unsigned char *,long);
public: virtual bool Mod::GetEnumLines(struct EnumLines * *);
public: virtual bool Mod::QueryLineFlags(unsigned long *);
public: virtual bool Mod::QueryFileNameInfo(unsigned long,unsigned short *,unsigned long *,unsigned long *,unsigned char *,unsigned long *);
public: virtual int Mod::AddPublicW(unsigned short const *,unsigned short,long,unsigned long);
public: virtual int Mod::AddLinesW(unsigned short const *fname,unsigned short sec,long off,long size,long off2,unsigned long firstline,unsigned char *plineInfo,long cbLineInfo);
public: virtual int Mod::QueryNameW(unsigned short * const,long *);
public: virtual int Mod::QueryFileW(unsigned short * const,long *);
public: virtual int Mod::QuerySrcFileW(unsigned short * const,long *);
public: virtual int Mod::QueryPdbFileW(unsigned short * const,long *);
public: virtual int Mod2::AddPublic2(char const *name,unsigned short sec,long off,unsigned long type);
public: virtual int Mod::InsertLines(unsigned char *,long);
public: virtual int Mod::QueryLines2(long,unsigned char *,long *);
};


struct DBI_part1 {
public: virtual unsigned long QueryImplementationVersion(void);
public: virtual unsigned long QueryInterfaceVersion(void);
public: virtual int OpenMod(char const *objName,char const *libName,struct Mod * *);
public: virtual int DeleteMod(char const *);
public: virtual int QueryNextMod(struct Mod *,struct Mod * *);
public: virtual int OpenGlobals(struct GSI * *);
public: virtual int OpenPublics(struct GSI * *);
public: virtual int AddSec(unsigned short sec,unsigned short flags,long offset,long cbseg);
public: virtual int QueryModFromAddr(unsigned short,long,struct Mod * *,unsigned short *,long *,long *);
public: virtual int QuerySecMap(unsigned char *,long *);
public: virtual int QueryFileInfo(unsigned char *,long *);
public: virtual void DumpMods(void);
public: virtual void DumpSecContribs(void);
public: virtual void DumpSecMap(void);
public: virtual int Close(void);
public: virtual int AddThunkMap(long *,unsigned int,long,struct SO *,unsigned int,unsigned short,long);
public: virtual int AddPublic(char const *,unsigned short,long);
public: virtual int getEnumContrib(struct Enum * *);
public: virtual int QueryTypeServer(unsigned char,struct TPI * *);
public: virtual int QueryItsmForTi(unsigned long,unsigned char *);
public: virtual int QueryNextItsm(unsigned char,unsigned char *);
public: virtual int reinitialize(void); // returns 0 (QueryLazyTypes in 10.0)
public: virtual int SetLazyTypes(int);
public: virtual int FindTypeServers(long *,char *);
public: virtual void noop(void); // noop (_Reserved_was_QueryMreLog in 10.0)
public: virtual int OpenDbg(enum DBGTYPE,struct Dbg * *);
public: virtual int QueryDbgTypes(enum DBGTYPE *,long *);
public: virtual int QueryAddrForSec(unsigned short *,long *,unsigned short,long,unsigned long,unsigned long);
};
struct DBI_part2 : public DBI_part1 {
// in mspdb100.dll:
public: virtual int QueryAddrForSecEx(unsigned short *,long *,unsigned short,long,unsigned long,unsigned long);
};

template<class BASE> 
struct DBI_BASE : public BASE {
public: virtual int QuerySupportsEC(void);
public: virtual int QueryPdb(struct PDB * *);
public: virtual int AddLinkInfo(struct LinkInfo *);
public: virtual int QueryLinkInfo(struct LinkInfo *,long *);
public: virtual unsigned long QueryAge(void)const ;
public: virtual int reinitialize2(void);  // returns 0 (QueryLazyTypes in 10.0)
public: virtual void FlushTypeServers(void);
public: virtual int QueryTypeServerByPdb(char const *,unsigned char *);
public: virtual int OpenModW(unsigned short const *objName,unsigned short const *libName,struct Mod * *);
public: virtual int DeleteModW(unsigned short const *);
public: virtual int AddPublicW(unsigned short const *name,unsigned short sec,long off,unsigned long type);
public: virtual int QueryTypeServerByPdbW(unsigned short const *,unsigned char *);
public: virtual int AddLinkInfoW(struct LinkInfoW *);
public: virtual int AddPublic2(char const *name,unsigned short sec,long off,unsigned long type);
public: virtual unsigned short QueryMachineType(void)const ;
public: virtual void SetMachineType(unsigned short);
public: virtual void RemoveDataForRva(unsigned long,unsigned long);
public: virtual int FStripped(void);
public: virtual int QueryModFromAddr2(unsigned short,long,struct Mod * *,unsigned short *,long *,long *,unsigned long *);
public: virtual int QueryNoOfMods(long *);
public: virtual int QueryMods(struct Mod * *,long);
public: virtual int QueryImodFromAddr(unsigned short,long,unsigned short *,unsigned short *,long *,long *,unsigned long *);
public: virtual int OpenModFromImod(unsigned short,struct Mod * *);
public: virtual int QueryHeader2(long,unsigned char *,long *);
public: virtual int FAddSourceMappingItem(unsigned short const *,unsigned short const *,unsigned long);
public: virtual int FSetPfnNotePdbUsed(void *,void (__cdecl*)(void *,unsigned short const *,int,int));
public: virtual int FCTypes(void);
public: virtual int QueryFileInfo2(unsigned char *,long *);
public: virtual int FSetPfnQueryCallback(void *,int (__cdecl*(__cdecl*)(void *,enum DOVC))(void));
};

struct DBI_VS9  : public DBI_BASE<DBI_part1> {};
struct DBI_VS10 : public DBI_BASE<DBI_part2> {};

struct DBI
{
    static bool isVS10;
    DBI_VS9 vs9;

    unsigned long QueryImplementationVersion() { return vs9.QueryImplementationVersion(); }
    unsigned long QueryInterfaceVersion() { return vs9.QueryInterfaceVersion(); }
    int Close() { return vs9.Close(); }
    int OpenMod(char const *objName,char const *libName,struct Mod * *pmod) { return vs9.OpenMod(objName,libName,pmod); }
    int AddSec(unsigned short sec,unsigned short flags,long offset,long cbseg) { return vs9.AddSec(sec,flags,offset,cbseg); }

    int AddPublic2(char const *name,unsigned short sec,long off,unsigned long type)
    {
        if(isVS10)
            return ((DBI_VS10*) &vs9)->AddPublic2(name, sec, off, type);
        return vs9.AddPublic2(name, sec, off, type);
    }
    void SetMachineType(unsigned short type)
    {
        if(isVS10)
            return ((DBI_VS10*) &vs9)->SetMachineType(type);
        return vs9.SetMachineType(type);
    }
};

struct StreamCached {
public: virtual long StreamCached::QueryCb(void);
public: virtual int StreamCached::Read(long,void *,long *);
public: virtual int StreamCached::Write(long,void *,long);
public: virtual int StreamCached::Replace(void *,long);
public: virtual int StreamCached::Append(void *,long);
public: virtual int StreamCached::Delete(void);
public: virtual int StreamCached::Release(void);
public: virtual int StreamCached::Read2(long,void *,long);
public: virtual int StreamCached::Truncate(long);
};

struct GSI {
public: virtual unsigned long GSI::QueryInterfaceVersion(void);
public: virtual unsigned long GSI::QueryImplementationVersion(void);
public: virtual unsigned char * GSI::NextSym(unsigned char *);
public: virtual unsigned char * GSI::HashSymW(unsigned short const *,unsigned char *);
public: virtual unsigned char * GSI2::NearestSym(unsigned short,long,long *);
public: virtual int GSI::Close(void);
public: virtual int GSI::getEnumThunk(unsigned short,long,struct EnumThunk * *);
public: virtual int GSI::QueryTpi(struct TPI * *); // returns 0
public: virtual int GSI::QueryTpi2(struct TPI * *); // returns 0
public: virtual unsigned char * GSI2::HashSymW2(unsigned short const *,unsigned char *); // same as GSI2::HashSymW
public: virtual int GSI::getEnumByAddr(struct EnumSyms * *);
};

struct TPI {
public: virtual unsigned long TPI::QueryInterfaceVersion(void);
public: virtual unsigned long TPI::QueryImplementationVersion(void);
public: virtual int TPI::QueryTi16ForCVRecord(unsigned char *,unsigned short *);
public: virtual int TPI::QueryCVRecordForTi16(unsigned short,unsigned char *,long *);
public: virtual int TPI::QueryPbCVRecordForTi16(unsigned short,unsigned char * *);
public: virtual unsigned short TPI::QueryTi16Min(void);
public: virtual unsigned short TPI::QueryTi16Mac(void);
public: virtual long TPI::QueryCb(void);
public: virtual int TPI::Close(void);
public: virtual int TPI::Commit(void);
public: virtual int TPI::QueryTi16ForUDT(char const *,int,unsigned short *);
public: virtual int TPI::SupportQueryTiForUDT(void);
public: virtual int TPI::fIs16bitTypePool(void);
public: virtual int TPI::QueryTiForUDT(char const *,int,unsigned long *);
public: virtual int TPI2::QueryTiForCVRecord(unsigned char *,unsigned long *);
public: virtual int TPI2::QueryCVRecordForTi(unsigned long,unsigned char *,long *);
public: virtual int TPI2::QueryPbCVRecordForTi(unsigned long,unsigned char * *);
public: virtual unsigned long TPI::QueryTiMin(void);
public: virtual unsigned long TPI::QueryTiMac(void);
public: virtual int TPI::AreTypesEqual(unsigned long,unsigned long);
public: virtual int TPI2::IsTypeServed(unsigned long);
public: virtual int TPI::QueryTiForUDTW(unsigned short const *,int,unsigned long *);
};


struct NameMap {
public: virtual int NameMap::close(void);
public: virtual int NameMap2::reinitialize(void);
public: virtual int NameMap2::getNi(char const *,unsigned long *);
public: virtual int NameMap2::getName(unsigned long,char const * *);
public: virtual int NameMap2::getEnumNameMap(struct Enum * *);
public: virtual int NameMap2::contains(char const *,unsigned long *);
public: virtual int NameMap::commit(void);
public: virtual int NameMap2::isValidNi(unsigned long);
public: virtual int NameMap2::getNiW(unsigned short const *,unsigned long *);
public: virtual int NameMap2::getNameW(unsigned long,unsigned short *,unsigned int *);
public: virtual int NameMap2::containsW(unsigned short const *,unsigned long *);
public: virtual int NameMap2::containsUTF8(char const *,unsigned long *);
public: virtual int NameMap2::getNiUTF8(char const *,unsigned long *);
public: virtual int NameMap2::getNameA(unsigned long,char const * *);
public: virtual int NameMap2::getNameW2(unsigned long,unsigned short const * *);
};

struct EnumNameMap {
public: virtual void EnumNameMap::release(void);
public: virtual void EnumNameMap::reset(void);
public: virtual int EnumNameMap::next(void);
public: virtual void EnumNameMap2::get(char const * *,unsigned long *);
};

struct EnumNameMap_Special {
public: virtual void EnumNameMap_Special::release(void);
public: virtual void EnumNameMap_Special::reset(void);
public: virtual int EnumNameMap_Special::next(void);
public: virtual void EnumNameMap_Special::get(char const * *,unsigned long *);
};

} // namespace mspdb

bool initMsPdb();
bool exitMsPdb();

mspdb::PDB* CreatePDB(wchar_t* pdbname);

extern char* mspdb_dll;

#endif // __MSPDB_H__
