#ifndef __BLOCKPIECES_H__
#define __BLOCKPIECES_H__

#include "readDwarf.h"

#include <stddef.h>
#include <stdint.h>
#include <vector>

// An address range [pclo, pchi) and the index `block` of the item it belongs to.
// `splitSiblingBlocks` sets `block` to an index into its `blockRanges` argument.
struct BlockPiece
{
	size_t block;
	uint64_t pclo;
	uint64_t pchi;
};

// Splits the sibling lexical blocks of one scope into pieces that lie in [lo, hi). Each block is
// given as its list of address ranges. The pieces are returned in ascending address order. Two
// ranges of one block become one piece, which also covers the gap between them, when no range of
// another block starts between them. The pieces do not overlap as long as the ranges of different
// blocks do not.
std::vector<BlockPiece> splitSiblingBlocks(const std::vector<std::vector<RangeEntry>>& blockRanges,
                                           uint64_t lo, uint64_t hi);

// Returns the parts of `pieces` that lie outside every piece of `holes`. Each part keeps the `block`
// of the piece it is cut from. Both lists must be in ascending address order, with no two pieces of
// a list overlapping. The result is in ascending order.
std::vector<BlockPiece> cutOutPieces(const std::vector<BlockPiece>& pieces,
                                     const std::vector<BlockPiece>& holes);

#endif
