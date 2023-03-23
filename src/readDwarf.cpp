#include "readDwarf.h"
#include <assert.h>
#include <array>
#include <memory> // unique_ptr

#include "PEImage.h"
#include "cv2pdb.h"
#include "dwarf.h"
#include "mspdb.h"

// declare hasher for pair<T1,T2>
namespace std
{
template<typename T1, typename T2>
struct hash<std::pair<T1, T2>>
{
	size_t operator()(const std::pair<T1, T2>& t) const
	{
		return std::hash<T1>()(t.first) ^ std::hash<T2>()(t.second);
	}
};
}

PEImage* DIECursor::img;
abbrevMap_t DIECursor::abbrevMap;
DebugLevel DIECursor::debug;

void DIECursor::setContext(PEImage* img_, DebugLevel debug_)
{
	img = img_;
	abbrevMap.clear();
	debug = debug_;
}

// Read one compilation unit from `img`'s .debug_info section, starting at
// offset `*off`, updating it in the process to the start of the next one in the
// section.
// Returns a pointer to the first DIE, skipping past the CU header, or NULL
// on failure.
byte* DWARF_CompilationUnitInfo::read(DebugLevel debug, const PEImage& img, unsigned long *off)
{
	byte* ptr = img.debug_info.byteAt(*off);

	start_ptr = ptr;
	cu_offset = *off;
	is_dwarf64 = false;
	base_address = 0;
	unit_length = RD4(ptr);
	if (unit_length == ~0) {
		// DWARF64 doesn't make sense in the context of the PE format since the
		// section size is limited to 32 bits.
		fprintf(stderr, "%s:%d: WARNING: DWARF64 compilation unit at offset=%x is not supported\n", __FUNCTION__, __LINE__,
				cu_offset);

		uint64_t len64 = RD8(ptr);
		*off = img.debug_info.sectOff(ptr + (intptr_t)len64);
		return nullptr;
	}

	end_ptr = ptr + unit_length;
	*off = img.debug_info.sectOff(end_ptr);
	version = RD2(ptr);
	unit_type = DW_UT_compile;
	if (version <= 4) {
		debug_abbrev_offset = RD4(ptr);
		address_size = *ptr++;
	} else if (version == 5) {
		unit_type = *ptr++;
		address_size = *ptr++;
		debug_abbrev_offset = RD4(ptr);
	} else {
		fprintf(stderr, "%s:%d: WARNING: Unsupported dwarf version %d for compilation unit at offset=%x\n", __FUNCTION__, __LINE__,
				version, cu_offset);

		return nullptr;
	}

	if (debug & DbgDwarfCompilationUnit)
		fprintf(stderr, "%s:%d: Reading compilation unit offs=%x, type=%d, ver=%d, addr_size=%d\n", __FUNCTION__, __LINE__,
				cu_offset, unit_type, version, address_size);

	return ptr;
}

static Location mkInReg(unsigned reg)
{
	Location l;
	l.type = Location::InReg;
	l.reg = reg;
	l.off = 0;
	return l;
}

static Location mkAbs(int off)
{
	Location l;
	l.type = Location::Abs;
	l.reg = 0;
	l.off = off;
	return l;
}

static Location mkRegRel(int reg, int off)
{
	Location l;
	l.type = Location::RegRel;
	l.reg = reg;
	l.off = off;
	return l;
}

Location decodeLocation(const DWARF_Attribute& attr, const Location* frameBase, int at)
{
	static Location invalid = { Location::Invalid };

	if (attr.type == Const)
		return mkAbs(attr.cons);

	if (attr.type != ExprLoc && attr.type != Block) // same memory layout
		return invalid;

	byte*p = attr.expr.ptr;
	byte*end = attr.expr.ptr + attr.expr.len;

	Location stack[256];
	int stackDepth = 0;
    if (at == DW_AT_data_member_location)
        stack[stackDepth++] = mkAbs(0);

	for (;;)
	{
		if (p >= end)
			break;

		int op = *p++;
		if (op == 0)
			break;

		switch (op)
		{
			case DW_OP_reg0:  case DW_OP_reg1:  case DW_OP_reg2:  case DW_OP_reg3:
			case DW_OP_reg4:  case DW_OP_reg5:  case DW_OP_reg6:  case DW_OP_reg7:
			case DW_OP_reg8:  case DW_OP_reg9:  case DW_OP_reg10: case DW_OP_reg11:
			case DW_OP_reg12: case DW_OP_reg13: case DW_OP_reg14: case DW_OP_reg15:
			case DW_OP_reg16: case DW_OP_reg17: case DW_OP_reg18: case DW_OP_reg19:
			case DW_OP_reg20: case DW_OP_reg21: case DW_OP_reg22: case DW_OP_reg23:
			case DW_OP_reg24: case DW_OP_reg25: case DW_OP_reg26: case DW_OP_reg27:
			case DW_OP_reg28: case DW_OP_reg29: case DW_OP_reg30: case DW_OP_reg31:
				stack[stackDepth++] = mkInReg(op - DW_OP_reg0);
				break;
			case DW_OP_regx:
				stack[stackDepth++] = mkInReg(LEB128(p));
				break;

			case DW_OP_const1u: stack[stackDepth++] = mkAbs(*p++); break;
			case DW_OP_const2u: stack[stackDepth++] = mkAbs(RD2(p)); break;
			case DW_OP_const4u: stack[stackDepth++] = mkAbs(RD4(p)); break;
			case DW_OP_const1s: stack[stackDepth++] = mkAbs((char)*p++); break;
			case DW_OP_const2s: stack[stackDepth++] = mkAbs((short)RD2(p)); break;
			case DW_OP_const4s: stack[stackDepth++] = mkAbs((int)RD4(p)); break;
			case DW_OP_constu:  stack[stackDepth++] = mkAbs(LEB128(p)); break;
			case DW_OP_consts:  stack[stackDepth++] = mkAbs(SLEB128(p)); break;

			case DW_OP_plus_uconst:
				if (stack[stackDepth - 1].is_inreg())
					return invalid;
				stack[stackDepth - 1].off += LEB128(p);
				break;

			case DW_OP_lit0:  case DW_OP_lit1:  case DW_OP_lit2:  case DW_OP_lit3:
			case DW_OP_lit4:  case DW_OP_lit5:  case DW_OP_lit6:  case DW_OP_lit7:
			case DW_OP_lit8:  case DW_OP_lit9:  case DW_OP_lit10: case DW_OP_lit11:
			case DW_OP_lit12: case DW_OP_lit13: case DW_OP_lit14: case DW_OP_lit15:
			case DW_OP_lit16: case DW_OP_lit17: case DW_OP_lit18: case DW_OP_lit19:
			case DW_OP_lit20: case DW_OP_lit21: case DW_OP_lit22: case DW_OP_lit23:
				stack[stackDepth++] = mkAbs(op - DW_OP_lit0);
				break;

			case DW_OP_breg0:  case DW_OP_breg1:  case DW_OP_breg2:  case DW_OP_breg3:
			case DW_OP_breg4:  case DW_OP_breg5:  case DW_OP_breg6:  case DW_OP_breg7:
			case DW_OP_breg8:  case DW_OP_breg9:  case DW_OP_breg10: case DW_OP_breg11:
			case DW_OP_breg12: case DW_OP_breg13: case DW_OP_breg14: case DW_OP_breg15:
			case DW_OP_breg16: case DW_OP_breg17: case DW_OP_breg18: case DW_OP_breg19:
			case DW_OP_breg20: case DW_OP_breg21: case DW_OP_breg22: case DW_OP_breg23:
			case DW_OP_breg24: case DW_OP_breg25: case DW_OP_breg26: case DW_OP_breg27:
			case DW_OP_breg28: case DW_OP_breg29: case DW_OP_breg30: case DW_OP_breg31:
				stack[stackDepth++] = mkRegRel(op - DW_OP_breg0, SLEB128(p));
				break;
			case DW_OP_bregx:
			{
				unsigned reg = LEB128(p);
				stack[stackDepth++] = mkRegRel(reg, SLEB128(p));
			}   break;


			case DW_OP_abs: case DW_OP_neg: case DW_OP_not:
			{
				Location& op1 = stack[stackDepth - 1];
				if (!op1.is_abs())
					return invalid;
				switch (op)
				{
					case DW_OP_abs:   op1 = mkAbs(abs(op1.off)); break;
					case DW_OP_neg:   op1 = mkAbs(-op1.off); break;
					case DW_OP_not:   op1 = mkAbs(~op1.off); break;
				}
			}   break;

			case DW_OP_plus:  // op2 + op1
			{
				Location& op1 = stack[stackDepth - 1];
				Location& op2 = stack[stackDepth - 2];
				// Can add only two offsets or a regrel and an offset.
				if (op2.is_regrel() && op1.is_abs())
					op2 = mkRegRel(op2.reg, op2.off + op1.off);
				else if (op2.is_abs() && op1.is_regrel())
					op2 = mkRegRel(op1.reg, op2.off + op1.off);
				else if (op2.is_abs() && op1.is_abs())
					op2 = mkAbs(op2.off + op1.off);
				else
					return invalid;
				--stackDepth;
			}   break;

			case DW_OP_minus: // op2 - op1
			{
				Location& op1 = stack[stackDepth - 1];
				Location& op2 = stack[stackDepth - 2];
				if (op2.is_regrel() && op1.is_regrel() && op2.reg == op1.reg)
					op2 = mkAbs(0); // X - X == 0
				else if (op2.is_regrel() && op1.is_abs())
					op2 = mkRegRel(op2.reg, op2.off - op1.off);
				else if (op2.is_abs() && op1.is_abs())
					op2 = mkAbs(op2.off - op1.off);
				else
					return invalid;
				--stackDepth;
			}   break;

			case DW_OP_mul:
			{
				Location& op1 = stack[stackDepth - 1];
				Location& op2 = stack[stackDepth - 2];
				if ((op1.is_abs() && op1.off == 0) || (op2.is_abs() && op2.off == 0))
					op2 = mkAbs(0); // X * 0 == 0
				else if (op1.is_abs() && op2.is_abs())
					op2 = mkAbs(op1.off * op2.off);
				else
					return invalid;
				--stackDepth;
			}   break;

			case DW_OP_and:
			{
				Location& op1 = stack[stackDepth - 1];
				Location& op2 = stack[stackDepth - 2];
				if ((op1.is_abs() && op1.off == 0) || (op2.is_abs() && op2.off == 0))
					op2 = mkAbs(0); // X & 0 == 0
				else if (op1.is_abs() && op2.is_abs())
					op2 = mkAbs(op1.off & op2.off);
				else
					return invalid;
				--stackDepth;
			}   break;

			case DW_OP_div: case DW_OP_mod: case DW_OP_shl:
			case DW_OP_shr: case DW_OP_shra: case DW_OP_or:
			case DW_OP_xor:
			case DW_OP_eq:  case DW_OP_ge:  case DW_OP_gt:
			case DW_OP_le:  case DW_OP_lt:  case DW_OP_ne:
			{
				Location& op1 = stack[stackDepth - 1];
				Location& op2 = stack[stackDepth - 2];
				if (!op1.is_abs() || !op2.is_abs()) // can't combine unless both are constants
					return invalid;
				switch (op)
				{
					case DW_OP_div:   op2.off = op2.off / op1.off; break;
					case DW_OP_mod:   op2.off = op2.off % op1.off; break;
					case DW_OP_shl:   op2.off = op2.off << op1.off; break;
					case DW_OP_shr:   op2.off = op2.off >> op1.off; break;
					case DW_OP_shra:  op2.off = op2.off >> op1.off; break;
					case DW_OP_or:    op2.off = op2.off | op1.off; break;
					case DW_OP_xor:   op2.off = op2.off ^ op1.off; break;
					case DW_OP_eq:    op2.off = op2.off == op1.off; break;
					case DW_OP_ge:    op2.off = op2.off >= op1.off; break;
					case DW_OP_gt:    op2.off = op2.off > op1.off; break;
					case DW_OP_le:    op2.off = op2.off <= op1.off; break;
					case DW_OP_lt:    op2.off = op2.off < op1.off; break;
					case DW_OP_ne:    op2.off = op2.off != op1.off; break;
				}
				--stackDepth;
			}   break;

			case DW_OP_fbreg:
			{
				if (!frameBase)
					return invalid;

				Location loc;
				if (frameBase->is_inreg()) // ok in frame base specification, per DWARF4 spec #3.3.5
					loc = mkRegRel(frameBase->reg, SLEB128(p));
				else if (frameBase->is_regrel())
					loc = mkRegRel(frameBase->reg, frameBase->off + SLEB128(p));
				else
					return invalid;
				stack[stackDepth++] = loc;
			}   break;

			case DW_OP_dup:   stack[stackDepth] = stack[stackDepth - 1]; stackDepth++; break;
			case DW_OP_drop:  stackDepth--; break;
			case DW_OP_over:  stack[stackDepth] = stack[stackDepth - 2]; stackDepth++; break;
			case DW_OP_pick:  stack[stackDepth++] = stack[*p]; break;
			case DW_OP_swap:  { Location tmp = stack[stackDepth - 1]; stack[stackDepth - 1] = stack[stackDepth - 2]; stack[stackDepth - 2] = tmp; } break;
			case DW_OP_rot:   { Location tmp = stack[stackDepth - 1]; stack[stackDepth - 1] = stack[stackDepth - 2]; stack[stackDepth - 2] = stack[stackDepth - 3]; stack[stackDepth - 3] = tmp; } break;

			case DW_OP_addr:
				stack[stackDepth++] = mkAbs(RD4(p)); // TODO: 64-bit
				break;

			case DW_OP_skip:
			{
				unsigned off = RD2(p);
				p = p + off;
			}   break;

			case DW_OP_bra:
			{
				Location& op1 = stack[stackDepth - 1];
				if (!op1.is_abs())
					return invalid;
				if (op1.off != 0)
				{
					unsigned off = RD2(p);
					p = p + off;
				}
				--stackDepth;
			}   break;

			case DW_OP_nop:
				break;

			case DW_OP_call_frame_cfa: // assume ebp+8/rbp+16
				stack[stackDepth++] = Location{ Location::RegRel, DW_REG_CFA, 0 };
				break;

			case DW_OP_deref:
			case DW_OP_deref_size:
			case DW_OP_push_object_address:
			case DW_OP_call2:
			case DW_OP_call4:
			case DW_OP_form_tls_address:
			case DW_OP_call_ref:
			case DW_OP_bit_piece:
			case DW_OP_implicit_value:
			case DW_OP_stack_value:
			default:
				return invalid;
		}
	}

	assert(stackDepth > 0);
	return stack[0];
}

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

LOCCursor::LOCCursor(const DIECursor& parent, unsigned long off)
	: parent(parent)
{
	// Default the base address to the compilation unit (DWARF v4 2.6.2)
	base = parent.cu->base_address;
	isLocLists = (parent.cu->version >= 5);

	// DWARF v4 uses .debug_loc, DWARF v5 uses .debug_loclists with a different
	// schema.
	const PESection& sec = isLocLists ? parent.img->debug_loclists : parent.img->debug_loc;
	ptr = sec.byteAt(off);
	end = sec.endByte();
}

bool LOCCursor::readNext(LOCEntry& entry)
{
	if (isLocLists)
	{
		// DWARF v5 location list parsing.

		if (parent.debug & DbgDwarfLocLists)
			fprintf(stderr, "%s:%d: loclists off=%x DIEoff=%x:\n", __FUNCTION__, __LINE__,
					parent.img->debug_loclists.sectOff(ptr), parent.entryOff);

		auto readCountedLocation = [&entry](byte* &ptr) {
			DWARF_Attribute attr;
			attr.type = Block;
			attr.block.len = LEB128(ptr);
			attr.block.ptr = ptr;
			ptr += attr.block.len;
			entry.loc = decodeLocation(attr);
			return true;
		};

		while (ptr < end)
		{
			byte type = *ptr++;
			switch (type)
			{
			case DW_LLE_end_of_list:
				return false;
			case DW_LLE_base_addressx:
				base = parent.readIndirectAddr(LEB128(ptr));
				continue;
			case DW_LLE_startx_endx:
				entry.beg_offset = parent.readIndirectAddr(LEB128(ptr));
				entry.end_offset = parent.readIndirectAddr(LEB128(ptr));
				return readCountedLocation(ptr);
			case DW_LLE_startx_length:
				entry.beg_offset = parent.readIndirectAddr(LEB128(ptr));
				entry.end_offset = entry.beg_offset + LEB128(ptr);
				return readCountedLocation(ptr);
			case DW_LLE_offset_pair:
				entry.beg_offset = LEB128(ptr);
				entry.end_offset = LEB128(ptr);
				return readCountedLocation(ptr);
			case DW_LLE_default_location:
				entry = {};
				entry.isDefault = true;
				return readCountedLocation(ptr);
			case DW_LLE_base_address:
				base = parent.RDAddr(ptr);
				continue;
			case DW_LLE_start_end:
				entry.beg_offset = parent.RDAddr(ptr);
				entry.end_offset = parent.RDAddr(ptr);
				return readCountedLocation(ptr);
			case DW_LLE_start_length:
				entry.beg_offset = parent.RDAddr(ptr);
				entry.end_offset = entry.beg_offset + LEB128(ptr);
				return readCountedLocation(ptr);
			default:
				fprintf(stderr, "ERROR: %s:%d: unknown loclists entry %d at offs=%x die_offs=%x\n", __FUNCTION__, __LINE__,
						type, parent.img->debug_loclists.sectOff(ptr - 1), parent.entryOff);

				assert(false && "unknown rnglist opcode");
				return false;
			}
		}
	}
	else
	{
		// The logic here is goverened by DWARF4 section 2.6.2.

		if (ptr >= end)
			return false;

		if (parent.debug & DbgDwarfLocLists)
			fprintf(stderr, "%s:%d: loclist off=%x DIEoff=%x:\n", __FUNCTION__, __LINE__,
					parent.img->debug_loc.sectOff(ptr), parent.entryOff);

		// Extract the begin and end offset
		// TODO: Why is this truncating to 32 bit?
		entry.beg_offset = (unsigned long) parent.RDAddr(ptr);
		entry.end_offset = (unsigned long) parent.RDAddr(ptr);

		// Check for a base-address-selection entry.
		if (entry.beg_offset == -1U) {
			// This is a base address selection entry and thus has no location
			// description.
			// Update the base address with this entry's value.
			base = entry.end_offset;

			// Continue the scan, but don't try to decode further since there
			// are no location description records following this type of entry.
			return true;
		}

		// Check for end-of-list entry. (Both offsets 0)
		if (!entry.beg_offset && !entry.end_offset) {
			// Terminate the scan.
			return false;
		}

		DWARF_Attribute attr;
		attr.type = Block;
		attr.block.len = RD2(ptr);
		attr.block.ptr = ptr;
		entry.loc = decodeLocation(attr);
		ptr += attr.expr.len;
		return true;
	}

	return false;
}

RangeCursor::RangeCursor(const DIECursor& parent, unsigned long off)
	: parent(parent)
{
	base = parent.cu->base_address;
	isRngLists = (parent.cu->version >= 5);

	const PESection& sec = isRngLists ? parent.img->debug_rnglists : parent.img->debug_ranges;
	ptr = sec.byteAt(off);
	end = sec.endByte();
}

bool RangeCursor::readNext(RangeEntry& entry)
{
	if (isRngLists)
	{
		if (parent.debug & DbgDwarfRangeLists)
			fprintf(stderr, "%s:%d: rnglists off=%x DIEoff=%x:\n", __FUNCTION__, __LINE__,
					parent.img->debug_rnglists.sectOff(ptr), parent.entryOff);

		while (ptr < end)
		{
			byte type = *ptr++;
			switch (type)
			{
			case DW_RLE_end_of_list:
				return false;
			case DW_RLE_base_addressx:
				base = parent.readIndirectAddr(LEB128(ptr));
				continue;
			case DW_RLE_startx_endx:
				entry.pclo = parent.readIndirectAddr(LEB128(ptr));
				entry.pchi = parent.readIndirectAddr(LEB128(ptr));
				return true;
			case DW_RLE_startx_length:
				entry.pclo = parent.readIndirectAddr(LEB128(ptr));
				entry.pchi = entry.pclo + LEB128(ptr);
				return true;
			case DW_RLE_offset_pair:
				entry.pclo = LEB128(ptr);
				entry.pchi = LEB128(ptr);
				entry.addBase(base);
				return true;
			case DW_RLE_base_address:
				base = parent.RDAddr(ptr);
				continue;
			case DW_RLE_start_end:
				entry.pclo = parent.RDAddr(ptr);
				entry.pchi = parent.RDAddr(ptr);
				return true;
			case DW_RLE_start_length:
				entry.pclo = parent.RDAddr(ptr);
				entry.pchi = entry.pclo + LEB128(ptr);
				return true;
			default:
				fprintf(stderr, "ERROR: %s:%d: unknown rnglists entry %d at offs=%x die_offs=%x\n", __FUNCTION__, __LINE__,
						type, parent.img->debug_rnglists.sectOff(ptr - 1), parent.entryOff);

				assert(false && "unknown rnglist opcode");
				return false;
			}
		}
	}
	else
	{
		while (ptr < end) {
			if (parent.debug & DbgDwarfRangeLists)
				fprintf(stderr, "%s:%d: rangelist off=%x DIEoff=%x:\n", __FUNCTION__, __LINE__,
						parent.img->debug_ranges.sectOff(ptr), parent.entryOff);

			entry.pclo = parent.RDAddr(ptr);
			entry.pchi = parent.RDAddr(ptr);
			if (!entry.pclo && !entry.pchi)
				return false;

			if (entry.pclo >= entry.pchi)
				continue;

			entry.addBase(parent.cu->base_address);
			return true;
		}
	}

	return false;
}

DIECursor::DIECursor(DWARF_CompilationUnitInfo* cu_, byte* ptr_)
{
	cu = cu_;
	ptr = ptr_;
	level = 0;
	prevHasChild = false;
	sibling = 0;
}

DIECursor::DIECursor(const DIECursor& parent, byte* ptr_)
	: DIECursor(parent)
{
	ptr = ptr_;
}

// Advance the cursor to the next sibling of the current node, using the fast
// path when possible.
void DIECursor::gotoSibling()
{
	if (sibling)
	{
		// Fast path: use sibling pointer, if available.
		ptr = sibling;
		prevHasChild = false;
	}
	else if (prevHasChild)
	{
		// Slow path. Skip over child nodes until we get back to the current
		// level.
		const int currLevel = level;
		level = currLevel + 1;
		prevHasChild = false;

		// Don't store these in the tree since this is just used for skipping over
		// last swaths of nodes.
		DWARF_InfoData dummy;

		// read until we pop back to the level we were at
		while (level > currLevel)
			readNext(&dummy, true /* stopAtNull */);
	}
}

DIECursor DIECursor::getSubtreeCursor()
{
	if (prevHasChild)
	{
		DIECursor subtree = *this;
		subtree.level = 0;
		subtree.prevHasChild = false;
		return subtree;
	}
	else // Return invalid cursor
	{
		DIECursor subtree = *this;
		subtree.level = -1;
		return subtree;
	}
}

const char Cv2PdbInvalidString[] = "<Cv2Pdb invalid string>";

const char* DIECursor::resolveIndirectString(uint32_t index) const
{
	if (!cu->str_offset_base)
	{
		fprintf(stderr, "ERROR: %s:%d: no string base for cu_offs=%x die_offs=%x\n", __FUNCTION__, __LINE__,
				cu->cu_offset, entryOff);
		return Cv2PdbInvalidString;
	}

	byte* refAddr = cu->str_offset_base + index * refSize();
	return (const char*)img->debug_str.byteAt(RDref(refAddr));
}

uint32_t DIECursor::readIndirectAddr(uint32_t index) const
{
	if (!cu->addr_base)
	{
		fprintf(stderr, "ERROR: %s:%d: no addr base for cu_offs=%x die_offs=%x\n", __FUNCTION__, __LINE__,
				cu->cu_offset, entryOff);

		return 0;
	}

	byte* refAddr = cu->addr_base + index * refSize();
	return RDAddr(refAddr);
}

uint32_t DIECursor::resolveIndirectSecPtr(uint32_t index, const SectionDescriptor &secDesc, byte *baseAddress) const
{
	if (!baseAddress)
	{
		fprintf(stderr, "ERROR: %s:%d: no base address in section %s for cu_offs=%x die_offs=%x\n", __FUNCTION__, __LINE__,
				secDesc.name, cu->cu_offset, entryOff);

		return 0;
	}

	byte* refAddr = baseAddress + index * refSize();
	byte* targetAddr = baseAddress + RDref(refAddr);
	return (img->*(secDesc.pSec)).sectOff(targetAddr);
}

static byte* getPointerInSection(const PEImage &img, const SectionDescriptor &secDesc, uint32_t offset)
{
	const PESection &peSec = img.*(secDesc.pSec);

	if (!peSec.isPresent() || offset >= peSec.length)
	{
		fprintf(stderr, "%s:%d: WARNING: offset %x is not valid in section %s\n", __FUNCTION__, __LINE__,
				offset, secDesc.name);
		return nullptr;
	}

	return peSec.byteAt(offset);
}

// Scan the next DIE from the current CU.
// TODO: Allocate a new element each time.
DWARF_InfoData* DIECursor::readNext(DWARF_InfoData* entry, bool stopAtNull)
{
	std::unique_ptr<DWARF_InfoData> node;

	// Controls whether we should bother establishing links between nodes.
	// If 'entry' is provided, we are just going to be using it instead
	// of allocating our own nodes. The callers typically reuse the same
	// node over and over in this case, so don't bother tracking the links.
	// Furthermore, since we clear the input node in this case, we can't rely
	// on it from call to call.
	// TODO: Rethink how to more cleanly express the alloc vs reuse modes of
	// operation.
	bool establishLinks = false;

	// If an entry was passed in, use it. Else allocate one.
	if (!entry) {
		establishLinks = true;
		node = std::make_unique<DWARF_InfoData>();
		entry = node.get();
	} else {
		// If an entry was provided, make sure we clear it.
		entry->clear();
	}

	entry->img = img;
	
	if (prevHasChild) {
		// Prior element had a child, thus this element is its first child.
		++level;

		if (establishLinks) {
			// Establish the first child.
			prevParent->children = entry;
		}
	}

	// Set up a convenience alias.
	DWARF_InfoData& id = *entry;

	// Find the first valid DIE.
	for (;;)
	{
		if (level == -1)
			return nullptr; // we were already at the end of the subtree

		if (ptr >= cu->end_ptr)
			return nullptr; // root of the tree does not have a null terminator, but we know the length

		id.entryPtr = ptr;
		entryOff = id.entryOff = img->debug_info.sectOff(ptr);
		id.code = LEB128(ptr);

		// If the previously scanned node claimed to have a child, this must be a valid DIE.
		assert(!prevHasChild || id.code);

		// Check if we need to terminate the sibling chain.
		if (id.code == 0)
		{
			// Done with this level.
			if (establishLinks) {
				// Continue linking siblings from the parent node.
				prevNode = prevParent;

				// Unwind the parent one level up.
				prevParent = prevParent->parent;
			}

			--level;
			if (stopAtNull)
			{
				prevHasChild = false;
				return nullptr;
			}
			continue; // read the next DIE
		}

		break;
	}

	byte* abbrev = getDWARFAbbrev(cu->debug_abbrev_offset, id.code);
	if (!abbrev) {
		fprintf(stderr, "ERROR: %s:%d: unknown abbrev: num=%d off=%x\n", __FUNCTION__, __LINE__,
				id.code, entryOff);
		assert(abbrev);
		return nullptr;
	}

	id.abbrev = abbrev;
	id.tag = LEB128(abbrev);
	id.hasChild = *abbrev++;

	if (establishLinks) {
		// If there was a previous node, link it to this one, thus continuing the chain.
		if (prevNode) {
			prevNode->next = entry;
		}

		// Establish parent of current node. If 'prevParent' is NULL, that is fine.
		// It just means this node is a top-level node.
		entry->parent = prevParent;

		if (id.hasChild) {
			// This node has children! Establish it as the new parent for future nodes.		
			prevParent = entry;

			// Clear the last DIE because the next scanned node will form the *start*
			// of a new linked list comprising the children of the current node.
			prevNode = nullptr;
		}
		else {
			// Ensure the next node appends itself to this one.
			prevNode = entry;
		}
	}

	if (debug & DbgDwarfAttrRead)
		fprintf(stderr, "%s:%d: offs=%x level=%d tag=%d abbrev=%d\n", __FUNCTION__, __LINE__,
				entryOff, level, id.tag, id.code);

	// Read all the attribute data for this DIE.
	int attr, form;
	for (;;)
	{
		attr = LEB128(abbrev);
		form = LEB128(abbrev);

		if (attr == 0 && form == 0)
			break;

		if (debug & DbgDwarfAttrRead)
			fprintf(stderr, "%s:%d: offs=%x, attr=%d, form=%d\n", __FUNCTION__, __LINE__,
					img->debug_info.sectOff(ptr), attr, form);

		while (form == DW_FORM_indirect) {
			form = LEB128(ptr);
			if (debug & DbgDwarfAttrRead)
				fprintf(stderr, "%s:%d: attr=%d, form=%d\n", __FUNCTION__, __LINE__,
						attr, form);
		}

		DWARF_Attribute a;
		switch (form)
		{
			case DW_FORM_addr:           a.type = Addr; a.addr = RDAddr(ptr); break;
			case DW_FORM_addrx:          a.type = Addr; a.addr = readIndirectAddr(LEB128(ptr)); break;
			case DW_FORM_addrx1:
			case DW_FORM_addrx2:
			case DW_FORM_addrx3:
			case DW_FORM_addrx4:         a.type = Addr; a.addr = readIndirectAddr(RDsize(ptr, 1 + (form - DW_FORM_addrx1))); break;
			case DW_FORM_block:          a.type = Block; a.block.len = LEB128(ptr); a.block.ptr = ptr; ptr += a.block.len; break;
			case DW_FORM_block1:         a.type = Block; a.block.len = *ptr++;      a.block.ptr = ptr; ptr += a.block.len; break;
			case DW_FORM_block2:         a.type = Block; a.block.len = RD2(ptr);   a.block.ptr = ptr; ptr += a.block.len; break;
			case DW_FORM_block4:         a.type = Block; a.block.len = RD4(ptr);   a.block.ptr = ptr; ptr += a.block.len; break;
			case DW_FORM_data1:          a.type = Const; a.cons = *ptr++; break;
			case DW_FORM_data2:          a.type = Const; a.cons = RD2(ptr); break;
			case DW_FORM_data4:          a.type = Const; a.cons = RD4(ptr); break;
			case DW_FORM_data8:          a.type = Const; a.cons = RD8(ptr); break;
			case DW_FORM_data16:         a.type = Block; a.block.len = 16; a.block.ptr = ptr; ptr += a.block.len; break;
			case DW_FORM_sdata:          a.type = Const; a.cons = SLEB128(ptr); break;
			case DW_FORM_udata:          a.type = Const; a.cons = LEB128(ptr); break;
			case DW_FORM_implicit_const: a.type = Const; a.cons = LEB128(abbrev); break;
			case DW_FORM_string:         a.type = String; a.string = (const char*)ptr; ptr += strlen(a.string) + 1; break;
            case DW_FORM_strp:           a.type = String; a.string = (const char*)img->debug_str.byteAt(RDref(ptr)); break;
			case DW_FORM_line_strp:      a.type = String; a.string = (const char*)img->debug_line_str.byteAt(RDref(ptr)); break;
			case DW_FORM_strx:           a.type = String; a.string = resolveIndirectString(LEB128(ptr)); break;
			case DW_FORM_strx1:
			case DW_FORM_strx2:
			case DW_FORM_strx3:
			case DW_FORM_strx4:          a.type = String; a.string = resolveIndirectString(RDsize(ptr, 1 + (form - DW_FORM_strx1))); break;
			case DW_FORM_strp_sup:       a.type = Invalid; assert(false && "Unsupported supplementary object"); ptr += refSize(); break;
			case DW_FORM_flag:           a.type = Flag; a.flag = (*ptr++ != 0); break;
			case DW_FORM_flag_present:   a.type = Flag; a.flag = true; break;
			case DW_FORM_ref1:           a.type = Ref; a.ref = cu->start_ptr + *ptr++; break;
			case DW_FORM_ref2:           a.type = Ref; a.ref = cu->start_ptr + RD2(ptr); break;
			case DW_FORM_ref4:           a.type = Ref; a.ref = cu->start_ptr + RD4(ptr); break;
			case DW_FORM_ref8:           a.type = Ref; a.ref = cu->start_ptr + RD8(ptr); break;
			case DW_FORM_ref_udata:      a.type = Ref; a.ref = cu->start_ptr + LEB128(ptr); break;
			case DW_FORM_ref_addr:       a.type = Ref; a.ref = img->debug_info.byteAt(RDref(ptr)); break;
			case DW_FORM_ref_sig8:       a.type = Invalid; ptr += 8;  break;
			case DW_FORM_ref_sup4:       a.type = Invalid; assert(false && "Unsupported supplementary object"); ptr += 4; break;
			case DW_FORM_ref_sup8:       a.type = Invalid; assert(false && "Unsupported supplementary object"); ptr += 8; break;
			case DW_FORM_exprloc:        a.type = ExprLoc; a.expr.len = LEB128(ptr); a.expr.ptr = ptr; ptr += a.expr.len; break;
			case DW_FORM_sec_offset:     a.type = SecOffset; a.sec_offset = RDref(ptr); break;
			case DW_FORM_loclistx:       a.type = SecOffset; a.sec_offset = resolveIndirectSecPtr(LEB128(ptr), sec_desc_debug_loclists, cu->loclist_base); break;
			case DW_FORM_rnglistx:       a.type = SecOffset; a.sec_offset = resolveIndirectSecPtr(LEB128(ptr), sec_desc_debug_rnglists, cu->rnglist_base); break;
			default: assert(false && "Unsupported DWARF attribute form"); return nullptr;
		}

		switch (attr)
		{
			case DW_AT_byte_size:
				assert(a.type == Const || a.type == Ref || a.type == ExprLoc);
				if (a.type == Const) // TODO: other types not supported yet
					id.byte_size = a.cons;
				break;
			case DW_AT_sibling:   assert(a.type == Ref); id.sibling = a.ref; break;
			case DW_AT_encoding:  assert(a.type == Const); id.encoding = a.cons; break;
			case DW_AT_name:      assert(a.type == String); id.name = a.string; break;
			case DW_AT_MIPS_linkage_name: assert(a.type == String); id.linkage_name = a.string; break;
			case DW_AT_comp_dir:  assert(a.type == String); id.dir = a.string; break;
			case DW_AT_low_pc:    assert(a.type == Addr); id.pclo = a.addr; break;
			case DW_AT_high_pc:
				if (a.type == Addr)
					id.pchi = a.addr;
				else if (a.type == Const)
					id.pchi = id.pclo + a.cons;
				else
					assert(false);
			    break;
		    case DW_AT_entry_pc:
			    if (a.type == Addr)
				    id.pcentry = a.addr;
			    else if (a.type == Const)
				    id.pcentry = id.pclo + a.cons;
			    else
				    assert(false);
			    break;
			case DW_AT_ranges:
				if (a.type == SecOffset)
					id.ranges = a.sec_offset;
				else if (a.type == Const)
					id.ranges = a.cons;
				else
					assert(false);
			    break;
			case DW_AT_type:      assert(a.type == Ref); id.type = a.ref; break;
			case DW_AT_inline:    assert(a.type == Const); id.inlined = a.cons; break;
			case DW_AT_external:  assert(a.type == Flag); id.external = a.flag; break;
			case DW_AT_declaration: assert(a.type == Flag); id.isDecl = a.flag; break;
			case DW_AT_upper_bound:
				assert(a.type == Const || a.type == Ref || a.type == ExprLoc || a.type == Block);
				if (a.type == Const) // TODO: other types not supported yet
					id.upper_bound = a.cons;
				break;
			case DW_AT_lower_bound:
				assert(a.type == Const || a.type == Ref || a.type == ExprLoc);
				if (a.type == Const)
				{
					// TODO: other types not supported yet
					id.lower_bound = a.cons;
					id.has_lower_bound = true;
				}
				break;
			case DW_AT_containing_type: assert(a.type == Ref); id.containing_type = a.ref; break;
			case DW_AT_specification: assert(a.type == Ref); id.specification = a.ref; break;
			case DW_AT_abstract_origin: assert(a.type == Ref); id.abstract_origin = a.ref; break;
			case DW_AT_data_member_location: id.member_location = a; break;
			case DW_AT_location: id.location = a; break;
			case DW_AT_frame_base: id.frame_base = a; break;
			case DW_AT_language: assert(a.type == Const); id.language = a.cons; break;
			case DW_AT_const_value:
				switch (a.type)
				{
				case Const:
					id.const_value = a.cons;
					id.has_const_value = true;
					break;

				// TODO: handle these
				case String:
				case Block:
					break;

				default:
					assert(false);
					break;
				}
				break;
		    case DW_AT_artificial:
				assert(a.type == Flag);
				id.has_artificial = true;
				id.is_artificial = true;
				break;

			case DW_AT_str_offsets_base:
				cu->str_offset_base = getPointerInSection(*img, sec_desc_debug_str_offsets, a.sec_offset);
				break;
			case DW_AT_addr_base:
				cu->addr_base = getPointerInSection(*img, sec_desc_debug_addr, a.sec_offset);
				break;
			case DW_AT_rnglists_base:
				cu->rnglist_base = getPointerInSection(*img, sec_desc_debug_rnglists, a.sec_offset);
				break;
			case DW_AT_loclists_base:
				cu->loclist_base = getPointerInSection(*img, sec_desc_debug_loclists, a.sec_offset);
				break;
		}
	}

	prevHasChild = id.hasChild != 0;
	sibling = id.sibling;

	// Transfer ownership of 'node' to caller, if we allocated one.
	node.release();
	return entry;
}

byte* DIECursor::getDWARFAbbrev(unsigned off, unsigned findcode)
{
	if (!img->debug_abbrev.isPresent())
		return 0;

	std::pair<unsigned, unsigned> key = std::make_pair(off, findcode);
	abbrevMap_t::iterator it = abbrevMap.find(key);
	if (it != abbrevMap.end())
	{
		return it->second;
	}

	byte* p = img->debug_abbrev.byteAt(off);
	byte* end = img->debug_abbrev.endByte();
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

			// Implicit const forms have an extra constant value attached.
			if (form == DW_FORM_implicit_const)
				LEB128(p);
		} while (attr || form);
	}
	return 0;
}
