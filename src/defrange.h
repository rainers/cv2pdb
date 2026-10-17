#ifndef __DEFRANGE_H__
#define __DEFRANGE_H__

#include <stdint.h>
#include <vector>

// The architecture whose CodeView register numbers a record uses.
enum class CVArch { X86, AMD64, ARM64 };

// Returns the CodeView register for the low bytes of the full register `fullReg`, in the narrowest
// form that has at least `size` bytes. Only general purpose registers and the ARM64 vector
// registers are narrowed, and any other register is returned unchanged. Returns 0 when `fullReg`
// is a general purpose register narrower than `size`.
int cvSizedRegister(CVArch arch, int fullReg, unsigned size);

// Where a variable, or a piece of it, is stored.
struct DefRangeLocation
{
	enum Kind
	{
		Register,         // the whole variable is in `reg`
		RegisterRel,      // the whole variable, or the piece at `offsetParent`, is in memory at `reg` + `offset`
		SubfieldRegister  // the piece at `offsetParent` is in `reg`
	};

	Kind kind;
	uint16_t reg;
	int32_t offset;
	uint16_t offsetParent;

	// Set for a RegisterRel location that stores a piece rather than the whole variable.
	bool spilledMember;

	bool operator==(const DefRangeLocation& other) const;
	bool operator<(const DefRangeLocation& other) const;
};

// A location of a variable over the code section offsets [start, end).
struct DefRange
{
	uint32_t start;
	uint32_t end;
	DefRangeLocation loc;
};

class DefRangeEncoder
{
public:
	// The longest address range that one record describes.
	static const uint32_t kMaxRange = 0xF000;

	// The most gaps that one record lists, which keeps the record within 0x8000 bytes, because
	// `CV2PDB::copySymbols` reads the record length as a signed short.
	static const uint32_t kMaxGaps = (0x8000 - 20) / 4;

	// `section` is the 1-based number of the code section that the ranges are in.
	explicit DefRangeEncoder(uint16_t section) : section(section) {}

	// Appends an S_LOCAL record. The S_DEFRANGE_* records appended after it give the locations of
	// the variable.
	void appendLocal(std::vector<uint8_t>& out, const char* name, uint32_t type, bool isParam,
	                 bool optimizedOut) const;

	// Appends the S_DEFRANGE_* records for `ranges`. Overlapping or adjacent ranges with the same
	// location are merged. One record can cover several ranges with the same location, and it lists
	// the addresses between them as gaps.
	void appendDefRanges(std::vector<uint8_t>& out, std::vector<DefRange> ranges) const;

private:
	uint16_t section;
};

#endif
