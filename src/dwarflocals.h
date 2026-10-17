#ifndef __DWARFLOCALS_H__
#define __DWARFLOCALS_H__

#include "defrange.h"
#include "readDwarf.h"

#include <utility>
#include <vector>

class CFIIndex;
class PEImage;

// The CFA over the addresses [lo, hi), from one row of the call frame information.
struct CFARow
{
	uint64_t lo;
	uint64_t hi;
	Location cfa;
};

// Returns the rows of the frame description entry that covers [pclo, pchi), in address order.
// Returns no rows when the image has no such entry.
std::vector<CFARow> getCFARows(const PEImage& img, const CFIIndex* index, uint64_t pclo, uint64_t pchi);

// Returns the CodeView register for the DWARF register `reg` of the architecture of `img`, or 0
// for a register that has none. On x86 the function returns 0 for every register other than the
// general purpose ones.
int dwarfToCVRegister(const PEImage& img, int reg);

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

// The location of a variable, or of one piece of it, over the addresses [lo, hi).
struct LocalFragment
{
	uint64_t lo;
	uint64_t hi;
	Location loc;

	// Set when the location stores the whole variable rather than one piece of it.
	bool whole;

	unsigned pieceOffset;

	// The size of the piece, or of the whole variable, in bytes; 0 when it is not known.
	unsigned size;
};

// Finds where the locations of the variables of one function are valid, inside the first address
// range of the function.
class LocalRangeResolver
{
public:
	// [scopeLo, scopeHi) is the first address range of the function, and every location is clamped
	// to it. `sectionBase` is the address that the returned code section offsets count from.
	// `frameBaseAttr` is the DW_AT_frame_base attribute of the function. `frameBase` is the location
	// decoded from that attribute, and it is used only when the attribute is not a location list.
	// `bestCFA` is used as the CFA when the call frame information has no rows for the function.
	LocalRangeResolver(const PEImage& img, const CFIIndex* cfiIndex, const DIECursor& procCursor,
	                   uint64_t scopeLo, uint64_t scopeHi, uint64_t sectionBase,
	                   const DWARF_Attribute& frameBaseAttr, const Location& frameBase, const Location& bestCFA);

	// Returns whether `attr`, a DW_AT_location or DW_AT_frame_base attribute, refers to a location list.
	static bool isLocationList(const DWARF_Attribute& attr, const DIECursor& cursor);

	// Returns the locations of the variable whose DW_AT_location is `location` and whose type is
	// `typeSize` bytes, over code section offsets. Addresses where no location is valid, or where it
	// has no CodeView form, are left out, as are addresses where some piece of a composite location
	// is missing.
	std::vector<DefRange> resolve(const DWARF_Attribute& location, const DIECursor& cursor, unsigned typeSize);

private:
	struct ListEntry
	{
		uint64_t lo;
		uint64_t hi;
		DWARF_Attribute expr;
	};

	std::vector<ListEntry> readList(const DIECursor& cursor, const DWARF_Attribute& attr) const;
	void addUncovered(uint64_t lo, uint64_t hi, const DWARF_Attribute& expr,
	                  std::vector<std::pair<uint64_t, uint64_t>>& covered, std::vector<ListEntry>& entries) const;
	void resolvePieceRanges(const LocalFragment& fragment, const LocationPiece& piece, std::vector<DefRange>& out);
	void resolveComposite(const ListEntry& entry, const std::vector<LocationPiece>& pieces, unsigned typeSize,
	                      std::vector<DefRange>& out);
	void resolvePiece(const LocalFragment& fragment, const LocationPiece& piece, std::vector<LocalFragment>& out);
	void emitResolved(const LocalFragment& fragment, std::vector<DefRange>& out);
	void emitLocation(uint64_t lo, uint64_t hi, const Location& loc, const LocalFragment& fragment,
	                  std::vector<DefRange>& out) const;

	const PEImage& img;
	const CFIIndex* cfiIndex;
	DIECursor procCursor;
	uint64_t scopeLo;
	uint64_t scopeHi;
	uint64_t sectionBase;
	DWARF_Attribute frameBaseAttr;
	Location frameBase;
	Location bestCFA;
	CVArch arch;
	bool fbIsList;

	bool fbEntriesRead = false;
	std::vector<ListEntry> fbEntries;
	bool cfaRowsRead = false;
	std::vector<CFARow> cfaRows;
};

#endif
