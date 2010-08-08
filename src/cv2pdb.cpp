// Convert DMD CodeView debug information to PDB files
// Copyright (c) 2009-2010 by Rainer Schuetze, All Rights Reserved
//
// License for redistribution is given by the Artistic License 2.0
// see file LICENSE for further details

#include "cv2pdb.h"
#include "PEImage.h"
#include "symutil.h"
#include "cvutil.h"

#include <stdio.h>
#include <direct.h>

#define REMOVE_LF_DERIVED  1  // types wrong by DMD
#define PRINT_INTERFACEVERSON 0

CV2PDB::CV2PDB(PEImage& image) 
: img(image), pdb(0), dbi(0), libraries(0), rsds(0), modules(0), globmod(0)
, segMap(0), segMapDesc(0), segFrame2Index(0), globalTypeHeader(0)
, globalTypes(0), cbGlobalTypes(0), allocGlobalTypes(0)
, userTypes(0), cbUserTypes(0), allocUserTypes(0)
, globalSymbols(0), cbGlobalSymbols(0), staticSymbols(0), cbStaticSymbols(0)
, udtSymbols(0), cbUdtSymbols(0), allocUdtSymbols(0)
, srcLineStart(0), srcLineSections(0)
, pointerTypes(0)
, Dversion(2)
, classEnumType(0), ifaceEnumType(0), cppIfaceEnumType(0), structEnumType(0)
, classBaseType(0), ifaceBaseType(0), cppIfaceBaseType(0), structBaseType(0)
{
	memset(typedefs, 0, sizeof(typedefs));
	memset(translatedTypedefs, 0, sizeof(translatedTypedefs));
	cntTypedefs = 0;

	addClassTypeEnum = true;
	useGlobalMod = true;
	thisIsNotRef = true;
	v3 = true;
	countEntries = img.countCVEntries(); 
}

CV2PDB::~CV2PDB()
{
	cleanup(false);
}

bool CV2PDB::cleanup(bool commit)
{
	if (modules)
		for (int m = 0; m < countEntries; m++)
			if (modules[m])
				modules[m]->Close();
	delete [] modules;
	if (globmod)
		globmod->Close();

	if (dbi)
		dbi->SetMachineType(0x14c);

	if (dbi)
		dbi->Close();
	if (tpi)
		tpi->Close();
	if (pdb)
		pdb->Commit();
	if (pdb)
		pdb->Close();

	if (rsds)
		delete [] (char*) rsds;
	if (globalTypes)
		free(globalTypes);
	if (userTypes)
		free(userTypes);
	if (udtSymbols)
		free(udtSymbols);
	delete [] pointerTypes;

	for(int i = 0; i < srcLineSections; i++)
		delete [] srcLineStart[i];
	delete [] srcLineStart;
	srcLineStart = 0;
	srcLineSections =  0;

	delete [] segFrame2Index;
	segFrame2Index = 0;

	globalTypes = 0;
	cbGlobalTypes = 0;
	allocGlobalTypes = 0;
	userTypes = 0;
	cbUserTypes = 0;
	allocUserTypes = 0;
	globalSymbols = 0;
	cbGlobalSymbols = 0;
	staticSymbols = 0;
	cbStaticSymbols = 0;
	udtSymbols = 0;
	cbUdtSymbols = 0;
	allocUdtSymbols = 0;
	modules = 0;
	globmod = 0;
	countEntries = 0;
	dbi = 0;
	pdb = 0;
	rsds = 0;
	segMap = 0;
	segMapDesc = 0;
	globalTypeHeader = 0;
	pointerTypes = 0;
	memset(typedefs, 0, sizeof(typedefs));
	memset(translatedTypedefs, 0, sizeof(translatedTypedefs));
	cntTypedefs = 0;

	return true;
}

bool CV2PDB::openPDB(const char* pdbname)
{
	wchar_t pdbnameW[260]; // = L"c:\\tmp\\aa\\ddoc4.pdb";
	mbstowcs (pdbnameW, pdbname, 260);

	if (!initMsPdb ())
		return setError("cannot load PDB helper DLL");
	pdb = CreatePDB (pdbnameW);
	if (!pdb)
		return setError("cannot create PDB file");

#if PRINT_INTERFACEVERSON
	printf("PDB::QueryInterfaceVersion() = %d\n", pdb->QueryInterfaceVersion());
	printf("PDB::QueryImplementationVersion() = %d\n", pdb->QueryImplementationVersion());
	printf("PDB::QueryPdbImplementationVersion() = %d\n", pdb->QueryPdbImplementationVersion());
#endif

	rsds = (OMFSignatureRSDS *) new char[24 + strlen(pdbname) + 1]; // sizeof(OMFSignatureRSDS) without name
	memcpy (rsds->Signature, "RSDS", 4);
	pdb->QuerySignature2(&rsds->guid);
	rsds->unknown = pdb->QueryAge();
	strcpy(rsds->name, pdbname);

	int rc = pdb->CreateDBI("", &dbi);
	if (rc <= 0 || !dbi)
		return setError("cannot create DBI");

#if PRINT_INTERFACEVERSON
	printf("DBI::QueryInterfaceVersion() = %d\n", dbi->QueryInterfaceVersion());
	printf("DBI::QueryImplementationVersion() = %d\n", dbi->QueryImplementationVersion());
#endif

	rc = pdb->OpenTpi("", &tpi);
	if (rc <= 0 || !tpi)
		return setError("cannot create TPI");

#if PRINT_INTERFACEVERSON
	printf("TPI::QueryInterfaceVersion() = %d\n", tpi->QueryInterfaceVersion());
	printf("TPI::QueryImplementationVersion() = %d\n", tpi->QueryImplementationVersion());
#endif

	return true;
}

bool CV2PDB::setError(const char* msg) 
{ 
	char pdbmsg[256];
	if(pdb)
		pdb->QueryLastError (pdbmsg);
	return LastError::setError(msg); 
}

bool CV2PDB::createModules()
{
	// assumes libraries and segMap initialized
	countEntries = img.countCVEntries();
	modules = new mspdb::Mod* [countEntries];
	memset (modules, 0, countEntries * sizeof(*modules));

	for (int m = 0; m < countEntries; m++)
	{
		OMFDirEntry* entry = img.getCVEntry(m);
		if (entry->SubSection == sstModule)
		{
			OMFModule* module   = img.CVP<OMFModule>(entry->lfo);
			OMFSegDesc* segDesc = img.CVP<OMFSegDesc>(entry->lfo + sizeof(OMFModule));
			BYTE* pname         = img.CVP<BYTE>(entry->lfo + sizeof(OMFModule) + sizeof(OMFSegDesc) * module->cSeg);
			char *name = p2c(pname);
			const BYTE* plib = getLibrary (module->iLib);
			const char* lib = (!plib || !*plib ? name : p2c(plib, 1));

			mspdb::Mod* mod;
			if (useGlobalMod)
			{
				mod = globalMod();
				if(!mod)
					return false;
			}
			else
			{
				if (modules[entry->iMod])
				{
					modules[entry->iMod]->Close();
					modules[entry->iMod] = 0;
				}
				int rc = dbi->OpenMod(name, lib, &modules[entry->iMod]);
				if (rc <= 0 || !modules[entry->iMod])
					return setError("cannot create mod");
				mod = modules[entry->iMod];
			}
#if PRINT_INTERFACEVERSON
			static bool once;
			if(!once)
			{
				printf("Mod::QueryInterfaceVersion() = %d\n", mod->QueryInterfaceVersion());
				printf("Mod::QueryImplementationVersion() = %d\n", mod->QueryImplementationVersion());
				once = true;
			}
#endif

			for (int s = 0; s < module->cSeg; s++)
			{
				int segIndex = segDesc[s].Seg;
				int segFlags = 0;
				if (segMap && segIndex < segMap->cSeg)
					segFlags = segMapDesc[segIndex].flags;
				segFlags = 0x60101020; // 0x40401040, 0x60500020; // TODO
				int rc = mod->AddSecContrib(segIndex, segDesc[s].Off, segDesc[s].cbSeg, segFlags);
				if (rc <= 0)
					return setError("cannot add section contribution to module");
			}
		}
	}
	return true;
}

mspdb::Mod* CV2PDB::globalMod()
{
	if (!globmod)
	{
		int rc = dbi->OpenMod("__Globals", "__Globals", &globmod);
		if (rc <= 0 || !globmod)
			setError("cannot create global module");
	}
	return globmod;
}

bool CV2PDB::initLibraries()
{
	libraries = 0;
	for (int m = 0; m < countEntries; m++)
		if (img.getCVEntry(m)->SubSection == sstLibraries)
			libraries = img.CVP<BYTE> (img.getCVEntry(m)->lfo);

	return true;
}

const BYTE* CV2PDB::getLibrary(int i) 
{ 
	if (!libraries)
		return 0;
	const BYTE* p = libraries;
	for (int j = 0; j < i; j++)
		p += *p + 1;
	return p;
}

bool CV2PDB::initSegMap()
{
	for (int m = 0; m < countEntries; m++)
	{
		OMFDirEntry* entry = img.getCVEntry(m);
		switch(entry->SubSection)
		{
		case sstSegMap:
			segMap = img.CVP<OMFSegMap>(entry->lfo);
			segMapDesc = img.CVP<OMFSegMapDesc>(entry->lfo + sizeof(OMFSegMap));
			int maxframe = -1;
			for (int s = 0; s < segMap->cSeg; s++)
			{
				int rc = dbi->AddSec(segMapDesc[s].frame, segMapDesc[s].flags, segMapDesc[s].offset, segMapDesc[s].cbSeg);
				if (rc <= 0)
					return setError("cannot add section");
				if (segMapDesc[s].frame > maxframe)
					maxframe = segMapDesc[s].frame;
			}

			segFrame2Index = new int[maxframe + 1];
			memset(segFrame2Index, -1, (maxframe + 1) * sizeof(*segFrame2Index));
			for (int s = 0; s < segMap->cSeg; s++)
				segFrame2Index[segMapDesc[s].frame] = s;
			break;
		}
	}
	return true;
}

int CV2PDB::numeric_leaf(int* value, const void* leaf)
{
	int length = ::numeric_leaf(value, leaf);
	if(length == 0)
		setError("unsupported numeric leaf");
	return length;
}

int CV2PDB::copy_leaf(unsigned char* dp, int& dpos, const unsigned char* p, int& pos)
{
	int value;
	int leaf_len = numeric_leaf(&value, p + pos);
	memcpy(dp + dpos, p + pos, leaf_len);
	pos += leaf_len;
	dpos += leaf_len;
	return leaf_len;
}

static int copy_p2dsym(unsigned char* dp, int& dpos, const unsigned char* p, int& pos, int maxdlen)
{
	int len = dsym2c(p + pos + 1, p[pos], (char*) dp + dpos, maxdlen - dpos) + 1;
	dpos += len;
	pos += p[pos] + 1;
	return len;
}

// if dfieldlist == 0, count fields
int CV2PDB::_doFields(int cmd, codeview_reftype* dfieldlist, const codeview_reftype* fieldlist, int arg)
{
	int maxdlen = (cmd == kCmdAdd ? arg : 0);
	int len = fieldlist->fieldlist.len - 2;
	const unsigned char* p = fieldlist->fieldlist.list;
	unsigned char* dp = dfieldlist ? dfieldlist->fieldlist.list : 0;
	int pos = 0, dpos = 0;
	int leaf_len, value;
	int nested_types = 0;
	int base_classes = 0;
	int test_nested_type = (cmd == kCmdNestedTypes ? arg : 0);

	int cntFields = 0;
	int prev = pos;
	while (pos < len && !hadError())
	{
		if (p[pos] >= 0xf1)       /* LF_PAD... */
		{
			pos += p[pos] & 0x0f;
			continue;
		}
		if(pos & 3)
		{
			setError("bad field alignment!");
			break;
		}

		prev = pos;
		const codeview_fieldtype* fieldtype = (const codeview_fieldtype*)(p + pos);
		codeview_fieldtype* dfieldtype = (codeview_fieldtype*)(dp + dpos);
		int copylen = 0;

		switch (fieldtype->common.id)
		{
		case LF_ENUMERATE_V1:
			if (dp && v3)
			{
				dfieldtype->enumerate_v3.id = LF_ENUMERATE_V3;
				dfieldtype->enumerate_v3.attribute = fieldtype->enumerate_v1.attribute;
				pos += sizeof(fieldtype->enumerate_v1) - sizeof(fieldtype->enumerate_v1.value);
				dpos += sizeof(fieldtype->enumerate_v3) - sizeof(fieldtype->enumerate_v3.value);
				copy_leaf(dp, dpos, p, pos);
				copy_p2dsym(dp, dpos, p, pos, maxdlen);
			}
			else
			{
				leaf_len = numeric_leaf(&value, &fieldtype->enumerate_v1.value);
				copylen = 2 + 2 + leaf_len + p[pos + 4 + leaf_len] + 1; // id,attr,value,name
			}
			break;

		case LF_ENUMERATE_V3:
			leaf_len = numeric_leaf(&value, &fieldtype->enumerate_v3.value);
			copylen = 2 + 2 + leaf_len + strlen((const char*) p + pos + 4 + leaf_len) + 1; // id,attr,value,name
			break;

		case LF_MEMBER_V1:
			if (dp)
			{
				dfieldtype->member_v2.id = v3 ? LF_MEMBER_V3 : LF_MEMBER_V2;
				dfieldtype->member_v2.attribute = fieldtype->member_v1.attribute;
				dfieldtype->member_v2.type = translateType(fieldtype->member_v1.type);
				dpos += sizeof(dfieldtype->member_v2.id) + sizeof(dfieldtype->member_v2.attribute) + sizeof(dfieldtype->member_v2.type);
			}
			pos  += sizeof(dfieldtype->member_v1.id) + sizeof(dfieldtype->member_v1.attribute) + sizeof(dfieldtype->member_v1.type);
			if (dp && v3)
			{
				copy_leaf(dp, dpos, p, pos);
				copy_p2dsym(dp, dpos, p, pos, maxdlen);
			}
			else
			{
				leaf_len = numeric_leaf(&value, &fieldtype->member_v1.offset);
				copylen = leaf_len + p[pos + leaf_len] + 1; // value,name
			}
			break;

		case LF_MEMBER_V2:
			leaf_len = numeric_leaf(&value, &fieldtype->member_v1.offset);
			copylen = sizeof(dfieldtype->member_v2) - sizeof(dfieldtype->member_v2.offset);
			copylen += leaf_len + p[pos + copylen + leaf_len] + 1; // value,name
			break;

		case LF_MEMBER_V3:
			leaf_len = numeric_leaf(&value, &fieldtype->member_v3.offset);
			copylen = sizeof(dfieldtype->member_v3) - sizeof(dfieldtype->member_v3.offset);
			copylen += leaf_len + strlen((const char*) p + pos + copylen + leaf_len) + 1; // value,name
			break;

		case LF_BCLASS_V1:
			base_classes++;
			if (dp)
			{
				dfieldtype->bclass_v2.id = LF_BCLASS_V2;
				dfieldtype->bclass_v2.attribute = fieldtype->bclass_v1.attribute;
				dfieldtype->bclass_v2.type = translateType(fieldtype->bclass_v1.type);
			}
			pos  += sizeof(fieldtype->bclass_v1) - sizeof(fieldtype->bclass_v1.offset);
#if 1
			copylen = numeric_leaf(&value, &fieldtype->bclass_v1.offset);
			if (dp)
			{
				dpos += sizeof(dfieldtype->bclass_v2) - sizeof(fieldtype->bclass_v2.offset);
				memcpy (dp + dpos, p + pos, copylen);
				dpos += copylen;
				// dp[dpos++] = 0;
			}
			pos += copylen;
			copylen = 0;
#else
			dfieldtype->member_v2.id = LF_MEMBER_V2;
			dfieldtype->member_v2.attribute = 0;
			dfieldtype->member_v2.type = fieldtype->bclass_v1.type;
			dfieldtype->member_v2.offset = fieldtype->bclass_v1.offset;
			//memcpy (&dfieldtype->member_v2 + 1, "\0", 1);
			//dpos += sizeof(dfieldtype->member_v2) + 1;
			memcpy (&dfieldtype->member_v2 + 1, "\004base", 5);
			dpos += sizeof(dfieldtype->member_v2) + 5;

			pos += numeric_leaf(&value, &fieldtype->bclass_v1.offset);
#endif
			break;

		case LF_BCLASS_V2:
			base_classes++;
			leaf_len = numeric_leaf(&value, &fieldtype->bclass_v2.offset);
			copylen = sizeof(dfieldtype->bclass_v2) - 2 + leaf_len;
			break;

		case LF_METHOD_V1:
			if (dp)
			{
				dfieldtype->method_v2.id = v3 ? LF_METHOD_V3 : LF_METHOD_V2;
				dfieldtype->method_v2.count = fieldtype->method_v1.count;
				dfieldtype->method_v2.mlist = fieldtype->method_v1.mlist;
				dpos += sizeof(dfieldtype->method_v2) - sizeof(dfieldtype->method_v2.p_name);
			}
			pos  += sizeof(dfieldtype->method_v1) - sizeof(dfieldtype->method_v1.p_name);
			if (v3 && dp)
				copy_p2dsym(dp, dpos, p, pos, maxdlen);
			else
				copylen = fieldtype->method_v1.p_name.namelen + 1;

			if(cmd == kCmdOffsetFirstVirtualMethod)
				if(const codeview_type* cvtype = getTypeData(fieldtype->method_v1.mlist))
					if(cvtype->common.id == LF_METHODLIST_V1 && cvtype->common.len > 2)
					{
						// just check the first entry
						const unsigned short *pattr = (const unsigned short*)(&cvtype->common + 1);
						int mode =(*pattr >> 2) & 7;
						if(mode == 4 || mode == 6)
							return *(const unsigned*)(&pattr[2]);
					}
			break;

		case LF_METHOD_V2:
			copylen = sizeof(dfieldtype->method_v2) - sizeof(dfieldtype->method_v2.p_name);
			copylen += fieldtype->method_v2.p_name.namelen + 1;
			break;

		case LF_METHOD_V3:
			copylen = sizeof(dfieldtype->method_v3);
			copylen += strlen((const char*) p + pos + copylen) + 1;
			break;

		case LF_STMEMBER_V1:
			if (dp)
			{
				dfieldtype->stmember_v2.id = v3 ? LF_STMEMBER_V3 : LF_STMEMBER_V2;
				dfieldtype->stmember_v2.attribute = fieldtype->stmember_v1.attribute;
				dfieldtype->stmember_v2.type = translateType(fieldtype->stmember_v1.type);
				dpos += sizeof(dfieldtype->stmember_v2) - sizeof(dfieldtype->stmember_v2.p_name);
			}
			pos  += sizeof(dfieldtype->stmember_v1) - sizeof(dfieldtype->stmember_v1.p_name);
			if (v3 && dp)
				copy_p2dsym(dp, dpos, p, pos, maxdlen);
			else
				copylen = fieldtype->stmember_v1.p_name.namelen + 1;
			break;

		case LF_STMEMBER_V2:
			copylen = sizeof(dfieldtype->stmember_v2) - sizeof(dfieldtype->stmember_v2.p_name);
			copylen += fieldtype->stmember_v2.p_name.namelen + 1;
			break;

		case LF_STMEMBER_V3:
			copylen = sizeof(dfieldtype->stmember_v3) - sizeof(dfieldtype->stmember_v3.name);
			copylen += strlen(fieldtype->stmember_v3.name) + 1;
			break;

		case LF_NESTTYPE_V1:
			if (dp)
			{
				dfieldtype->nesttype_v2.id = v3 ? LF_NESTTYPE_V3 : LF_NESTTYPE_V2;
				dfieldtype->nesttype_v2.type = translateType(fieldtype->nesttype_v1.type);
				dfieldtype->nesttype_v2._pad0 = 0;
				dpos += sizeof(dfieldtype->nesttype_v2) - sizeof(dfieldtype->nesttype_v2.p_name);
			}
			pos  += sizeof(dfieldtype->nesttype_v1) - sizeof(dfieldtype->nesttype_v1.p_name);
			if (v3 && dp)
				copy_p2dsym(dp, dpos, p, pos, maxdlen);
			else
				copylen = fieldtype->nesttype_v1.p_name.namelen + 1;
			if(test_nested_type == 0 || test_nested_type == fieldtype->nesttype_v1.type)
				nested_types++;
			if(cmd == kCmdHasClassTypeEnum && p2ccmp(fieldtype->nesttype_v1.p_name, CLASSTYPEENUM_TYPE))
				return true;
			break;

		case LF_NESTTYPE_V2:
			copylen = sizeof(dfieldtype->nesttype_v2) - sizeof(dfieldtype->nesttype_v2.p_name);
			copylen += fieldtype->nesttype_v2.p_name.namelen + 1;
			if(test_nested_type == 0 || test_nested_type == fieldtype->nesttype_v1.type)
				nested_types++;
			if(cmd == kCmdHasClassTypeEnum && p2ccmp(fieldtype->nesttype_v2.p_name, CLASSTYPEENUM_TYPE))
				return true;
			break;

		case LF_NESTTYPE_V3:
			copylen = sizeof(dfieldtype->nesttype_v3) - sizeof(dfieldtype->nesttype_v3.name);
			copylen += strlen(fieldtype->nesttype_v3.name) + 1;
			if(test_nested_type == 0 || test_nested_type == fieldtype->nesttype_v1.type)
				nested_types++;
			if(cmd == kCmdHasClassTypeEnum && strcmp(fieldtype->nesttype_v3.name, CLASSTYPEENUM_TYPE) == 0)
				return true;
			break;

		case LF_VFUNCTAB_V1:
			if (dp)
			{
				dfieldtype->vfunctab_v2.id = LF_VFUNCTAB_V2;
				dfieldtype->vfunctab_v2.type = fieldtype->vfunctab_v1.type;
				dfieldtype->vfunctab_v2._pad0 = 0;
				dpos += sizeof(dfieldtype->vfunctab_v2);
			}
			pos  += sizeof(fieldtype->vfunctab_v1);
			break;

		case LF_VFUNCTAB_V2:
			copylen = sizeof(dfieldtype->vfunctab_v2);
			break;

			// throw away friend function declarations, there is no v3 replacement and the debugger won't need them
		case LF_FRIENDFCN_V1:
			pos += sizeof(fieldtype->friendfcn_v1) + fieldtype->friendfcn_v1.p_name.namelen - 1;
			continue;
		case LF_FRIENDFCN_V2:
			copylen = sizeof(fieldtype->friendfcn_v2) + fieldtype->friendfcn_v2.p_name.namelen - 1;
			continue;

		case LF_FRIENDCLS_V1:
			if(dp)
			{
				dfieldtype->friendcls_v2.id = LF_FRIENDCLS_V2;
				dfieldtype->friendcls_v2._pad0 = 0;
				dfieldtype->friendcls_v2.type = fieldtype->friendcls_v1.type;
				dpos += sizeof(fieldtype->friendcls_v2);
			}
			pos += sizeof(fieldtype->friendcls_v1);
			break;
		case LF_FRIENDCLS_V2:
			copylen = sizeof(fieldtype->friendcls_v2);
			break; 

			// necessary to convert this info? no data associated with it, so it might not be used
		case LF_VBCLASS_V1:
		case LF_IVBCLASS_V1:
			base_classes++;
			if (dp)
			{
				dfieldtype->vbclass_v2.id = fieldtype->common.id == LF_VBCLASS_V1 ? LF_VBCLASS_V2 : LF_IVBCLASS_V2;
				dfieldtype->vbclass_v2.attribute = fieldtype->vbclass_v1.attribute;
				dfieldtype->vbclass_v2.btype = fieldtype->vbclass_v1.btype;
				dfieldtype->vbclass_v2.vbtype = fieldtype->vbclass_v1.vbtype;
				dpos += sizeof(dfieldtype->vbclass_v2) - sizeof(dfieldtype->vbclass_v2.vbpoff);
			}
			pos += sizeof(fieldtype->vbclass_v1) - sizeof(fieldtype->vbclass_v1.vbpoff);
			leaf_len = numeric_leaf(&value, &fieldtype->vbclass_v1.vbpoff);
			leaf_len += numeric_leaf(&value, (char*) &fieldtype->vbclass_v1.vbpoff + leaf_len);
			copylen = leaf_len;
			break;

		case LF_VBCLASS_V2:
		case LF_IVBCLASS_V2:
			base_classes++;
			leaf_len = numeric_leaf(&value, &fieldtype->vbclass_v2.vbpoff);
			leaf_len += numeric_leaf(&value, (char*) &fieldtype->vbclass_v2.vbpoff + leaf_len);
			copylen = sizeof(fieldtype->vbclass_v2) - sizeof(fieldtype->vbclass_v2.vbpoff) + leaf_len;
			break;

		default:
			setError("unsupported field entry");
			break;
		}

		if (dp)
		{
			memcpy (dp + dpos, p + pos, copylen);
			dpos += copylen;

			for ( ; dpos & 3; dpos++)
				dp[dpos] = 0xf4 - (dpos & 3);
		}
		pos += copylen;
		cntFields++;
	}
	switch(cmd)
	{
	case kCmdAdd:
		return dpos;
	case kCmdCount:
		return cntFields;
	case kCmdNestedTypes:
		return nested_types;
	case kCmdCountBaseClasses:
		return base_classes;
	case kCmdOffsetFirstVirtualMethod:
		return -1;
	case kCmdHasClassTypeEnum:
		return false;
	}

	return setError("_doFields: unknown command");
}

int CV2PDB::addFields(codeview_reftype* dfieldlist, const codeview_reftype* fieldlist, int maxdlen)
{
	return _doFields(kCmdAdd, dfieldlist, fieldlist, maxdlen);
}

int CV2PDB::countFields(const codeview_reftype* fieldlist)
{
	return _doFields(kCmdCount, 0, fieldlist, 0);
}

int CV2PDB::countNestedTypes(const codeview_reftype* fieldlist, int type)
{
	return _doFields(kCmdNestedTypes, 0, fieldlist, type);
}

int CV2PDB::addAggregate(codeview_type* dtype, bool clss, int n_element, int fieldlist, int property, 
                         int derived, int vshape, int structlen, const char*name)
{
	dtype->struct_v2.id = clss ? (v3 ? LF_CLASS_V3 : LF_CLASS_V2) : (v3 ? LF_STRUCTURE_V3 : LF_STRUCTURE_V2);
	dtype->struct_v2.n_element = n_element;
	dtype->struct_v2.fieldlist = fieldlist;
	dtype->struct_v2.property = property;
	dtype->struct_v2.derived = derived;
	dtype->struct_v2.vshape = vshape;
	dtype->struct_v2.structlen = structlen;
	int len = cstrcpy_v(v3, (BYTE*)(&dtype->struct_v2 + 1), name);
	len += sizeof (dtype->struct_v2);

	unsigned char* p = (unsigned char*) dtype;
	for (; len & 3; len++)
		p[len] = 0xf4 - (len & 3);
	dtype->struct_v2.len = len - 2;
	return len;
}

int CV2PDB::addClass(codeview_type* dtype, int n_element, int fieldlist, int property, 
                     int derived, int vshape, int structlen, const char*name)
{
	return addAggregate(dtype, true, n_element, fieldlist, property, derived, vshape, structlen, name);
}

int CV2PDB::addStruct(codeview_type* dtype, int n_element, int fieldlist, int property, 
                      int derived, int vshape, int structlen, const char*name)
{
	return addAggregate(dtype, false, n_element, fieldlist, property, derived, vshape, structlen, name);
}

int CV2PDB::addEnum(codeview_type* dtype, int count, int fieldlist, int property, 
                    int type, const char*name)
{
	dtype->enumeration_v2.id = (v3 ? LF_ENUM_V3 : LF_ENUM_V2);
	dtype->enumeration_v2.count = count;
	dtype->enumeration_v2.fieldlist = fieldlist;
	dtype->enumeration_v2.property = property;
	dtype->enumeration_v2.type = type;
	int len = cstrcpy_v(v3, (BYTE*)(&dtype->enumeration_v2.p_name), name);
	len += sizeof (dtype->enumeration_v2) - sizeof(dtype->enumeration_v2.p_name);

	unsigned char* p = (unsigned char*) dtype;
	for (; len & 3; len++)
		p[len] = 0xf4 - (len & 3);
	dtype->enumeration_v2.len = len - 2;
	return len;
}

int CV2PDB::addPointerType(codeview_type* dtype, int type, int attr)
{
	dtype->pointer_v2.id = LF_POINTER_V2;
	dtype->pointer_v2.len = 10;
	dtype->pointer_v2.datatype = translateType(type);
	dtype->pointer_v2.attribute = attr;
	return dtype->common.len + 2; // no alignment data needed, because always 12 bytes
}
int CV2PDB::addPointerType(unsigned char* dtype, int type, int attr)
{
	return addPointerType((codeview_type*) dtype, type, attr);
}

int CV2PDB::addFieldMember(codeview_fieldtype* dfieldtype, int attr, int offset, int type, const char* name)
{
	dfieldtype->member_v2.id = v3 ? LF_MEMBER_V3 : LF_MEMBER_V2;
	dfieldtype->member_v2.attribute = attr;
	dfieldtype->member_v2.offset = offset;
	dfieldtype->member_v2.type = translateType(type);
	int len = cstrcpy_v(v3, (BYTE*)(&dfieldtype->member_v2 + 1), name);
	len += sizeof (dfieldtype->member_v2);

	unsigned char* p = (unsigned char*) dfieldtype;
	for (; len & 3; len++)
		p[len] = 0xf4 - (len & 3);
	return len;
}

int CV2PDB::addFieldStaticMember(codeview_fieldtype* dfieldtype, int attr, int type, const char* name)
{
	dfieldtype->stmember_v2.id = v3 ? LF_STMEMBER_V3 : LF_STMEMBER_V2;
	dfieldtype->stmember_v2.attribute = attr;
	dfieldtype->stmember_v2.type = translateType(type);
	int len = cstrcpy_v(v3, (BYTE*)(&dfieldtype->stmember_v2.p_name), name);
	len += sizeof (dfieldtype->stmember_v2) - sizeof (dfieldtype->stmember_v2.p_name);

	unsigned char* p = (unsigned char*) dfieldtype;
	for (; len & 3; len++)
		p[len] = 0xf4 - (len & 3);
	return len;
}

int CV2PDB::addFieldNestedType(codeview_fieldtype* dfieldtype, int type, const char* name)
{
	dfieldtype->nesttype_v2.id = v3 ? LF_NESTTYPE_V3 : LF_NESTTYPE_V2;
	dfieldtype->nesttype_v2._pad0 = 0;
	dfieldtype->nesttype_v2.type = type;
	int len = cstrcpy_v(v3, (BYTE*)(&dfieldtype->nesttype_v2.p_name), name);
	len += sizeof (dfieldtype->nesttype_v2) - sizeof(dfieldtype->nesttype_v2.p_name);

	unsigned char* p = (unsigned char*) dfieldtype;
	for (; len & 3; len++)
		p[len] = 0xf4 - (len & 3);
	return len;
}

int CV2PDB::addFieldEnumerate(codeview_fieldtype* dfieldtype, const char* name, int val)
{
	dfieldtype->enumerate_v1.id = v3 ? LF_ENUMERATE_V3 : LF_ENUMERATE_V1;
	dfieldtype->enumerate_v1.attribute = 0;
	//assert(val < LF_NUMERIC);
	dfieldtype->enumerate_v1.value = val;
	int len = cstrcpy_v(v3, (BYTE*)(&dfieldtype->enumerate_v1 + 1), name);
	len += sizeof (dfieldtype->enumerate_v1);

	unsigned char* p = (unsigned char*) dfieldtype;
	for (; len & 3; len++)
		p[len] = 0xf4 - (len & 3);
	return len;
}

void CV2PDB::checkUserTypeAlloc(int size, int add)
{
	if (cbUserTypes + size >= allocUserTypes)
	{
		allocUserTypes += add;
		userTypes = (BYTE*) realloc(userTypes, allocUserTypes);
	}
}

void CV2PDB::writeUserTypeLen(codeview_type* type, int len)
{
	unsigned char* p = (unsigned char*) type;
	for (; len & 3; len++)
		p[len] = 0xf4 - (len & 3);

	type->common.len = len - 2;
	cbUserTypes += len;
}

void CV2PDB::checkGlobalTypeAlloc(int size, int add)
{
	if (cbGlobalTypes + size > allocGlobalTypes)
	{
		allocGlobalTypes += size + add;
		globalTypes = (unsigned char*) realloc(globalTypes, allocGlobalTypes);
	}
}

const codeview_type* CV2PDB::getTypeData(int type)
{
	if (!globalTypeHeader)
		return 0;
	if (type < 0x1000 || type >= (int) (0x1000 + globalTypeHeader->cTypes + nextUserType))
		return 0;
	if (type >= (int) (0x1000 + globalTypeHeader->cTypes))
		return getUserTypeData(type);

	DWORD* offset = (DWORD*)(globalTypeHeader + 1);
	BYTE* typeData = (BYTE*)(offset + globalTypeHeader->cTypes);
	
	return (codeview_type*)(typeData + offset[type - 0x1000]);
}

const codeview_type* CV2PDB::getUserTypeData(int type)
{
	type -= 0x1000 + globalTypeHeader->cTypes;
	if (type < 0 || type >= nextUserType - 0x1000)
		return 0;

	int pos = 0;
	while(type > 0 && pos < cbUserTypes)
	{
		const codeview_type* ptype = (codeview_type*)(userTypes + pos);
		int len = ptype->common.len + 2;
		pos += len;
		type--;
	}
	return (codeview_type*)(userTypes + pos);
}

const codeview_type* CV2PDB::getConvertedTypeData(int type)
{
	type -= 0x1000;
	if (type < 0 || type >= nextUserType - 0x1000)
		return 0;

	int pos = 4;
	while(type > 0 && pos < cbGlobalTypes)
	{
		const codeview_type* ptype = (codeview_type*)(globalTypes + pos);
		int len = ptype->common.len + 2;
		pos += len;
		type--;
	}
	return (codeview_type*)(globalTypes + pos);
}

const codeview_type* CV2PDB::findCompleteClassType(const codeview_type* cvtype, int* ptype)
{
	bool cstr;
	const BYTE* pname = getStructName(cvtype, cstr);
	if(!pname)
		return 0;

	if(globalTypeHeader)
	{
		DWORD* offset = (DWORD*)(globalTypeHeader + 1);
		BYTE* typeData = (BYTE*)(offset + globalTypeHeader->cTypes);
		for (unsigned int t = 0; t < globalTypeHeader->cTypes; t++)
		{
			const codeview_type* type = (const codeview_type*)(typeData + offset[t]);
			if (isCompleteStruct(type, pname, cstr))
			{
				if(ptype)
					*ptype = t;
				return type;
			}
		}
	}
	if(userTypes)
	{
		int t = globalTypeHeader->cTypes;
		for(int pos = 0; pos < cbUserTypes; t++)
		{
			const codeview_type* type = (codeview_type*)(userTypes + pos);
			if (isCompleteStruct(type, pname, cstr))
			{
				if(ptype)
					*ptype = t;
				return type;
			}
			pos += type->common.len + 2;
		}
	}
	return cvtype;
}

int CV2PDB::findMemberFunctionType(codeview_symbol* lastGProcSym, int thisPtrType)
{
	const codeview_type* proctype = getTypeData(lastGProcSym->proc_v2.proctype);
	if (!proctype || proctype->common.id != LF_PROCEDURE_V1)
		return lastGProcSym->proc_v2.proctype;

	const codeview_type* thisPtrData = getTypeData(thisPtrType);
	if (!thisPtrData || thisPtrData->common.id != LF_POINTER_V1)
		return lastGProcSym->proc_v2.proctype;

	int thistype = thisPtrData->pointer_v1.datatype;

	// search method with same arguments and return type
	DWORD* offset = (DWORD*)(globalTypeHeader + 1);
	BYTE* typeData = (BYTE*)(offset + globalTypeHeader->cTypes);
	for (unsigned int t = 0; t < globalTypeHeader->cTypes; t++)
	{
		// remember: mfunction_v1.class_type falsely is pointer, not class type
		const codeview_type* type = (const codeview_type*)(typeData + offset[t]);
		if (type->common.id == LF_MFUNCTION_V1 && type->mfunction_v1.this_type == thisPtrType)
		{
			if (type->mfunction_v1.arglist == proctype->procedure_v1.arglist &&
				type->mfunction_v1.call == proctype->procedure_v1.call &&
				type->mfunction_v1.rvtype == proctype->procedure_v1.rvtype)
			{
				return t + 0x1000;
			}
		}
	}
	return lastGProcSym->proc_v2.proctype;
}

int CV2PDB::fixProperty(int type, int prop, int fieldType)
{
	const codeview_reftype* cv_fieldtype = (const codeview_reftype*) getTypeData(fieldType);
	if(cv_fieldtype && countNestedTypes(cv_fieldtype, 0) > 0)
		prop |= kPropHasNested;

	// search types for field list with nested type
	DWORD* offset = (DWORD*)(globalTypeHeader + 1);
	BYTE* typeData = (BYTE*)(offset + globalTypeHeader->cTypes);
	for (unsigned int t = 0; t < globalTypeHeader->cTypes; t++)
	{
		const codeview_reftype* cvtype = (const codeview_reftype*)(typeData + offset[t]);
		if (cvtype->common.id == LF_FIELDLIST_V1 || cvtype->common.id == LF_FIELDLIST_V2)
		{
			if (countNestedTypes(cvtype, type) > 0)
			{
				prop |= kPropIsNested;
				break;
			}
		}
	}
	return prop;
}

int CV2PDB::sizeofClassType(const codeview_type* cvtype)
{
	if (getStructProperty(cvtype) & kPropIncomplete)
		cvtype = findCompleteClassType(cvtype);

	int value;
	int leaf_len = numeric_leaf(&value, &cvtype->struct_v1.structlen);
	return value;
}

int CV2PDB::sizeofBasicType(int type)
{
	int size = type & 7;
	int typ  = (type & 0xf0) >> 4;
	int mode = (type & 0x700) >> 8;
	
	switch (mode)
	{
	case 1:
	case 2:
	case 3:
	case 4:
	case 5: // pointer variations
		return 4;
	case 6: // 64-bit pointer
		return 8;
	case 7: // reserved
		return 4;
	case 0: // not pointer
		switch (typ)
		{
		case 0: // special, cannot determine
			return 4;
		case 1:
		case 2: // integral types
			switch (size)
			{
			case 0: return 1;
			case 1: return 2;
			case 2: return 4;
			case 3: return 8;
				// other reserved
			}
			return 4;
		case 3: // boolean
			return 1;
		case 4:
		case 5: // real and complex
			switch (size)
			{
			case 0: return 4;
			case 1: return 8;
			case 2: return 10;
			case 3: return 16;
			case 4: return 6;
				// other reserved
			}
			return 4;
		case 6: // special2 (bit or pascal char)
			return 1;
		case 7: // real int
			switch (size)
			{
			case 0: return 1; // char
			case 1: return 4; // wide char
			case 2: return 2;
			case 3: return 2;
			case 4: return 4;
			case 5: return 4;
			case 6: return 8;
			case 7: return 8;
			}
		}
	}
	return 4;
}

int CV2PDB::sizeofType(int type)
{
	if (type < 0x1000)
		return sizeofBasicType(type);

	const codeview_type* cvtype = getTypeData(type);
	if (!cvtype)
		return 4;

	if (cvtype->common.id == LF_CLASS_V1 || cvtype->common.id == LF_STRUCTURE_V1)
		return sizeofClassType(cvtype);

	if (cvtype->common.id == LF_OEM_V1 || cvtype->common.id == LF_OEM_V2)
		if (((codeview_oem_type*) (&cvtype->common + 1))->common.oemid == 0x42)
			return 8; // all D oem types

	// everything else must be pointer or function pointer
	return 4;
}

// to be used when writing new type only to avoid double translation
int CV2PDB::translateType(int type)
{
	if (type < 0x1000)
	{
		for(int i = 0; i < cntTypedefs; i++)
			if(type == typedefs[i])
				return translatedTypedefs[i];
		return type;
	}

	const codeview_type* cvtype = getTypeData(type);
	if (!cvtype)
		return type;

	if (cvtype->common.id != LF_OEM_V1)
		return type;

	codeview_oem_type* oem = (codeview_oem_type*) (&cvtype->common + 1);
	if (oem->common.oemid == 0x42 && oem->common.id == 3)
	{
		if (oem->d_delegate.this_type == 0x403 && oem->d_delegate.func_type == 0x74)
			return translateType(0x13); // int64
	}
	if (oem->common.oemid == 0x42 && oem->common.id == 1 && Dversion == 0)
	{
		// C does not have D types, so this must be unsigned long
		if (oem->d_dyn_array.index_type == 0x12 && oem->d_dyn_array.elem_type == 0x74)
			return translateType(0x23); // unsigned int64
	}

	return type;
}

bool CV2PDB::nameOfBasicType(int type, char* name, int maxlen)
{
	int size =  type & 0xf;
	int typ  = (type & 0xf0) >> 4;
	int mode = (type & 0x700) >> 8;
	
	switch (typ)
	{
	case 0: // special, cannot determine
		if (size == 3)
			strcpy(name, "void");
		else
			return setError("nameOfBasicType: unsupported basic special type");
		break;
	case 1: // signed integral types
		switch (size)
		{
		case 0: strcpy(name, "byte"); break; // cannot distinguish char und byte
		case 1: strcpy(name, "short"); break;
		case 2: strcpy(name, "int");  break;
		case 3: strcpy(name, "long"); break;
		default:
			return setError("nameOfBasicType: unsupported basic signed integral type");
			// other reserved
		}
		break;
	case 2: // unsigned integral types
		switch (size)
		{
		case 0: strcpy(name, "ubyte"); break;
		case 1: strcpy(name, "ushort"); break;
		case 2: strcpy(name, "uint");  break;
		case 3: strcpy(name, "ulong"); break;
		default:
			return setError("nameOfBasicType: unsupported basic unsigned integral type");
		}
		break;
	case 3: // boolean
		strcpy(name, "bool");
		break;
	case 4:
		switch (size)
		{
		case 0: strcpy(name, "ifloat"); break;
		case 1: strcpy(name, "idouble"); break;
		case 2: strcpy(name, "ireal"); break;
		default:
			return setError("nameOfBasicType: unsupported basic complex type");
		}
		break;
	case 5: // real and complex
		switch (size)
		{
		case 0: strcpy(name, "float"); break;
		case 1: strcpy(name, "double"); break;
		case 2: strcpy(name, "real"); break;
		default:
			return setError("nameOfBasicType: unsupported basic real type");
		}
		break;
	case 6: // special2 (bit or pascal char)
		return setError("nameOfBasicType: unsupported basic special2 type");
		return 1;
	case 7: // real int
		switch (size)
		{
		case 0: strcpy(name, "char"); break;
		case 1: strcpy(name, "wchar"); break; //??
		case 2: strcpy(name, "short"); break;
		case 3: strcpy(name, "ushort"); break;
		case 4: strcpy(name, "int"); break;
		case 5: strcpy(name, "uint"); break;
		case 6: strcpy(name, "long"); break;
		case 7: strcpy(name, "ulong"); break;
		case 8: strcpy(name, "dchar"); break;
		default:
			return setError("nameOfBasicType: unsupported size real int type");
		}
	}
	if (mode != 0 && mode != 7)
		strcat(name, "*");
	return true;
}

bool CV2PDB::nameOfModifierType(int type, int mod, char* name, int maxlen)
{
	*name = 0;
	if(mod & 1)
		strcat(name, "const ");
	if(mod & 2)
		strcat(name, "volatile ");
	if(mod & 4)
		strcat(name, "unaligned ");
	int len = strlen(name);
	if(!nameOfType(type, name + len, maxlen - len))
		return false;
	return true;
}

bool CV2PDB::nameOfType(int type, char* name, int maxlen)
{
	if(type < 0x1000)
		return nameOfBasicType(type, name, maxlen);

	const codeview_type* ptype = getTypeData(type);
	if(!ptype)
		return setError("nameOfType: invalid type while retreiving name of type");

	int leaf_len, value, len;
	switch(ptype->common.id)
	{
	case LF_CLASS_V1:
	case LF_STRUCTURE_V1:
		leaf_len = numeric_leaf(&value, &ptype->struct_v1.structlen);
		p2ccpy(name, (const BYTE*) &ptype->struct_v1.structlen + leaf_len);
		break;
	case LF_CLASS_V2:
	case LF_STRUCTURE_V2:
		leaf_len = numeric_leaf(&value, &ptype->struct_v2.structlen);
		p2ccpy(name, (const BYTE*) &ptype->struct_v2.structlen + leaf_len);
		break;
	case LF_CLASS_V3:
	case LF_STRUCTURE_V3:
		leaf_len = numeric_leaf(&value, &ptype->struct_v3.structlen);
		strcpy(name, (const char*) &ptype->struct_v3.structlen + leaf_len);
		break;

	case LF_UNION_V1:
		leaf_len = numeric_leaf(&value, &ptype->union_v1.un_len);
		p2ccpy(name, (const BYTE*) &ptype->union_v1.un_len + leaf_len);
		break;
	case LF_UNION_V2:
		leaf_len = numeric_leaf(&value, &ptype->union_v2.un_len);
		p2ccpy(name, (const BYTE*) &ptype->union_v2.un_len + leaf_len);
		break;
	case LF_UNION_V3:
		leaf_len = numeric_leaf(&value, &ptype->union_v3.un_len);
		strcpy(name, (const char*) &ptype->union_v3.un_len + leaf_len);
		break;

	case LF_POINTER_V1:
		if(!nameOfType(ptype->pointer_v1.datatype, name, maxlen))
			return false;
		strcat(name,"*");
		break;
	case LF_POINTER_V2:
		if(!nameOfType(ptype->pointer_v2.datatype, name, maxlen))
			return false;
		strcat(name,"*");
		break;

	case LF_ARRAY_V1:
		if(!nameOfType(ptype->array_v1.elemtype, name, maxlen))
			return false;
		leaf_len = numeric_leaf(&value, &ptype->array_v1.arrlen);
		len = strlen(name);
		sprintf(name + len, "[%d]", leaf_len);
		break;
	case LF_ARRAY_V2:
		if(!nameOfType(ptype->array_v2.elemtype, name, maxlen))
			return false;
		leaf_len = numeric_leaf(&value, &ptype->array_v2.arrlen);
		len = strlen(name);
		sprintf(name + len, "[%d]", leaf_len);
		break;
	case LF_ARRAY_V3:
		if(!nameOfType(ptype->array_v3.elemtype, name, maxlen))
			return false;
		leaf_len = numeric_leaf(&value, &ptype->array_v3.arrlen);
		len = strlen(name);
		sprintf(name + len, "[%d]", leaf_len);
		break;

	case LF_ENUM_V1:
		//strcpy(name, "enum ");
		p2ccpy(name, (const BYTE*) &ptype->enumeration_v1.p_name);
		break;
	case LF_ENUM_V2:
		//strcpy(name, "enum ");
		p2ccpy(name, (const BYTE*) &ptype->enumeration_v2.p_name);
		break;
	case LF_ENUM_V3:
		//strcpy(name, "enum ");
		strcpy(name, ptype->enumeration_v3.name);
		break;

	case LF_MODIFIER_V1:
		if (!nameOfModifierType(ptype->modifier_v1.type, ptype->modifier_v1.attribute, name, maxlen))
			return false;
		break;
	case LF_MODIFIER_V2:
		if (!nameOfModifierType(ptype->modifier_v2.type, ptype->modifier_v2.attribute, name, maxlen))
			return false;
		break;

	case LF_PROCEDURE_V1:
		if(!nameOfType(ptype->procedure_v1.rvtype, name, maxlen))
			return false;
		strcat(name, "()");
		break;
	case LF_PROCEDURE_V2:
		if(!nameOfType(ptype->procedure_v2.rvtype, name, maxlen))
			return false;
		strcat(name, "()");
		break;

	case LF_MFUNCTION_V1:
		if(!nameOfType(ptype->mfunction_v1.rvtype, name, maxlen))
			return false;
		strcat(name, "()");
		break;
	case LF_MFUNCTION_V2:
		if(!nameOfType(ptype->mfunction_v2.rvtype, name, maxlen))
			return false;
		strcat(name, "()");
		break;

	case LF_OEM_V1:
		if (!nameOfOEMType((codeview_oem_type*) (&ptype->common + 1), name, maxlen))
			return false;
		break;
	default:
		return setError("nameOfType: unsupported type");
	}
	return true;
}

bool CV2PDB::nameOfDynamicArray(int indexType, int elemType, char* name, int maxlen)
{
	if (!nameOfType(elemType, name, maxlen))
		return false;

	if (Dversion >= 2 && strcmp(name, "const char") == 0)
		strcpy(name, "string");
	else if (Dversion >= 2 && strcmp(name, "const wchar") == 0)
		strcpy(name, "wstring");
	else if (Dversion >= 2 && strcmp(name, "const dchar") == 0)
		strcpy(name, "dstring");

	else if (Dversion < 2 && strcmp(name, "char") == 0)
		strcpy(name, "string");
	else if (Dversion < 2 && strcmp(name, "wchar") == 0)
		strcpy(name, "wstring");
	else if (Dversion < 2 && strcmp(name, "dchar") == 0)
		strcpy(name, "dstring");
	else
		strcat (name, "[]");
	// sprintf(name, "dyn_array<%X,%X>", indexType, elemType);
	return true;
}

bool CV2PDB::nameOfAssocArray(int indexType, int elemType, char* name, int maxlen)
{
	if(Dversion >= 2.043)
		strcpy(name, "aa2<"); // to distinguish tree from list implementation
	else
		strcpy(name, "aa<");
	int len = strlen(name);
	if (!nameOfType(elemType, name + len, maxlen - len))
		return false;
	strcat(name, "[");
	len = strlen(name);
	if (!nameOfType(indexType, name + len, maxlen - len))
		return false;
	strcat(name,"]>");

	// sprintf(name, "assoc_array<%X,%X>", indexType, elemType);
	return true;
}

bool CV2PDB::nameOfDelegate(int thisType, int funcType, char* name, int maxlen)
{
	strcpy(name, "delegate ");
	int len = strlen(name);
	if (!nameOfType(funcType, name + len, maxlen - len))
		return false;
	// sprintf(name, "delegate<%X,%X>", indexType, elemType);
	return true;
}

bool CV2PDB::nameOfOEMType(codeview_oem_type* oem, char* name, int maxlen)
{
	if (oem->common.oemid == 0x42 && oem->common.id == 1)
		return nameOfDynamicArray(oem->d_dyn_array.index_type, oem->d_dyn_array.elem_type, name, maxlen);
	if (oem->common.oemid == 0x42 && oem->common.id == 2)
		return nameOfAssocArray(oem->d_assoc_array.key_type, oem->d_assoc_array.elem_type, name, maxlen);
	if (oem->common.oemid == 0x42 && oem->common.id == 3)
		return nameOfDelegate(oem->d_delegate.this_type, oem->d_delegate.func_type, name, maxlen);

	return setError("nameOfOEMType: unknown OEM type record");
}

const char* CV2PDB::appendDynamicArray(int indexType, int elemType)
{
	indexType = translateType(indexType);
	elemType = translateType(elemType);

	codeview_reftype* rdtype;
	codeview_type* dtype;

	checkUserTypeAlloc();

	static char name[256];
	nameOfDynamicArray(indexType, elemType, name, sizeof(name));

	// nextUserType: pointer to elemType
	cbUserTypes += addPointerType(userTypes + cbUserTypes, elemType);
	int dataptrType = nextUserType++;

	int dstringType = 0;
	if(strcmp(name, "string") == 0 || strcmp(name, "wstring") == 0 || strcmp(name, "dstring") == 0)
	{
		// nextUserType + 1: field list (size, array)
		rdtype = (codeview_reftype*) (userTypes + cbUserTypes);
		rdtype->fieldlist.id = LF_FIELDLIST_V2;
		int helpfieldlistType = nextUserType++;

		rdtype->fieldlist.len = 2;
		cbUserTypes += rdtype->fieldlist.len + 2;

		char helpertype[64];
		strcat(strcpy(helpertype, name), "_viewhelper");
		dtype = (codeview_type*) (userTypes + cbUserTypes);
		cbUserTypes += addClass(dtype, 0, helpfieldlistType, 0, 0, 0, 4, helpertype);
		dstringType = nextUserType++;
		addUdtSymbol(dstringType, helpertype);
	}

	// nextUserType + 1: field list (size, array)
	rdtype = (codeview_reftype*) (userTypes + cbUserTypes);
	rdtype->fieldlist.id = LF_FIELDLIST_V2;
	int fieldlistType = nextUserType++;

	// member indexType length
	codeview_fieldtype* dfieldtype = (codeview_fieldtype*)rdtype->fieldlist.list;
	int len1 = addFieldMember(dfieldtype, 1, 0, indexType, "length");
	
	// member elemType* data[]
	dfieldtype = (codeview_fieldtype*)(rdtype->fieldlist.list + len1);
	int len2 = addFieldMember(dfieldtype, 1, 4, dataptrType, "ptr");

	int numElem = 2;
	rdtype->fieldlist.len = len1 + len2 + 2;

	if(dstringType > 0)
	{
		dfieldtype = (codeview_fieldtype*)(rdtype->fieldlist.list + rdtype->fieldlist.len - 2);
		rdtype->fieldlist.len += addFieldMember(dfieldtype, 1, 0, dstringType, "__viewhelper");
		numElem++;
	}

	cbUserTypes += rdtype->fieldlist.len + 2;
	dtype = (codeview_type*) (userTypes + cbUserTypes);
	cbUserTypes += addClass(dtype, numElem, fieldlistType, 0, 0, 0, 8, name);
	int udType = nextUserType++;

	addUdtSymbol(udType, name);
	return name;
}

const char* CV2PDB::appendAssocArray(int keyType, int elemType)
{
	// rebuilding types
	// struct aaA {
	//    aaA *left;
	//    aaA *right;
	//    hash_t hash;
	//    keyType key;
	//    elemType value;
	// };

	keyType = translateType(keyType);
	elemType = translateType(elemType);

	codeview_reftype* rdtype;
	codeview_type* dtype;
	codeview_fieldtype* dfieldtype;

	checkUserTypeAlloc();

	static char name[256];
#if 1
	char keyname[256];
	char elemname[256];
	if(!nameOfType(keyType, keyname, sizeof(keyname)))
		return false;
	if(!nameOfType(elemType, elemname, sizeof(elemname)))
		return false;

	sprintf(name, "internal@aaA<%s,%s>", keyname, elemname);

	// undefined struct aaA
	dtype = (codeview_type*) (userTypes + cbUserTypes);
	cbUserTypes += addClass(dtype, 0, 0, kPropIncomplete, 0, 0, 0, name);
	int aaAType = nextUserType++;

	// pointer to aaA
	cbUserTypes += addPointerType(userTypes + cbUserTypes, aaAType);
	int aaAPtrType = nextUserType++;

	// field list (left, right, hash, key, value)
	rdtype = (codeview_reftype*) (userTypes + cbUserTypes);
	rdtype->fieldlist.id = LF_FIELDLIST_V2;

	int len1 = 0;
	int len2 = 0;
	int off = 0;
	// member aaA* left
	if(Dversion >= 2.043)
	{
		dfieldtype = (codeview_fieldtype*)rdtype->fieldlist.list;
		len1 = addFieldMember(dfieldtype, 1, off, aaAPtrType, "next");
		off += 4;
	}
	else
	{
		dfieldtype = (codeview_fieldtype*)rdtype->fieldlist.list;
		len1 = addFieldMember(dfieldtype, 1, off, aaAPtrType, "left");
		off += 4;

		dfieldtype = (codeview_fieldtype*)(rdtype->fieldlist.list + len1);
		len2 = addFieldMember(dfieldtype, 1, off, aaAPtrType, "right");
		off += 4;
	}
	dfieldtype = (codeview_fieldtype*)(rdtype->fieldlist.list + len1 + len2);
	int len3 = addFieldMember(dfieldtype, 1, off, 0x74, "hash");
	off += 4;

	dfieldtype = (codeview_fieldtype*)(rdtype->fieldlist.list + len1 + len2 + len3);
	int len4 = addFieldMember(dfieldtype, 1, off, keyType, "key");

	int typeLen = sizeofType(keyType);
	typeLen = (typeLen + 3) & ~3; // align to 4 byte
	off += typeLen;

	dfieldtype = (codeview_fieldtype*)(rdtype->fieldlist.list + len1 + len2 + len3 + len4);
	int len5 = addFieldMember(dfieldtype, 1, off, elemType, "value");

	int elemLen = sizeofType(elemType);
	elemLen = (elemLen + 3) & ~3; // align to 4 byte
	off += elemLen;

	rdtype->fieldlist.len = len1 + len2 + len3 + len4 + len5 + 2;
	cbUserTypes += rdtype->fieldlist.len + 2;
	int fieldListType = nextUserType++;

	dtype = (codeview_type*) (userTypes + cbUserTypes);
	cbUserTypes += addClass(dtype, len2 == 0 ? 4 : 5, fieldListType, 0, 0, 0, off, name);
	addUdtSymbol(nextUserType, name);
	int completeAAAType = nextUserType++;

	// struct BB {
	//    aaA*[] b;
	//    size_t nodes;	// total number of aaA nodes
	// };
	const char* dynArray = appendDynamicArray(0x74, aaAPtrType);
	int dynArrType = nextUserType - 1;

	// field list (aaA*[] b, size_t nodes)
	rdtype = (codeview_reftype*) (userTypes + cbUserTypes);
	rdtype->fieldlist.id = LF_FIELDLIST_V2;

	// member aaA*[] b
	dfieldtype = (codeview_fieldtype*)rdtype->fieldlist.list;
	len1 = addFieldMember(dfieldtype, 1, 0, dynArrType, "b");

	dfieldtype = (codeview_fieldtype*)(rdtype->fieldlist.list + len1);
	len2 = addFieldMember(dfieldtype, 1, 8, 0x74, "nodes");

	rdtype->fieldlist.len = len1 + len2 + 2;
	cbUserTypes += rdtype->fieldlist.len + 2;
	int bbFieldListType = nextUserType++;

	sprintf(name, "internal@BB<%s,%s>", keyname, elemname);

	// struct BB
	dtype = (codeview_type*) (userTypes + cbUserTypes);
	cbUserTypes += addClass(dtype, 2, bbFieldListType, 0, 0, 0, 12, name);
	addUdtSymbol(nextUserType, name);
	int bbType = nextUserType++;

	// struct AA {
	//    BB* a;
	// };
	// pointer to BB
	cbUserTypes += addPointerType(userTypes + cbUserTypes, bbType);
	int bbPtrType = nextUserType++;
#else
	int len1, bbPtrType = elemType;
#endif

	// field list (BB* aa)
	rdtype = (codeview_reftype*) (userTypes + cbUserTypes);
	rdtype->fieldlist.id = LF_FIELDLIST_V2;

	// member aaA*[] b
	dfieldtype = (codeview_fieldtype*)rdtype->fieldlist.list;
	len1 = addFieldMember(dfieldtype, 1, 0, bbPtrType, "a");

	rdtype->fieldlist.len = len1 + 2;
	cbUserTypes += rdtype->fieldlist.len + 2;
	int aaFieldListType = nextUserType++;

	nameOfAssocArray(keyType, elemType, name, sizeof(name));
	dtype = (codeview_type*) (userTypes + cbUserTypes);
	cbUserTypes += addClass(dtype, 1, aaFieldListType, 0, 0, 0, 4, name);

	addUdtSymbol(nextUserType, name);
	nextUserType++;

	return name;
}

const char* CV2PDB::appendDelegate(int thisType, int funcType)
{
	thisType = translateType(thisType);
	funcType = translateType(funcType);

	codeview_reftype* rdtype;
	codeview_type* dtype;

	checkUserTypeAlloc();

	// nextUserType + 1: pointer to funcType
	cbUserTypes += addPointerType(userTypes + cbUserTypes, funcType);

	bool thisTypeIsVoid = (thisType == 0x403);
	if (!thisTypeIsVoid)
	{
		// nextUserType: pointer to thisType
		dtype = (codeview_type*) (userTypes + cbUserTypes);
		cbUserTypes += addPointerType(dtype, thisType);
	}

	// nextUserType + 2: field list (size, array)
	rdtype = (codeview_reftype*) (userTypes + cbUserTypes);
	rdtype->fieldlist.id = LF_FIELDLIST_V2;

	// member thisType* thisptr
	codeview_fieldtype* dfieldtype = (codeview_fieldtype*)rdtype->fieldlist.list;
	int len1 = addFieldMember(dfieldtype, 1, 0, thisTypeIsVoid ? thisType : nextUserType + 1, "thisptr");
	
	// member funcType* funcptr
	dfieldtype = (codeview_fieldtype*)(rdtype->fieldlist.list + len1);
	int len2 = addFieldMember(dfieldtype, 1, 4, nextUserType, "funcptr");

	rdtype->fieldlist.len = len1 + len2 + 2;
	cbUserTypes += rdtype->fieldlist.len + 2;

	static char name[256];
	nameOfDelegate(thisType, funcType, name, sizeof(name));

	// nextUserType + 3: struct delegate<>
	dtype = (codeview_type*) (userTypes + cbUserTypes);
	cbUserTypes += addClass(dtype, 2, nextUserType + (thisTypeIsVoid ? 1 : 2), 0, 0, 0, 8, name);

	nextUserType += thisTypeIsVoid ? 3 : 4;
	addUdtSymbol(nextUserType - 1, name);
	return name;
}

int CV2PDB::appendObjectType (int object_type, int enumType, const char* classSymbol)
{
	checkUserTypeAlloc();

	// append object type info
	codeview_reftype* rdtype;
	codeview_type* dtype;

	int viewHelperType = 0;
	bool addViewHelper = object_type == kClassTypeObject;
	if(addViewHelper)
	{
		rdtype = (codeview_reftype*) (userTypes + cbUserTypes);
		rdtype->fieldlist.id = LF_FIELDLIST_V2;
		int helpfieldlistType = nextUserType++;
		rdtype->fieldlist.len = 2;
		cbUserTypes += rdtype->fieldlist.len + 2;

		dtype = (codeview_type*) (userTypes + cbUserTypes);
		cbUserTypes += addClass(dtype, 0, helpfieldlistType, 0, 0, 0, 0, "object_viewhelper");
		viewHelperType = nextUserType++;
		addUdtSymbol(viewHelperType, "object_viewhelper");
	}

	// vtable
	rdtype = (codeview_reftype*) (userTypes + cbUserTypes);
	rdtype->common.len = 6;
	rdtype->common.id = LF_VTSHAPE_V1;
	((unsigned short*) (&rdtype->common + 1))[0] = 1;
	((unsigned short*) (&rdtype->common + 1))[1] = 0xf150;
	cbUserTypes += rdtype->common.len + 2;
	int vtableType = nextUserType++;

	// vtable*
	dtype = (codeview_type*) (userTypes + cbUserTypes);
	cbUserTypes += addPointerType(dtype, vtableType);
	int vtablePtrType = nextUserType++;

	// field list
	rdtype = (codeview_reftype*) (userTypes + cbUserTypes);
	rdtype->fieldlist.id = LF_FIELDLIST_V2;

	codeview_fieldtype* dfieldtype = (codeview_fieldtype*)rdtype->fieldlist.list;
	dfieldtype->vfunctab_v2.id = LF_VFUNCTAB_V2; // id correct?
	dfieldtype->vfunctab_v2._pad0 = 0;
	dfieldtype->vfunctab_v2.type = vtablePtrType; // vtable*
	rdtype->fieldlist.len = sizeof(dfieldtype->vfunctab_v2) + 2;
	int numElem = 1;

	if(addViewHelper)
	{
		dfieldtype = (codeview_fieldtype*)(rdtype->fieldlist.list + rdtype->fieldlist.len - 2);
		rdtype->fieldlist.len += addFieldMember(dfieldtype, 1, 0, viewHelperType, "__viewhelper");
		numElem++;
	}
	if(addClassTypeEnum)
	{
		dfieldtype = (codeview_fieldtype*)(rdtype->fieldlist.list + rdtype->fieldlist.len - 2);
		rdtype->fieldlist.len += addFieldNestedType(dfieldtype, enumType, CLASSTYPEENUM_TYPE);
		numElem++;
	}

	cbUserTypes += rdtype->common.len + 2;
	int fieldListType = nextUserType++;

	dtype = (codeview_type*) (userTypes + cbUserTypes);
	int prop = addClassTypeEnum ? kPropHasNested : 0;
	cbUserTypes += addClass(dtype, numElem, fieldListType, prop, 0, vtableType, 4, classSymbol);
	int objType = nextUserType++;

	addUdtSymbol(objType, classSymbol);
	return objType;
}

int CV2PDB::appendPointerType(int pointedType, int attr)
{
	checkUserTypeAlloc();

	cbUserTypes += addPointerType(userTypes + cbUserTypes, pointedType, attr);
	nextUserType++;

	return nextUserType - 1;
}

int CV2PDB::appendComplex(int cplxtype, int basetype, int elemsize, const char* name)
{
	basetype = translateType(basetype);

	codeview_reftype* rdtype;
	codeview_type* dtype;

	checkUserTypeAlloc();

	// nextUserType: field list (size, array)
	rdtype = (codeview_reftype*) (userTypes + cbUserTypes);
	rdtype->fieldlist.id = LF_FIELDLIST_V2;

	// member type re
	codeview_fieldtype* dfieldtype = (codeview_fieldtype*)rdtype->fieldlist.list;
	int len1 = addFieldMember(dfieldtype, 1, 0, basetype, "re");
	
	// member funcType* funcptr
	dfieldtype = (codeview_fieldtype*)(rdtype->fieldlist.list + len1);
	int len2 = addFieldMember(dfieldtype, 1, elemsize, basetype, "im");

	rdtype->fieldlist.len = len1 + len2 + 2;
	cbUserTypes += rdtype->fieldlist.len + 2;
	int fieldlistType = nextUserType++;

	// nextUserType + 3: struct delegate<>
	dtype = (codeview_type*) (userTypes + cbUserTypes);
	cbUserTypes += addClass(dtype, 2, fieldlistType, 0, 0, 0, 2*elemsize, name);

	int classType = nextUserType++;
	addUdtSymbol(classType, name);

	typedefs[cntTypedefs] = cplxtype;
	translatedTypedefs[cntTypedefs] = classType;
	cntTypedefs++;

	return classType;
}

int CV2PDB::appendEnumerator(const char* typeName, const char* enumName, int enumValue, int prop)
{
	codeview_reftype* rdtype;
	codeview_type* dtype;

	checkUserTypeAlloc();

	// nextUserType: field list (size, array)
	rdtype = (codeview_reftype*) (userTypes + cbUserTypes);
	rdtype->fieldlist.id = LF_FIELDLIST_V2;

	// member type re
	codeview_fieldtype* dfieldtype = (codeview_fieldtype*)rdtype->fieldlist.list;
	int len1 = addFieldEnumerate(dfieldtype, enumName, enumValue);
	
	rdtype->fieldlist.len = len1 + 2;
	cbUserTypes += rdtype->fieldlist.len + 2;
	int fieldlistType = nextUserType++;

	dtype = (codeview_type*) (userTypes + cbUserTypes);
	cbUserTypes += addEnum(dtype, 1, fieldlistType, prop, 0x74, typeName);
	int enumType = nextUserType++;

	addUdtSymbol(enumType, typeName);
	return enumType;
}

int CV2PDB::getBaseClass(const codeview_type* cvtype)
{
	if (getStructProperty(cvtype) & kPropIncomplete)
		cvtype = findCompleteClassType(cvtype);

	const codeview_reftype* fieldlist = (const codeview_reftype*) getConvertedTypeData(getStructFieldlist(cvtype));
	if(!fieldlist || (fieldlist->common.id != LF_FIELDLIST_V1 && fieldlist->common.id != LF_FIELDLIST_V2))
		return 0;

	codeview_fieldtype* fieldtype = (codeview_fieldtype*)(fieldlist->fieldlist.list);
	if(fieldtype->common.id == LF_BCLASS_V1)
		return fieldtype->bclass_v1.type;
	if(fieldtype->common.id == LF_BCLASS_V2)
		return fieldtype->bclass_v2.type;
	return 0;
}

int CV2PDB::countBaseClasses(const codeview_type* cvtype)
{
	if (getStructProperty(cvtype) & kPropIncomplete)
		cvtype = findCompleteClassType(cvtype);

	const codeview_reftype* fieldlist = (const codeview_reftype*) getConvertedTypeData(getStructFieldlist(cvtype));
	if(!fieldlist || (fieldlist->common.id != LF_FIELDLIST_V1 && fieldlist->common.id != LF_FIELDLIST_V2))
		return 0;

	return _doFields(kCmdCountBaseClasses, 0, fieldlist, 0);
}

bool CV2PDB::derivesFromObject(const codeview_type* cvtype)
{
	if(cmpStructName(cvtype, (const BYTE*) OBJECT_SYMBOL, true))
		return true;

	int baseType = getBaseClass(cvtype);
	const codeview_type* basetype = getTypeData(baseType);
	if(!basetype)
		return false;

	return derivesFromObject(basetype);
}

bool CV2PDB::isCppInterface(const codeview_type* cvtype)
{
	// check whether the first virtual function is at offset 0 (C++) or 4 (D)

	if (getStructProperty(cvtype) & kPropIncomplete)
		cvtype = findCompleteClassType(cvtype);

	const codeview_reftype* fieldlist = (const codeview_reftype*) getTypeData(getStructFieldlist(cvtype));
	if(!fieldlist || (fieldlist->common.id != LF_FIELDLIST_V1 && fieldlist->common.id != LF_FIELDLIST_V2))
		return false;

	codeview_fieldtype* fieldtype = (codeview_fieldtype*)(fieldlist->fieldlist.list);
	const codeview_type* basetype = 0;
	if(fieldtype->common.id == LF_BCLASS_V1)
		basetype = getTypeData(fieldtype->bclass_v1.type);
	if(fieldtype->common.id == LF_BCLASS_V2)
		basetype = getTypeData(fieldtype->bclass_v2.type);
	if(basetype)
		return isCppInterface(basetype);

	int off = _doFields(kCmdOffsetFirstVirtualMethod, 0, fieldlist, 0);
	return off == 0;
}

void CV2PDB::ensureUDT(int type, const codeview_type* cvtype)
{
	if (getStructProperty(cvtype) & kPropIncomplete)
		cvtype = findCompleteClassType(cvtype, &type);

	if(!findUdtSymbol(type + 0x1000))
	{
		char name[300];
		int value, leaf_len = numeric_leaf(&value, &cvtype->struct_v1.structlen);
		pstrcpy_v(true, (BYTE*) name, (const BYTE*)  &cvtype->struct_v1.structlen + leaf_len);

		codeview_reftype* rdtype = (codeview_reftype*) (userTypes + cbUserTypes);
		rdtype->fieldlist.id = LF_FIELDLIST_V2;
		int helpfieldlistType = nextUserType++;
		rdtype->fieldlist.len = 2;
		cbUserTypes += rdtype->fieldlist.len + 2;

		codeview_type*dtype = (codeview_type*) (userTypes + cbUserTypes);
		cbUserTypes += addClass(dtype, 0, helpfieldlistType, 0, 0, 0, 4, name);
		int viewHelperType = nextUserType++;
		// addUdtSymbol(viewHelperType, "object_viewhelper");
		addUdtSymbol(viewHelperType, name);
	}
}

int CV2PDB::appendTypedef(int type, const char* name)
{
	checkUserTypeAlloc();

	int basetype = type;
	if(type == 0x78)
		basetype = 0x75; // dchar type not understood by debugger, use uint instead

	codeview_reftype* rdtype = (codeview_reftype*) (userTypes + cbUserTypes);
	rdtype->fieldlist.id = LF_FIELDLIST_V2;
	rdtype->fieldlist.len = 2;
	cbUserTypes += rdtype->fieldlist.len + 2;
	int fieldlistType = nextUserType++;

	codeview_type* dtype = (codeview_type*) (userTypes + cbUserTypes);
	dtype->enumeration_v2.id = (v3 ? LF_ENUM_V3 : LF_ENUM_V2);
	dtype->enumeration_v2.type = basetype;
	dtype->enumeration_v2.fieldlist = fieldlistType;
	dtype->enumeration_v2.count = 0;
	dtype->enumeration_v2.property = 0x200;
	int len = cstrcpy_v (v3, (BYTE*) &dtype->enumeration_v2.p_name, name);
	len += sizeof(dtype->enumeration_v2) - sizeof(dtype->enumeration_v2.p_name);
	writeUserTypeLen(dtype, len);

	typedefs[cntTypedefs] = type;
	translatedTypedefs[cntTypedefs] = nextUserType;
	cntTypedefs++;

	nextUserType++;
	return nextUserType - 1;
}

void CV2PDB::appendTypedefs()
{
	if(Dversion == 0)
		return;

	appendTypedef(0x10, "byte");
	appendTypedef(0x20, "ubyte");
	appendTypedef(0x21, "ushort");
	appendTypedef(0x75, "uint");
	appendTypedef(0x13, "dlong"); // instead of "long"
	appendTypedef(0x23, "ulong");
	appendTypedef(0x42, "real");
	// no imaginary types
	appendTypedef(0x71, "wchar");
	appendTypedef(0x78, "dchar");

	appendComplex(0x50, 0x40, 4, "cfloat");
	appendComplex(0x51, 0x41, 8, "cdouble");
	appendComplex(0x52, 0x42, 10, "creal");
}

bool CV2PDB::initGlobalTypes()
{
	int object_derived_type = 0;
	for (int m = 0; m < countEntries; m++)
	{
		OMFDirEntry* entry = img.getCVEntry(m);
		if(entry->SubSection == sstGlobalTypes)
		{
			globalTypeHeader = img.CVP<OMFGlobalTypes>(entry->lfo);
			DWORD* offset = img.CVP<DWORD>(entry->lfo + sizeof(OMFGlobalTypes));
			BYTE* typeData = img.CVP<BYTE>(entry->lfo + sizeof(OMFGlobalTypes) + 4*globalTypeHeader->cTypes);

			if (globalTypes)
				return setError("only one global type entry expected");

			pointerTypes = new int[globalTypeHeader->cTypes];
			memset(pointerTypes, 0, globalTypeHeader->cTypes * sizeof(*pointerTypes));

			globalTypes = (unsigned char*) malloc(entry->cb + 4);
			allocGlobalTypes = entry->cb + 4;
			if (!globalTypes)
				return setError("Out of memory");
			*(DWORD*) globalTypes = 4;
			cbGlobalTypes = 4;

			nextUserType = globalTypeHeader->cTypes + 0x1000;

			appendTypedefs();
			if(Dversion > 0)
			{
				if(addClassTypeEnum)
				{
					classEnumType    = appendEnumerator("__ClassType",    CLASSTYPEENUM_NAME, kClassTypeObject,   kPropIsNested);
					ifaceEnumType    = appendEnumerator("__IfaceType",    CLASSTYPEENUM_NAME, kClassTypeIface,    kPropIsNested);
					cppIfaceEnumType = appendEnumerator("__CppIfaceType", CLASSTYPEENUM_NAME, kClassTypeCppIface, kPropIsNested);
					structEnumType   = appendEnumerator("__StructType",   CLASSTYPEENUM_NAME, kClassTypeStruct,   kPropIsNested);
					
					ifaceBaseType    = appendObjectType (kClassTypeIface,    ifaceEnumType, IFACE_SYMBOL);
					cppIfaceBaseType = appendObjectType (kClassTypeCppIface, cppIfaceEnumType, CPPIFACE_SYMBOL);
				}
				classBaseType = appendObjectType (kClassTypeObject, classEnumType, OBJECT_SYMBOL);
			}

			for (unsigned int t = 0; t < globalTypeHeader->cTypes && !hadError(); t++)
			{
				const codeview_type* type = (codeview_type*)(typeData + offset[t]);
				const codeview_reftype* rtype = (codeview_reftype*)(typeData + offset[t]);
				int leaf_len, value;

				int len = type->common.len + 2;
				checkGlobalTypeAlloc(len + 1000);

				unsigned int clsstype;
				codeview_type* dtype = (codeview_type*) (globalTypes + cbGlobalTypes);
				codeview_reftype* rdtype = (codeview_reftype*) (globalTypes + cbGlobalTypes);

				// for debugging, cancel special processing after the limit
				unsigned int typeLimit = 0x7fffffff; // 0x1ddd; // 
				if (t > typeLimit)
				{
					dtype->pointer_v2.id = LF_POINTER_V2;
					dtype->pointer_v2.len = 10;
					dtype->pointer_v2.datatype = 0x74;
					dtype->pointer_v2.attribute = 0x800a;
					cbGlobalTypes += 12;
					continue;
				}

				switch (type->common.id)
				{
				case LF_OEM_V1:
				{
					codeview_oem_type* oem = (codeview_oem_type*) (&type->common + 1);

					if (oem->common.oemid == 0x42 && oem->common.id == 1)
					{
						if(Dversion == 0) // in dmc, this is used for (u)int64
						{
							dtype->modifier_v2.id = LF_MODIFIER_V2;
							dtype->modifier_v2.attribute = 0;
							dtype->modifier_v2.type = 0x13;
							len = sizeof(dtype->modifier_v2);
						}
						else
						{
							const char* name = appendDynamicArray(oem->d_dyn_array.index_type, oem->d_dyn_array.elem_type);
							len = addClass(dtype, 0, 0, kPropIncomplete, 0, 0, 0, name);
						}
					}
					else if (oem->common.oemid == 0x42 && oem->common.id == 3)
					{
						const char* name = appendDelegate(oem->d_delegate.this_type, oem->d_delegate.func_type);
						len = addClass(dtype, 0, 0, kPropIncomplete, 0, 0, 0, name);
					}
					else if (oem->common.oemid == 0x42 && oem->common.id == 2)
					{
						const char* name = appendAssocArray(oem->d_assoc_array.key_type, oem->d_assoc_array.elem_type);
						len = addClass(dtype, 0, 0, kPropIncomplete, 0, 0, 0, name);
					}
					else
					{
						dtype->pointer_v2.id = LF_POINTER_V2;
						dtype->pointer_v2.len = 10;
						dtype->pointer_v2.datatype = oem->d_dyn_array.elem_type;
						dtype->pointer_v2.attribute = 0x800a;
						len = 12;
					}
					break;
				}
				case LF_ARGLIST_V1:
					rdtype->arglist_v2.id = LF_ARGLIST_V2;
					rdtype->arglist_v2.num = rtype->arglist_v1.num;
					for (int i = 0; i < rtype->arglist_v1.num; i++)
						rdtype->arglist_v2.args [i] = translateType(rtype->arglist_v1.args [i]);
					len = sizeof(rdtype->arglist_v2) + 4 * rdtype->arglist_v2.num - sizeof(rdtype->arglist_v2.args);
					break;

				case LF_PROCEDURE_V1:
					dtype->procedure_v2.id = LF_PROCEDURE_V2;
					dtype->procedure_v2.rvtype   = translateType(type->procedure_v1.rvtype);
					dtype->procedure_v2.call     = type->procedure_v1.call;
					dtype->procedure_v2.reserved = type->procedure_v1.reserved;
					dtype->procedure_v2.params   = type->procedure_v1.params;
					dtype->procedure_v2.arglist  = type->procedure_v1.arglist;
					len = sizeof(dtype->procedure_v2);
					break;

				case LF_STRUCTURE_V1:
					dtype->struct_v2.id = v3 ? LF_STRUCTURE_V3 : LF_STRUCTURE_V2;
					goto LF_CLASS_V1_struct;
				case LF_CLASS_V1:
					//dtype->struct_v2.id = v3 ? LF_STRUCTURE_V3 : LF_STRUCTURE_V2;
					dtype->struct_v2.id = v3 ? LF_CLASS_V3 : LF_CLASS_V2;
				LF_CLASS_V1_struct:
					dtype->struct_v2.fieldlist = type->struct_v1.fieldlist;
					dtype->struct_v2.n_element = type->struct_v1.n_element;
					if(type->struct_v1.fieldlist != 0)
						if(const codeview_type* td = getTypeData(type->struct_v1.fieldlist))
							if(td->common.id == LF_FIELDLIST_V1 || td->common.id == LF_FIELDLIST_V2)
								dtype->struct_v2.n_element = countFields((const codeview_reftype*)td);
					dtype->struct_v2.property = fixProperty(t + 0x1000, type->struct_v1.property, 
					                                        type->struct_v1.fieldlist) | 0x200;
#if REMOVE_LF_DERIVED
					dtype->struct_v2.derived = 0;
#else
					dtype->struct_v2.derived = type->struct_v1.derived;
#endif
					dtype->struct_v2.vshape = type->struct_v1.vshape;
					leaf_len = numeric_leaf(&value, &type->struct_v1.structlen);
					memcpy (&dtype->struct_v2.structlen, &type->struct_v1.structlen, leaf_len);
					len = pstrcpy_v(v3, (BYTE*)       &dtype->struct_v2.structlen + leaf_len,
					                    (const BYTE*)  &type->struct_v1.structlen + leaf_len);
					// alternate name can be added here?
#if 0
					if (dtype->struct_v2.id == LF_CLASS_V2)
						len += pstrcpy((BYTE*)       &dtype->struct_v2.structlen + leaf_len + len,
						               (const BYTE*)  &type->struct_v1.structlen + leaf_len);
#endif
					len += leaf_len + sizeof(dtype->struct_v2) - sizeof(type->struct_v2.structlen);

					ensureUDT(t, type);
					// remember type index of derived list for object.Object
					if (Dversion > 0 && dtype->struct_v2.derived)
						if (memcmp((char*) &type->struct_v1.structlen + leaf_len, "\x0dobject.Object", 14) == 0)
							object_derived_type = type->struct_v1.derived;
					break;

				case LF_UNION_V1:
					dtype->union_v2.id = v3 ? LF_UNION_V3 : LF_UNION_V2;
					dtype->union_v2.count = type->union_v1.count;
					dtype->union_v2.fieldlist = type->struct_v1.fieldlist;
					dtype->union_v2.property = fixProperty(t + 0x1000, type->struct_v1.property, type->struct_v1.fieldlist);
					leaf_len = numeric_leaf(&value, &type->union_v1.un_len);
					memcpy (&dtype->union_v2.un_len, &type->union_v1.un_len, leaf_len);
					len = pstrcpy_v(v3, (BYTE*)      &dtype->union_v2.un_len + leaf_len,
					                    (const BYTE*) &type->union_v1.un_len + leaf_len);
					len += leaf_len + sizeof(dtype->union_v2) - sizeof(type->union_v2.un_len);
					break;

				case LF_POINTER_V1:
					dtype->pointer_v2.id = LF_POINTER_V2;
					dtype->pointer_v2.datatype = translateType(type->pointer_v1.datatype);
					if (Dversion > 0 && type->pointer_v1.datatype >= 0x1000
					                 && (type->pointer_v1.attribute & 0xE0) == 0)
					{
						if (thisIsNotRef) // const pointer for this
							pointerTypes[t] = appendPointerType(type->pointer_v1.datatype, 
							                                    type->pointer_v1.attribute | 0x400);
						dtype->pointer_v2.attribute = type->pointer_v1.attribute | 0x20; // convert to reference
					}
					else
						dtype->pointer_v2.attribute = type->pointer_v1.attribute;
					len = 12; // ignore p_name field in type->pointer_v1/2
					break;

				case LF_ARRAY_V1:
					dtype->array_v2.id = v3 ? LF_ARRAY_V3 : LF_ARRAY_V2;
					dtype->array_v2.elemtype = translateType(type->array_v1.elemtype);
					dtype->array_v2.idxtype = translateType(type->array_v1.idxtype);
					leaf_len = numeric_leaf(&value, &type->array_v1.arrlen);
					memcpy (&dtype->array_v2.arrlen, &type->array_v1.arrlen, leaf_len);
					len = pstrcpy_v(v3, (BYTE*)      &dtype->array_v2.arrlen + leaf_len,
					                    (const BYTE*) &type->array_v1.arrlen + leaf_len);
					len += leaf_len + sizeof(dtype->array_v2) - sizeof(dtype->array_v2.arrlen);
					// followed by name
					break;

				case LF_MFUNCTION_V1:
					dtype->mfunction_v2.id = LF_MFUNCTION_V2;
					dtype->mfunction_v2.rvtype = translateType(type->mfunction_v1.rvtype);
					clsstype = type->mfunction_v1.class_type;
					dtype->mfunction_v2.class_type = translateType(clsstype);
					if (clsstype >= 0x1000 && clsstype < 0x1000 + globalTypeHeader->cTypes)
					{
						// fix class_type to point to class, not pointer to class
						codeview_type* ctype = (codeview_type*)(typeData + offset[clsstype - 0x1000]);
						if (ctype->common.id == LF_POINTER_V1)
							dtype->mfunction_v2.class_type = translateType(ctype->pointer_v1.datatype);
					}
					dtype->mfunction_v2.this_type = translateType(type->mfunction_v1.this_type);
					dtype->mfunction_v2.call = type->mfunction_v1.call;
					dtype->mfunction_v2.reserved = type->mfunction_v1.reserved;
					dtype->mfunction_v2.params = type->mfunction_v1.params;
					dtype->mfunction_v2.arglist = type->mfunction_v1.arglist;
					dtype->mfunction_v2.this_adjust = type->mfunction_v1.this_adjust;
					len = sizeof(dtype->mfunction_v2);
					break;

				case LF_ENUM_V1:
					dtype->enumeration_v2.id = v3 ? LF_ENUM_V3 : LF_ENUM_V2;
					dtype->enumeration_v2.count = type->enumeration_v1.count;
					dtype->enumeration_v2.type = translateType(type->enumeration_v1.type);
					dtype->enumeration_v2.fieldlist = type->enumeration_v1.fieldlist;
					dtype->enumeration_v2.property = fixProperty(t + 0x1000, type->enumeration_v1.property, type->enumeration_v1.fieldlist);
					len = pstrcpy_v (v3, (BYTE*) &dtype->enumeration_v2.p_name, (BYTE*) &type->enumeration_v1.p_name);
					len += sizeof(dtype->enumeration_v2) - sizeof(dtype->enumeration_v2.p_name);
					if(dtype->enumeration_v2.fieldlist && v3)
						if(!findUdtSymbol(t + 0x1000))
							addUdtSymbol(t + 0x1000, (char*) &dtype->enumeration_v2.p_name);
					break;

				case LF_FIELDLIST_V1:
				case LF_FIELDLIST_V2:
					rdtype->fieldlist.id = LF_FIELDLIST_V2;
					len = addFields(rdtype, rtype, allocGlobalTypes - cbGlobalTypes) + 4;
					break;

				case LF_DERIVED_V1:
#if REMOVE_LF_DERIVED
					rdtype->common.id = LF_NULL_V1;
					len = 4;
#else
					rdtype->derived_v2.id = LF_DERIVED_V2;
					rdtype->derived_v2.num = rtype->derived_v1.num;
					for (int i = 0; i < rtype->derived_v1.num; i++)
						if (rtype->derived_v1.drvdcls[i] < 0x1000) // + globalTypeHeader->cTypes)
							rdtype->derived_v2.drvdcls[i] = translateType(rtype->derived_v1.drvdcls[i] + 0xfff);
						else
							rdtype->derived_v2.drvdcls[i] = translateType(rtype->derived_v1.drvdcls[i]);
					len = sizeof(rdtype->derived_v2) + 4 * rdtype->derived_v2.num - sizeof(rdtype->derived_v2.drvdcls);
#endif
					break;

				case LF_VTSHAPE_V1: // no alternate version known
					len = ((short*)type)[2]; // number of nibbles following
					len = 6 + (len + 1) / 2; // cut-off extra bytes
					memcpy(dtype, type, len);
					//*((char*)dtype + 6) = 0x50;
					break;

				case LF_METHODLIST_V1:
				{
					dtype->common.id = LF_METHODLIST_V2;
					const unsigned short* pattr = (const unsigned short*)((const char*)type + 4);
					unsigned* dpattr = (unsigned*)((char*)dtype + 4);
					while ((const char*)pattr + 4 <= (const char*)type + type->common.len + 2)
					{
						// type translation?
						switch ((*pattr >> 2) & 7)
						{
						case 4:
						case 6:
							*dpattr++ = *pattr++;
						default:
							*dpattr++ = *pattr++;
							*dpattr++ = *pattr++;
							break;
						}
					}
					len = (char*) dpattr - (char*)dtype;
					break;
				}
				case LF_MODIFIER_V1:
					dtype->modifier_v2.id = LF_MODIFIER_V2;
					dtype->modifier_v2.attribute = type->modifier_v1.attribute;
					dtype->modifier_v2.type = translateType(type->modifier_v1.type);
					len = sizeof(dtype->modifier_v2);
					break;

				case LF_BITFIELD_V1:
					rdtype->bitfield_v2.id = LF_BITFIELD_V2;
					rdtype->bitfield_v2.nbits = rtype->bitfield_v1.nbits;
					rdtype->bitfield_v2.bitoff = rtype->bitfield_v1.bitoff;
					rdtype->bitfield_v2.type = translateType(rtype->bitfield_v1.type);
					len = sizeof(rdtype->bitfield_v2);
					break;

				default:
					memcpy(dtype, type, len);
					break;
				}

				for (; len & 3; len++)
					globalTypes[cbGlobalTypes + len] = 0xf4 - (len & 3);
				dtype->common.len = len - 2;
			
				cbGlobalTypes += len;
			}

#if 0
			if(Dversion > 0)
				appendObjectType (object_derived_type, 0, OBJECT_SYMBOL);
#endif
#if 1
			checkGlobalTypeAlloc(cbUserTypes);

			memcpy (globalTypes + cbGlobalTypes, userTypes, cbUserTypes);
			cbGlobalTypes += cbUserTypes;
#endif
			if(addClassTypeEnum)
				insertClassTypeEnums();
		}
	}
	return !hadError();
}

bool CV2PDB::hasClassTypeEnum(const codeview_type* fieldlist)
{
	const codeview_reftype* rfieldlist = (const codeview_reftype*) fieldlist;
	return _doFields(kCmdHasClassTypeEnum, 0, rfieldlist, 0) != 0;
}

int CV2PDB::appendClassTypeEnum(const codeview_type* fieldlist, int type, const char* name)
{
	BYTE data[200];
	int len = addFieldNestedType((codeview_fieldtype*) data, type, name);

	int fieldlen = fieldlist->common.len + 2;
	int off = (unsigned char*) fieldlist - globalTypes;
	checkGlobalTypeAlloc(len);

	int copyoff = off + fieldlen;
	memmove(globalTypes + copyoff + len, globalTypes + copyoff, cbGlobalTypes - copyoff);
	memcpy(globalTypes + copyoff, data, len);
	cbGlobalTypes += len;

	codeview_type* nfieldlist = (codeview_type*) (globalTypes + off);
	nfieldlist->common.len = fieldlen + len - 2;
	return len;
}

int CV2PDB::insertBaseClass(const codeview_type* fieldlist, int type)
{
	codeview_fieldtype cvtype;
	cvtype.bclass_v2.id = LF_BCLASS_V2;
	cvtype.bclass_v2.type = type;
	cvtype.bclass_v2.attribute = 3; // public
	cvtype.bclass_v2.offset = 0;
	int len = sizeof(cvtype.bclass_v2);
	unsigned char* p = (unsigned char*) &cvtype;
	for (; len & 3; len++)
		p[len] = 0xf4 - (len & 3);

	int fieldlen = fieldlist->common.len + 2;
	int off = (unsigned char*) fieldlist - globalTypes;
	checkGlobalTypeAlloc(len);

	int copyoff = off + 4;
	memmove(globalTypes + copyoff + len, globalTypes + copyoff, cbGlobalTypes - copyoff);
	memcpy(globalTypes + copyoff, &cvtype, len);
	cbGlobalTypes += len;

	codeview_type* nfieldlist = (codeview_type*) (globalTypes + off);
	nfieldlist->common.len = fieldlen + len - 2;
	return len;
}

bool CV2PDB::insertClassTypeEnums()
{
	int pos = 4; // skip prefix
	for (unsigned int t = 0; pos < cbGlobalTypes && t < globalTypeHeader->cTypes; t++)
	{
		codeview_type* type = (codeview_type*)(globalTypes + pos);
		int typelen = type->common.len + 2;
	
		switch(type->common.id)
		{
		case LF_STRUCTURE_V3:
		case LF_STRUCTURE_V2:
		case LF_CLASS_V3:
		case LF_CLASS_V2:
			if(const codeview_type* fieldlist = getConvertedTypeData(type->struct_v2.fieldlist))
			{
				if(!hasClassTypeEnum(fieldlist))
				{
					int enumtype = 0;
					int basetype = 0;
					const char* name;

					if(type->common.id == LF_STRUCTURE_V2 || type->common.id == LF_STRUCTURE_V3)
					{
						enumtype = structEnumType;
						basetype = structBaseType;
						name = "__StructType";
					}
					else if(derivesFromObject(type))
					{
						enumtype = classEnumType;
						basetype = classBaseType;
						name = "__ClassType";
					}
					else if(isCppInterface(type))
					{
						enumtype = cppIfaceEnumType;
						basetype = cppIfaceBaseType;
						name = "__CppIfaceType";
					}
					else
					{
						enumtype = ifaceEnumType;
						basetype = ifaceBaseType;
						name = "__IfaceType";
					}
					if(basetype && !getBaseClass(type))
					{
						type->struct_v2.n_element++;
						// appending can realloc globalTypes, changing its address!
						int flpos = (unsigned char*) fieldlist - globalTypes;
						int len = insertBaseClass(fieldlist, basetype);
						if(fieldlist < type)
							pos += len;
						type = (codeview_type*)(globalTypes + pos);
						fieldlist = (codeview_type*)(globalTypes + flpos);
					}
					if(enumtype)
					{
						type->struct_v2.n_element++;
						// appending can realloc globalTypes, changing its address!
						int len = appendClassTypeEnum(fieldlist, enumtype, name);
						if(fieldlist < type)
							pos += len;
					}
				}
			}
			break;
		}
		pos += typelen;
	}
	return true;
}

bool CV2PDB::addTypes()
{
	if (!globalTypes)
		return true;

	if (useGlobalMod)
	{
		int rc = globalMod()->AddTypes(globalTypes, cbGlobalTypes);
		if (rc <= 0)
			return setError("cannot add type info to module");
		return true;
	}

	for (int m = 0; m < countEntries; m++)
	{
		OMFDirEntry* entry = img.getCVEntry(m);
		if(entry->SubSection == sstSrcModule)
		{
			mspdb::Mod* mod = modules[entry->iMod];
			if (!mod)
				return setError("sstSrcModule for non-existing module");

			int rc = mod->AddTypes(globalTypes, cbGlobalTypes);
			if (rc <= 0)
				return setError("cannot add type info to module");
		}
	}
	return true;
}

bool CV2PDB::markSrcLineInBitmap(int segIndex, int adr)
{
	if (segIndex < 0 || segIndex >= segMap->cSeg)
		return setError("invalid segment info in line number info");

	int off = adr - segMapDesc[segIndex].offset;
	if (off < 0 || off >= (int) segMapDesc[segIndex].cbSeg)
		return setError("invalid segment offset in line number info");
	
	srcLineStart[segIndex][off] = true;
	return true;
}

bool CV2PDB::createSrcLineBitmap()
{
	if (srcLineStart)
		return true;
	if (!segMap || !segMapDesc || !segFrame2Index)
		return false;

	srcLineSections = segMap->cSeg;
	srcLineStart = new char*[srcLineSections];
	memset(srcLineStart, 0, srcLineSections * sizeof (*srcLineStart));

	for (int s = 0; s < segMap->cSeg; s++)
	{
		// cbSeg=-1 found in binary created by Metroworks CodeWarrior, so avoid new char[(size_t)-1]
		if (segMapDesc[s].cbSeg <= LONG_MAX)
		{
			srcLineStart[s] = new char[segMapDesc[s].cbSeg];
			memset(srcLineStart[s], 0, segMapDesc[s].cbSeg);
		}
	}

	for (int m = 0; m < countEntries; m++)
	{
		OMFDirEntry* entry = img.getCVEntry(m);
		if(entry->SubSection == sstSrcModule)
		{
			// mark the beginning of each line
			OMFSourceModule* sourceModule = img.CVP<OMFSourceModule>(entry->lfo);
			int* segStartEnd = img.CVP<int>(entry->lfo + 4 + 4 * sourceModule->cFile);
			short* seg = img.CVP<short>(entry->lfo + 4 + 4 * sourceModule->cFile + 8 * sourceModule->cSeg);

			for (int f = 0; f < sourceModule->cFile; f++)
			{
				int cvoff = entry->lfo + sourceModule->baseSrcFile[f];
				OMFSourceFile* sourceFile = img.CVP<OMFSourceFile> (cvoff);
				int* lnSegStartEnd = img.CVP<int>(cvoff + 4 + 4 * sourceFile->cSeg);

				for (int s = 0; s < sourceFile->cSeg; s++)
				{
					int lnoff = entry->lfo + sourceFile->baseSrcLn[s];
					OMFSourceLine* sourceLine = img.CVP<OMFSourceLine> (lnoff);
					short* lineNo = img.CVP<short> (lnoff + 4 + 4 * sourceLine->cLnOff);

					int cnt = sourceLine->cLnOff;
					int segIndex = segFrame2Index[sourceLine->Seg];
					
					 // also mark the start of the line info segment
					if (!markSrcLineInBitmap(segIndex, lnSegStartEnd[2*s]))
						return false;

					for (int ln = 0; ln < cnt; ln++)
						if (!markSrcLineInBitmap(segIndex, sourceLine->offset[ln]))
							return false;
				}
			}
		}
		if (entry->SubSection == sstModule)
		{
			// mark the beginning of each section
			OMFModule* module   = img.CVP<OMFModule>(entry->lfo);
			OMFSegDesc* segDesc = img.CVP<OMFSegDesc>(entry->lfo + sizeof(OMFModule));

			for (int s = 0; s < module->cSeg; s++)
			{
				int seg = segDesc[s].Seg;
				int segIndex = seg >= 0 && seg < segMap->cSeg ? segFrame2Index[seg] : -1;
				if (!markSrcLineInBitmap(segIndex, segDesc[s].Off))
					return false;
			}
		}
	}

	return true;
}

int CV2PDB::getNextSrcLine(int seg, unsigned int off)
{
	if (!createSrcLineBitmap())
		return -1;

	int s = segFrame2Index[seg];
	if (s < 0)
		return -1;

	off -= segMapDesc[s].offset;
	if (off < 0 || off >= segMapDesc[s].cbSeg || off > LONG_MAX)
		return 0;

	for (off++; off < segMapDesc[s].cbSeg; off++)
		if (srcLineStart[s][off])
			break;

	return off + segMapDesc[s].offset;
}

bool CV2PDB::addSrcLines()
{
	for (int m = 0; m < countEntries; m++)
	{
		OMFDirEntry* entry = img.getCVEntry(m);
		if(entry->SubSection == sstSrcModule)
		{
			mspdb::Mod* mod = useGlobalMod ? globalMod() : modules[entry->iMod];
			if (!mod)
				return setError("sstSrcModule for non-existing module");

			OMFSourceModule* sourceModule = img.CVP<OMFSourceModule>(entry->lfo);
			int* segStartEnd = img.CVP<int>(entry->lfo + 4 + 4 * sourceModule->cFile);
			short* seg = img.CVP<short>(entry->lfo + 4 + 4 * sourceModule->cFile + 8 * sourceModule->cSeg);

			for (int f = 0; f < sourceModule->cFile; f++)
			{
				int cvoff = entry->lfo + sourceModule->baseSrcFile[f];
				OMFSourceFile* sourceFile = img.CVP<OMFSourceFile> (cvoff);
				int* lnSegStartEnd = img.CVP<int>(cvoff + 4 + 4 * sourceFile->cSeg);
				BYTE* pname = (BYTE*)(lnSegStartEnd + 2 * sourceFile->cSeg);
				char* name = p2c (pname);

				for (int s = 0; s < sourceFile->cSeg; s++)
				{
					int lnoff = entry->lfo + sourceFile->baseSrcLn[s];
					OMFSourceLine* sourceLine = img.CVP<OMFSourceLine> (lnoff);
					unsigned short* lineNo = img.CVP<unsigned short> (lnoff + 4 + 4 * sourceLine->cLnOff);

					int seg = sourceLine->Seg;
					int cnt = sourceLine->cLnOff;
					if(cnt <= 0)
						continue;
					int segoff = lnSegStartEnd[2*s];
					// lnSegStartEnd[2*s + 1] only spans until the first byte of the last source line
					int segend = getNextSrcLine(seg, sourceLine->offset[cnt-1]);
					int seglength = (segend >= 0 ? segend - 1 - segoff : lnSegStartEnd[2*s + 1] - segoff);

					mspdb::LineInfoEntry* lineInfo = new mspdb::LineInfoEntry[cnt];
					for (int ln = 0; ln < cnt; ln++)
					{
						lineInfo[ln].offset = sourceLine->offset[ln] - segoff;
						lineInfo[ln].line = lineNo[ln] - lineNo[0];
					}
					int rc = mod->AddLines(name, seg, segoff, seglength, segoff, lineNo[0], 
					                       (unsigned char*) lineInfo, cnt * sizeof(*lineInfo));
					if (rc <= 0)
						return setError("cannot add line number info to module");
					delete [] lineInfo;
				}
			}
		}
	}
	return true;
}

bool CV2PDB::addPublics()
{
	for (int m = 0; m < countEntries; m++)
	{
		OMFDirEntry* entry = img.getCVEntry(m);
		if(entry->SubSection == sstGlobalPub)
		{
			mspdb::Mod* mod = 0;
			if (entry->iMod < countEntries)
				mod = useGlobalMod ? globalMod() : modules[entry->iMod];
			
			OMFSymHash* header = img.CVP<OMFSymHash>(entry->lfo);
			BYTE* symbols = img.CVP<BYTE>(entry->lfo + sizeof(OMFSymHash));
			int length;
			for (unsigned int i = 0; i < header->cbSymbol; i += length)
			{
				union codeview_symbol* sym = (union codeview_symbol*)(symbols + i);
				length = sym->common.len + 2;
				if (!sym->common.id || length < 4) 
					break;

				int rc;
				switch (sym->common.id)
				{
				case S_GDATA_V1:
				case S_LDATA_V1:
				case S_PUB_V1:
					char symname[2000];
					dsym2c((BYTE*)sym->data_v1.p_name.name, sym->data_v1.p_name.namelen, symname, sizeof(symname));
					int type = translateType(sym->data_v1.symtype);
					if (mod)
						rc = mod->AddPublic2(symname, sym->data_v1.segment, sym->data_v1.offset, type);
					else
						rc = dbi->AddPublic2(symname, sym->data_v1.segment, sym->data_v1.offset, type);
					if (rc <= 0)
						return setError("cannot add public");
					break;
				}
			}
		}
	}
	return true;
}

bool CV2PDB::initGlobalSymbols()
{
	for (int m = 0; m < countEntries; m++)
	{
		OMFDirEntry* entry = img.getCVEntry(m);
		if (entry->SubSection == sstGlobalSym)
		{
			BYTE* symbols = img.CVP<BYTE>(entry->lfo);
			OMFSymHash* header = (OMFSymHash*) symbols;
			globalSymbols = symbols + sizeof(OMFSymHash);
			cbGlobalSymbols = header->cbSymbol;
		}
		if (entry->SubSection == sstStaticSym)
		{
			BYTE* symbols = img.CVP<BYTE>(entry->lfo);
			OMFSymHash* header = (OMFSymHash*) symbols;
			staticSymbols = symbols + sizeof(OMFSymHash);
			cbStaticSymbols = header->cbSymbol;
		}
	}
	return true;
}

// returns new destSize
int CV2PDB::copySymbols(BYTE* srcSymbols, int srcSize, BYTE* destSymbols, int destSize)
{
	codeview_symbol* lastGProcSym = 0;
	int type, length, destlength;
	int leaf_len, value;
	for (int i = 0; i < srcSize; i += length)
	{
		codeview_symbol* sym = (codeview_symbol*)(srcSymbols + i);
		length = sym->common.len + 2;
		if (!sym->common.id || length < 4) 
			break;

		codeview_symbol* dsym = (codeview_symbol*)(destSymbols + destSize);
		memcpy(dsym, sym, length);
		destlength = length;

		switch (sym->common.id)
		{
		case S_UDT_V1:
			dsym->udt_v1.type = translateType(sym->udt_v1.type);
			for(int p = 0; p < dsym->udt_v1.p_name.namelen; p++)
				if(dsym->udt_v1.p_name.name[p] == '.')
					dsym->udt_v1.p_name.name[p] = '@';
			//sym->udt_v1.type = 0x101e;
			break;

		case S_LDATA_V1:
			dsym->data_v2.id = v3 ? S_LDATA_V3 : S_LDATA_V2;
			goto case_DATA_V1;
		case S_GDATA_V1:
			dsym->data_v2.id = v3 ? S_GDATA_V3 : S_GDATA_V2;
		case_DATA_V1:
			dsym->data_v2.symtype = translateType(sym->data_v1.symtype);
			dsym->data_v2.offset  = sym->data_v1.offset;
			dsym->data_v2.offset  = sym->data_v1.offset;
			dsym->data_v2.segment = sym->data_v1.segment;
			destlength = pstrcpy_v (v3, (BYTE*) &dsym->data_v2.p_name, (BYTE*) &sym->data_v1.p_name);
			destlength += (BYTE*) &dsym->data_v2.p_name - (BYTE*) dsym;
			dsym->data_v2.len = destlength - 2;
			break;

		case S_LPROC_V1:
			dsym->proc_v2.id = v3 ? S_LPROC_V3 : S_LPROC_V2;
			goto case_PROC_V1;
		case S_GPROC_V1:
			dsym->proc_v2.id = v3 ? S_GPROC_V3 : S_GPROC_V2;
		case_PROC_V1:
			dsym->proc_v2.pparent  = sym->proc_v1.pparent;
			dsym->proc_v2.pend     = sym->proc_v1.pend;
			dsym->proc_v2.next     = sym->proc_v1.next;
			dsym->proc_v2.proc_len = sym->proc_v1.proc_len;
			dsym->proc_v2.debug_start = sym->proc_v1.debug_start;
			dsym->proc_v2.debug_end   = sym->proc_v1.debug_end;
			dsym->proc_v2.offset   = sym->proc_v1.offset;
			dsym->proc_v2.segment  = sym->proc_v1.segment;
			dsym->proc_v2.proctype = translateType(sym->proc_v1.proctype);
			dsym->proc_v2.flags    = sym->proc_v1.flags;

			destlength = pstrcpy_v (v3, (BYTE*) &dsym->proc_v2.p_name, (BYTE*) &sym->proc_v1.p_name);
			destlength += (BYTE*) &dsym->proc_v2.p_name - (BYTE*) dsym;
			dsym->data_v2.len = destlength - 2;

			lastGProcSym = dsym;
			break;

		case S_BPREL_V1:
			type = dsym->stack_v1.symtype;
#if 1
			if (type == 0 && p2ccmp(dsym->stack_v1.p_name, "@sblk"))
			{
				unsigned offset = dsym->stack_v1.offset & 0xffff;
				unsigned length = dsym->stack_v1.offset >> 16;
				dsym->block_v3.id = S_BLOCK_V3;
				dsym->block_v3.parent = 0;
				dsym->block_v3.end = 0; // destSize + sizeof(dsym->block_v3) + 12;
				dsym->block_v3.length = length;
				dsym->block_v3.offset = offset + (lastGProcSym ? lastGProcSym->proc_v2.offset : 0);
				dsym->block_v3.segment = (lastGProcSym ? lastGProcSym->proc_v2.segment : 0);
				dsym->block_v3.name[0] = 0;
				destlength = sizeof(dsym->block_v3);
				dsym->data_v2.len = destlength - 2;
			}
			else if (type == 0 && p2ccmp(dsym->stack_v1.p_name, "@send"))
			{
				destlength = 4;
				dsym->common.id = S_END_V1;
				dsym->common.len = destlength - 2;
			}
			else
#endif
			if (p2ccmp(dsym->stack_v1.p_name, "this"))
			{
				if (lastGProcSym)
					lastGProcSym->proc_v2.proctype = findMemberFunctionType(lastGProcSym, type);
				if (thisIsNotRef && pointerTypes)
				{
#if 0
					// insert function info before this
					memset (&dsym->funcinfo_32, 0, sizeof (dsym->funcinfo_32));
					dsym->funcinfo_32.id = S_FUNCINFO_32;
					dsym->funcinfo_32.len = sizeof (dsym->funcinfo_32) - 2;
					dsym->funcinfo_32.sizeLocals = 4;
					dsym->funcinfo_32.info = 0x220;
					destSize += sizeof (dsym->funcinfo_32);
					codeview_symbol* dsym = (codeview_symbol*)(destSymbols + destSize);
					memcpy(dsym, sym, length);
#endif
#if 0
					// create another "this" symbol that is a pointer to the object, not a reference
					destSize += length;
					codeview_symbol* dsym = (codeview_symbol*)(destSymbols + destSize);
					memcpy(dsym, sym, length);
#endif
					if (type >= 0x1000 && pointerTypes[type - 0x1000])
						type = pointerTypes[type - 0x1000];
				}
				dsym->stack_v1.symtype = translateType(type);
			}
			else if(Dversion == 0)
			{
				// remove function scope from variable name
				int p = -1;
				for(int i = 0; i < dsym->stack_v1.p_name.namelen; i++)
					if(dsym->stack_v1.p_name.name[i] == ':')
						p = i + 1;
				if(p > 0)
				{
					for(int i = p; i < dsym->stack_v1.p_name.namelen; i++)
						dsym->stack_v1.p_name.name[i - p] = dsym->stack_v1.p_name.name[i];
					dsym->stack_v1.p_name.namelen -= p;
					destlength = sizeof(dsym->stack_v1) + dsym->stack_v1.p_name.namelen - 1;
					for (; destlength & 3; destlength++)
						destSymbols[destSize + destlength] = 0;
					dsym->stack_v1.len = destlength - 2;
				}
				dsym->stack_v1.symtype = translateType(type);
			}
			else
			{
				dsym->stack_v2.id = v3 ? S_BPREL_V3 : S_BPREL_V1;
				dsym->stack_v2.offset = sym->stack_v1.offset;
				dsym->stack_v2.symtype = translateType(type);
				destlength = pstrcpy_v (v3, (BYTE*) &dsym->stack_v2.p_name, 
				                            (BYTE*) &sym->stack_v1.p_name);
				destlength += sizeof(dsym->stack_v2) - sizeof(dsym->stack_v2.p_name);
				dsym->stack_v2.len = destlength - 2;
			}
			//sym->stack_v1.symtype = 0x1012;
			break;
		case S_ENDARG_V1:
		case S_RETURN_V1:
		case S_SSEARCH_V1:
			break;
		case S_END_V1:
			lastGProcSym = 0;
			break;
		case S_COMPILAND_V1:
			if (((dsym->compiland_v1.unknown >> 8) & 0xFF) == 0) // C?
				dsym->compiland_v1.unknown = (dsym->compiland_v1.unknown & ~0xFF00 | 0x100); // C++
			break;
		case S_PROCREF_V1:
		case S_DATAREF_V1:
		case S_LPROCREF_V1:
			if(Dversion > 0)
			{
				// dmd does not add a string, but it's not obvious to detect whether it exists or not
				if (dsym->procref_v1.len != sizeof(dsym->procref_v1) - 4)
					break;
					
				dsym->procref_v1.p_name.namelen = 0;
				memset (dsym->procref_v1.p_name.name, 0, 3);  // also 4-byte alignment assumed
				destlength += 4;
			}
			else
				// throw entry away, it's use is unknown anyway, and it causes a lot of trouble
				destlength = 0;
			break;

		case S_CONSTANT_V1:
			dsym->constant_v2.id = v3 ? S_CONSTANT_V3 : S_CONSTANT_V2;
			dsym->constant_v2.type = translateType(sym->constant_v1.type);
			leaf_len = numeric_leaf(&value, &sym->constant_v1.cvalue);
			memcpy(&dsym->constant_v2.cvalue, &sym->constant_v1.cvalue, leaf_len);
			destlength = pstrcpy_v (v3, (BYTE*) &dsym->constant_v2.cvalue + leaf_len, 
			                            (BYTE*) &sym->constant_v1.cvalue + leaf_len);
			destlength += sizeof(dsym->constant_v2) - sizeof(dsym->constant_v2.cvalue) + leaf_len;
			dsym->constant_v2.len = destlength - 2;
			break;

		case S_ALIGN_V1:
			continue; // throw away
			break;

		default:
			sym = sym;
			break;
		}

		destSize += destlength;
	}
	return destSize;
}

codeview_symbol* CV2PDB::findUdtSymbol(int type)
{
	type = translateType(type);
	for(int p = 0; p < cbGlobalSymbols; )
	{
		codeview_symbol* sym = (codeview_symbol*) (globalSymbols + p);
		if(sym->common.id == S_UDT_V1 && sym->udt_v1.type == type)
			return sym;
		p += sym->common.len + 2;
	}
	for(int p = 0; p < cbStaticSymbols; )
	{
		codeview_symbol* sym = (codeview_symbol*) (staticSymbols + p);
		if(sym->common.id == S_UDT_V1 && sym->udt_v1.type == type)
			return sym;
		p += sym->common.len + 2;
	}
	for(int p = 0; p < cbUdtSymbols; )
	{
		codeview_symbol* sym = (codeview_symbol*) (udtSymbols + p);
		if(sym->common.id == S_UDT_V1 && sym->udt_v1.type == type)
			return sym;
		p += sym->common.len + 2;
	}
	return 0;
}

bool CV2PDB::addUdtSymbol(int type, const char* name)
{
	if (cbUdtSymbols + 100 > allocUdtSymbols)
	{
		allocUdtSymbols += 1000;
		udtSymbols = (BYTE*) realloc(udtSymbols, allocUdtSymbols);
	}

	codeview_symbol* sym = (codeview_symbol*) (udtSymbols + cbUdtSymbols);
	sym->udt_v1.id = S_UDT_V1;
	sym->udt_v1.type = translateType(type);
	strcpy (sym->udt_v1.p_name.name, name);
	sym->udt_v1.p_name.namelen = strlen(sym->udt_v1.p_name.name);
	sym->udt_v1.len = sizeof(sym->udt_v1) + sym->udt_v1.p_name.namelen - 1 - 2;
	cbUdtSymbols += sym->udt_v1.len + 2;

	return true;
}

bool CV2PDB::addSymbols(mspdb::Mod* mod, BYTE* symbols, int cb, bool addGlobals)
{
	int prefix = 4; // mod == globmod ? 3 : 4;
	int words = (cb + cbGlobalSymbols + cbStaticSymbols + cbUdtSymbols + 3) / 4 + prefix;
	DWORD* data = new DWORD[2 * words + 1000];

	int databytes = copySymbols(symbols, cb, (BYTE*) (data + prefix), 0);

	bool rc = writeSymbols(mod, data, databytes, prefix, addGlobals);
	delete [] data;
	return rc;
}

bool CV2PDB::writeSymbols(mspdb::Mod* mod, DWORD* data, int databytes, int prefix, bool addGlobals)
{
	if (addGlobals && staticSymbols)
		databytes = copySymbols(staticSymbols, cbStaticSymbols, (BYTE*) (data + prefix), databytes);
	if (addGlobals && globalSymbols)
		databytes = copySymbols(globalSymbols, cbGlobalSymbols, (BYTE*) (data + prefix), databytes);
	if (addGlobals && udtSymbols)
		databytes = copySymbols(udtSymbols, cbUdtSymbols, (BYTE*) (data + prefix), databytes);
	
	data[0] = 4;
	data[1] = 0xf1;
	data[2] = databytes + 4 * (prefix - 3);
	if (prefix > 3)
		data[3] = 1;
	int rc = mod->AddSymbols((BYTE*) data, ((databytes + 3) / 4 + prefix) * 4);
	if (rc <= 0)
		return setError(mspdb::DBI::isVS10
		                ? "cannot add symbols to module, probably msobj100.dll missing"
		                : "cannot add symbols to module, probably msobj80.dll missing");
	return true;
}

bool CV2PDB::addSymbols(int iMod, BYTE* symbols, int cb, bool addGlobals)
{
	mspdb::Mod* mod = 0;
	if (iMod < countEntries)
		mod = modules[iMod];
	for (int i = 0; !mod && i < countEntries; i++)
		mod = modules[i]; // add global symbols to first module
	if (!mod)
		mod = globalMod();
	if (!mod)
		return setError("no module to set symbols");

	return addSymbols(mod, symbols, cb, addGlobals);
}

bool CV2PDB::addSymbols()
{
	int prefix = 4;
	DWORD* data = 0;
	int databytes = 0;
	if (useGlobalMod)
		data = new DWORD[2 * img.getCVSize() + 1000]; // enough for all symbols

	bool addGlobals = true;
	for (int m = 0; m < countEntries; m++)
	{
		OMFDirEntry* entry = img.getCVEntry(m);
		mspdb::Mod* mod = 0;
		BYTE* symbols = img.CVP<BYTE>(entry->lfo);

		switch(entry->SubSection)
		{
		case sstAlignSym:
			if (useGlobalMod)
				databytes = copySymbols(symbols + 4, entry->cb - 4, (BYTE*) (data + prefix), databytes);
			else if (!addSymbols (entry->iMod, symbols + 4, entry->cb - 4, addGlobals))
				return false;
			addGlobals = false;
			break;

		case sstStaticSym:
		case sstGlobalSym:
			break; // handled in initGlobalSymbols
		}
	}
	bool rc = true;
	if (useGlobalMod)
		rc = writeSymbols (globalMod(), data, databytes, prefix, true);

	delete [] data;
	return rc;
}

bool CV2PDB::writeImage(const char* opath)
{
	int len = sizeof(*rsds) + strlen((char*)(rsds + 1)) + 1;
	if (!img.replaceDebugSection(rsds, len))
		return setError(img.getLastError());

	if (!img.save(opath))
		return setError(img.getLastError());

	return true;
}
