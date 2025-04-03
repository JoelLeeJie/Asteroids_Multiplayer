#include <main.h>
#include "Client.hpp"
#include "Main.h"
//#include "GameState_Asteroids.cpp"

//Player transform(YT)

//[0x1][4 bytes, X position][4 bytes, Y position]
// [4 bytes, Rotation in radian][4 bytes, X velocity][4 bytes, Y velocity]
// [4 bytes, X Acceleration][4 bytes, Y Acceleration]

Player_Session this_player;
std::mutex this_player_lock{};
SOCKET udp_socket;
std::mutex socket_lock{};

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


std::string Write_PlayerTransform(Player player) {

	//uint16_t port_network_order = htons(port);
	//uint32_t network_order = htonl(host_value);

	//rev: ntohl
	//semd: ht
	// Create a string with at least 4 bytes
	uint32_t Xpos, Ypos;
	uint32_t Xvel, Yvel;
	uint32_t Xacc, Yacc;
	uint32_t rot;

	std::memcpy(&Xpos, &player.Position_X, 4);
	std::memcpy(&Ypos, &player.Position_Y, 4);

	std::memcpy(&Xvel, &player.Velocity_X, 4);
	std::memcpy(&Yvel, &player.Velocity_Y, 4);

	std::memcpy(&Xacc, &player.Acceleration_X, 4);
	std::memcpy(&Yacc, &player.Acceleration_Y, 4);

	std::memcpy(&rot, &player.Rotation, 4);
	
	
	Xpos = htonl(Xpos);
	Ypos = htonl(Ypos);
	Xvel = htonl(Xvel);
	Yvel = htonl(Yvel);
	Xacc = htonl(Xacc);
	Yacc = htonl(Yacc);
	rot = htonl(rot);

	std::string result(29, '\0');
	result[0] = static_cast<char>(0x1); //Command ID //remove this out if (change to 28 instead)
	// we are passing this already in the start

	std::memcpy(&result[1], &Xpos, 4);
	std::memcpy(&result[5], &Ypos, 4);
	std::memcpy(&result[9], &Xvel, 4);
	std::memcpy(&result[13], &Yvel, 4);
	std::memcpy(&result[17], &Xacc, 4);
	std::memcpy(&result[21], &Yacc, 4);
	std::memcpy(&result[25], &rot, 4);


	/*std::cout << "before: =============================\n";
	std::cout << "POS: x: " << player.Position_X << " y: " << player.Position_Y << std::endl;
	std::cout << "VEL: x: " << player.Velocity_X << " y: " << player.Velocity_Y << std::endl;
	std::cout << "rotation: " << player.Rotation << std::endl;

	std::memcpy(&Xpos, &result[1], 4);
	std::memcpy(&Ypos, &result[5], 4);
	std::memcpy(&Xvel, &result[9], 4);
	std::memcpy(&Yvel, &result[13], 4);
	std::memcpy(&Xacc, &result[17], 4);
	std::memcpy(&Yacc, &result[21], 4);
	std::memcpy(&rot, &result[25], 4);

	Xpos = ntohl(Xpos);
	Ypos = ntohl(Ypos);
	Xvel = ntohl(Xvel);
	Yvel = ntohl(Yvel);
	Xacc = ntohl(Xacc);
	Yacc = ntohl(Yacc);
	rot = ntohl(rot);

	float posX, posY, velX, velY, accX, accY, rot2;
	std::memcpy(&posX, &Xpos, 4);
	std::memcpy(&posY, &Ypos, 4);
	std::memcpy(&velX, &Xvel, 4);
	std::memcpy(&velY, &Yvel, 4);
	std::memcpy(&accX, &Xacc, 4);
	std::memcpy(&accY, &Yacc, 4);
	std::memcpy(&rot2, &rot, 4);

	std::cout << "after: =============================\n";
	std::cout << "POS: x: " << posX << " y: " << posY << std::endl;
	std::cout << "VEL: x: " << velX << " y: " << velY << std::endl;
	std::cout << "rotation: " << rot2 << std::endl;*/



	return result;

	//return std::string("Hello ");

	// To verify (optional): print each byte in hex
	/*for (unsigned char c : Xpos) {
		printf("%02X ", c);
	}
	std::cout << std::endl;*/


}

int Read_PlayersTransform(std::string buffer, std::map<unsigned int, Player>& player_map, std::vector<unsigned int>& players_to_create) {

	if (buffer.empty()) {

		PrintString("Read_PlayersTransform: buffer is empty!");

		return 0;
	}

	int bytes_read = 0;

	//char ID_Dump = buffer[0];


	uint16_t num_players = 0;
	std::memcpy(&num_players, &buffer[0], 2);
	num_players = ntohs(num_players);

	bytes_read = 2;

	for (int i = 0; i < (int)num_players; i++) {

		Player player;

		int offset = i * 30 + 2; //30 is total bytes, 3 is offset

		uint16_t player_ID = 0;
		std::memcpy(&player_ID, &buffer[offset], 2);
		player_ID = ntohs(player_ID);

		uint32_t Xpos = 0, Ypos = 0;
		uint32_t Xvel = 0, Yvel = 0;
		uint32_t Xacc = 0, Yacc = 0;
		uint32_t rot = 0;

		std::memcpy(&Xpos, &buffer[2 + offset], 4);
		std::memcpy(&Ypos, &buffer[6 + offset], 4);
		std::memcpy(&Xvel, &buffer[10 + offset], 4);
		std::memcpy(&Yvel, &buffer[14 + offset], 4);
		std::memcpy(&Xacc, &buffer[18 + offset], 4);
		std::memcpy(&Yacc, &buffer[22 + offset], 4);
		std::memcpy(&rot, &buffer[26 + offset], 4);

		Xpos = ntohl(Xpos);
		Ypos = ntohl(Ypos);
		Xvel = ntohl(Xvel);
		Yvel = ntohl(Yvel);
		Xacc = ntohl(Xacc);
		Yacc = ntohl(Yacc);
		rot = ntohl(rot);


		std::memcpy(&player.Position_X, &Xpos, 4);
		std::memcpy(&player.Position_Y, &Ypos, 4);
		std::memcpy(&player.Velocity_X, &Xvel, 4);
		std::memcpy(&player.Velocity_Y, &Yvel, 4);
		std::memcpy(&player.Acceleration_X, &Xacc, 4);
		std::memcpy(&player.Acceleration_Y, &Yacc, 4);
		std::memcpy(&player.Rotation, &rot, 4);

		//if player does not exist, means its a new player, so we create a new profile for him

		auto it = player_map.find(static_cast<unsigned int>(player_ID));

		if (it == player_map.end()) {

			player_map[static_cast<unsigned int>(player_ID)] = player;
			players_to_create.push_back(player_ID);

		}
		else {

			//if the player exists, just update the value
			it->second = player;

		}

		bytes_read += 30;


	}
	std::cout << "Read_PlayersTransform | bytes read: " << bytes_read << std::endl;

	return bytes_read;

}

/*
[0x2] [2 bytes, number of bullets] [4bytes, int Object ID] [4 bytes, float X position][4 bytes, float Y position][8 bytes,
vec2 velocity][4 bytes, float rotation][4 bytes, float timestamp][4 bytes, int Object ID 2]...
*/
std::string Write_NewBullet(unsigned int session_ID, std::map<unsigned int, Bullet>& new_bullets) {


	uint16_t num_bullets = new_bullets.size();
	num_bullets = htons(num_bullets);


	std::string result(3 + new_bullets.size() * 28, '\0');

	result[0] = static_cast<char>(0x2); //Command ID //remove this out if (change to 28 instead)
	std::memcpy(&result[1], &num_bullets, 2); //copy the number of bullets

	int j = 0;
	for (auto i = new_bullets.begin(); i != new_bullets.end(); i++) {

		int offset = 3 + j * 28;

		uint32_t bullet_id = 1;
		std::memcpy(&bullet_id, &i->first, 4);


		auto it = i->second;
		uint32_t Xpos = 0, Ypos = 0;
		uint32_t Xvel = 0, Yvel = 0;

		uint32_t rot = 0, time_stamp = 0;

		std::memcpy(&Xpos, &it.Position_X, 4);
		std::memcpy(&Ypos, &it.Position_Y, 4);
		std::memcpy(&Xvel, &it.Velocity_X, 4);
		std::memcpy(&Yvel, &it.Velocity_Y, 4);
		std::memcpy(&time_stamp, &it.Time_Stamp, 4);
		std::memcpy(&rot, &it.Rotation, 4);

		Xpos = htonl(Xpos);
		Ypos = htonl(Ypos);

		Xvel = htonl(Xvel);
		Yvel = htonl(Yvel);

		time_stamp = htonl(time_stamp);
		rot = htonl(rot);
		bullet_id = htonl(bullet_id);

		std::memcpy(&result[offset], &Xpos, 4);
		std::memcpy(&result[offset + 4], &Ypos, 4);
		std::memcpy(&result[offset + 8], &Xvel, 4);
		std::memcpy(&result[offset + 12], &Yvel, 4);
		std::memcpy(&result[offset + 16], &rot, 4);
		std::memcpy(&result[offset + 20], &time_stamp, 4);
		std::memcpy(&result[offset + 24], &bullet_id, 4);

		

		j++;


	}

	//std::cout << "new bullets size: " << new_bullets.size() << std::endl;

	//after creating, remove the bullets to be created. to avoid duplication
	new_bullets.clear();

	return result;
	//return std::string("World\n");
}



int Read_New_Bullets(std::string buffer, std::map<unsigned int, std::map<unsigned int, Bullet>>& bullets_map, std::map<unsigned int, Player> player_map, std::vector<std::pair<unsigned int, unsigned int>>& other_bullets) {

	if (buffer.empty()) {

		PrintString("Read_New_Bullets: buffer is empty!");

		return 0;
	}

	int bytes_read = 0;

	//char ID_Dump = buffer[0];

	uint16_t num_players = 0;
	std::memcpy(&num_players, &buffer[0], 2);
	num_players = ntohs(num_players);

	int offset = 2;

	bytes_read = 2;


	for (int i = 0; i < (int)num_players; i++) {

		Player player;

		uint16_t player_ID = 0;
		std::memcpy(&player_ID, &buffer[offset], 2);
		player_ID = ntohs(player_ID);

		uint16_t num_bullets = 0;
		std::memcpy(&num_bullets, &buffer[offset + 2], 2);
		num_bullets = ntohs(num_bullets);

		offset += 4; //if bullet num =1, offset here should be 7
		bytes_read += 4;

		for (int j = 0; j < (int)num_bullets; j++) {

			uint32_t bullet_id = 0;
			uint32_t Xpos = 0, Ypos = 0;
			uint32_t Xvel = 0, Yvel = 0;
			uint32_t rot = 0, time_stamp = 0;

			std::memcpy(&bullet_id, &buffer[offset], 4);
			std::memcpy(&Xpos, &buffer[offset + 4], 4);
			std::memcpy(&Ypos, &buffer[offset + 8], 4);
			std::memcpy(&Xvel, &buffer[offset + 12], 4);
			std::memcpy(&Yvel, &buffer[offset + 16], 4);
			std::memcpy(&rot, &buffer[offset + 20], 4);
			std::memcpy(&time_stamp, &buffer[offset + 24], 4);

			offset += 28;
			bytes_read += 28;

			Xpos = ntohl(Xpos);
			Ypos = ntohl(Ypos);
			Xvel = ntohl(Xvel);
			Yvel = ntohl(Yvel);
			rot = ntohl(rot);
			time_stamp = ntohl(time_stamp);
			bullet_id = ntohl(bullet_id);

			Bullet new_bullet;

			std::memcpy(&new_bullet.Position_X, &Xpos, 4);
			std::memcpy(&new_bullet.Position_Y, &Ypos, 4);
			std::memcpy(&new_bullet.Velocity_X, &Xvel, 4);
			std::memcpy(&new_bullet.Velocity_Y, &Yvel, 4);
			std::memcpy(&new_bullet.Rotation, &rot, 4);
			std::memcpy(&new_bullet.Time_Stamp, &time_stamp, 4);


			auto it = bullets_map.find(static_cast<unsigned int>(player_ID)); //add it to the player id map

			//if the player exists
			if (it != bullets_map.end()) {

				//if the bullet id is new, then we add to the map, if not we can just ignore
				if (it->second.find(static_cast<unsigned int>(bullet_id)) == it->second.end()) {

					it->second[static_cast<unsigned int>(bullet_id)] = new_bullet;
					other_bullets.push_back(std::pair<unsigned int, unsigned int>(player_ID, bullet_id));
				}

			}
			else {

				//if the player does not exist on the session map: means 2 scenario

				//case 1: the player is new, however, it does have have any exisiting bullets yet, hence,
				//        the bullet map is not created for the player yet.
				// So we create a map with the session ID with the first few bullets he have created

				if (player_map.find(static_cast<unsigned int>(player_ID)) != player_map.end()) {

					std::map<unsigned int, Bullet> new_bullet_map;

					new_bullet_map[static_cast<unsigned int>(bullet_id)] = new_bullet;

					bullets_map[static_cast<unsigned int>(player_ID)] = new_bullet_map;
					other_bullets.push_back(std::pair<unsigned int, unsigned int>(player_ID, bullet_id));
				}
				else {

					//case 2: the player DO NOT EXISTS AT ALL and they are not in the list of available players
					// So we just IGNORE PLS
				}

			}


		}

	}
	std::cout << "Read_New_Bullets | bytes read: " << bytes_read << std::endl;
	return bytes_read;

}


// [0x3][2 bytes, number of collisions][4 bytes, Object ID][4 bytes, Asteroid ID][4 bytes, float timestamp][4 bytes, Object ID 2]... 
// Using a loop to create number of strings per collision 
/*
	Collision if any (Daryl & Asteriod)

	Bullet to asteroid with timestamp

	Player to asteroid with timestamp

	[0x3][2 bytes, number of collisions][4 bytes, Object ID][4 bytes, Asteroid ID][4 bytes, float timestamp][4 bytes, Object ID 2]...

	Object ID = 0 means player’s ship

	Object ID != 0 means bullets

	This means that when added to CollisionEvent

*/
std::string Write_AsteroidCollision(unsigned int session_ID, std::vector<CollisionEvent>& all_collisions)
{
	// Check number of collisions
	uint16_t num_collides = all_collisions.size();
	num_collides = htons(num_collides);

	// [4 bytes, Object ID][4 bytes, Asteroid ID][4 bytes, float timestamp]
	std::string result(3 + num_collides * 12, '\0');

	// Set first byte to command ID
	result[0] = static_cast<char>(0x3);

	if (all_collisions.size() == 0) return "";
	// Copy number of collisions to next 2 byte
	std::memcpy(&result[1], &num_collides, 2);

	// Append the collisions to the back of the message
	for (size_t i = 0; i < all_collisions.size(); i++) {
		CollisionEvent temp = all_collisions[i];

		// Find the offset of current collision event
		int offset_of_bytes = 3 + i * 12;

		// Obtain object ID, Asteroid ID and Time Stamp
		uint32_t obj_ID = temp.object_ID;
		uint32_t asteroid_ID = temp.asteroid_ID;

		uint32_t time_stamp;
		std::memcpy(&time_stamp, &temp.timestamp, 4); // reinterpret float as uint32_t

		// Convert all to network byte order
		obj_ID = htonl(obj_ID);
		asteroid_ID = htonl(asteroid_ID);
		time_stamp = htonl(time_stamp);

		// Write to buffer
		std::memcpy(&result[offset_of_bytes], &obj_ID, 4);
		std::memcpy(&result[offset_of_bytes + 4], &asteroid_ID, 4);
		std::memcpy(&result[offset_of_bytes + 8], &time_stamp, 4);
	}

	// Avoid re-writing the same collision
	all_collisions.clear();

	return result;
}

int Read_AsteroidCreations(const std::string& buffer, std::map<unsigned int, Asteroids>& Asteroid_map, std::vector<std::pair<unsigned int, Asteroids>>& new_asteroids)
{
	// Check empty string
	if (buffer.empty()) {
		return 0;
	}

	//// Check if wrong string was sent to this function
	//if (buffer[0] != 0x6) {
	//	PrintString("Read_AsteroidCreations: Wrong Command ID sent to this function");
	//	return 0;
	//}

	// Getting number of Asteroids
	uint16_t num_asteroids = 0;
	std::memcpy(&num_asteroids, &buffer[0], 2);
	num_asteroids = ntohs(num_asteroids);

	int bytes_read = 2;

	for (int i = 0; i < num_asteroids; i++) {
		int offset = 2 + i * 36;
		Asteroids temp{};
		int asteroid_ID;

		// Copy Asteroid ID
		std::memcpy(&asteroid_ID, &buffer[offset], 4);
		asteroid_ID = ntohl(asteroid_ID);
		asteroid_ID = ntohl(asteroid_ID);

		// Copy the rest of Asteroid information into a temp Asteroid object
		// Position
		std::memcpy(&temp.Position_x, &buffer[offset + 4], 4);
		std::memcpy(&temp.Position_y, &buffer[offset + 8], 4);
		temp.Position_x = ntohf(temp.Position_x);
		temp.Position_y = ntohf(temp.Position_y);

		// Velocity
		std::memcpy(&temp.Velocity_x, &buffer[offset + 12], 4);
		std::memcpy(&temp.Velocity_y, &buffer[offset + 16], 4);
		temp.Velocity_x = ntohf(temp.Velocity_x);
		temp.Velocity_y = ntohf(temp.Velocity_y);

		// Rotation
		std::memcpy(&temp.Rotation, &buffer[offset + 20], 4);
		temp.Rotation = ntohf(temp.Rotation);

		// Scale
		std::memcpy(&temp.Scale_x, &buffer[offset + 24], 4);
		std::memcpy(&temp.Scale_y, &buffer[offset + 28], 4);
		temp.Scale_x = ntohf(temp.Scale_x);
		temp.Scale_y = ntohf(temp.Scale_y);

		// Time Stamp
		std::memcpy(&temp.time_of_creation, &buffer[offset + 32], 4);
		temp.time_of_creation = ntohf(temp.time_of_creation);

		Asteroid_map[asteroid_ID] = temp;
		new_asteroids.push_back({ asteroid_ID, temp });
		bytes_read += 36;
	}
	return bytes_read;
}

int Read_AsteroidDestruction(const std::string& buffer, std::map<unsigned int, std::map<unsigned int, Bullet>>& all_bullets, 
							std::map<unsigned int, Asteroids>& Asteroid_map, std::vector<std::pair<unsigned int, unsigned int>>& bullet_destruction, 
							std::vector<unsigned int>& asteroid_destruction)
{
	// Check empty string
	if (buffer.empty()) {
		return 0;
	}

	//// Check if wrong string was sent to this function
	//if (buffer[0] != 0x7) {
	//	PrintString("Read_AsteroidDestruction: Wrong Command ID sent to this function");
	//	return 0;
	//}

	// Getting number of Collisions
	uint16_t num_col = 0;
	std::memcpy(&num_col, &buffer[0], 2);
	num_col = ntohs(num_col);

	int bytes_read = 2;

	for (int i = 0; i < num_col; i++) {
		int offset = 2 + i * 10;

		// Obtain Player ID
		int Player_ID;
		std::memcpy(&Player_ID, &buffer[offset], 2);
		Player_ID = ntohs(Player_ID);

		int obj_ID;
		std::memcpy(&obj_ID, &buffer[offset + 2], 4);
		obj_ID = ntohl(obj_ID);

		// Asteroid
		int Asteroid_ID;
		std::memcpy(&Asteroid_ID, &buffer[offset + 6], 4);
		Asteroid_ID = ntohl(Asteroid_ID);

		// Asteroid Response ( Deleting from Map )
		Asteroid_map.erase(Asteroid_ID);

		// Player
		if (obj_ID == 0) {
			// Player Response

			// Resetting player
			players[Player_ID].Position_X = 0.f;
			players[Player_ID].Position_Y = 0.f;
			players[Player_ID].Velocity_X = 0.f;
			players[Player_ID].Velocity_Y = 0.f;
			players[Player_ID].Acceleration_X = 0.f;
			players[Player_ID].Acceleration_Y = 0.f;

			// Asteroid Response ( Deleting from Map )
			Asteroid_map.erase(Asteroid_ID);
			asteroid_destruction.push_back(Asteroid_ID);
		}
		// Bullet
		else if (obj_ID > 0) {
			// Bullet response ( Deleting from Map )
			//players[Player_ID].score += amount;
			int bulletID = obj_ID;
			all_bullets[Player_ID].erase(bulletID);

			// Asteroid Response ( Deleting from Map )
			Asteroid_map.erase(Asteroid_ID);
			bullet_destruction.push_back({ Player_ID, bulletID });
			asteroid_destruction.push_back(Asteroid_ID);
		}

		bytes_read += 10;
	}

	return bytes_read;
}

/*

[0x5] [2 bytes, number of players] [2 bytes, playerID] [2 bytes, number of bullets] [4bytes Object ID][8bytes vec2 pos][8 bytes vec2 velocity][4 bytes float rotation]
[4bytes, float timestamp][4bytes Object ID2]...[2 bytes, playerID 2]...

*/
namespace
{
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
}

int InitializeUDP()
{
	std::string temp{};
	std::string server_ip{};
	std::string server_udp_portString{}, client_udp_portString{};
	std::ifstream config_file{ "Config.txt" };
	if (!config_file.is_open())
	{
		PrintString("Config.txt not found: Put with executable.");
		throw std::exception("Config.txt not found: Put with executable.");
	}
	// Get Server IP Address
	config_file >> temp >> server_ip >> std::ws;
	//Get Server Port number
	config_file >> temp >> server_udp_portString >> std::ws;
	//Get Client Port number
	config_file >> temp >> client_udp_portString >> std::ws;

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
	int errorCode = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (NO_ERROR != errorCode)
	{
		std::cerr << "WSAStartup() failed." << std::endl;
		return errorCode;
	}
	char hostname[1000]{};
	gethostname(hostname, 1000);
	/*=====
	* Creation of UDP socket.


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
	if (bind(udp_socket, info_udp->ai_addr, static_cast<int>(info_udp->ai_addrlen)) != NO_ERROR) {
		std::cerr << "Bind failed" << std::endl;
		closesocket(udp_socket);
		udp_socket = INVALID_SOCKET;
		WSACleanup();
		return errorCode;
	}

	// Enable non-blocking I/O on the udp socket.
	u_long enable = 1;
	ioctlsocket(udp_socket, FIONBIO, &enable);


	//==Convert server ip and port to a sockaddr, for sending.
	sockaddr_storage server_addr{};
	int result = resolvehelper(server_ip.data(), AF_INET, server_udp_portString.data(), &server_addr);
	if (result != 0)
	{
		std::cerr << "Unable to convert server ip/port to valid sockaddr" << std::endl;
		return -1;
	}
	this_player.addrDest = server_addr;
	return 10; //No errors.
}

void FreeUDP()
{
	closesocket(udp_socket); //Shutdown not necessary.
	udp_socket = INVALID_SOCKET;
	WSACleanup();
}


namespace {
	//Just used to shift writing of socket to outside the map, to prevent locking of 2 mutexes (which may lead to deadlock if not done well).
	struct WriteData
	{
		sockaddr_storage addrDest;
		std::string data;
	};
}

/*
	\brief
	Helper function to handle received packets.
*/
void HandleReceivedPackets(std::string data, int seq_or_ack_number)
{
	/*
		Data only (without sequence number/ack/checksum), and are uncorrupted (checksum check has passed).
		Sequence number/Ack number is located under Packet, as a separate variable.
		Size of packets may start from 0, don't assume there is data inside.
	*/

	/*
		Types of packets
		- ACK: [General Command ID (ACK) unsigned char]
		- Non ACK: Reply with ACK [Checksum][ACK number][General Command ID (ACK) unsigned char][Player ID]
	*/

	//Discard packets if empty, since no command ID.
	if (data.empty()) return;
	unsigned char command_ID = data[0];


	if (command_ID == ACK)
	{
		PrintString(std::string("ACK RECV, Seq Num: ") + std::to_string(seq_or_ack_number));
		/*
			Using ACK number, decide what to do with ACK.
			If ACK == current sequence number, packet has been received successfully
			- Increment sequence number, get rid of the current packet to send the next packet.
			- Reset timeout to infinity.
			- If there's buffer left, set isSend to true.
		*/
		std::lock_guard<std::mutex> player_lock{ this_player_lock };
		//==ACK number is less than packet sent out, so ignore.
		if (seq_or_ack_number < this_player.reliable_transfer.current_sequence_number) return;
		//==ACK matches packet sent out, meaning packet received successfully.
		this_player.reliable_transfer.current_sequence_number++;
		//Clear all data that has been sent successfully.
		if (!this_player.messages_to_send.empty()) this_player.messages_to_send.pop();

		//Reset timers
		this_player.reliable_transfer.time_last_packet_sent = 20000000000000; //Reset to some time in the future to effectively set timeout to infinity.
		//Send the remaining data in the buffer, if any.
		if (!this_player.messages_to_send.empty()) this_player.reliable_transfer.toSend = true;
		return; //Handling of packet finished.
	}
	//==Below here, it is a non-ACK packet (i.e. command).




	/*
		Two scenarios
		1. JOIN_RESPONSE
		- Treat it like an ACK.
		- If player id has not been set, then increment and treat it like ACK successful. Also set player ID.
		2. Existing player --> [General Command ID][Player ID][Length of message][Command ID]...
		- Send back ACK.
		- Add to recv buffer if necessary.
		In both cases, increment ack_last_packet_received if it's higher.
	*/
	if (command_ID == JOIN_RESPONSE)
	{
		std::lock_guard<std::mutex> player_lock{ this_player_lock };
		//Ignore JOIN_RESPONSE if already have player ID or invalid format.
		if (this_player.player_ID != -1 || data.size() < 3) return;

		//Set new player ID.
		uint16_t id{};
		memcpy_s(&id, 2, data.data() + 1, 2);
		this_player.player_ID = ntohs(id);
		PrintString("JOIN_RESPONSE RECV, Seq Num: " + std::to_string(seq_or_ack_number) + " Player ID: " + std::to_string(this_player.player_ID));

		//Since "ACK" for the recently sent "JOIN_REQUEST" message is successful, then respond accordingly.
		this_player.reliable_transfer.current_sequence_number++;
		//Clear all data that has been sent successfully.
		if (!this_player.messages_to_send.empty()) this_player.messages_to_send.pop();

		//Reset timers
		this_player.reliable_transfer.time_last_packet_sent = 20000000000000; //Reset to some time in the future to effectively set timeout to infinity.
		//Send the remaining data in the buffer, if any.
		if (!this_player.messages_to_send.empty()) this_player.reliable_transfer.toSend = true;
		return;
	}


	/*
		COMMAND_INCOMPLETE or COMMAND_COMPLETE.

		Just add the server message (whatever it is) to the buffer.
		Set messageIncomplete to false or true depending on the general command.
	*/

	/*
		*Send back an ACK for the packet received. Don't need to for JOIN_RESPONSE, as JOIN_RESPONSE is just an ACK.
		*Format: [Checksum, 2][ACK, 4][ACK command ID, 1][player id, 2]
	*/
	int network_player_id{}; sockaddr_storage senderAddr{};
	{
		std::lock_guard<std::mutex> player_lock{ this_player_lock };
		network_player_id = htons((uint16_t)this_player.player_ID);
		senderAddr = this_player.addrDest;
	}
	char ack_buffer[9]{};
	//Add in ACK number and command ID.
	uint32_t network_response_ACK = htonl(seq_or_ack_number);
	memcpy_s(ack_buffer + 2, 4, &network_response_ACK, 4);
	ack_buffer[6] = ACK;
	memcpy_s(ack_buffer + 7, 2, &network_player_id, 2);
	//Calculate and add in checksum.
	uint16_t network_checksum = htons(CalculateChecksum(7, ack_buffer + 2));
	memcpy_s(ack_buffer, 2, &network_checksum, 2);

	//Send back ACK response to sender.
	{
		std::lock_guard<std::mutex> socket_locker{ socket_lock };
		WriteToSocket(udp_socket, senderAddr, ack_buffer, 9);
	}

	/*
		In both cases, add to the recv buffer. Set message complete to be true or false depending.
	*/
	if (command_ID == COMMAND_COMPLETE || command_ID == COMMAND_INCOMPLETE)
	{
		//Message format: [General Command = COMMAND][Command ID]...[Command ID 2]
		std::lock_guard<std::mutex> player_lock{ this_player_lock };

		//==Check if it's a new message, by comparing packet number with the last successful packet.
		if (this_player.reliable_transfer.ack_last_packet_received >= seq_or_ack_number) return;
		//it's a new packet, so update the stored ack number.
		this_player.reliable_transfer.ack_last_packet_received = seq_or_ack_number;


		/*
			Add to the player's recv buffer after removing [General Command ID]
			This is because general command ID is not necessary.
			Doing this also helps to chain incomplete packets together.
		*/
		this_player.recv_buffer.insert(this_player.recv_buffer.end(), data.begin() + 1, data.end());
		if (command_ID == COMMAND_COMPLETE) this_player.is_recv_message_complete = true;
		else this_player.is_recv_message_complete = false; //Still need to wait for more packets.

		PrintString("MESSAGE RECV, Seq Num: " + std::to_string(seq_or_ack_number) + " Data: " + data);
	}

}
/*
		It should be called in a separate thread.
		Will continually read messages from the udp socket, handling them.
		Will write messages to socket as needed (for messages that require ACK) based on toSend and timeout.

		Note:
		- Only messages that require an ACK should be sent in this method. Otherwise, just write as per normal (note need to use mutex lock)
		- To Send: Set the send buffer to the message (excluding checksum and sequence number). Set toSend to be true.
		- Packet data in the queue has their checksum and sequence number stripped away. They are all confirmed to be uncorrupted,
		and the number is a separate variable from the data.
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
		for (int i = 0; i < 1; i++) //Loop of 1 iteration is just for early returns like "continue" or "break".
		{
			std::lock_guard<std::mutex> player_lock{ this_player_lock };
			Player_Session& session = this_player;
			//Ensure that data being written is not empty.
			if (session.messages_to_send.empty()) break;
			if (session.messages_to_send.front().empty())
			{
				session.messages_to_send.pop();
				break;
			}
			if (GetTime() - session.reliable_transfer.time_last_packet_sent > TIMEOUT_TIMER)
			{
				session.reliable_transfer.toSend = true;
			}
			if (!session.reliable_transfer.toSend) break;
			//Below here, packet is to be sent.
			std::string message_to_send = session.messages_to_send.front();
			//Don't pop, unless ACK'd.

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

		//==Handle the uncorrupted packets
		//First, remove the checksum and sequence number from the packet to make it easier to read.
		std::string data_recv(buffer + 6, buffer + bytes_read);
		HandleReceivedPackets(data_recv, number);
	}
}