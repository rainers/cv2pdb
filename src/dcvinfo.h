#ifndef __DCVINFO_H__
#define __DCVINFO_H__

// DMD CodeViev extensions

union codeview_oem_type
{
	struct
	{
		short int oemid;
		short int id;
		short int count;
	} generic;

	struct
	{
		short int oemid; // 0x42 for D
		short int id;    // 1
		short int count; // 2
		short unsigned int index_type;
		short unsigned int elem_type;
	} d_dyn_array;

	struct
	{
		short int oemid; // 0x42 for D
		short int id;    // 2
		short int count; // 2
		short unsigned int key_type;
		short unsigned int elem_type;
	} d_assoc_array;

	struct
	{
		short int oemid; // 0x42 for D
		short int id;    // 3
		short int count; // 2
		short unsigned int this_type;
		short unsigned int func_type;
	} d_delegate;
};

#endif
