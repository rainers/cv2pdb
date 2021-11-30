#ifndef __READDWARF_H__
#define __READDWARF_H__

#include <Windows.h>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include "mspdb.h"

typedef unsigned char byte;
class PEImage;
class DIECursor;

enum DebugLevel : unsigned {
	DbgBasic = 0x1,
	DbgPdbTypes = 0x2,
	DbgPdbSyms = 0x4,
	DbgPdbLines = 0x8,
	DbgDwarfTagRead = 0x10,
	DbgDwarfAttrRead = 0x20,
	DbgDwarfLocLists = 0x40,
	DbgDwarfLines = 0x80
};

DEFINE_ENUM_FLAG_OPERATORS(DebugLevel);

inline unsigned int LEB128(byte* &p)
{
	unsigned int x = 0;
	int shift = 0;
	while (*p & 0x80)
	{
		x |= (*p & 0x7f) << shift;
		shift += 7;
		p++;
	}
	x |= *p << shift;
	p++;
	return x;
}

inline int SLEB128(byte* &p)
{
	unsigned int x = 0;
	int shift = 0;
	while (*p & 0x80)
	{
		x |= (*p & 0x7f) << shift;
		shift += 7;
		p++;
	}
	x |= *p << shift;
	if (*p & 0x40)
		x |= -(1 << (shift + 7)); // sign extend
	p++;
	return x;
}

inline unsigned int RD2(byte* &p)
{
	unsigned int x = *p++;
	x |= *p++ << 8;
	return x;
}

inline unsigned int RD4(byte* &p)
{
	unsigned int x = *p++;
	x |= *p++ << 8;
	x |= *p++ << 16;
	x |= *p++ << 24;
	return x;
}

inline unsigned long long RD8(byte* &p)
{
	unsigned long long x = *p++;
	for (int shift = 8; shift < 64; shift += 8)
		x |= (unsigned long long) *p++ << shift;
	return x;
}

inline unsigned long long RDsize(byte* &p, int size)
{
	if (size > 8)
		size = 8;
	unsigned long long x = *p++;
	for (int shift = 8; shift < size * 8; shift += 8)
		x |= (unsigned long long) *p++ << shift;
	return x;
}

enum AttrClass
{
	Invalid,
	Addr,
	Block,
	Const,
	String,
	Flag,
	Ref,
	ExprLoc,
	SecOffset
};

struct DWARF_Attribute
{
	AttrClass type;
	union
	{
		unsigned long addr;
		struct { byte* ptr; unsigned len; } block;
		unsigned long cons;
		const char* string;
		bool flag;
		byte* ref;
		struct { byte* ptr; unsigned len; } expr;
		unsigned long sec_offset;
	};
};

///////////////////////////////////////////////////////////////////////////////

#include "pshpack1.h"

struct DWARF_CompilationUnit
{
	unsigned int unit_length; // 12 byte in DWARF-64
	unsigned short version;
	unsigned int debug_abbrev_offset; // 8 byte in DWARF-64
	byte address_size;

	bool isDWARF64() const { return unit_length == ~0; }
	int refSize() const { return unit_length == ~0 ? 8 : 4; }
};

struct DWARF_FileName
{
	const char* file_name;
	unsigned int  dir_index;
	unsigned long lastModification;
	unsigned long fileLength;

	void read(byte* &p)
	{
		file_name = (const char*)p;
		p += strlen((const char*)p) + 1;
		dir_index = LEB128(p);
		lastModification = LEB128(p);
		fileLength = LEB128(p);
	}
};

struct DWARF_InfoData
{
	byte* entryPtr;
	int code;
	byte* abbrev;
	int tag;
	int hasChild;

	const char* name;
	const char* linkage_name;
	const char* dir;
	unsigned long byte_size;
	byte* sibling;
	unsigned long encoding;
	unsigned long pclo;
	unsigned long pchi;
	unsigned long ranges; // -1u when attribute is not present
	unsigned long pcentry;
	byte* type;
	byte* containing_type;
	byte* specification;
	byte* abstract_origin;
	unsigned long inlined;
	bool external;
	DWARF_Attribute location;
	DWARF_Attribute member_location;
	DWARF_Attribute frame_base;
	long upper_bound;
	long lower_bound;
	bool has_lower_bound;
	unsigned language;
	unsigned long const_value;
	bool has_const_value;
	bool is_artificial;
	bool has_artificial;

	void clear()
	{
		entryPtr = 0;
		code = 0;
		abbrev = 0;
		tag = 0;
		hasChild = 0;

		name = 0;
		linkage_name = 0;
		dir = 0;
		byte_size = 0;
		sibling = 0;
		encoding = 0;
		pclo = 0;
		pchi = 0;
		ranges = ~0;
		pcentry = 0;
		type = 0;
		containing_type = 0;
		specification = 0;
		abstract_origin = 0;
		inlined = 0;
		external = 0;
		member_location.type = Invalid;
		location.type = Invalid;
		frame_base.type = Invalid;
		upper_bound = 0;
		lower_bound = 0;
		has_lower_bound = false;
		language = 0;
		const_value = 0;
		has_const_value = false;
		is_artificial = false;
		has_artificial = false;
	}

	void merge(const DWARF_InfoData& id)
	{
		if (!name) name = id.name;
		if (!linkage_name) linkage_name = id.linkage_name;
		if (!dir) dir = id.dir;
		if (!byte_size) byte_size = id.byte_size;
		if (!sibling) sibling = id.sibling;
		if (!encoding) encoding = id.encoding;
		if (!pclo) pclo = id.pclo;
		if (!pchi) pchi = id.pchi;
		if (ranges == ~0) ranges = id.ranges;
		if (!pcentry) pcentry = id.pcentry;
		if (!type) type = id.type;
		if (!containing_type) containing_type = id.containing_type;
		if (!specification) specification = id.specification;
		if (!abstract_origin) abstract_origin = id.abstract_origin;
		if (!inlined) inlined = id.inlined;
		if (!external) external = id.external;
		if (member_location.type == Invalid) member_location = id.member_location;
		if (location.type == Invalid) location = id.location;
		if (frame_base.type == Invalid) frame_base = id.frame_base;
		if (!upper_bound) upper_bound = id.upper_bound;
		if (!has_lower_bound) { lower_bound = id.lower_bound; has_lower_bound = id.has_lower_bound; }
		if (!has_const_value) { const_value = id.const_value; has_const_value = id.has_const_value; }
		if (!has_artificial) { is_artificial = id.is_artificial; has_artificial = id.has_artificial; }
	}
};

static const int maximum_operations_per_instruction = 1;

struct DWARF_TypeForm
{
	unsigned int type, form;
};

struct DWARF_LineNumberProgramHeader
{
	unsigned int unit_length; // 12 byte in DWARF-64
	unsigned short version;
	byte address_size; // new in DWARF5
	byte segment_selector_size; // new in DWARF5
	unsigned int header_length; // 8 byte in DWARF-64
	byte minimum_instruction_length;
	byte maximum_operations_per_instruction; // not in DWARF 2/3
	byte default_is_stmt;
	signed char line_base;
	byte line_range;
	byte opcode_base;
	//LEB128 standard_opcode_lengths[opcode_base];
	// string include_directories[] // zero byte terminated
	// DWARF_FileNames file_names[] // zero byte terminated
};

struct DWARF4_LineNumberProgramHeader
{
	unsigned int unit_length; // 12 byte in DWARF-64
	unsigned short version;
	unsigned int header_length; // 8 byte in DWARF-64
	byte minimum_instruction_length;
	byte maximum_operations_per_instruction; // not in DWARF 2/3
	byte default_is_stmt;
	signed char line_base;
	byte line_range;
	byte opcode_base;
	//LEB128 standard_opcode_lengths[opcode_base];
	// string include_directories[] // zero byte terminated
	// DWARF_FileNames file_names[] // zero byte terminated
};

struct DWARF2_LineNumberProgramHeader
{
	unsigned int unit_length; // 12 byte in DWARF-64
	unsigned short version;
	unsigned int header_length; // 8 byte in DWARF-64
	byte minimum_instruction_length;
	//byte maximum_operations_per_instruction; (// not in DWARF 2
	byte default_is_stmt;
	signed char line_base;
	byte line_range;
	byte opcode_base;
	//LEB128 standard_opcode_lengths[opcode_base];
	// string include_directories[] // zero byte terminated
	// DWARF_FileNames file_names[] // zero byte terminated
};

struct DWARF_LineState
{
	// hdr info
	std::vector<const char*> include_dirs;
	std::vector<DWARF_FileName> files;

	unsigned long address;
	unsigned int  op_index;
	unsigned int  file;
	unsigned int  line;
	unsigned int  column;
	bool          is_stmt;
	bool          basic_block;
	bool          end_sequence;
	bool          prologue_end;
	bool          epilogue_end;
	unsigned int  isa;
	unsigned int  discriminator;

	// not part of the "documented" state
	DWARF_FileName* file_ptr;
	unsigned long seg_offset;
	unsigned long section;
	unsigned long last_addr;
	std::vector<mspdb::LineInfoEntry> lineInfo;
	unsigned int lineInfo_file;

	DWARF_LineState()
	{
		file_ptr = nullptr;
		seg_offset = 0x400000;
		last_addr = 0;
		lineInfo_file = 0;

		init(0);
	}

	void init(DWARF_LineNumberProgramHeader* hdr)
	{
        section = -1;
		address = 0;
		op_index = 0;
		file = 1;
		line = 1;
		column = 0;
		is_stmt = hdr && hdr->default_is_stmt != 0;
		basic_block = false;
		end_sequence = false;
		prologue_end = false;
		epilogue_end = false;
		isa = 0;
		discriminator = 0;
	}

	void advance_addr(DWARF_LineNumberProgramHeader* hdr, int operation_advance)
	{
		int address_advance = hdr->minimum_instruction_length * ((op_index + operation_advance) / maximum_operations_per_instruction);
		address += address_advance;
		op_index = (op_index + operation_advance) % maximum_operations_per_instruction;
	}
};

#include "poppack.h"

///////////////////////////////////////////////////////////////////////////////

#define DW_REG_CFA 257

struct Location
{
	enum Type
	{
		Invalid, // Failed to evaluate the location expression
		InReg,   // In register (reg)
		Abs,     // Absolute address (off)
		RegRel   // Register-relative address ($reg + off)
	};

	Type type;
	int reg;
	int off;

	bool is_invalid() const { return type == Invalid; }
	bool is_inreg() const { return type == InReg; }
	bool is_abs() const { return type == Abs; }
	bool is_regrel() const { return type == RegRel; }
};

typedef std::unordered_map<std::pair<unsigned, unsigned>, byte*> abbrevMap_t;


// Attempts to partially evaluate DWARF location expressions.
// The only supported expressions are those, whose result may be represented
// as either an absolute value, a register, or a register-relative address.
Location decodeLocation(const PEImage& img, const DWARF_Attribute& attr, const Location* frameBase = 0, int at = 0);

void mergeAbstractOrigin(DWARF_InfoData& id, const DIECursor& parent);
void mergeSpecification(DWARF_InfoData& id, const DIECursor& parent);

// Debug Information Entry Cursor
class DIECursor
{
public:
	DWARF_CompilationUnit* cu;
	byte* ptr;
	unsigned int entryOff;
	int level;
	bool hasChild; // indicates whether the last read DIE has children
	byte* sibling;

	static PEImage *img;
	static abbrevMap_t abbrevMap;
	static DebugLevel debug;

	byte* getDWARFAbbrev(unsigned off, unsigned findcode);

public:

	static void setContext(PEImage* img_, DebugLevel debug_);

	// Create a new DIECursor
	DIECursor(DWARF_CompilationUnit* cu_, byte* ptr);

	// Create a child DIECursor
	DIECursor(const DIECursor& parent, byte* ptr_);

	// Goto next sibling DIE.  If the last read DIE had any children, they will be skipped over.
	void gotoSibling();

	// Reads next sibling DIE.  If the last read DIE had any children, they will be skipped over.
	// Returns 'false' upon reaching the last sibling on the current level.
	bool readSibling(DWARF_InfoData& id);

	// Returns cursor that will enumerate children of the last read DIE.
	DIECursor getSubtreeCursor();

	// Reads the next DIE in physical order, returns 'true' if succeeds.
	// If stopAtNull is true, readNext() will stop upon reaching a null DIE (end of the current tree level).
	// Otherwise, it will skip null DIEs and stop only at the end of the subtree for which this DIECursor was created.
	bool readNext(DWARF_InfoData& id, bool stopAtNull = false);

	// Read an address from p according to the ambient pointer size.
	uint64_t RDAddr(byte* &p) const
	{
		if (cu->address_size == 4)
			return RD4(p);

		return RD8(p);
	}
};

// iterate over DWARF debug_line information
// if mod is null, print them out, otherwise add to module
bool interpretDWARFLines(const PEImage& img, mspdb::Mod* mod, DebugLevel debug = DebugLevel{});

#endif
