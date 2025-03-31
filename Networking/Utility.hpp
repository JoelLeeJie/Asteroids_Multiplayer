/* Start Header
*****************************************************************/
/*!
\file Utility.hpp
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
#ifndef UTILITY_HPP
#define UTILITY_HPP
#include "Checksum.hpp"
#include <chrono>
#include "winsock2.h"

/*
	Defines used for the client and server program.
*/
//Max size of udp packet
constexpr int MAX_PACKET_SIZE = 1000;
//Max buffer size when receiving.
constexpr int MAX_BUFFER_SIZE = 2000;
//Time before packet should be resent again, if no correct ACK is received.
constexpr float TIMEOUT_TIMER = 0.5f;

/*
	Contains data required to manage reliable data transfer to and from clients (or server).
*/
struct Reliable_Transfer
{
	/*
		Reliable Send
	*/
	//Represents the current packet to send/resend. 
	int current_sequence_number{ 0 };
	//Timeout, when timer completes then packet is to be resent. Set to max since no packet sent yet.
	double time_last_packet_sent{ 20000000000000 };
	//Controls if the packet should be sent. Set to true to send packet immediately.
	bool toSend{ true };
	/*
		Reliable Recv
	*/
	//Represents the last packet successfully received.
	int ack_last_packet_received{ -1 };
};


enum CommandID
{
	CLIENT_PLAYER_TRANSFORM = 0x1,
	CLIENT_BULLET_CREATION = 0x2,
	CLIENT_COLLISION = 0x3,
	SERVER_PLAYER_TRANSFORM = 0x4,
	SERVER_BULLET_CREATION = 0x5, //Not actually creating one, it's just a collection of newly created bullet data.
	SERVER_ASTEROID_CREATION = 0x6,
	SERVER_COLLISION = 0x7,
	JOIN_REQUEST = 0x20,
	JOIN_RESPONSE = 0x21,
	START_GAME = 0x22,
	ACK = 0x30
};

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
int ReadChecksumAndNumber(char* data, size_t length_of_data_with_checksum);

/*
	\brief
	Reads a length of data from file, returning the bytes read.
*/
std::array<unsigned char, 4> GetIPAddressBytes(std::string ip_addr);

/*
	\brief
	Returns current time in double since epoch.
*/
double GetTime();

// Function to compare two sockaddr structures
int Compare_SockAddr(const struct sockaddr_storage* addr1, const struct sockaddr_storage* addr2);

/*
	\brief
	Write to UDP socket. Shouldn't be called by multiple threads at the same time.
	\return
	Bytes written to socket.
	-1 on SOCKET_ERROR, 0 if other port has been gracefully closed.
*/
int WriteToSocket(SOCKET udp_socket, sockaddr_storage& addrDest, char* data, size_t num_bytes);

#endif