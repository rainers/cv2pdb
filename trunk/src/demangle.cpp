// adopted from demangle.d distributed with DMD

/****
 * Demangle D mangled names.
 * Macros:
 *	WIKI = Phobos/StdDemangle
 */

/* Authors:
 *	Walter Bright, Digital Mars, www.digitalmars.com
 *	Thomas Kuehne
 *	Frits van Bommel
 */

#include <string>
#include <ctype.h>
#include <assert.h>

#include "symutil.h"

using namespace std;

typedef unsigned char ubyte;
typedef long double real;

#define length length()

#define size_t_max 0x7FFFFFFFU

class MangleException
{
public:
	virtual ~MangleException() {}
};

class Demangle
{
public:
	size_t ni;
	string name;
	string (Demangle::*fparseTemplateInstanceName)();
	
	static void error()
	{
		//writefln("error()");
		throw MangleException();
	}

	static ubyte ascii2hex(char c)
	{
		if (!isxdigit(c))
			error();
		return (ubyte)
			( (c >= 'a') ? c - 'a' + 10 :
			  (c >= 'A') ? c - 'A' + 10 : c - '0' );
	}

	size_t parseNumber()
	{
		//writefln("parseNumber() %d", ni);
		size_t result = 0;

		while (ni < name.length && isdigit(name[ni]))
		{
			int i = name[ni] - '0';
			if (result > (size_t_max - i) / 10)
				error();
			result = result * 10 + i;
			ni++;
		}
		return result;
	}

	string parseSymbolName()
	{
		//writefln("parseSymbolName() %d", ni);
		size_t i = parseNumber();
		if (ni + i > name.length)
			error();
		string result;
		if (i >= 5 &&
			name[ni] == '_' &&
			name[ni + 1] == '_' &&
			name[ni + 2] == 'T')
		{
			size_t nisave = ni;
			bool err = false;
			ni += 3;
			try
			{
				result = parseTemplateInstanceName(); // (this->*fparseTemplateInstanceName)();
				if (ni != nisave + i)
					err = true;
			}
			catch (MangleException me)
			{
				err = true;
			}
			ni = nisave;
			if (err)
				goto L1;
			goto L2;
		}
	L1:
		result = name.substr(ni, i);
	L2:
		ni += i;
		return result;
	}

	string parseQualifiedName()
	{
		//writefln("parseQualifiedName() %d", ni);
		string result;

		while (ni < name.length && isdigit(name[ni]))
		{
			if (result.length)
				result += ".";
			result += parseSymbolName();
		}
		return result;
	}

	string parseType(string identifier = string())
	{
		//writefln("parseType() %d", ni);
		int isdelegate = 0;
		bool hasthisptr = false; /// For function/delegate types: expects a 'this' pointer as last argument
	Lagain:
		if (ni >= name.length)
			error();
		string p;
		switch (name[ni++])
		{
		case 'v':	p = "void";	goto L1;
		case 'b':	p = "bool";	goto L1;
		case 'g':	p = "byte";	goto L1;
		case 'h':	p = "ubyte";	goto L1;
		case 's':	p = "short";	goto L1;
		case 't':	p = "ushort";	goto L1;
		case 'i':	p = "int";	goto L1;
		case 'k':	p = "uint";	goto L1;
		case 'l':	p = "long";	goto L1;
		case 'm':	p = "ulong";	goto L1;
		case 'f':	p = "float";	goto L1;
		case 'd':	p = "double";	goto L1;
		case 'e':	p = "real";	goto L1;
		case 'o':	p = "ifloat";	goto L1;
		case 'p':	p = "idouble";	goto L1;
		case 'j':	p = "ireal";	goto L1;
		case 'q':	p = "cfloat";	goto L1;
		case 'r':	p = "cdouble";	goto L1;
		case 'c':	p = "creal";	goto L1;
		case 'a':	p = "char";	goto L1;
		case 'u':	p = "wchar";	goto L1;
		case 'w':	p = "dchar";	goto L1;

		case 'A':				// dynamic array
			p = parseType() + "[]";
			goto L1;

		case 'P':				// pointer
			p = parseType() + "*";
			goto L1;

		case 'G':				// static array
			{	size_t ns = ni;
			parseNumber();
			size_t ne = ni;
			p = parseType() + "[" + name.substr(ns, ne-ns) + "]";
			goto L1;
			}

		case 'H':				// associative array
			p = parseType();
			p = parseType() + "[" + p + "]";
			goto L1;

		case 'D':				// delegate
			isdelegate = 1;
			goto Lagain;

		case 'M':
			hasthisptr = true;
			goto Lagain;

		case 'y':
			p = "immutable(" + parseType() + ")";
			goto L1;

		case 'x':
			p = "const(" + parseType() + ")";
			goto L1;

		case 'O':
			p = "shared(" + parseType() + ")";
			goto L1;

		case 'F':				// D function
		case 'U':				// C function
		case 'W':				// Windows function
		case 'V':				// Pascal function
		case 'R':				// C++ function
			{	char mc = name[ni - 1];
			string args;

			while (1)
			{
				if (ni >= name.length)
					error();
				char c = name[ni];
				if (c == 'Z')
					break;
				if (c == 'X')
				{
					if (!args.length) error();
					args += " ...";
					break;
				}
				if (args.length)
					args += ", ";
				switch (c)
				{
				case 'J':
					args += "out ";
					ni++;
					goto Ldefault;

				case 'K':
					args += "ref ";
					ni++;
					goto Ldefault;

				case 'L':
					args += "lazy ";
					ni++;
					goto Ldefault;

				default:
				Ldefault:
					args += parseType();
					continue;

				case 'Y':
					args += "...";
					break;
				}
				break;
			}
			ni++;
			if (!isdelegate && identifier.length)
			{
				switch (mc)
				{
				case 'F': p = "";                  break; // D function
				case 'U': p = "extern (C) ";       break; // C function
				case 'W': p = "extern (Windows) "; break; // Windows function
				case 'V': p = "extern (Pascal) ";  break; // Pascal function
				default:  assert(0);
				}
				p += parseType() + " " + identifier + "(" + args + ")";
				return p;
			}
			p = parseType() +
				(isdelegate ? " delegate(" : " function(") + args + ")";
			isdelegate = 0;
			goto L1;
			}

		case 'C':	p = "class ";	goto L2;
		case 'S':	p = "struct ";	goto L2;
		case 'E':	p = "enum ";	goto L2;
		case 'T':	p = "typedef ";	goto L2;

	L2:	p += parseQualifiedName();
			goto L1;

	L1:
			if (isdelegate)
				error();		// 'D' must be followed by function
			if (identifier.length)
				p += " " + identifier;
			return p;

		default:
			size_t i = ni - 1;
			ni = name.length;
			p = name.substr(i, name.length-i);
			goto L1;
		}
	}

	void getReal(string &result)
	{
		real r;
		ubyte rdata[10];
		ubyte *p = rdata;

		if (ni + 10 * 2 > name.length)
			error();
		for (size_t i = 0; i < 10; i++)
		{
			ubyte b;

			b = (ubyte) ((ascii2hex(name[ni + i * 2]) << 4) + ascii2hex(name[ni + i * 2 + 1]));
			p[i] = b;
		}
		// extract 10-byte double from rdata
		__asm {
			fld TBYTE PTR rdata;
			fstp r;
		}

		char num[30];
		sprintf(num, "%g", r);
		result += num; // format(r);
		ni += 10 * 2;
	}

	string parseTemplateInstanceName()
	{
		string result = parseSymbolName() + "!(";
		int nargs = 0;

		while (1)
		{
			size_t i;

			if (ni >= name.length)
				error();
			if (nargs && name[ni] != 'Z')
				result += ", ";
			nargs++;
			switch (name[ni++])
			{
			case 'T':
				result += parseType();
				continue;

			case 'V':

				result += parseType() + " ";
				if (ni >= name.length)
					error();
				switch (name[ni++])
				{
				case '0': case '1': case '2': case '3': case '4':
				case '5': case '6': case '7': case '8': case '9':
					i = ni - 1;
					while (ni < name.length && isdigit(name[ni]))
						ni++;
					result += name.substr(i, ni - i);
					break;

				case 'N':
					i = ni;
					while (ni < name.length && isdigit(name[ni]))
						ni++;
					if (i == ni)
						error();
					result += "-" + name.substr(i, ni - i);
					break;

				case 'n':
					result += "null";
					break;

				case 'e':
					getReal(result);
					break;

				case 'c':
					getReal(result);
					result += '+';
					getReal(result);
					result += 'i';
					break;

				case 'a':
				case 'w':
				case 'd':
					{   char m = name[ni - 1];
					if (m == 'a')
						m = 'c';
					size_t n = parseNumber();
					if (ni >= name.length || name[ni++] != '_' ||
						ni + n * 2 > name.length)
						error();
					result += '"';
					for (i = 0; i < n; i++)
					{	char c;

					c = (char)((ascii2hex(name[ni + i * 2]) << 4) +
						ascii2hex(name[ni + i * 2 + 1]));
					result += c;
					}
					ni += n * 2;
					result += '"';
					result += m;
					break;
					}

				default:
					error();
					break;
				}
				continue;

			case 'S':
				result += parseSymbolName();
				continue;

			case 'Z':
				break;

			default:
				error();
			}
			break;
		}
		result += ")";
		return result;
	}

	string demangle(string _name, bool plainName = false)
	{
		ni = 2;
		name = _name;

		if (name.length < 3 ||
			name[0] != '_' ||
			name[1] != 'D' ||
			!isdigit(name[2]))
		{
			goto Lnot;
		}

		// fparseTemplateInstanceName = &parseTemplateInstanceName;

		try
		{
			string result = parseQualifiedName();
			string typed_result = parseType(result);
			while(ni < name.length)
			{
				// throw away outer type (e.g. for local functions)
				result = result + "." + parseQualifiedName();
				typed_result = parseType(result);
			}
			if (!plainName)
				result = typed_result;

			if (ni != name.length)
				goto Lnot;
			return result;
		}
		catch (MangleException e)
		{
		}

	Lnot:
		// Not a recognized D mangled name; so return original
		return name;
	}

};

void unittest()
{
	// debug(demangle) printf("demangle.demangle.unittest\n");

	static string table[][2] = 
	{
		{ "_D6object14_moduleTlsCtorUZv15_moduleTlsCtor2MFAPS6object10ModuleInfoiZv", "void object._moduleTlsCtor._moduleTlsCtor2(struct object.ModuleInfo*[], int)"},
		{ "_D7dparser3dmd8Template21TemplateTypeParameter13overloadMatchMFC7dparser3dmd8Template17TemplateParameterZi", "int dparser.dmd.Template.TemplateTypeParameter.overloadMatch(class dparser.dmd.Template.TemplateParameter)"},
		{ "printf",	"printf" },
		{ "_foo",	"_foo" },
		{ "_D88",	"_D88" }, // causes exception error, return symbol as is
		{ "_D4test3fooAa", "char[] test.foo"},
		{ "_D8demangle8demangleFAaZAa", "char[] demangle.demangle(char[])" },
		{ "_D6object6Object8opEqualsFC6ObjectZi", "int object.Object.opEquals(class Object)" },
		{ "_D4test2dgDFiYd", "double delegate(int, ...) test.dg" },
		{ "_D4test58__T9factorialVde67666666666666860140VG5aa5_68656c6c6fVPvnZ9factorialf", "float test.factorial!(double 4.2, char[5] \"hello\"c, void* null).factorial" },
		{ "_D4test101__T9factorialVde67666666666666860140Vrc9a999999999999d9014000000000000000c00040VG5aa5_68656c6c6fVPvnZ9factorialf", "float test.factorial!(double 4.2, cdouble 6.8+3i, char[5] \"hello\"c, void* null).factorial" },
		{ "_D4test34__T3barVG3uw3_616263VG3wd3_646566Z1xi", "int test.bar!(wchar[3] \"abc\"w, dchar[3] \"def\"d).x" },
		{ "_D8demangle4testFLC6ObjectLDFLiZiZi", "int demangle.test(lazy class Object, lazy int delegate(lazy int))"},
		{ "_D8demangle4testFAiXi", "int demangle.test(int[] ...)"},
		{ "_D8demangle4testFLAiXi", "int demangle.test(lazy int[] ...)"} ,
	};

	Demangle d;
	for(int i = 0; i < sizeof(table)/sizeof(table[0]); i++)
	{
		string r = d.demangle(table[i][0]);
		assert(r == table[i][1]);
		//	"table entry #" + toString(i) + ": '" + name[0] + "' demangles as '" + r + "' but is expected to be '" + name[1] + "'");
	}

	const char s[] = "_D12intellisen\xd1" "11LibraryInfo14findDe\xeaitionMFKS\x80\x8f\xaf" "0SearchDataZA\x80\x91\x9d\x80\x8a\xbb" "8count\x80\x83\x90MFAyaP\x80\x8f\xaa" "9JSONscopeH\x80\x83\x93S3std4json\x80\x85\x98ValueZb";
    char buf[512];
    dsym2c((const BYTE*) s, sizeof(s) - 1, buf, sizeof(buf));
}

bool d_demangle(const char* name, char* demangled, int maxlen, bool plain)
{
#ifdef _DEBUG
    static bool once; if(!once) { once = true; unittest(); }
#endif

	Demangle d;
	string r = d.demangle(name, plain);
	if (r.length == 0)
		return false;
	strncpy(demangled, r.c_str(), maxlen);
	return true;
}
