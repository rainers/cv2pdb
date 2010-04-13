// Convert DMD CodeView debug information to PDB files
// Copyright (c) 2009-2010 by Rainer Schuetze, All Rights Reserved
//
// License for redistribution is given by the Artistic License 2.0
// see file LICENSE for further details

#ifndef __LASTERROR_H__
#define __LASTERROR_H__

class LastError
{
public:
	LastError() : lastError("") {}

	bool setError(const char* msg) { lastError = msg; return false; }
	const char* getLastError() const { return lastError; }
	bool hadError() const { return lastError != 0 && *lastError; }

private:
	const char* lastError;
};


#endif //__LASTERROR_H__