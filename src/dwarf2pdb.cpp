// Convert DMD CodeView/DWARF debug information to PDB files
// Copyright (c) 2009-2012 by Rainer Schuetze, All Rights Reserved
//
// License for redistribution is given by the Artistic License 2.0
// see file LICENSE for further details
//
// todo:
//  display associative array
//  64 bit:
//   - arguments passed by register
//   - real

#include "cv2pdb.h"
#include "PEImage.h"
#include "symutil.h"
#include "cvutil.h"

#include "dwarf.h"

#include <algorithm>
#include <assert.h>
#include <string>
#include <vector>

// Returns all non-empty address ranges specified by id.  The entry point (if applicable) is always the low address of the first range.
static std::vector<RangeEntry> getRanges(DIECursor cursor, const DWARF_InfoData &id)
{
	std::vector<RangeEntry> ret;
	RangeEntry range;
	if (id.pclo)
	{
		if (id.pchi == 0 || id.pchi == id.pclo)
			return ret;
		if (id.pclo < id.pcentry && id.pcentry < id.pchi)
		{
			range.pclo = id.pcentry;
			range.pchi = id.pchi;
			ret.push_back(range);
			range.pclo = id.pclo;
			range.pchi = id.pcentry;
			ret.push_back(range);
		}
		else
		{
			range.pclo = id.pclo;
			range.pchi = id.pchi;
			ret.push_back(range);
		}
	}
	else if (id.ranges != ~0)
	{
		RangeCursor rangeCursor(cursor, id.ranges);
		while (rangeCursor.readNext(range))
		{
			if (range.pclo == range.pchi)
				continue;
			if (range.pclo <= id.pcentry && id.pcentry < range.pchi)
			{
				std::uint64_t pclo = range.pclo;
				range.pclo = id.pcentry;
				ret.insert(ret.begin(), range);
				if (pclo != id.pcentry)
				{
					range.pclo = pclo;
					range.pchi = id.pcentry;
					ret.push_back(range);
				}
			}
			else
			{
				ret.push_back(range);
			}
		}
	}
	return ret;
}

void CV2PDB::checkDWARFTypeAlloc(int size, int add)
{
	if (cbDwarfTypes + size > allocDwarfTypes)
	{
		//allocDwarfTypes += size + add;
		allocDwarfTypes += allocDwarfTypes/2 + size + add;
		dwarfTypes = (BYTE*) realloc(dwarfTypes, allocDwarfTypes);
		if (dwarfTypes == nullptr)
			__debugbreak();
	}
}

enum CV_X86_REG
{
	CV_REG_NONE = 0,
	CV_REG_EAX = 17,
	CV_REG_ECX = 18,
	CV_REG_EDX = 19,
	CV_REG_EBX = 20,
	CV_REG_ESP = 21,
	CV_REG_EBP = 22,
	CV_REG_ESI = 23,
	CV_REG_EDI = 24,
	CV_REG_ES = 25,
	CV_REG_CS = 26,
	CV_REG_SS = 27,
	CV_REG_DS = 28,
	CV_REG_FS = 29,
	CV_REG_GS = 30,
	CV_REG_IP = 31,
	CV_REG_FLAGS = 32,
	CV_REG_EIP = 33,
	CV_REG_EFLAGS = 34,
	CV_REG_ST0 = 128, /* this includes ST1 to ST7 */
	CV_REG_XMM0 = 154, /* this includes XMM1 to XMM7 */
	CV_REG_XMM8 = 252, /* this includes XMM9 to XMM15 */

	// 64-bit regular registers
	CV_AMD64_RAX      =  328,
	CV_AMD64_RBX      =  329,
	CV_AMD64_RCX      =  330,
	CV_AMD64_RDX      =  331,
	CV_AMD64_RSI      =  332,
	CV_AMD64_RDI      =  333,
	CV_AMD64_RBP      =  334,
	CV_AMD64_RSP      =  335,

	// 64-bit integer registers with 8-, 16-, and 32-bit forms (B, W, and D)
	CV_AMD64_R8       =  336,
	CV_AMD64_R9       =  337,
	CV_AMD64_R10      =  338,
	CV_AMD64_R11      =  339,
	CV_AMD64_R12      =  340,
	CV_AMD64_R13      =  341,
	CV_AMD64_R14      =  342,
	CV_AMD64_R15      =  343,
};

CV_X86_REG dwarf_to_x86_reg(unsigned dwarf_reg)
{
	switch (dwarf_reg)
	{
		case  0: return CV_REG_EAX;
		case  1: return CV_REG_ECX;
		case  2: return CV_REG_EDX;
		case  3: return CV_REG_EBX;
		case  4: return CV_REG_ESP;
		case  5: return CV_REG_EBP;
		case  6: return CV_REG_ESI;
		case  7: return CV_REG_EDI;
		case  8: return CV_REG_EIP;
		case  9: return CV_REG_EFLAGS;
		case 10: return CV_REG_CS;
		case 11: return CV_REG_SS;
		case 12: return CV_REG_DS;
		case 13: return CV_REG_ES;
		case 14: return CV_REG_FS;
		case 15: return CV_REG_GS;

		case 16: case 17: case 18: case 19:
		case 20: case 21: case 22: case 23:
			return (CV_X86_REG)(CV_REG_ST0 + dwarf_reg - 16);
		case 32: case 33: case 34: case 35:
		case 36: case 37: case 38: case 39:
			return (CV_X86_REG)(CV_REG_XMM0 + dwarf_reg - 32);
		default:
			return CV_REG_NONE;
	}
}

CV_X86_REG dwarf_to_amd64_reg(unsigned dwarf_reg)
{
	switch (dwarf_reg)
	{
		case  0: return CV_AMD64_RAX;
		case  1: return CV_AMD64_RDX;
		case  2: return CV_AMD64_RCX;
		case  3: return CV_AMD64_RBX;
		case  4: return CV_AMD64_RSI;
		case  5: return CV_AMD64_RDI;
		case  6: return CV_AMD64_RBP;
		case  7: return CV_AMD64_RSP;
		case  8: return CV_AMD64_R8;
		case  9: return CV_AMD64_R9;
		case 10: return CV_AMD64_R10;
		case 11: return CV_AMD64_R11;
		case 12: return CV_AMD64_R12;
		case 13: return CV_AMD64_R13;
		case 14: return CV_AMD64_R14;
		case 15: return CV_AMD64_R15;
		case 16: return CV_REG_IP;
		case 49: return CV_REG_EFLAGS;
		case 50: return CV_REG_ES;
		case 51: return CV_REG_CS;
		case 52: return CV_REG_SS;
		case 53: return CV_REG_DS;
		case 54: return CV_REG_FS;
		case 55: return CV_REG_GS;

		case 17: case 18: case 19: case 20:
		case 21: case 22: case 23: case 24:
			return (CV_X86_REG)(CV_REG_XMM0 + dwarf_reg - 17);
		case 25: case 26: case 27: case 28:
		case 29: case 30: case 31: case 32:
			return (CV_X86_REG)(CV_REG_XMM8 + dwarf_reg - 25);
		case 33: case 34: case 35: case 36:
		case 37: case 38: case 39: case 40:
			return (CV_X86_REG)(CV_REG_ST0 + dwarf_reg - 33);
		default:
			return CV_REG_NONE;
	}
}

// Index for efficient lookups in Call Frame Information entries
class CFIIndex
{
public:
	// Build the index, which will be tied to IMG.
	CFIIndex(const PEImage& img);

	// Look for a FDE whose PC range covers the PCLO/PCHI range and return a
	// pointer to the FDE in the .debug_frame section. Return NULL if no such
	// FDE exists.
	byte *lookup(unsigned int pclo, unsigned pchi) const;

private:
	struct index_entry
	{
		// PC range for the FDE
		unsigned int pclo, pchi;
		// Pointer to the FDE in the .debug_frame section
		byte *ptr;

		// Sort entries by PCLO first and then by PCHI.
		bool operator<(const index_entry& other) const {
			if (pclo < other.pclo)
				return true;
			else if (pclo == other.pclo)
				return pchi < other.pchi;
			else
				return false;
		}

	};

	std::vector<index_entry> index;
};

// Call Frame Information entry (CIE or FDE)
class CFIEntry
{
public:
	enum Type
	{
		CIE,
		FDE
	};

	byte* ptr;
	byte* end;
	byte type;
	unsigned long CIE_pointer; //

	// CIE
	byte version;
	const char* augmentation;
	byte address_size;
	byte segment_size;
	unsigned long code_alignment_factor;
	unsigned long data_alignment_factor;
	unsigned long return_address_register;
	byte* initial_instructions;
	unsigned long initial_instructions_length;

	// FDE
	unsigned long segment;
	unsigned long initial_location;
	unsigned long address_range;
	byte* instructions;
	unsigned long instructions_length;
};

// Call Frame Information Cursor
class CFICursor
{
public:
	CFICursor(const PEImage& img)
	: beg(img.debug_frame.startByte())
	, end(img.debug_frame.endByte())
	, ptr(beg)
	{
		default_address_size = img.isX64() ? 8 : 4;
	}

	byte* beg;
	byte* end;
	byte* ptr;
	byte default_address_size;

	bool readCIE(CFIEntry& entry, byte* &p)
	{
		entry.version = *p++;
		entry.augmentation = (char*) p++;
		if(entry.augmentation[0])
		{
			// not supporting any augmentation
			entry.address_size = 4;
			entry.segment_size = 0;
			entry.code_alignment_factor = 0;
			entry.data_alignment_factor = 0;
			entry.return_address_register = 0;
		}
		else
		{
			if (entry.version >= 4)
			{
				entry.address_size = *p++;
				entry.segment_size = *p++;
			}
			else
			{
				entry.address_size = default_address_size;
				entry.segment_size = 0;
			}
			entry.code_alignment_factor = LEB128(p);
			entry.data_alignment_factor = SLEB128(p);
			entry.return_address_register = LEB128(p);
		}
		entry.initial_instructions = p;
		entry.initial_instructions_length = 0; // to be calculated outside
		return true;
	}

	bool readHeader(byte* &p, byte* &pend, unsigned long& CIE_pointer)
	{
		if (p >= end)
			return false;
		long long len = RDsize(p, 4);
		bool dwarf64 = (len == 0xffffffff);
		int ptrsize = dwarf64 ? 8 : 4;
		if(dwarf64)
			len = RDsize(p, 8);
		if(p + len > end)
			return false;

		pend = p + (unsigned long) len;
		CIE_pointer = (unsigned long) RDsize(p, ptrsize);
		return true;
	}

	bool readNext(CFIEntry& entry)
	{
		byte* p = ptr;
		if(!readHeader(p, entry.end, entry.CIE_pointer))
			return false;

		entry.ptr = ptr;

		if (entry.CIE_pointer == 0xffffffff)
		{
			entry.type = CFIEntry::CIE;
			readCIE(entry, p);
			entry.initial_instructions_length = entry.end - p;
		}
		else
		{
			entry.type = CFIEntry::FDE;

			byte* q = beg + entry.CIE_pointer, *qend;
			unsigned long cie_off;
			if (!readHeader(q, qend, cie_off))
				return false;
			if (cie_off != 0xffffffff)
				return false;
			readCIE(entry, q);
			entry.initial_instructions_length = qend - entry.initial_instructions;

			entry.segment = (unsigned long)(entry.segment_size > 0 ? RDsize(p, entry.segment_size) : 0);
			entry.initial_location = (unsigned long)RDsize(p, entry.address_size);
			entry.address_range = (unsigned long)RDsize(p, entry.address_size);
			entry.instructions = p;
			entry.instructions_length = entry.end - p;
		}
		ptr = entry.end;
		return true;
	}
};

class CFACursor
{
public:
	CFACursor(const PEImage& image, const CFIEntry& cfientry, unsigned long location)
	: img (image)
	, entry (cfientry)
	{
		loc = location;
		cfa = { Location::RegRel, DW_REG_CFA, 0 };
		setInstructions(entry.initial_instructions, entry.initial_instructions_length);
	}

	void setInstructions(byte* instructions, int length)
	{
		beg = instructions;
		end = instructions + length;
		ptr = beg;
	}

	bool beforeRestore()
	{
		if(ptr >= end)
			return false;
		byte instr = *ptr;
		if ((instr & 0xc0) == DW_CFA_restore || instr == DW_CFA_restore_extended || instr == DW_CFA_restore_state)
			return true;
		return false;
	}

	bool processNext()
	{
		if(ptr >= end)
			return false;
		byte instr = *ptr++;
		int reg, off;

		switch(instr & 0xc0)
		{
			case DW_CFA_advance_loc:
				loc += (instr & 0x3f) * entry.code_alignment_factor;
				break;
			case DW_CFA_offset:
				reg = instr & 0x3f; // set register rule to "factored offset"
				off = LEB128(ptr) * entry.data_alignment_factor;
				break;
			case DW_CFA_restore:
				reg = instr & 0x3f; // restore register to initial state
				break;

			case DW_CFA_extended:
			switch(instr)
			{
			case DW_CFA_set_loc:
				loc = RDsize(ptr, entry.address_size);
				break;
			case DW_CFA_advance_loc1:
				loc = *ptr++;
				break;
			case DW_CFA_advance_loc2:
				loc = RDsize(ptr, 2);
				break;
			case DW_CFA_advance_loc4:
				loc = RDsize(ptr, 4);
				break;

			case DW_CFA_def_cfa:
				cfa.reg = LEB128(ptr);
				cfa.off = LEB128(ptr);
				break;
			case DW_CFA_def_cfa_sf:
				cfa.reg = LEB128(ptr);
				cfa.off = SLEB128(ptr) * entry.data_alignment_factor;
				break;
			case DW_CFA_def_cfa_register:
				cfa.reg = LEB128(ptr);
				break;
			case DW_CFA_def_cfa_offset:
				cfa.off = LEB128(ptr);
				break;
			case DW_CFA_def_cfa_offset_sf:
				cfa.off = SLEB128(ptr) * entry.data_alignment_factor;
				break;
			case DW_CFA_def_cfa_expression:
			{
				DWARF_Attribute attr;
				attr.type = ExprLoc;
				attr.expr.len = LEB128(ptr);
				attr.expr.ptr = ptr;
				cfa = decodeLocation(attr);
				ptr += attr.expr.len;
				break;
			}

			case DW_CFA_undefined:
				reg = LEB128(ptr); // set register rule to "undefined"
				break;
			case DW_CFA_same_value:
				reg = LEB128(ptr); // set register rule to "same value"
				break;
			case DW_CFA_offset_extended:
				reg = LEB128(ptr); // set register rule to "factored offset"
				off = LEB128(ptr) * entry.data_alignment_factor;
				break;
			case DW_CFA_offset_extended_sf:
				reg = LEB128(ptr); // set register rule to "factored offset"
				off = SLEB128(ptr) * entry.data_alignment_factor;
				break;
			case DW_CFA_val_offset:
				reg = LEB128(ptr); // set register rule to "val offset"
				off = LEB128(ptr) * entry.data_alignment_factor;
				break;
			case DW_CFA_val_offset_sf:
				reg = LEB128(ptr); // set register rule to "val offset"
				off = SLEB128(ptr) * entry.data_alignment_factor;
				break;
			case DW_CFA_register:
				reg = LEB128(ptr); // set register rule to "register"
				reg = LEB128(ptr);
				break;
			case DW_CFA_expression:
			case DW_CFA_val_expression:
			{
				reg = LEB128(ptr); // set register rule to "expression"
				DWARF_Attribute attr;
				attr.type = Block;
				attr.block.len = LEB128(ptr);
				attr.block.ptr = ptr;
				cfa = decodeLocation(attr); // TODO: push cfa on stack
				ptr += attr.expr.len;
				break;
			}
			case DW_CFA_restore_extended:
				reg = LEB128(ptr); // restore register to initial state
				break;

				/* GNU extensions. */
			case DW_CFA_GNU_window_save:
				break; // used only by SPARC and AArch64
			case DW_CFA_GNU_args_size:
				LEB128(ptr); // ignore
				break;
			case DW_CFA_GNU_negative_offset_extended:
				reg = LEB128(ptr);
				off = -LEB128(ptr) * entry.data_alignment_factor;
				break;

			case DW_CFA_remember_state:
			case DW_CFA_restore_state:
			case DW_CFA_nop:
				break;
			}
		}
		return true;
	}

	const PEImage& img;
	const CFIEntry& entry;
	byte* beg;
	byte* end;
	byte* ptr;

	unsigned long long loc;
	Location cfa;
};

Location findBestCFA(const PEImage& img, const CFIIndex* index, unsigned int pclo, unsigned int pchi)
{
	bool x64 = img.isX64();
	Location ebp = { Location::RegRel, x64 ? 6 : 5, x64 ? 16 : 8 };
	if (!img.debug_frame.isPresent())
		return ebp;

	byte *fde_ptr = index->lookup(pclo, pchi);
	if (fde_ptr == NULL)
		return ebp;

	CFIEntry entry;
	CFICursor cursor(img);
	cursor.ptr = fde_ptr;

	if (cursor.readNext(entry))
	{
		CFACursor cfa(img, entry, pclo);
		while (cfa.processNext()) {}
		cfa.setInstructions(entry.instructions, entry.instructions_length);
		while (!cfa.beforeRestore() && cfa.processNext()) {}
		return cfa.cfa;
	}
	return ebp;
}

Location findBestFBLoc(const DIECursor& parent, unsigned long fblocoff)
{
	int regebp = parent.img->isX64() ? 6 : 5;
	LOCCursor cursor(parent, fblocoff);
	LOCEntry entry;
	Location longest = { Location::RegRel, DW_REG_CFA, 0 };
	unsigned long longest_range = 0;
	while(cursor.readNext(entry))
	{
		if(entry.loc.is_regrel() && entry.loc.reg == regebp)
			return entry.loc;
		unsigned long range = entry.end_offset - entry.beg_offset;
		if(range > longest_range)
		{
			longest_range = range;
			longest = entry.loc;
		}
	}
	return longest;
}

unsigned int alignLength(BYTE* data, unsigned int len)
{
	const unsigned int sym_align = 4;
	for (; len & (sym_align - 1); len++)
		data[len] = 0xf0 + sym_align - (len & (sym_align - 1));
	return len;
}

void CV2PDB::appendStackVar(const char* name, int type, Location& loc, Location& cfa)
{
	unsigned int len;
	unsigned int align = 4;
	checkUdtSymbolAlloc(100 + kMaxNameLen);

	codeview_symbol*cvs = (codeview_symbol*) (udtSymbols + cbUdtSymbols);

	int reg = loc.reg;
	int off = loc.off;
	CV_X86_REG baseReg;
	if (reg == DW_REG_CFA)
	{
		reg = cfa.reg;
		off += cfa.off;
	}
	if (img.isX64())
		baseReg = dwarf_to_amd64_reg(reg);
    else
		baseReg = dwarf_to_x86_reg(reg);

	if (baseReg == CV_REG_NONE)
		return;

	if (baseReg == CV_REG_EBP)
	{
		cvs->stack_v2.id = v3 ? S_BPREL_V3 : S_BPREL_V2;
		cvs->stack_v2.offset = off;
		cvs->stack_v2.symtype = type;
		len = cstrcpy_v(v3, (BYTE*)&cvs->stack_v2.p_name, name);
		len += (BYTE*)&cvs->stack_v2.p_name - (BYTE*)cvs;
	}
	else
	{
		cvs->regrel_v3.id = S_REGREL_V3;
		cvs->regrel_v3.reg = baseReg;
		cvs->regrel_v3.offset = off;
		cvs->regrel_v3.symtype = type;
		len = cstrcpy_v(true, (BYTE*)cvs->regrel_v3.name, name);
		len += (BYTE*)&cvs->regrel_v3.name - (BYTE*)cvs;
	}
	len = alignLength(udtSymbols + cbUdtSymbols, len);
	cvs->stack_v2.len = len - 2;
	cbUdtSymbols += len;
}

void CV2PDB::appendGlobalVar(const char* name, int type, int seg, int offset)
{
	unsigned int len;

	for(char* cname = (char*) name; *cname; cname++)
		if (*cname == '.')
			*cname = dotReplacementChar;

	checkUdtSymbolAlloc(100 + kMaxNameLen);

	codeview_symbol*cvs = (codeview_symbol*) (udtSymbols + cbUdtSymbols);
	cvs->data_v2.id = v3 ? S_GDATA_V3 : S_GDATA_V2;
	cvs->data_v2.offset = offset;
	cvs->data_v2.symtype = type;
	cvs->data_v2.segment = seg;
	len = cstrcpy_v (v3, (BYTE*) &cvs->data_v2.p_name, name);
	len += (BYTE*) &cvs->data_v2.p_name - (BYTE*) cvs;
	len = alignLength(udtSymbols + cbUdtSymbols, len);
	cvs->data_v2.len = len - 2;
	cbUdtSymbols += len;
}

bool CV2PDB::appendEndArg()
{
	checkUdtSymbolAlloc(8);

	codeview_symbol*cvs = (codeview_symbol*) (udtSymbols + cbUdtSymbols);
	cvs->generic.id = S_ENDARG_V1;
	cvs->generic.len = 2;
	cbUdtSymbols += 4;
	return true;
}

void CV2PDB::appendEnd()
{
	checkUdtSymbolAlloc(8);

	codeview_symbol*cvs = (codeview_symbol*) (udtSymbols + cbUdtSymbols);
	cvs->generic.id = S_END_V1;
	cvs->generic.len = 2;
	cbUdtSymbols += 4;
}

void CV2PDB::appendLexicalBlock(DWARF_InfoData& id, unsigned int proclo)
{
	checkUdtSymbolAlloc(32);

	codeview_symbol*dsym = (codeview_symbol*) (udtSymbols + cbUdtSymbols);
	dsym->block_v3.id = S_BLOCK_V3;
	dsym->block_v3.parent = 0;
	dsym->block_v3.end = 0; // destSize + sizeof(dsym->block_v3) + 12;
	dsym->block_v3.length = id.pchi - id.pclo;
	dsym->block_v3.offset = id.pclo - codeSegOff;
	dsym->block_v3.segment = img.text.secNo + 1;
	dsym->block_v3.name[0] = 0;
	int len = sizeof(dsym->block_v3);
	len = alignLength(udtSymbols + cbUdtSymbols, len);
	dsym->block_v3.len = len - 2;
	cbUdtSymbols += len;
}

// Helper to format a fully qualified proc name like 'some_ns::Foo::Foo' since
// for a Foo constructor in a Foo class in a namespace called "some_ns".
// PDBs require fully qualified names in their symbols.
// TODO: better error handling for out of space.
void CV2PDB::formatFullyQualifiedName(const DWARF_InfoData* node, char* buf, size_t cbBuf) const {
	if (node->specification) {
		// If the proc has a "specification", i.e. a declaration, use it instead
		// of the definition, as it has a proper hierarchy connected to it
		// which will give us a proper fully-qualified name like Foo::Foo
		// instead of just Foo.
		const DWARF_InfoData* entry = findEntryByPtr(node->specification);
		if (entry) {
			node = entry;
		}
	} else {
		// Find the node's entry in the DWARF tree. We can't use 'node' as is because
		// it is a local copy without linkage into the tree, as it comes from
		// the 2nd pass scan after the tree is already built.
		const DWARF_InfoData* entry = findEntryByPtr(node->entryPtr);
		assert(entry);  // how can it not exist? Bug in tree construction.
		node = entry;
	}
	DWARF_InfoData* parent = node->parent;
	std::vector<const DWARF_InfoData*> segments;
	segments.push_back(node);

	// Accumulate all the valid parent scopes so that we can reverse them for
	// formatting.
	while (parent) {
		switch (parent->tag) {
		// TODO: are there any other kinds of valid parents?
		case DW_TAG_class_type:
		case DW_TAG_structure_type:
		case DW_TAG_namespace:			
			segments.push_back(parent);
			break;
		default:
			break;
		}
		parent = parent->parent;
	}

	int remain = cbBuf;
	char* p = buf;

	// Format the parents in reverse order with :: operator in between.
	for (int i = segments.size() - 1; i >= 0; --i) {
		const char* name = segments[i]->name;
		char nameBuf[64] = {};
		int nameLen = 0;
		if (!segments[i]->name) {
			// This segment has no name. This could be because it is part of
			// an anonymous class, which often happens for lambda expressions.
			// Generate a unique anonymous name for it.
			nameLen = sprintf_s(nameBuf, "[anon_%x]", segments[i]->entryOff);
			if (nameLen < 0) {
				// Formatting failed. Try a default name.
				assert(false);  // crash in debug builds.
				name = "[anon]";
			}
			name = nameBuf;
		} else {
			nameLen = strlen(name);
		}
		if (remain < nameLen) {
			fprintf(stderr, "unable to fit full symbol name: %s\n", node->name);
			return;
		}

		memcpy(p, name, nameLen);

		p += nameLen;
		remain -= nameLen;

		if (i > 0) {
			// Append :: separator
			if (remain < 2) {
				fprintf(stderr, "unable to fit full symbol name (:: separator): %s\n", node->name);
				return;
			}
			*p++ = ':';
			*p++ = ':';
			remain -= 2;
		}
	}
	
	if (remain > 0) {
		*p = 0;  // NUL terminate.
	}
}

void mergeSpecification(DWARF_InfoData& id, const CV2PDB& context);

// Find the source of an inlined function by following its 'abstract_origin' 
// attribute references and recursively merge it into 'id'.
// TODO: this description isn't quite right. See section 3.3.8.1 in DWARF 4 spec.
void mergeAbstractOrigin(DWARF_InfoData& id, const CV2PDB& context)
{
	DWARF_InfoData* abstractOrigin = context.findEntryByPtr(id.abstract_origin);
	if (!abstractOrigin) {
		// Could not find abstract origin. Why not?
		assert(false);
		return;
	}

	// assert seems invalid, combination DW_TAG_member and DW_TAG_variable found
	// in the wild.
	//
	// assert(id.tag == idspec.tag);

	if (abstractOrigin->abstract_origin)
		mergeAbstractOrigin(*abstractOrigin, context);
	if (abstractOrigin->specification)
		mergeSpecification(*abstractOrigin, context);
	id.merge(*abstractOrigin);
}

// Find the declaration entry for a definition by following its 'specification'
// attribute references and merge it into 'id'.
void mergeSpecification(DWARF_InfoData& id, const CV2PDB& context)
{
	DWARF_InfoData* idspec = context.findEntryByPtr(id.specification);
	if (!idspec) {
		// Could not find decl for this definition. Why not?
		assert(false);
		return;
	}

	// assert seems invalid, combination DW_TAG_member and DW_TAG_variable found
	// in the wild.
	//
	// assert(id.tag == idspec.tag);

	if (idspec->abstract_origin)
		mergeAbstractOrigin(*idspec, context);
	if (idspec->specification) {
		mergeSpecification(*idspec, context);
	}
	id.merge(*idspec);
}

bool CV2PDB::addDWARFProc(DWARF_InfoData& procid, const std::vector<RangeEntry> &ranges, DIECursor cursor)
{
	unsigned int pclo = ranges.front().pclo - codeSegOff;
	unsigned int pchi = ranges.front().pchi - codeSegOff;

	unsigned int len;
	unsigned int align = 4;

	checkUdtSymbolAlloc(100 + kMaxNameLen);

	if (debug & DbgPdbSyms)
		fprintf(stderr, "%s:%d: Adding a proc: %s at %x\n", __FUNCTION__, __LINE__, procid.name, pclo);

	// GLOBALPROC
	codeview_symbol*cvs = (codeview_symbol*) (udtSymbols + cbUdtSymbols);
	cvs->proc_v2.id = v3 ? S_GPROC_V3 : S_GPROC_V2;
	cvs->proc_v2.pparent  = 0;
	cvs->proc_v2.pend     = 0;
	cvs->proc_v2.next     = 0;
	cvs->proc_v2.proc_len = pchi - pclo;
	cvs->proc_v2.debug_start = pclo - pclo;
	cvs->proc_v2.debug_end   = pchi - pclo;
	cvs->proc_v2.offset   = pclo;
	cvs->proc_v2.segment  = img.text.secNo + 1;
	cvs->proc_v2.proctype = 0; // translateType(sym->proc_v1.proctype);
	cvs->proc_v2.flags    = 0;

//    printf("GlobalPROC %s\n", procid.name);
	char namebuf[kMaxNameLen] = {};
	formatFullyQualifiedName(&procid, namebuf, sizeof namebuf);
	len = cstrcpy_v (v3, (BYTE*) &cvs->proc_v2.p_name, namebuf);
	len += (BYTE*) &cvs->proc_v2.p_name - (BYTE*) cvs;
	len = alignLength(udtSymbols + cbUdtSymbols, len);
	cvs->proc_v2.len = len - 2;
	cbUdtSymbols += len;

#if 0 // add funcinfo
	cvs = (codeview_symbol*) (udtSymbols + cbUdtSymbols);
	cvs->funcinfo_32.id = S_FUNCINFO_32;
	cvs->funcinfo_32.sizeLocals = 20;
	memset(cvs->funcinfo_32.unknown, 0, sizeof(cvs->funcinfo_32.unknown));
	cvs->funcinfo_32.unknown[5] = 4;
	cvs->funcinfo_32.info = 0x4200;
	cvs->funcinfo_32.unknown2 = 0x11;
	len = sizeof(cvs->funcinfo_32);
	len = alignLength(udtSymbols + cbUdtSymbols, len);
	cvs->funcinfo_32.len = len - 2;
	cbUdtSymbols += len;
#endif

#if 0
	addStackVar("local_var", 0x1001, 8);
#endif

	Location frameBase = decodeLocation(procid.frame_base, 0, DW_AT_frame_base);
	if (frameBase.is_abs()) // pointer into location list in .debug_loc? assume CFA
		frameBase = findBestFBLoc(cursor, frameBase.off);

    Location cfa = findBestCFA(*imgDbg, cfi_index, procid.pclo, procid.pchi);

	if (cursor.cu)
	{
		bool endarg = false;
		DWARF_InfoData id;
		int off = 8;

		// Save off the cursor to the start of the proc.
		DIECursor prev = cursor;

		// First, collect all the formal parameters of the proc.
		// Don't worry about storing these in the tree as we're not going to need
		// to generate fully-qualified names like we would for functions/classes.
		while (cursor.readNext(&id, true /* stopAtNull */))
		{
			if (id.tag == DW_TAG_formal_parameter && id.name)
			{
				if (id.location.type == ExprLoc || id.location.type == Block || id.location.type == SecOffset)
				{
					Location loc = id.location.type == SecOffset ? findBestFBLoc(cursor, id.location.sec_offset)
					                                             : decodeLocation(id.location, &frameBase);
					if (loc.is_regrel())
						appendStackVar(id.name, getTypeByDWARFPtr(id.type), loc, cfa);
				}
			}
		}
		appendEndArg();

		//  Now, collect all the lexical blocks and their stack variables.
		std::vector<DIECursor> lexicalBlocks;

		// Start from the proc base, and push all nested lexical blocks as you
		// encounter them.
		lexicalBlocks.push_back(prev);

		while (!lexicalBlocks.empty())
		{
			cursor = lexicalBlocks.back();
			lexicalBlocks.pop_back();

			while (cursor.readNext(&id))
			{
				if (id.tag == DW_TAG_lexical_block)
				{
					// It seems it is not possible to describe blocks with
					// non-contiguous address ranges in CodeView. Instead,
					// just create a range that is large enough to cover
					// all continuous ranges.
					if (id.hasChild && id.ranges != ~0)
					{
						id.pclo = ~0;
						id.pchi = 0;

						// TODO: handle base address selection
						RangeEntry range;
						RangeCursor rangeCursor(cursor, id.ranges);
						while (rangeCursor.readNext(range))
						{
							id.pclo = min(id.pclo, range.pclo);
							id.pchi = max(id.pchi, range.pchi);
						}
					}

					if (id.hasChild && id.pchi > id.pclo)
					{
						appendLexicalBlock(id, pclo + codeSegOff);
						DIECursor next = cursor;

						// Compute the sibling node of this lexical block.
						next.gotoSibling();
						assert(lexicalBlocks.empty() || next.ptr <= lexicalBlocks.back().ptr);

						// Append the next lexical block to the list of blocks
						// to scan later.
						lexicalBlocks.push_back(next);

						// But for now, scan down the current lexical block.
						cursor = cursor.getSubtreeCursor();
						continue;
					}
				}
				else if (id.tag == DW_TAG_variable)
				{
					// Found a local variable.
					if (id.name && (id.location.type == ExprLoc || id.location.type == Block))
					{
						Location loc = id.location.type == SecOffset ? findBestFBLoc(cursor, id.location.sec_offset)
						                                             : decodeLocation(id.location, &frameBase);
						if (loc.is_regrel())
							appendStackVar(id.name, getTypeByDWARFPtr(id.type), loc, cfa);
					}
				}
				cursor.gotoSibling();
			}
			appendEnd();
			assert(lexicalBlocks.empty() || cursor.ptr <= lexicalBlocks.back().ptr);
		}
	}
	else
	{
		appendEndArg();
		appendEnd();
	}

	for (std::size_t i=1; i<ranges.size(); ++i)
	{
		const int sepcode_size = sizeof(codeview_symbol::sepcode_v3);
		checkUdtSymbolAlloc(sepcode_size);
		codeview_symbol*cvs = (codeview_symbol*) (udtSymbols + cbUdtSymbols);
		cvs->sepcode_v3.id = S_SEPCODE_V3;
		cvs->sepcode_v3.len = sepcode_size - 2;
		cvs->sepcode_v3.parent = 0;
		cvs->sepcode_v3.end = 0;
		cvs->sepcode_v3.length = ranges[i].pchi - ranges[i].pclo;
		cvs->sepcode_v3.flags = 0;
		cvs->sepcode_v3.offset = ranges[i].pclo - codeSegOff;
		cvs->sepcode_v3.parent_offset = pclo;
		cvs->sepcode_v3.section = img.text.secNo + 1;
		cvs->sepcode_v3.parent_section = img.text.secNo + 1;
		cbUdtSymbols += sepcode_size;

		appendEnd();
	}
	return true;
}

// Only looks at DW_TAG_member and DW_TAG_inheritance
int CV2PDB::addDWARFFields(DWARF_InfoData& structid, DIECursor& cursor, int baseoff, int flStart, bool& hasBackRef)
{
	bool isunion = structid.tag == DW_TAG_union_type;
	int nfields = 0;

	// cursor points to the first member of the class/struct/union.
	DWARF_InfoData id;
	while (cursor.readNext(&id, true /* stopAtNull */))
	{
		if (cbDwarfTypes - flStart > 0x10000 - kMaxNameLen - 100)
			break; // no more space in field list, TODO: add continuation record, see addDWARFEnum

		int cvid = -1;
		if (id.tag == DW_TAG_member)
		{
			//printf("    Adding field %s\n", id.name);
			int off = 0;
			if (!isunion)
			{
				Location loc = decodeLocation(id.member_location, 0, DW_AT_data_member_location);
				if (loc.is_abs())
				{
					off = loc.off;
					cvid = S_CONSTANT_V2;
				}
			}

			if (isunion || cvid == S_CONSTANT_V2)
			{
				if (id.name)
				{
					checkDWARFTypeAlloc(kMaxNameLen + 100);
					codeview_fieldtype* dfieldtype = (codeview_fieldtype*)(dwarfTypes + cbDwarfTypes);
					const DWARF_InfoData* entry = findEntryByPtr(id.type);
					if (entry && entry->tag == DW_TAG_pointer_type)
					{
						const DWARF_InfoData* ptrEntry = findEntryByPtr(entry->type);
						if (ptrEntry && ptrEntry->abbrev == structid.abbrev)
							hasBackRef = true;
					}
					cbDwarfTypes += addFieldMember(dfieldtype, 0, baseoff + off, getTypeByDWARFPtr(id.type), id.name);
					nfields++;
				}
				else if (id.type)
				{
					// if it doesn't have a name, and it's a struct or union, embed it directly
					DIECursor membercursor(cursor, id.type);
					DWARF_InfoData memberid;
					if (membercursor.readNext(&memberid))
					{
						if (memberid.abstract_origin)
							mergeAbstractOrigin(memberid, *this);
						if (memberid.specification)
							mergeSpecification(memberid, *this);

						int cvtype = -1;
						switch (memberid.tag)
						{
						case DW_TAG_class_type:
						case DW_TAG_structure_type:
						case DW_TAG_union_type:
							nfields += addDWARFFields(memberid, membercursor, baseoff + off, flStart, hasBackRef);
							break;
						}
					}
				}
			}
		}
		else if (id.tag == DW_TAG_inheritance)
		{
			int off = 0;
			Location loc = decodeLocation(id.member_location, 0, DW_AT_data_member_location);
			if (loc.is_abs())
			{
				cvid = S_CONSTANT_V2;
				off = loc.off;
			}
			if (cvid == S_CONSTANT_V2)
			{
				checkDWARFTypeAlloc(sizeof(codeview_fieldtype) + 4);
				codeview_fieldtype* bc = (codeview_fieldtype*)(dwarfTypes + cbDwarfTypes);
				bc->bclass_v2.id = LF_BCLASS_V2;
				bc->bclass_v2.offset = baseoff + off;
				bc->bclass_v2.type = getTypeByDWARFPtr(id.type);
				bc->bclass_v2.attribute = 3; // public
				cbDwarfTypes += sizeof(bc->bclass_v2);
				cbDwarfTypes = alignLength(dwarfTypes, cbDwarfTypes);
				nfields++;
			}
		}
		cursor.gotoSibling();
	}
	return nfields;
}

// Add a class/struct/union to the database.
int CV2PDB::addDWARFStructure(DWARF_InfoData& structid, DIECursor cursor)
{
	//printf("Adding struct %s, entryoff %d, abbrev %d\n", structid.name, structid.entryOff, structid.abbrev);

	int fieldlistType = 0;
	int nfields = 0;
	bool hasBackRef = false;
	if (cursor.cu)
	{
		checkDWARFTypeAlloc(100);
		codeview_reftype* fl = (codeview_reftype*) (dwarfTypes + cbDwarfTypes);
		int flbegin = cbDwarfTypes;
		fl->fieldlist.id = LF_FIELDLIST_V2;
		cbDwarfTypes += 4;

#if 0
		if(structid.containing_type && structid.containing_type != structid.entryOff)
		{
			codeview_fieldtype* bc = (codeview_fieldtype*) (dwarfTypes + cbDwarfTypes);
			bc->bclass_v2.id = LF_BCLASS_V2;
			bc->bclass_v2.offset = 0;
			bc->bclass_v2.type = getTypeByDWARFPtr(cu, structid.containing_type);
			bc->bclass_v2.attribute = 3; // public
			cbDwarfTypes += sizeof(bc->bclass_v2);
			cbDwarfTypes = alignLength(dwarfTypes, cbDwarfTypes);
			nfields++;
		}
#endif
		nfields += addDWARFFields(structid, cursor, 0, flbegin, hasBackRef);
		fl = (codeview_reftype*) (dwarfTypes + flbegin);
		fl->fieldlist.len = cbDwarfTypes - flbegin - 2;
		fieldlistType = nextDwarfType++;
	}

	char namebuf[kMaxNameLen] = {};
	formatFullyQualifiedName(&structid, namebuf, sizeof namebuf);

	int udttype = nextUserType;
	if (hasBackRef && fieldlistType)
	{
		// with back references, make the original struct incomplete and
		// put the full definition into the DWARF chunk
		checkDWARFTypeAlloc(kMaxNameLen + 100);
		codeview_type* dwarf = (codeview_type*)(dwarfTypes + cbDwarfTypes);
		int len = addAggregate(dwarf, structid.tag == DW_TAG_class_type, nfields, fieldlistType, 0, 0, 0, structid.byte_size, namebuf, nullptr);
		cbDwarfTypes += len;
		udttype = nextDwarfType++;
		fieldlistType = 0;
		nfields = 0;
	}
	checkUserTypeAlloc(kMaxNameLen + 100);
	codeview_type* cvt = (codeview_type*)(userTypes + cbUserTypes);
	int attr = fieldlistType ? 0 : kPropIncomplete;
	int len = addAggregate(cvt, structid.tag == DW_TAG_class_type, nfields, fieldlistType, attr, 0, 0, structid.byte_size, namebuf, nullptr);
	cbUserTypes += len;
	int cvtype = nextUserType++;

	//ensureUDT()?
	addUdtSymbol(udttype, namebuf);
	return cvtype;
}

// Compute the array bounds of the DIE at the given 'cursor'.
void CV2PDB::getDWARFArrayBounds(DIECursor cursor, int& basetype, int& lowerBound, int& upperBound)
{
	DWARF_InfoData id;

	// TODO: handle multi-dimensional arrays
	if (cursor.cu)
	{
		// Don't insert these elements into the DB. We're just using it for
		// array bounds calculation.
		while (cursor.readNext(&id, true /* stopAtNull */))
		{
			if (id.tag == DW_TAG_subrange_type)
			{
				getDWARFSubrangeInfo(id, cursor, basetype, lowerBound, upperBound);
				return;
			}
			cursor.gotoSibling();
		}
	}

	// In case of error, return plausible defaults
	getDWARFSubrangeInfo(id, cursor, basetype, lowerBound, upperBound);
}

void CV2PDB::getDWARFSubrangeInfo(DWARF_InfoData& subrangeid, const DIECursor& parent,
	int& basetype, int& lowerBound, int& upperBound)
{
	// In case of error, return plausible defaults. Assume the array
	// contains one item: this is probably helpful to users.
	basetype = T_INT4;
	lowerBound = currentDefaultLowerBound;
	upperBound = lowerBound;

	if (!parent.cu || subrangeid.tag != DW_TAG_subrange_type)
		return;

	basetype = getTypeByDWARFPtr(subrangeid.type);
	if (subrangeid.has_lower_bound)
		lowerBound = subrangeid.lower_bound;
	upperBound = subrangeid.upper_bound;
}

// Compute a type ID for a basic DWARF type.
int CV2PDB::getDWARFBasicType(int encoding, int byte_size)
{
	int type = 0, mode = 0, size = 0;
	switch (encoding)
	{
	case DW_ATE_boolean:        type = 3; break;
	case DW_ATE_complex_float:  type = 5; byte_size /= 2; break;
	case DW_ATE_float:          type = 4; break;
	case DW_ATE_signed:         type = 1; break;
	case DW_ATE_signed_char:    type = 7; break;
	case DW_ATE_unsigned:       type = 2; break;
	case DW_ATE_unsigned_char:  type = 7; break;
	case DW_ATE_imaginary_float:type = 4; break;
	case DW_ATE_UTF:            type = 7; break;
	default:
		setError("unknown basic type encoding");
	}
	switch (type)
	{
	case 1: // signed
	case 2: // unsigned
	case 3: // boolean
		switch (byte_size)
		{
		case 1: size = 0; break;
		case 2: size = 1; break;
		case 4: size = 2; break;
		case 8: size = 3; break;
		case 16: size = 4; break; // __int128? experimental, type exists with GCC for Win64
		default:
			setError("unsupported integer type size");
		}
		break;
	case 4:
	case 5:
		switch (byte_size)
		{
		case 4:  size = 0; break;
		case 8:  size = 1; break;
		case 10: size = 2; break;
		case 12: size = 2; break; // with padding bytes
		case 16: size = 3; break;
		case 6:  size = 4; break;
		default:
			setError("unsupported real type size");
		}
		break;
	case 7:
		switch (byte_size)
		{
		case 1:  size = 0; break;
		case 2:  size = encoding == DW_ATE_signed_char ? 2 : 3; break;
		case 4:  size = encoding == DW_ATE_signed_char ? 4 : 5; break;
		case 8:  size = encoding == DW_ATE_signed_char ? 6 : 7; break;
		default:
			setError("unsupported real int type size");
		}
	}
	int t = size | (type << 4);
	return translateType(t);
}

// TODO: Array wanted to be scanned twice due to DW_TAG_subrange_type being looked at
// in the caller. See if it can be handled in a single place for clarity, simplicity & efficiency.
// Goal: don't rescan the same DIE twice.
int CV2PDB::addDWARFArray(DWARF_InfoData& arrayid, const DIECursor& cursor)
{
	int basetype, upperBound, lowerBound;
	getDWARFArrayBounds(cursor, basetype, lowerBound, upperBound);

	checkUserTypeAlloc(kMaxNameLen + 100);
	codeview_type* cvt = (codeview_type*) (userTypes + cbUserTypes);

	cvt->array_v2.id = v3 ? LF_ARRAY_V3 : LF_ARRAY_V2;
	cvt->array_v2.elemtype = getTypeByDWARFPtr(arrayid.type);
	cvt->array_v2.idxtype = basetype;
	int len = (BYTE*)&cvt->array_v2.arrlen - (BYTE*)cvt;
	int size = (upperBound - lowerBound + 1) * getDWARFTypeSize(cursor, arrayid.type);
	len += write_numeric_leaf(size, &cvt->array_v2.arrlen);
	((BYTE*)cvt)[len++] = 0; // empty name
	len = alignLength(userTypes + cbUserTypes, len);
	cvt->array_v2.len = len - 2;

	cbUserTypes += len;

	int cvtype = nextUserType++;
	return cvtype;
}

bool CV2PDB::addDWARFSymbols()
{
	checkUdtSymbolAlloc(100);

	int prefix = 4;
	DWORD* ddata = new DWORD [imgDbg->debug_info.length/4]; // large enough
	unsigned char *data = (unsigned char*) (ddata + prefix);
	unsigned int off = 0;
	unsigned int len;
	unsigned int align = 4;

	// SSEARCH
	codeview_symbol* cvs = (codeview_symbol*) (data + off);
	cvs->ssearch_v1.id = S_SSEARCH_V1;
	cvs->ssearch_v1.segment = img.text.secNo + 1;
	cvs->ssearch_v1.offset = 0;
	len = sizeof(cvs->ssearch_v1);
	len = alignLength(data + off, len);
	cvs->ssearch_v1.len = len - 2;
	off += len;

	// COMPILAND
	cvs = (codeview_symbol*) (data + off);
	cvs->compiland_v1.id = S_COMPILAND_V1;
	cvs->compiland_v1.language = 1; // C++
	cvs->compiland_v1.flags = 0x80; // ?, data model
	cvs->compiland_v1.machine = img.isX64() ? 0xd0 : 6; //0x06: Pentium Pro/II, 0xd0: x64
	len = sizeof(cvs->compiland_v1) - sizeof(cvs->compiland_v1.p_name);
	len += c2p("cv2pdb", cvs->compiland_v1.p_name);
	len = alignLength(data + off, len);
	cvs->compiland_v1.len = len - 2;
	off += len;

#if 0
	// define one proc over everything
	int s = codeSegment;
	int pclo = 0; // img.getImageBase() + img.getSection(s).VirtualAddress;
	int pchi = pclo + img.getSection(s).Misc.VirtualSize;
	addDWARFProc("procall", pclo, pchi, 0, 0, 0);
#endif

	//////////////////////////
	mspdb::Mod* mod = globalMod();
	//return writeSymbols (mod, ddata, off, prefix, true);
	return addSymbols(mod, data, off, true);
}

bool CV2PDB::addDWARFSectionContrib(mspdb::Mod* mod, unsigned long pclo, unsigned long pchi)
{
	int segIndex = imgDbg->findSection(pclo);
	if(segIndex >= 0)
	{
		int segFlags = 0x60101020; // 0x40401040, 0x60500020; // TODO
		int rc = mod->AddSecContrib(segIndex, pclo, pchi - pclo, segFlags);
		if (rc <= 0)
			return setError("cannot add section contribution to module");
	}
	return true;
}

int CV2PDB::addDWARFBasicType(const char*name, int encoding, int byte_size)
{
	int t = getDWARFBasicType(encoding, byte_size);
	int cvtype = appendTypedef(t, name, false);
	if(useTypedefEnum)
		addUdtSymbol(cvtype, name);
	return cvtype;
}

int CV2PDB::addDWARFEnum(DWARF_InfoData& enumid, DIECursor cursor)
{
	/* Enumerated types are described in CodeView with two components:

	   1. A LF_ENUM leaf, representing the type itself. We put this one in the
		  userTypes buffer.

	   2. One or several LF_FIELDLIST records, to contain the list of
		  enumerators (name and value) associated to the enum type
		  (LF_ENUMERATE leaves). As type records cannot be larger 2**16 bytes,
		  we need to create multiple records when there are too many
		  enumerators. The first record contains the first LF_ENUMERATE leaves,
		  and then a LF_INDEX leaf that references a second LF_FIELDLIST
		  record, which contains the following LF_ENUMERATE leaves, and so
		  on. */

	codeview_reftype* rdtype;
	codeview_type* dtype;

	/* Type index and offset/length in dwarfTypes for the last LF_FIELDLIST record
	   we produced. */
	int fieldlistType = nextDwarfType++;
	int fieldlistOffset = cbDwarfTypes;
	int fieldlistLength = 0;

	/* Type index for the first LF_FIELDLIST record we produce. This is the one
	   that LF_ENUM will refer to */
	const int firstFieldlistType = fieldlistType;

	/* Total number of DW_TAG_enumerator DIEs we translate into
	   LF_ENUMERATE. */
	int count = 0;

	/* Create the LF_FIELDLIST record to contain enumerators. We will fill in
	   its length once done. */
	checkDWARFTypeAlloc(100);
	rdtype = (codeview_reftype*)(dwarfTypes + fieldlistOffset);
	rdtype->fieldlist.len = 0;
	rdtype->fieldlist.id = LF_FIELDLIST_V2;
	fieldlistLength += 4;

	/* Now fill this field list with the enumerators we find in DWARF. */
	DWARF_InfoData id;
	while (cursor.readNext(&id, true /* stopAtNull */))
	{
		if (id.tag == DW_TAG_enumerator && id.has_const_value)
		{
			cbDwarfTypes = fieldlistOffset + fieldlistLength;
			checkDWARFTypeAlloc(kMaxNameLen + 100);
			codeview_fieldtype* dfieldtype
				= (codeview_fieldtype*)(dwarfTypes + fieldlistOffset + fieldlistLength);
			int len = addFieldEnumerate(dfieldtype, id.name, id.const_value);

			/* If adding this enumerate leaves no room for a LF_INDEX leaf,
			   create a new LF_FIELDLIST record now. */
			if (fieldlistLength + len + sizeof(dfieldtype->index_v2) > 0xffff)
			{
				/* Append the LF_INDEX leaf. */
				codeview_fieldtype* indexLeaf
					= (codeview_fieldtype*)(dwarfTypes + fieldlistOffset + fieldlistLength);
				indexLeaf->index_v2.id = LF_INDEX_V2;
				indexLeaf->index_v2.unk = 0;
				fieldlistLength += sizeof(indexLeaf->index_v2);

				/* Set the length of the previous LF_FIELDLIST record. */
				rdtype = (codeview_reftype*)(dwarfTypes + fieldlistOffset);
				rdtype->fieldlist.len += fieldlistLength - 2;

				/* Create the new LF_FIELDLIST record. */
				cbDwarfTypes = fieldlistOffset + fieldlistLength;
				int newFieldlistType = nextDwarfType++;
				int newFieldlistOffset = cbDwarfTypes;
				int newFieldlistLength = 0;

				rdtype = (codeview_reftype*)(dwarfTypes + newFieldlistOffset);
				rdtype->fieldlist.len = 0;
				rdtype->fieldlist.id = LF_FIELDLIST_V2;
				newFieldlistLength += 4;

				/* Reference this new record from the LF_INDEX leaf. */
				indexLeaf->index_v2.ref = newFieldlistType;

				/* Make next runs target the new LF_FIELDLIST record. */
				fieldlistType = newFieldlistType;
				fieldlistOffset = newFieldlistOffset;
				fieldlistLength = newFieldlistLength;

				/* Append the current enumerator to the new record. */
				cbDwarfTypes = fieldlistOffset + fieldlistLength;
				checkDWARFTypeAlloc(kMaxNameLen + 100);
				dfieldtype = (codeview_fieldtype*)(dwarfTypes + fieldlistOffset + fieldlistLength);
				len = addFieldEnumerate(dfieldtype, id.name, id.const_value);
			}
			fieldlistLength += len;
			count++;
		}
	}
	cbDwarfTypes = fieldlistOffset + fieldlistLength;

	/* The field list is ready, so we can know fill in its length: it is the
	   number of bytes we stored in dwarfTypes since we created it, minus the
	   usual 2 bytes for type record size. */
	rdtype = (codeview_reftype*)(dwarfTypes + fieldlistOffset);
	rdtype->fieldlist.len += fieldlistLength - 2;

	/* Now the LF_FIELDLIST is ready, create the LF_ENUM type record itself. */
	checkUserTypeAlloc();
	const DWARF_InfoData* entry = findEntryByPtr(enumid.entryPtr);
	int prop = 0;
	if (entry && entry->parent) {
		int tag = entry->parent->tag;
		if (tag == DW_TAG_class_type ||
			tag == DW_TAG_structure_type ||
			tag == DW_TAG_union_type)
		{
			prop |= kPropIsNested;
		}
	}

	// NOTE: WinDbg/VS Dbg expects enum types to be base types, not indirect
	// refs/UDTs.
	// 
	// Compute the best base/underlying type to use.
	int encoding = DW_ATE_signed;  // default to int
	const DWARF_InfoData* typeEntry = findEntryByPtr(enumid.type);
	const DWARF_InfoData* t = typeEntry;

	// Follow all the parent types to get to the base UDT.
	while (t) {
		t = findEntryByPtr(t->type);
		if (t) typeEntry = t;
	}

	if (typeEntry) {
		encoding = typeEntry->encoding;
		assert(typeEntry->byte_size == enumid.byte_size);
	}

	const int basetype = getDWARFBasicType(encoding, enumid.byte_size);

	dtype = (codeview_type*)(userTypes + cbUserTypes);
	char namebuf[kMaxNameLen] = {};
	formatFullyQualifiedName(&enumid, namebuf, sizeof namebuf);
	cbUserTypes += addEnum(dtype, count, firstFieldlistType, prop, basetype, namebuf);
	int enumType = nextUserType++;

	addUdtSymbol(enumType, namebuf);
	return enumType;
}

// Try to find or compute the "best" CV TypeID for a given DIE found by following
// a DW_AT_type attribute or its closest counterpart.
int CV2PDB::getTypeByDWARFPtr(byte* typePtr)
{
	if (typePtr == nullptr)
		return T_NOTYPE;

	// First just attempt to find the type entry directly.
	int ret = findTypeIdByPtr(typePtr);
	if (!ret) {
		// TypeID was not found in the map. This may be due to struct
		// decl / definition consolidation. I.e. we don't emit the struct decl
		// because they show up as "empty" structs (devoid of members).
		// Try to match this against the logically equivalent "definition"
		// type.
		DWARF_InfoData* entry = findEntryByPtr(typePtr);
		assert(entry); // how can the entry not exist in the map?

		// Skip anonymous structures or similar.
		if (!entry || !entry->name) {
			return T_NOTYPE;
		}

		// See if there exists another "logically equivalent" entry in the tree.
		// 
		// First, find all entries with the same (local) name as this type.
		auto range = mapEntryNameToEntries.equal_range(entry->name);
		for (auto it = range.first; it != range.second; ++it) {
			DWARF_InfoData* candidate = it->second;

			// Skip self.
			if (candidate == entry) {
				continue;
			}

			// Skip declarations (as when they are of structs, they don't help.
			// We want definitions only as they define the fields in DWARF.)
			if (candidate->isDecl) {
				continue;
			}
			
			// Filter nodes based on the matching tag.
			if (candidate->tag != entry->tag) {
				continue;
			}

			// Found a matching tag for this element. Walk up the tree and check
			// if all parent tags and names match also. If they do, we found an
			// "equivalent" node to 'typePtr' one that wasn't added to the
			// typeID registry (because it was likely a decl that we filtered out)
			DWARF_InfoData* candidateParent = candidate->parent;
			DWARF_InfoData* entryParent = entry->parent;

			bool equivalentHierarchy = true;
			while (candidateParent && entryParent) {
				if (candidateParent->tag != entryParent->tag) {
					// Tag mismatch.
					equivalentHierarchy = false;
					break;
				}

				// Skip CUs as of course they have different names. We only
				// care about namespaces, other containing structs, classes, etc.
				// Entries have the same tag. Checking one is sufficient.
				if (entryParent->tag != DW_TAG_compile_unit) {

					if (strcmp(candidateParent->name, entryParent->name)) {
						// Name mismatch.
						equivalentHierarchy = false;
						break;
					}
				}

				candidateParent = candidateParent->parent;
				entryParent = entryParent->parent;
			}

			if (equivalentHierarchy) {
				// Try another lookup with this new candidate.
				ret = findTypeIdByPtr(candidate->entryPtr);
				assert(ret);  // how can it now be in the map?
			} else {
				fprintf(stderr, "warn: could not find equivalent entry for typePtr %p\n", typePtr);
			}
		}
	}
	return ret;
}

// Get the logical size of a DWARF type, starting from 'typePtr' and recursing
// if necessary. E.g. for arrays.
int CV2PDB::getDWARFTypeSize(const DIECursor& parent, byte* typePtr)
{
	DWARF_InfoData id;
	DIECursor cursor(parent, typePtr);

	// Don't allocate this into the tree since we're just interested
	// in computing a type.
	if (!cursor.readNext(&id))
		return 0;

	if(id.byte_size > 0)
		return id.byte_size;

	switch(id.tag)
	{
		case DW_TAG_ptr_to_member_type:
		case DW_TAG_reference_type:
		case DW_TAG_pointer_type:
			return cursor.cu->address_size;
		case DW_TAG_array_type:
		{
			int basetype, upperBound, lowerBound;
			getDWARFArrayBounds(cursor, basetype, lowerBound, upperBound);
			return (upperBound - lowerBound + 1) * getDWARFTypeSize(cursor, id.type);
		}
		default:
			if(id.type)
				return getDWARFTypeSize(cursor, id.type);
			break;
	}
	return 0;
}

// Scan the .debug_info section and allocate type IDs for each unique type and
// create a mapping to look them up by their address.
// This is the first pass scan that builds up the DWARF tree. The second pass (createTypes)
// emits the actual PDB symbols.
bool CV2PDB::mapTypes()
{
	int typeID = nextUserType;
	unsigned long off = 0;

	if (debug & DbgBasic)
		fprintf(stderr, "%s:%d: mapTypes()\n", __FUNCTION__, __LINE__);

	// Maintain the first node of each CU to ensure all of them get linked.
	DWARF_InfoData* firstNode = nullptr;

	// Scan each compilation unit in '.debug_info'.
	while (off < imgDbg->debug_info.length)
	{
		DWARF_CompilationUnitInfo cu{};

		// Read the next compilation unit from 'off' and update it to the next
		// CU.
		byte* ptr = cu.read(debug, *imgDbg, &off);
		if (!ptr)
			continue;

		// We only support regular full 'DW_UT_compile' compilation units.
		if (cu.unit_type != DW_UT_compile) {
			if (debug & DbgDwarfCompilationUnit)
				fprintf(stderr, "%s:%d: skipping compilation unit offs=%x, unit_type=%d\n", __FUNCTION__, __LINE__,
						cu.cu_offset, cu.unit_type);

			continue;
		}

		DIECursor cursor(&cu, ptr);

		// Set up link to ensure this CU links to the prior one.
		cursor.prevNode = firstNode;

		DWARF_InfoData* node = nullptr;
		bool setFirstNode = false;
		// Start scanning this CU from the beginning and *build a tree of DIE nodes*.
		while ((node = cursor.readNext(nullptr)) != nullptr)
		{
			DWARF_InfoData& id = *node;

			// Initialize the head of the DWARF DIE list the first time.
			if (!dwarfHead) {
				dwarfHead = node;
			}

			if (!setFirstNode) {
				firstNode = node;
				setFirstNode = true;
			}

			if (debug & DbgDwarfTagRead)
				fprintf(stderr, "%s:%d: 0x%08x, level = %d, id.code = %d, id.tag = %d\n", __FUNCTION__, __LINE__,
						cursor.entryOff, cursor.level, id.code, id.tag);

			// Insert the node into the entryPtr-based index.
			mapEntryPtrToEntry[node->entryPtr] = node;

			// Insert named nodes into the name-based index.
			if (node->name) {
				mapEntryNameToEntries.insert({ node->name, node });
			}

			switch (id.tag)
			{
				case DW_TAG_structure_type:
				case DW_TAG_class_type:
				case DW_TAG_union_type:
					// skip generating a typeID for declaration flavor of
					// class/struct/union since we don't emit the PDB symbol
					// for them. See related code in CV2PDB::createTypes().
					if (id.isDecl) continue; 
				case DW_TAG_base_type:
				case DW_TAG_typedef:
				case DW_TAG_pointer_type:
				case DW_TAG_subroutine_type:
				case DW_TAG_array_type:
				case DW_TAG_const_type:
				case DW_TAG_reference_type:
				case DW_TAG_enumeration_type:
				case DW_TAG_string_type:
				case DW_TAG_ptr_to_member_type:
				case DW_TAG_set_type:
				case DW_TAG_subrange_type:
				case DW_TAG_file_type:
				case DW_TAG_packed_type:
				case DW_TAG_thrown_type:
				case DW_TAG_volatile_type:
				case DW_TAG_restrict_type: // DWARF3
				case DW_TAG_interface_type:
				case DW_TAG_unspecified_type:
				case DW_TAG_mutable_type: // withdrawn
				case DW_TAG_shared_type:
				case DW_TAG_rvalue_reference_type:
					// Reserve a typeID and store it in the map for quick lookup.
					mapEntryPtrToTypeID.insert(std::make_pair(id.entryPtr, typeID));
					typeID++;
			}
		}
	}

	if (debug & DbgBasic)
		fprintf(stderr, "%s:%d: mapped %zd types\n", __FUNCTION__, __LINE__, mapEntryPtrToTypeID.size());

	nextDwarfType = typeID;
	assert(nextDwarfType == nextUserType + mapEntryPtrToTypeID.size());
	return true;
}

// Walks the .debug_info section and builds a DIE tree.
bool CV2PDB::createTypes()
{
	img.createSymbolCache();
	if (&img != imgDbg)
		imgDbg->createSymbolCache();
	mspdb::Mod* mod = globalMod();
	int firstUserType = nextUserType;
	int typeID = nextUserType;
	int pointerAttr = img.isX64() ? 0x1000C : 0x800A;

	if (debug & DbgBasic)
		fprintf(stderr, "%s:%d: createTypes()\n", __FUNCTION__, __LINE__);

	unsigned long off = 0;

	// Scan each compilation unit in '.debug_info'.
	while (off < imgDbg->debug_info.length)
	{
		DWARF_CompilationUnitInfo cu{};

		// Read the next compilation unit from 'off' and update it to the next
		// CU, returning the pointer just beyond the header to the first DIE.
		byte* ptr = cu.read(debug, *imgDbg, &off);
		if (!ptr)
			continue;

		if (cu.unit_type != DW_UT_compile) {
			if (debug & DbgDwarfCompilationUnit)
				fprintf(stderr, "%s:%d: skipping compilation unit offs=%x, unit_type=%d\n", __FUNCTION__, __LINE__,
						cu.cu_offset, cu.unit_type);

			continue;
		}

		DIECursor cursor(&cu, ptr);

		DWARF_InfoData* node = nullptr;
		bool setFirstNode = false;
		DWARF_InfoData id;

		// Scan the DIEs in this CU, reusing the elements.
		while (cursor.readNext(&id))
		{
			if (debug & DbgDwarfTagRead)
				fprintf(stderr, "%s:%d: 0x%08x, level = %d, id.code = %d, id.tag = %d\n", __FUNCTION__, __LINE__,
						cursor.entryOff, cursor.level, id.code, id.tag);

			// Merge in related entries. This relies on the DWARF tree having been built
			// in the first pass (mapTypes).
			if (id.abstract_origin)
				mergeAbstractOrigin(id, *this);
			if (id.specification)
				mergeSpecification(id, *this);

			int cvtype = -1;
			switch (id.tag)
			{
			case DW_TAG_base_type:
				cvtype = addDWARFBasicType(id.name, id.encoding, id.byte_size);
				break;
			case DW_TAG_typedef:
				cvtype = appendModifierType(getTypeByDWARFPtr(id.type), 0);
				addUdtSymbol(cvtype, id.name);
				break;
			case DW_TAG_pointer_type:
				cvtype = appendPointerType(getTypeByDWARFPtr(id.type), pointerAttr);
				break;
			case DW_TAG_const_type:
				cvtype = appendModifierType(getTypeByDWARFPtr(id.type), 1);
				break;
			case DW_TAG_reference_type:
				cvtype = appendPointerType(getTypeByDWARFPtr(id.type), pointerAttr | 0x20);
				break;

			case DW_TAG_subrange_type:
				// It seems we cannot materialize bounds for scalar types in
				// CodeView, so just redirect to a mere base type.
				cvtype = appendModifierType(getTypeByDWARFPtr(id.type), 0);
				break;

			case DW_TAG_class_type:
			case DW_TAG_structure_type:
			case DW_TAG_union_type:
				if (!id.isDecl) {
					// Only export the non-declaration version of structs/classes.
					// DWARF emits multiple copies of the same class, some of
					// which are marked as declarations and lack members, resulting
					// in an empty struct UDT in the PDB. Then when we encounter
					// the non-declaration copy we emit it again, but now we
					// end up with multiple copies of the same UDT in the PDB
					// and the debugger gets confused.
					cvtype = addDWARFStructure(id, cursor);
				}
				break;
			case DW_TAG_array_type:
				cvtype = addDWARFArray(id, cursor);
				break;

			case DW_TAG_enumeration_type:
				cvtype = addDWARFEnum(id, cursor);
				break;

			case DW_TAG_subroutine_type:
			case DW_TAG_string_type:
			case DW_TAG_ptr_to_member_type:
			case DW_TAG_set_type:
			case DW_TAG_file_type:
			case DW_TAG_packed_type:
			case DW_TAG_thrown_type:
			case DW_TAG_volatile_type:
			case DW_TAG_restrict_type: // DWARF3
			case DW_TAG_interface_type:
			case DW_TAG_unspecified_type:
			case DW_TAG_mutable_type: // withdrawn
			case DW_TAG_shared_type:
			case DW_TAG_rvalue_reference_type:
				cvtype = appendPointerType(0x74, pointerAttr);
				break;

			case DW_TAG_subprogram:
				if (id.name)
				{
					std::vector<RangeEntry> ranges = getRanges(cursor, id);
					if (!ranges.empty())
					{
						if (!id.is_artificial)
						{
							std::uint64_t entry_point = ranges.front().pclo;
							if (debug & DbgPdbSyms)
								fprintf(stderr, "%s:%d: Adding a public: %s at %llx\n", __FUNCTION__, __LINE__, id.name, entry_point);

							mod->AddPublic2(id.name, img.text.secNo + 1, entry_point - codeSegOff, 0);
						}

						// Only add the definition, not declaration, because
						// MSVC toolset only produces a single symbol for
						// each function and will get confused if there are
						// 2 PDB symbols for the same routine.
						//
						// TODO: Add more type info to the routine. Today we
						// expose it as "T_NOTYPE" when we could do better.
						if (!id.isDecl) {
							addDWARFProc(id, ranges, cursor);
						}
					}
				}
				break;

			case DW_TAG_compile_unit:
				// Set the implicit base address for range lists.
				cu.base_address = id.pclo;
				switch (id.language)
				{
				case DW_LANG_Ada83:
				case DW_LANG_Cobol74:
				case DW_LANG_Cobol85:
				case DW_LANG_Fortran77:
				case DW_LANG_Fortran90:
				case DW_LANG_Pascal83:
				case DW_LANG_Modula2:
				case DW_LANG_Ada95:
				case DW_LANG_Fortran95:
				case DW_LANG_PLI:
					currentDefaultLowerBound = 1;
					break;

				default:
					currentDefaultLowerBound = 0;
				}
#if !FULL_CONTRIB
				if (id.dir && id.name)
				{
					if (id.ranges > 0 && id.ranges < imgDbg->debug_ranges.length)
					{
						RangeEntry range;
						RangeCursor rangeCursor(cursor, id.ranges);
						while (rangeCursor.readNext(range))
						{
							if (debug & DbgPdbContrib)
								fprintf(stderr, "%s:%d: Adding a section contrib: %I64x-%I64x\n", __FUNCTION__, __LINE__,
										range.pclo, range.pchi);

							if (!addDWARFSectionContrib(mod, range.pclo, range.pchi))
								return false;
						}
					}
					else
					{
						if (debug & DbgPdbContrib)
							fprintf(stderr, "%s:%d: Adding a section contrib: %x-%x\n", __FUNCTION__, __LINE__,
									id.pclo, id.pchi);

						if (!addDWARFSectionContrib(mod, id.pclo, id.pchi))
							return false;
					}
				}
#endif
				break;

			case DW_TAG_variable:
				if (id.name)
				{
					int seg = -1;
					unsigned long segOff;
					bool dllimport = false;
					if (id.location.type == Invalid && id.external && id.linkage_name)
					{
						seg = imgDbg->findSymbol(id.linkage_name, segOff, dllimport);
					}
					else if (id.location.type == Invalid && id.external)
					{
						seg = imgDbg->findSymbol(id.name, segOff, dllimport);
					}
					else
					{
						Location loc = decodeLocation(id.location);
						if (loc.is_abs())
						{
							segOff = loc.off;
							seg = imgDbg->findSection(segOff);
							if (seg >= 0)
								segOff -= imgDbg->getImageBase() + imgDbg->getSection(seg).VirtualAddress;
						}
					}
					if (seg >= 0)
					{
						int type = getTypeByDWARFPtr(id.type);
						if (dllimport)
						{
							checkDWARFTypeAlloc(100);
							cbDwarfTypes += addPointerType(dwarfTypes + cbDwarfTypes, type, pointerAttr | 0x20); // needs to be deduplicted?
							type = nextDwarfType++;
						}
						appendGlobalVar(id.name, type, seg + 1, segOff);
						int rc = mod->AddPublic2(id.name, seg + 1, segOff, type);
					}
				}
				break;
			case DW_TAG_formal_parameter:
			case DW_TAG_unspecified_parameters:
			case DW_TAG_inheritance:
			case DW_TAG_member:
			case DW_TAG_inlined_subroutine:
			case DW_TAG_lexical_block:
			default:
				break;
			}

			if (cvtype >= 0)
			{
				assert(cvtype == typeID); 
				typeID++;

				assert(mapEntryPtrToTypeID[id.entryPtr] == cvtype);
				assert(typeID == nextUserType);
			}
		}
	}

	assert(typeID == nextUserType);
	assert(typeID == firstUserType + mapEntryPtrToTypeID.size());
	return true;
}

void printIndent(int level) {
	for (int i = 0; i < level; ++i) {
		printf("  ");
	}
}

void dumpTreeHelper(DWARF_InfoData* node, int level) {
	for (DWARF_InfoData* n = node; n; n = n->next) {
		const unsigned dieOffset = n->img->debug_info.sectOff(n->entryPtr);

		printIndent(level);
		printf("offset: %#x, name: \"%s\", tag: %#x, abbrev: %d\n", dieOffset, n->name, n->tag, n->code);

		// Visit the children.
		dumpTreeHelper(n->children, level + 1);
	}
}

void CV2PDB::dumpDwarfTree() const {
	dumpTreeHelper(dwarfHead, 0);	
}

bool CV2PDB::createDWARFModules()
{
	if(!imgDbg->debug_info.isPresent())
		return setError("no .debug_info section found");

	codeSegOff = img.getImageBase() + img.getSection(img.text.secNo).VirtualAddress;

	mspdb::Mod* mod = globalMod();
	int s = 0;
	for (; s < img.countSections(); s++)
	{
		const IMAGE_SECTION_HEADER& sec = img.getSection(s);
		int rc = dbi->AddSec(s + 1, 0x10d, 0, sec.Misc.VirtualSize);
		if (rc <= 0)
			return setError("cannot add section");
	}
	if (&img != imgDbg)
	{
		for (int ds = 0; ds < imgDbg->countSections(); ds++)
		{
			const IMAGE_SECTION_HEADER& sec = imgDbg->getSection(ds);

			const char* name = (const char*)sec.Name;
			if (name[0] == '/')
			{
				int off = strtol(name + 1, 0, 10);
				name = imgDbg->getStrTable() + off;
			}

			// Is 'name' one of the debug sections?
			if (!strncmp(name, ".debug_", 7)) {
				int rc = dbi->AddSec(s++, 0x10d, 0, sec.Misc.VirtualSize);
				if (rc <= 0)
					return setError("cannot add section");
			}
		}
	}

#define FULL_CONTRIB 1
#if FULL_CONTRIB
	// we use a single global module, so we can simply add the whole text segment
	int segFlags = 0x60101020; // 0x40401040, 0x60500020; // TODO
	s = img.text.secNo;
	int pclo = 0; // img.getImageBase() + img.getSection(s).VirtualAddress;
	int pchi = pclo + img.getSection(s).Misc.VirtualSize;
	int rc = mod->AddSecContrib(s + 1, pclo, pchi - pclo, segFlags);
	if (rc <= 0)
		return setError("cannot add section contribution to module");
#endif

	checkUserTypeAlloc();
	*(DWORD*) userTypes = 4;
	cbUserTypes = 4;

	createEmptyFieldListType();
	if(Dversion > 0)
	{
		appendComplex(0x50, 0x40, 4, "cfloat");
		appendComplex(0x51, 0x41, 8, "cdouble");
		appendComplex(0x52, 0x42, 12, "creal");
	}

	DIECursor::setContext(imgDbg, debug);

	countEntries = 0;
	if (!mapTypes())
		return false;
	if (!createTypes())
		return false;

	if (debug & DbgPrintDwarfTree) {
		dumpDwarfTree();
	}

	/*
	if(!iterateDWARFDebugInfo(kOpMapTypes))
		return false;
	if(!iterateDWARFDebugInfo(kOpCreateTypes))
		return false;
	*/

#if 0
	modules = new mspdb::Mod* [countEntries];
	memset (modules, 0, countEntries * sizeof(*modules));

	for (int m = 0; m < countEntries; m++)
	{
		mspdb::Mod* mod = globalMod();
	}
#endif

	if(cbUserTypes > 0 || cbDwarfTypes)
	{
		if(dwarfTypes)
		{
			checkUserTypeAlloc(cbDwarfTypes);
			memcpy(userTypes + cbUserTypes, dwarfTypes, cbDwarfTypes);
			cbUserTypes += cbDwarfTypes;
			cbDwarfTypes = 0;
		}
		int rc = mod->AddTypes(userTypes, cbUserTypes);
		if (rc <= 0)
			return setError("cannot add type info to module");
	}
	return true;
}

bool CV2PDB::addDWARFLines()
{
	if(!imgDbg->debug_line.isPresent())
		return setError("no .debug_line section found");

	if (!interpretDWARFLines(*imgDbg, globalMod(), debug))
		return setError("cannot add line number info to module");

	return true;
}

bool CV2PDB::addDWARFPublics()
{
	mspdb::Mod* mod = globalMod();

	int type = 0;
	int rc = mod->AddPublic2("public_all", img.text.secNo + 1, 0, BASE_USER_TYPE);
	if (rc <= 0)
		return setError("cannot add public");
	return true;
}

// Try to lookup a DWARF_InfoData in the constructed DWARF tree given its
// "entryPtr". I.e. its memory-mapped location in the loaded PE image buffer.
DWARF_InfoData* CV2PDB::findEntryByPtr(byte* entryPtr) const
{
	auto it = mapEntryPtrToEntry.find(entryPtr);
	if (it == mapEntryPtrToEntry.end()) {
		// Could not find decl for this definition.
		return nullptr;
	} 
	return it->second;
}

// Try to lookup a TypeID in the set of registered types by a
// "typePtr". I.e. its memory-mapped location in the loaded PE image buffer.
int CV2PDB::findTypeIdByPtr(byte* typePtr) const
{
	auto it = mapEntryPtrToTypeID.find(typePtr);
	if (it == mapEntryPtrToTypeID.end()) {
		// Could not find type for this definition.
		return T_NOTYPE;
	}
	return it->second;
}

bool CV2PDB::writeDWARFImage(const TCHAR* opath)
{
	int len = sizeof(*rsds) + strlen((char*)(rsds + 1)) + 1;
	if (!img.replaceDebugSection(rsds, len, false))
		return setError(img.getLastError());

	if (!img.save(opath))
		return setError(img.getLastError());

	return true;
}

void CV2PDB::build_cfi_index()
{
	if (!imgDbg->debug_frame.isPresent())
		return;
	cfi_index = new CFIIndex(*imgDbg);
}

CFIIndex::CFIIndex(const PEImage& img)
{
	CFIEntry entry;
	CFICursor cursor(img);

	// First register all FDE as index entries
	while (cursor.readNext(entry))
	{
		if (entry.type != CFIEntry::FDE)
			continue;

		index_entry e = {
			entry.initial_location,
			entry.initial_location + entry.address_range,
			entry.ptr
		};
		index.push_back(e);
	}

	// Then make them sorted so we can perform binary searches later on
	std::sort(index.begin(), index.end());
}

byte *CFIIndex::lookup(unsigned int pclo, unsigned int pchi) const
{
	// TODO: here, we are just looking for the first entry whose range contains PCLO,
	// assuming the found entry will have the same PCLO and PCHI as arguments. Maybe
	// this is not always true.
	index_entry e = { pclo, pclo, NULL };
	std::vector<index_entry>::const_iterator it
		= std::lower_bound(index.begin(), index.end(), e);
	if (it == index.end())
		return NULL;
	e = *it;
	if (e.pclo <= pclo && pchi <= e.pchi)
		return e.ptr;
	else
		return NULL;
}
