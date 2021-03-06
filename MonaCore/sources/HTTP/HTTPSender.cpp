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

#include "Mona/HTTP/HTTPSender.h"
#include "Mona/Session.h"

using namespace std;


namespace Mona {

Buffer& HTTPSender::buffer() {
	if (!_pBuffer)
		_pBuffer.reset(new Buffer(4, "\r\n\r\n"));
	return *_pBuffer;
}

void HTTPSender::setCookies(shared<Buffer>& pSetCookie) {
	if (!pSetCookie || !hasHeader())
		return;
	if (!_pBuffer)
		_pBuffer = move(pSetCookie);
	else
		_pBuffer->resize(_pBuffer->size() - 4, true).append(pSetCookie->data(), pSetCookie->size());
	_pBuffer->append(EXPAND("\r\n\r\n"));
}

bool HTTPSender::send(const Packet& content) {
	if (pRequest->type == HTTP::TYPE_HEAD)
		return true;
	if (_chunked) {
		shared<Buffer> pBuffer(new Buffer());
		if (_chunked == 2)
			pBuffer->append(EXPAND("\r\n")); // prefix
		else
			++_chunked;
		String::Append(*pBuffer, String::Format<UInt32>("%X", content.size())).append(EXPAND("\r\n"));
		if(!content)
			pBuffer->append(EXPAND("\r\n")); // end!
		if (!socketSend(Packet(pBuffer)))
			return false;
	}
	return socketSend(content);
}

bool HTTPSender::socketSend(const Packet& packet) {
	if (!packet)
		return true;
	Exception ex;
	DUMP_RESPONSE(pSocket->isSecure() ? "HTTPS" : "HTTP", packet.data(), packet.size(), pSocket->peerAddress());
	int result = pSocket->write(ex, packet);
	if (ex || result<0)
		WARN(ex);
	// no shutdown required, already done by write!
	return result >= 0;
}

bool HTTPSender::send(const char* code, MIME::Type mime, const char* subMime, UInt64 extraSize) {
	// COMPUTE SIZE
	const UInt8* headerEnd = _pBuffer ? _pBuffer->data() : NULL;
	if (headerEnd) {
		while (memcmp(headerEnd++, EXPAND("\r\n\r\n")) != 0);
		headerEnd += 3;
		extraSize += _pBuffer->size() - (headerEnd - _pBuffer->data());
	}

	shared_ptr<Buffer> pBuffer(new Buffer());
	BinaryWriter writer(*pBuffer);

	/// First line (HTTP/1.1 200 OK)
	writer.write(EXPAND("HTTP/1.1 ")).write(code);

	/// Date + Mona
	Date().format(Date::FORMAT_HTTP, writer.write(EXPAND("\r\nDate: ")));
	writer.write(EXPAND("\r\nServer: Mona"));

	/// Last modified
	const Path& path = this->path();
	if (path)
		String::Append(writer.write(EXPAND("\r\nLast-Modified: ")), String::Date(Date(path.lastChange()), Date::FORMAT_HTTP));
	
	/// Content Type/length
	if (mime)  // If no mime type as "304 Not Modified" response or HTTPWriter::writeRaw which write itself content-type => no content!
		MIME::Write(writer.write(EXPAND("\r\nContent-Type: ")), mime, subMime);
	if (extraSize == UINT64_MAX) {
		if (path || !mime) {
			// Transfer-Encoding: chunked!
			writer.write(EXPAND("\r\nTransfer-Encoding: chunked"));
			_chunked = 1;
		} else {
			// live => no content-length + live attributes + close on end
			writer.write(EXPAND("\r\n" HTTP_LIVE_HEADER));
			connection = HTTP::CONNECTION_CLOSE; // write "connection: close" (session until end of socket)
		}
	// no content-length for any Informational response OR 204 no content response OR 304 not modified response
	// see https://tools.ietf.org/html/rfc7230#section-3.3.2
	} else if(code[0]>'3' || (code[0]>'1' && (code[1]!='0' || code[2] != '4')))
		String::Append(writer.write(EXPAND("\r\nContent-Length: ")), extraSize);

	/// Connection type, same than request!
	if (connection&HTTP::CONNECTION_KEEPALIVE) {
		writer.write(EXPAND("\r\nConnection: keep-alive"));
		if (connection&HTTP::CONNECTION_UPGRADE)
			writer.write(EXPAND(", upgrade"));
	} else if (connection&HTTP::CONNECTION_UPGRADE)
		writer.write(EXPAND("\r\nConnection: upgrade"));
	else
		writer.write(EXPAND("\r\nConnection: close"));

	/// allow cross request, indeed if onConnection has not been rejected, every cross request are allowed
	if (pRequest->origin && String::ICompare(pRequest->origin, pRequest->host) != 0)
		writer.write(EXPAND("\r\nAccess-Control-Allow-Origin: ")).write(pRequest->origin);

	if (headerEnd)
		return socketSend(Packet(pBuffer)) && socketSend(pRequest->type == HTTP::TYPE_HEAD ? Packet(_pBuffer, _pBuffer->data(), headerEnd - _pBuffer->data()) : Packet(_pBuffer));
	// no _pBuffer
	writer.write(EXPAND("\r\n\r\n"));
	return socketSend(Packet(pBuffer));
}


} // namespace Mona
