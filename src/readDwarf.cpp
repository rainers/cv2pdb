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

Location makeLoc(int r, int o)
{
	Location e = { r, o };
	return e;
}

bool decodeLocation(byte* loc, long len, Location& result, Location* frameBase)
{
	byte* p = loc;
	Location stack[256];
	int stackDepth = 0;

	for (;;)
	{
		if (p >= loc + len)
			break;

		int op = *p++;
		if (op == 0)
			break;

		switch (op)
		{
		case DW_OP_const1u: stack[stackDepth++] = makeLoc(NoReg, *p); break;
		case DW_OP_const2u: stack[stackDepth++] = makeLoc(NoReg, RD2(p)); break;
		case DW_OP_const4u: stack[stackDepth++] = makeLoc(NoReg, RD4(p)); break;
		case DW_OP_const1s: stack[stackDepth++] = makeLoc(NoReg, (char)*p); break;
		case DW_OP_const2s: stack[stackDepth++] = makeLoc(NoReg, (short)RD2(p)); break;
		case DW_OP_const4s: stack[stackDepth++] = makeLoc(NoReg, (int)RD4(p)); break;
		case DW_OP_constu:  stack[stackDepth++] = makeLoc(NoReg, LEB128(p)); break;
		case DW_OP_consts:  stack[stackDepth++] = makeLoc(NoReg, SLEB128(p)); break;

		case DW_OP_plus_uconst: stack[stackDepth - 1].offset += LEB128(p); break;

		case DW_OP_lit0:  case DW_OP_lit1:  case DW_OP_lit2:  case DW_OP_lit3:
		case DW_OP_lit4:  case DW_OP_lit5:  case DW_OP_lit6:  case DW_OP_lit7:
		case DW_OP_lit8:  case DW_OP_lit9:  case DW_OP_lit10: case DW_OP_lit11:
		case DW_OP_lit12: case DW_OP_lit13: case DW_OP_lit14: case DW_OP_lit15:
		case DW_OP_lit16: case DW_OP_lit17: case DW_OP_lit18: case DW_OP_lit19:
		case DW_OP_lit20: case DW_OP_lit21: case DW_OP_lit22: case DW_OP_lit23:
			stack[stackDepth++] = makeLoc(NoReg, op - DW_OP_lit0);
			break;

		case DW_OP_reg0:  case DW_OP_reg1:  case DW_OP_reg2:  case DW_OP_reg3:
		case DW_OP_reg4:  case DW_OP_reg5:  case DW_OP_reg6:  case DW_OP_reg7:
		case DW_OP_reg8:  case DW_OP_reg9:  case DW_OP_reg10: case DW_OP_reg11:
		case DW_OP_reg12: case DW_OP_reg13: case DW_OP_reg14: case DW_OP_reg15:
		case DW_OP_reg16: case DW_OP_reg17: case DW_OP_reg18: case DW_OP_reg19:
		case DW_OP_reg20: case DW_OP_reg21: case DW_OP_reg22: case DW_OP_reg23:
		case DW_OP_reg24: case DW_OP_reg25: case DW_OP_reg26: case DW_OP_reg27:
		case DW_OP_reg28: case DW_OP_reg29: case DW_OP_reg30: case DW_OP_reg31:
			stack[stackDepth++] = makeLoc(op - DW_OP_reg0, 0);
			break;
			case DW_OP_regx:
				stack[stackDepth++] = makeLoc(LEB128(p), 0);
				break;

			case DW_OP_breg0:  case DW_OP_breg1:  case DW_OP_breg2:  case DW_OP_breg3:
			case DW_OP_breg4:  case DW_OP_breg5:  case DW_OP_breg6:  case DW_OP_breg7:
			case DW_OP_breg8:  case DW_OP_breg9:  case DW_OP_breg10: case DW_OP_breg11:
			case DW_OP_breg12: case DW_OP_breg13: case DW_OP_breg14: case DW_OP_breg15:
			case DW_OP_breg16: case DW_OP_breg17: case DW_OP_breg18: case DW_OP_breg19:
			case DW_OP_breg20: case DW_OP_breg21: case DW_OP_breg22: case DW_OP_breg23:
			case DW_OP_breg24: case DW_OP_breg25: case DW_OP_breg26: case DW_OP_breg27:
			case DW_OP_breg28: case DW_OP_breg29: case DW_OP_breg30: case DW_OP_breg31:
				stack[stackDepth++] = makeLoc(op - DW_OP_breg0, SLEB128(p));
				break;
		case DW_OP_bregx:
			{
				unsigned reg = LEB128(p);
				stack[stackDepth++] = makeLoc(reg, SLEB128(p));
			}   break;

		case DW_OP_plus:  // op2 + op1
		{
			Location& op1 = stack[stackDepth - 1];
			Location& op2 = stack[stackDepth - 2];
			if (op1.reg != NoReg && op2.reg != NoReg) // only one of them may be reg-relative
				return false;

			op2.reg = (op1.reg != NoReg) ? op1.reg : op2.reg;
			op2.offset = op2.offset + op1.offset;
			--stackDepth;
		}   break;

		case DW_OP_minus: // op2 - op1
		{
			Location& op1 = stack[stackDepth - 1];
			Location& op2 = stack[stackDepth - 2];
			if (op1.reg == op2.reg)
				op2 = makeLoc(NoReg, op2.offset - op1.offset);
			else if (op1.reg == NoReg)
				op2.offset = op2.offset - op1.offset;
			else
				return false;  // cannot subtract reg-relative
			--stackDepth;
		}   break;

		case DW_OP_and:
		{
			Location& op1 = stack[stackDepth - 1];
			Location& op2 = stack[stackDepth - 2];
			if ((op1.reg == NoReg && op1.offset == 0) || (op2.reg == NoReg && op1.offset == 0)) // X & 0 == 0
				op2 = makeLoc(NoReg, 0);
			else if (op1.reg == NoReg && op2.reg == NoReg)
				op2 = makeLoc(NoReg, op1.offset & op2.offset);
			else
				return false;
			--stackDepth;
		}   break;

		case DW_OP_xor:
		{
			Location& op1 = stack[stackDepth - 1];
			Location& op2 = stack[stackDepth - 2];
			if (op1.reg == op2.reg && op1.offset == op2.offset) // X ^ X == 0
				op2 = makeLoc(NoReg, 0);
			else if (op1.reg == NoReg && op2.reg == NoReg)
				op2 = makeLoc(NoReg, op1.offset ^ op2.offset);
			else
				return false;
			--stackDepth;
		}   break;

		case DW_OP_mul:
		{
			Location& op1 = stack[stackDepth - 1];
			Location& op2 = stack[stackDepth - 2];
			if (op1.reg == NoReg && op1.offset == 0) // X * 0 == 0
				op2 = makeLoc(NoReg, 0);
			else if (op1.reg == NoReg && op2.reg == NoReg)
				op2 = makeLoc(NoReg, op1.offset * op2.offset);
			else
				return false;
			--stackDepth;
		}   break;


		case DW_OP_abs: case DW_OP_neg: case DW_OP_not:
		{
			Location& op1 = stack[stackDepth - 1];
			if (op1.reg != NoReg)
				return false;
			switch (op)
			{
			case DW_OP_abs:   op1.offset = abs(op1.offset); break;
			case DW_OP_neg:   op1.offset = -op1.offset; break;
			case DW_OP_not:   op1.offset = ~op1.offset; break;
			}
		}   break;

		case DW_OP_eq:  case DW_OP_ge:  case DW_OP_gt:
		case DW_OP_le:  case DW_OP_lt:  case DW_OP_ne:
		{
			Location& op1 = stack[stackDepth - 1];
			Location& op2 = stack[stackDepth - 2];
			if (op1.reg != op2.reg) // can't compare unless both use the same register (or NoReg)
				return false;
			switch (op)
			{
			case DW_OP_eq:    op2.offset = op2.offset == op1.offset; break;
			case DW_OP_ge:    op2.offset = op2.offset >= op1.offset; break;
			case DW_OP_gt:    op2.offset = op2.offset > op1.offset; break;
			case DW_OP_le:    op2.offset = op2.offset <= op1.offset; break;
			case DW_OP_lt:    op2.offset = op2.offset < op1.offset; break;
			case DW_OP_ne:    op2.offset = op2.offset != op1.offset; break;
			}
			--stackDepth;
		}   break;

		case DW_OP_div: case DW_OP_mod: case DW_OP_shl:
		case DW_OP_shr: case DW_OP_shra: case DW_OP_or:
		{
			Location& op1 = stack[stackDepth - 1];
			Location& op2 = stack[stackDepth - 2];
			if (op1.reg != NoReg || op2.reg != NoReg) // can't combine unless both are constants
				return false;
			switch (op)
			{
			case DW_OP_div:   op2.offset = op2.offset / op1.offset; break;
			case DW_OP_mod:   op2.offset = op2.offset % op1.offset; break;
			case DW_OP_shl:   op2.offset = op2.offset << op1.offset; break;
			case DW_OP_shr:   op2.offset = op2.offset >> op1.offset; break;
			case DW_OP_shra:  op2.offset = op2.offset >> op1.offset; break;
			case DW_OP_or:    op2.offset = op2.offset | op1.offset; break;
			}
			--stackDepth;
		}   break;

		case DW_OP_fbreg:
			if (!frameBase)
				return false;
			stack[stackDepth++] = makeLoc(frameBase->reg, frameBase->offset + SLEB128(p));
			break;

		case DW_OP_dup:   stack[stackDepth] = stack[stackDepth - 1]; stackDepth++; break;
		case DW_OP_drop:  stackDepth--; break;
		case DW_OP_over:  stack[stackDepth] = stack[stackDepth - 2]; stackDepth++; break;
		case DW_OP_pick:  stack[stackDepth++] = stack[*p]; break;
		case DW_OP_swap:  { Location tmp = stack[stackDepth - 1]; stack[stackDepth - 1] = stack[stackDepth - 2]; stack[stackDepth - 2] = tmp; } break;
		case DW_OP_rot:   { Location tmp = stack[stackDepth - 1]; stack[stackDepth - 1] = stack[stackDepth - 2]; stack[stackDepth - 2] = stack[stackDepth - 3]; stack[stackDepth - 3] = tmp; } break;

		case DW_OP_addr:
			stack[stackDepth++] = makeLoc(NoReg, RD4(p));
			break;

		case DW_OP_skip:
		{
			unsigned off = RD2(p);
			p = p + off;
		}   break;

		case DW_OP_bra:
		{
			Location& op1 = stack[stackDepth - 1];
			if (op1.reg != NoReg)
				return false;
			if (op1.offset != 0)
			{
				unsigned off = RD2(p);
				p = p + off;
			}
			--stackDepth;
		}   break;

			case DW_OP_nop:
				break;

			case DW_OP_push_object_address:
			case DW_OP_call2:
			case DW_OP_call4:
			case DW_OP_form_tls_address:
			case DW_OP_call_frame_cfa:
			case DW_OP_call_ref:
			case DW_OP_bit_piece:
			case DW_OP_implicit_value:
			case DW_OP_stack_value:
			default:
				return false;
		}
	}

	assert(stackDepth > 0);
	result = stack[0];
	return true;
}


long decodeLocation(byte* loc, long len, bool push0, int &id, int& size)
{
	byte* p = loc;
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
			id = (op == DW_OP_breg4 ? S_REGREL_V3 : op == DW_OP_breg5 ? S_BPREL_V2 : S_REGISTER_V2);
			stack[stackDepth++] = SLEB128(p);
			break;
		case DW_OP_bregx:
			data = LEB128(p); // reg
			id = (data == DW_OP_breg4 ? S_REGREL_V3 : data == DW_OP_breg5 ? S_BPREL_V2 : S_REGISTER_V2);
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
			size = RD2(p);
			p += size;
			break;
		}
	} while (stackDepth > 0);
	size = p - loc;
	return stack[0];
}


//decodeLocation(byte* loc, long len)

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

typedef std::unordered_map<std::pair<unsigned, unsigned>, byte*> abbrevMap_t;

static PEImage* img;
static abbrevMap_t abbrevMap;

void DIECursor::setContext(PEImage* img_)
{
	img = img_;
	abbrevMap.clear();
}


DIECursor::DIECursor(DWARF_CompilationUnit* cu_, byte* ptr_)
{
	cu = cu_;
	ptr = ptr_;
	level = 0;
	hasChild = false;
	sibling = 0;
}


bool DIECursor::readSibling(DWARF_InfoData& id)
{
	if (sibling)
	{
		// use sibling pointer, if available
		ptr = sibling;
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

		if (ptr >= ((byte*)cu + sizeof(cu->unit_length) + cu->unit_length))
			return false; // root of the tree does not have a null terminator, but we know the length

		id.entryPtr = ptr;
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

	byte* abbrev = getDWARFAbbrev(cu->debug_abbrev_offset, id.code);
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

		while (form == DW_FORM_indirect)
			form = LEB128(ptr);

		DWARF_Attribute a;
		switch (form)
		{
			case DW_FORM_addr:           a.type = Addr; a.addr = (unsigned long)RDsize(ptr, cu->address_size); break;
			case DW_FORM_block:          a.type = Block; a.block.len = LEB128(ptr); a.block.ptr = ptr; ptr += a.block.len; break;
			case DW_FORM_block1:         a.type = Block; a.block.len = *ptr++;      a.block.ptr = ptr; ptr += a.block.len; break;
			case DW_FORM_block2:         a.type = Block; a.block.len = RD2(ptr);   a.block.ptr = ptr; ptr += a.block.len; break;
			case DW_FORM_block4:         a.type = Block; a.block.len = RD4(ptr);   a.block.ptr = ptr; ptr += a.block.len; break;
			case DW_FORM_data1:          a.type = Const; a.cons = *ptr++; break;
			case DW_FORM_data2:          a.type = Const; a.cons = RD2(ptr); break;
			case DW_FORM_data4:          a.type = Const; a.cons = RD4(ptr); break;
			case DW_FORM_data8:          a.type = Const; a.cons = RD8(ptr); break;
			case DW_FORM_sdata:          a.type = Const; a.cons = SLEB128(ptr); break;
			case DW_FORM_udata:          a.type = Const; a.cons = LEB128(ptr); break;
			case DW_FORM_string:         a.type = String; a.string = (const char*)ptr; ptr += strlen(a.string) + 1; break;
			case DW_FORM_strp:           a.type = String; a.string = (const char*)(img->debug_str + RDsize(ptr, cu->address_size)); break;
			case DW_FORM_flag:           a.type = Flag; a.flag = (*ptr++ != 0); break;
			case DW_FORM_flag_present:   a.type = Flag; a.flag = true; break;
			case DW_FORM_ref1:           a.type = Ref; a.ref = (byte*)cu + *ptr++; break;
			case DW_FORM_ref2:           a.type = Ref; a.ref = (byte*)cu + RD2(ptr); break;
			case DW_FORM_ref4:           a.type = Ref; a.ref = (byte*)cu + RD4(ptr); break;
			case DW_FORM_ref8:           a.type = Ref; a.ref = (byte*)cu + RD8(ptr); break;
			case DW_FORM_ref_udata:      a.type = Ref; a.ref = (byte*)cu + LEB128(ptr); break;
			case DW_FORM_ref_addr:       a.type = Ref; a.ref = (byte*)img->debug_info + (cu->isDWARF64() ? RD8(ptr) : RD4(ptr)); break;
			case DW_FORM_ref_sig8:       a.type = Invalid; ptr += 8;  break;
			case DW_FORM_exprloc:        a.type = ExprLoc; a.expr.len = LEB128(ptr); a.expr.ptr = ptr; ptr += a.expr.len; break;
			case DW_FORM_sec_offset:     a.type = SecOffset;  a.sec_offset = cu->isDWARF64() ? RD8(ptr) : RD4(ptr); break;
			case DW_FORM_indirect:
			default: assert(false && "Unsupported DWARF attribute form"); return false;
		}

		switch (attr)
		{
			case DW_AT_byte_size: assert(a.type == Const); id.byte_size = a.cons; break;
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
			case DW_AT_ranges:    assert(a.type == SecOffset); id.ranges = a.sec_offset; break;
			case DW_AT_type:      assert(a.type == Ref); id.type = a.ref; break;
			case DW_AT_inline:    assert(a.type == Const); id.inlined = a.cons; break;
			case DW_AT_external:  assert(a.type == Flag); id.external = a.flag; break;
			case DW_AT_upper_bound: assert(a.type == Const); id.upper_bound = a.cons; break;
			case DW_AT_lower_bound: assert(a.type == Const); id.lower_bound = a.cons; break;
			case DW_AT_containing_type: assert(a.type == Ref); id.containing_type = a.ref; break;
			case DW_AT_specification: assert(a.type == Ref); id.specification = a.ref; break;
			case DW_AT_data_member_location: id.member_location = a; break;
			case DW_AT_location: id.location = a; break;
			case DW_AT_frame_base: id.frame_base = a; break;
		}
	}

	hasChild = id.hasChild != 0;
	sibling = id.sibling;

	return true;
}

byte* DIECursor::getDWARFAbbrev(unsigned off, unsigned findcode)
{
	if (!img->debug_abbrev)
		return 0;

	std::pair<unsigned, unsigned> key = std::make_pair(off, findcode);
	abbrevMap_t::iterator it = abbrevMap.find(key);
	if (it != abbrevMap.end())
	{
		return it->second;
	}

	byte* p = (byte*)img->debug_abbrev + off;
	byte* end = (byte*)img->debug_abbrev + img->debug_abbrev_length;
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
