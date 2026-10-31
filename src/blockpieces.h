#ifndef __BLOCKPIECES_H__
#define __BLOCKPIECES_H__

#include "readDwarf.h"

#include <stddef.h>
#include <stdint.h>
#include <vector>

// One S_BLOCK32 record to write, spanning [pclo, pchi), for the block at index `block` of the
// `blockRanges` argument of `splitSiblingBlocks`.
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

#endif
