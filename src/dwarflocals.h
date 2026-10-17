#ifndef __DWARFLOCALS_H__
#define __DWARFLOCALS_H__

#include "readDwarf.h"

#include <vector>

// One piece of a composite location, or the whole location of a variable that is not composite.
struct LocationPiece
{
	// The operations that locate the piece. A piece without operations is optimized out.
	DWARF_Attribute expr;

	// The offset of the piece in the variable, in bytes.
	unsigned offset;

	// The size in bytes that DW_OP_piece gives, or 0 for a location that is not composite.
	unsigned size;
};

// Splits the location expression `expr` at its DW_OP_piece operations. An expression without
// DW_OP_piece yields one piece of size 0. Returns false when the expression cannot be split, for
// example when it uses DW_OP_bit_piece or an operation that this function does not know.
bool splitPieces(const DWARF_Attribute& expr, const DIECursor& cursor, std::vector<LocationPiece>& pieces);

#endif
