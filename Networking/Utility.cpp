/* /* Start Header
*****************************************************************/
/*!
\file Utility.cpp
\author Joel Lee Jie
\date 30 March 2025
\brief
This file implements helper functions, such as reliable sending/receiving of data through UDP.
It is used by both client and server.

Copyright (C) 2025 DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the
prior written consent of DigiPen Institute of Technology is prohibited.
*/
/* End Header
*******************************************************************/
#include "Utility.hpp"
#include <fstream>
#include <array>
#include <sstream>
/*
	\brief
	Write to UDP socket. Shouldn't be called by multiple threads at the same time.
	\return
	Bytes written to socket.
	-1 on SOCKET_ERROR, 0 if other port has been gracefully closed.
*/
int WriteToSocket(SOCKET udp_socket, sockaddr_storage& addrDest, char* data, size_t num_bytes)
{
	size_t offset{};
	while (true)
	{
		//All data sent.
		if (offset >= num_bytes) break;
		int num_bytes_sent = sendto(udp_socket, data + offset, static_cast<int>(num_bytes - offset), 0, (sockaddr*)&addrDest, sizeof(addrDest));
		//Operation could be blocking, check to see what is the error.
		if (num_bytes_sent == SOCKET_ERROR)
		{
			size_t errorCode = WSAGetLastError();
			//Blocking, just sleep and continue.
			if (errorCode == WSAEWOULDBLOCK)
			{
				continue;
			}
			//Gracefully closed
			else if (errorCode == WSAESHUTDOWN)
			{
				return 0;
			}
			//Not because of blocking, something actually went wrong.
			return SOCKET_ERROR;
		}
		//Gracefully closed
		if (num_bytes_sent == 0)
		{
			return 0;
		}
		offset += num_bytes_sent;
	}

	return (int)num_bytes;
}

/*
	\brief
	Checks and returns if packet checksum is valid, and the sequence number of the packet.
	Returns -1 if checksum is invalid, sequence number if it is valid.

	Do not pass in if data is < 6 bytes.

	\param data
	The entire data inclusive of checksum.
	\param length_of_data
	The length of the entire data inclusive of checksum
*/
int ReadChecksumAndNumber(char* data, size_t length_of_data_with_checksum)
{
	if (length_of_data_with_checksum < 6) return -1;
	uint16_t checksum{};
	memcpy_s(&checksum, 2, data, 2);
	uint32_t sequence_number{};
	memcpy_s(&sequence_number, 4, data + 2, 4);
	checksum = ntohs(checksum);
	sequence_number = ntohl(sequence_number);
	if (ValidateChecksum(length_of_data_with_checksum - 2, data + 2, checksum)) return sequence_number;
	return -1;
}

/*
	\brief
	Convert the ip address string to bytes.
*/
std::array<unsigned char, 4> GetIPAddressBytes(std::string ip_addr)
{
	std::array<unsigned char, 4> byte_arr{};
	char delimiter{}; int byte{};
	std::istringstream ip_addr_stream{ ip_addr };
	ip_addr_stream >> std::ws;
	for (int i = 0; i < 4; i++)
	{
		ip_addr_stream >> byte >> delimiter;
		byte_arr[i] = static_cast<unsigned char>(byte);
	}
	return byte_arr;
}

/*
	\brief
	Returns current time in double since epoch.
*/
double GetTime()
{
	// Get the current time since epoch
	auto now = std::chrono::system_clock::now();
	auto duration = now.time_since_epoch();

	// Convert to seconds and return.
	return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() / 1000.0;
}

// Function to compare two sockaddr structures
int Compare_SockAddr(const struct sockaddr_storage* addr1, const struct sockaddr_storage* addr2) {
	// IPv4 comparison
	struct sockaddr_in* ipv4_1 = (struct sockaddr_in*)addr1;
	struct sockaddr_in* ipv4_2 = (struct sockaddr_in*)addr2;
	return (ipv4_1->sin_port == ipv4_2->sin_port) &&
		(ipv4_1->sin_addr.s_addr == ipv4_2->sin_addr.s_addr);
}
