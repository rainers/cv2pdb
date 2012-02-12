// Convert DMD CodeView debug information to PDB files
// Copyright (c) 2009-2010 by Rainer Schuetze, All Rights Reserved
//
// License for redistribution is given by the Artistic License 2.0
// see file LICENSE for further details

#ifndef __CVUTIL_H__
#define __CVUTIL_H__

#include "cv2pdb.h"
#include "symutil.h"

#define OBJECT_SYMBOL   "object@Object"
#define IFACE_SYMBOL    "DInterface"
#define CPPIFACE_SYMBOL "CppInterface"

#define P_OBJECT_SYMBOL ((const BYTE*)("\x0d" OBJECT_SYMBOL))

#define CLASSTYPEENUM_TYPE  "__ClassType"
#define CLASSTYPEENUM_NAME  "__classtype"

enum
{
	kClassTypeObject   = 1,
	kClassTypeIface    = 2,
	kClassTypeCppIface = 3,
	kClassTypeStruct   = 4
};

// class properties (also apply to struct,union and enum)
static const int kPropNone        = 0x00;
static const int kPropPacked      = 0x01;
static const int kPropHasCtorDtor = 0x02;
static const int kPropHasOverOps  = 0x04;
static const int kPropIsNested    = 0x08;
static const int kPropHasNested   = 0x10;
static const int kPropHasOverAsgn = 0x20;
static const int kPropHasCasting  = 0x40;
static const int kPropIncomplete  = 0x80;
static const int kPropScoped      = 0x100;
static const int kPropReserved2   = 0x200;

bool isStruct(const codeview_type* cvtype);
bool isClass(const codeview_type* cvtype);
int getStructProperty(const codeview_type* cvtype);
int getStructFieldlist(const codeview_type* cvtype);
bool isCompleteStruct(const codeview_type* type, const BYTE* name, bool cstr);

const BYTE* getStructName(const codeview_type* cvtype, bool &cstr);
bool cmpStructName(const codeview_type* cvtype, const BYTE* name, bool cstr);

int numeric_leaf(int* value, const void* leaf);
int write_numeric_leaf(int value, void* leaf);

#endif // __CVUTIL_H__
