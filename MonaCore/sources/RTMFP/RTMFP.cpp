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

#include "Mona/RTMFP/RTMFP.h"
#include "Mona/Crypto.h"
#include "Mona/Logs.h"


using namespace std;


namespace Mona {

Buffer& RTMFP::InitBuffer(shared<Buffer>& pBuffer, UInt8 marker) {
	pBuffer.reset(new Buffer(6));
	return BinaryWriter(*pBuffer).write8(marker).write16(RTMFP::TimeNow()).buffer();
}

Buffer& RTMFP::InitBuffer(shared<Buffer>& pBuffer, Mona::Time& expTime, UInt8 marker) {
	if (!expTime)
		return InitBuffer(pBuffer, marker);
	pBuffer.reset(new Buffer(6));
	BinaryWriter(*pBuffer).write8(marker+4).write16(RTMFP::TimeNow()).write16(Time(expTime.elapsed()));
	expTime = 0;
	return *pBuffer;
}

bool RTMFP::Send(Socket& socket, const Packet& packet, const SocketAddress& address) {
	Exception ex;
	int sent = socket.write(ex, packet, address);
	if (sent < 0) {
		DEBUG(ex);
		return false;
	}
	if (ex)
		DEBUG(ex);
	return true;
}

bool RTMFP::Engine::decode(Exception& ex, Buffer& buffer, const SocketAddress& address) {
	static UInt8 IV[KEY_SIZE];
	EVP_CipherInit_ex(&_context, EVP_aes_128_cbc(), NULL, _key, IV, 0);
	int temp;
	EVP_CipherUpdate(&_context, buffer.data(), &temp, buffer.data(), buffer.size());
	// Check CRC
	BinaryReader reader(buffer.data(), buffer.size());
	UInt16 crc(reader.read16());
	if (Crypto::ComputeChecksum(reader) != crc) {
		ex.set<Ex::Protocol>("Bad RTMFP CRC sum computing");
		return false;
	}
	buffer.clip(2);
	if (address)
		DUMP_REQUEST("RTMFP", buffer.data(), buffer.size(), address);
	return true;
}

shared<Buffer>& RTMFP::Engine::encode(shared<Buffer>& pBuffer, UInt32 farId, const SocketAddress& address) {
	if(address)
		DUMP_RESPONSE("RTMFP", pBuffer->data() + 6, pBuffer->size() - 6, address);

	int size = pBuffer->size();
	// paddingBytesLength=(0xffffffff-plainRequestLength+5)&0x0F
	int temp = (0xFFFFFFFF - size + 5) & 0x0F;
	// Padd the plain request with paddingBytesLength of value 0xff at the end
	pBuffer->resize(size + temp);
	memset(pBuffer->data() + size, 0xFF, temp);
	size += temp;

	UInt8* data = pBuffer->data();

	// Write CRC (at the beginning of the request)
	BinaryReader reader(data, size);
	reader.next(6);
	BinaryWriter(data + 4, 2).write16(Crypto::ComputeChecksum(reader));
	// Encrypt the resulted request
	static UInt8 IV[KEY_SIZE];
	EVP_CipherInit_ex(&_context, EVP_aes_128_cbc(), NULL, _key, IV, 1);
	EVP_CipherUpdate(&_context, data+4, &temp, data + 4, size-4);

	reader.reset(4);
	BinaryWriter(data, 4).write32(reader.read32() ^ reader.read32() ^ farId);
	return pBuffer;
}

void RTMFP::ComputeAsymetricKeys(Binary& secret, const UInt8* initiatorNonce, UInt16 initNonceSize, const UInt8* responderNonce, UInt16 respNonceSize, UInt8* requestKey, UInt8* responseKey) {
	Crypto::HMAC::SHA256(responderNonce, respNonceSize, initiatorNonce, initNonceSize, requestKey);
	Crypto::HMAC::SHA256(initiatorNonce, initNonceSize, responderNonce, respNonceSize, responseKey);
	// now doing HMAC-sha256 of both result with the shared secret DH key
	Crypto::HMAC::SHA256(secret.data(), secret.size(), requestKey, Crypto::SHA256_SIZE, requestKey);
	Crypto::HMAC::SHA256(secret.data(), secret.size(), responseKey, Crypto::SHA256_SIZE, responseKey);
}

BinaryWriter& RTMFP::WriteAddress(BinaryWriter& writer,const SocketAddress& address, Location location) {
	const IPAddress& host = address.host();
	if (host.family() == IPAddress::IPv6)
		writer.write8(location | 0x80);
	else
		writer.write8(location);
	NET_SOCKLEN size(host.size());
	const UInt8* bytes = BIN host.data();
	for(NET_SOCKLEN i=0;i<size;++i)
		writer.write8(bytes[i]);
	return writer.write16(address.port());
}

UInt32 RTMFP::ReadID(Buffer& buffer) {
	BinaryReader reader(buffer.data(), buffer.size());
	UInt32 id(0);
	for(UInt8 i=0;i<3;++i)
		id ^= reader.read32();
	buffer.clip(4);
	return id;
}


}  // namespace Mona
