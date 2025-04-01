
#ifndef CLIENT_HPP
#define CLIENT_HPP




#include <iostream>
#include <string>
#include <map>
#include <vector>




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
extern std::map<unsigned int, std::map<unsigned int, Bullet>> new_bullets; //not used yet
extern std::map<unsigned int, std::map<unsigned int, Bullet>> all_bullets; //not used yet

extern unsigned int bullet_ID; //start from 0
extern unsigned int mySession_ID;
// 
//std::map<unsigned int, Asteroids> Asteroid_map 
//std::vector<CollisionEvent> all_collisions;


std::string Write_PlayerTransform(Player player);
void Read_PlayersTransform(std::string buffer, std::map<unsigned int, Player>& player_map);

std::string Write_NewBullet(unsigned int session_ID,std::map<unsigned int, Bullet>& new_bullets);
void Read_New_Bullets(std::string buffer, std::map<unsigned int, std::map<unsigned int, Bullet>>&, std::map<unsigned int, Player> player_map);

std::string Write_AsteroidCollision(unsigned int session_ID, std::vector<CollisionEvent>& all_collisions);
void Read_AsteroidCreations(const std::string& buffer, std::map<unsigned int, Asteroids>& Asteroid_map);
void Read_AsteroidDestruction(const std::string& buffer, std::map<unsigned int, std::map<unsigned int, Bullet>>&, std::map<unsigned int, Asteroids>& Asteroid_map);



#endif
