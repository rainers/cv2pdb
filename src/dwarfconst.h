#ifndef __DWARFCONST_H__
#define __DWARFCONST_H__

#include "readDwarf.h"

#include <stdint.h>

// Evaluates the location expression `expr` when it computes a constant value: the expression pushes
// only constants, combines them with unary and binary arithmetic, and ends with DW_OP_stack_value.
// Returns false for any other expression, and for an operation whose result is undefined, such as
// a division by zero. On success the result is stored in `value`.
bool evalConstantValue(const DWARF_Attribute& expr, uint64_t& value);

#endif
