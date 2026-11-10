#ifndef __CONSTANTBLOCKS_H__
#define __CONSTANTBLOCKS_H__

#include "blockpieces.h"
#include "scopeconstant.h"

#include <stdint.h>
#include <vector>

// One S_BLOCK32 record to write, spanning [lo, hi), that contains an S_CONSTANT record for each
// of `constants`.
struct ConstantSegment
{
	uint64_t lo;
	uint64_t hi;
	std::vector<ScopeConstant> constants;
};

// Splits the constants of a scope that spans [lo, hi) around `pieces`, the pieces of its nested
// scopes. `pieces` must be in ascending address order, with no two overlapping. The parts of the
// constants that lie outside every piece are returned as segments in ascending address order, and
// no segment overlaps a piece. A segment contains at most one constant of each name. Where several
// constants of one name cover an address, the constant of that name in the segment has the value of
// the first of them in `constants`. The part of a constant that lies inside a piece is returned in
// the entry of `pieceConstants` for that piece.
std::vector<ConstantSegment> splitConstantBlocks(const std::vector<ScopeConstant>& constants,
                                                 const std::vector<BlockPiece>& pieces, uint64_t lo, uint64_t hi,
                                                 std::vector<std::vector<ScopeConstant>>& pieceConstants);

#endif
