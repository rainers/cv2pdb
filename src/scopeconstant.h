#ifndef __SCOPECONSTANT_H__
#define __SCOPECONSTANT_H__

#include <stdint.h>
#include <string>

// The constant value of a variable over the addresses [lo, hi), to write as an S_CONSTANT record.
struct ScopeConstant
{
	uint64_t lo;
	uint64_t hi;
	std::string name;

	// The CodeView type of the variable.
	int type;

	uint64_t value;

	// Set when the type of the variable is signed, so that `value` is read as a signed number.
	bool isSigned;
};

#endif
