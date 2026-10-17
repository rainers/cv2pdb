#include "dwarflocals.h"

#include "dwarf.h"

namespace
{

bool skipLEB128(byte*& p, byte* end)
{
	while (p < end)
		if (!(*p++ & 0x80))
			return true;
	return false;
}

bool skipBytes(byte*& p, byte* end, unsigned n)
{
	if ((size_t)(end - p) < n)
		return false;
	p += n;
	return true;
}

bool skipBlock(byte*& p, byte* end)
{
	byte* len = p;
	if (!skipLEB128(p, end))
		return false;
	return skipBytes(p, end, LEB128(len));
}

// Advances `p` past the operands of the operation `op`. Returns false for an operation whose
// operands are not known.
bool skipOperands(int op, byte*& p, byte* end, const DIECursor& cursor)
{
	if ((op >= DW_OP_lit0 && op <= DW_OP_lit31) || (op >= DW_OP_reg0 && op <= DW_OP_reg31))
		return true;
	if (op >= DW_OP_breg0 && op <= DW_OP_breg31)
		return skipLEB128(p, end);

	switch (op)
	{
	case DW_OP_deref:  case DW_OP_dup:   case DW_OP_drop:  case DW_OP_over:  case DW_OP_swap:
	case DW_OP_rot:    case DW_OP_xderef: case DW_OP_abs:  case DW_OP_and:   case DW_OP_div:
	case DW_OP_minus:  case DW_OP_mod:   case DW_OP_mul:   case DW_OP_neg:   case DW_OP_not:
	case DW_OP_or:     case DW_OP_plus:  case DW_OP_shl:   case DW_OP_shr:   case DW_OP_shra:
	case DW_OP_xor:    case DW_OP_eq:    case DW_OP_ge:    case DW_OP_gt:    case DW_OP_le:
	case DW_OP_lt:     case DW_OP_ne:    case DW_OP_nop:   case DW_OP_push_object_address:
	case DW_OP_form_tls_address: case DW_OP_call_frame_cfa: case DW_OP_stack_value:
	case DW_OP_GNU_push_tls_address: case DW_OP_GNU_uninit:
		return true;

	case DW_OP_const1u: case DW_OP_const1s: case DW_OP_pick: case DW_OP_deref_size: case DW_OP_xderef_size:
		return skipBytes(p, end, 1);
	case DW_OP_const2u: case DW_OP_const2s: case DW_OP_skip: case DW_OP_bra: case DW_OP_call2:
		return skipBytes(p, end, 2);
	case DW_OP_const4u: case DW_OP_const4s: case DW_OP_call4: case DW_OP_GNU_parameter_ref:
		return skipBytes(p, end, 4);
	case DW_OP_const8u: case DW_OP_const8s:
		return skipBytes(p, end, 8);
	case DW_OP_addr:
		return skipBytes(p, end, cursor.cu->address_size == 4 ? 4 : 8);
	case DW_OP_call_ref:
		return skipBytes(p, end, cursor.refSize());

	case DW_OP_constu: case DW_OP_consts: case DW_OP_plus_uconst: case DW_OP_regx: case DW_OP_fbreg:
	case DW_OP_piece: case DW_OP_addrx: case DW_OP_constx: case DW_OP_convert: case DW_OP_reinterpret:
	case DW_OP_GNU_convert: case DW_OP_GNU_reinterpret: case DW_OP_GNU_addr_index: case DW_OP_GNU_const_index:
		return skipLEB128(p, end);
	case DW_OP_bregx: case DW_OP_bit_piece: case DW_OP_regval_type: case DW_OP_GNU_regval_type:
		return skipLEB128(p, end) && skipLEB128(p, end);
	case DW_OP_deref_type: case DW_OP_xderef_type: case DW_OP_GNU_deref_type:
		return skipBytes(p, end, 1) && skipLEB128(p, end);
	case DW_OP_implicit_pointer: case DW_OP_GNU_implicit_pointer:
		return skipBytes(p, end, cursor.refSize()) && skipLEB128(p, end);
	case DW_OP_implicit_value: case DW_OP_entry_value: case DW_OP_GNU_entry_value:
		return skipBlock(p, end);
	case DW_OP_const_type: case DW_OP_GNU_const_type:
	{
		if (!skipLEB128(p, end) || p >= end)
			return false;
		unsigned size = *p++;
		return skipBytes(p, end, size);
	}

	default:
		return false;
	}
}

} // namespace

bool splitPieces(const DWARF_Attribute& expr, const DIECursor& cursor, std::vector<LocationPiece>& pieces)
{
	pieces.clear();
	byte* p = expr.expr.ptr;
	byte* end = p + expr.expr.len;
	byte* start = p;
	unsigned offset = 0;
	bool branches = false;
	while (p < end)
	{
		byte* opPtr = p;
		int op = *p++;
		if (op == DW_OP_bit_piece)
			return false;
		if (op == DW_OP_skip || op == DW_OP_bra)
			branches = true;
		if (!skipOperands(op, p, end, cursor))
			return false;
		if (op == DW_OP_piece)
		{
			byte* size = opPtr + 1;
			LocationPiece piece;
			piece.expr.type = Block;
			piece.expr.block.ptr = start;
			piece.expr.block.len = (unsigned)(opPtr - start);
			piece.offset = offset;
			piece.size = LEB128(size);
			pieces.push_back(piece);
			offset += piece.size;
			start = p;
		}
	}

	if (pieces.empty())
	{
		LocationPiece whole;
		whole.expr = expr;
		whole.offset = 0;
		whole.size = 0;
		pieces.push_back(whole);
		return true;
	}

	// Operations after the last DW_OP_piece belong to no piece, and a branch can jump from one
	// piece into another.
	return start == end && !branches;
}
