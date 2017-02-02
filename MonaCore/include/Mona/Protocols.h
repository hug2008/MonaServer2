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
#include "Mona/Protocol.h"

namespace Mona {


struct Protocols : virtual Object {
	Protocols() {}
	~Protocols() { stop(); }

	typedef std::map<const char*, Protocol*>::const_iterator Iterator;

	UInt32	 count() const { return _protocols.size(); }

	Iterator begin() const { return _protocols.begin(); }
	Iterator end() const { return _protocols.end(); }

	template<typename ProtocolType=Protocol>
	ProtocolType* find(const char* name) const {
		auto& it = _protocols.find(name);
		return it == _protocols.end() ? NULL : (ProtocolType*)it->second;
	}

	void start(ServerAPI& api, Sessions& sessions) { _pAPI = &api; load(*_pAPI, sessions); }
	void stop() {
		// unload
		for (auto& it : _protocols)
			delete it.second;
		_protocols.clear();
	}

	void manage() { for (auto& it : _protocols) it.second->manage(); }

private:

	void load(ServerAPI& api, Sessions& sessions);

	/*!
	Load protocol */
	template<typename ProtocolType, bool enabled = true, typename ...Args >
	ProtocolType* loadProtocol(const char* name, ServerAPI& api, Sessions& sessions, Args&&... args) {
		return loadProtocolIntern<ProtocolType, enabled>(name, api, sessions, std::forward<Args>(args)...);
	}

	/*!
	Load sub protocol */
	template<typename ProtocolType, bool enabled = true, typename ...Args >
	ProtocolType* loadProtocol(const char* name, Protocol* pTunnel, Args&&... args) {
		if(pTunnel)
			return loadProtocolIntern<ProtocolType, enabled>(name, *pTunnel, std::forward<Args>(args)...);
		_pAPI->setBoolean(name, false);
		return NULL;
	}

	template<typename ProtocolType, bool enabled, typename ...Args >
	ProtocolType* loadProtocolIntern(const char* name, Args&&... args) {
		bool enabled(enabled);
		if (!_pAPI->getBoolean(name, enabled))
			_pAPI->setBoolean(name, enabled);
		if (!enabled)
			return NULL;

		// check name protocol is unique!
		const auto& it(_protocols.lower_bound(name));
		if (it != _protocols.end() && it->first == name) {
			ERROR(name, " protocol already exists, use a unique name for each protocol");
			return NULL;
		}

		ProtocolType* pProtocol = new ProtocolType(name, std::forward<Args>(args)...);
	
		std::string buffer;
		// Copy parameters from API to PROTOCOL (duplicated to get easy access with protocol object)
		// => override default protocol parameters
		for(auto& it : _pAPI->band(String::Assign(buffer, name, ".")))
			pProtocol->setString(it.first.c_str() + buffer.size(), it.second);

		Exception ex;
		SocketAddress& address((SocketAddress&)pProtocol->address); // address is wildcard here!

		// Build address
		bool success;
		buffer.clear();
		pProtocol->getString("host", buffer);
		if (!buffer.empty()) {
			// Build host address
			AUTO_ERROR(success = address.host().setWithDNS(ex, buffer), "Impossible to resolve host ", buffer, " of ", name, " server")
			if (!success) {
				// 0.0.0.0 is a valid host!
				delete pProtocol;
				_pAPI->setBoolean(name, false); // to warn on protocol absence
				return NULL; // Bad host parameter can't start!
			}
		}
		address.setPort(pProtocol->getNumber<UInt16>("port"));

		// Load protocol and read its address
		
		AUTO_ERROR(success = pProtocol->load(ex), name, " server");
		if (!success) {
			delete pProtocol;
			_pAPI->setBoolean(name, false); // to warn on protocol absence
			return false;
		}

		// Fix address after binding
		if (pProtocol->socket())
			address = pProtocol->socket()->address();
		pProtocol->setString("host", address.host());
		pProtocol->setNumber("port", address.port());

		NOTE(name, " server started on ", address, pProtocol->socket() ? (pProtocol->socket()->type == Socket::TYPE_DATAGRAM ? " (UDP)" : " (TCP)") : "");
		_protocols.emplace_hint(it, name, pProtocol);

		// Build protocol.address (public address)
		buffer.clear();
		pProtocol->getString("publicHost", buffer);
		if (!buffer.empty())
			AUTO_WARN(address.host().setWithDNS(ex, buffer), "Impossible to resolve publicHost ", buffer, " of ", name, " server");
		if (!address.host()) // 0.0.0.0 => 127.0.0.1
			address.host().set(IPAddress::Loopback());
		UInt16 port;
		if (pProtocol->getNumber("publicPort", port))
			address.setPort(port);
		pProtocol->setString("publicHost", address.host());
		pProtocol->setNumber("publicPort", address.port());

		// Copy protocol params to api params! (to get defaults protocol params + configs params on api)
		for (auto& it : *pProtocol)
			_pAPI->setString(String::Assign(buffer, name, ".", it.first), it.second);

		return pProtocol;
	}


	ServerAPI*						_pAPI;
	std::map<const char*,Protocol*>	_protocols;
};


} // namespace Mona
