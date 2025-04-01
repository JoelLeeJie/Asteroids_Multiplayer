#include <main.h>
#include "Client.hpp"


//Player transform(YT)

//[0x1][4 bytes, X position][4 bytes, Y position]
// [4 bytes, Rotation in radian][4 bytes, X velocity][4 bytes, Y velocity]
// [4 bytes, X Acceleration][4 bytes, Y Acceleration]


std::string Write_PlayerTransform(Player player) {

    //uint16_t port_network_order = htons(port);
    //uint32_t network_order = htonl(host_value);

    //rev: ntohl
    //semd: ht

    // Create a string with at least 4 bytes
    uint32_t Xpos = 0, Ypos = 0;
    uint32_t Xvel = 0, Yvel = 0;
    uint32_t Xacc = 0, Yacc = 0;
    uint32_t rot  = 0;

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

    return result;

    // To verify (optional): print each byte in hex
    /*for (unsigned char c : Xpos) {
        printf("%02X ", c);
    }
    std::cout << std::endl;*/


}

void Read_PlayersTransform(std::string buffer, std::map<unsigned int, Player>& player_map) {

    if (buffer.empty()) {

        std::cout << "Read_PlayersTransform: buffer is empty!\n";

        return;
    }

    char ID_Dump = buffer[0];

    
    uint16_t num_players = 0;
    std::memcpy(&num_players, &buffer[1], 2);    
    num_players = ntohs(num_players);
    
    for (int i = 0; i < (int)num_players; i++) {

        Player player;

        int offset = i * 30 + 3; //30 is total bytes, 3 is offset

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
        rot  = ntohl(rot);


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
            
        }
        else {

            //if the player exists, just update the value
            it->second = player;

        }


    }

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

        uint32_t bullet_id = 0;
        std::memcpy(&bullet_id, &i->first, 4);


        auto it = i->second;
        uint32_t Xpos = 0, Ypos = 0;
        uint32_t Xvel = 0, Yvel = 0;

        uint32_t rot = 0, time_stamp =0;

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
    //after creating, remove the bullets to be created. to avoid duplication
    new_bullets.clear();

    return result;

}



void Read_New_Bullets(std::string buffer, std::map<unsigned int, std::map<unsigned int, Bullet>>& bullets_map, std::map<unsigned int, Player> player_map) {

    if (buffer.empty()) {

        std::cout << "Read_New_Bullets: buffer is empty!\n";

        return;
    }

    char ID_Dump = buffer[0];

    uint16_t num_players = 0;
    std::memcpy(&num_players, &buffer[1], 2);
    num_players = ntohs(num_players);

    int offset = 3;

   
    for (int i = 0; i < (int)num_players; i++) {

        Player player;

        uint16_t player_ID = 0;
        std::memcpy(&player_ID, &buffer[offset], 2);
        player_ID = ntohs(player_ID);

        uint16_t num_bullets = 0;
        std::memcpy(&num_bullets, &buffer[offset+2], 2);
        num_bullets = ntohs(num_bullets);

        offset += 4; //if bullet num =1, offset here should be 7


        for (int j = 0; j < (int) num_bullets; j++) {

            uint32_t bullet_id = 0;          
            uint32_t Xpos = 0, Ypos = 0;
            uint32_t Xvel = 0, Yvel = 0;
            uint32_t rot = 0, time_stamp = 0;

            std::memcpy(&bullet_id, &buffer[offset], 4);
            std::memcpy(&Xpos, &buffer[offset+4], 4);
            std::memcpy(&Ypos, &buffer[offset+8], 4);
            std::memcpy(&Xvel, &buffer[offset+12], 4);
            std::memcpy(&Yvel, &buffer[offset+16], 4);
            std::memcpy(&rot, &buffer[offset+20], 4);
            std::memcpy(&time_stamp, &buffer[offset+24], 4);

            offset += 28;

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

                }
                else {

                    //case 2: the player DO NOT EXISTS AT ALL and they are not in the list of available players
                    // So we just IGNORE PLS
                }

            }
           

        }

    }

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
std::string Write_AsteroidCollision(unsigned int session_ID, std::vector<CollisionEvent> &all_collisions)
{
    // Check number of collisions
    uint16_t num_collides = all_collisions.size();
    num_collides = htons(num_collides);

    // [4 bytes, Object ID][4 bytes, Asteroid ID][4 bytes, float timestamp]
    std::string result(3 + num_collides * 12, '\0');

    // Set first byte to command ID
    result[0] = static_cast<char>(0x3);
    // Copy number of collisions to next 2 byte
    std::memcpy(&result[1], &num_collides, 2);

    // Append the collisions to the back of the message
    for(size_t i = 0; i < all_collisions.size(); i++){
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

void Read_AsteroidCreations(const std::string &buffer, std::map<unsigned int, Asteroids> &Asteroid_map)
{
    // Check empty string
    if(buffer.empty()){
        return;
    }
    
    // Check if wrong string was sent to this function
    if(buffer[0] != 0x6){
        std::cout << "Read_AsteroidCreations: Wrong Command ID sent to this function" << std::endl;
        return;
    }

    // Getting number of Asteroids
    uint16_t num_asteroids = 0;
    std::memcpy(&num_asteroids, &buffer[1], 2);
    num_asteroids = ntohs(num_asteroids);

    for(int i = 0; i < num_asteroids; i++){
        int offset = 3 + i * 36;
        Asteroids temp;
        int asteroid_ID;

        // Copy Asteroid ID
        std::memcpy(&asteroid_ID, &buffer[offset], 4);
        asteroid_ID = ntohl(asteroid_ID);

        // Copy the rest of Asteroid information into a temp Asteroid object
        // Position
        std::memcpy(&temp.Position_x, &buffer[offset + 4], 4);
        std::memcpy(&temp.Position_y, &buffer[offset + 8], 4);

        // Velocity
        std::memcpy(&temp.Velocity_x, &buffer[offset + 12], 4);
        std::memcpy(&temp.Velocity_y, &buffer[offset + 16], 4);  
        
        // Rotation
        std::memcpy(&temp.Rotation, &buffer[offset + 20], 4);

        // Scale
        std::memcpy(&temp.Scale_x, &buffer[offset + 24], 4);
        std::memcpy(&temp.Scale_y, &buffer[offset + 28], 4);

        // Time Stamp
        std::memcpy(&temp.time_of_creation, &buffer[offset + 32], 4);

        Asteroid_map[asteroid_ID] = temp;
    }
}

void Read_AsteroidDestruction(const std::string &buffer, std::map<unsigned int, std::map<unsigned int, Bullet>>& all_bullets, std::map<unsigned int, Asteroids> &Asteroid_map)
{
    // Check empty string
    if(buffer.empty()){
        return;
    }
    
    // Check if wrong string was sent to this function
    if(buffer[0] != 0x7){
        std::cout << "Read_AsteroidCreations: Wrong Command ID sent to this function" << std::endl;
        return;
    }

    // Getting number of Collisions
    uint16_t num_col = 0;
    std::memcpy(&num_col, &buffer[1], 2);
    num_col = ntohs(num_col);

    for(int i = 0; i < num_col; i++){
        int offset = 3 + i * 10;

        // Obtain Player ID
        int Player_ID;
        std::memcpy(&Player_ID, &buffer[offset], 2);
        Player_ID = ntohs(Player_ID);

        int obj_ID;
        std::memcpy(&obj_ID, &buffer[offset + 2], 4);
        obj_ID = ntohl(obj_ID);

        // Player
        if(obj_ID == 0){
            // Player Response
        }
        // Bullet
        else if (obj_ID > 0){
            // Bullet response ( Deleting from Map )
            //players[Player_ID].score += amount;
            int bulletID = obj_ID;
            all_bullets[Player_ID].erase(bulletID);
        }
        // Asteroid
        int Asteroid_ID;
        std::memcpy(&Asteroid_ID, &buffer[offset + 6], 4);
        Asteroid_ID = ntohl(Asteroid_ID);

        // Asteroid Response ( Deleting from Map )
        Asteroid_map.erase(Asteroid_ID);
    }
}

/*

[0x5] [2 bytes, number of players] [2 bytes, playerID] [2 bytes, number of bullets] [4bytes Object ID][8bytes vec2 pos][8 bytes vec2 velocity][4 bytes float rotation]
[4bytes, float timestamp][4bytes Object ID2]...[2 bytes, playerID 2]...

*/



