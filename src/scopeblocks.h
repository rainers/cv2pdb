#ifndef __SCOPEBLOCKS_H__
#define __SCOPEBLOCKS_H__

#include "readDwarf.h"

#include <utility>
#include <vector>

// DWARF scopes of one kind, either lexical blocks or inlined calls. Each element of `ranges` lists
// the address ranges of one scope, and the element of `children` at the same index reads the
// children of that scope.
struct ScopeBlocks
{
	std::vector<std::vector<RangeEntry>> ranges;
	std::vector<DIECursor> children;

	void add(std::vector<RangeEntry>&& blockRanges, const DIECursor& blockChildren)
	{
		ranges.push_back(std::move(blockRanges));
		children.push_back(blockChildren);
	}

	// Returns the scopes that have an address range that overlaps [lo, hi).
	ScopeBlocks overlapping(uint64_t lo, uint64_t hi) const
	{
		ScopeBlocks found;
		for (size_t i = 0; i < ranges.size(); i++)
		{
			for (const RangeEntry& range : ranges[i])
			{
				if (range.pclo < hi && lo < range.pchi)
				{
					found.ranges.push_back(ranges[i]);
					found.children.push_back(children[i]);
					break;
				}
			}
		}
		return found;
	}
};

#endif
