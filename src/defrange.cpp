#include "defrange.h"

#include <windows.h>
#include <algorithm>
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <tuple>

extern "C" {
	#include "mscvpdb.h"
}

namespace
{

enum
{
	CV_REG_AL = 1,
	CV_REG_BL = 4,
	CV_REG_AX = 9,
	CV_REG_EAX = 17,
	CV_REG_EBX = 20,
	CV_REG_EDI = 24,

	CV_AMD64_RAX = 328,
	CV_AMD64_RSP = 335,
	CV_AMD64_R8 = 336,
	CV_AMD64_R15 = 343,
	CV_AMD64_R8B = 344,
	CV_AMD64_R8W = 352,
	CV_AMD64_R8D = 360,

	CV_ARM64_W0 = 10,
	CV_ARM64_X0 = 50,
	CV_ARM64_LR = 80,
	CV_ARM64_S0 = 100,
	CV_ARM64_D0 = 140,
	CV_ARM64_Q0 = 180,
	CV_ARM64_Q31 = 211,
	CV_ARM64_B0 = 230,
	CV_ARM64_H0 = 270,
};

const uint16_t CV_LVARFLAG_ISPARAM = 0x1;
const uint16_t CV_LVARFLAG_ISOPTIMIZEDOUT = 0x100;

static_assert(sizeof(codeview_symbol::defrange_register_v3) == 16, "S_DEFRANGE_REGISTER record is not 16 bytes");
static_assert(sizeof(codeview_symbol::defrange_subfield_register_v3) == 20, "S_DEFRANGE_SUBFIELD_REGISTER record is not 20 bytes");
static_assert(sizeof(codeview_symbol::defrange_register_rel_v3) == 20, "S_DEFRANGE_REGISTER_REL record is not 20 bytes");
static_assert(sizeof(codeview_lvar_addr_gap) == 4, "CV_LVAR_ADDR_GAP is not 4 bytes");

unsigned registerWidth(unsigned size)
{
	return size <= 1 ? 1 : size <= 2 ? 2 : size <= 4 ? 4 : size <= 8 ? 8 : 16;
}

std::tuple<int, int, int, int, bool> key(const DefRangeLocation& loc)
{
	return std::make_tuple(loc.kind, loc.reg, loc.offset, loc.offsetParent, loc.spilledMember);
}

void appendPadding(std::vector<uint8_t>& out, size_t start)
{
	while ((out.size() - start) & 3)
		out.push_back((uint8_t)(0xf0 + 4 - ((out.size() - start) & 3)));
}

} // namespace

int cvSizedRegister(CVArch arch, int fullReg, unsigned size)
{
	unsigned width = registerWidth(size);
	switch (arch)
	{
	case CVArch::AMD64:
		if (fullReg >= CV_AMD64_RAX && fullReg <= CV_AMD64_RSP)
		{
			// CodeView numbers the 64-bit registers RAX, RBX, RCX, RDX, RSI, RDI, RBP, RSP, and
			// `byteRegs` and `wordRegs` list the narrower forms of each register in that order.
			static const int byteRegs[] = { 1, 4, 2, 3, 324, 325, 326, 327 };
			static const int wordRegs[] = { 9, 12, 10, 11, 15, 16, 14, 13 };
			int i = fullReg - CV_AMD64_RAX;
			switch (width)
			{
			case 1: return byteRegs[i];
			case 2: return wordRegs[i];
			case 4: return wordRegs[i] + (CV_REG_EAX - CV_REG_AX);
			case 8: return fullReg;
			default: return 0;
			}
		}
		if (fullReg >= CV_AMD64_R8 && fullReg <= CV_AMD64_R15)
		{
			int i = fullReg - CV_AMD64_R8;
			switch (width)
			{
			case 1: return CV_AMD64_R8B + i;
			case 2: return CV_AMD64_R8W + i;
			case 4: return CV_AMD64_R8D + i;
			case 8: return fullReg;
			default: return 0;
			}
		}
		return fullReg;

	case CVArch::X86:
		if (fullReg >= CV_REG_EAX && fullReg <= CV_REG_EDI)
		{
			if (width == 1 && fullReg <= CV_REG_EBX)
				return fullReg - CV_REG_EAX + CV_REG_AL;
			if (width <= 2)
				return fullReg - CV_REG_EAX + CV_REG_AX;
			return width == 4 ? fullReg : 0;
		}
		return fullReg;

	case CVArch::ARM64:
		if (fullReg >= CV_ARM64_X0 && fullReg <= CV_ARM64_LR)
		{
			if (width <= 4)
				return fullReg - CV_ARM64_X0 + CV_ARM64_W0;
			return width == 8 ? fullReg : 0;
		}
		if (fullReg >= CV_ARM64_Q0 && fullReg <= CV_ARM64_Q31)
		{
			int i = fullReg - CV_ARM64_Q0;
			switch (width)
			{
			case 1: return CV_ARM64_B0 + i;
			case 2: return CV_ARM64_H0 + i;
			case 4: return CV_ARM64_S0 + i;
			case 8: return CV_ARM64_D0 + i;
			default: return fullReg;
			}
		}
		return fullReg;
	}
	return fullReg;
}

bool DefRangeLocation::operator==(const DefRangeLocation& other) const
{
	return key(*this) == key(other);
}

bool DefRangeLocation::operator<(const DefRangeLocation& other) const
{
	return key(*this) < key(other);
}

void DefRangeEncoder::appendLocal(std::vector<uint8_t>& out, const char* name, uint32_t type, bool isParam,
                                  bool optimizedOut) const
{
	size_t start = out.size();
	size_t nameLen = strlen(name);
	size_t len = offsetof(codeview_symbol, local_v3.name) + nameLen + 1;
	out.resize(start + len);
	codeview_symbol* sym = (codeview_symbol*)&out[start];
	sym->local_v3.id = S_LOCAL_V3;
	sym->local_v3.symtype = type;
	sym->local_v3.flags = (isParam ? CV_LVARFLAG_ISPARAM : 0) | (optimizedOut ? CV_LVARFLAG_ISOPTIMIZEDOUT : 0);
	memcpy(sym->local_v3.name, name, nameLen + 1);
	appendPadding(out, start);
	assert(out.size() - start <= 0x8000);
	((codeview_symbol*)&out[start])->local_v3.len = (short)(out.size() - start - 2);
}

void DefRangeEncoder::appendDefRanges(std::vector<uint8_t>& out, std::vector<DefRange> ranges) const
{
	ranges.erase(std::remove_if(ranges.begin(), ranges.end(), [](const DefRange& r) { return r.start >= r.end; }),
	             ranges.end());
	std::sort(ranges.begin(), ranges.end(), [](const DefRange& a, const DefRange& b) {
		return a.loc == b.loc ? a.start < b.start : a.loc < b.loc;
	});

	std::vector<DefRange> merged;
	for (const DefRange& r : ranges)
	{
		if (!merged.empty() && merged.back().loc == r.loc && r.start <= merged.back().end)
			merged.back().end = (std::max)(merged.back().end, r.end);
		else
			merged.push_back(r);
	}

	// The records are grouped by location, and the groups are ordered by the first address of each
	// location.
	std::vector<std::pair<size_t, size_t>> groups;
	for (size_t i = 0; i < merged.size(); i++)
	{
		if (i == 0 || !(merged[i].loc == merged[i - 1].loc))
			groups.push_back(std::make_pair(i, i));
		groups.back().second = i + 1;
	}
	std::sort(groups.begin(), groups.end(), [&merged](const std::pair<size_t, size_t>& a, const std::pair<size_t, size_t>& b) {
		return merged[a.first].start < merged[b.first].start;
	});

	for (const auto& group : groups)
	{
		for (size_t i = group.first; i < group.second; )
		{
			uint32_t start = merged[i].start;
			size_t j = i + 1;
			while (j < group.second && merged[j].end - start <= kMaxRange && j - i - 1 < kMaxGaps)
				j++;
			uint32_t end = merged[j - 1].end;

			// A range longer than `kMaxRange` is split into consecutive records that list no gaps.
			for (uint32_t chunk = start; chunk < end; )
			{
				uint32_t length = (std::min)(kMaxRange, end - chunk);
				size_t recStart = out.size();
				size_t fixed = merged[i].loc.kind == DefRangeLocation::Register
				             ? sizeof(codeview_symbol::defrange_register_v3)
				             : merged[i].loc.kind == DefRangeLocation::SubfieldRegister
				             ? sizeof(codeview_symbol::defrange_subfield_register_v3)
				             : sizeof(codeview_symbol::defrange_register_rel_v3);
				out.resize(recStart + fixed + 4 * (j - i - 1));
				codeview_symbol* sym = (codeview_symbol*)&out[recStart];
				const DefRangeLocation& loc = merged[i].loc;
				codeview_lvar_addr_range* range;
				switch (loc.kind)
				{
				case DefRangeLocation::Register:
					sym->defrange_register_v3.id = S_DEFRANGE_REGISTER_V3;
					sym->defrange_register_v3.reg = loc.reg;
					sym->defrange_register_v3.attr = 0;
					range = &sym->defrange_register_v3.range;
					break;
				case DefRangeLocation::SubfieldRegister:
					sym->defrange_subfield_register_v3.id = S_DEFRANGE_SUBFIELD_REGISTER_V3;
					sym->defrange_subfield_register_v3.reg = loc.reg;
					sym->defrange_subfield_register_v3.attr = 0;
					sym->defrange_subfield_register_v3.offParent = loc.offsetParent & 0xfff;
					range = &sym->defrange_subfield_register_v3.range;
					break;
				default:
					sym->defrange_register_rel_v3.id = S_DEFRANGE_REGISTER_REL_V3;
					sym->defrange_register_rel_v3.baseReg = loc.reg;
					sym->defrange_register_rel_v3.flags = (loc.spilledMember ? 1 : 0) | ((loc.offsetParent & 0xfff) << 4);
					sym->defrange_register_rel_v3.offBasePointer = loc.offset;
					range = &sym->defrange_register_rel_v3.range;
					break;
				}
				range->offStart = chunk;
				range->isectStart = section;
				range->cbRange = (unsigned short)length;

				codeview_lvar_addr_gap* gaps = (codeview_lvar_addr_gap*)&out[recStart + fixed];
				for (size_t k = i + 1; k < j; k++)
				{
					gaps[k - i - 1].gapStartOffset = (unsigned short)(merged[k - 1].end - start);
					gaps[k - i - 1].cbRange = (unsigned short)(merged[k].start - merged[k - 1].end);
				}
				sym->generic.len = (short)(out.size() - recStart - 2);
				assert(out.size() - recStart <= 0x8000 && (out.size() - recStart) % 4 == 0);
				chunk += length;
			}
			i = j;
		}
	}
}
