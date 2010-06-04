// Convert DMD CodeView debug information to PDB files
// Copyright (c) 2009-2010 by Rainer Schuetze, All Rights Reserved
//
// License for redistribution is given by the Artistic License 2.0
// see file LICENSE for further details

#ifndef __CV2PDB_H__
#define __CV2PDB_H__

#include "LastError.h"
#include "mspdb.h"

#include <windows.h>

extern "C" {
#include "mscvpdb.h"
}

class PEImage;

class CV2PDB : public LastError
{
public:
	CV2PDB(PEImage& image);
	~CV2PDB();

	bool cleanup(bool commit);
	bool openPDB(const char* pdbname);

	bool setError(const char* msg);
	bool createModules();

	bool initLibraries();
	const BYTE* getLibrary(int i);
	bool initSegMap();

	int addFields(codeview_reftype* dfieldlist, const codeview_reftype* fieldlist, int maxdlen);
	int countFields(const codeview_reftype* fieldlist);

	int addAggregate(codeview_type* dtype, bool clss, int n_element, int fieldlist, int property, 
	                 int derived, int vshape, int structlen, const char*name);
	int addClass(codeview_type* dtype, int n_element, int fieldlist, int property, 
	                                   int derived, int vshape, int structlen, const char*name);
	int addStruct(codeview_type* dtype, int n_element, int fieldlist, int property, 
	                                    int derived, int vshape, int structlen, const char*name);

	int addPointerType(codeview_type* dtype, int type, int attr = 0x800A);
	int addPointerType(unsigned char* dtype, int type, int attr = 0x800A);

	int addFieldMember(codeview_fieldtype* dfieldtype, int attr, int offset, int type, const char* name);
	int addFieldStaticMember(codeview_fieldtype* dfieldtype, int attr, int type, const char* name);
	int addFieldNestedType(codeview_fieldtype* dfieldtype, int type, const char* name);

	void checkUserTypeAlloc(int size = 1000, int add = 10000);

	const codeview_type* getTypeData(int type);
	const codeview_type* getUserTypeData(int type);
	const codeview_type* findCompleteClassType(const codeview_type* cvtype);

	int findMemberFunctionType(codeview_symbol* lastGProcSym, int thisPtrType);

	int sizeofClassType(const codeview_type* cvtype);
	int sizeofBasicType(int type);
	int sizeofType(int type);

	// to be used when writing new type only to avoid double translation
	int translateType(int type);

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
	const char* appendAssocArray(int keyType, int elemType);
	const char* appendDelegate(int thisType, int funcType);
	int appendObjectType (int object_derived_type);
	int appendPointerType(int pointedType, int attr);
	int appendTypedef(int type, const char* name);
	int appendComplex(int cplxtype, int basetype, int elemsize, const char* name);
	void appendTypedefs();

	bool initGlobalTypes();
	bool initGlobalSymbols();

	bool addTypes();
	bool addSrcLines();
	bool addPublics();

	bool addUdtSymbol(int type, const char* name);

	// returns new destSize
	int copySymbols(BYTE* srcSymbols, int srcSize, BYTE* destSymbols, int destSize);

	bool writeSymbols(mspdb::Mod* mod, DWORD* data, int databytes, int prefix, bool addGlobals);
	bool addSymbols(mspdb::Mod* mod, BYTE* symbols, int cb, bool addGlobals);
	bool addSymbols(int iMod, BYTE* symbols, int cb, bool addGlobals);
	bool addSymbols();

	bool markSrcLineInBitmap(int segIndex, int adr);
	bool createSrcLineBitmap();
	int  getNextSrcLine(int seg, unsigned int off);

	bool writeImage(const char* opath);

	mspdb::Mod* globalMod();

// private:
	BYTE* libraries;

	PEImage& img;

	mspdb::PDB* pdb;
	mspdb::DBI *dbi;
	mspdb::TPI *tpi;

	mspdb::Mod** modules;
	mspdb::Mod* globmod;
	int countEntries;

	OMFSignatureRSDS* rsds;

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

	int nextUserType;
	int objectType;

	// D named types
	int typedefs[20];
	int translatedTypedefs[20];
	int cntTypedefs;

	bool useGlobalMod;
	bool thisIsNotRef;
	bool v3;
	const char* lastError;

	int srcLineSections;
	char** srcLineStart; // array of bitmaps per segment, indicating whether src line start is available for corresponding address

	double Dversion;
};


#endif //__CV2PDB_H__