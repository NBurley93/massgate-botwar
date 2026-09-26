// Massgate
// Copyright (C) 2017 Ubisoft Entertainment
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
#ifndef MMS_DATAITEM
#define MMS_DATAITEM

#include <errno.h>
#include <ctype.h>
#include <math.h>
#include <float.h>
#include <stdlib.h>

class MDB_DataItem
{
public:
	MDB_DataItem(const void* theData) : myData(theData) { }
	operator const char* () const { return (const char*)myData; }
	operator const MC_String () const { return MC_String((const char*)myData); }

	operator const bool () const { return bool(atoi(PrivNumber()) == 0 ? false : true); }

	operator const short () const { return short(atoi(PrivNumber())); }
	operator const unsigned short () const { return unsigned short(atoi(PrivNumber())); }

	operator const int () const { return atoi(PrivNumber()); }
	operator const unsigned int () const { return (unsigned int)_tcstoui64(PrivNumber(), NULL, 10 ); }

	operator const __int64 () const { return _atoi64(PrivNumber()); }
	operator const unsigned __int64 () const { return _tcstoui64(PrivNumber(), NULL, 10 ); }  // for some reason _atoi64 didn't work?

	operator const double () const {return atof(PrivNumber()); }
	operator const long () const { return atol(PrivNumber()); }
	operator const float () const {return float(atof(PrivNumber())); }

private:
	// SQL NULL arrives as a NULL pointer; read it as 0 rather than passing NULL to the CRT.
	// (Modern MySQL/MariaDB return NULL where MySQL 4 returned 0, e.g. UNIX_TIMESTAMP('0000-00-00 00:00:00').)
	const char* PrivNumber() const { return myData ? (const char*)myData : "0"; }

	const void* myData;
};

#endif
