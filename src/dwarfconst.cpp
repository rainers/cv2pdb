#include "dwarfconst.h"

#include "dwarf.h"

namespace
{

class ConstantReader
{
public:
	ConstantReader(byte* p, byte* end) : p(p), end(end) {}

	bool atEnd() const { return p >= end; }

	bool byteOp(int& op)
	{
		if (p >= end)
			return false;
		op = *p++;
		return true;
	}

	bool fixed(unsigned size, bool isSigned, uint64_t& value)
	{
		if ((size_t)(end - p) < size)
			return false;
		value = 0;
		for (unsigned i = 0; i < size; i++)
			value |= (uint64_t)p[i] << (8 * i);
		if (isSigned && size < 8 && (value >> (8 * size - 1)) & 1)
			value |= ~(uint64_t)0 << (8 * size);
		p += size;
		return true;
	}

	bool leb128(bool isSigned, uint64_t& value)
	{
		value = 0;
		unsigned shift = 0;
		while (p < end)
		{
			byte b = *p++;
			if (shift < 64)
				value |= (uint64_t)(b & 0x7f) << shift;
			shift += 7;
			if (!(b & 0x80))
			{
				if (isSigned && shift < 64 && (b & 0x40))
					value |= ~(uint64_t)0 << shift;
				return true;
			}
		}
		return false;
	}

private:
	byte* p;
	byte* end;
};

bool binaryOp(int op, uint64_t a, uint64_t b, uint64_t& result)
{
	int64_t sa = (int64_t)a;
	int64_t sb = (int64_t)b;
	switch (op)
	{
	case DW_OP_and:   result = a & b; return true;
	case DW_OP_or:    result = a | b; return true;
	case DW_OP_xor:   result = a ^ b; return true;
	case DW_OP_plus:  result = a + b; return true;
	case DW_OP_minus: result = a - b; return true;
	case DW_OP_mul:   result = a * b; return true;
	case DW_OP_div:
		if (sb == 0 || (sa == INT64_MIN && sb == -1))
			return false;
		result = (uint64_t)(sa / sb);
		return true;
	case DW_OP_mod:
		if (b == 0)
			return false;
		result = a % b;
		return true;
	case DW_OP_shl:
	case DW_OP_shr:
	case DW_OP_shra:
		if (b >= 64)
			return false;
		result = op == DW_OP_shl ? a << b : op == DW_OP_shr ? a >> b : (uint64_t)(sa >> b);
		return true;
	case DW_OP_eq: result = sa == sb; return true;
	case DW_OP_ne: result = sa != sb; return true;
	case DW_OP_lt: result = sa < sb; return true;
	case DW_OP_le: result = sa <= sb; return true;
	case DW_OP_gt: result = sa > sb; return true;
	case DW_OP_ge: result = sa >= sb; return true;
	default:
		return false;
	}
}

} // namespace

bool evalConstantValue(const DWARF_Attribute& expr, uint64_t& value)
{
	if (expr.type != ExprLoc && expr.type != Block)
		return false;

	ConstantReader reader(expr.expr.ptr, expr.expr.ptr + expr.expr.len);
	uint64_t stack[64];
	int depth = 0;
	int op;
	while (reader.byteOp(op))
	{
		if (op == DW_OP_stack_value)
		{
			if (!reader.atEnd() || depth != 1)
				return false;
			value = stack[0];
			return true;
		}

		uint64_t operand = 0;
		bool push = true;
		if (op >= DW_OP_lit0 && op <= DW_OP_lit31)
			operand = op - DW_OP_lit0;
		else
		{
			switch (op)
			{
			case DW_OP_const1u: if (!reader.fixed(1, false, operand)) return false; break;
			case DW_OP_const1s: if (!reader.fixed(1, true, operand)) return false; break;
			case DW_OP_const2u: if (!reader.fixed(2, false, operand)) return false; break;
			case DW_OP_const2s: if (!reader.fixed(2, true, operand)) return false; break;
			case DW_OP_const4u: if (!reader.fixed(4, false, operand)) return false; break;
			case DW_OP_const4s: if (!reader.fixed(4, true, operand)) return false; break;
			case DW_OP_const8u: case DW_OP_const8s: if (!reader.fixed(8, false, operand)) return false; break;
			case DW_OP_constu: if (!reader.leb128(false, operand)) return false; break;
			case DW_OP_consts: if (!reader.leb128(true, operand)) return false; break;

			case DW_OP_abs: case DW_OP_neg: case DW_OP_not:
				if (depth < 1)
					return false;
				if (op == DW_OP_not)
					stack[depth - 1] = ~stack[depth - 1];
				else if (op == DW_OP_neg || (int64_t)stack[depth - 1] < 0)
					stack[depth - 1] = 0 - stack[depth - 1];
				push = false;
				break;

			case DW_OP_plus_uconst:
				if (depth < 1 || !reader.leb128(false, operand))
					return false;
				stack[depth - 1] += operand;
				push = false;
				break;

			default:
				if (depth < 2 || !binaryOp(op, stack[depth - 2], stack[depth - 1], stack[depth - 2]))
					return false;
				depth--;
				push = false;
				break;
			}
		}

		if (push)
		{
			if (depth == sizeof(stack) / sizeof(stack[0]))
				return false;
			stack[depth++] = operand;
		}
	}
	return false;
}
