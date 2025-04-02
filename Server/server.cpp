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

#include "..\Utility.hpp"
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
		: addrDest{ addr }
	{
	}
	
	void SendLongMessage(const std::string& message)
	{
		if (message.empty()) return;
		std::string long_message = message;
		while (true)
		{
			//The last part of the message, add COMMAND_COMPLETE to indicate it is complete.
			if (long_message.size() <= MAX_PAYLOAD_SIZE - 1)
			{
				long_message = (char)(COMMAND_COMPLETE) + long_message;
				//String length is less than or equal to max payload size, so just push it all as one packet.
				messages_to_send.push(long_message);
				reliable_transfer.toSend = true;
				return; //All parts of the message have been sent.
			}
			else
			{
				//Add incomplete, to show that there are more packets on the way.
				long_message = (char)(COMMAND_INCOMPLETE)+long_message;
				//Can only send MAX_PAYLOAD_SIZE for each packet, as 6 extra bytes are left for seq number and checksum.
				messages_to_send.push(long_message.substr(0, MAX_PAYLOAD_SIZE));
				//Move to the next chunk of the message.
				long_message = long_message.substr(MAX_PAYLOAD_SIZE);
			}
			
		}
	}
	//Used to control reliable data transfer.
	Reliable_Transfer reliable_transfer{};
	//Used to determine if a player should be forcibly disconnected, like after X seconds of no response.
	double time_last_packet_received{ 20000000000000 };

	//Used to indicate how to send. Need to set when recvfrom is called.
	sockaddr_storage addrDest{};

	/*
		Indicates if the message stored in recv_buffer is complete.
		Set to false at the start of the frame, and if the packets received from the player come with "COMMAND_INCOMPLETE".
		Set to true when the last command packet from the player "COMMAND_COMPLETE" is received.
	*/
	bool is_recv_message_complete = false;
	//Stores messages received from player, cleared at the end of each frame.
	//Only stores "COMMAND" type messages, so it doesn't store ACK or JOIN_REQUEST messages.
	std::string recv_buffer{};

	/*
		Each string in the vector signifies a packet to send.
		Packets are sent in order (FCFS).
		Only one packet is sent at a time. The next packet can only be sent when the first packet has been ACK'd.
		Don't include checksum or seq number as they will be automatically added.

		Each string contains [GeneralCommandID] as the first byte, indicating the type of packet it is (Either COMMAND_INCOMPLETE or COMMAND_COMPLETE).
		ACK and JOIN_RESPONSE aren't sent this way, as they only need to be sent once without needing to be ACK'd.
		Ensure packets confirm to MAX_PAYLOAD_SIZE (not MAX_PACKET_SIZE).
	*/
	std::queue<std::string> messages_to_send{};
};

/*
	Represents a packet received from socket, used so they can be added to a queue.
*/
struct Packet
{
	sockaddr_storage senderAddr{};
	std::string data{};
	int seq_or_ack_number{}; //Either sequence number or ACK.
};

//Time before server stops waiting for player response, and disconnects them.
constexpr float AUTOMATIC_DISCONNECTION_TIMER = 4.f;

//Used to manage interactions with players, including sending/receiving, automatic disconnection, reliable data transfer.
std::map<int, Player_Session> player_Session_Map{};
std::mutex session_map_lock{};

//Server information, set and forget in main().
std::array<unsigned char, 4> server_ip_addr{};
int server_tcp_port_number{}, server_udp_port_number{};

//Controls what the next player's ID should be, to prevent players from having the same ID.
//Reconnecting players will reconnect via sending the player_ID, letting the server know which session to reassume.
int player_id = 0;

//For any sending/receiving, set at the start.
SOCKET udp_socket{};
std::mutex socket_lock{};

//For temporarily storing packets received.
std::queue<Packet> packet_recv_queue{};
std::mutex packet_queue_lock{};

//Indicates if the game has ended, so0 the multi-threaded functions can end too.
bool isGameRunning{ true };

//Forward declarations:
//void HandleStartGame();
/*
	Thread-safe atomic writing to console.
*/
void PrintString(const std::string& message_to_print)
{
#ifdef _DEBUG
	static std::mutex console_mutex{};
	std::lock_guard<std::mutex> console_lock{ console_mutex };
	std::cout << message_to_print << std::endl;
#endif
}

/*
	\brief
	The starting point where server-player interactions are managed.
	Called in main after udp_socket has been created and binded.
	Return (without closing udp_socket) when server-player interactions are completed.
*/
void GameProgram()
{
	//Wait for players to join.
	//HandleStartGame();
	while (isGameRunning)
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
		//ReceiveAllMessages();

	}
}

///*
//	\brief
//	Continually get join requests from players, adding them as new players to the map.
//	This happens until the START command is given, which is then echoed to all players and then the game starts (the function returns).
//*/
//void HandleStartGame()
//{
//	char temp_buffer[MAX_BUFFER_SIZE]{};
//	while (true)
//	{
//		memset(temp_buffer, 0, MAX_BUFFER_SIZE);
//		//==Read any incoming message.
//		sockaddr_storage sender_addr{}; //temporarily store sender's address.
//		int size_sockaddr = sizeof(sender_addr);
//		int bytes_read{};
//		
//		{
//			std::lock_guard<std::mutex> socket_locker{socket_lock};
//			bytes_read = recvfrom(udp_socket, temp_buffer, MAX_BUFFER_SIZE, 0, (sockaddr*)&sender_addr, &size_sockaddr);
//		}
//		 
//		
//		//Join request or Start request detected.
//		//Both only has 1 byte.
//		if (bytes_read == 1)
//		{
//			char command_ID = temp_buffer[0];
//			//Player wants to join.
//			if (command_ID == JOIN_REQUEST)
//			{
//				int client_player_id = -1; //-1 to indicate it doesn't have a player id yet.
//				//Iterate over the map, to see if the player already is in the game (maybe they never received the JOIN_RESPONSE).
//				for (auto& player_entry : player_Session_Map)
//				{
//					//Check if they're already in the map.
//					if (!Compare_SockAddr(&sender_addr, &player_entry.second.addrDest)) continue;
//					//They are already in the map.
//					client_player_id = player_entry.first;
//				}
//
//				//No player entry found for this ip address, so add in a new entry.
//				if (client_player_id == -1)
//				{
//					client_player_id = player_id;
//					/*
//						Store new player information into the map.
//					*/
//					player_Session_Map.emplace(player_id++, Player_Session{ sender_addr });
//				}
//
//				//Send the information back to the player as a JOIN_RESPONSE, [Checksum, 2][0x21][Player_ID, 2].
//				uint16_t network_player_id = htons((uint16_t)client_player_id);
//				char buffer[10]{};
//				buffer[2] = JOIN_RESPONSE;
//				memcpy_s(buffer+3, 2, &network_player_id, 2);
//				uint16_t checksum = CalculateChecksum(3, buffer + 2);
//				checksum = htons(checksum);
//				memcpy_s(buffer, 2, &checksum, 2);
//				std::lock_guard<std::mutex> socket_locker{socket_lock};
//				WriteToSocket(udp_socket, sender_addr, buffer, 5);
//			}
//		}
//
//		/*
//			TODO: Handle start request, ensuring that all players receive start command via ACK.
//		*/
//
//	}
//}

///*
//	\brief
//	Called by GameProgram() to ensure all messages are received (and ACK'd) from players.
//	- If player hasn't responded for some time, they are removed from the map and the server no longer waits for them.
//	- If a new player requests to join, they are assigned a new ID.
//
//	Follows reliable data transfer protocols.
//*/
//void ReceiveAllMessages()
//{
//	char temp_buffer[MAX_BUFFER_SIZE]{};
//	while (true)
//	{
//		/*
//			TODO: Automatic/Manual Disconnection and Reconnection not being considered yet.
//		*/
//		memset(temp_buffer, 0, MAX_BUFFER_SIZE);
//		//==Read any incoming message.
//		sockaddr_storage sender_addr{}; //temporarily store sender's address.
//		int size_sockaddr = sizeof(sender_addr);
//		int bytes_read{};
//
//		{
//			std::lock_guard<std::mutex> socket_locker{socket_lock};
//			bytes_read = recvfrom(udp_socket, temp_buffer, MAX_BUFFER_SIZE, 0, (sockaddr*)&sender_addr, &size_sockaddr);
//		}
//		//Minimum message size is 6 bytes (disregarding the join and start commands at the start of the game) to account for checksum and sequence number.
//		if (bytes_read >= 6)
//		{
//			/*
//				First ensure that the checksum is ok, and sequence number is not a duplicate packet.
//			*/
//
//		}
//
//
//		/*
//			Iterate over the entire player map
//			- If all player messages received successfully, return.
//			- TODO: Check if a player has not responded in AUTOMATIC_DISCONNECTION_TIMER seconds, and set to inactive if so (i.e. no need to wait).
//		*/
//		bool isAllMessageReceived{ true };
//		for (auto& player_session_pair : player_Session_Map)
//		{
//			Player_Session& player_session = player_session_pair.second;
//			if (player_session.recv_data_to_recv == -1 ||  //No data received yet.
//				player_session.recv_buffer.size() != player_session.recv_data_to_recv) //Not enough data received yet.
//			{
//				isAllMessageReceived = false;
//				break;
//			}
//		}
//		
//		//Passed lockstep 
//		//if (isAllMessageReceived);
//		
//	}
//}

namespace {
	//Just used to shift writing of socket to outside the map, to prevent locking of 2 mutexes (which may lead to deadlock if not done well).
	struct WriteData
	{
		sockaddr_storage addrDest;
		std::string data;
	};
}


/*
		It should be called in a separate thread.
		Will continually read messages from the udp socket, adding them to the packet queue for another function to handle.
		Will write messages to socket as needed (for messages that require ACK) based on toSend and timeout.

		Note:
		- Only messages that require an ACK should be sent in this method. Otherwise, just write as per normal (note need to use mutex lock)
		- To Send: Set the send buffer to the message (excluding checksum and sequence number). Set toSend to be true.
		- Packet data in the queue has their checksum and sequence number stripped away. They are all confirmed to be uncorrupted,
		and the number is a separate variable from the data.
		- Only access the queue through a mutex.
	*/
void ReceiveSendMessages()
{
	//Buffer used for receiving data.
	char buffer[MAX_BUFFER_SIZE]{};

	while (isGameRunning)
	{
		std::vector<WriteData> data_to_write{};
		/*
			Send all pending messages
			- Timeout has run out
			- isSend is activated.

			Ensure send buffer isn't empty.
		*/
		{
			std::lock_guard<std::mutex> map_lock{ session_map_lock };
			for (auto& player_pair : player_Session_Map)
			{
				auto& session = player_pair.second;
				//Ensure that data being written is not empty.
				if (session.messages_to_send.empty()) continue;
				if (session.messages_to_send.front().empty())
				{
					session.messages_to_send.pop();
					continue;
				}
				if (GetTime() - session.reliable_transfer.time_last_packet_sent > TIMEOUT_TIMER)
				{
					session.reliable_transfer.toSend = true;
				}
				if (!session.reliable_transfer.toSend) continue;
				//Below here, packet is to be sent.
				std::string message_to_send = session.messages_to_send.front();
				//Don't pop unless ACK'd
				
				//Add sequence number to the send.
				uint32_t network_sequence_number = htonl(session.reliable_transfer.current_sequence_number);
				char number_buffer[4]{};
				memcpy_s(number_buffer, 4, &network_sequence_number, 4);
				std::string sequence_number(number_buffer, number_buffer + 4); //Casting the number to a string.

				std::string data = sequence_number + message_to_send;

				//Add in checksum, convert to network order.
				uint16_t checksum = htons(CalculateChecksum(data.size(), data.data()));
				//Collate everything into a single string.
				memcpy_s(number_buffer, 2, &checksum, 2); //overwrite the previous data in buffer for 0 and 1 byte.
				std::string checksum_string(number_buffer, number_buffer + 2); //Convert checksum to number.
				data = checksum_string + data; //checksum in front of data.

				//Reset timeout.
				session.reliable_transfer.time_last_packet_sent = GetTime();
				session.reliable_transfer.toSend = false;
				data_to_write.push_back({ session.addrDest, data });

				PrintString("MESSAGE SENT, Seq Num: " + std::to_string(session.reliable_transfer.current_sequence_number) + " Data: " + data);
			}
		}
		{
			std::lock_guard<std::mutex> socket_locker{ socket_lock };
			for (WriteData& write_data : data_to_write)
			{
				//Send data over
				WriteToSocket(udp_socket, write_data.addrDest, write_data.data.data(), write_data.data.size());
			}
		}

		memset(buffer, 0, MAX_BUFFER_SIZE);
		/*
			Receive all messages.
			Upon receiving, add the packet to the queue.
		*/
		//==Read any incoming message.
		sockaddr_storage sender_addr{}; //temporarily store sender's address.
		int size_sockaddr = sizeof(sender_addr);
		int bytes_read{};
		{
			std::lock_guard<std::mutex> socket_locker{ socket_lock };
			bytes_read = recvfrom(udp_socket, buffer, MAX_BUFFER_SIZE, 0, (sockaddr*)&sender_addr, &size_sockaddr);
		}
		if (bytes_read <= 0) continue; //Since it is a non-blocking socket read.


		//==Check if valid.
		//Do not accept any message that doesn't have both a checksum and a sequence number
		//since the game always uses RDT protocol for all messages.
		if (bytes_read < 6) continue;
		int number = ReadChecksumAndNumber(buffer, bytes_read);
		if (number == -1) continue; //Checksum failed.

		//==Add to queue
		//Exclude checksum and sequence number from the data.
		std::string data_recv(buffer + 6, buffer + bytes_read);
		std::lock_guard<std::mutex> packet_locker{ packet_queue_lock };
		packet_recv_queue.push(Packet{ sender_addr, data_recv, number });
	}
}

/*
	\brief
	Should be called in a separate thread.
	Will continually read the packet queue and act on it
*/
void HandleReceivedPackets()
{
	/*
		All packets in the queue are data only (without sequence number/ack/checksum).
		All packets in the queue are uncorrupted (checksum check has passed).
		Sequence number/Ack number is located under Packet, as a separate variable.
		Size of packets may start from 0, don't assume there is data inside.

		Use mutex to pop packets from the queue, one at a time.
	*/
	while (isGameRunning)
	{
		Packet packet;
		{
			std::lock_guard<std::mutex> packet_locker{ packet_queue_lock };
			if (packet_recv_queue.empty()) continue;
			packet = packet_recv_queue.front();
			packet_recv_queue.pop();
		}
		/*
			Types of packets
			- ACK: [General Command ID (ACK) unsigned char][2 bytes unsigned, player id]
			- Non ACK: Reply with ACK [Checksum][ACK number][General Command ID (ACK) unsigned char]
				- player in map: [General Command ID unsigned char][2 bytes unsigned, player id]
				- player not in map: [General Command ID unsigned char]
		*/

		//Just discard empty packets since no command ID.
		if (packet.data.empty()) continue;
		unsigned char command_ID = packet.data[0];


		if (command_ID == ACK)
		{
			//Not enough data since no player ID.
			if (packet.data.size() < 3) continue;
			//Get the player ID, for checking against the map.
			uint16_t player_id{};
			memcpy_s(&player_id, 2, packet.data.data() + 1, 2);
			player_id = ntohs(player_id);

			//Find the player in the map.
			std::lock_guard<std::mutex> map_lock{ session_map_lock };
			auto iter = player_Session_Map.find((int)player_id);
			PrintString(std::string("ACK RECV, Seq Num: ") + std::to_string(packet.seq_or_ack_number) + " Player ID: " + std::to_string(player_id));

			//Can't be found in map, so ignore the packet.
			if (iter == player_Session_Map.end()) continue;
			Player_Session& session = iter->second;

			/*
				Using ACK number, decide what to do with ACK.
				If ACK == current sequence number, packet has been received successfully
				- Increment sequence number, get rid of the current packet to send the next packet.
				- Reset timeout to infinity.
				- If there's buffer left, set isSend to true.
			*/
			//==ACK number is less than packet sent out, so ignore.
			if (packet.seq_or_ack_number < session.reliable_transfer.current_sequence_number) continue;
			//==ACK matches packet sent out, meaning packet received successfully.
			session.reliable_transfer.current_sequence_number++;
			//Clear all data that has been sent successfully.
			if (!session.messages_to_send.empty()) session.messages_to_send.pop();

			//Reset timers
			session.time_last_packet_received = GetTime();
			session.reliable_transfer.time_last_packet_sent = 20000000000000; //Reset to some time in the future to effectively set timeout to infinity.
			//Send the remaining data in the buffer, if any.
			if (!session.messages_to_send.empty()) session.reliable_transfer.toSend = true;
			continue; //Handling of packet finished.
		}
		//==Below here, it is a non-ACK packet (i.e. command).




		/*
			Two scenarios
			1. Player looking to join --> [General Command ID] only
			- Send back [General Command ID][Player ID, 2] as JOIN_RESPONSE
			2. Existing player --> [General Command ID][Player ID][Length of message][Command ID]...
			- Send back ACK.
			- Add to recv buffer if necessary.
			In both cases, increment ack_last_packet_received if it's higher.
		*/
		if (command_ID == JOIN_REQUEST)
		{
			/*
				Check if it's a duplicate message, like if they're an existing player but don't know yet.
				1. Check if player in map
				If so, then don't assign a new entry and player id. instead reuse the player id.
				2. Send back join response using rdt (set send buffer).
				[Checksum, 2][ACK, 4][Command ID][Player_ID, 2].
				Player repeatedly sends JOIN request to server until JOIN_RESPONSE is sent back.
				Since server receives the JOIN request, it should also increment its ack of last packet received when it first receives.
			*/
			char ack_buffer[9]{};
			{
				std::lock_guard<std::mutex> map_lock{ session_map_lock };


				/*
					After receiving request, get its existing or new player id.
				*/
				int client_player_id = -1; //-1 to indicate it doesn't have a player id yet.
				//Iterate over the map, to see if the player already is in the game (maybe they never received the JOIN_RESPONSE).
				for (auto& player_entry : player_Session_Map)
				{
					//Check if they're already in the map.
					if (!Compare_SockAddr(&packet.senderAddr, &player_entry.second.addrDest)) continue;
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
					player_Session_Map.emplace(player_id++, Player_Session{ packet.senderAddr });
				}

				//Send the information back to the player as a JOIN_RESPONSE, [Checksum, 2][ACK, 4][Command ID][Player_ID, 2].
				uint16_t network_player_id = htons((uint16_t)client_player_id);


				//Add in ACK number and command ID.
				uint32_t network_response_ACK = htonl(packet.seq_or_ack_number);
				memcpy_s(ack_buffer + 2, 4, &network_response_ACK, 4);
				ack_buffer[6] = JOIN_RESPONSE;
				memcpy_s(ack_buffer + 7, 2, &network_player_id, 2);
				//Calculate and add in checksum.
				uint16_t network_checksum = htons(CalculateChecksum(7, ack_buffer + 2));
				memcpy_s(ack_buffer, 2, &network_checksum, 2);


				Player_Session& session = player_Session_Map.find(client_player_id)->second;
				session.time_last_packet_received = GetTime();
				//Increment ack of last packet received.
				if (session.reliable_transfer.ack_last_packet_received < packet.seq_or_ack_number)
				{
					session.reliable_transfer.ack_last_packet_received = packet.seq_or_ack_number;
				}
				PrintString("JOIN_REQUEST RECV, Seq Num: " + std::to_string(packet.seq_or_ack_number) + " Player ID: " + std::to_string(client_player_id));
			}
			//Send back JOIN response to sender.
			{
				std::lock_guard<std::mutex> socket_locker{ socket_lock };
				WriteToSocket(udp_socket, packet.senderAddr, ack_buffer, 9);
			}
			continue;
		}


		/*
			COMMAND_INCOMPLETE or COMMAND_COMPLETE.

			Just add their message (whatever it is) to the map.
			Set messageIncomplete to false or true depending on the general command.
		*/
		/*
			*Send back an ACK for the packet received. Don't need to for JOIN_REQUEST, as JOIN_RESPONSE is already an ACK.
			*Format: [Checksum, 2][ACK, 4][ACK command ID, 1]
		*/
		char ack_buffer[7]{};
		//Add in ACK number and command ID.
		uint32_t network_response_ACK = htonl(packet.seq_or_ack_number);
		memcpy_s(ack_buffer + 2, 4, &network_response_ACK, 4);
		ack_buffer[6] = ACK;
		//Calculate and add in checksum.
		uint16_t network_checksum = htons(CalculateChecksum(5, ack_buffer + 2));
		memcpy_s(ack_buffer, 2, &network_checksum, 2);

		//Send back ACK response to sender.
		{
			std::lock_guard<std::mutex> socket_locker{ socket_lock };
			WriteToSocket(udp_socket, packet.senderAddr, ack_buffer, 7);
		}

		/*
			In both cases, add to the recv buffer. Set message complete to be true or false depending.
		*/
		if (command_ID == COMMAND_COMPLETE || command_ID == COMMAND_INCOMPLETE)
		{
			//Message format: [General Command = COMMAND][Player ID, 2][Command ID]...[Command ID 2]
			//Not enough data since no player ID.
			if (packet.data.size() < 3) continue;
			std::lock_guard<std::mutex> map_lock{ session_map_lock };
			//Get the player ID, for checking against the map.
			uint16_t player_id{};
			memcpy_s(&player_id, 2, packet.data.data() + 1, 2);
			player_id = ntohs(player_id);
			auto player_session_iter = player_Session_Map.find(player_id);
			//Invalid player ID, no such player.
			if (player_session_iter == player_Session_Map.end()) continue;

			//==From here, player is valid. 

			Player_Session& session = player_session_iter->second;
			session.time_last_packet_received = GetTime(); //Reset timer.
			//==Check if it's a new message, by comparing packet number with the last successful packet.
			if (session.reliable_transfer.ack_last_packet_received >= packet.seq_or_ack_number) continue;
			//it's a new packet, so update the stored ack number.
			session.reliable_transfer.ack_last_packet_received = packet.seq_or_ack_number;


			/*
				Add to the player's recv buffer after removing [General Command ID] and [Player ID]
				This is because both general command ID and player ID are no longer necessary (any message in the player recvbuffer is both a COMMAND and belongs to that player).
				Doing this also helps to chain incomplete packets together.
			*/
			session.recv_buffer.insert(session.recv_buffer.end(), packet.data.begin()+3, packet.data.end());
			if (command_ID == COMMAND_COMPLETE) session.is_recv_message_complete = true;
			else session.is_recv_message_complete = false; //Still need to wait for more packets.

			PrintString("MESSAGE RECV, Seq Num: " + std::to_string(packet.seq_or_ack_number) + " Data: " + packet.data);
		}
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


	/* PRINT SERVER IP ADDRESS AND PORT NUMBER */
	char serverIPAddr[1000];
	struct sockaddr_in* serverAddress = reinterpret_cast<struct sockaddr_in*> (info_udp->ai_addr);
	inet_ntop(AF_INET, &(serverAddress->sin_addr), serverIPAddr, INET_ADDRSTRLEN);
	getnameinfo(info_udp->ai_addr, static_cast <socklen_t> (info_udp->ai_addrlen), serverIPAddr, sizeof(serverIPAddr), nullptr, 0, NI_NUMERICHOST);
	std::cerr << std::endl;
	std::cerr << "Server IP Address: " << serverIPAddr << std::endl;
	std::cerr << "Server UDP Port Number: " << udp_port_string << std::endl;

	/*
		1st thread.
		Will continually read messages from the udp socket, adding them to the packet queue for another function to handle.
		Will write messages to socket as needed (for messages that require ACK) based on toSend and timeout.

		Note:
		- Only messages that require an ACK should be sent in this method. Otherwise, just write as per normal (note need to use mutex lock)
		- To Send: Set the send buffer to the message (excluding checksum and sequence number). Set toSend to be true.
		- Packet data in the queue has their checksum and sequence number stripped away. They are all confirmed to be uncorrupted,
		and the number is a separate variable from the data.
		- Only access the queue through a mutex.
	*/
	std::thread thread_to_receive_and_send_message(ReceiveSendMessages);

	/*
		\brief
		Should be called in a separate thread.
		Will continually read the packet queue and act on it
	*/
	std::thread thread_to_handle_messages(HandleReceivedPackets);
	//Will run until game program closes (server-player interaction stops).
	GameProgram();


	// -------------------------------------------------------------------------
	// Clean-up after Winsock.
	//
	// WSACleanup()
	// -------------------------------------------------------------------------
	if (thread_to_receive_and_send_message.joinable()) thread_to_receive_and_send_message.join();
	if (thread_to_handle_messages.joinable()) thread_to_handle_messages.join();
	closesocket(udp_socket); //Shutdown not necessary.
	udp_socket = INVALID_SOCKET;
	WSACleanup();
}





