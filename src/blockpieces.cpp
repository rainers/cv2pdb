#include "blockpieces.h"

#include <algorithm>

static bool startsBefore(const BlockPiece& a, const BlockPiece& b)
{
	return a.pclo < b.pclo;
}

std::vector<BlockPiece> splitSiblingBlocks(const std::vector<std::vector<RangeEntry>>& blockRanges,
                                           uint64_t lo, uint64_t hi)
{
	std::vector<BlockPiece> ranges;
	for (size_t block = 0; block < blockRanges.size(); block++)
	{
		for (const RangeEntry& range : blockRanges[block])
		{
			BlockPiece piece = { block, range.pclo < lo ? lo : range.pclo, range.pchi > hi ? hi : range.pchi };
			if (piece.pclo < piece.pchi)
				ranges.push_back(piece);
		}
	}
	std::stable_sort(ranges.begin(), ranges.end(), startsBefore);

	std::vector<BlockPiece> pieces;
	for (const BlockPiece& range : ranges)
	{
		if (!pieces.empty() && pieces.back().block == range.block)
		{
			if (pieces.back().pchi < range.pchi)
				pieces.back().pchi = range.pchi;
		}
		else
			pieces.push_back(range);
	}
	return pieces;
}
