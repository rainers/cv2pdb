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

static bool endsAfter(uint64_t pc, const BlockPiece& piece)
{
	return pc < piece.pchi;
}

std::vector<BlockPiece> cutOutPieces(const std::vector<BlockPiece>& pieces,
                                     const std::vector<BlockPiece>& holes)
{
	std::vector<BlockPiece> parts;
	for (const BlockPiece& piece : pieces)
	{
		uint64_t pc = piece.pclo;
		size_t next = std::upper_bound(holes.begin(), holes.end(), pc, endsAfter) - holes.begin();
		for (; pc < piece.pchi; next++)
		{
			uint64_t end = next < holes.size() ? (std::min)(piece.pchi, holes[next].pclo) : piece.pchi;
			if (pc < end)
			{
				BlockPiece part = { piece.block, pc, end };
				parts.push_back(part);
			}
			if (next >= holes.size())
				break;
			pc = holes[next].pchi;
		}
	}
	return parts;
}
