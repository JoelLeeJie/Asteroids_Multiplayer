
#ifndef CLIENT_HPP
#define CLIENT_HPP



#include "main.h"
#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <queue>
#include <mutex>
#include "..\Utility.hpp"
/*
	Represents a player session, where communications with the server is controlled through this.
	Note: addrDest needs to be set before this struct can be used for sending/receiving.
*/
struct Player_Session
{
public:
	Player_Session(sockaddr_storage addr)
		: addrDest{ addr }
	{
	}
	Player_Session() = default;

	/*
		Splits a message into COMMAND_COMPLETE and COMMAND_INCOMPLETE messages, based on whether they fit within MAX_PAYLOAD_SIZE.
	*/
	void SendLongMessage(const std::string& message)
	{
		if (message.empty()) return;
		std::string long_message = message;
		char player_identification[2]{};
		uint16_t network_id = htons((uint16_t)player_ID);
		memcpy_s(player_identification, 2, &network_id, 2);
		std::string player_id_string(player_identification, player_identification + 2);
		while (true)
		{
			//The last part of the message, add COMMAND_COMPLETE to indicate it is complete.
			if (long_message.size() <= MAX_PAYLOAD_SIZE - 3)
			{
				long_message = (char)(COMMAND_COMPLETE)+player_id_string +long_message;
				//String length is less than or equal to max payload size, so just push it all as one packet.
				messages_to_send.push(long_message);
				return; //All parts of the message have been sent.
			}
			else
			{
				//Add incomplete, to show that there are more packets on the way.
				long_message = (char)(COMMAND_INCOMPLETE)+player_id_string+long_message;
				//Can only send MAX_PAYLOAD_SIZE for each packet, as 6 extra bytes are left for seq number and checksum.
				messages_to_send.push(long_message.substr(0, MAX_PAYLOAD_SIZE));
				//Move to the next chunk of the message.
				long_message = long_message.substr(MAX_PAYLOAD_SIZE);
			}
		}
	}
	//Used to control reliable data transfer.
	Reliable_Transfer reliable_transfer{};

	//Used to indicate how to send. Need to set when recvfrom is called.
	sockaddr_storage addrDest{};

	/*
		Indicates if the message stored in recv_buffer is complete.
		Set to false at the start of the frame, and if the packets received from the server come with "COMMAND_INCOMPLETE".
		Set to true when the last command packet from the server "COMMAND_COMPLETE" is received.
	*/
	bool is_recv_message_complete = false;
	//Stores messages received from server, cleared at the end of each frame.
	//Only stores "COMMAND" type messages, so it doesn't store ACK or JOIN_RESPONSE messages.
	std::string recv_buffer{};

	/*
		Each string in the vector signifies a packet to send.
		Packets are sent in order (FCFS).
		Only one packet is sent at a time. The next packet can only be sent when the first packet has been ACK'd.
		Don't include checksum or seq number as they will be automatically added.

		Each string contains [GeneralCommandID] as the first byte, indicating the type of packet it is (Either COMMAND_INCOMPLETE or COMMAND_COMPLETE or JOIN_REQUEST).
		ACK aren't sent this way, as they only need to be sent once without needing to be ACK'd.
		Ensure packets confirm to MAX_PAYLOAD_SIZE (not MAX_PACKET_SIZE).
	*/
	std::queue<std::string> messages_to_send{};

	int player_ID{ -1 };
};

struct Player {

	float Position_X;
	float Position_Y;
	float Velocity_X;
	float Velocity_Y;
	float Acceleration_X;
	float Acceleration_Y;
	float Rotation;

}; // if player do not exist, then add as new player, else update values or smth, limit to 4


struct Bullet {

	float Position_X;
	float Position_Y;
	float Velocity_X;
	float Velocity_Y;

	float Rotation;
	float Time_Stamp;

}; //calculate the collision of its bullet first 

struct Asteroids{ 

	float Position_x;
	float Position_y;
	float Velocity_x;
	float Velocity_y;
	float Scale_x;
	float Scale_y;
	float Rotation;

	float time_of_creation;
};//add new asteroid to the map 

// Add Asteroid event into a map so that you only read from the map, not create message every individual frame
struct CollisionEvent {
    uint32_t object_ID;         // 0 is player, anything after is bullet, offset by 1 ***
    uint32_t asteroid_ID;
    float timestamp;
};

extern std::map<unsigned int, Player> players; //not used yet
extern std::vector<unsigned int> new_players;
extern std::map<unsigned int, Bullet> new_bullets; //list of bullets created by player
extern std::map<unsigned int, std::map<unsigned int, Bullet>> all_bullets; //all the bullets
extern std::vector<std::pair<unsigned int, unsigned int>> new_otherbullets; //list of bullets created by other players

extern unsigned int bullet_ID; //start from 0
// 
//std::map<unsigned int, Asteroids> Asteroid_map 
//std::vector<CollisionEvent> all_collisions;


std::string Write_PlayerTransform(Player player);
void Read_PlayersTransform(std::string buffer, std::map<unsigned int, Player>& player_map, std::vector<unsigned int>& players_to_create);

std::string Write_NewBullet(unsigned int session_ID,std::map<unsigned int, Bullet>& new_bullets);
void Read_New_Bullets(std::string buffer, std::map<unsigned int, std::map<unsigned int, Bullet>>&, std::map<unsigned int, Player> player_map, std::vector<std::pair<unsigned int, unsigned int>>&);

std::string Write_AsteroidCollision(unsigned int session_ID, std::vector<CollisionEvent>& all_collisions);
void Read_AsteroidCreations(const std::string& buffer, std::map<unsigned int, Asteroids>& Asteroid_map);
void Read_AsteroidDestruction(const std::string& buffer, std::map<unsigned int, std::map<unsigned int, Bullet>>&, std::map<unsigned int, Asteroids>& Asteroid_map);

/*
	UDP functions
*/
extern Player_Session this_player;
extern std::mutex this_player_lock;
extern SOCKET udp_socket;
extern std::mutex socket_lock;

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
void ReceiveSendMessages();
int InitializeUDP();
void FreeUDP();
#endif
