/* /* Start Header
*****************************************************************/
/*!
\file client.cpp
\author Joel Lee Jie, Rachel, Yee Tong
\date 19 March 2025
\brief
This file implements the client file which will be used to implement a
client that handles client-server communication.
- Quit command
- Listing of files to download
- File downloading.

It first establishes a connection with the server's socket, then reads the message from it.
Depending on the message contents, various actions can be taken.

Note that stdin and message communication like downloading can be done simultaneuosly, but any feedback from inputs only come after the file downloading.
This meets rubrics as stdin can still be done during downloading.

Copyright (C) 2025 DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the
prior written consent of DigiPen Institute of Technology is prohibited.
*/
/* End Header
*******************************************************************/

/*******************************************************************************
 * A multi-threaded TCP/IP client application with non-blocking user input + communication with server
 ******************************************************************************/

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "Windows.h"		// Entire Win32 API...
#include "winsock2.h"		// ...or Winsock alone
#include "ws2tcpip.h"		// getaddrinfo()

 // Tell the Visual Studio linker to include the following library in linking.
 // Alternatively, we could add this file to the linker command-line parameters,
 // but including it in the source code simplifies the configuration.
#pragma comment(lib, "ws2_32.lib")

#include <iostream>			// cout, cerr
#include <string>			// string
#include <vector>
//For read and write formatting
#include <sstream>
#include <iomanip>

//For Multi-threading
#include <thread>
#include <mutex>
#include <chrono>

//For communication between main thread and secondary thread.
#include <queue>
#include <array>

#include <filesystem>
#include "Checksum.hpp"
#include <fstream>


#ifndef WINSOCK_VERSION
#define WINSOCK_VERSION     2
#endif
#define WINSOCK_SUBVERSION  2
#define MAX_STR_LEN         1000
#define RETURN_CODE_1       1
#define RETURN_CODE_2       2
#define RETURN_CODE_3       3
#define RETURN_CODE_4       4





//Ensures race-free printing to console, to ensure clean printout.
std::mutex stdout_mutex{};
//Ensures race-free change of input_queue
std::mutex input_queue_mutex{};
//For communication between main thread and secondary thread.
std::queue<std::string> input_queue{};
std::atomic<bool> isThreadDone = false;

/*
	Singular udp socket to manage communications with server.
	Used for file downloading.
*/
SOCKET file_download_socket{};
//Directory to download files to.
std::string file_download_folder{};
//Indicates the current file being/to be downloaded.
std::string file_path{};
//addrDest and sockaddr used as temp variables. They are the server udp socket addresses, and are global so they can be set once and no need to set again.
sockaddr_storage addrDest = {};
sockaddr* server_sockaddr{};
int size_sock_addr{};

/*
	\brief
	Converts ip address and port number to a valid sockaddr_storage
	This sockaddr_storage is used in sendto().
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
	\return
	Bytes written to socket.
	-1 on SOCKET_ERROR, 0 if other port has been gracefully closed, -2 if unable to write ip/port to socketaddr struct.
*/
int WriteToDownloadSocket(std::string dest_ip_address, std::string dest_port, char* data, size_t num_bytes)
{
	//Setting of temp variable if it hasn't been set yet.
	if (!server_sockaddr)
	{
		int result = resolvehelper(dest_ip_address.c_str(), AF_INET, dest_port.c_str(), &addrDest);
		if (result != 0) //Unable to resolve to sockaddr_storage.
		{
			return -2; //No bytes written.
		}
		server_sockaddr = (sockaddr*)&addrDest;
		size_sock_addr = sizeof(addrDest);
	}


	size_t offset{};
	while (true)
	{
		//All data sent.
		if (offset >= num_bytes) break;
		int num_bytes_sent = sendto(file_download_socket, data + offset, static_cast<int>(num_bytes - offset), 0, server_sockaddr, size_sock_addr);
		//Operation could be blocking, check to see what is the error.
		if (num_bytes_sent == SOCKET_ERROR)
		{
			size_t errorCode = WSAGetLastError();
			//Blocking, just sleep and continue.
			if (errorCode == WSAEWOULDBLOCK)
			{
				// A non-blocking call returned no data; sleep and try again.
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
	Atomic pop of queue.
*/
std::string PopFromQueue()
{
	std::lock_guard<std::mutex> command_mutex{ input_queue_mutex };
	if (input_queue.size() == 0) return {};
	std::string message = input_queue.front();
	input_queue.pop();
	return message;
}

/*
	Atomic add to queue.
*/
void AddToQueue(std::string input)
{
	std::lock_guard<std::mutex> command_mutex{ input_queue_mutex };
	input_queue.push(input);
}

/*
	\brief
	Writes to socket
	Ensures
	- All data written, even if it means multiple writes.
	- Non-blocking.
	\param num_bytes
	Number of bytes for the message.
	\param data
	Message to send.
	\return
	- Number of bytes written
	- 0 if send() returns 0, i.e. FIN sent by client
	- -1 if unable to send and not because of blocking.
*/
int Write_To_Socket(SOCKET client_socket, size_t num_bytes, const char* data) noexcept //Using char* instead of std::string, to prevent truncation of 0 at the end of the message.
{
	//Sleep for abit to prevent multiple distinct messages from becoming a single message.
	//May not need.
#if defined(DEBUG_ASSIGNTMENT2_TEST) || defined(DEBUG_ASSIGNMENT2_TEST)
	using namespace std::chronoliterals;
	std::this_thread::sleepfor(300ms);
#endif
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

	return static_cast<int>(num_bytes);
}

/*
	\brief
	Reads user input from the queue (appended to by main thread), and acts on it accordingly, sending/receiving messages to the server.
	When it returns, it indicates that the tcp connection is closed.
	\param client_socket
	The client-socket after connecting with the server.
*/
void ManageServerInteraction(SOCKET client_socket)
{
	/*
		Every loop
		1. Read from queue and act on the user input accordingly.
		2. Read messages from server and act accordingly (e.g. output to console or reply back).
		3. Break loop if /q message is detected, or recv/send returns FIN(graceful shutdown) or error.
	*/

	char buffer[1000]{};
	//Used for getting an incomplete message.
	std::string complete_buffer{};  //Stores messages read in a buffer until the entire message is read.
	bool isMessageIncomplete = false;  //Determines whether to append received message to complete_buffer, or to treat it as a whole message (i.e. with commandID).
	int failed_to_read = 0; //Failsafe. If it can't read the message after X tries, it means no data left in recv buffer.

	std::string unused_buffer{}; //Stores unused messages after the command. For example, /e .... /e .... being passed to the server, the server will separate the two /e commands and put the latter inside this buffer.

	while (true)
	{
		/*
			Read user input.
			Input won't be added if < 2 in size.
			Thus, only need to check for empty string (returned if nothing in queue).
		*/
		std::string user_input = PopFromQueue();
		//==Manage user input, if found.
		//Check for the commands /t or /q or /e or /l.
		if (user_input.size() >= 2)
		{
			std::string commandID = user_input.substr(0, 2);

			//Received the quit message. Send "QUIT" to the server, and return from this function to indicate tcp connection is to be closed.
			if (commandID == "/q")
			{
				//quit message
				char quit_command = { '\x01' };
				int bytes_sent = Write_To_Socket(client_socket, 1, &quit_command);
				break; //quit.
			}
			//Received list command
			else if (commandID == "/l")
			{
				char list_command = { '\x04' };
				int bytes_sent = Write_To_Socket(client_socket, 1, &list_command);
				//==Error checking, -1 returned if send failed, 0 returned if nothing written (client socket has shutdown'd).
				if (bytes_sent == SOCKET_ERROR)
				{
					std::lock_guard<std::mutex> usersLock{ stdout_mutex };
					std::cerr << "send() failed." << std::endl;
					break;
				}
				if (bytes_sent == 0) //Graceful shutdown.
				{
					break;
				}
			}
			else if (commandID == "/t")
			{
				//Don't consider commands that are nothing.
				//A proper /t should be at least 5 characters (2 for /t, 1 for space, 2 for hexadecimals).
				if (user_input.size() <= 4)
				{
					std::lock_guard<std::mutex> output_mutex{ stdout_mutex };
					std::cout << "Invalid Command Length" << std::endl; // /t should contain hexadecimals.
					continue;
				}
				std::string message{};
				//Start from index 3, to exclude /t and spacing
				user_input = user_input.substr(3);
				std::string hex_converter{};
				/*
					User input is in hexadecimal format
					/t <command 2 characters><others>

					To convert, put 2 hexaecimal characters into stoi to get a byte (uchar).
					Continue until it can't take 2 characters at a time anymore.
				*/
				for (int i = 0; static_cast<size_t>(i) + 1 < user_input.size(); i += 2)
				{
					unsigned char command_value{};
					hex_converter = user_input.substr(i, 2);
					command_value = static_cast<unsigned char>(stoi(hex_converter, 0, 16));
					message.push_back(command_value);
				}
				int bytes_sent = Write_To_Socket(client_socket, message.size(), message.data());
				//==Error checking, -1 returned if send failed, 0 returned if nothing written (client socket has shutdown'd).
				if (bytes_sent == SOCKET_ERROR)
				{
					std::lock_guard<std::mutex> usersLock{ stdout_mutex };
					std::cerr << "send() failed." << std::endl;
					break;
				}
				if (bytes_sent == 0) //Graceful shutdown.
				{
					break;
				}
			}
			else if (commandID == "/e")
			{
				std::string message{};
				//A proper /e command should have
				// 2 for /e, 1 for space, 7 for ip, 1 for :, 1 for port number and 1 space.
				if (user_input.size() <= 13)
				{
					std::lock_guard<std::mutex> output_mutex{ stdout_mutex };
					std::cout << "Invalid Command Length" << std::endl;
					continue;
				}
				//Trim off /e and space.
				std::istringstream input_stream{ user_input.substr(3) };
				/*
					/e command is in the form
					/e ip.ip.ip.ip:port message

					Convert to form
					[2][ip, 4 bytes][port, 2 bytes][text length, 4 bytes][text]
					Where port and text length are converted to network-byte order.
				*/
				int command = 0x2;
				message.push_back(static_cast<char>(command));
				int ip{}; uint16_t port_number{}; char temp{};
				for (int i = 0; i < 4; i++)
				{
					//>> temp to remove '.'
					input_stream >> std::ws >> ip >> temp;
					message.push_back(static_cast<char>(ip));
				}
				input_stream >> port_number >> std::ws;
				port_number = htons(port_number);

				//If the stringstream has failed so far, it indicates IP or port wasn't read correctly, so tell the user.
				if (input_stream.fail())
				{
					std::lock_guard<std::mutex> output_mutex{ stdout_mutex };
					std::cout << "Invalid Command Parameters" << std::endl;
					continue;
				}

				/*
					At this point, [command][IP] has been added to the message buffer.
					To add
					- Port number
					- Text length
					- Text
				*/

				std::string string_to_echo{};
				std::getline(input_stream, string_to_echo);
				uint32_t text_length = static_cast<uint32_t>(string_to_echo.size());
				text_length = htonl(text_length);

				//Converting port number and text length to 6 unsigned characters (bytes) before adding to message.
				unsigned char buffer[6]{};
				memcpy_s(buffer, 2, &port_number, 2);
				memcpy_s(buffer + 2, 4, &text_length, 4);
				message.insert(message.end(), buffer, buffer + 6);
				//Adding of text.
				message.insert(message.end(), string_to_echo.begin(), string_to_echo.end());

				/*
					Send to server socket.
					If it returns error or has closed gracefully, then quit program.
				*/
				int bytes_sent = Write_To_Socket(client_socket, message.size(), message.data());
				//==Error checking, -1 returned if send failed, 0 returned if nothing written (client socket has shutdown'd).
				if (bytes_sent == SOCKET_ERROR)
				{
					std::lock_guard<std::mutex> usersLock{ stdout_mutex };
					std::cerr << "send() failed." << std::endl;
					break;
				}
				if (bytes_sent == 0) //Graceful shutdown.
				{
					break;
				}
			}
			//download file 
			else if (commandID == "/d")
			{
				// Format of message: "/d ip:port filename"
				std::stringstream ss(user_input.c_str());
				std::string directive_dump, network_info, filename;

				// extract the info, directive dump then the network info which is ip:port
				ss >> directive_dump >> network_info;

				// Use getline to get the rest of the input (filename)
				std::getline(ss, filename);

				// Remove any leading whitespaces from the filename
				while (!filename.empty() && std::isspace(filename.front())) {
					filename.erase(0, 1);
				}

				// Find the position of ':' to separate IP and port
				size_t colon_pos = network_info.find(':');
				if (colon_pos == std::string::npos) {
					std::lock_guard<std::mutex> output_mutex{ stdout_mutex };
					std::cout << "Invalid Command: Missing ':' in IP:port" << std::endl;
					continue;
				}

				std::string ip_address_str = network_info.substr(0, colon_pos);
				std::string port_str = network_info.substr(colon_pos + 1);

				// need to conver ip to 4 bytes like the message format
				struct in_addr ip_address;
				if (inet_pton(AF_INET, ip_address_str.c_str(), &ip_address) != 1) {
					std::lock_guard<std::mutex> output_mutex{ stdout_mutex };
					std::cerr << "Invalid IP address format!" << std::endl;
					continue;
				}

				// Convert port to network byte order
				uint16_t port = static_cast<uint16_t>(std::stoi(port_str));
				uint16_t port_network_order = htons(port);

				// when client receive /d we need to send REQ_DOWNLOAD to server
				char buffer[1000] = { 0 }; 
				buffer[0] = 0x2;  // REQ_DOWNLOAD

				// Copy IP address to buffer
				memcpy(buffer + 1, &ip_address, 4);

				// Copy port number to buffer
				memcpy(buffer + 5, &port_network_order, 2);

				// Copy filename length to buffer
				uint32_t filename_length = static_cast<uint32_t>(filename.size());
				uint32_t filename_length_network_order = htonl(filename_length);
				memcpy(buffer + 7, &filename_length_network_order, 4);

				// Copy filename to buffer
				memcpy(buffer + 11, filename.c_str(), filename.size());
				file_path = (std::filesystem::path(file_download_folder) / filename).string();
				// Calculate total message size
				size_t total_size = 11 + filename.size();

				// Write message to the server
				int bytes_sent = Write_To_Socket(client_socket, total_size, buffer);
				if (bytes_sent == SOCKET_ERROR) {
					std::lock_guard<std::mutex> usersLock{ stdout_mutex };
					std::cerr << "send() failed." << std::endl;
					break;
				}
				if (bytes_sent == 0) {
					break;  // Graceful shutdown
				}

				std::lock_guard<std::mutex> output_mutex{ stdout_mutex };
				std::cout << "Download request sent for file: " << filename << std::endl;
			}
			else
			{
				std::lock_guard<std::mutex> output_mutex{ stdout_mutex };
				std::cout << "Invalid Command" << std::endl; //Doesn't match any of the prior commands.
			}
		}

		/*
			Read from server (non-blocking).

			Cases:
			- Failed to read because of blocking (no data to read) --> continue after sleeping
			- Failed to read because graceful shutdown or unrelated error --> break to shutdown connection
			- Data carried over from previous iteration --> Continue even if blocking
			- Incomplete data --> Try to read data. if failed to read 3 times, it means incomplete message sent from server.
		*/

		//==Carry over unused buffer from previous iteration.
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

		/*
			- No message received (-1) --> check if it's because of non-blocking mode, if so then continue checking for message after sleeping.
			- FIN received (0) --> client shutdown initiated, so shutdown this connection and close the socket for this connection.
			- Incomplete message received (num_bytes doesn't match byte_received) --> enable isMessageIncomplete, to continue looping until all bytes received.
		*/
		memset(buffer, 0, 1000);
		const int bytesReceived = recv(
			client_socket,
			buffer,
			999,
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
				std::lock_guard<std::mutex> usersLock{ stdout_mutex };
				std::cerr << "Server Socket Forced Closure." << std::endl;
				break;
			}
			//not caused by failing to read more data for an incomplete message.
			else
			{
				std::lock_guard<std::mutex> usersLock{ stdout_mutex };
				std::cerr << "recv() failed. Gracefully closing." << std::endl;
				break;
			}
		}
		//FIN sent from client, so close the tcp connection gracefully.
		if (bytesReceived == 0)
		{
			break;
		}

		/*
			From this point, a valid message is received.
		*/
		//Bytesreceived can be -1, in the case of failedToRead reaching 3 or overflow from previous message (unused buffer)
		if (bytesReceived > 0) complete_buffer.insert(complete_buffer.end(), buffer, buffer + bytesReceived);

		/*
			3 commands
			- 0x2 --> REQ_ECHO, print out the message received and send back to server with RSP_ECHO.
			- 0x3 --> RSP_ECHO, print out message.
			- 0x5 RSP_LISTUSERS --> print the list of users.
			- 0x30 --> Print echo error.
		*/

		//==Get Command ID
		char commandID = complete_buffer[0];

		bool isInvalidCommand = false;
		//Check if command is invalid.
		switch (commandID)
		{
		case 2: case 3: case 5: case 0x30:
			break;
		default: //Invalid command.
			isInvalidCommand = true;
			break;
		};

		//==If invalid message is sent by server, then continue checking but discard the entire message buffer.
		if (isInvalidCommand)
		{
			std::lock_guard<std::mutex> output_mutex{ stdout_mutex };
			std::cout << "==========RECV START==========" << std::endl;
			if (isDataCarriedOver)
			{
				std::cout << "Received unknown message format or previous message length wasn't accurate" << std::endl;
			}
			else std::cout << "Received unknown message format" << std::endl;
			std::cout << "==========RECV END==========" << std::endl;
			continue;
		}

		//==Print out error message and continue checking.
		if (commandID == 0x30)
		{
			std::lock_guard<std::mutex> output_mutex{ stdout_mutex };
			std::cout << "==========RECV START==========" << std::endl;
			std::cout << "DOWNLOAD ERROR!" << std::endl;
			std::cout << "==========RECV END==========" << std::endl;
			unused_buffer = complete_buffer.substr(1); //Keep the rest of the unused buffer.
			continue;
		}

		/*
			RSP_LISTFILES
			- Print out the message

			In format:

		*/
		if (commandID == 5) // RSP_LISTFILES
		{
			/*
				Check if message is incomplete first.
				Check number of files.
			*/
			// Incomplete message sent to this client, but no more data to be read. Thus, invalid list files command.
			if (failed_to_read >= 3)
			{
				isMessageIncomplete = false; // Wipe out the buffer and start afresh.
				std::lock_guard<std::mutex> output_mutex{ stdout_mutex };
				std::cout << "==========RECV START==========" << std::endl;
				std::cout << "Received Incomplete Message." << std::endl;
				std::cout << "==========RECV END==========" << std::endl;
				continue; // Continue waiting for new message from server.
			}

			isMessageIncomplete = false; // Reset back to default before checking.
			if (complete_buffer.size() < 3) // Minimum message length is 3 (command and number of files).
			{
				isMessageIncomplete = true;
				continue;
			}

			uint16_t number_files{};
			memcpy_s(&number_files, 2, complete_buffer.data() + 1, 2);
			number_files = ntohs(number_files);

			// Check if message is incomplete. There should be 4 bytes for total length and then 4 bytes for each filename length plus the filename itself.
			if (complete_buffer.size() < 7) // 1 (command) + 2 (number of files) + 4 (total length)
			{
				isMessageIncomplete = true;
				continue;
			}

			uint32_t total_length{};
			memcpy_s(&total_length, 4, complete_buffer.data() + 3, 4);
			total_length = ntohl(total_length);

			// Check if the complete buffer has enough data for the total length.
			if (complete_buffer.size() < 7 + total_length)
			{
				isMessageIncomplete = true;
				continue;
			}

			// Keep extra unused buffer.
			if (complete_buffer.size() > 7 + total_length) unused_buffer = complete_buffer.substr(7 + total_length);

			// Past here, message is complete.

			// Trim off commandID, number of files, and total length.
			complete_buffer = complete_buffer.substr(7);

			/*
				Formatted string is now [filename length][filename] for each file.
				Iterate through each file and print out the filename.
			*/
			std::lock_guard<std::mutex> output_mutex{ stdout_mutex };
			std::cout << "==========RECV START==========" << std::endl;
			std::cout << "Command ID: 5 (RSP_LISTFILES)" << std::endl;
			std::cout << "Number of Files: " << number_files << std::endl;
			std::cout << "File List Length: " << total_length << std::endl;
			for (int i = 0; i < static_cast<int>(number_files); i++)
			{
				// Get filename length
				uint32_t filename_length{};
				memcpy_s(&filename_length, 4, complete_buffer.data(), 4);
				filename_length = ntohl(filename_length);

				// Get filename
				std::string filename(complete_buffer.data() + 4, filename_length);

				// Print the filename
				std::cout << "Filename: " << filename << std::endl;

				// Move to the next file in the buffer
				complete_buffer = complete_buffer.substr(4 + filename_length);
			}
			std::cout << "==========RECV END==========" << std::endl;
			continue; // Continue checking for server messages.
		}

		/*
			Management of RSP_ECHO and REQ_ECHO
			- 0x2 --> REQ_ECHO, print out the message received and send back to server with RSP_ECHO.
			- 0x3 --> RSP_ECHO, print out message.
		*/
		if (commandID == 3) //RSPDOWNLOAD = (unsigned char)0x3
		{
			/*
				Format of REC/RSP_ECHO
				[Command][IP, 4 bytes][Port, 2 bytes][Text length, 4 bytes][Text, Text length bytes]

				Minimum size is 11 bytes.
			*/
			/*
				Bytes received (in the buffer) may be incomplete.
				Check for 2 scenarios
				1. Incomplete, < 11 bytes (need to be up to text_length)
				2. Incomplete, < 11 + text_length bytes

				Note that null-terminator for string isn't considered (nor sent by server).
			*/

			//Incomplete message sent to this client, but no more data to be read. Thus, invalid echo command.
			if (failed_to_read >= 3)
			{
				isMessageIncomplete = false; //Wipe out the buffer and start afresh.
				std::lock_guard<std::mutex> output_mutex{ stdout_mutex };
				std::cout << "==========RECV START==========" << std::endl;
				std::cout << "Echo Error, Received Incomplete Message." << std::endl;
				std::cout << "==========RECV END==========" << std::endl;
				continue; //Continue waiting for new message from server.
			}

			isMessageIncomplete = false; //Reset back to default before checking.
			if (complete_buffer.size() < 15)
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
			memcpy_s(&text_length, 4, complete_buffer.data() + 11, 4);
			text_length = ntohl(text_length);

			//Note that text_length doesn't account for null-terminator, so "hello" text is size 5.
			//if (complete_buffer.size() < static_cast<size_t>(15) + text_length) //Incomplete message.
			//{
			//	isMessageIncomplete = true;
			//	continue;
			//}

			//==Past here, message is complete.

			//Keep the rest of the buffer that wasn't used.
			/*if (complete_buffer.size() > static_cast<size_t>(11) + text_length) unused_buffer = complete_buffer.substr(static_cast<size_t>(11) + text_length);*/
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
			//std::cout << "port: " << port_num << std::endl;

			uint32_t session_ID{};
			memcpy_s(&session_ID, 4, complete_buffer.data() + 7, 4);
			session_ID = ntohl(session_ID);

			uint32_t file_length{};
			memcpy_s(&file_length, 4, complete_buffer.data() + 11, 4);
			file_length = ntohl(file_length);


			{
				std::lock_guard<std::mutex> usersLock{ stdout_mutex };
				std::cout << "====RECEIVED DOWNLOAD RESPONSE====" << std::endl;
				std::cout << "Socket: " << ip_string.str() << ":" << port_str << std::endl;
				std::cout << "Session: " << session_ID << " File Length: " << file_length << std::endl;
				std::cout << "==================================" << std::endl;
			}



			// Start reading from the UDP port to receive the file data
			char udp_buffer[1024]{}; // Buffer to store UDP data
			bool isFileComplete = false;
			uint32_t totalBytesReceived = 0;
			int SequenceNumber = 0; // Initialize sequence number
			std::fstream create_or_clear_the_file;
			create_or_clear_the_file.open(file_path, std::fstream::out | std::fstream::trunc | std::ios::binary);
			create_or_clear_the_file.close();
			std::fstream download_file;
			download_file.open(file_path, std::fstream::out | std::ios::binary | std::fstream::app);

			while (!isFileComplete)
			{
				// Read from the UDP socket
				int bytesReceived = recvfrom(file_download_socket, udp_buffer, sizeof(udp_buffer), 0, nullptr, nullptr);
				if (bytesReceived == SOCKET_ERROR)
				{
					int errorCode = WSAGetLastError();
					if (errorCode == WSAEWOULDBLOCK)
					{
						continue;
					}
					else
					{
						std::lock_guard<std::mutex> usersLock{ stdout_mutex };
						std::cerr << "UDP recv() failed. Error code: " << errorCode << std::endl;
						continue;
					}
				}
				else if (bytesReceived == 0)
				{
					// UDP connection closed
					std::lock_guard<std::mutex> usersLock{ stdout_mutex };
					std::cerr << "UDP connection closed." << std::endl;
					break;
				}

				// get checksum, sequence number, and ACK from the packet
				uint16_t receivedChecksum = 0;
				uint32_t receivedSequenceNumber = 0;

				// get checksum 16 bits
				memcpy(&receivedChecksum, udp_buffer, 2);
				receivedChecksum = ntohs(receivedChecksum);

				// get sequence number 32 bits
				memcpy(&receivedSequenceNumber, udp_buffer + 2, 4);
				receivedSequenceNumber = ntohl(receivedSequenceNumber);

				// get data length 32 bits
				uint32_t dataLength = 0;
				memcpy(&dataLength, udp_buffer + 6, 4);
				dataLength = ntohl(dataLength);

				// get file data payload untill null
				void* data_to_validate = udp_buffer + 2; // Pointer to the file data after checksum, sequence number, and ACK

				bool isChecksumValid = false;
				// Validate the checksum
				if (dataLength < 1000) //data length may be corrupted and force the function to go past buffer boundaries. If so, it's automatically false.
				{
					isChecksumValid = ValidateChecksum(static_cast<size_t>(dataLength + 8), data_to_validate, receivedChecksum);
				}

				if (!isChecksumValid)
				{
					std::lock_guard<std::mutex> usersLock{ stdout_mutex };
					std::cerr << "=======Checksum validation failed for UDP packet!=======" << std::endl;
				}
				else if ((int)receivedSequenceNumber <= SequenceNumber)
				{
					// Duplicate packet or out-of-order packet
					/*std::lock_guard<std::mutex> usersLock{ stdout_mutex };
					std::cerr << "Received duplicate or out-of-order packet. Expected: " << SequenceNumber + 1
						<< ", Received: " << receivedSequenceNumber << std::endl;*/
				}
				// Check if the sequence number is as expected
				else if ((int)receivedSequenceNumber > SequenceNumber)
				{
					// Increment sequence number for the next packet
					SequenceNumber = receivedSequenceNumber;
					// Process the file data (e.g., write to a file or store in memory)
					totalBytesReceived += dataLength;

					download_file.write(udp_buffer+10, dataLength);

					// Print download progress
					/*std::lock_guard<std::mutex> usersLock{ stdout_mutex };
					std::cout << "Download progress: " << std::dec <<  totalBytesReceived << "/" << file_length << " bytes ("
						<< static_cast<double>(totalBytesReceived) / file_length * 100 << "%)" << std::endl;*/


				}

				/*
					Send back ACK message
					[Checksum, 2][Session number, 4][ACK, 4]
				*/
				char send_buffer[10]{};
				uint32_t network_session_id = htonl((uint32_t)session_ID);
				uint32_t network_ack = htonl((uint32_t)SequenceNumber); //Send ACK corresponding to last packet successfully received.
				memcpy_s(send_buffer + 2, 4, &network_session_id, 4);
				memcpy_s(send_buffer + 6, 4, &network_ack, 4);
				uint16_t checksum = CalculateChecksum(8, send_buffer + 2);
				uint16_t network_checksum = htons(checksum);
				memcpy_s(send_buffer, 2, &network_checksum, 2);

				int bytes_sent = WriteToDownloadSocket(ip_string.str(), port_str, send_buffer, 10);
				
				

				// Check if the file is complete (e.g., based on file_length received earlier)
				if (totalBytesReceived >= file_length)
				{
					isFileComplete = true;
					std::lock_guard<std::mutex> usersLock{ stdout_mutex };
					std::cout << "File download complete. Total bytes received: " << totalBytesReceived << std::endl;

					download_file.close();
					break;
				}
				//Graceful closure
				if (bytes_sent == 0)
				{
					std::lock_guard<std::mutex> usersLock{ stdout_mutex };
					std::cout << "Server UDP Port closed, stopping file download" << std::endl;
					break;
				}
			}
		}
	}

	std::lock_guard<std::mutex> output_mutex{ stdout_mutex };
	std::cout << "disconnection..." << std::endl;
	int errorCode = shutdown(client_socket, SD_SEND);
	if (SOCKET_ERROR == errorCode)
	{
		std::cerr << "shutdown() failed." << std::endl;
	}
	closesocket(client_socket);
	//Set before thread ends, to tell main thread that this thread has finished execution.
	isThreadDone = true;
}

/*
* /brief
It first establishes a connection with a server, and then sends it a message based on user input.
If the server responds back with a message, it will print its contents accordingly.
*/
int main(int argc, char** argv)
{

	constexpr uint16_t port = 2048;
	std::string host{};
	std::string portString, server_udp_portString{}, client_udp_portString{};

	// Get IP Address
	std::cout << "Server IP Address: ";
	std::getline(std::cin, host);

	std::cout << std::endl;

	// Get Port Number
	std::cout << "Server TCP Port Number: ";
	std::getline(std::cin, portString);
	std::cout << "Server UDP Port Number: ";
	std::getline(std::cin, server_udp_portString);
	std::cout << "Client UDP Port Number: ";
	std::getline(std::cin, client_udp_portString);
	std::cout << "Path                  : ";
	std::getline(std::cin, file_download_folder);
	std::cout << std::endl;
	try
	{
		std::filesystem::create_directory(file_download_folder);
	}
	catch (std::exception&)
	{
		std::cerr << "Unable to create/open directory: " << file_download_folder;
		return -1;
	}
	// -------------------------------------------------------------------------
	// Start up Winsock, asking for version 2.2.
	//
	// WSAStartup()
	// -------------------------------------------------------------------------

	// This object holds the information about the version of Winsock that we
	// are using, which is not necessarily the version that we requested.
	WSADATA wsaData{};
	SecureZeroMemory(&wsaData, sizeof(wsaData));

	// Initialize Winsock. You must call WSACleanup when you are finished.
	// As this function uses a reference counter, for each call to WSAStartup,
	// you must call WSACleanup or suffer memory issues.
	int errorCode = WSAStartup(MAKEWORD(WINSOCK_VERSION, WINSOCK_SUBVERSION), &wsaData);
	if (NO_ERROR != errorCode)
	{
		std::cerr << "WSAStartup() failed." << std::endl;
		return errorCode;
	}
	char hostname[1000]{};
	gethostname(hostname, 1000);
	/*=====
	* Creation of UDP file download socket.


	*/
	/*
		Creation of UDP socket.
	*/ //JOELSTART

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
	errorCode = getaddrinfo(hostname, client_udp_portString.c_str(), &hints_udp, &info_udp);
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

	char clientIPAddr[1000]{};
	struct sockaddr_in* client_address = reinterpret_cast<struct sockaddr_in*> (info_udp->ai_addr);
	inet_ntop(AF_INET, &(client_address->sin_addr), clientIPAddr, INET_ADDRSTRLEN);
	getnameinfo(info_udp->ai_addr, static_cast <socklen_t> (info_udp->ai_addrlen), clientIPAddr, sizeof(clientIPAddr), nullptr, 0, NI_NUMERICHOST);
	std::cout << "Client IP: " << clientIPAddr << std::endl;
	// Enable non-blocking I/O on the download socket.
	u_long enable = 1;
	ioctlsocket(file_download_socket, FIONBIO, &enable);
	//JOELEND

	// -------------------------------------------------------------------------
	// Resolve a server host name into IP addresses (in a singly-linked list).
	//
	// getaddrinfo()
	// -------------------------------------------------------------------------

	// Object hints indicates which protocols to use to fill in the info.
	addrinfo hints{};
	SecureZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;			// IPv4
	hints.ai_socktype = SOCK_STREAM;	// Reliable delivery
	// Could be 0 to autodetect, but reliable delivery over IPv4 is always TCP.
	hints.ai_protocol = IPPROTO_TCP;	// TCP

	addrinfo* info = nullptr;
	errorCode = getaddrinfo(host.c_str(), portString.c_str(), &hints, &info);
	if ((NO_ERROR != errorCode) || (nullptr == info))
	{
		std::cerr << "getaddrinfo() failed." << std::endl;
		WSACleanup();
		return errorCode;
	}

	
	// -------------------------------------------------------------------------
	// Create a socket and attempt to connect to the first resolved address.
	//
	// socket()
	// connect()
	// -------------------------------------------------------------------------

	SOCKET clientSocket = socket(
		info->ai_family,
		info->ai_socktype,
		info->ai_protocol);
	if (INVALID_SOCKET == clientSocket)
	{
		std::cerr << "socket() failed." << std::endl;
		freeaddrinfo(info);
		WSACleanup();
		return RETURN_CODE_2;
	}

	errorCode = connect(
		clientSocket,
		info->ai_addr,
		static_cast<int>(info->ai_addrlen));
	if (SOCKET_ERROR == errorCode)
	{
		std::cerr << "connect() failed." << std::endl;
		freeaddrinfo(info);
		closesocket(clientSocket);
		WSACleanup();
		return RETURN_CODE_3;
	}



	// Enable non-blocking I/O on the client socket.
	u_long enable123 = 1;
	ioctlsocket(clientSocket, FIONBIO, &enable123);

	/*
		Past here, only write to console using std::mutex to lock, to prevent race conditions with the threads created.
	*/

	//For the client to send and receive messages from the server.
	//Returning from this function indicates the client program should shutdown all sockets and exit.
	std::thread server_interaction_thread(ManageServerInteraction, clientSocket);

	/*
		Run while server_interaction_thread is still interacting with server, i.e. tcp connection shouldn't be closed.
		Continue checking for input and adding to queue.

		Note that if isThreadDone is set after std::getline is called, it won't close the loop until a character is entered into the stream, due to getline blocking.
	*/
	while (!isThreadDone)
	{
		std::string user_input{};
		std::getline(std::cin, user_input);
		//If the size of the input is smaller than 2, it's not a valid command (every command starts with /c where c is a character).
		if (user_input.size() < 2)
		{
			std::lock_guard<std::mutex> output_mutex{ stdout_mutex };
			std::cout << "Invalid Command" << std::endl;
			continue;
		}

		//Append to queue
		AddToQueue(user_input);

		//Unlikely that user will write multiple lines at once, so sleeping is more efficient. 
		//If they do, 100ms * number of lines entered is a small delay.
		using namespace std::chrono_literals;
		std::this_thread::sleep_for(100ms);
	}

	//isThreadDone is true, i.e. tcp connection is to be closed.

	if (server_interaction_thread.joinable()) server_interaction_thread.join();



	// -------------------------------------------------------------------------
	// Clean-up after Winsock.
	//
	// WSACleanup()
	// -------------------------------------------------------------------------
	closesocket(file_download_socket);
	file_download_socket = INVALID_SOCKET;
	WSACleanup();
}
