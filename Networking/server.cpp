/* Start Header
*****************************************************************/
/*!
\file server.cpp
\author Joel Lee Jie, Tong Yan, Rachel
\date 30 March 2025
\brief
This file implements the server file which will be used to implement a game-server that primarily controls:
1. Accepting player connections/reconnections
2. Message passing from client -> server -> client
- This is conducted through the use of lockstep protocol, where the server will wait for all messages to come from all clients before sending out.
3. Handling player automatic/manual disconnections.

Important Information:
- A safe UDP packet size is ~540 bytes
- RecvFrom only returns 1 packet at a time, so no need to consider other client packets piggybacking onto the data received.
- Packets are truncated if not received in full, so transfer using 2 packets instead.
- Avoid using TCP for game data as indicated in the rubrics

Copyright (C) 2025 DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the
prior written consent of DigiPen Institute of Technology is prohibited.
*/
/* End Header
*******************************************************************/

/*******************************************************************************
 * A multi-threaded TCP/IP server application with non-blocking sockets
 ******************************************************************************/


#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "Windows.h"		// Entire Win32 API...
 // #include "winsock2.h"	// ...or Winsock alone
#include "ws2tcpip.h"		// getaddrinfo()

// Tell the Visual Studio linker to include the following library in linking.
// Alternatively, we could add this file to the linker command-line parameters,
// but including it in the source code simplifies the configuration.
#pragma comment(lib, "ws2_32.lib")

#include <iostream>			// cout, cerr
#include <string>			// string
#include <list>
#include <map>
#include <array>
#include <sstream>
#include <filesystem>
#include "taskqueue.h"	

#include "Utility.hpp"
#include "Checksum.hpp"
#include <filesystem> //For file operations.
#include <chrono> //for timeout timer.
#include <thread> //to create a separate thread for file downloader.
#include <fstream>

/*
	Contains data required to manage reliable data transfer to and from clients.
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

/*
	Represents a player session, where communications with the player is controlled through this.
	Each player has their own session (and only one session).
	New players get a new session, whilst reconnected players will assume back their sessions.

	Note: addrDest needs to be set before this struct can be used for sending/receiving.
*/
struct Player_Session
{
public:
	//Used to control reliable data transfer.
	Reliable_Transfer reliable_transfer{};
	//Used to determine if a player should be forcibly disconnected, like after X seconds of no response.
	double time_last_packet_received{ 20000000000000 };

	//Used to indicate how to send. Need to set when recvfrom is called.
	sockaddr_storage addrDest{};

	/*
		Used to indicate how much data is to be received from the player.
		Set upon the first packet received from the player for that frame.
		If set to -1, packet not received yet from player for this frame.
		If != recv_buffer.size(), it means there are still some packets to receive.
	*/
	int recv_data_to_recv{ -1 };
	//Stores messages received from player, cleared at the end of each frame.
	std::string recv_buffer{};

	//Stores messages to send to player, cleared at the end of each frame.
	std::string send_buffer{};
};

/*
	Defines used for the server program.
*/
//Max size of udp packet
constexpr int MAX_PACKET_SIZE = 1000;
//Max buffer size when receiving.
constexpr int BUFFER_SIZE = 2000;
//Time before packet should be resent again, if no correct ACK is received.
constexpr float TIMEOUT_TIMER = 0.5f;
//Time before server stops waiting for player response, and disconnects them.
constexpr float AUTOMATIC_DISCONNECTION_TIMER = 4.f;

//Used to manage interactions with players, including sending/receiving, automatic disconnection, reliable data transfer.
std::map<int, Player_Session> player_Session_Map{};

//Server information, set and forget in main().
std::array<unsigned char, 4> server_ip_addr{};
int server_tcp_port_number{}, server_udp_port_number{};

//Controls what the next player's ID should be, to prevent players from having the same ID.
//Reconnecting players will reconnect via sending the player_ID, letting the server know which session to reassume.
int player_id = 0;

//For any sending/receiving, set at the start.
SOCKET file_download_socket{};

///*
//	\brief
//	Write to UDP file socket
//	Only called by File_Download_Interaction, which is a single threaded function.
//	Basically hardcoded for the UDP socket used for file downloading.
//	\return
//	Bytes written to socket.
//	-1 on SOCKET_ERROR, 0 if other port has been gracefully closed, -2 if unable to write ip/port to socketaddr struct.
//*/
//int WriteToDownloadSocket(sockaddr_storage& addrDest, char* data, size_t num_bytes)
//{
//	size_t offset{};
//	while (true)
//	{
//		//All data sent.
//		if (offset >= num_bytes) break;
//		int num_bytes_sent = sendto(file_download_socket, data + offset, static_cast<int>(num_bytes - offset), 0, (sockaddr*)&addrDest, sizeof(addrDest));
//		//Operation could be blocking, check to see what is the error.
//		if (num_bytes_sent == SOCKET_ERROR)
//		{
//			size_t errorCode = WSAGetLastError();
//			//Blocking, just sleep and continue.
//			if (errorCode == WSAEWOULDBLOCK)
//			{
//				continue;
//			}
//			//Gracefully closed
//			else if (errorCode == WSAESHUTDOWN)
//			{
//				return 0;
//			}
//			//Not because of blocking, something actually went wrong.
//			return SOCKET_ERROR;
//		}
//		//Gracefully closed
//		if (num_bytes_sent == 0)
//		{
//			return 0;
//		}
//		offset += num_bytes_sent;
//	}
//
//	return (int)num_bytes;
//}


/*
	\brief
	Entry point of this program, which creates a server that handles server-player communication and interactions.
*/
int main(int argc, char* argv[])
{
	std::string udp_port_string{};
	if (argc < 2)
	{
		// Get Port Number
		std::cout << "Server UDP Port Number: ";
		std::getline(std::cin, udp_port_string);
	}
	else
	{
		udp_port_string = argv[1];
	}
	server_udp_port_number = std::stoi(udp_port_string);


	// -------------------------------------------------------------------------
	// Start up Winsock, asking for version 2.2.
	//
	// WSAStartup()
	// -------------------------------------------------------------------------

	// This object holds the information about the version of Winsock that we
	// are using, which is not necessarily the version that we requested.
	WSADATA wsaData{};

	// Initialize Winsock. You must call WSACleanup when you are finished.
	// As this function uses a reference counter, for each call to WSAStartup,
	// you must call WSACleanup or suffer memory issues.
	int errorCode = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (errorCode != NO_ERROR)
	{
		std::cerr << "WSAStartup() failed." << std::endl;
		return errorCode;
	}

	char host[1000];
	gethostname(host, 1000);

	/*=====
	* Creation of UDP file download socket.


	*/
	/*
		Creation of UDP socket.
	*/

	file_download_socket = socket(
		AF_INET, //IPV4
		SOCK_DGRAM, //UDP.
		IPPROTO_UDP);
	if (file_download_socket == INVALID_SOCKET)
	{
		std::cerr << "socket() failed." << std::endl;
		WSACleanup();
		return 1;
	}

	/*
		Get address information and bind udp socket.
	*/
	addrinfo hints_udp{};
	SecureZeroMemory(&hints_udp, sizeof(hints_udp));
	hints_udp.ai_family = AF_INET;
	// For UDP use SOCK_DGRAM instead of SOCK_STREAM.
	hints_udp.ai_socktype = SOCK_DGRAM;
	hints_udp.ai_protocol = IPPROTO_UDP;	// UDP

	//==Getting address information.
	addrinfo* info_udp = nullptr;
	errorCode = getaddrinfo(host, udp_port_string.c_str(), &hints_udp, &info_udp);
	if ((NO_ERROR != errorCode) || (nullptr == info_udp))
	{
		std::cerr << "getaddrinfo() failed." << std::endl;
		WSACleanup();
		return errorCode;
	}

	/*
		Binding of UDP socket to current ip address
	*/
	if (bind(file_download_socket, info_udp->ai_addr, static_cast<int>(info_udp->ai_addrlen)) != NO_ERROR) {
		std::cerr << "Bind failed" << std::endl;
		closesocket(file_download_socket);
		file_download_socket = INVALID_SOCKET;
		WSACleanup();
		return errorCode;
	}
	// Enable non-blocking I/O on the download socket.
	u_long enable = 1;
	ioctlsocket(file_download_socket, FIONBIO, &enable);

	std::thread file_download_thread(File_Download_Interaction);
	//JOELEND

	// -------------------------------------------------------------------------
	// Resolve own host name into IP addresses (in a singly-linked list).
	//
	// getaddrinfo()
	// -------------------------------------------------------------------------

	// Object hints indicates which protocols to use to fill in the info.

	addrinfo hints{};
	SecureZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;			// IPv4
	// For UDP use SOCK_DGRAM instead of SOCK_STREAM.
	hints.ai_socktype = SOCK_STREAM;	// Reliable delivery
	// Could be 0 for autodetect, but reliable delivery over IPv4 is always TCP.
	hints.ai_protocol = IPPROTO_TCP;	// TCP
	// Create a passive socket that is suitable for bind() and listen().
	hints.ai_flags = AI_PASSIVE;



	addrinfo* info = nullptr;
	errorCode = getaddrinfo(host, portString.c_str(), &hints, &info);
	if ((NO_ERROR != errorCode) || (nullptr == info))
	{
		std::cerr << "getaddrinfo() failed." << std::endl;
		WSACleanup();
		return errorCode;
	}

	/* PRINT SERVER IP ADDRESS AND PORT NUMBER */
	char serverIPAddr[1000];
	struct sockaddr_in* serverAddress = reinterpret_cast<struct sockaddr_in*> (info->ai_addr);
	inet_ntop(AF_INET, &(serverAddress->sin_addr), serverIPAddr, INET_ADDRSTRLEN);
	getnameinfo(info->ai_addr, static_cast <socklen_t> (info->ai_addrlen), serverIPAddr, sizeof(serverIPAddr), nullptr, 0, NI_NUMERICHOST);
	std::cerr << std::endl;
	std::cerr << "Server IP Address: " << serverIPAddr << std::endl;
	std::cerr << "Server TCP Port Number: " << portString << std::endl;
	std::cerr << "Server UDP Port Number: " << udp_port_string << std::endl;

	server_ip_addr = GetIPAddressBytes(serverIPAddr);

	/*
		Creation and binding of tcp socket.
	*/

	SOCKET listenerSocket = socket(
		hints.ai_family,
		hints.ai_socktype,
		hints.ai_protocol);
	if (listenerSocket == INVALID_SOCKET)
	{
		std::cerr << "socket() failed." << std::endl;
		freeaddrinfo(info);
		WSACleanup();
		return 1;
	}

	errorCode = bind(
		listenerSocket,
		info->ai_addr,
		static_cast<int>(info->ai_addrlen));
	if (errorCode != NO_ERROR)
	{
		std::cerr << "bind() failed." << std::endl;
		closesocket(listenerSocket);
		listenerSocket = INVALID_SOCKET;
	}

	freeaddrinfo(info);

	if (listenerSocket == INVALID_SOCKET)
	{
		std::cerr << "bind() failed." << std::endl;
		WSACleanup();
		return 2;
	}


	// -------------------------------------------------------------------------
	// Set a socket in a listening mode and accept 1 incoming client.
	//
	// listen()
	// accept()
	// -------------------------------------------------------------------------

	errorCode = listen(listenerSocket, SOMAXCONN);
	if (errorCode != NO_ERROR)
	{
		std::cerr << "listen() failed." << std::endl;
		closesocket(listenerSocket);
		WSACleanup();
		return 3;
	}

	{
		const auto onDisconnect = [&]() { disconnect(listenerSocket); };
		auto tq = TaskQueue<SOCKET, decltype(execute), decltype(onDisconnect)>{ 10, 20, execute, onDisconnect };
		while (listenerSocket != INVALID_SOCKET)
		{
			sockaddr clientAddress{};
			SecureZeroMemory(&clientAddress, sizeof(clientAddress));
			int clientAddressSize = sizeof(clientAddress);
			SOCKET clientSocket = accept(
				listenerSocket,
				&clientAddress,
				&clientAddressSize);
			if (clientSocket == INVALID_SOCKET)
			{
				break;
			}
			/* PRINT CLIENT IP ADDRESS AND PORT NUMBER */
			char clientIPAddr[1000];
			char clientPort[1000];
			getpeername(clientSocket, &clientAddress, &clientAddressSize);
			getnameinfo(&clientAddress, clientAddressSize, clientIPAddr, sizeof(clientIPAddr), clientPort, sizeof(clientPort), NI_NUMERICHOST);
			std::cerr << std::endl;
			std::cerr << "Client IP Address: " << clientIPAddr << std::endl;
			std::cerr << "Client Port Number: " << clientPort << std::endl;
			AddSocketToList(clientSocket, clientIPAddr, clientPort);
			tq.produce(clientSocket);
		}
	}

	// -------------------------------------------------------------------------
	// Clean-up after Winsock.
	//
	// WSACleanup()
	// -------------------------------------------------------------------------
	isProgramDone = true; //So that the thread can be joined.
	if (file_download_thread.joinable()) file_download_thread.join();
	closesocket(file_download_socket);
	file_download_socket = INVALID_SOCKET;
	WSACleanup();
}

/*
	\brief
	Called upon termination of program, to close the server socket.
*/
void disconnect(SOCKET& listenerSocket)
{
	if (listenerSocket != INVALID_SOCKET)
	{
		shutdown(listenerSocket, SD_BOTH);
		closesocket(listenerSocket);
		listenerSocket = INVALID_SOCKET;
	}
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




