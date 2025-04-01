/* Start Header
*****************************************************************/
/*!
\file server.cpp
\author Joel Lee Jie, Rachel, Yee Tong
\date 19 March 2025
\brief
This file implements the server file which will be used to implement a
server that handles client-server communication.
- Quit command
- Listing of files to download
- File downloading.

It first establishes a connection with the client's socket, then reads the message from it.
Depending on the message contents, various actions can be taken.

File downloading is hosted on a separate thread that continually runs to read/write to the udp socket.

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
	Represents a file download session, started by the client REQ_DOWNLOAD.
*/
struct File_Session
{
	//Represents the current packet to send.
	int current_sequence_number{ -1 };
	std::string dest_ip_addr{};
	std::string dest_port_num{};
	std::filesystem::path file_path{};
	double time_last_packet_sent{};
	bool isSend{ false };

	sockaddr_storage addrDest{};
};

/*
	Represents individual client tcp socket connections.
*/
class CLIENT_INFO
{
public:
	CLIENT_INFO(SOCKET clientSocket, std::string ip_addr, std::string port_num)
		: clientSocket{ clientSocket }, ip_addr{ ip_addr }, port_num{ port_num }
	{
	}
	SOCKET clientSocket{};
	std::string ip_addr{};
	std::string port_num{};
	//Used for socket-specific lock, i.e. can't write to the same socket at the same time, but can write to two different sockets.
	std::mutex mutex_lock{};
};

bool execute(SOCKET clientSocket);
void disconnect(SOCKET& listenerSocket);
int Write_To_Socket(SOCKET client_socket, size_t num_bytes, char* data) noexcept;
void AddSocketToList(SOCKET client_socket, std::string ip_addr, std::string port_num);
void RemoveSocketFromList(SOCKET client_socket);
CLIENT_INFO& FindSocketInfo(SOCKET client_socket);
CLIENT_INFO& FindSocketInfo(std::string ip_addr, std::string port_num);
std::array<unsigned char, 4> GetIPAddressBytes(std::string ip_addr);
std::vector<uint8_t> format_RSP_ListFiles(const std::vector<std::string>& fileList);

//Used to manage tcp client connections.
std::list<CLIENT_INFO> current_tcp_connections{};
std::mutex tcp_connection_list_mutex{};

//Used to manage file download sessions.
//Any file session in the map is an active one, completed sessions are removed from map.
std::map<int, File_Session> file_sessions{};
std::mutex file_map_mutex{};

//Global variables, set and forget in main().
std::array<unsigned char, 4> server_ip_addr{};
int server_tcp_port_number{}, server_udp_port_number{};

//For file downloading
//The singular udp socket used to communicate with clients during file download.
//only one socket created/binded for the entire server program.
//All interactions with clients for file download is to/from this socket.
SOCKET file_download_socket{}; 
std::atomic<uint32_t> file_session{}; //Global variable, always incrementing used to assign session id.
std::string file_download_path{}; //Directory to download to.
int udp_data_size = 512; //default, will be read from file later.
float timeout = 0.5f; //Default, will be read from file later.

//Global variable, set for download thread to see so it can quit program.
std::atomic<bool> isProgramDone = false;

/*
	\brief
	Converts ip address and port number to a valid sockaddr_storage
	Helper function to convert destination ip/port to a sockaddr for sendto() udp.
*/
int resolvehelper(const char* hostname, int family, const char* service, sockaddr_storage* pAddr)
{
	int result;
	addrinfo* result_list = NULL;
	addrinfo hints = {};
	hints.ai_family = family;
	hints.ai_socktype = SOCK_DGRAM; // without this flag, getaddrinfo will return 3x the number of addresses (one for each socket type).
	result = getaddrinfo(hostname, service, &hints, &result_list);
	if (result == 0)
	{
		memcpy(pAddr, result_list->ai_addr, result_list->ai_addrlen);
		freeaddrinfo(result_list);
	}

	return result;
}

/*
	\brief
	Write to UDP file socket
	Only called by File_Download_Interaction, which is a single threaded function.
	Basically hardcoded for the UDP socket used for file downloading.
	\return
	Bytes written to socket.
	-1 on SOCKET_ERROR, 0 if other port has been gracefully closed, -2 if unable to write ip/port to socketaddr struct.
*/
int WriteToDownloadSocket(sockaddr_storage& addrDest, char* data, size_t num_bytes)
{
	size_t offset{};
	while (true)
	{
		//All data sent.
		if (offset >= num_bytes) break;
		int num_bytes_sent = sendto(file_download_socket, data + offset, static_cast<int>(num_bytes - offset), 0, (sockaddr*)&addrDest, sizeof(addrDest));
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
	The function for the UDP socket file downloader.
	Hosted on a separate thread.
	Runs continuously in the background, managing file download sessions in the map.
*/
void File_Download_Interaction()
{
	char buffer[1000];

	while (!isProgramDone)
	{
		/*
			Get data from socket, if any.
		*/
		char udp_buffer[1000]{};

		bool validated{};

		while (true) //read everything.
		{
			int const bytesReceived = recvfrom(file_download_socket, udp_buffer, 1000 - 1, 0, nullptr, nullptr);

			if (bytesReceived == SOCKET_ERROR || bytesReceived == 0) //WSAEWOULDBLOCK --> ERRORCODE
			{
				break;
			}

			//Bytesreceived can be -1, in the case of failedToRead reaching 3 or overflow from previous message (unused buffer)
			if (bytesReceived > 0) {

				uint16_t Checksum{};
				uint32_t Session_number{}, ACK{};

				memcpy_s(&Checksum, 2, udp_buffer, 2);
				Checksum = ntohs(Checksum);

				memcpy_s(&Session_number, 4, udp_buffer + 2, 4);
				Session_number = ntohl(Session_number);

				memcpy_s(&ACK, 4, udp_buffer + 6, 4);
				ACK = ntohl(ACK);

				void* data = udp_buffer + 2;  // Skip the first 2 bytes (Checksum)

				validated = ValidateChecksum(8, data, static_cast<uint16_t>(Checksum));

				if (validated) {
					int ack_value = ACK;
					std::lock_guard<std::mutex> file_map_lock{ file_map_mutex };
					if (file_sessions.find(Session_number) == file_sessions.end()) continue; //No valid session for this ID.
					if (ack_value == file_sessions[Session_number].current_sequence_number) {
						//Packet sent has been received, so send next packet.
						file_sessions[Session_number].current_sequence_number++;
						file_sessions[Session_number].isSend = true; //Send the next packet.
					}
					else if (ack_value < file_sessions[Session_number].current_sequence_number) {
						//Not what we expected, do nothing.
						//Only resend upon timeout.
					}
				}
				else
				{
					//corrupted ack, do nothing.
					//Only resend upon timeout.
				}
			}
		}


		/*
			Send packets if necessary.
		*/
		//Get current time in seconds, to compare against current time stored in struct.
		std::lock_guard<std::mutex> file_map_lock{ file_map_mutex };
		for (auto session = file_sessions.begin(); session != file_sessions.end();)
		{
			/*
				Condition to send packet
				1. Timeout finished.
				2. isSend is true.
			*/
			memset(buffer, 0, 1000);
			File_Session& session_ref = session->second;

			auto duration = std::chrono::system_clock::now().time_since_epoch();
			double current_time = std::chrono::duration<double>(duration).count();

			bool isSendPacket = false;

			//Either starting the file session, or when next packet should be sent.
			//Before this the sequence number has already been incremented, so don't change it again.
			if (session_ref.isSend)
			{
				session_ref.isSend = false;
				isSendPacket = true;
				//timeout -= 0.01f; //Maybe can have a smaller timeout since able to receive ACK successfully.
			}
			else if (current_time - session_ref.time_last_packet_sent > timeout)
			{
				//Increment timeout, it could be due to premature timeout.
				//timeout *= 1.5f;
				//timeout += 0.05f;
				isSendPacket = true;
				//Printing debug message to indicate packet failure.
				std::lock_guard<std::mutex> usersLock{ _stdoutMutex };
				std::cerr << "========PACKET TIMEOUT, NOT RECEIVED BY CLIENT OR CORRUPTED========\nPacket: " << session_ref.current_sequence_number << "\n================================" << std::endl;
			}

			if (!isSendPacket)
			{
				session++; //Increment iterator for next loop.
				continue; //Don't send packet for the current session.
			}

			/*
				======
				From this point, packet should be sent to client.
				Send the packet of [Current Sequence Number]
				So if triggered by timeout, it resends packet.
				If triggered by receiving correct ACK, it already increments the number before this, so don't need to do it again here.
			*/
			session_ref.time_last_packet_sent = current_time; //Reset timeout timer.
			/*
			* When sending packet
				1. Get file data
				- If cannot, that means invalid session so delete session and send download error?
				- By right shouldn't be cannot since the file was checked before adding to session map.

				2. Concatenate into a message with sequence number, data length and data.
				3. Get checksum and add it in.
				4. Send to udp port.

				[Checksum, 2][Sequence number, 4][data length, 4][file data payload …]
			*/


			bool isInvalidSession = false;
			//start session.
			int bytes_read = 0;
			try
			{
				//Buffer + 10, so it's already put in the right position.
				bytes_read = GetDataFromFile(session->second.file_path.string(), (session_ref.current_sequence_number-1)*(udp_data_size-10), udp_data_size - 10, buffer + 10);
				if (bytes_read == 0) isInvalidSession = true; //Nothing read from file, file downloading finished.
			}
			catch (std::runtime_error&)
			{
				//Invalid file read, so erase session.
				//By right this shouldn't occur, as session is only added if file is valid.
				isInvalidSession = true;
			}
			if (isInvalidSession)
			{
				session = file_sessions.erase(session);
				continue;
			}

			//File read is valid, data has been read and isn't 0.
			//Buffer already contains the data, starting at buffer+10 position.
			int bytes_to_send = bytes_read + 10; //10 accounts for [Checksum, 2][Sequence number, 4][data length, 4]

			//Convert to network order and insert sequence number/data length 
			uint32_t network_sequence = htonl((uint32_t)session_ref.current_sequence_number);
			uint32_t network_payload_length = htonl((uint32_t)bytes_read);
			memcpy_s(buffer + 2, 4, &network_sequence, 4);
			memcpy_s(buffer + 6, 4, &network_payload_length, 4);

			//Get checksum.
			//Checksum is all the data to send, not including checksum itself. This includes sequence number etc.
			uint16_t checksum = CalculateChecksum(bytes_to_send - 2, buffer + 2);
			checksum = htons(checksum);
			memcpy_s(buffer, 2, &checksum, 2);

			//Send message, since it's now formatted.
			int bytes_sent = WriteToDownloadSocket(session_ref.addrDest, buffer, bytes_to_send);
			{
				//std::lock_guard<std::mutex> usersLock{ _stdoutMutex };
				//std::cerr << "========PACKET SENT========\nPacket: " << session_ref.current_sequence_number << "   Payload: " << bytes_read << " bytes" << "\n================================" << std::endl;
			}
			//Gracefully closed, so end file download session by removing from map.
			if (bytes_sent == 0)
			{
				session = file_sessions.erase(session);
				continue;
			}

			session++;
		}

	}

}


/*
	\brief
	Entry point of this program, which creates a server that handles client-client communication and commands.
*/
int main(int argc, char* argv[])
{

	std::string portString{}, udp_port_string{};
	if (argc <= 2)
	{
		// Get Port Number
		std::cout << "Server TCP Port Number: ";
		std::getline(std::cin, portString);
		std::cout << "Server UDP Port Number: ";
		std::getline(std::cin, udp_port_string);
		std::cout << "Path                  : ";
		std::getline(std::cin, file_download_path);
	}
	else
	{
		portString = argv[1];
		udp_port_string = argv[2];
	}
	server_tcp_port_number = std::stoi(portString);
	server_udp_port_number = std::stoi(udp_port_string);
	try
	{
		std::filesystem::create_directory(file_download_path);
	}
	catch (std::exception&)
	{
		std::cerr << "Unable to create/open directory: " << file_download_path;
		return -1;
	}

	std::ifstream config_file{ "config.txt" };
	if (!config_file.is_open())
	{
		std::cerr << "Unable to open config file: " << std::endl;
		return -1;
	}
	config_file >> std::ws >> udp_data_size >> std::ws >> timeout;
	std::cout << "UDP Data Packet Size: " << udp_data_size << " bytes\n";
	std::cout << "Timeout: " << timeout << std::endl;


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
	*/ //JOEL START

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
	Writes to socket
	Ensures
	- All data written, even if it means multiple writes.
	- Multi-threading protection, specific to each socket.
	\param num_bytes
	Number of bytes for the message.
	\param data
	Message to send.
	\return
	- Number of bytes written
	- 0 if send() returns 0, i.e. FIN sent by client
	- -1 if unable to send and not because of blocking.
*/
int Write_To_Socket(SOCKET client_socket, size_t num_bytes, char* data) noexcept //Using char* instead of std::string, to prevent truncation of 0 at the end of the message.
{
	//Sleep for abit to prevent multiple distinct messages from becoming a single message.
	//May not need.
#if defined(DEBUG_ASSIGNTMENT2_TEST) || defined(DEBUG_ASSIGNMENT2_TEST)
	using namespace std::chronoliterals;
	std::this_thread::sleepfor(300ms);
#endif

	//Get the associated mutex
	CLIENT_INFO& socket_info = FindSocketInfo(client_socket);
	//Lock mutex, don't write to the same socket at the same time from multiple threads (i.e. when two sockets want to send a message to the same socket).
	//If unable to lock mutex, sleep for abit and try again later to prevent blocking.
	while (!socket_info.mutex_lock.try_lock())
	{
		using namespace std::chrono_literals;
		std::this_thread::sleep_for(200ms);
	}
	size_t offset{};
	while (true)
	{
		//All data sent.
		if (offset >= num_bytes) break;
		int num_bytes_sent = send(client_socket, data + offset, static_cast<int>(num_bytes - offset), 0);
		//Operation could be blocking, check to see what is the error.
		if (num_bytes_sent == SOCKET_ERROR)
		{
			size_t errorCode = WSAGetLastError();
			//Blocking, just sleep and continue.
			if (errorCode == WSAEWOULDBLOCK)
			{
				// A non-blocking call returned no data; sleep and try again.
				using namespace std::chrono_literals;
				std::this_thread::sleep_for(200ms);
				continue;
			}
			//Gracefully closed
			else if (errorCode == WSAESHUTDOWN)
			{
				socket_info.mutex_lock.unlock();
				return 0;
			}
			//Not because of blocking, something actually went wrong.
			socket_info.mutex_lock.unlock();
			return SOCKET_ERROR;
		}
		//Gracefully closed
		if (num_bytes_sent == 0)
		{
			socket_info.mutex_lock.unlock();
			return 0;
		}
		offset += num_bytes_sent;
	}

	socket_info.mutex_lock.unlock();

	return static_cast<int>(num_bytes);
}

/*
	\brief
	Adds tcp connection socket to the list
*/
void AddSocketToList(SOCKET client_socket, std::string ip_addr, std::string port_num)
{
	std::lock_guard<std::mutex> tcp_mutex{ tcp_connection_list_mutex };
	current_tcp_connections.emplace_back(client_socket, ip_addr, port_num);
}

/*
	\brief
	Removes tcp connection socket from list
*/
void RemoveSocketFromList(SOCKET client_socket)
{
	std::lock_guard<std::mutex> tcp_mutex{ tcp_connection_list_mutex };
	for (auto iter = current_tcp_connections.begin(); iter != current_tcp_connections.end(); iter++)
	{
		if (iter->clientSocket == client_socket)
		{
			current_tcp_connections.erase(iter);
			return;
		}
	}
}

/*
	\brief
	Finds and returns CLIENT_INFO for the corresponding socket
	Throws std::exception if not found.
*/
CLIENT_INFO& FindSocketInfo(SOCKET client_socket)
{
	std::lock_guard<std::mutex> tcp_mutex{ tcp_connection_list_mutex };
	for (auto iter = current_tcp_connections.begin(); iter != current_tcp_connections.end(); iter++)
	{
		if (iter->clientSocket == client_socket)
		{
			return *iter;
		}
	}
	throw std::exception("Unable to find socket");
}

/*
	\brief
	Finds and returns CLIENT_INFO for the corresponding socket
	Throws std::exception if not found.
*/
CLIENT_INFO& FindSocketInfo(std::string ip_addr, std::string port_num)
{
	std::lock_guard<std::mutex> tcp_mutex{ tcp_connection_list_mutex };
	for (auto iter = current_tcp_connections.begin(); iter != current_tcp_connections.end(); iter++)
	{
		if ((iter->port_num == port_num) && (iter->ip_addr == ip_addr))
		{
			return *iter;
		}
	}
	throw std::exception("Unable to find socket");
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
	Checks if file of that name exists inside the directory passed, and returns its size in bytes.
	Returns -1 if can't be found.
*/
static int GetFileLength(std::string name, std::string directory)
{
	std::filesystem::path file_path = std::filesystem::path(directory);
	if (std::filesystem::is_regular_file(file_path / name) == false) return -1;
	return (int)std::filesystem::file_size(file_path / name);
}

/*
	\brief
	A multi-threaded function called by each thread, where each thread handles a specific tcp connection (that may have the same server socket)
	Handles different commands and messages by the client of the tcp connection, and performs different actions as required.
	Will only return when tcp connection handled by this thread is closed.
	\return
	true to continue program, false to terminate all threads and program.
*/
bool execute(SOCKET clientSocket)
{
	// -------------------------------------------------------------------------
	// Receive some text and send it back.
	//
	// recv()
	// send()
	// -------------------------------------------------------------------------

	constexpr size_t BUFFER_SIZE = 1000;
	char buffer[BUFFER_SIZE];
	bool stay = true;

	//Used for getting an incomplete message.
	std::string complete_buffer{};  //Stores messages read in a buffer until the entire message is read.
	bool isMessageIncomplete = false;  //Determines whether to append received message to complete_buffer, or to treat it as a whole message (i.e. with commandID).
	int failed_to_read = 0; //Failsafe. If it can't read the message after X tries, it means no data left in recv buffer.

	std::string unused_buffer{}; //Stores unused messages after the command. For example, /e .... /e .... being passed to the server, the server will separate the two /e commands and put the latter inside this buffer.

	// Enable non-blocking I/O on a socket.
	u_long enable = 1;
	ioctlsocket(clientSocket, FIONBIO, &enable);


	while (true)
	{
		bool isDataCarriedOver = false;
		//Treat the received message as a new message, so clear old messages.
		if (!isMessageIncomplete)
		{
			complete_buffer.clear();
			complete_buffer = unused_buffer;
			if (!unused_buffer.empty())
			{
				isDataCarriedOver = true;
				unused_buffer.clear();
			}
			failed_to_read = 0;
		}
		memset(buffer, '\0', BUFFER_SIZE);

		/*
			- No message received (-1) --> check if it's because of non-blocking mode, if so then continue checking for message after sleeping.
			- FIN received (0) --> client shutdown initiated, so shutdown this connection and close the socket for this connection.
			- Incomplete message received (num_bytes doesn't match byte_received) --> enable isMessageIncomplete, to continue looping until all bytes received.
		*/
		const int bytesReceived = recv(
			clientSocket,
			buffer,
			BUFFER_SIZE - 1,
			0);
		//If nothing received
		if (bytesReceived == SOCKET_ERROR)
		{
			//Keep checking, but sleep for abit to allow other threads to run first.
			size_t errorCode = WSAGetLastError();
			if (errorCode == WSAEWOULDBLOCK)
			{
				// A non-blocking call returned no data; sleep and try again.
				using namespace std::chrono_literals;
				std::this_thread::sleep_for(200ms);
				/*
					Two scenarios
					- Checking for new message --> just continue checking
					- Incomplete message, trying to read it but unable to --> increment failed_to_read and continue checking.
					If it fails enough times, it means an incomplete command was sent to server.
				*/
				if (isMessageIncomplete) failed_to_read++;

				//==Unable to read the rest of the message after 3 tries, that means incomplete command was sent to server.
				//==Don't continue checking, just send down the line to be handled.
				if (failed_to_read >= 3)
				{
				}
				//==Unused buffer carried over from previous message, just send down the line to be handled.
				else if (isDataCarriedOver) {}
				else continue;
			}
			//Caused by force close of server socket
			else if (errorCode == WSAECONNRESET)
			{
				std::lock_guard<std::mutex> usersLock{ _stdoutMutex };
				std::cerr << "Client Socket Forced Closure." << std::endl;
				break;
			}
			//not caused by failing to read more data for an incomplete message.
			else
			{
				std::lock_guard<std::mutex> usersLock{ _stdoutMutex };
				std::cerr << "recv() failed. Gracefully closing." << std::endl;
				break;
			}
		}
		//FIN sent from client, so close the tcp connection gracefully.
		if (bytesReceived == 0)
		{
			std::lock_guard<std::mutex> usersLock{ _stdoutMutex };
			std::cerr << "Graceful shutdown." << std::endl;
			break;
		}

		/*
			From this point, a valid message is received.
		*/

		//Bytesreceived can be -1, in the case of failedToRead reaching 3 or overflow from previous message (unused buffer)
		if (bytesReceived > 0) complete_buffer.insert(complete_buffer.end(), buffer, buffer + bytesReceived);
		/*if (text == "*")
		{
			std::lock_guard<std::mutex> usersLock{ _stdoutMutex };
			std::cout << "Requested to close the server!" << std::endl;
			stay = false;
			break;
		}*/

		/*
			3 commands
			- 0x1 --> REQ_QUIT, so quit gracefully. Don't quit program.
			- 0x2/3 --> REQ_ECHO/RSP_ECHO, print out the message received and forward to the client.
			- 0x4 REQ_LISTUSERS --> return the list of users to the client.
		*/

		//TODO: a check for isMessageIncomplete, just in case the message is to be added to the complete_buffer instead opf checking command ID.
		//==Get Command ID
		char commandID = complete_buffer[0];
		bool isInvalidCommand = false;
		//Check if command is invalid.
		switch (commandID)
		{
		case 0x1: case 0x2: case 0x4:
			break;
		default: //Invalid command.
			isInvalidCommand = true;
			break;
		};
		////==REQ_QUIT or invalid command.
		if (commandID == 0x1 || isInvalidCommand)
		{
			std::lock_guard<std::mutex> usersLock{ _stdoutMutex };
			if (isInvalidCommand)
			{
				if (isDataCarriedOver) std::cerr << "Incorrect previous message length or invalid command received" << std::endl;
				else std::cerr << "Invalid Command received" << std::endl;
			}
			else std::cerr << "Graceful shutdown." << std::endl;
			break;
		}
		//REQ_DOWNLOAD message received
		//JOEL START
		if (commandID == 0x2)
		{
			/*
					Format of REC/RSP_ECHO
					[Command][IP, 4 bytes][Port, 2 bytes][Text length, 4 bytes][Filename]

					Minimum size is 11 bytes.
			*/
			/*
				Bytes received (in the buffer) may be incomplete.
				Check for 2 scenarios
				1. Incomplete, < 11 bytes (need to be up to text_length)
				2. Incomplete, < 11 + text_length bytes

				Note that null-terminator for string isn't considered (nor sent by client).
			*/

			//Incomplete message sent to server, but no more data to be read. Thus, invalid echo command.
			//Send ECHO_ERROR back to client.
			if (failed_to_read >= 3)
			{
				isMessageIncomplete = false;
				_stdoutMutex.lock();
				std::cerr << "Invalid Echo Message Length, too short" << std::endl;
				_stdoutMutex.unlock();
				//==Send ECHO_ERROR back to the client.
				char err_command = 0x30;
				int bytes_sent = Write_To_Socket(clientSocket, 1, &err_command);
				//==Error checking, -1 returned if send failed, 0 returned if nothing written (client socket has shutdown'd).
				if (bytes_sent == SOCKET_ERROR)
				{
					std::lock_guard<std::mutex> usersLock{ _stdoutMutex };
					std::cerr << "send() failed." << std::endl;
					break;
				}
				if (bytes_sent == 0)
				{
					std::lock_guard<std::mutex> usersLock{ _stdoutMutex };
					std::cerr << "Graceful shutdown." << std::endl;
					break;
				}
				continue; //Continue waiting for new message from client.
			}

			isMessageIncomplete = false; //Reset back to default before checking.
			if (complete_buffer.size() < 11)
			{
				isMessageIncomplete = true;
				continue;
			}


			/*
				Get
				- IP as byte array and convert to string
				- Port as ushort and convert to string
				- Text length as int

				And then check text length.
			*/
			//==Check text length to see if message is incomplete first
			uint32_t text_length{};
			memcpy_s(&text_length, 4, complete_buffer.data() + 7, 4);
			text_length = ntohl(text_length);
			//Note that text_length doesn't account for null-terminator, so "hello" text is size 5.
			if (complete_buffer.size() < static_cast<size_t>(11) + text_length) //Incomplete message.
			{
				isMessageIncomplete = true;
				continue;
			}

			//Keep the rest of the buffer that wasn't used.
			if (complete_buffer.size() > static_cast<size_t>(11) + text_length) unused_buffer = complete_buffer.substr(static_cast<size_t>(11) + text_length);
			//TODO: Text message may be longer than text_length, in which case may need to send ECHO_ERROR back.

			//==Get IP and Port
			std::array<unsigned char, 4> ip_byte_arr{};
			memcpy_s(ip_byte_arr.data(), 4, complete_buffer.data() + 1, 4); //No need ntohs as it's just 1 byte.
			uint16_t port_num{};
			memcpy_s(&port_num, 2, complete_buffer.data() + 5, 2);
			port_num = ntohs(port_num);

			//Convert ip and port to string
			std::ostringstream ip_string{};
			ip_string << static_cast<int>(ip_byte_arr[0]);
			for (int i = 1; i < 4; i++)
			{
				ip_string << "." << static_cast<int>(ip_byte_arr[i]);
			}
			std::string port_str = std::to_string(port_num);

			std::string file_name = complete_buffer.substr(11, text_length);
			int32_t file_size = GetFileLength(file_name, file_download_path);
			//File not found.
			if (file_size == -1)
			{
				char err_command = 0x30;
				int bytes_sent = Write_To_Socket(clientSocket, 1, &err_command);
				//==Error checking, -1 returned if send failed, 0 returned if nothing written (client socket has shutdown'd).
				if (bytes_sent == SOCKET_ERROR)
				{
					std::lock_guard<std::mutex> usersLock{ _stdoutMutex };
					std::cerr << "send() failed." << std::endl;
					break;
				}
				if (bytes_sent == 0)
				{
					std::lock_guard<std::mutex> usersLock{ _stdoutMutex };
					std::cerr << "Graceful shutdown." << std::endl;
					break;
				}
				continue; //Continue waiting for new message from client.
			}

			{
				std::lock_guard<std::mutex> usersLock{ _stdoutMutex };
				std::cout << "====RECEIVED DOWNLOAD REQUEST====" << std::endl;
				std::cout << "Socket: " << ip_string.str() << ":" << port_str << std::endl;
				std::cout << "File: " << file_name << " (" << file_size << ")" << std::endl;
				std::cout << "==============================" << std::endl;
			}

			/*
				Return RSP_DOWNLOAD
				Format: [cmd][IP, 4][Port, 2][Session ID, 4][File length, 4]

				15 byte message.
			*/
			//Add in command id, server ip, server udp port number.
			uint16_t port_num_to_send = static_cast<uint16_t>(server_udp_port_number);
			//==Convert to network-byte order.
			port_num_to_send = htons(port_num_to_send);
			char message_to_send[15]{};
			message_to_send[0] = 0x3;
			for (int i = 0; i < 4; i++) //ip address
			{
				message_to_send[i + 1] = server_ip_addr[i];
			}
			memcpy_s(message_to_send + 5, 2, &port_num_to_send, 2); //port number
			//Add in session number. Incremental for each download session.
			uint32_t ntohs_session = htonl(file_session++);
			memcpy_s(message_to_send + 7, 4, &ntohs_session, 4);
			//Add in file length.
			uint32_t file_length = file_size;
			file_length = htonl(file_length);
			memcpy_s(message_to_send + 11, 4, &file_length, 4);

			//Send RSP_DOWNLOAD to tcp socket.
			int	bytes_sent = Write_To_Socket(clientSocket, 15, message_to_send);
			//==Error checking, -1 returned if send failed, 0 returned if nothing written (client socket has shutdown'd).
			if (bytes_sent == SOCKET_ERROR)
			{
				std::lock_guard<std::mutex> usersLock{ _stdoutMutex };
				std::cerr << "send() failed." << std::endl;
				break;
			}
			if (bytes_sent == 0)
			{
				std::lock_guard<std::mutex> usersLock{ _stdoutMutex };
				std::cerr << "Graceful shutdown." << std::endl;
				break;
			}

			/*
				Start file transfer via udp socket.
			*/
			std::filesystem::path path = std::filesystem::path(file_download_path) / file_name;
			{
				//First packet to send starts at 1.
				//Use 0.f for time, to ensure it is in "timeout" state so the packet is immediately sent.
				File_Session new_file_session{ 1, ip_string.str(), port_str, path, 0.0, false, sockaddr_storage{} };
				int result = resolvehelper(ip_string.str().c_str(), AF_INET, port_str.c_str(), &new_file_session.addrDest);
				if (result != 0) //Unable to resolve to sockaddr_storage.
				{
					continue; //Unable to allocate.
				}
				//Ensure that the file downloader doesn't read the map while writing to it.
				std::lock_guard<std::mutex> file_map_lock{ file_map_mutex };
				file_sessions.emplace(file_session - 1, new_file_session);
			}

			//==Finished, continue checking on tcp side.
			continue;
		}



		
		//==REQ_LISTFILES
		if (commandID == 4)
		{
			// Keep the rest of the buffer that wasn't used.
			unused_buffer = complete_buffer.substr(1);

			// Use the file_download_path variable instead of the hardcoded "Downloads" path
			std::string downloadsPath = file_download_path;

			// Vector to store the list of files
			std::vector<std::string> fileList;

			// Check if the directory exists and is accessible
			try
			{
				if (!std::filesystem::exists(downloadsPath) || !std::filesystem::is_directory(downloadsPath))
				{
					std::lock_guard<std::mutex> usersLock{ _stdoutMutex };
					std::cerr << "Error: Directory does not exist or is not accessible: " << downloadsPath << std::endl;
					break;
				}

				// Iterate over the files in the directory
				for (const auto& entry : std::filesystem::directory_iterator(downloadsPath))
				{
					// Check if the entry is a regular file (not a directory)
					if (entry.is_regular_file())
					{
						// Add the filename to the list
						fileList.push_back(entry.path().filename().string());
					}
				}
			}
			catch (const std::filesystem::filesystem_error& e)
			{
				std::lock_guard<std::mutex> usersLock{ _stdoutMutex };
				std::cerr << "Error accessing directory: " << e.what() << std::endl;
				break;
			}

			// Format the list of files into the RSP_LISTFILES message format
			std::vector<uint8_t> response_message = format_RSP_ListFiles(fileList);

			// Write, in non-blocking format, to the client socket.
			// Send RSP_LISTFILES message to client.
			int bytes_sent = Write_To_Socket(clientSocket, response_message.size(), reinterpret_cast<char*>(response_message.data()));

			// Error checking, -1 returned if send failed, 0 returned if nothing written (client socket has shutdown'd).
			if (bytes_sent == SOCKET_ERROR)
			{
				std::lock_guard<std::mutex> usersLock{ _stdoutMutex };
				std::cerr << "send() failed." << std::endl;
				break;
			}
			if (bytes_sent == 0)
			{
				std::lock_guard<std::mutex> usersLock{ _stdoutMutex };
				std::cerr << "Graceful shutdown." << std::endl;
				break;
			}
			continue; // Continue waiting for message from client.
		}
		//==RSP_LISTFILES
		if (commandID == 5)
		{

		} //

	}


	// -------------------------------------------------------------------------
	// Shut down and close sockets.
	//
	// shutdown()
	// closesocket()
	// -------------------------------------------------------------------------
	RemoveSocketFromList(clientSocket);
	shutdown(clientSocket, SD_BOTH);
	closesocket(clientSocket);
	return stay;
}

/*
	Helper function used to format RSP_LISTFILES message.
*/
std::vector<uint8_t> format_RSP_ListFiles(const std::vector<std::string>& fileList) {
	std::vector<uint8_t> message;

	// Command ID (0x05)
	message.push_back(0x05);

	// Number of files (2 bytes, big-endian order)
	uint16_t numFiles = (uint16_t)fileList.size();
	message.push_back((numFiles >> 8) & 0xFF);
	message.push_back(numFiles & 0xFF);

	// Calculate total length of filename list
	uint32_t totalLength = 0;
	for (const auto& file : fileList) {
		totalLength += 4 + (uint32_t)file.size(); // 4 bytes for length, plus filename bytes
	}

	// Append total length in big-endian order (4 bytes)
	message.push_back((totalLength >> 24) & 0xFF);
	message.push_back((totalLength >> 16) & 0xFF);
	message.push_back((totalLength >> 8) & 0xFF);
	message.push_back(totalLength & 0xFF);

	// Append each filename with its length
	for (const auto& file : fileList) {
		uint32_t nameLen = (uint32_t)file.size();

		// Append filename length in big-endian order (4 bytes)
		message.push_back((nameLen >> 24) & 0xFF);
		message.push_back((nameLen >> 16) & 0xFF);
		message.push_back((nameLen >> 8) & 0xFF);
		message.push_back(nameLen & 0xFF);

		// Append filename string
		message.insert(message.end(), file.begin(), file.end());
	}

	return message;
}
