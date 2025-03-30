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
	Represents a player session, where communications with the player is controlled through this.
	Each player has their own session (and only one session).
	New players get a new session, whilst reconnected players will assume back their sessions.

	Note: addrDest needs to be set before this struct can be used for sending/receiving.
*/
struct Player_Session
{
public:
	Player_Session(sockaddr_storage addr)
		: addrDest{addr}
	{
	}
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
SOCKET udp_socket{};

//Forward declarations:
void HandleStartGame();


/*
	\brief
	The starting point where server-player interactions are managed.
	Called in main after udp_socket has been created and binded.
	Return (without closing udp_socket) when server-player interactions are completed.
*/
void GameProgram()
{
	//Wait for players to join.
	HandleStartGame();
	while (true)
	{
		/*
			Structure of Program:
			It first receives the message, and checks which client sent it (session ID).
			It then waits for other clients, then after all clients are done it checks for a few things.
			It stops waiting after a few seconds, and clients who haven’t responded back means they disconnected, so remove them from client list and don’t wait for them.
			Check who collided with asteroid first, based on their sent timestamps.
			Send message back to clients
			- New player transforms (from other players).
			- New bullet creations (from other players)
			- Asteroid creations.
			- Asteroid destruction (who destroyed what).
		*/
		//==Ensure all messages received and ACK'd.
		ReceiveAllMessages();
		
	}
}

/*
	\brief
	Continually get join requests from players, adding them as new players to the map.
	This happens until the START command is given, which is then echoed to all players and then the game starts (the function returns).
*/
void HandleStartGame()
{
	char temp_buffer[MAX_BUFFER_SIZE]{};
	while (true)
	{
		memset(temp_buffer, 0, MAX_BUFFER_SIZE);
		//==Read any incoming message.
		sockaddr_storage sender_addr{}; //temporarily store sender's address.
		int size_sockaddr = sizeof(sender_addr);
		int bytes_read = recvfrom(udp_socket, temp_buffer, MAX_BUFFER_SIZE, 0, (sockaddr*)&sender_addr, &size_sockaddr);
		
		//Join request or Start request detected.
		//Both only has 1 byte.
		if (bytes_read == 1)
		{
			char command_ID = temp_buffer[0];
			//Player wants to join.
			if (command_ID == JOIN_REQUEST)
			{
				int client_player_id = -1; //-1 to indicate it doesn't have a player id yet.
				//Iterate over the map, to see if the player already is in the game (maybe they never received the JOIN_RESPONSE).
				for (auto& player_entry : player_Session_Map)
				{
					//Check if they're already in the map.
					if (!Compare_SockAddr(&sender_addr, &player_entry.second.addrDest)) continue;
					//They are already in the map.
					client_player_id = player_entry.first;
				}

				//No player entry found for this ip address, so add in a new entry.
				if (client_player_id == -1)
				{
					client_player_id = player_id;
					/*
						Store new player information into the map.
					*/
					player_Session_Map.emplace(player_id++, Player_Session{ sender_addr });
				}

				//Send the information back to the player as a JOIN_RESPONSE, [Checksum, 2][0x21][Player_ID, 2].
				uint16_t network_player_id = htons((uint16_t)client_player_id);
				char buffer[10]{};
				buffer[2] = JOIN_RESPONSE;
				memcpy_s(buffer+3, 2, &network_player_id, 2);
				uint16_t checksum = CalculateChecksum(3, buffer + 2);
				checksum = htons(checksum);
				memcpy_s(buffer, 2, &checksum, 2);

				WriteToSocket(udp_socket, sender_addr, buffer, 5);
			}
		}

		/*
			TODO: Handle start request, ensuring that all players receive start command via ACK.
		*/

	}
}

/*
	\brief
	Called by GameProgram() to ensure all messages are received (and ACK'd) from players.
	- If player hasn't responded for some time, they are removed from the map and the server no longer waits for them.
	- If a new player requests to join, they are assigned a new ID.

	Follows reliable data transfer protocols.
*/
void ReceiveAllMessages()
{
	char temp_buffer[MAX_BUFFER_SIZE]{};
	while (true)
	{
		/*
			TODO: Automatic/Manual Disconnection and Reconnection not being considered yet.
		*/
		memset(temp_buffer, 0, MAX_BUFFER_SIZE);
		//==Read any incoming message.
		sockaddr_storage sender_addr{}; //temporarily store sender's address.
		int size_sockaddr = sizeof(sender_addr);
		int bytes_read = recvfrom(udp_socket, temp_buffer, MAX_BUFFER_SIZE, 0, (sockaddr*)&sender_addr, &size_sockaddr);
		
		//Minimum message size is 6 bytes (disregarding the join and start commands at the start of the game) to account for checksum and sequence number.
		if (bytes_read >= 6)
		{
			/*
				First ensure that the checksum is ok, and sequence number is not a duplicate packet.
			*/

		}


		/*
			Iterate over the entire player map
			- If all player messages received successfully, return.
			- TODO: Check if a player has not responded in AUTOMATIC_DISCONNECTION_TIMER seconds, and set to inactive if so (i.e. no need to wait).
		*/
		bool isAllMessageReceived{ true };
		for (auto& player_session_pair : player_Session_Map)
		{
			Player_Session& player_session = player_session_pair.second;
			if (player_session.recv_data_to_recv == -1 ||  //No data received yet.
				player_session.recv_buffer.size() != player_session.recv_data_to_recv) //Not enough data received yet.
			{
				isAllMessageReceived = false;
				break;
			}
		}
		
		//Passed lockstep 
		if(isAllMessageReceived)
		
	}
}


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

	/*
		1. Create a UDP socket with port number based on client input
		2. Bind the UDP socket to the machine
		3. Call another function (which will run things in a while loop).
		- This function handles all server-client interactions.
		4. Close and release all resources properly after the function returns.
	*/

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
	udp_socket = socket(
		AF_INET, //IPV4
		SOCK_DGRAM, //UDP.
		IPPROTO_UDP);
	if (udp_socket == INVALID_SOCKET)
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
	if (bind(udp_socket, info_udp->ai_addr, static_cast<int>(info_udp->ai_addrlen)) != NO_ERROR) {
		std::cerr << "Bind failed" << std::endl;
		closesocket(udp_socket);
		udp_socket = INVALID_SOCKET;
		WSACleanup();
		return errorCode;
	}
	// Enable non-blocking I/O on the download socket.
	u_long enable = 1;
	ioctlsocket(udp_socket, FIONBIO, &enable);

	

	//Will run until game program closes (server-player interaction stops).
	GameProgram();
	

	// -------------------------------------------------------------------------
	// Clean-up after Winsock.
	//
	// WSACleanup()
	// -------------------------------------------------------------------------
	closesocket(udp_socket); //Shutdown not necessary.
	udp_socket = INVALID_SOCKET;
	WSACleanup();
}





