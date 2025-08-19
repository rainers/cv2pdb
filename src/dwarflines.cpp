// Convert DMD CodeView/DWARF debug information to PDB files
// Copyright (c) 2009-2012 by Rainer Schuetze, All Rights Reserved
//
// License for redistribution is given by the Artistic License 2.0
// see file LICENSE for further details
//

#include <assert.h>
#include "PEImage.h"
#include "mspdb.h"
#include "dwarf.h"
#include "readDwarf.h"

static DebugLevel debug;

bool isRelativePath(const std::string& s)
{
	if(s.length() < 1)
		return true;
	if(s[0] == '/' || s[0] == '\\')
		return false;
	if(s.length() < 2)
		return true;
	if(s[1] == ':')
		return false;
	return true;
}

static void addTrailingSlash(std::string& dir)
{
	// Make sure dirs always end in a trailing slash
	if (!dir.size() || (dir.back() != '\\' && dir.back() != '/'))
		dir += '\\';
}

static int cmpAdr(const void* s1, const void* s2)
{
	const mspdb::LineInfoEntry* e1 = (const mspdb::LineInfoEntry*) s1;
	const mspdb::LineInfoEntry* e2 = (const mspdb::LineInfoEntry*) s2;
	return e1->offset - e2->offset;
}


bool printLines(char const *fname, unsigned short sec, char const *secname, unsigned int low_line,
                mspdb::LineInfoEntry* pLineInfo, long numLineInfo)
{
    printf("Sym: %s\n", secname ? secname : "<none>");
    printf("File: %s\n", fname);
    for (int i = 0; i < numLineInfo; i++)
        printf("\tOff 0x%x: Line %d\n", pLineInfo[i].offset, pLineInfo[i].line + low_line);
    return true;
}


bool _flushDWARFLines(const PEImage& img, mspdb::Mod* mod, DWARF_LineState& state)
{
	if(state.lineInfo.size() == 0)
		return true;

	unsigned int saddr = state.lineInfo[0].offset;
	unsigned int eaddr = state.lineInfo.back().offset;
    int segIndex = state.section;
    if (segIndex < 0)
	    segIndex = img.findSection(saddr + state.seg_offset);
	if(segIndex < 0)
	{
		// throw away invalid lines (mostly due to "set address to 0")
		state.lineInfo.resize(0);
		return true;
	}

	const DWARF_FileName* dfn;
	if(state.lineInfo_file == 0)
		dfn = &state.cur_file;
	else if(state.lineInfo_file > 0 && state.lineInfo_file <= state.files.size())
		dfn = &state.files[state.lineInfo_file - 1];
	else
		return false;
	std::string fname = dfn->file_name;

	if(isRelativePath(fname) &&
	   dfn->dir_index > 0 && dfn->dir_index <= state.include_dirs.size())
	{
		std::string dir = state.include_dirs[dfn->dir_index - 1];
		fname = dir + fname;
	}
	for(size_t i = 0; i < fname.length(); i++)
		if(fname[i] == '/')
			fname[i] = '\\';

    if (!mod)
    {
        printLines(fname.c_str(), segIndex, img.findSectionSymbolName(segIndex), state.lineInfo_low_line,
                   state.lineInfo.data(), state.lineInfo.size());
		state.lineInfo.resize(0);
		return true;
    }
#if 1
	//qsort(&state.lineInfo[0], state.lineInfo.size(), sizeof(state.lineInfo[0]), cmpAdr);

	int rc = 1;
	unsigned int low_offset = state.lineInfo[0].offset;
	unsigned int low_line = state.lineInfo_low_line;

	for (size_t ln = 0; ln < state.lineInfo.size(); ++ln)
	{
		auto& line_entry = state.lineInfo[ln];
		line_entry.offset -= low_offset;
	}

	unsigned int high_offset = state.address - state.seg_offset;
	if (high_offset < state.lineInfo.back().offset + low_offset)
		// The current address is before the previous address; use that as the end instead to ensure we capture all of the preceeding line info
		high_offset = state.lineInfo.back().offset + low_offset;
	// PDB address ranges are fully closed, so point to before the next instruction
	--high_offset;
	// This subtraction can underflow to (unsigned)-1 if this info is only for a single instruction, but AddLines will immediately increment it to 0, so this is fine.  Not underflowing this can cause the debugger to ignore other line info for address ranges that include this address.
	unsigned int address_range_length = high_offset - low_offset;

	if (debug & DbgPdbLines)
		fprintf(stderr, "%s:%d: AddLines(%08x+%04x, Line=%4d+%3d, %s)\n", __FUNCTION__, __LINE__,
				low_offset, address_range_length, low_line,
				(unsigned int)state.lineInfo.size(), fname.c_str());

	rc = mod->AddLines(fname.c_str(), segIndex + 1, low_offset, address_range_length, low_offset, low_line,
	                   (unsigned char*)&state.lineInfo[0],
	                   state.lineInfo.size() * sizeof(state.lineInfo[0]));

#else
	unsigned int firstLine = 0;
	unsigned int firstAddr = 0;
	int rc = mod->AddLines(fname.c_str(), segIndex + 1, saddr, eaddr - saddr, firstAddr, firstLine,
						   (unsigned char*) &state.lineInfo[0], state.lineInfo.size() * sizeof(state.lineInfo[0]));
#endif

	state.lineInfo.resize(0);
	return rc > 0;
}

bool addLineInfo(const PEImage& img, mspdb::Mod* mod, DWARF_LineState& state)
{
	// The DWARF standard says about end_sequence: "indicating that the current
	// address is that of the first byte after the end of a sequence of target
	// machine instructions". So if this is a end_sequence row, don't append any
	// lines to the list, just flush it.
	if (state.end_sequence)
		return _flushDWARFLines(img, mod, state);

	if (state.address < state.seg_offset)
		return true;
	mspdb::LineInfoEntry entry;
	entry.offset = state.address - state.seg_offset;
	if (!state.lineInfo.empty())
	{
		auto last_entry = state.lineInfo.back();
		// We can handle out-of-order line numbers, but we can't handle out-of-order addresses
		if (state.line < state.lineInfo_low_line || state.line > state.lineInfo_low_line + 0xffff ||
			entry.offset < last_entry.offset || state.lineInfo_file != state.file)
		{
			if (!_flushDWARFLines(img, mod, state))
				return false;
		}
		else if (state.line == last_entry.line + state.lineInfo_low_line &&
		         entry.offset == last_entry.offset)
		{
			// There's no need to add duplicate entries
			return true;
		}
	}
	if (state.lineInfo.empty())
		state.lineInfo_low_line = state.line;
	entry.line = state.line - state.lineInfo_low_line;
	state.lineInfo.push_back(entry);
	state.lineInfo_file = state.file;
	return true;
}

bool interpretDWARFLines(const PEImage& img, mspdb::Mod* mod, DebugLevel debug_)
{

	DWARF_CompilationUnitInfo cu{};

	unsigned long offs = 0;
	if (!cu.read(debug_, img, &offs)) {
		return false;
	}

	int ptrsize = cu.address_size;

	debug = debug_;

	DWARF_LineNumberProgramHeader hdr5;
	for(unsigned long off = 0; off < img.debug_line.length; )
	{
		DWARF_LineNumberProgramHeader* hdrver = (DWARF_LineNumberProgramHeader*)img.debug_line.byteAt(off);
		int length = hdrver->unit_length;
		if(length < 0)
			break;
		length += sizeof(length);

		DWARF_LineNumberProgramHeader* hdr;
		if (hdrver->version <= 3)
		{
			auto hdr2 = (DWARF2_LineNumberProgramHeader*)hdrver;
			hdr5.default_is_stmt = hdr2->default_is_stmt;
			hdr5.header_length = hdr2->header_length;
			hdr5.line_base = hdr2->line_base;
			hdr5.line_range = hdr2->line_range;
			hdr5.minimum_instruction_length = hdr2->minimum_instruction_length;
			hdr5.maximum_operations_per_instruction = 0xff;
			hdr5.opcode_base = hdr2->opcode_base;
			hdr5.unit_length = hdr2->unit_length;
			hdr5.version = hdr2->version;
			hdr = &hdr5;
		}
		else if (hdrver->version == 4)
		{
			auto hdr4 = (DWARF4_LineNumberProgramHeader*)hdrver;
			hdr5.default_is_stmt = hdr4->default_is_stmt;
			hdr5.header_length = hdr4->header_length;
			hdr5.line_base = hdr4->line_base;
			hdr5.line_range = hdr4->line_range;
			hdr5.minimum_instruction_length = hdr4->minimum_instruction_length;
			hdr5.maximum_operations_per_instruction = hdr4->maximum_operations_per_instruction;
			hdr5.opcode_base = hdr4->opcode_base;
			hdr5.unit_length = hdr4->unit_length;
			hdr5.version = hdr4->version;
			hdr = &hdr5;
		}
		else
			hdr = hdrver;
		int hdrlength = hdr->version <= 3 ? sizeof(DWARF2_LineNumberProgramHeader) : hdr->version == 4 ? sizeof(DWARF4_LineNumberProgramHeader) : sizeof(DWARF_LineNumberProgramHeader);
		unsigned char* p = (unsigned char*) hdrver + hdrlength;
		unsigned char* end = (unsigned char*) hdrver + length;

		if (debug & DbgDwarfLines)
			fprintf(stderr, "%s:%d: LineNumberProgramHeader offs=%x ver=%d\n", __FUNCTION__, __LINE__,
					off, hdr->version);

		std::vector<unsigned int> opcode_lengths;
		opcode_lengths.resize(hdr->opcode_base);
		if (hdr->opcode_base > 0)
		{
			opcode_lengths[0] = 0;
			for(byte o = 1; o < hdr->opcode_base && p < end; o++)
				opcode_lengths[o] = LEB128(p);
		}

		DWARF_LineState state;
		state.seg_offset = img.getImageBase() + img.getSection(img.text.secNo).VirtualAddress;

		if (hdr->version <= 4)
		{
			// dirs
			while(p < end)
			{
				if(*p == 0)
					break;
				state.include_dirs.emplace_back((const char*) p);
				auto &dir = state.include_dirs.back();
				p += dir.size() + 1;
				addTrailingSlash(dir);
			}
			p++;

			// files
			while(p < end && *p)
			{
				DWARF_FileName fname;
				fname.read(p);
				state.files.emplace_back(std::move(fname));
			}
			p++;
		}
		else
		{
			DWARF_TypeForm type_and_form;

			byte directory_entry_format_count = *(p++);
			std::vector<DWARF_TypeForm> directory_entry_format;
			for (int i = 0; i < directory_entry_format_count; i++)
			{
				type_and_form.type = LEB128(p);
				type_and_form.form = LEB128(p);
				directory_entry_format.push_back(type_and_form);
			}

			unsigned int directories_count = LEB128(p);
			for (unsigned int o = 0; o < directories_count; o++)
			{
				for (const auto &typeForm : directory_entry_format)
				{
					switch (typeForm.type)
					{
						case DW_LNCT_path:
						{
							switch (typeForm.form)
							{
							case DW_FORM_line_strp:
							{
								size_t offset = cu.isDWARF64() ? RD8(p) : RD4(p);
								state.include_dirs.emplace_back((const char*)img.debug_line_str.byteAt(offset));
								break;
							}
							case DW_FORM_string:
								state.include_dirs.emplace_back((const char*)p);
								p += strlen((const char*)p) + 1;
								break;
							default:
								fprintf(stderr, "%s:%d: ERROR: invalid form=%d for path lineHdrOffs=%x\n", __FUNCTION__, __LINE__,
										typeForm.form, off);
								return false;
							}

							auto& dir = state.include_dirs.back();

							// Relative dirs are relative to the first directory
							// in the table.
							if (state.include_dirs.size() > 1 && isRelativePath(dir))
								dir = state.include_dirs.front() + dir;

							addTrailingSlash(dir);
							break;
						}
						case DW_LNCT_directory_index:
						case DW_LNCT_timestamp:
						case DW_LNCT_size:
						default:
							fprintf(stderr, "%s:%d: ERROR: unexpected type=%d form=%d for directory path lineHdrOffs=%x\n", __FUNCTION__, __LINE__,
										typeForm.type, typeForm.form, off);
							return false;
					}
				}
			}

			byte file_name_entry_format_count = *(p++);
			std::vector<DWARF_TypeForm> file_name_entry_format;
			for (int i = 0; i < file_name_entry_format_count; i++)
			{
				type_and_form.type = LEB128(p);
				type_and_form.form = LEB128(p);
				file_name_entry_format.push_back(type_and_form);
			}

			unsigned int file_names_count = LEB128(p);
			for (unsigned int o = 0; o < file_names_count; o++)
			{
				DWARF_FileName fname;

				for (const auto &typeForm : file_name_entry_format)
				{
					switch (typeForm.type)
					{
						case DW_LNCT_path:
							switch (typeForm.form)
							{
							case DW_FORM_line_strp:
							{
								size_t offset = cu.isDWARF64() ? RD8(p) : RD4(p);
								fname.file_name = (const char*)img.debug_line_str.byteAt(offset);
								break;
							}
							case DW_FORM_string:
								fname.file_name = (const char*)p;
								p += strlen((const char*)p) + 1;
								break;
							default:
								fprintf(stderr, "%s:%d: ERROR: invalid form=%d for path lineHdrOffs=%x\n", __FUNCTION__, __LINE__,
										typeForm.form, off);
								assert(false && "invalid path form");
								return false;
							}
							break;
						case DW_LNCT_directory_index:
							// bias the directory index by 1 since _flushDWARFLines
							// will check for 0 and subtract one (which is
							// useful for DWARF4).
							switch (typeForm.form)
							{
							case DW_FORM_data1:
								fname.dir_index = *p++ + 1;
								break;
							case DW_FORM_data2:
								fname.dir_index = RD2(p) + 1;
								break;
							case DW_FORM_udata:
								fname.dir_index = LEB128(p) + 1;
								break;
							default:
								fprintf(stderr, "%s:%d: ERROR: invalid form=%d for directory index lineHdrOffs=%x\n", __FUNCTION__, __LINE__,
										typeForm.form, off);
								return false;
							}
							break;
						case DW_LNCT_timestamp:
						case DW_LNCT_size:
						default:
							fprintf(stderr, "%s:%d: ERROR: unexpected type=%d form=%d for file path lineHdrOffs=%x\n", __FUNCTION__, __LINE__,
										typeForm.type, typeForm.form, off);
							return false;
					}
				}

				state.files.emplace_back(std::move(fname));
			}
		}

		state.init(hdr);
		while(p < end)
		{
			int opcode = *p++;
			if(opcode >= hdr->opcode_base)
			{
				// special opcode
				int adjusted_opcode = opcode - hdr->opcode_base;
				int operation_advance = adjusted_opcode / hdr->line_range;
				state.advance_addr(hdr, operation_advance);
				int line_advance = hdr->line_base + (adjusted_opcode % hdr->line_range);
				state.line += line_advance;

				if (!addLineInfo(img, mod, state))
					return false;

				state.basic_block = false;
				state.prologue_end = false;
				state.epilogue_end = false;
				state.discriminator = 0;
			}
			else
			{
				switch(opcode)
				{
				case 0: // extended
				{
					int exlength = LEB128(p);
					unsigned char* q = p + exlength;
					int excode = *p++;
					switch(excode)
					{
					case DW_LNE_end_sequence:
						state.end_sequence = true;
						state.last_addr = state.address;
						if(!addLineInfo(img, mod, state))
							return false;
						state.init(hdr);
						break;
					case DW_LNE_set_address:
					{
						if (!mod && state.section == -1)
							state.section = img.getRelocationInLineSegment(img.debug_line.sectOff(p));
						unsigned long adr = ptrsize == 8 ? RD8(p) : RD4(p);
						state.address = adr;
						state.op_index = 0;
						break;
					}
					case DW_LNE_define_file:
						state.cur_file.read(p);
						state.file = 0;
						break;
					case DW_LNE_set_discriminator:
						state.discriminator = LEB128(p);
						break;
					}
					p = q;
					break;
				}
				case DW_LNS_copy:
					if (!addLineInfo(img, mod, state))
						return false;
					state.basic_block = false;
					state.prologue_end = false;
					state.epilogue_end = false;
					state.discriminator = 0;
					break;
				case DW_LNS_advance_pc:
					state.advance_addr(hdr, LEB128(p));
					break;
				case DW_LNS_advance_line:
					state.line += SLEB128(p);
					break;
				case DW_LNS_set_file:
					state.file = LEB128(p);
					// DWARF5 numbers all files starting at zero.  We will
					// subtract one in _flushDWARFLines when indexing the files
					// array.
					if (hdr->version >= 5)
						state.file += 1;
					break;
				case DW_LNS_set_column:
					state.column = LEB128(p);
					break;
				case DW_LNS_negate_stmt:
					state.is_stmt = !state.is_stmt;
					break;
				case DW_LNS_set_basic_block:
					state.basic_block = true;
					break;
				case DW_LNS_const_add_pc:
					state.advance_addr(hdr, (255 - hdr->opcode_base) / hdr->line_range);
					break;
				case DW_LNS_fixed_advance_pc:
					state.address += RD2(p);
					state.op_index = 0;
					break;
				case DW_LNS_set_prologue_end:
					state.prologue_end = true;
					break;
				case DW_LNS_set_epilogue_begin:
					state.epilogue_end = true;
					break;
				case DW_LNS_set_isa:
					state.isa = LEB128(p);
					break;
				default:
					// unknown standard opcode
					for(unsigned int arg = 0; arg < opcode_lengths[opcode]; arg++)
						LEB128(p);
					break;
				}
			}
		}
		if(!_flushDWARFLines(img, mod, state))
			return false;

		off += length;
	}

	return true;
}

