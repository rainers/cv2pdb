// Convert DMD CodeView debug information to PDB files
// Copyright (c) 2009-2010 by Rainer Schuetze, All Rights Reserved
//
// License for redistribution is given by the Artistic License 2.0
// see file LICENSE for further details

#ifndef __CV2PDB_H__
#define __CV2PDB_H__

#include <stdint.h>

#include "LastError.h"
#include "mspdb.h"
#include "readDwarf.h"

#include <windows.h>
#include <map>

extern "C" {
	#include "mscvpdb.h"
	#include "dcvinfo.h"
}

class PEImage;
struct DWARF_InfoData;
struct DWARF_CompilationUnit;
class CFIIndex;

class CV2PDB : public LastError
{
public:
	CV2PDB(PEImage& image, DebugLevel debug);
	~CV2PDB();

	bool cleanup(bool commit);
	bool openPDB(const TCHAR* pdbname, const TCHAR* pdbref);

	bool setError(const char* msg);
	bool createModules();

	bool initLibraries();
	const BYTE* getLibrary(int i);
	bool initSegMap();

	enum
	{
		kCmdAdd,
		kCmdCount,
		kCmdNestedTypes,
		kCmdOffsetFirstVirtualMethod,
		kCmdHasClassTypeEnum,
		kCmdCountBaseClasses
	};
	int _doFields(int cmd, codeview_reftype* dfieldlist, const codeview_reftype* fieldlist, int arg);
	int addFields(codeview_reftype* dfieldlist, const codeview_reftype* fieldlist, int maxdlen);
	int countFields(const codeview_reftype* fieldlist);
	int countNestedTypes(const codeview_reftype* fieldlist, int type);

	int addAggregate(codeview_type* dtype, bool clss, int n_element, int fieldlist, int property,
	                 int derived, int vshape, int structlen, const char*name, const char*uniquename);
	int addClass(codeview_type* dtype, int n_element, int fieldlist, int property,
	                                   int derived, int vshape, int structlen, const char*name, const char*uniquename = 0);
	int addStruct(codeview_type* dtype, int n_element, int fieldlist, int property,
	                                    int derived, int vshape, int structlen, const char*name, const char*uniquename = 0);
	int addEnum(codeview_type* dtype, int count, int fieldlist, int property,
	                                  int type, const char*name);

	int addPointerType(codeview_type* dtype, int type, int attr = 0x800A);
	int addPointerType(unsigned char* dtype, int type, int attr = 0x800A);

	int addFieldMember(codeview_fieldtype* dfieldtype, int attr, int offset, int type, const char* name);
	int addFieldStaticMember(codeview_fieldtype* dfieldtype, int attr, int type, const char* name);
	int addFieldNestedType(codeview_fieldtype* dfieldtype, int type, const char* name);
	int addFieldEnumerate(codeview_fieldtype* dfieldtype, const char* name, int val);

	void checkUserTypeAlloc(int size = 1000, int add = 10000);
	void checkGlobalTypeAlloc(int size, int add = 1000);
	void checkUdtSymbolAlloc(int size, int add = 10000);
	void checkDWARFTypeAlloc(int size, int add = 10000);
	void writeUserTypeLen(codeview_type* type, int len);

	const codeview_type* getTypeData(int type);
	const codeview_type* getUserTypeData(int type);
	const codeview_type* getConvertedTypeData(int type);
	const codeview_type* findCompleteClassType(const codeview_type* cvtype, int* ptype = 0);

	int findMemberFunctionType(codeview_symbol* lastGProcSym, int thisPtrType);
	int createEmptyFieldListType();

	int fixProperty(int type, int prop, int fieldType);
	bool derivesFromObject(const codeview_type* cvtype);
	bool isCppInterface(const codeview_type* cvtype);
	bool isClassType(int type);

	int sizeofClassType(const codeview_type* cvtype);
	int sizeofBasicType(int type);
	int sizeofType(int type);

	// to be used when writing new type only to avoid double translation
	int translateType(int type);
	int getBaseClass(const codeview_type* cvtype);
	int countBaseClasses(const codeview_type* cvtype);

	bool nameOfBasicType(int type, char* name, int maxlen);
	bool nameOfType(int type, char* name, int maxlen);
	bool nameOfDynamicArray(int indexType, int elemType, char* name, int maxlen);
	bool nameOfAssocArray(int indexType, int elemType, char* name, int maxlen);
	bool nameOfDelegate(int thisType, int funcType, char* name, int maxlen);
	bool nameOfOEMType(codeview_oem_type* oem, char* name, int maxlen);
	bool nameOfModifierType(int type, int mod, char* name, int maxlen);

	int numeric_leaf(int* value, const void* leaf);
	int copy_leaf(unsigned char* dp, int& dpos, const unsigned char* p, int& pos);

	const char* appendDynamicArray(int indexType, int elemType);
	const char* appendDelegate(int thisType, int funcType);
	int  appendAssocArray2068(codeview_type* dtype, int keyType, int elemType);
	int  appendAssocArray(codeview_type* dtype, int keyType, int elemType);
	int  appendObjectType (int object_derived_type, int enumType, const char* classSymbol);
	int  appendPointerType(int pointedType, int attr);
	int  appendModifierType(int type, int attr);
	int  appendTypedef(int type, const char* name, bool saveTranslation = true);
	int  appendComplex(int cplxtype, int basetype, int elemsize, const char* name);
	void appendTypedefs();
	int  appendEnumerator(const char* typeName, const char* enumName, int enumValue, int prop);
	int  appendClassTypeEnum(const codeview_type* fieldlist, int type, const char* name);
	void appendStackVar(const char* name, int type, Location& loc, Location& cfa);
	void appendGlobalVar(const char* name, int type, int seg, int offset);
	bool appendEndArg();
	void appendEnd();
	void appendLexicalBlock(DWARF_InfoData& id, unsigned int proclo);

	bool hasClassTypeEnum(const codeview_type* fieldlist);
	bool insertClassTypeEnums();
	int  insertBaseClass(const codeview_type* fieldlist, int type);

	bool initGlobalTypes();
	bool initGlobalSymbols();

	bool addTypes();
	bool addSrcLines();
	bool addSrcLines14();
	bool addPublics();

	codeview_symbol* findUdtSymbol(int type);
	codeview_symbol* findUdtSymbol(const char* name);
	bool addUdtSymbol(int type, const char* name);
	void ensureUDT(int type, const codeview_type* cvtype);

	// returns new destSize
	int copySymbols(BYTE* srcSymbols, int srcSize, BYTE* destSymbols, int destSize);

	bool writeSymbols(mspdb::Mod* mod, DWORD* data, int databytes, int prefix, bool addGlobals);
	bool addSymbols(mspdb::Mod* mod, BYTE* symbols, int cb, bool addGlobals);
	bool addSymbols(int iMod, BYTE* symbols, int cb, bool addGlobals);
	bool addSymbols();

	bool markSrcLineInBitmap(int segIndex, int adr);
	bool createSrcLineBitmap();
	int  getNextSrcLine(int seg, unsigned int off);

	bool writeImage(const TCHAR* opath, PEImage& exeImage);

	mspdb::Mod* globalMod();

	// DWARF
	bool createDWARFModules();
	bool addDWARFTypes();
	bool addDWARFLines();
	bool addDWARFPublics();
	bool writeDWARFImage(const TCHAR* opath);

	bool addDWARFSectionContrib(mspdb::Mod* mod, unsigned long pclo, unsigned long pchi);
	bool addDWARFProc(DWARF_InfoData& id, DIECursor cursor);
	int  addDWARFStructure(DWARF_InfoData& id, DIECursor cursor);
	int  addDWARFFields(DWARF_InfoData& structid, DIECursor cursor, int off);
	int  addDWARFArray(DWARF_InfoData& arrayid, DIECursor cursor);
	int  addDWARFBasicType(const char*name, int encoding, int byte_size);
	int  addDWARFEnum(DWARF_InfoData& enumid, DIECursor cursor);
	int  getTypeByDWARFPtr(byte* ptr);
	int  getDWARFTypeSize(const DIECursor& parent, byte* ptr);
	void getDWARFArrayBounds(DWARF_InfoData& arrayid, DIECursor cursor,
		int& basetype, int& lowerBound, int& upperBound);
	void getDWARFSubrangeInfo(DWARF_InfoData& subrangeid, const DIECursor& parent,
		int& basetype, int& lowerBound, int& upperBound);
	int getDWARFBasicType(int encoding, int byte_size);

	void build_cfi_index();
	bool mapTypes();
	bool createTypes();

// private:
	BYTE* libraries;

	PEImage& img;
	CFIIndex* cfi_index;

	mspdb::PDB* pdb;
	mspdb::DBI *dbi;
	mspdb::TPI *tpi;
	mspdb::TPI *ipi;

	mspdb::Mod** modules;
	mspdb::Mod* globmod;
	int countEntries;

	OMFSignatureRSDS* rsds;
	int rsdsLen;

	OMFSegMap* segMap;
	OMFSegMapDesc* segMapDesc;
	int* segFrame2Index;

	OMFGlobalTypes* globalTypeHeader;

	unsigned char* globalTypes;
	int cbGlobalTypes;
	int allocGlobalTypes;

	unsigned char* userTypes;
	int* pointerTypes;
	int cbUserTypes;
	int allocUserTypes;

	unsigned char* globalSymbols;
	int cbGlobalSymbols;

	unsigned char* staticSymbols;
	int cbStaticSymbols;

	unsigned char* udtSymbols;
	int cbUdtSymbols;
	int allocUdtSymbols;

	unsigned char* dwarfTypes;
	int cbDwarfTypes;
	int allocDwarfTypes;

	int nextUserType;
	int nextDwarfType;
	int objectType;

	int emptyFieldListType;
	int classEnumType;
	int ifaceEnumType;
	int cppIfaceEnumType;
	int structEnumType;

	int classBaseType;
	int ifaceBaseType;
	int cppIfaceBaseType;
	int structBaseType;

	// D named types
	int typedefs[20];
	int translatedTypedefs[20];
	int cntTypedefs;

	bool addClassTypeEnum;
	bool addStringViewHelper;
	bool addObjectViewHelper;
	bool methodListToOneMethod;
	bool removeMethodLists;
	bool useGlobalMod;
	bool thisIsNotRef;
	bool v3;
	DebugLevel debug;
	const char* lastError;

	int srcLineSections;
	char** srcLineStart; // array of bitmaps per segment, indicating whether src line start is available for corresponding address

	double Dversion;

	// DWARF
	int codeSegOff;
	std::unordered_map<byte*, int> mapOffsetToType;

	// Default lower bound for the current compilation unit. This depends on
	// the language of the current unit.
	unsigned currentDefaultLowerBound;

	// Value of the DW_AT_low_pc attribute for the current compilation unit.
	// Specify the default base address for use in location lists and range
	// lists.
	uint32_t currentBaseAddress;
};

#endif //__CV2PDB_H__
