#include "readDwarf.h"
#include <assert.h>
#include <unordered_map>
#include <array>
#include <windows.h>

#include "PEImage.h"
#include "dwarf.h"
#include "mspdb.h"
extern "C" {
	#include "mscvpdb.h"
}

long decodeLocation(unsigned char* loc, long len, bool push0, int &id, int& size)
{
	unsigned char* p = loc;
	long stack[8] = { 0 };
	int stackDepth = push0 ? 1 : 0;
	long data = 0;
	id = push0 ? S_CONSTANT_V2 : -1;
	do
	{
		if (p - loc >= len)
			break;

		int op = *p++;
		if (op == 0)
			break;
		size = 0;

		switch (op)
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
		case DW_OP_plus_uconst: stack[stackDepth - 1] += LEB128(p); break;
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
			id = (op == DW_OP_breg4 ? S_BPREL_XXXX_V3 : op == DW_OP_breg5 ? S_BPREL_V2 : S_REGISTER_V2);
			stack[stackDepth++] = SLEB128(p);
			break;
		case DW_OP_bregx:
			data = LEB128(p); // reg
			id = (data == DW_OP_breg4 ? S_BPREL_XXXX_V3 : data == DW_OP_breg5 ? S_BPREL_V2 : S_REGISTER_V2);
			stack[stackDepth++] = SLEB128(p);
			break;

		case DW_OP_deref: break;
		case DW_OP_deref_size: size = 1; break;
		case DW_OP_dup:   stack[stackDepth] = stack[stackDepth - 1]; stackDepth++; break;
		case DW_OP_drop:  stackDepth--; break;
		case DW_OP_over:  stack[stackDepth] = stack[stackDepth - 2]; stackDepth++; break;
		case DW_OP_pick:  size = 1; stack[stackDepth++] = stack[*p]; break;
		case DW_OP_swap:  data = stack[stackDepth - 1]; stack[stackDepth - 1] = stack[stackDepth - 2]; stack[stackDepth - 2] = data; break;
		case DW_OP_rot:   data = stack[stackDepth - 1]; stack[stackDepth - 1] = stack[stackDepth - 2]; stack[stackDepth - 2] = stack[stackDepth - 3]; stack[stackDepth - 3] = data; break;
		case DW_OP_xderef:     stackDepth--; break;
		case DW_OP_xderef_size: size = 1; stackDepth--; break;

		case DW_OP_push_object_address: stackDepth++; break; /* DWARF3 */
		case DW_OP_call2: size = 2; break;
		case DW_OP_call4: size = 4; break;
		case DW_OP_form_tls_address: break;
		case DW_OP_call_frame_cfa:   stack[stackDepth++] = -1; break; // default stack offset?
		case DW_OP_call_ref:
		case DW_OP_bit_piece:
		case DW_OP_implicit_value: /* DWARF4 */
		case DW_OP_stack_value:
			//assert(!"unsupported expression operations");
			id = -1;
			return 0;

			// unary operations pop and push
		case DW_OP_abs:   stack[stackDepth - 1] = abs(stack[stackDepth - 1]); break;
		case DW_OP_neg:   stack[stackDepth - 1] = -stack[stackDepth - 1]; break;
		case DW_OP_not:   stack[stackDepth - 1] = ~stack[stackDepth - 1]; break;
			break;
			// binary operations pop twice and push
		case DW_OP_and:   stack[stackDepth - 2] = stack[stackDepth - 2] & stack[stackDepth - 1]; stackDepth--; break;
		case DW_OP_div:   stack[stackDepth - 2] = stack[stackDepth - 2] / stack[stackDepth - 1]; stackDepth--; break;
		case DW_OP_minus: stack[stackDepth - 2] = stack[stackDepth - 2] - stack[stackDepth - 1]; stackDepth--; break;
		case DW_OP_mod:   stack[stackDepth - 2] = stack[stackDepth - 2] % stack[stackDepth - 1]; stackDepth--; break;
		case DW_OP_mul:   stack[stackDepth - 2] = stack[stackDepth - 2] * stack[stackDepth - 1]; stackDepth--; break;
		case DW_OP_or:    stack[stackDepth - 2] = stack[stackDepth - 2] | stack[stackDepth - 1]; stackDepth--; break;
		case DW_OP_plus:  stack[stackDepth - 2] = stack[stackDepth - 2] + stack[stackDepth - 1]; stackDepth--; break;
		case DW_OP_shl:   stack[stackDepth - 2] = stack[stackDepth - 2] << stack[stackDepth - 1]; stackDepth--; break;
		case DW_OP_shr:   stack[stackDepth - 2] = stack[stackDepth - 2] >> stack[stackDepth - 1]; stackDepth--; break;
		case DW_OP_shra:  stack[stackDepth - 2] = stack[stackDepth - 2] >> stack[stackDepth - 1]; stackDepth--; break;
		case DW_OP_xor:   stack[stackDepth - 2] = stack[stackDepth - 2] ^ stack[stackDepth - 1]; stackDepth--; break;
		case DW_OP_eq:    stack[stackDepth - 2] = stack[stackDepth - 2] == stack[stackDepth - 1]; stackDepth--; break;
		case DW_OP_ge:    stack[stackDepth - 2] = stack[stackDepth - 2] >= stack[stackDepth - 1]; stackDepth--; break;
		case DW_OP_gt:    stack[stackDepth - 2] = stack[stackDepth - 2] >  stack[stackDepth - 1]; stackDepth--; break;
		case DW_OP_le:    stack[stackDepth - 2] = stack[stackDepth - 2] <= stack[stackDepth - 1]; stackDepth--; break;
		case DW_OP_lt:    stack[stackDepth - 2] = stack[stackDepth - 2] <  stack[stackDepth - 1]; stackDepth--; break;
		case DW_OP_ne:    stack[stackDepth - 2] = stack[stackDepth - 2] != stack[stackDepth - 1]; stackDepth--; break;

		case DW_OP_bra:
		case DW_OP_skip:
			size = RD2(p) + 2;
			break;
		}
		p += size;
	} while (stackDepth > 0);
	size = p - loc;
	return stack[0];
}


// declare hasher for pair<T1,T2>
namespace std
{
	template<typename T1, typename T2>
	struct hash<std::pair<T1, T2>>
	{
		size_t operator()(const std::pair<T1, T2>& t)
		{
			return std::hash<T1>()(t.first) ^ std::hash<T2>()(t.second);
		}
	};
}

typedef std::unordered_map<std::pair<unsigned, unsigned>, unsigned char*> abbrevMap_t;

static PEImage* img;
static abbrevMap_t abbrevMap;

void DIECursor::setContext(PEImage* img_)
{
	img = img_;
	abbrevMap.clear();
}


DIECursor::DIECursor(DWARF_CompilationUnit* cu_, unsigned long offset)
{
	cu = cu_;
	ptr = (unsigned char*)cu + offset;
	level = 0;
	hasChild = false;
	sibling = 0;
}


bool DIECursor::readSibling(DWARF_InfoData& id)
{
	if (sibling)
	{
		// use sibling pointer, if available
		ptr = (unsigned char*)cu + sibling;
		hasChild = false;
	} 
	else if (hasChild)
	{
		int currLevel = level;
		level = currLevel + 1;
		hasChild = false;

		DWARF_InfoData dummy;
		// read untill we pop back to the level we were at
		while (level > currLevel)
			readNext(dummy);
	}

	return readNext(id, true);
}

DIECursor DIECursor::getSubtreeCursor()
{
	if (hasChild)
	{
		DIECursor subtree = *this;
		subtree.level = 0;
		subtree.hasChild = false;
		return subtree;
	}
	else // Return invalid cursor
	{
		DIECursor subtree = *this;
		subtree.level = -1;
		return subtree;
	}
}

bool DIECursor::readNext(DWARF_InfoData& id, bool stopAtNull)
{
	id.clear();

	if (hasChild)
		++level;

	for (;;)
	{
		if (level == -1)
			return false; // we were already at the end of the subtree

		if (ptr >= ((unsigned char*)cu + sizeof(cu->unit_length) + cu->unit_length))
			return false; // root of the tree does not have a null terminator, but we know the length

		id.entryOff = ptr - (unsigned char*)cu;
		id.code = LEB128(ptr);
		if (id.code == 0)
		{
			--level; // pop up one level
			if (stopAtNull)
			{
				hasChild = false;
				return false;
			}
			continue; // read the next DIE
		}

		break;
	}

	unsigned char* abbrev = getDWARFAbbrev(cu->debug_abbrev_offset, id.code);
	assert(abbrev);
	if (!abbrev)
		return false;

	id.abbrev = abbrev;
	id.tag = LEB128(abbrev);
	id.hasChild = *abbrev++;

	int attr, form;
	for (;;)
	{
		attr = LEB128(abbrev);
		form = LEB128(abbrev);

		if (attr == 0 && form == 0)
			break;

		const char* str = 0;
		int size = 0;
		unsigned long addr = 0;
		unsigned long data = 0;
		unsigned long long lldata = 0;
		int locid;
		bool isRef = false;
		switch (form)
		{
		case DW_FORM_addr:           addr = *(unsigned long *)ptr; size = cu->address_size; break;
		case DW_FORM_block2:         size = RD2(ptr) + 2; break;
		case DW_FORM_block4:         size = RD4(ptr) + 4; break;
		case DW_FORM_data2:          size = 2; lldata = data = RD2(ptr); break;
		case DW_FORM_data4:          size = 4; lldata = data = RD4(ptr); break;
		case DW_FORM_data8:          size = 8; lldata = RD8(ptr); data = (unsigned long)lldata; break;
		case DW_FORM_string:         str = (const char*)ptr; size = strlen(str) + 1; break;
		case DW_FORM_block:          size = LEB128(ptr); lldata = RDsize(ptr, size); data = (unsigned long)lldata; break;
		case DW_FORM_block1:         size = *ptr++;      lldata = RDsize(ptr, size); data = (unsigned long)lldata; break;
		case DW_FORM_data1:          size = 1; lldata = data = *ptr; break;
		case DW_FORM_flag:           size = 1; lldata = data = *ptr; break;
		case DW_FORM_sdata:          lldata = data = SLEB128(ptr); size = 0; break;
		case DW_FORM_strp:           size = cu->refSize(); str = (const char*)(img->debug_str + RDsize(ptr, size)); break;
		case DW_FORM_udata:          lldata = data = LEB128(ptr); size = 0; break;
		case DW_FORM_ref_addr:       size = cu->address_size; lldata = RDsize(ptr, size); data = (unsigned long)lldata; isRef = true; break;
		case DW_FORM_ref1:           size = 1; lldata = data = *ptr; isRef = true; break;
		case DW_FORM_ref2:           size = 2; lldata = data = RD2(ptr); isRef = true; break;
		case DW_FORM_ref4:           size = 4; lldata = data = RD4(ptr); isRef = true; break;
		case DW_FORM_ref8:           size = 8; lldata = RD8(ptr); data = (unsigned long)lldata; isRef = true; break;
		case DW_FORM_ref_udata:      lldata = data = LEB128(ptr); size = 0; isRef = true; break;
		case DW_FORM_exprloc:        size = LEB128(ptr); lldata = decodeLocation(ptr, size, false, locid); data = (unsigned long)lldata; isRef = true; break;
		case DW_FORM_flag_present:   size = 0; lldata = data = 1; break;
		case DW_FORM_ref_sig8:       size = 8; break;
		case DW_FORM_sec_offset:     size = img->isX64() ? 8 : 4; lldata = RDsize(ptr, size); data = (unsigned long)lldata; isRef = true; break;
		case DW_FORM_indirect:
		default: assert(false && "Unsupported DWARF attribute form"); return false;
		}
		switch (attr)
		{
		case DW_AT_byte_size: id.byte_size = data; break;
		case DW_AT_sibling:   id.sibling = data; break;
		case DW_AT_encoding:  id.encoding = data; break;
		case DW_AT_name:      id.name = str; break;
		case DW_AT_MIPS_linkage_name: id.linkage_name = str; break;
		case DW_AT_comp_dir:  id.dir = str; break;
		case DW_AT_low_pc:    id.pclo = addr; break;
		case DW_AT_high_pc:
			if (form == DW_FORM_addr)
				id.pchi = addr;
			else
				id.pchi = id.pclo + data;
			break;
		case DW_AT_ranges:    id.ranges = data; break;
		case DW_AT_type:      id.type = data; break;
		case DW_AT_inline:    id.inlined = data; break;
		case DW_AT_external:  id.external = data; break;
		case DW_AT_upper_bound: id.upper_bound = data; break;
		case DW_AT_lower_bound: id.lower_bound = data; break;
		case DW_AT_containing_type: id.containing_type = data; break;
		case DW_AT_specification: id.specification = data; break;
		case DW_AT_data_member_location:
			if (form == DW_FORM_block1 || form == DW_FORM_exprloc)
				id.member_location_ptr = ptr, id.member_location_len = size;
			else
				id.member_location_data = data;
			break;
		case DW_AT_location:
			if (form == DW_FORM_block1 || form == DW_FORM_exprloc)
				id.location_ptr = ptr, id.location_len = size;
			else
				id.locationlist = data;
			break;
		case DW_AT_frame_base:
			if (form != DW_FORM_block2 && form != DW_FORM_block4 && form != DW_FORM_block1 && form != DW_FORM_block)
				id.frame_base = data;
			break;
		}
		ptr += size;
	}

	hasChild = id.hasChild != 0;
	sibling = id.sibling;

	return true;
}

unsigned char* DIECursor::getDWARFAbbrev(unsigned off, unsigned findcode)
{
	if (!img->debug_abbrev)
		return 0;

	std::pair<unsigned, unsigned> key = std::make_pair(off, findcode);
	abbrevMap_t::iterator it = abbrevMap.find(key);
	if (it != abbrevMap.end())
	{
		return it->second;
	}

	unsigned char* p = (unsigned char*)img->debug_abbrev + off;
	unsigned char* end = (unsigned char*)img->debug_abbrev + img->debug_abbrev_length;
	while (p < end)
	{
		int code = LEB128(p);
		if (code == findcode)
		{
			abbrevMap.insert(std::make_pair(key, p));
			return p;
		}
		if (code == 0)
			return 0;

		int tag = LEB128(p);
		int hasChild = *p++;

		// skip attributes
		int attr, form;
		do
		{
			attr = LEB128(p);
			form = LEB128(p);
		} while (attr || form);
	}
	return 0;
}
