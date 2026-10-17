#include "dwarflocals.h"

#include "PEImage.h"
#include "dwarf.h"

#include <algorithm>
#include <limits>

namespace
{

Location cfaRelative(const Location& cfa, int64_t off)
{
	if (!cfa.is_regrel() || cfa.reg == DW_REG_CFA)
		return Location{ Location::Invalid };
	return Location{ Location::RegRel, cfa.reg, cfa.off + off };
}

bool skipLEB128(byte*& p, byte* end)
{
	while (p < end)
		if (!(*p++ & 0x80))
			return true;
	return false;
}

bool skipBytes(byte*& p, byte* end, unsigned n)
{
	if ((size_t)(end - p) < n)
		return false;
	p += n;
	return true;
}

bool skipBlock(byte*& p, byte* end)
{
	byte* len = p;
	if (!skipLEB128(p, end))
		return false;
	return skipBytes(p, end, LEB128(len));
}

// Advances `p` past the operands of the operation `op`. Returns false for an operation whose
// operands are not known.
bool skipOperands(int op, byte*& p, byte* end, const DIECursor& cursor)
{
	if ((op >= DW_OP_lit0 && op <= DW_OP_lit31) || (op >= DW_OP_reg0 && op <= DW_OP_reg31))
		return true;
	if (op >= DW_OP_breg0 && op <= DW_OP_breg31)
		return skipLEB128(p, end);

	switch (op)
	{
	case DW_OP_deref:  case DW_OP_dup:   case DW_OP_drop:  case DW_OP_over:  case DW_OP_swap:
	case DW_OP_rot:    case DW_OP_xderef: case DW_OP_abs:  case DW_OP_and:   case DW_OP_div:
	case DW_OP_minus:  case DW_OP_mod:   case DW_OP_mul:   case DW_OP_neg:   case DW_OP_not:
	case DW_OP_or:     case DW_OP_plus:  case DW_OP_shl:   case DW_OP_shr:   case DW_OP_shra:
	case DW_OP_xor:    case DW_OP_eq:    case DW_OP_ge:    case DW_OP_gt:    case DW_OP_le:
	case DW_OP_lt:     case DW_OP_ne:    case DW_OP_nop:   case DW_OP_push_object_address:
	case DW_OP_form_tls_address: case DW_OP_call_frame_cfa: case DW_OP_stack_value:
	case DW_OP_GNU_push_tls_address: case DW_OP_GNU_uninit:
		return true;

	case DW_OP_const1u: case DW_OP_const1s: case DW_OP_pick: case DW_OP_deref_size: case DW_OP_xderef_size:
		return skipBytes(p, end, 1);
	case DW_OP_const2u: case DW_OP_const2s: case DW_OP_skip: case DW_OP_bra: case DW_OP_call2:
		return skipBytes(p, end, 2);
	case DW_OP_const4u: case DW_OP_const4s: case DW_OP_call4: case DW_OP_GNU_parameter_ref:
		return skipBytes(p, end, 4);
	case DW_OP_const8u: case DW_OP_const8s:
		return skipBytes(p, end, 8);
	case DW_OP_addr:
		return skipBytes(p, end, cursor.cu->address_size == 4 ? 4 : 8);
	case DW_OP_call_ref:
		return skipBytes(p, end, cursor.refSize());

	case DW_OP_constu: case DW_OP_consts: case DW_OP_plus_uconst: case DW_OP_regx: case DW_OP_fbreg:
	case DW_OP_piece: case DW_OP_addrx: case DW_OP_constx: case DW_OP_convert: case DW_OP_reinterpret:
	case DW_OP_GNU_convert: case DW_OP_GNU_reinterpret: case DW_OP_GNU_addr_index: case DW_OP_GNU_const_index:
		return skipLEB128(p, end);
	case DW_OP_bregx: case DW_OP_bit_piece: case DW_OP_regval_type: case DW_OP_GNU_regval_type:
		return skipLEB128(p, end) && skipLEB128(p, end);
	case DW_OP_deref_type: case DW_OP_xderef_type: case DW_OP_GNU_deref_type:
		return skipBytes(p, end, 1) && skipLEB128(p, end);
	case DW_OP_implicit_pointer: case DW_OP_GNU_implicit_pointer:
		return skipBytes(p, end, cursor.refSize()) && skipLEB128(p, end);
	case DW_OP_implicit_value: case DW_OP_entry_value: case DW_OP_GNU_entry_value:
		return skipBlock(p, end);
	case DW_OP_const_type: case DW_OP_GNU_const_type:
	{
		if (!skipLEB128(p, end) || p >= end)
			return false;
		unsigned size = *p++;
		return skipBytes(p, end, size);
	}

	default:
		return false;
	}
}

} // namespace

bool splitPieces(const DWARF_Attribute& expr, const DIECursor& cursor, std::vector<LocationPiece>& pieces)
{
	pieces.clear();
	byte* p = expr.expr.ptr;
	byte* end = p + expr.expr.len;
	byte* start = p;
	unsigned offset = 0;
	bool branches = false;
	while (p < end)
	{
		byte* opPtr = p;
		int op = *p++;
		if (op == DW_OP_bit_piece)
			return false;
		if (op == DW_OP_skip || op == DW_OP_bra)
			branches = true;
		if (!skipOperands(op, p, end, cursor))
			return false;
		if (op == DW_OP_piece)
		{
			byte* size = opPtr + 1;
			LocationPiece piece;
			piece.expr.type = Block;
			piece.expr.block.ptr = start;
			piece.expr.block.len = (unsigned)(opPtr - start);
			piece.offset = offset;
			piece.size = LEB128(size);
			pieces.push_back(piece);
			offset += piece.size;
			start = p;
		}
	}

	if (pieces.empty())
	{
		LocationPiece whole;
		whole.expr = expr;
		whole.offset = 0;
		whole.size = 0;
		pieces.push_back(whole);
		return true;
	}

	// Operations after the last DW_OP_piece belong to no piece, and a branch can jump from one
	// piece into another.
	return start == end && !branches;
}

LocalRangeResolver::LocalRangeResolver(const PEImage& img, const CFIIndex* cfiIndex, const DIECursor& procCursor,
                                       uint64_t scopeLo, uint64_t scopeHi, uint64_t sectionBase,
                                       const DWARF_Attribute& frameBaseAttr, const Location& frameBase,
                                       const Location& bestCFA)
	: img(img), cfiIndex(cfiIndex), procCursor(procCursor), scopeLo(scopeLo), scopeHi(scopeHi),
	  sectionBase(sectionBase), frameBaseAttr(frameBaseAttr), frameBase(frameBase), bestCFA(bestCFA)
{
	arch = img.isARM64() ? CVArch::ARM64 : img.isX64() ? CVArch::AMD64 : CVArch::X86;
	fbIsList = isLocationList(frameBaseAttr, procCursor);
}

bool LocalRangeResolver::isLocationList(const DWARF_Attribute& attr, const DIECursor& cursor)
{
	// DWARF 2 and 3 refer to a location list with a data4 or data8 offset, which reads as a constant.
	return attr.type == SecOffset || (attr.type == Const && cursor.cu && cursor.cu->version <= 3);
}

std::vector<DefRange> LocalRangeResolver::resolve(const DWARF_Attribute& location, const DIECursor& cursor,
                                                  unsigned typeSize)
{
	std::vector<ListEntry> entries;
	if (isLocationList(location, cursor))
		entries = readList(cursor, location);
	else
		entries.push_back(ListEntry{ scopeLo, scopeHi, location });

	std::vector<DefRange> ranges;
	std::vector<LocationPiece> pieces;
	for (const ListEntry& entry : entries)
	{
		if (!splitPieces(entry.expr, cursor, pieces))
			continue;
		// A single piece that covers the whole variable is the variable itself.
		if (pieces.size() == 1 && (pieces[0].size == 0 || pieces[0].size == typeSize))
		{
			LocalFragment fragment = { entry.lo, entry.hi, Location{ Location::Invalid }, true, 0, typeSize };
			resolvePieceRanges(fragment, pieces[0], ranges);
		}
		else
			resolveComposite(entry, pieces, typeSize, ranges);
	}
	return ranges;
}

void LocalRangeResolver::resolvePieceRanges(const LocalFragment& fragment, const LocationPiece& piece,
                                            std::vector<DefRange>& out)
{
	if (piece.expr.block.len == 0)
		return;
	std::vector<LocalFragment> fragments;
	resolvePiece(fragment, piece, fragments);
	for (const LocalFragment& resolved : fragments)
		emitResolved(resolved, out);
}

// dbgeng was seen to read the missing pieces of a composite location from the memory next to a
// spilled piece, so the records cover only the addresses where every piece has a location.
void LocalRangeResolver::resolveComposite(const ListEntry& entry, const std::vector<LocationPiece>& pieces,
                                          unsigned typeSize, std::vector<DefRange>& out)
{
	std::vector<std::vector<DefRange>> pieceRanges(pieces.size());
	std::vector<uint32_t> bounds;
	for (size_t i = 0; i < pieces.size(); i++)
	{
		LocalFragment fragment = { entry.lo, entry.hi, Location{ Location::Invalid }, false, pieces[i].offset,
		                           pieces[i].size ? pieces[i].size : typeSize };
		resolvePieceRanges(fragment, pieces[i], pieceRanges[i]);
		if (pieceRanges[i].empty())
			return;
		std::sort(pieceRanges[i].begin(), pieceRanges[i].end(),
		          [](const DefRange& a, const DefRange& b) { return a.start < b.start; });
		for (const DefRange& r : pieceRanges[i])
		{
			bounds.push_back(r.start);
			bounds.push_back(r.end);
		}
	}
	std::sort(bounds.begin(), bounds.end());
	bounds.erase(std::unique(bounds.begin(), bounds.end()), bounds.end());

	const LocationPiece& last = pieces.back();
	bool coversType = typeSize && last.offset + last.size == typeSize;
	std::vector<size_t> next(pieces.size(), 0);
	std::vector<const DefRangeLocation*> at(pieces.size());
	for (size_t b = 0; b + 1 < bounds.size(); b++)
	{
		uint32_t lo = bounds[b];
		uint32_t hi = bounds[b + 1];
		bool everyPiece = true;
		bool oneBlock = coversType;
		for (size_t i = 0; i < pieces.size(); i++)
		{
			const std::vector<DefRange>& r = pieceRanges[i];
			while (next[i] < r.size() && r[next[i]].end <= lo)
				next[i]++;
			everyPiece = next[i] < r.size() && r[next[i]].start <= lo;
			if (!everyPiece)
				break;
			at[i] = &r[next[i]].loc;
			oneBlock = oneBlock && at[i]->kind == DefRangeLocation::RegisterRel && at[i]->reg == at[0]->reg &&
			           (int64_t)at[i]->offset - at[0]->offset == (int64_t)pieces[i].offset;
		}
		if (!everyPiece)
			continue;

		// The `??` command of dbgeng was seen to show a wrong value for a spilled piece, so pieces
		// that lie in order in one block of memory are written as the whole variable at the start of
		// the block.
		if (oneBlock)
		{
			DefRangeLocation whole = *at[0];
			whole.offsetParent = 0;
			whole.spilledMember = false;
			out.push_back(DefRange{ lo, hi, whole });
		}
		else
			for (const DefRangeLocation* loc : at)
				out.push_back(DefRange{ lo, hi, *loc });
	}
}

// Reads the location list that `attr` refers to. Entries are clamped to [scopeLo, scopeHi), and
// where entries overlap, the first one is used. A default entry is used at the addresses that no
// other entry covers.
std::vector<LocalRangeResolver::ListEntry> LocalRangeResolver::readList(const DIECursor& cursor,
                                                                        const DWARF_Attribute& attr) const
{
	std::vector<ListEntry> entries;
	std::vector<std::pair<uint64_t, uint64_t>> covered;
	bool hasDefault = false;
	DWARF_Attribute defaultExpr = {};

	LOCCursor list(cursor, attr.type == SecOffset ? attr.sec_offset : attr.cons);
	LOCEntry entry;
	while (list.readNext(entry))
	{
		if (entry.isDefault)
		{
			if (!hasDefault)
				defaultExpr = entry.expr;
			hasDefault = true;
			continue;
		}
		addUncovered((std::max)(entry.beg_offset, scopeLo), (std::min)(entry.end_offset, scopeHi), entry.expr,
		             covered, entries);
	}
	if (hasDefault)
		addUncovered(scopeLo, scopeHi, defaultExpr, covered, entries);
	return entries;
}

// Appends to `entries` the parts of [lo, hi) outside the sorted, disjoint ranges of `covered`,
// then adds [lo, hi) to `covered`.
void LocalRangeResolver::addUncovered(uint64_t lo, uint64_t hi, const DWARF_Attribute& expr,
                                      std::vector<std::pair<uint64_t, uint64_t>>& covered,
                                      std::vector<ListEntry>& entries) const
{
	if (lo >= hi)
		return;

	uint64_t pos = lo;
	for (const auto& c : covered)
	{
		if (c.first >= hi)
			break;
		if (c.second <= pos)
			continue;
		if (c.first > pos)
			entries.push_back(ListEntry{ pos, c.first, expr });
		pos = c.second;
	}
	if (pos < hi)
		entries.push_back(ListEntry{ pos, hi, expr });

	auto it = covered.insert(std::lower_bound(covered.begin(), covered.end(), std::make_pair(lo, hi)),
	                         std::make_pair(lo, hi));
	if (it != covered.begin() && (it - 1)->second >= it->first)
	{
		(it - 1)->second = (std::max)((it - 1)->second, it->second);
		it = covered.erase(it) - 1;
	}
	while (it + 1 != covered.end() && (it + 1)->first <= it->second)
	{
		it->second = (std::max)(it->second, (it + 1)->second);
		covered.erase(it + 1);
	}
}

// A piece that uses DW_OP_fbreg is decoded relative to the frame base. When the frame base is a
// location list, the piece has one location for each entry of that list.
void LocalRangeResolver::resolvePiece(const LocalFragment& fragment, const LocationPiece& piece,
                                      std::vector<LocalFragment>& out)
{
	LocalFragment resolved = fragment;
	resolved.loc = decodeLocation(piece.expr);
	if (!resolved.loc.is_invalid() || !fbIsList)
	{
		if (resolved.loc.is_invalid())
			resolved.loc = decodeLocation(piece.expr, &frameBase);
		out.push_back(resolved);
		return;
	}

	if (!fbEntriesRead)
	{
		fbEntries = readList(procCursor, frameBaseAttr);
		fbEntriesRead = true;
	}
	for (const ListEntry& fb : fbEntries)
	{
		resolved.lo = (std::max)(fragment.lo, fb.lo);
		resolved.hi = (std::min)(fragment.hi, fb.hi);
		if (resolved.lo >= resolved.hi)
			continue;
		Location fbLoc = decodeLocation(fb.expr);
		resolved.loc = decodeLocation(piece.expr, &fbLoc);
		out.push_back(resolved);
	}
}

// A location relative to the CFA has one location for each row of the call frame information.
void LocalRangeResolver::emitResolved(const LocalFragment& fragment, std::vector<DefRange>& out)
{
	if (!fragment.loc.is_regrel() || fragment.loc.reg != DW_REG_CFA)
	{
		emitLocation(fragment.lo, fragment.hi, fragment.loc, fragment, out);
		return;
	}

	if (!cfaRowsRead)
	{
		cfaRows = getCFARows(img, cfiIndex, scopeLo, scopeHi);
		cfaRowsRead = true;
	}
	if (cfaRows.empty())
	{
		emitLocation(fragment.lo, fragment.hi, cfaRelative(bestCFA, fragment.loc.off), fragment, out);
		return;
	}
	for (const CFARow& row : cfaRows)
	{
		uint64_t lo = (std::max)(fragment.lo, row.lo);
		uint64_t hi = (std::min)(fragment.hi, row.hi);
		if (lo < hi)
			emitLocation(lo, hi, cfaRelative(row.cfa, fragment.loc.off), fragment, out);
	}
}

void LocalRangeResolver::emitLocation(uint64_t lo, uint64_t hi, const Location& loc, const LocalFragment& fragment,
                                      std::vector<DefRange>& out) const
{
	// CodeView stores the offset of a piece in 12 bits.
	if (!fragment.whole && fragment.pieceOffset >= 0x1000)
		return;

	DefRangeLocation def = {};
	def.offsetParent = fragment.whole ? 0 : (uint16_t)fragment.pieceOffset;
	if (loc.is_inreg())
	{
		int reg = dwarfToCVRegister(img, loc.reg);
		if (reg && fragment.size)
			reg = cvSizedRegister(arch, reg, fragment.size);
		if (!reg)
			return;
		def.kind = fragment.whole ? DefRangeLocation::Register : DefRangeLocation::SubfieldRegister;
		def.reg = (uint16_t)reg;
	}
	else if (loc.is_regrel())
	{
		int reg = dwarfToCVRegister(img, loc.reg);
		if (!reg || loc.off < (std::numeric_limits<int32_t>::min)() || loc.off > (std::numeric_limits<int32_t>::max)())
			return;
		def.kind = DefRangeLocation::RegisterRel;
		def.reg = (uint16_t)reg;
		def.offset = (int32_t)loc.off;
		def.spilledMember = !fragment.whole;
	}
	else
		return;

	out.push_back(DefRange{ (uint32_t)(lo - sectionBase), (uint32_t)(hi - sectionBase), def });
}
