
module cvtest;

import std.string;
import std.stdio;

///////////////// field types ////////////////////////
// field type LF_ENUMERATE_V1
enum enum_name
{
	kEnum1 = 1,
	kEnum2,
	kEnum3,
	kEnum500 = 500,
};

// field type LF_MEMBER_V1
class class_member
{
	int member1;
};

// field type LF_BCLASS_V1
class base_class
{
	int base_member;
};

class derived_class : public base_class
{
	int derived_member;
	int a1_1234;

private:
	int a2;
	int a3;
	int a4;
	int a5;
	int a6;
	int a7;
	int a8;
	int a9;
	int a10;
	int a11;
	int a12;
	int a13;
	int a14;
	int a15;
	int a16;
	int a17;
};

// field type LF_METHOD_V1
class class_method
{
	int member = 3;
	static int stmember = 2;
	int method() 
	{ 
		return member + stmember + this.member; 
	}
};

// function with same arguments as class_method.method. does it have different type?
int sim_method(class_method mthd)
{
	return 2;
}

// field type LF_STMEMBER_V1
class class_staticmember
{
	static int static_member = 2;
};

// field type LF_NESTTYPE_V1
class class_outer
{
	class class_inner
	{
		int a, b;
	};
	class_inner inner;
	int c, d;
};

// LF_ARRAY_V1
long[4] global_fixed_array;

// OEM types;
long[] global_oem_long_dynarray;
int[int] global_oem_int_assoc_array;

alias int[] alias_int_array;

void procedure(int arg1)
{
}

struct struct_name
{
	int member;
	static int static_member;
}

union union_name
{
	int int_member;
	long long_member;
	float float_member;
	struct_name struct_member;
}

class this_is_a_rather_long_classname_to_test_what_happens_if_the_classname_gets_longer_than_the_limit_imposed_by_the_old_codeview_format_which_limits_the_length_of_names_to_tw0_hundred_and_fifty_five_characters_because_it_uses_pascal_strings_with_a_length_byte_and_chars_appended
{
	int member;
};
	
int longfoo(this_is_a_rather_long_classname_to_test_what_happens_if_the_classname_gets_longer_than_the_limit_imposed_by_the_old_codeview_format_which_limits_the_length_of_names_to_tw0_hundred_and_fifty_five_characters_because_it_uses_pascal_strings_with_a_length_byte_and_chars_appended x)
{
	return 1;
}

class Templ(T)
{
  T foo(T x)
  {
    return x+x;
  }
}

struct struc
{
	int a;
	struc * next;
}

class class_with_struct_member
{
	int c1;
	int c2;
	struc *s1;

	this() { s1 = new struc; }
};

version(D2)
{
    string stringMixin = "int a = 0;
    return x + a;
    ";

    int mixinTest(int x)
    {
        mixin(stringMixin);
    }
}

int main2(char[][]argv)
{
	enum_name inst_enum = enum_name.kEnum2;
	class_member inst_member = new class_member;
	base_class inst_base = new base_class;
	derived_class inst_derived = new derived_class;
	class_method inst_method = new class_method;
	class_staticmember inst_staticmember = new class_staticmember;
	class_outer inst_outer = new class_outer;
	class_outer.class_inner inst_inner = inst_outer.inner; // = new class_outer.class_inner(inst_outer);
	struct_name inst_struct;
	inst_struct.member = 1;
	struct_name.static_member = 3;
	this_is_a_rather_long_classname_to_test_what_happens_if_the_classname_gets_longer_than_the_limit_imposed_by_the_old_codeview_format_which_limits_the_length_of_names_to_tw0_hundred_and_fifty_five_characters_because_it_uses_pascal_strings_with_a_length_byte_and_chars_appended long_class_name;
	
	inst_member.member1 = 2;

	union_name inst_union;
	inst_union.float_member = 1;
	
	int* pointer_int = null;
	struct_name* pointer_struct_name = &inst_struct;
	class_member* pointer_class_member = &inst_member;
	
	alias_int_array int_array;
	
	int[] int_oem_long_dynarray; int_oem_long_dynarray ~= 12;
	int[int] local_oem_int_assoc_array; 

	for (int i = 0; i < 1024; i++)
	    local_oem_int_assoc_array[i] = 2*i;

	local_oem_int_assoc_array[5000] = 1;

	int *intptr = int_oem_long_dynarray.ptr;
	Object null_obj;
	derived_class null_derived;

	// delegate
	int delegate() dg = &inst_method.method;
	int res = dg();
	
	class_with_struct_member cwsm = new class_with_struct_member;

	return 0;
}


int main(char[][]argv)
{
	long lng = 3;

	main2(argv);

	writefln("Hello world");

	int[int] int_arr;
	int_arr[1] = 100;
	int_arr[98] = 101;
	int_arr[8] = 7;
	int_arr[12] = 11;
	int_arr[17] = 28;
	int_arr[45] = 91;

	struct ab {
		int a;
		int b;
	}
	ab ab1 = { 1, 2 };
	ab ab2 = { 3, 4 };
	ab ab3 = { 1, 3 };
	int[ab] struc_arr;
	struc_arr[ab1] = 5;
	struc_arr[ab2] = 6;
	struc_arr[ab3] = 7;

	struc s = { 2, null };
	
	Templ!(int) templ = new Templ!(int);
	int y = templ.foo(3);
	version(D2)
		int z = mixinTest(7);

	(new Test).test();

	dmd_quirks();
	strings();

	int[] dynint_arr;
	dynint_arr ~= 12;
	return dynint_arr.length;
}

// alias invariant(char)[] string;

class Test
{
	int test()
	{
		class_outer clss = new class_outer;

		string[] dyn_arr;
		dyn_arr ~= "foo";
		dyn_arr ~= "bar";

		int[string] assoc_arr;
		assoc_arr["foo"] = 3;
		assoc_arr["bar"] = 1;
		assoc_arr["abc"] = 7;

		return dyn_arr.length + assoc_arr.length + clss.c;
	}
}

int strings()
{
	string cs = "char string";
	wstring ws = "wchar string"w;
	dstring ds = "dchar string"d;

	return 0;
}

void dmd_quirks()
{
	long quirk_long; // written as delegate<void*,int>
	ulong quirk_ulong; // written as int[]
}
