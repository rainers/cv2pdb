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

#include <assert.h> 
#include <string>
#include <vector>

///////////////////////////////////////////////////////////////////////////////

unsigned int LEB128(unsigned char* &p)
{
	unsigned int x = 0;
	int shift = 0;
	while(*p & 0x80)
	{
		x |= (*p & 0x7f) << shift;
		shift += 7;
		p++;
	}
	x |= *p << shift;
	p++;
	return x;
}

unsigned int LEB128(unsigned char* base, int& off)
{
	unsigned char* p = base + off;
	unsigned int x = LEB128(p);
	off = p - base;
	return x;
}

int SLEB128(unsigned char* &p)
{
	unsigned int x = 0;
	int shift = 0;
	while(*p & 0x80)
	{
		x |= (*p & 0x7f) << shift;
		shift += 7;
		p++;
	}
	x |= *p << shift;
	if(*p & 0x40)
		x |= -(1 << (shift + 7)); // sign extend
	p++;
	return x;
}

unsigned int RD2(unsigned char* p)
{
	unsigned int x = *p++;
	x |= *p++ << 8;
	return x;
}

unsigned int RD4(unsigned char* p)
{
	unsigned int x = *p++;
	x |= *p++ << 8;
	x |= *p++ << 16;
	x |= *p++ << 24;
	return x;
}

unsigned long long RD8(unsigned char* p)
{
	unsigned long long x = *p++;
	for(int shift = 8; shift < 64; shift += 8)
		x |= (unsigned long long) *p++ << shift;
	return x;
}

unsigned long long RDsize(unsigned char* p, int size)
{
	if(size > 8)
		size = 8;
	unsigned long long x = *p++;
	for(int shift = 8; shift < size * 8; shift += 8)
		x |= (unsigned long long) *p++ << shift;
	return x;
}


///////////////////////////////////////////////////////////////////////////////

#include "pshpack1.h"

struct DWARF_CompilationUnit
{
	unsigned int unit_length; // 12 byte in DWARF-64
	unsigned short version;
	unsigned int debug_abbrev_offset; // 8 byte in DWARF-64
	unsigned char address_size;

	bool isDWARF64() const { return unit_length == ~0; }
	int refSize() const { return unit_length == ~0 ? 8 : 4; }
};

struct DWARF_FileName
{
	const char* file_name;
	unsigned int  dir_index;
	unsigned long lastModification;
	unsigned long fileLength;

	void read(unsigned char* &p)
	{
		file_name = (const char*) p;
		p += strlen((const char*) p) + 1;
		dir_index = LEB128(p);
		lastModification = LEB128(p);
		fileLength = LEB128(p);
	}
};

struct DWARF_InfoData
{
	int entryOff;
	int code;
	int tag;
	int hasChild;

	const char* name;
	const char* linkage_name;
	const char* dir;
	unsigned long byte_size;
	unsigned long sibling;
	unsigned long encoding;
	unsigned long pclo;
	unsigned long pchi;
	unsigned long ranges;
	unsigned long type;
	unsigned long containing_type;
	unsigned long specification;
	unsigned long inlined;
	unsigned long external;
	unsigned long long location; // first 8 bytes
	unsigned long long member_location; // first 8 bytes
	unsigned long locationlist; // offset into debug_loc
	long frame_base;
	long upper_bound;
	long lower_bound;
};

static const int maximum_operations_per_instruction = 1;

struct DWARF_LineNumberProgramHeader
{
	unsigned int unit_length; // 12 byte in DWARF-64
	unsigned short version;
	unsigned int header_length; // 8 byte in DWARF-64
	unsigned char minimum_instruction_length;
	//unsigned char maximum_operations_per_instruction; (// not in DWARF 2
	unsigned char default_is_stmt;
	  signed char line_base;
	unsigned char line_range;
	unsigned char opcode_base;
	//LEB128 standard_opcode_lengths[opcode_base]; 
	// string include_directories[] // zero byte terminated
	// DWARF_FileNames file_names[] // zero byte terminated
};

struct DWARF_LineState
{
	// hdr info
	std::vector<const char*> include_dirs;
	std::vector<DWARF_FileName> files;

	unsigned long address;
	unsigned int  op_index;
	unsigned int  file;
	unsigned int  line;
	unsigned int  column;
	bool          is_stmt;
	bool          basic_block;
	bool          end_sequence;
	bool          prologue_end;
	bool          epilogue_end;
	unsigned int  isa;
	unsigned int  discriminator;

	// not part of the "documented" state
	DWARF_FileName* file_ptr;
	unsigned long seg_offset;
	unsigned long last_addr;
	std::vector<mspdb::LineInfoEntry> lineInfo;

	DWARF_LineState()
	{
		seg_offset = 0x400000;
		init(0);
	}

	void init(DWARF_LineNumberProgramHeader* hdr)
	{
		address = 0;
		op_index = 0;
		file = 1;
		line = 1;
		column = 0;
		is_stmt = hdr && hdr->default_is_stmt != 0;
		basic_block = false;
		end_sequence = false;
		prologue_end = false;
		epilogue_end = false;
		isa = 0;
		discriminator = 0;
	}

	void advance_addr(DWARF_LineNumberProgramHeader* hdr, int operation_advance)
	{
		int address_advance = hdr->minimum_instruction_length * ((op_index + operation_advance) / maximum_operations_per_instruction);
		address += address_advance;
		op_index = (op_index + operation_advance) % maximum_operations_per_instruction;
	}

	void addLineInfo()
	{
#if 0
		const char* fname = (file == 0 ? file_ptr->file_name : files[file - 1].file_name);
		printf("Adr:%08x Line: %5d File: %s\n", address, line, fname);
#endif
		if(address < seg_offset)
			return;
		mspdb::LineInfoEntry entry;
		entry.offset = address - seg_offset;
		entry.line = line;
		lineInfo.push_back(entry);
	}
};

#include "poppack.h"

///////////////////////////////////////////////////////////////////////////////

long decodeLocation(unsigned char* loc, bool push0, int &id, int& size)
{
	unsigned char* p = loc;
	long stack[8] = {0};
	int stackDepth = push0 ? 1 : 0;
	long data = 0;
	id = push0 ? S_CONSTANT_V2 : -1;
	do
	{
		int op = *p++;
		if(op == 0)
			break;
		size = 0;

		switch(op)
		{
			case DW_OP_addr:        id = S_GDATA_V2;    size = 4; stack[stackDepth++] = RD4(p); break;
			case DW_OP_fbreg:       id = S_BPREL_V2;              stack[stackDepth++] = SLEB128(p); break;
			case DW_OP_const1u:     id = S_CONSTANT_V2; size = 1; stack[stackDepth++] = *p; break;
			case DW_OP_const2u:     id = S_CONSTANT_V2; size = 2; stack[stackDepth++] = RD2(p); break;
			case DW_OP_const4u:     id = S_CONSTANT_V2; size = 4; stack[stackDepth++] = RD4(p); break;
			case DW_OP_const1s:     id = S_CONSTANT_V2; size = 1; stack[stackDepth++] = (char)*p; break;
			case DW_OP_const2s:     id = S_CONSTANT_V2; size = 2; stack[stackDepth++] = (short)RD2(p); break;
			case DW_OP_const4s:     id = S_CONSTANT_V2; size = 4; stack[stackDepth++] = (int)RD4(p); break;
			case DW_OP_constu:      id = S_CONSTANT_V2;           stack[stackDepth++] = LEB128(p); break;
			case DW_OP_consts:      id = S_CONSTANT_V2;           stack[stackDepth++] = SLEB128(p); break;
			case DW_OP_plus_uconst: stack[stackDepth-1] += LEB128(p); break;
			case DW_OP_lit0:  case DW_OP_lit1:  case DW_OP_lit2:  case DW_OP_lit3:
			case DW_OP_lit4:  case DW_OP_lit5:  case DW_OP_lit6:  case DW_OP_lit7:
			case DW_OP_lit8:  case DW_OP_lit9:  case DW_OP_lit10: case DW_OP_lit11:
			case DW_OP_lit12: case DW_OP_lit13: case DW_OP_lit14: case DW_OP_lit15:
			case DW_OP_lit16: case DW_OP_lit17: case DW_OP_lit18: case DW_OP_lit19:
			case DW_OP_lit20: case DW_OP_lit21: case DW_OP_lit22: case DW_OP_lit23:
			case DW_OP_lit24: case DW_OP_lit25: case DW_OP_lit26: case DW_OP_lit27:
			case DW_OP_lit28: case DW_OP_lit29: case DW_OP_lit30: case DW_OP_lit31:
				id = S_CONSTANT_V2;
				stack[stackDepth++] = op - DW_OP_lit0; 
				break;
			case DW_OP_reg0:  case DW_OP_reg1:  case DW_OP_reg2:  case DW_OP_reg3:
			case DW_OP_reg4:  case DW_OP_reg5:  case DW_OP_reg6:  case DW_OP_reg7:
			case DW_OP_reg8:  case DW_OP_reg9:  case DW_OP_reg10: case DW_OP_reg11:
			case DW_OP_reg12: case DW_OP_reg13: case DW_OP_reg14: case DW_OP_reg15:
			case DW_OP_reg16: case DW_OP_reg17: case DW_OP_reg18: case DW_OP_reg19:
			case DW_OP_reg20: case DW_OP_reg21: case DW_OP_reg22: case DW_OP_reg23:
			case DW_OP_reg24: case DW_OP_reg25: case DW_OP_reg26: case DW_OP_reg27:
			case DW_OP_reg28: case DW_OP_reg29: case DW_OP_reg30: case DW_OP_reg31:
				id = S_REGISTER_V2;
				break;
			case DW_OP_regx:
				id = S_REGISTER_V2;
				data = LEB128(p); // reg
				break;
			case DW_OP_breg0:  case DW_OP_breg1:  case DW_OP_breg2:  case DW_OP_breg3:
			case DW_OP_breg4:  case DW_OP_breg5:  case DW_OP_breg6:  case DW_OP_breg7:
			case DW_OP_breg8:  case DW_OP_breg9:  case DW_OP_breg10: case DW_OP_breg11:
			case DW_OP_breg12: case DW_OP_breg13: case DW_OP_breg14: case DW_OP_breg15:
			case DW_OP_breg16: case DW_OP_breg17: case DW_OP_breg18: case DW_OP_breg19:
			case DW_OP_breg20: case DW_OP_breg21: case DW_OP_breg22: case DW_OP_breg23:
			case DW_OP_breg24: case DW_OP_breg25: case DW_OP_breg26: case DW_OP_breg27:
			case DW_OP_breg28: case DW_OP_breg29: case DW_OP_breg30: case DW_OP_breg31:
				id = S_REGISTER_V2;
				data = SLEB128(p); 
				break;
			case DW_OP_bregx:
				id = S_REGISTER_V2;
				data = LEB128(p); // reg
				data = SLEB128(p); 
				break;

			case DW_OP_deref: break;
			case DW_OP_deref_size: size = 1; break;
			case DW_OP_dup:   stack[stackDepth] = stack[stackDepth-1]; stackDepth++; break;
			case DW_OP_drop:  stackDepth--; break;
			case DW_OP_over:  stack[stackDepth] = stack[stackDepth-2]; stackDepth++; break;
			case DW_OP_pick:  size = 1; stack[stackDepth++] = stack[*p]; break;
			case DW_OP_swap:  data = stack[stackDepth-1]; stack[stackDepth-1] = stack[stackDepth-2]; stack[stackDepth-2] = data; break;
			case DW_OP_rot:   data = stack[stackDepth-1]; stack[stackDepth-1] = stack[stackDepth-2]; stack[stackDepth-2] = stack[stackDepth-3]; stack[stackDepth-3] = data; break;
			case DW_OP_xderef:     stackDepth--; break;
			case DW_OP_xderef_size: size = 1; stackDepth--; break;

			case DW_OP_push_object_address: stackDepth++; break; /* DWARF3 */
			case DW_OP_call2: size = 2; break;
			case DW_OP_call4: size = 4; break;
			case DW_OP_form_tls_address: break;
			case DW_OP_call_frame_cfa:   stackDepth++; break; 
			case DW_OP_call_ref:
			case DW_OP_bit_piece:
			case DW_OP_implicit_value: /* DWARF4 */
			case DW_OP_stack_value:
				//assert(!"unsupported expression operations");
				id = -1;
				return 0;

			// unary operations pop and push
			case DW_OP_abs:   stack[stackDepth-1] = abs(stack[stackDepth-1]); break;
			case DW_OP_neg:   stack[stackDepth-1] = -stack[stackDepth-1]; break;
			case DW_OP_not:   stack[stackDepth-1] = ~stack[stackDepth-1]; break;
				break;
			// biary operations pop twice and push
			case DW_OP_and:   stack[stackDepth-2] = stack[stackDepth-2] &  stack[stackDepth-1]; stackDepth--; break;
			case DW_OP_div:   stack[stackDepth-2] = stack[stackDepth-2] /  stack[stackDepth-1]; stackDepth--; break;
			case DW_OP_minus: stack[stackDepth-2] = stack[stackDepth-2] -  stack[stackDepth-1]; stackDepth--; break;
			case DW_OP_mod:   stack[stackDepth-2] = stack[stackDepth-2] %  stack[stackDepth-1]; stackDepth--; break;
			case DW_OP_mul:   stack[stackDepth-2] = stack[stackDepth-2] *  stack[stackDepth-1]; stackDepth--; break;
			case DW_OP_or:    stack[stackDepth-2] = stack[stackDepth-2] |  stack[stackDepth-1]; stackDepth--; break;
			case DW_OP_plus:  stack[stackDepth-2] = stack[stackDepth-2] +  stack[stackDepth-1]; stackDepth--; break;
			case DW_OP_shl:   stack[stackDepth-2] = stack[stackDepth-2] << stack[stackDepth-1]; stackDepth--; break;
			case DW_OP_shr:   stack[stackDepth-2] = stack[stackDepth-2] >> stack[stackDepth-1]; stackDepth--; break;
			case DW_OP_shra:  stack[stackDepth-2] = stack[stackDepth-2] >> stack[stackDepth-1]; stackDepth--; break;
			case DW_OP_xor:   stack[stackDepth-2] = stack[stackDepth-2] ^  stack[stackDepth-1]; stackDepth--; break;
			case DW_OP_eq:    stack[stackDepth-2] = stack[stackDepth-2] == stack[stackDepth-1]; stackDepth--; break;
			case DW_OP_ge:    stack[stackDepth-2] = stack[stackDepth-2] >= stack[stackDepth-1]; stackDepth--; break;
			case DW_OP_gt:    stack[stackDepth-2] = stack[stackDepth-2] >  stack[stackDepth-1]; stackDepth--; break;
			case DW_OP_le:    stack[stackDepth-2] = stack[stackDepth-2] <= stack[stackDepth-1]; stackDepth--; break;
			case DW_OP_lt:    stack[stackDepth-2] = stack[stackDepth-2] <  stack[stackDepth-1]; stackDepth--; break;
			case DW_OP_ne:    stack[stackDepth-2] = stack[stackDepth-2] != stack[stackDepth-1]; stackDepth--; break;

			case DW_OP_bra:
			case DW_OP_skip:
				size = RD2(p) + 2;
				break;
		}
		p += size;
	}
	while(stackDepth > 0);
	size = p - loc;
	return stack[0];
}


long decodeLocation(unsigned long long loc, bool push0, int &id)
{
	int size;
	return decodeLocation((unsigned char*) &loc, push0, id, size);
}

unsigned char* CV2PDB::getDWARFAbbrev(int off, int findcode)
{
	if(!img.debug_abbrev)
		return 0;

	unsigned char* p = (unsigned char*) img.debug_abbrev;
	while(off < (int)img.debug_abbrev_length)
	{
		int code = LEB128(p, off);
		if(code == findcode)
			return p + off;
		if(code == 0)
			return 0;
		int tag  = LEB128(p, off);
		int hasChild = p[off++];

		// skip attributes
		int attr, form;
		do
		{
			attr = LEB128(p, off);
			form = LEB128(p, off);
		}
		while(attr || form);
	}
	return 0;
}

bool CV2PDB::readDWARFInfoData(DWARF_InfoData& id, DWARF_CompilationUnit* cu, unsigned char* &p, bool mergeInfo)
{
	int entryOff = p - (unsigned char*) cu;
	int code = LEB128(p);
	if(code == 0)
		return false; // end of children list?

	unsigned char* abbrev = getDWARFAbbrev(cu->debug_abbrev_offset, code);
	assert(abbrev);
	if(!abbrev)
		return false;
	int tag  = LEB128(abbrev);
	int hasChild = *abbrev++;

	if(!mergeInfo)
	{
		id.entryOff = entryOff;
		id.code = code;
		id.tag  = tag;
		id.hasChild = hasChild;

		id.name = 0;
		id.linkage_name = 0;
		id.dir  = 0;
		id.byte_size = 0;
		id.sibling = 0;
		id.encoding = 0;
		id.pclo = 0;
		id.pchi = 0;
		id.ranges = 0;
		id.type = 0;
		id.containing_type = 0;
		id.specification = 0;
		id.inlined = 0;
		id.external = 0;
		id.member_location = 0;
		id.location = 0;
		id.locationlist = 0;
		id.frame_base = -1;
		id.upper_bound = 0;
		id.lower_bound = 0;
	}
	int attr, form;
	for( ; ; )
	{
		attr = LEB128(abbrev);
		form = LEB128(abbrev);

		if(attr == 0 && form == 0)
			break;

		const char* str = 0;
		int size = 0;
		unsigned long addr = 0;
		unsigned long data = 0;
		unsigned long long lldata = 0;
		bool isRef = false;
		switch(form)
		{
		case DW_FORM_addr:           addr = *(unsigned long *)p; size = cu->address_size; break;
		case DW_FORM_block2:         size = RD2(p) + 2; break;
		case DW_FORM_block4:         size = RD4(p) + 4; break;
		case DW_FORM_data2:          size = 2; lldata = data = RD2(p); break;
		case DW_FORM_data4:          size = 4; lldata = data = RD4(p); break;
		case DW_FORM_data8:          size = 8; lldata = RD8(p); data = (unsigned long) lldata; break;
		case DW_FORM_string:         str = (const char*) p; size = strlen(str) + 1; break;
		case DW_FORM_block:          size = LEB128(p); lldata = RDsize(p, size); data = (unsigned long) lldata; break;
		case DW_FORM_block1:         size = *p++;      lldata = RDsize(p, size); data = (unsigned long) lldata; break;
		case DW_FORM_data1:          size = 1; lldata = data = *p; break;
		case DW_FORM_flag:           size = 1; lldata = data = *p; break;
		case DW_FORM_sdata:          lldata = data = SLEB128(p); size = 0; break;
		case DW_FORM_strp:           size = cu->refSize(); str = (const char*) (img.debug_str + RDsize(p, size)); break;
		case DW_FORM_udata:          lldata = data = LEB128(p); size = 0; break;
		case DW_FORM_ref_addr:       size = cu->address_size; lldata = RDsize(p, size); data = (unsigned long) lldata; isRef = true; break;
		case DW_FORM_ref1:           size = 1; lldata = data = *p; isRef = true; break;
		case DW_FORM_ref2:           size = 2; lldata = data = RD2(p); isRef = true; break;
		case DW_FORM_ref4:           size = 4; lldata = data = RD4(p); isRef = true; break;
		case DW_FORM_ref8:           size = 8; lldata = RD8(p); data = (unsigned long) lldata; isRef = true; break;
		case DW_FORM_ref_udata:      lldata = data = LEB128(p); size = 0; isRef = true; break;
		case DW_FORM_exprloc:        size = LEB128(p); break;
		case DW_FORM_flag_present:   size = 1; break;
		case DW_FORM_ref_sig8:       size = 8; break;
		case DW_FORM_indirect:
		case DW_FORM_sec_offset:
		default: return setError("unknown DWARF form entry");
		}
		switch(attr)
		{
		case DW_AT_byte_size: id.byte_size = data; break;
		case DW_AT_sibling:   id.sibling = data; break;
		case DW_AT_encoding:  id.encoding = data; break;
		case DW_AT_name:      id.name = str; break;
		case DW_AT_MIPS_linkage_name: id.linkage_name = str; break;
		case DW_AT_comp_dir:  id.dir  = str; break;
		case DW_AT_low_pc:    id.pclo = addr; break;
		case DW_AT_high_pc:   id.pchi = addr; break;
		case DW_AT_ranges:    id.ranges = data; break;
		case DW_AT_type:      id.type = data; break;
		case DW_AT_inline:    id.inlined = data; break;
		case DW_AT_external:  id.external = data; break;
		case DW_AT_upper_bound: id.upper_bound = data; break;
		case DW_AT_lower_bound: id.lower_bound = data; break;
		case DW_AT_containing_type: id.containing_type = data; break;
		case DW_AT_specification: id.specification = data; break;
		case DW_AT_data_member_location: id.member_location = lldata; break;
		case DW_AT_location:  
			if(form == DW_FORM_block1) 
				id.location = lldata; 
			else 
				id.locationlist = data;
			break;
		case DW_AT_frame_base: 
			if(form != DW_FORM_block2 && form != DW_FORM_block4 && form != DW_FORM_block1 && form != DW_FORM_block) 
				id.frame_base = data; 
			break;
		}
		p += size;
	}
	return true;
}

void CV2PDB::checkDWARFTypeAlloc(int size, int add)
{
	if (cbDwarfTypes + size > allocDwarfTypes)
	{
		allocDwarfTypes += size + add;
		dwarfTypes = (BYTE*) realloc(dwarfTypes, allocDwarfTypes);
	}
}

void CV2PDB::appendStackVar(const char* name, int type, int offset)
{
	unsigned int len;
	unsigned int align = 4;

//    if(type > 0x1020)
//        type = 0x74;

	checkUdtSymbolAlloc(100 + kMaxNameLen);

	codeview_symbol*cvs = (codeview_symbol*) (udtSymbols + cbUdtSymbols);
	cvs->stack_v2.offset = offset;
	cvs->stack_v2.symtype = type;
	if(img.isX64())
	{
		cvs->stack_xxxx_v3.id = S_BPREL_XXXX_V3;
		// register as in "Microsoft Symbol and Type Information" 6.1, see also dia2dump/regs.cpp
		cvs->stack_xxxx_v3.unknown = 0x14e; // 0x14f: esp relative, 0x14e: ebp relative
		len = cstrcpy_v (true, (BYTE*) cvs->stack_xxxx_v3.name, name);
		len += (BYTE*) &cvs->stack_xxxx_v3.name - (BYTE*) cvs;
	}
	else
	{
		cvs->stack_v2.id = v3 ? S_BPREL_V3 : S_BPREL_V2;
		len = cstrcpy_v (v3, (BYTE*) &cvs->stack_v2.p_name, name);
		len += (BYTE*) &cvs->stack_v2.p_name - (BYTE*) cvs;
	}
	for (; len & (align-1); len++)
		udtSymbols[cbUdtSymbols + len] = 0xf4 - (len & 3);
	cvs->stack_v2.len = len - 2;
	cbUdtSymbols += len;
}

void CV2PDB::appendGlobalVar(const char* name, int type, int seg, int offset)
{
	unsigned int len;
	unsigned int align = 4;

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
	for (; len & (align-1); len++)
		udtSymbols[cbUdtSymbols + len] = 0xf4 - (len & 3);
	cvs->data_v2.len = len - 2;
	cbUdtSymbols += len;
}

bool CV2PDB::appendEndArg()
{
	checkUdtSymbolAlloc(8);

	codeview_symbol*cvs = (codeview_symbol*) (udtSymbols + cbUdtSymbols);
	cvs->common.id = S_ENDARG_V1;
	cvs->common.len = 2;
	cbUdtSymbols += 4;
	return true;
}

void CV2PDB::appendEnd()
{
	checkUdtSymbolAlloc(8);

	codeview_symbol*cvs = (codeview_symbol*) (udtSymbols + cbUdtSymbols);
	cvs->common.id = S_END_V1;
	cvs->common.len = 2;
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
	dsym->block_v3.segment = img.codeSegment + 1;
	dsym->block_v3.name[0] = 0;
	int len = sizeof(dsym->block_v3);
	for (; len & 3; len++)
		udtSymbols[cbUdtSymbols + len] = 0xf4 - (len & 3);
	dsym->block_v3.len = len - 2;
	cbUdtSymbols += len;
}

bool CV2PDB::addDWARFProc(DWARF_InfoData& procid, DWARF_CompilationUnit* cu, 
						  unsigned char* &locals, unsigned char* end)
{
	unsigned int pclo = procid.pclo - codeSegOff;
	unsigned int pchi = procid.pchi - codeSegOff;

	int length = procid.hasChild ? end - locals : 0;
	unsigned int len;
	unsigned int align = 4;

	checkUdtSymbolAlloc(100 + kMaxNameLen);

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
	cvs->proc_v2.segment  = img.codeSegment + 1;
	cvs->proc_v2.proctype = 0; // translateType(sym->proc_v1.proctype);
	cvs->proc_v2.flags    = 0;

//    printf("GlobalPROC %s\n", procid.name);

	len = cstrcpy_v (v3, (BYTE*) &cvs->proc_v2.p_name, procid.name);
	len += (BYTE*) &cvs->proc_v2.p_name - (BYTE*) cvs;
	for (; len & (align-1); len++)
		udtSymbols[cbUdtSymbols + len] = 0xf4 - (len & 3);
	cvs->proc_v2.len = len - 2;
	cbUdtSymbols += len;

#if 0
	addStackVar("local_var", 0x1001, 8);
#endif

	int frameOff = 8;  // assume ebp+8 in fb
	if(img.debug_loc && procid.frame_base >= 0)
	{
		unsigned char* loc = (unsigned char*) (img.debug_loc + procid.frame_base);
		int frame_breg = cu->address_size == 8 ? DW_OP_breg6 : DW_OP_breg5;
#if 1
		while(RDsize(loc, cu->address_size) != 0 || RDsize(loc + cu->address_size, cu->address_size) != 0)
		{
			loc += 2*cu->address_size;
			int opsize = RD2(loc);
			loc += 2;
			if(*loc == frame_breg)
			{
				unsigned char* p = loc + 1;
				frameOff = SLEB128(p);
				break;
			}
			loc += opsize;
		}
#endif
	}
	if(cu && locals && length)
	{
		bool endarg = false;
		unsigned char* p = locals;
		unsigned char* end = locals + length;
		DWARF_InfoData id;
		int off = 8;
		int cvid;

		std::vector<unsigned char*> lexicalBlockEnd;
		lexicalBlockEnd.push_back(end);
		while(lexicalBlockEnd.size() > 0)
		{
			if(p >= lexicalBlockEnd.back())
			{
				if (!endarg)
					endarg = appendEndArg();
				appendEnd();
				lexicalBlockEnd.pop_back();
				continue;
			}
			if(!readDWARFInfoData(id, cu, p))
				continue;

			if (id.tag == DW_TAG_formal_parameter)
			{
				if(id.name)
				{
					off = decodeLocation(id.location, false, cvid);
					if(cvid == S_BPREL_V2)
						appendStackVar(id.name, getTypeByDWARFOffset(cu, id.type), off + frameOff);
				}
			}
			else
			{
				if(!endarg)
					endarg = appendEndArg();
				switch(id.tag)
				{
					case DW_TAG_variable:
						if(id.name)
						{
							off = decodeLocation(id.location, false, cvid);
							if(cvid == S_BPREL_V2)
								appendStackVar(id.name, getTypeByDWARFOffset(cu, id.type), off + frameOff);
						}
						break;
					case DW_TAG_subprogram:
						if(id.hasChild && !id.inlined)
							p = (id.sibling ? (unsigned char*) cu + id.sibling : end);
						break;
					case DW_TAG_lexical_block:
						if(id.hasChild && id.pchi != id.pclo)
						{
							appendLexicalBlock(id, pclo + codeSegOff);
							if(id.sibling)
								lexicalBlockEnd.push_back((unsigned char*) cu + id.sibling);
							else
								lexicalBlockEnd.push_back(lexicalBlockEnd.back());
						}
						break;
					default:
						break;
				}
			}
		}
	}
	else
	{
		appendEndArg();
		appendEnd();
	}
	return true;
}

int CV2PDB::addDWARFStructure(DWARF_InfoData& structid, DWARF_CompilationUnit* cu, 
							  unsigned char* &locals, unsigned char* end)
{
	bool isunion = structid.tag == DW_TAG_union_type;
	int length = structid.hasChild ? end - locals : 0;

	int fieldlistType = 0;
	int nfields = 0;
	if(cu && locals && length)
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
			bc->bclass_v2.type = getTypeByDWARFOffset(cu, structid.containing_type);
			bc->bclass_v2.attribute = 3; // public
			cbDwarfTypes += sizeof(bc->bclass_v2);
			for (; cbDwarfTypes & 3; cbDwarfTypes++)
				dwarfTypes[cbDwarfTypes] = 0xf4 - (cbDwarfTypes & 3);
			nfields++;
		}
#endif
		unsigned char* p = locals;
		unsigned char* end = locals + length;
		DWARF_InfoData id;
		int len = 0;
		while(p < end)
		{
			if(!readDWARFInfoData(id, cu, p))
				continue;

			int cvid = -1;
			if (id.tag == DW_TAG_member && id.name)
			{
				int off = isunion ? 0 : decodeLocation(id.member_location, true, cvid);
				if(isunion || cvid == S_CONSTANT_V2)
				{
					checkDWARFTypeAlloc(kMaxNameLen + 100);
					codeview_fieldtype* dfieldtype = (codeview_fieldtype*) (dwarfTypes + cbDwarfTypes);
					cbDwarfTypes += addFieldMember(dfieldtype, 0, off, getTypeByDWARFOffset(cu, id.type), id.name);
					nfields++;
				}
			}
			else if(id.tag == DW_TAG_inheritance)
			{
				int off = decodeLocation(id.member_location, true, cvid);
				if(cvid == S_CONSTANT_V2)
				{
					codeview_fieldtype* bc = (codeview_fieldtype*) (dwarfTypes + cbDwarfTypes);
					bc->bclass_v2.id = LF_BCLASS_V2;
					bc->bclass_v2.offset = off;
					bc->bclass_v2.type = getTypeByDWARFOffset(cu, id.type);
					bc->bclass_v2.attribute = 3; // public
					cbDwarfTypes += sizeof(bc->bclass_v2);
					for (; cbDwarfTypes & 3; cbDwarfTypes++)
						dwarfTypes[cbDwarfTypes] = 0xf4 - (cbDwarfTypes & 3);
					nfields++;
				}
			}
			if(id.sibling)
				p = (unsigned char*) cu + id.sibling;
		}
		fl = (codeview_reftype*) (dwarfTypes + flbegin);
		fl->fieldlist.len = cbDwarfTypes - flbegin - 2;
		fieldlistType = nextDwarfType++;
	}

	checkUserTypeAlloc(kMaxNameLen + 100);
	codeview_type* cvt = (codeview_type*) (userTypes + cbUserTypes);

	const char* name = (structid.name ? structid.name : "__noname");
	int attr = fieldlistType ? 0 : kPropIncomplete;
	int len = addAggregate(cvt, false, nfields, fieldlistType, attr, 0, 0, structid.byte_size, name);
	cbUserTypes += len;

	//ensureUDT()?
	int cvtype = nextUserType++;
	addUdtSymbol(cvtype, name);
	return cvtype;
}

int CV2PDB::getDWARFArrayBounds(DWARF_InfoData& arrayid, DWARF_CompilationUnit* cu, 
								unsigned char* &locals, unsigned char* end, int& upperBound)
{
	int length = arrayid.hasChild ? end - locals : 0;
	int lowerBound = 0;

	if(cu && locals && length)
	{
		unsigned char* p = locals;
		unsigned char* end = locals + length;
		DWARF_InfoData id;
		int len = 0;
		while(p < end)
		{
			if(!readDWARFInfoData(id, cu, p))
				continue;

			int cvid = -1;
			if (id.tag == DW_TAG_subrange_type)
			{
				lowerBound = id.lower_bound;
				upperBound = id.upper_bound;
			}
			if(id.sibling)
				p = (unsigned char*) cu + id.sibling;
		}
	}
	return lowerBound;
}

int CV2PDB::addDWARFArray(DWARF_InfoData& arrayid, DWARF_CompilationUnit* cu, 
						  unsigned char* &locals, unsigned char* end)
{
	int upperBound, lowerBound = getDWARFArrayBounds(arrayid, cu, locals, end, upperBound);
	
	checkUserTypeAlloc(kMaxNameLen + 100);
	codeview_type* cvt = (codeview_type*) (userTypes + cbUserTypes);

	cvt->array_v2.id = v3 ? LF_ARRAY_V3 : LF_ARRAY_V2;
	cvt->array_v2.elemtype = getTypeByDWARFOffset(cu, arrayid.type);
	cvt->array_v2.idxtype = 0x74;
	int len = (BYTE*)&cvt->array_v2.arrlen - (BYTE*)cvt;
	int size = (upperBound - lowerBound + 1) * getDWARFTypeSize(cu, arrayid.type);
	len += write_numeric_leaf(size, &cvt->array_v2.arrlen);
	((BYTE*)cvt)[len++] = 0; // empty name
	for (; len & 3; len++)
		userTypes[cbUserTypes + len] = 0xf4 - (len & 3);
	cvt->array_v2.len = len - 2;

	cbUserTypes += len;

	int cvtype = nextUserType++;
	return cvtype;
}

bool CV2PDB::addDWARFTypes()
{
	checkUdtSymbolAlloc(100);

	int prefix = 4;
	DWORD* ddata = new DWORD [img.debug_info_length/4]; // large enough
	unsigned char *data = (unsigned char*) (ddata + prefix);
	unsigned int off = 0;
	unsigned int len;
	unsigned int align = 4;

	// SSEARCH
	codeview_symbol* cvs = (codeview_symbol*) (data + off);
	cvs->ssearch_v1.id = S_SSEARCH_V1;
	cvs->ssearch_v1.segment = img.codeSegment + 1;
	cvs->ssearch_v1.offset = 0;
	len = sizeof(cvs->ssearch_v1);
	for (; len & (align-1); len++)
		data[off + len] = 0xf4 - (len & 3);
	cvs->ssearch_v1.len = len - 2;
	off += len;

	// COMPILAND
	cvs = (codeview_symbol*) (data + off);
	cvs->compiland_v1.id = S_COMPILAND_V1;
	cvs->compiland_v1.unknown = 0x800100; // ?, 0x100: C++, 
	cvs->compiland_v1.unknown |= img.isX64() ? 0xd0 : 6; //0x06: Pentium Pro/II, 0xd0: x64
	len = sizeof(cvs->compiland_v1) - sizeof(cvs->compiland_v1.p_name);
	len += c2p("cv2pdb", cvs->compiland_v1.p_name);
	for (; len & (align-1); len++)
		data[off + len] = 0xf4 - (len & 3);
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
	return addSymbols (mod, data, off, true);
}

bool CV2PDB::addDWARFSectionContrib(mspdb::Mod* mod, unsigned long pclo, unsigned long pchi)
{
	int segIndex = img.findSection(pclo);
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
	int type = 0, mode = 0, size = 0;
	switch(encoding)
	{
	case DW_ATE_boolean:        type = 3; break;
	case DW_ATE_complex_float:  type = 5; byte_size /= 2; break;
	case DW_ATE_float:          type = 4; break;
	case DW_ATE_signed:         type = 1; break;
	case DW_ATE_signed_char:    type = 7; break;
	case DW_ATE_unsigned:       type = 2; break;
	case DW_ATE_unsigned_char:  type = 7; break;
	case DW_ATE_imaginary_float:type = 4; break;
	default:
		setError("unknown basic type encoding");
	}
	switch(type)
	{
	case 1: // signed
	case 2: // unsigned
	case 3: // boolean
		switch(byte_size)
		{
		case 1: size = 0; break;
		case 2: size = 1; break;
		case 4: size = 2; break;
		case 8: size = 3; break;
		default:
			setError("unsupported integer type size");
		}
		break;
	case 4:
	case 5:
		switch(byte_size)
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
		switch(byte_size)
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
	t = translateType(t);
	int cvtype = appendTypedef(t, name, false);
	if(useTypedefEnum)
		addUdtSymbol(cvtype, name);
	return cvtype;
}

int CV2PDB::getTypeByDWARFOffset(DWARF_CompilationUnit* cu, int off)
{
	int cuoff = (char*) cu - img.debug_info;
	std::map<int,int>::iterator it = mapOffsetToType.find(cuoff + off);
	if(it == mapOffsetToType.end())
		return 0x03; // void
	return it->second;
}

int CV2PDB::getDWARFTypeSize(DWARF_CompilationUnit* cu, int typeOff)
{
	DWARF_InfoData id;
	unsigned char* p = (unsigned char*) cu + typeOff;
	if(!readDWARFInfoData(id, cu, p))
		return 0;
	if(id.byte_size > 0)
		return id.byte_size;

	switch(id.tag)
	{
		case DW_TAG_ptr_to_member_type:
		case DW_TAG_reference_type:
		case DW_TAG_pointer_type:
			return cu->address_size;
		case DW_TAG_array_type:
		{
			int upperBound, lowerBound = getDWARFArrayBounds(id, cu, p, p + 1, upperBound);
			return (upperBound + lowerBound + 1) * getDWARFTypeSize(cu, id.type);
		}
		default:
			if(id.type)
				return getDWARFTypeSize(cu, id.type);
			break;
	}
	return 0;
}

enum iterateOp { kOpMapTypes, kOpCreateTypes };

bool CV2PDB::iterateDWARFDebugInfo(int op)
{
	mspdb::Mod* mod = globalMod();
	int typeID = nextUserType;

	int pointerAttr = img.isX64() ? 0x1000C : 0x800A;
	for(unsigned long off = 0; off < img.debug_info_length; )
	{
		DWARF_CompilationUnit* cu = (DWARF_CompilationUnit*) (img.debug_info + off);
		int length = cu->unit_length;
		if(length < 0)
			break;

		length += sizeof(length);
		unsigned char* end = (unsigned char*) cu + length;
		std::vector<unsigned char*> endStack;
		endStack.push_back(end);

		for(unsigned char* p = (unsigned char*) (cu + 1); endStack.size() > 0; )
		{
			if(p >= endStack.back())
			{
				endStack.pop_back();
				continue;
			}
			DWARF_InfoData id;
			if(!readDWARFInfoData(id, cu, p))
				continue;
			if(id.specification)
			{
				unsigned char* q = (unsigned char*) cu + id.specification;
				readDWARFInfoData(id, cu, q, true);
			}
			if(id.hasChild)
				if(id.sibling)
					endStack.push_back((unsigned char*) cu + id.sibling);
				else
					endStack.push_back(endStack.back());

			if(op == kOpMapTypes)
			{
				switch(id.tag)
				{
				case DW_TAG_base_type:
				case DW_TAG_typedef:
				case DW_TAG_pointer_type:
				case DW_TAG_subroutine_type:
				case DW_TAG_array_type:
				case DW_TAG_const_type:
				case DW_TAG_structure_type:
				case DW_TAG_reference_type:

				case DW_TAG_class_type:
				case DW_TAG_enumeration_type:
				case DW_TAG_string_type:
				case DW_TAG_union_type:
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
					mapOffsetToType.insert(std::pair<int,int>(off + id.entryOff, typeID));
					typeID++;
				}
			}
			else
			{
				int cvtype = -1;
				switch(id.tag)
				{
				case DW_TAG_base_type:
					cvtype = addDWARFBasicType(id.name, id.encoding, id.byte_size);
					break;
				case DW_TAG_typedef:
					cvtype = appendModifierType(getTypeByDWARFOffset(cu, id.type), 0);
					addUdtSymbol(cvtype, id.name);
					break;
				case DW_TAG_pointer_type:
					cvtype = appendPointerType(getTypeByDWARFOffset(cu, id.type), pointerAttr);
					break;
				case DW_TAG_const_type:
					cvtype = appendModifierType(getTypeByDWARFOffset(cu, id.type), 1);
					break;
				case DW_TAG_reference_type:
					cvtype = appendPointerType(getTypeByDWARFOffset(cu, id.type), pointerAttr | 0x20);
					break;

				case DW_TAG_class_type:
				case DW_TAG_structure_type:
				case DW_TAG_union_type:
					cvtype = addDWARFStructure(id, cu, p, endStack.back());
					break;
				case DW_TAG_array_type:
					cvtype = addDWARFArray(id, cu, p, endStack.back());
					break;
				case DW_TAG_subroutine_type:
				case DW_TAG_subrange_type:

				case DW_TAG_enumeration_type:
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
					if(id.name && id.pclo && id.pchi)
					{
						addDWARFProc(id, cu, p, endStack.back());
						int rc = mod->AddPublic2(id.name, img.codeSegment + 1, id.pclo - codeSegOff, 0);
					}
					break;

				case DW_TAG_compile_unit:
	#if !FULL_CONTRIB
					if(id.dir && id.name)
					{
						if(id.ranges > 0 && id.ranges < img.debug_ranges_length)
						{
							unsigned char* r    = (unsigned char*)img.debug_ranges + id.ranges;
							unsigned char* rend = (unsigned char*)img.debug_ranges + img.debug_ranges_length;
							while(r < rend)
							{
								unsigned long pclo = RD4(r); r += 4;
								unsigned long pchi = RD4(r); r += 4;
								if(pclo == 0 && pchi == 0)
									break;
								//printf("%s %s %x - %x\n", dir, name, pclo, pchi);
								if(!addDWARFSectionContrib(mod, pclo, pchi))
									return false;
							}
						}
						else
						{
							//printf("%s %s %x - %x\n", dir, name, pclo, pchi);
							if(!addDWARFSectionContrib(mod, id.pclo, id.pchi))
								return false;
						}
					}
	#endif
					break;

				case DW_TAG_variable:
					if(id.name)
					{
						int seg = -1;
						unsigned long segOff;
						if(id.location == 0 && id.external && id.linkage_name)
						{
							seg = img.findSymbol(id.linkage_name, segOff);
						}
						else
						{
							int cvid;
							segOff = decodeLocation(id.location, false, cvid);
							if(cvid == S_GDATA_V2)
								seg = img.findSection(segOff);
							if(seg >= 0)
								segOff -= img.getImageBase() + img.getSection(seg).VirtualAddress;
						}
						if(seg >= 0)
						{
							int type = getTypeByDWARFOffset(cu, id.type);
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
				if(cvtype >= 0)
				{
					assert(cvtype == typeID); typeID++;
					assert(mapOffsetToType[off + id.entryOff] == cvtype);
				}
			}
		}
		off += length;
	}
	
	if(op == kOpMapTypes)
		nextDwarfType = typeID;

	return true;
}

bool CV2PDB::createDWARFModules()
{
	if(!img.debug_info)
		return setError("no .debug_info section found");

	codeSegOff = img.getImageBase() + img.getSection(img.codeSegment).VirtualAddress;

	mspdb::Mod* mod = globalMod();
	for (int s = 0; s < img.countSections(); s++)
	{
		const IMAGE_SECTION_HEADER& sec = img.getSection(s);
		int rc = dbi->AddSec(s + 1, 0x10d, 0, sec.SizeOfRawData);
		if (rc <= 0)
			return setError("cannot add section");
	}

#define FULL_CONTRIB 1
#if FULL_CONTRIB
	// we use a single global module, so we can simply add the whole text segment
	int segFlags = 0x60101020; // 0x40401040, 0x60500020; // TODO
	int s = img.codeSegment;
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

	countEntries = 0;
	if(!iterateDWARFDebugInfo(kOpMapTypes))
		return false;
	if(!iterateDWARFDebugInfo(kOpCreateTypes))
		return false;

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

bool isRelativePath(const std::string& s)
{
	if(s.length() < 1)
		return true;
	if(s[0] == '/' || s[0] == '\\')
		return false;
	if(s.length() < 2)
		return true;
	if(s[1] == ':')
		return false;
	return true;
}

static int cmpAdr(const void* s1, const void* s2)
{
	const mspdb::LineInfoEntry* e1 = (const mspdb::LineInfoEntry*) s1;
	const mspdb::LineInfoEntry* e2 = (const mspdb::LineInfoEntry*) s2;
	return e1->offset - e2->offset;
}

bool _flushDWARFLines(CV2PDB* cv2pdb, DWARF_LineState& state)
{
	if(state.lineInfo.size() == 0)
		return true;

	unsigned int saddr = state.lineInfo[0].offset;
	unsigned int eaddr = state.lineInfo.back().offset;
	int segIndex = cv2pdb->img.findSection(saddr + state.seg_offset);
	if(segIndex < 0)
	{
		// throw away invalid lines (mostly due to "set address to 0")
		state.lineInfo.resize(0);
		return true;
		//return false;
	}

//    if(saddr >= 0x4000)
//        return true;

	const DWARF_FileName* dfn;
	if(state.file == 0)
		dfn = state.file_ptr;
	else if(state.file > 0 && state.file <= state.files.size())
		dfn = &state.files[state.file - 1];
	else
		return false;
	std::string fname = dfn->file_name;
	
	if(isRelativePath(fname) && 
	   dfn->dir_index > 0 && dfn->dir_index <= state.include_dirs.size())
	{
		std::string dir = state.include_dirs[dfn->dir_index - 1];
		if(dir.length() > 0 && dir[dir.length() - 1] != '/' && dir[dir.length() - 1] != '\\')
			dir.append("\\");
		fname = dir + fname;
	}
	for(size_t i = 0; i < fname.length(); i++)
		if(fname[i] == '/')
			fname[i] = '\\';

	mspdb::Mod* mod = cv2pdb->globalMod();
#if 1
	bool dump = false; // (fname == "cvtest.d");
	//qsort(&state.lineInfo[0], state.lineInfo.size(), sizeof(state.lineInfo[0]), cmpAdr);
#if 0
	printf("%s:\n", fname.c_str());
	for(size_t ln = 0; ln < state.lineInfo.size(); ln++)
		printf("  %08x: %4d\n", state.lineInfo[ln].offset + 0x401000, state.lineInfo[ln].line);
#endif

	unsigned int firstLine = state.lineInfo[0].line;
	unsigned int firstAddr = state.lineInfo[0].offset;
	unsigned int firstEntry = 0;
	unsigned int entry = 0;
	for(size_t ln = firstEntry; ln < state.lineInfo.size(); ln++)
	{
		if(state.lineInfo[ln].line < firstLine || state.lineInfo[ln].offset < firstAddr)
		{
			if(ln > firstEntry)
			{
				unsigned int length = state.lineInfo[entry-1].offset + 1; // firstAddr has been subtracted before
				if(dump)
					printf("AddLines(%08x+%04x, Line=%4d+%3d, %s)\n", firstAddr, length, firstLine, entry - firstEntry, fname.c_str());
				int rc = mod->AddLines(fname.c_str(), segIndex + 1, firstAddr, length, firstAddr, firstLine,
									   (unsigned char*) &state.lineInfo[firstEntry], 
									   (ln - firstEntry) * sizeof(state.lineInfo[0]));
				firstLine = state.lineInfo[ln].line;
				firstAddr = state.lineInfo[ln].offset;
				firstEntry = entry;
			}
		}
		else if(ln > firstEntry && state.lineInfo[ln].offset == state.lineInfo[ln-1].offset)
			continue; // skip entries without offset change
		state.lineInfo[entry].line = state.lineInfo[ln].line - firstLine;
		state.lineInfo[entry].offset = state.lineInfo[ln].offset - firstAddr;
		entry++;
	}
	unsigned int length = eaddr - firstAddr;
	if(dump)
		printf("AddLines(%08x+%04x, Line=%4d+%3d, %s)\n", firstAddr, length, firstLine, entry - firstEntry, fname.c_str());
	int rc = mod->AddLines(fname.c_str(), segIndex + 1, firstAddr, length, firstAddr, firstLine,
						   (unsigned char*) &state.lineInfo[firstEntry], 
						   (entry - firstEntry) * sizeof(state.lineInfo[0]));
#else
	unsigned int firstLine = 0;
	unsigned int firstAddr = 0;
	int rc = mod->AddLines(fname.c_str(), segIndex + 1, saddr, eaddr - saddr, firstAddr, firstLine,
						   (unsigned char*) &state.lineInfo[0], state.lineInfo.size() * sizeof(state.lineInfo[0]));
#endif

	state.lineInfo.resize(0);
	return rc > 0;
}

bool CV2PDB::addDWARFLines()
{
	if(!img.debug_line)
		return setError("no .debug_line section found");

	mspdb::Mod* mod = globalMod();
	for(unsigned long off = 0; off < img.debug_line_length; )
	{
		DWARF_LineNumberProgramHeader* hdr = (DWARF_LineNumberProgramHeader*) (img.debug_line + off);
		int length = hdr->unit_length;
		if(length < 0)
			break;
		length += sizeof(length);

		unsigned char* p = (unsigned char*) (hdr + 1);
		unsigned char* end = (unsigned char*) hdr + length;

		std::vector<unsigned int> opcode_lengths;
		opcode_lengths.resize(hdr->opcode_base);
		opcode_lengths[0] = 0;
		for(int o = 1; o < hdr->opcode_base && p < end; o++)
			opcode_lengths[o] = LEB128(p);

		DWARF_LineState state;
		state.seg_offset = img.getImageBase() + img.getSection(img.codeSegment).VirtualAddress;

		// dirs
		while(p < end)
		{
			if(*p == 0)
				break;
			state.include_dirs.push_back((const char*) p);
			p += strlen((const char*) p) + 1;
		}
		p++;

		// files
		DWARF_FileName fname;
		while(p < end && *p)
		{
			fname.read(p);
			state.files.push_back(fname);
		}
		p++;

		std::vector<mspdb::LineInfoEntry> lineInfo;

		state.init(hdr);
		while(p < end)
		{
			int opcode = *p++;
			if(opcode >= hdr->opcode_base)
			{
				// special opcode
				int adjusted_opcode = opcode - hdr->opcode_base;
				int operation_advance = adjusted_opcode / hdr->line_range;
				state.advance_addr(hdr, operation_advance);
				int line_advance = hdr->line_base + (adjusted_opcode % hdr->line_range);
				state.line += line_advance;

				state.addLineInfo();

				state.basic_block = false;
				state.prologue_end = false;
				state.epilogue_end = false;
				state.discriminator = 0;
			}
			else
			{
				switch(opcode)
				{
				case 0: // extended
					{
						int exlength = LEB128(p);
						unsigned char* q = p + exlength;
						int excode = *p++;
						switch(excode)
						{
						case DW_LNE_end_sequence:
							if((char*)p - img.debug_line >= 0xe4e0)
								p = p;
							state.end_sequence = true;
							state.last_addr = state.address;
							state.addLineInfo();
							if(!_flushDWARFLines(this, state))
								return setError("cannot add line number info to module");
							state.init(hdr);
							break;
						case DW_LNE_set_address:
							if(unsigned long adr = RD4(p))
								state.address = adr;
							else
								state.address = state.last_addr; // strange adr 0 for templates?
							state.op_index = 0;
							break;
						case DW_LNE_define_file:
							fname.read(p);
							state.file_ptr = &fname;
							state.file = 0;
							break;
						case DW_LNE_set_discriminator:
							state.discriminator = LEB128(p);
							break;
						}
						p = q;
					}
					break;
				case DW_LNS_copy:
					state.addLineInfo();
					state.basic_block = false;
					state.prologue_end = false;
					state.epilogue_end = false;
					state.discriminator = 0;
					break;
				case DW_LNS_advance_pc:
					state.advance_addr(hdr, LEB128(p));
					break;
				case DW_LNS_advance_line:
					state.line += SLEB128(p);
					break;
				case DW_LNS_set_file:
					if(!_flushDWARFLines(this, state))
						return setError("cannot add line number info to module");
					state.file = LEB128(p);
					break;
				case DW_LNS_set_column:
					state.column = LEB128(p);
					break;
				case DW_LNS_negate_stmt:
					state.is_stmt = !state.is_stmt;
					break;
				case DW_LNS_set_basic_block:
					state.basic_block = true;
					break;
				case DW_LNS_const_add_pc:
					state.advance_addr(hdr, (255 - hdr->opcode_base) / hdr->line_range);
					break;
				case DW_LNS_fixed_advance_pc:
					state.address += RD2(p);
					state.op_index = 0;
					break;
				case DW_LNS_set_prologue_end:
					state.prologue_end = true;
					break;
				case DW_LNS_set_epilogue_begin:
					state.epilogue_end = true;
					break;
				case DW_LNS_set_isa:
					state.isa = LEB128(p);
					break;
				default:
					// unknown standard opcode
					for(unsigned int arg = 0; arg < opcode_lengths[opcode]; arg++)
						LEB128(p);
					break;
				}
			}
		}
		if(!_flushDWARFLines(this, state))
			return setError("cannot add line number info to module");

		off += length;
	}

	return true;
}

bool CV2PDB::relocateDebugLineInfo()
{
	if(!img.reloc || !img.reloc_length)
		return true;

	unsigned int img_base = 0x400000;
	char* relocbase = img.reloc;
	char* relocend = img.reloc + img.reloc_length;
	while(relocbase < relocend)
	{
		unsigned int virtadr = *(unsigned int *) relocbase;
		unsigned int chksize = *(unsigned int *) (relocbase + 4);

		char* p = img.RVA<char> (virtadr, 1);
		if(p >= img.debug_line && p < img.debug_line + img.debug_line_length)
		{
			for (unsigned int p = 8; p < chksize; p += 2)
			{
				unsigned short entry = *(unsigned short*)(relocbase + p);
				unsigned short type = (entry >> 12) & 0xf;
				unsigned short off = entry & 0xfff;

				if(type == 3) // HIGHLOW
				{
					*(long*) (p + off) += img_base;
				}
			}
		}
		if(chksize == 0 || chksize >= img.reloc_length)
			break;
		relocbase += chksize;
	}
	return true;
}

bool CV2PDB::addDWARFPublics()
{
	mspdb::Mod* mod = globalMod();

	int type = 0;
	int rc = mod->AddPublic2("public_all", img.codeSegment + 1, 0, 0x1000);
	if (rc <= 0)
		return setError("cannot add public");
	return true;
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
