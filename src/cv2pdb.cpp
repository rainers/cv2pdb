// Convert DMD CodeView debug information to PDB files
// Copyright (c) 2009 by Rainer Schuetze, All Rights Reserved
//
// License for redistribution is given by the Artistic License 2.0
// see file LICENSE for further details

#include "cv2pdb.h"
#include "PEImage.h"
#include "symutil.h"

#include <stdio.h>
#include <direct.h>

static const int kIncomplete = 0x80;

CV2PDB::CV2PDB(PEImage& image) 
: img(image), pdb(0), dbi(0), libraries(0), rsds(0), modules(0), globmod(0)
, segMap(0), segMapDesc(0), globalTypeHeader(0)
, globalTypes(0), cbGlobalTypes(0), allocGlobalTypes(0)
, userTypes(0), cbUserTypes(0), allocUserTypes(0)
, globalSymbols(0), cbGlobalSymbols(0), staticSymbols(0), cbStaticSymbols(0)
, udtSymbols(0), cbUdtSymbols(0), allocUdtSymbols(0)
, pointerTypes(0)
, Dversion(2)
{
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
	objectType = 0;
	pointerTypes = 0;

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

	//printf("PDB::QueryInterfaceVersion() = %d\n", pdb->QueryInterfaceVersion());
	//printf("PDB::QueryImplementationVersion() = %d\n", pdb->QueryImplementationVersion());
	//printf("PDB::QueryPdbImplementationVersion() = %d\n", pdb->QueryPdbImplementationVersion());

	rsds = (OMFSignatureRSDS *) new char[24 + strlen(pdbname) + 1]; // sizeof(OMFSignatureRSDS) without name
	memcpy (rsds->Signature, "RSDS", 4);
	pdb->QuerySignature2(&rsds->guid);
	rsds->unknown = pdb->QueryAge();
	strcpy(rsds->name, pdbname);

	int rc = pdb->CreateDBI("", &dbi);
	if (rc <= 0 || !dbi)
		return setError("cannot create DBI");

	rc = pdb->OpenTpi("", &tpi);
	if (rc <= 0 || !tpi)
		return setError("cannot create TPI");

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
			for (int s = 0; s < segMap->cSeg; s++)
			{
				int rc = dbi->AddSec(segMapDesc[s].frame, segMapDesc[s].flags, segMapDesc[s].offset, segMapDesc[s].cbSeg);
				if (rc <= 0)
					return setError("cannot add section");
			}
			break;
		}
	}
	return true;
}

int CV2PDB::numeric_leaf(int* value, const void* leaf)
{
	unsigned short int type = *(const unsigned short int*) leaf;
	leaf = (const unsigned short int*) leaf + 2;
	int length = 2;

	*value = 0;
	switch (type)
	{
	case LF_CHAR:
		length += 1;
		*value = *(const char*)leaf;
		break;

	case LF_SHORT:
		length += 2;
		*value = *(const short*)leaf;
		break;

	case LF_USHORT:
		length += 2;
		*value = *(const unsigned short*)leaf;
		break;

	case LF_LONG:
	case LF_ULONG:
		length += 4;
		*value = *(const int*)leaf;
		break;

	case LF_COMPLEX64:
	case LF_QUADWORD:
	case LF_UQUADWORD:
	case LF_REAL64:
		length += 8;
		break;

	case LF_COMPLEX32:
	case LF_REAL32:
		length += 4;
		break;

	case LF_REAL48:
		length += 6;
		break;

	case LF_COMPLEX80:
	case LF_REAL80:
		length += 10;
		break;

	case LF_COMPLEX128:
	case LF_REAL128:
		length += 16;
		break;

	case LF_VARSTRING:
		length += 2 + *(const unsigned short*)leaf;
		break;

	default:
		if (type < LF_NUMERIC)
			*value = type;
		else
		{
			setError("unsupported numeric leaf");
			length = 0;
		}
		break;
	}
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

int CV2PDB::addFields(codeview_reftype* dfieldlist, const codeview_reftype* fieldlist, int maxdlen)
{
	int len = fieldlist->fieldlist.len - 2;
	const unsigned char* p = fieldlist->fieldlist.list;
	unsigned char* dp = dfieldlist->fieldlist.list;
	int pos = 0, dpos = 0;
	int leaf_len, value;

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

		switch (fieldtype->generic.id)
		{
		case LF_ENUMERATE_V1:
			if (v3)
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
			dfieldtype->member_v2.id = v3 ? LF_MEMBER_V3 : LF_MEMBER_V2;
			dfieldtype->member_v2.attribute = fieldtype->member_v1.attribute;
			dfieldtype->member_v2.type = translateType(fieldtype->member_v1.type);
			pos  += sizeof(dfieldtype->member_v1.id) + sizeof(dfieldtype->member_v1.attribute) + sizeof(dfieldtype->member_v1.type);
			dpos += sizeof(dfieldtype->member_v2.id) + sizeof(dfieldtype->member_v2.attribute) + sizeof(dfieldtype->member_v2.type);
			if (v3)
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
			dfieldtype->bclass_v2.id = LF_BCLASS_V2;
			dfieldtype->bclass_v2.attribute = fieldtype->bclass_v1.attribute;
			dfieldtype->bclass_v2.type = translateType(fieldtype->bclass_v1.type);
			pos  += sizeof(fieldtype->bclass_v1) - sizeof(fieldtype->bclass_v1.offset);
#if 1
			dpos += sizeof(dfieldtype->bclass_v2) - sizeof(fieldtype->bclass_v2.offset);
			copylen = numeric_leaf(&value, &fieldtype->bclass_v1.offset);
			memcpy (dp + dpos, p + pos, copylen);
			pos += copylen;
			dpos += copylen;
			// dp[dpos++] = 0;
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
			leaf_len = numeric_leaf(&value, &fieldtype->bclass_v2.offset);
			copylen = sizeof(dfieldtype->bclass_v2) - 2 + leaf_len;
			break;

		case LF_METHOD_V1:
			dfieldtype->method_v2.id = v3 ? LF_METHOD_V3 : LF_METHOD_V2;
			dfieldtype->method_v2.count = fieldtype->method_v1.count;
			dfieldtype->method_v2.mlist = fieldtype->method_v1.mlist;
			pos  += sizeof(dfieldtype->method_v1) - sizeof(dfieldtype->method_v1.p_name);
			dpos += sizeof(dfieldtype->method_v2) - sizeof(dfieldtype->method_v2.p_name);
			if (v3)
				copy_p2dsym(dp, dpos, p, pos, maxdlen);
			else
				copylen = fieldtype->method_v1.p_name.namelen + 1;
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
			dfieldtype->stmember_v2.id = v3 ? LF_STMEMBER_V3 : LF_STMEMBER_V2;
			dfieldtype->stmember_v2.attribute = fieldtype->stmember_v1.attribute;
			dfieldtype->stmember_v2.type = translateType(fieldtype->stmember_v1.type);
			pos  += sizeof(dfieldtype->stmember_v1) - sizeof(dfieldtype->stmember_v1.p_name);
			dpos += sizeof(dfieldtype->stmember_v2) - sizeof(dfieldtype->stmember_v2.p_name);
			if (v3)
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
			dfieldtype->nesttype_v2.id = v3 ? LF_NESTTYPE_V3 : LF_NESTTYPE_V2;
			dfieldtype->nesttype_v2.type = translateType(fieldtype->nesttype_v1.type);
			dfieldtype->nesttype_v2._pad0 = 0;
			pos  += sizeof(dfieldtype->nesttype_v1) - sizeof(dfieldtype->nesttype_v1.p_name);
			dpos += sizeof(dfieldtype->nesttype_v2) - sizeof(dfieldtype->nesttype_v2.p_name);
			if (v3)
				copy_p2dsym(dp, dpos, p, pos, maxdlen);
			else
				copylen = fieldtype->nesttype_v1.p_name.namelen + 1;
			break;

		case LF_NESTTYPE_V2:
			copylen = sizeof(dfieldtype->nesttype_v2) - sizeof(dfieldtype->nesttype_v2.p_name);
			copylen += fieldtype->nesttype_v2.p_name.namelen + 1;
			break;

		case LF_NESTTYPE_V3:
			copylen = sizeof(dfieldtype->nesttype_v3) - sizeof(dfieldtype->nesttype_v3.name);
			copylen += strlen(fieldtype->nesttype_v3.name) + 1;
			break;

		case LF_VFUNCTAB_V1:
			dfieldtype->vfunctab_v2.id = LF_VFUNCTAB_V2;
			dfieldtype->vfunctab_v2.type = fieldtype->vfunctab_v1.type;
			dfieldtype->vfunctab_v2._pad0 = 0;
			pos  += sizeof(fieldtype->vfunctab_v1);
			dpos += sizeof(dfieldtype->vfunctab_v2);
			break;

		case LF_VFUNCTAB_V2:
			copylen = sizeof(dfieldtype->vfunctab_v2);
			break;

		default:
			setError("unsupported field entry");
			break;
		}

		memcpy (dp + dpos, p + pos, copylen);
		pos += copylen;
		dpos += copylen;

		for ( ; dpos & 3; dpos++)
			dp[dpos] = 0xf4 - (dpos & 3);
	}
	return dpos;
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

int CV2PDB::addPointerType(codeview_type* dtype, int type, int attr)
{
	dtype->pointer_v2.id = LF_POINTER_V2;
	dtype->pointer_v2.len = 10;
	dtype->pointer_v2.datatype = translateType(type);
	dtype->pointer_v2.attribute = attr;
	return dtype->generic.len + 2; // no alignment data needed, because always 12 bytes
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

void CV2PDB::checkUserTypeAlloc(int size, int add)
{
	if (cbUserTypes + size >= allocUserTypes)
	{
		allocUserTypes += add;
		userTypes = (BYTE*) realloc(userTypes, allocUserTypes);
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
	while(type > 0)
	{
		const codeview_type* ptype = (codeview_type*)(userTypes + pos);
		int len = ptype->generic.len + 2;
		pos += len;
		type--;
	}
	return (codeview_type*)(userTypes + pos);
}

const codeview_type* CV2PDB::findCompleteClassType(const codeview_type* cvtype)
{
	if (!globalTypeHeader)
		return 0;

	int value;
	int cvleaf_len = numeric_leaf(&value, &cvtype->struct_v1.structlen);

	DWORD* offset = (DWORD*)(globalTypeHeader + 1);
	BYTE* typeData = (BYTE*)(offset + globalTypeHeader->cTypes);
	for (unsigned int t = 0; t < globalTypeHeader->cTypes; t++)
	{
		const codeview_type* type = (const codeview_type*)(typeData + offset[t]);
		if (type->generic.id == LF_CLASS_V1 || type->generic.id == LF_STRUCTURE_V1)
		{
			if (!(type->struct_v1.property & kIncomplete))
			{
				int leaf_len = numeric_leaf(&value, &type->struct_v1.structlen);
				if (pstrcmp((const BYTE*) &cvtype->struct_v1.structlen + cvleaf_len,
					        (const BYTE*)   &type->struct_v1.structlen + leaf_len) == 0)
					return type;
			}
		}
	}
	return cvtype;
}

int CV2PDB::findMemberFunctionType(codeview_symbol* lastGProcSym, int thisPtrType)
{
	const codeview_type* proctype = getTypeData(lastGProcSym->proc_v2.proctype);
	if (!proctype || proctype->generic.id != LF_PROCEDURE_V1)
		return lastGProcSym->proc_v2.proctype;

	const codeview_type* thisPtrData = getTypeData(thisPtrType);
	if (!thisPtrData || thisPtrData->generic.id != LF_POINTER_V1)
		return lastGProcSym->proc_v2.proctype;

	int thistype = thisPtrData->pointer_v1.datatype;

	// search method with same arguments and return type
	DWORD* offset = (DWORD*)(globalTypeHeader + 1);
	BYTE* typeData = (BYTE*)(offset + globalTypeHeader->cTypes);
	for (unsigned int t = 0; t < globalTypeHeader->cTypes; t++)
	{
		// remember: mfunction_v1.class_type falsely is pointer, not class type
		const codeview_type* type = (const codeview_type*)(typeData + offset[t]);
		if (type->generic.id == LF_MFUNCTION_V1 && type->mfunction_v1.this_type == thisPtrType)
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

int CV2PDB::sizeofClassType(const codeview_type* cvtype)
{
	if (cvtype->struct_v1.property & kIncomplete)
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

	if (cvtype->generic.id == LF_CLASS_V1 || cvtype->generic.id == LF_STRUCTURE_V1)
		return sizeofClassType(cvtype);

	if (cvtype->generic.id == LF_OEM_V1 || cvtype->generic.id == LF_OEM_V2)
		if (((codeview_oem_type*) (&cvtype->generic + 1))->generic.oemid == 0x42)
			return 8; // all D oem types

	// everything else must be pointer or function pointer
	return 4;
}

// to be used when writing new type only to avoid double translation
int CV2PDB::translateType(int type)
{
	if (type < 0x1000)
		return type;
	const codeview_type* cvtype = getTypeData(type);
	if (!cvtype)
		return type;

	if (cvtype->generic.id != LF_OEM_V1)
		return type;

	codeview_oem_type* oem = (codeview_oem_type*) (&cvtype->generic + 1);
	if (oem->generic.oemid == 0x42 && oem->generic.id == 3)
	{
		if (oem->d_delegate.this_type == 0x403 && oem->d_delegate.func_type == 0x74)
			return 0x76; // long
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
	switch(ptype->generic.id)
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
		strcpy(name, "enum ");
		p2ccpy(name + 5, (const BYTE*) &ptype->enumeration_v1.p_name);
		break;
	case LF_ENUM_V2:
		strcpy(name, "enum ");
		p2ccpy(name + 5, (const BYTE*) &ptype->enumeration_v2.p_name);
		break;
	case LF_ENUM_V3:
		strcpy(name, "enum ");
		strcpy(name + 5, ptype->enumeration_v3.name);
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
		if (!nameOfOEMType((codeview_oem_type*) (&ptype->generic + 1), name, maxlen))
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
	if (oem->generic.oemid == 0x42 && oem->generic.id == 1)
		return nameOfDynamicArray(oem->d_dyn_array.index_type, oem->d_dyn_array.elem_type, name, maxlen);
	if (oem->generic.oemid == 0x42 && oem->generic.id == 2)
		return nameOfAssocArray(oem->d_assoc_array.key_type, oem->d_assoc_array.elem_type, name, maxlen);
	if (oem->generic.oemid == 0x42 && oem->generic.id == 3)
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
		cbUserTypes += addClass(dtype, 0, helpfieldlistType, 0, 0, 0, 0, helpertype);
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
	int len2 = addFieldMember(dfieldtype, 1, 4, dataptrType, "data");

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
	cbUserTypes += addClass(dtype, 0, 0, kIncomplete, 0, 0, 0, name);
	int aaAType = nextUserType++;

	// pointer to aaA
	cbUserTypes += addPointerType(userTypes + cbUserTypes, aaAType);
	int aaAPtrType = nextUserType++;

	// field list (left, right, hash, key, value)
	rdtype = (codeview_reftype*) (userTypes + cbUserTypes);
	rdtype->fieldlist.id = LF_FIELDLIST_V2;

	// member aaA* left
	dfieldtype = (codeview_fieldtype*)rdtype->fieldlist.list;
	int len1 = addFieldMember(dfieldtype, 1, 0, aaAPtrType, "left");

	dfieldtype = (codeview_fieldtype*)(rdtype->fieldlist.list + len1);
	int len2 = addFieldMember(dfieldtype, 1, 4, aaAPtrType, "right");

	dfieldtype = (codeview_fieldtype*)(rdtype->fieldlist.list + len1 + len2);
	int len3 = addFieldMember(dfieldtype, 1, 8, 0x74, "hash");

	dfieldtype = (codeview_fieldtype*)(rdtype->fieldlist.list + len1 + len2 + len3);
	int len4 = addFieldMember(dfieldtype, 1, 12, keyType, "key");

	int typeLen = sizeofType(keyType);
	typeLen = (typeLen + 3) & ~3; // align to 4 byte
	dfieldtype = (codeview_fieldtype*)(rdtype->fieldlist.list + len1 + len2 + len3 + len4);
	int len5 = addFieldMember(dfieldtype, 1, 12 + typeLen, elemType, "value");

	int elemLen = sizeofType(elemType);
	elemLen = (elemLen + 3) & ~3; // align to 4 byte

	rdtype->fieldlist.len = len1 + len2 + len3 + len4 + len5 + 2;
	cbUserTypes += rdtype->fieldlist.len + 2;
	int fieldListType = nextUserType++;

	dtype = (codeview_type*) (userTypes + cbUserTypes);
	cbUserTypes += addClass(dtype, 5, fieldListType, 0, 0, 0, 12 + typeLen + elemLen, name);
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

int CV2PDB::appendObjectType (int object_derived_type)
{
	checkUserTypeAlloc();

	// append object type info
	codeview_reftype* rdtype;
	codeview_type* dtype;

	int viewHelperType = 0;
	bool addViewHelper = true;
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
	rdtype->generic.len = 6;
	rdtype->generic.id = LF_VTSHAPE_V1;
	((unsigned short*) (&rdtype->generic + 1))[0] = 1;
	((unsigned short*) (&rdtype->generic + 1))[1] = 0xf150;
	cbUserTypes += rdtype->generic.len + 2;
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

	cbUserTypes += rdtype->generic.len + 2;
	int fieldListType = nextUserType++;

#define OBJECT_SYMBOL "object@Object"

	dtype = (codeview_type*) (userTypes + cbUserTypes);
	cbUserTypes += addClass(dtype, numElem, fieldListType, 0, object_derived_type, vtableType, 4, OBJECT_SYMBOL);
	objectType = nextUserType++;

	addUdtSymbol(objectType, OBJECT_SYMBOL);
	return objectType;
}

int CV2PDB::appendPointerType(int pointedType, int attr)
{
	checkUserTypeAlloc();

	cbUserTypes += addPointerType(userTypes + cbUserTypes, pointedType, attr);
	nextUserType++;

	return nextUserType - 1;
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

			for (unsigned int t = 0; t < globalTypeHeader->cTypes && !hadError(); t++)
			{
				const codeview_type* type = (codeview_type*)(typeData + offset[t]);
				const codeview_reftype* rtype = (codeview_reftype*)(typeData + offset[t]);
				int leaf_len, value;

				int len = type->generic.len + 2;
				if (cbGlobalTypes + len + 1000 > allocGlobalTypes)
				{
					allocGlobalTypes += len + 1000;
					globalTypes = (unsigned char*) realloc(globalTypes, allocGlobalTypes);
				}

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

				switch (type->generic.id)
				{
				case LF_OEM_V1:
				{
					codeview_oem_type* oem = (codeview_oem_type*) (&type->generic + 1);

					if (oem->generic.oemid == 0x42 && oem->generic.id == 1)
					{
						const char* name = appendDynamicArray(oem->d_dyn_array.index_type, oem->d_dyn_array.elem_type);
						len = addClass(dtype, 0, 0, kIncomplete, 0, 0, 0, name);
					}
					else if (oem->generic.oemid == 0x42 && oem->generic.id == 3)
					{
						const char* name = appendDelegate(oem->d_delegate.this_type, oem->d_delegate.func_type);
						len = addClass(dtype, 0, 0, kIncomplete, 0, 0, 0, name);
					}
					else if (oem->generic.oemid == 0x42 && oem->generic.id == 2)
					{
						const char* name = appendAssocArray(oem->d_assoc_array.key_type, oem->d_assoc_array.elem_type);
						len = addClass(dtype, 0, 0, kIncomplete, 0, 0, 0, name);
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
					dtype->struct_v2.n_element = type->struct_v1.n_element;
					dtype->struct_v2.fieldlist = type->struct_v1.fieldlist;
					dtype->struct_v2.property = type->struct_v1.property | 0x200;
					dtype->struct_v2.derived = type->struct_v1.derived;
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

					// remember type index of derived list for object.Object
					if (Dversion > 0 && type->struct_v1.derived)
						if (memcmp((char*) &type->struct_v1.structlen + leaf_len, "\x0dobject.Object", 14) == 0)
							object_derived_type = type->struct_v1.derived;
					break;

				case LF_UNION_V1:
					dtype->union_v2.id = v3 ? LF_UNION_V3 : LF_UNION_V2;
					dtype->union_v2.count = type->union_v1.count;
					dtype->union_v2.fieldlist = type->struct_v1.fieldlist;
					dtype->union_v2.property = type->struct_v1.property;
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
						if (ctype->generic.id == LF_POINTER_V1)
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
					dtype->enumeration_v2.property = type->enumeration_v1.property;
					len = pstrcpy_v (v3, (BYTE*) &dtype->enumeration_v2.p_name, (BYTE*) &type->enumeration_v1.p_name);
					len += sizeof(dtype->enumeration_v2) - sizeof(dtype->enumeration_v2.p_name);
					break;

				case LF_FIELDLIST_V1:
				case LF_FIELDLIST_V2:
					rdtype->fieldlist.id = LF_FIELDLIST_V2;
					len = addFields(rdtype, rtype, allocGlobalTypes - cbGlobalTypes) + 4;
					break;

				case LF_DERIVED_V1:
#if 1 // types wrong by DMD
					rdtype->generic.id = LF_NULL_V1;
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
					dtype->generic.id = LF_METHODLIST_V2;
					const unsigned short* pattr = (const unsigned short*)((const char*)type + 4);
					unsigned* dpattr = (unsigned*)((char*)dtype + 4);
					while ((const char*)pattr + 4 <= (const char*)type + type->generic.len + 2)
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
				dtype->generic.len = len - 2;
			
				cbGlobalTypes += len;
			}

#if 1
			if(Dversion > 0)
				appendObjectType (object_derived_type);
#endif
#if 1
			if (cbGlobalTypes + cbUserTypes > allocGlobalTypes)
			{
				allocGlobalTypes += cbUserTypes + 1000;
				globalTypes = (unsigned char*) realloc(globalTypes, allocGlobalTypes);
			}

			memcpy (globalTypes + cbGlobalTypes, userTypes, cbUserTypes);
			cbGlobalTypes += cbUserTypes;
#endif
		}
	}
	return !hadError();
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
					short* lineNo = img.CVP<short> (lnoff + 4 + 4 * sourceLine->cLnOff);

					int segoff = lnSegStartEnd[2*s];
					int seglength = lnSegStartEnd[2*s + 1] - segoff;
					int cnt = sourceLine->cLnOff;

					mspdb::LineInfoEntry* lineInfo = new mspdb::LineInfoEntry[cnt];
					for (int ln = 0; ln < cnt; ln++)
					{
						lineInfo[ln].offset = sourceLine->offset[ln] - segoff;
						lineInfo[ln].line = lineNo[ln] - lineNo[0];
					}
					int rc = mod->AddLines(name, sourceLine->Seg, segoff, seglength, segoff, lineNo[0], 
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
				length = sym->generic.len + 2;
				if (!sym->generic.id || length < 4) 
					break;

				int rc;
				switch (sym->generic.id)
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
		length = sym->generic.len + 2;
		if (!sym->generic.id || length < 4) 
			break;

		codeview_symbol* dsym = (codeview_symbol*)(destSymbols + destSize);
		memcpy(dsym, sym, length);
		destlength = length;

		switch (sym->generic.id)
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
			}
			else if(Dversion == 0)
			{
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
			}
			dsym->stack_v1.symtype = translateType(type);
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
		return setError("cannot add symbols to module");
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
	if (!initGlobalSymbols())
		return false;

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
