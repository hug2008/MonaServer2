/*
This file is a part of MonaSolutions Copyright 2017
mathieu.poux[a]gmail.com
jammetthomas[a]gmail.com

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License received along this program for more
details (or else see http://www.gnu.org/licenses/).

*/

#pragma once

#include "Mona/Mona.h"
#include "Mona/DataReader.h"

namespace Mona {

template<typename MapType>
struct MapReader : DataReader, virtual Object {
	MapReader(const MapType& map) : _begin(map.begin()),_it(map.begin()),_end(map.end()) {}
	MapReader(const MapType& map, const typename MapType::const_iterator itBegin) : _begin(itBegin), _it(itBegin), _end(map.end()) {}
	MapReader(const MapType& map, const typename MapType::const_iterator itBegin, const typename MapType::const_iterator itEnd) : _begin(itBegin), _it(itBegin), _end(itEnd) {}
	template<typename BandType>
	MapReader(const BandType& band) : _begin(band.begin()), _it(band.begin()), _end(band.end()) {}

	void			reset() { _it = _begin; }

private:

	UInt8 followingType() {
		if (_it == _end)
			return END;
		return OTHER;
	}

	bool readOne(UInt8 type, DataWriter& writer) { string prefix;  return readOne(type, writer, prefix); }

	bool readOne(UInt8 type, DataWriter& writer, std::string& prefix) {
		
		// read all
		writer.beginObject();
		while (_it != _end) {
			const char* key = _it->first.c_str();
			if (_it->first.compare(0, prefix.size(), prefix) != 0)
				return true; // stop sub
			key += prefix.size();
	
			if (const char* sub = strchr(key, '.')) {
				prefix.append(key, sub-key);
				writer.writePropertyName(prefix.c_str());
				readOne(type, writer, prefix += '.');
				continue;
			}
	
			writer.writePropertyName(key);

			if (String::ICompare(_it->second, "true") == 0)
				writer.writeBoolean(true);
			else if (String::ICompare(_it->second, "false") == 0)
				writer.writeBoolean(false);
			else if (String::ICompare(_it->second, "null") == 0)
				writer.writeNull();
			else {
				double number;
				if (String::ToNumber(_it->second, number))
					writer.writeNumber(number);
				else {
					Exception ex;
					if (_date.update(ex, _it->second))
						writer.writeDate(_date);
					else
						writer.writeString(_it->second.data(),_it->second.size());
				}
			}
			++_it;
		}
		writer.endObject();

		return true;
	}

	Date								_date;
	typename MapType::const_iterator	_begin;
	typename MapType::const_iterator	_it;
	typename MapType::const_iterator	_end;
};


} // namespace Mona
