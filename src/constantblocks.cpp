#include "constantblocks.h"

#include <algorithm>

static bool sameConstant(const ScopeConstant* a, const ScopeConstant* b)
{
	return a->name == b->name && a->type == b->type && a->value == b->value && a->isSigned == b->isSigned;
}

static bool sameConstants(const std::vector<const ScopeConstant*>& a, const std::vector<const ScopeConstant*>& b)
{
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), sameConstant);
}

static bool hasName(const std::vector<const ScopeConstant*>& constants, const std::string& name)
{
	for (const ScopeConstant* constant : constants)
		if (constant->name == name)
			return true;
	return false;
}

static void appendSegment(uint64_t lo, uint64_t hi, const std::vector<const ScopeConstant*>& constants,
                          std::vector<ConstantSegment>& segments)
{
	ConstantSegment segment = { lo, hi };
	for (const ScopeConstant* constant : constants)
	{
		segment.constants.push_back(*constant);
		segment.constants.back().lo = lo;
		segment.constants.back().hi = hi;
	}
	segments.push_back(segment);
}

std::vector<ConstantSegment> splitConstantBlocks(const std::vector<ScopeConstant>& constants,
                                                 const std::vector<BlockPiece>& pieces, uint64_t lo, uint64_t hi,
                                                 std::vector<std::vector<ScopeConstant>>& pieceConstants)
{
	pieceConstants.assign(pieces.size(), std::vector<ScopeConstant>());
	std::vector<uint64_t> bounds;
	for (const ScopeConstant& constant : constants)
	{
		uint64_t clo = (std::max)(constant.lo, lo);
		uint64_t chi = (std::min)(constant.hi, hi);
		if (clo >= chi)
			continue;
		bounds.push_back(clo);
		bounds.push_back(chi);
		for (size_t i = 0; i < pieces.size(); i++)
		{
			ScopeConstant inner = constant;
			inner.lo = (std::max)(clo, pieces[i].pclo);
			inner.hi = (std::min)(chi, pieces[i].pchi);
			if (inner.lo < inner.hi)
				pieceConstants[i].push_back(inner);
		}
	}
	std::sort(bounds.begin(), bounds.end());
	bounds.erase(std::unique(bounds.begin(), bounds.end()), bounds.end());

	std::vector<BlockPiece> runs;
	std::vector<std::vector<const ScopeConstant*>> runConstants;
	for (size_t b = 0; b + 1 < bounds.size(); b++)
	{
		uint64_t blo = bounds[b];
		uint64_t bhi = bounds[b + 1];
		std::vector<const ScopeConstant*> here;
		for (const ScopeConstant& constant : constants)
			if (constant.lo <= blo && bhi <= constant.hi && !hasName(here, constant.name))
				here.push_back(&constant);
		if (here.empty())
			continue;

		if (!runs.empty() && runs.back().pchi == blo && sameConstants(here, runConstants[runs.back().block]))
			runs.back().pchi = bhi;
		else
		{
			BlockPiece run = { runConstants.size(), blo, bhi };
			runs.push_back(run);
			runConstants.push_back(here);
		}
	}

	std::vector<ConstantSegment> segments;
	for (const BlockPiece& part : cutOutPieces(runs, pieces))
		appendSegment(part.pclo, part.pchi, runConstants[part.block], segments);
	return segments;
}
