/******************************************************************************/
/*!
\file		GameState_Asteroids.cpp
\author 	Digipen, Joel Lee Jie
\par    	email: joeljie.lee\@digipen.edu
\date   	February 08, 2024
\brief		Declares and defines struct and gamestate functions to run the game.
void GameStateAsteroidsLoad(void); Loads objects into the game.
void GameStateAsteroidsInit(void); Initialises objects.
void GameStateAsteroidsUpdate(void); Runs every frame
void GameStateAsteroidsDraw(void); Runs every frame and controls graphics
void GameStateAsteroidsFree(void); Prepare objects for initialisation.
void GameStateAsteroidsUnload(void); Unload memory

Copyright (C) 20xx DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the
prior written consent of DigiPen Institute of Technology is prohibited.
 */
 /******************************************************************************/

#include "main.h"
#include <cstdlib>
#include <map>
#include <vector>
#include <iostream>
#include <queue>
#include <sstream>
#include <limits>

/******************************************************************************/
/*!
	Defines constants used in the game.
*/
/******************************************************************************/
const unsigned int	GAME_OBJ_NUM_MAX = 32;			// The total number of different objects (Shapes)
const unsigned int	GAME_OBJ_INST_NUM_MAX = 2048;			// The total number of different game object instances


const unsigned int	SHIP_INITIAL_NUM = 3;			// initial number of ship lives
const float			SHIP_SCALE_X = 16.0f;		// ship scale x
const float			SHIP_SCALE_Y = 16.0f;		// ship scale y
const float			BULLET_SCALE_X = 20.0f;		// bullet scale x
const float			BULLET_SCALE_Y = 3.0f;			// bullet scale y
const float			ASTEROID_MIN_SCALE_X = 10.0f;		// asteroid minimum scale x
const float			ASTEROID_MAX_SCALE_X = 60.0f;		// asteroid maximum scale x
const float			ASTEROID_MIN_SCALE_Y = 10.0f;		// asteroid minimum scale y
const float			ASTEROID_MAX_SCALE_Y = 60.0f;		// asteroid maximum scale y

const float			WALL_SCALE_X = 64.0f;		// wall scale x
const float			WALL_SCALE_Y = 164.0f;		// wall scale y

const float			SHIP_ACCEL_FORWARD = 100.0f;		// ship forward acceleration (in m/s^2)
const float			SHIP_ACCEL_BACKWARD = 100.0f;		// ship backward acceleration (in m/s^2)
const float			SHIP_ROT_SPEED = (2.0f * PI);	// ship rotation speed (degree/second)

const float			BULLET_SPEED = 400.0f;		// bullet speed (m/s)

const float         BOUNDING_RECT_SIZE = 1.0f;         // this is the normalized bounding rectangle (width and height) sizes - AABB collision data

const float			MAX_FLOAT_VALUE = 3.402823466e+38F;

// Constants
constexpr float epsilon = 0.001f;

// -----------------------------------------------------------------------------
enum TYPE
{
	// list of game object types
	TYPE_SHIP = 0,
	TYPE_BULLET,
	TYPE_ASTEROID,
	TYPE_WALL,

	TYPE_NUM
};

// -----------------------------------------------------------------------------
// object flag definition

const unsigned long FLAG_ACTIVE = 0x00000001;

/******************************************************************************/
/*!
	Struct/Class Definitions
*/
/******************************************************************************/

//Game object structure
struct GameObj
{
	unsigned long		type;		// object type
	AEGfxVertexList* pMesh;		// This will hold the triangles which will form the shape of the object
};

// ---------------------------------------------------------------------------

//Game object instance structure
struct GameObjInst
{
	GameObj* pObject;	// pointer to the 'original' shape
	unsigned long		flag;		// bit flag or-ed together
	AEVec2				scale;		// scaling value of the object instance
	AEVec2				posCurr;	// object current position

	AEVec2				posPrev;	// object previous position -> it's the position calculated in the previous loop

	AEVec2				velCurr;	// object current velocity
	float				dirCurr;	// object current direction. It is the rotation in radians, and not a vector(x,y).
	AABB				boundingBox;// object bouding box that encapsulates the object
	AEMtx33				transform;	// object transformation matrix: Each frame, 
	// calculate the object instance's transformation matrix and save it here

	//since everything will be in instance, we will have to check what the instance belongs to
	// via the type, object id , and which player session it belongs to
	int Player_ID; //see which player it belongs to
	int Object_ID; // for bullet, asterods

};

/******************************************************************************/
/*!
	Static Variables
*/
/******************************************************************************/

// list of original object
static GameObj				sGameObjList[GAME_OBJ_NUM_MAX];				// Each element in this array represents a unique game object (shape)
static unsigned long		sGameObjNum;								// The number of defined game objects

// list of object instances
static GameObjInst			sGameObjInstList[GAME_OBJ_INST_NUM_MAX];	// Each element in this array represents a unique game object instance (sprite)
static unsigned long		sGameObjInstNum;							// The number of used game object instances

// pointer to the ship object
static GameObjInst* spShip;										// Pointer to the "Ship" game object instance

// pointer to the wall object
static GameObjInst* spWall;										// Pointer to the "Wall" game object instance

// number of ship available (lives 0 = game over)
static long					sShipLives;									// The number of lives left

// the score = number of asteroid destroyed
static unsigned long		sScore;										// Current score

static bool runGame;



// asteroid collision data map
static std::map<unsigned short, std::map<unsigned int, float>> asteroidCollisionMap;


// ---------------------------------------------------------------------------

// functions to create/destroy a game object instance
GameObjInst* gameObjInstCreate(unsigned long type, AEVec2* scale,
	AEVec2* pPos, AEVec2* pVel, float dir);

GameObjInst* gameObjInstCreate(int player_id, int object_id, unsigned long type,
	AEVec2* scale, AEVec2* pPos, AEVec2* pVel, float dir);

void DestroyInstanceByID(int objectID, unsigned long type, int player_ID);

void				gameObjInstDestroy(GameObjInst* pInst);

void				Helper_Wall_Collision();



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//NETWORKING GLOBAL VARIABLES/FUNCTIONS


auto program_start = std::chrono::steady_clock::now(); // Starts at 0


std::map<unsigned int, Player> players;
std::map<unsigned int, Bullet> new_bullets;
std::map<unsigned int, std::map<unsigned int, Bullet>> all_bullets;
unsigned int bullet_ID = 1;
std::vector<unsigned int> new_players;
std::vector<std::pair<unsigned int, unsigned int>> new_otherbullets; //list of bullets created by other players
std::map<unsigned int, Asteroids> Asteroid_map;
std::vector<std::pair<unsigned int, Asteroids>> new_asteroids;
std::vector<CollisionEvent> all_collisions;
std::vector<unsigned int> asteroid_destruction;
std::set<unsigned int> player_hit;
std::vector<std::pair<unsigned int, unsigned int>> bullet_destruction;

float get_TimeStamp() {
	auto now = std::chrono::steady_clock::now();
	return (float)std::chrono::duration<double>(now - program_start).count();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////




/******************************************************************************/
/*!
\brief
Adds a new random asteroid within min-max x,y scale as defined by the constants ASTEROID_MAX_SCALE....
Gives it a random pos within window screen size, and set its velocity randomly within (-100 to 100, -100 to 100)
Adds newly created asteroid to gameobjectinstance list.
*/
/******************************************************************************/
void AddNewAsteroid()
{
	//static lambda to only run once.
	static auto once = []() {
		srand((unsigned int)AEGetTime(nullptr));
		};
	AEVec2 pos, vel, scale;
	//Set it so that it doesn't spawn on the player.
	do
	{
		pos = { (float)(rand() % ((int)AEGfxGetWinMaxX() * 2)) + AEGfxGetWinMinX(), (float)(rand() % ((int)AEGfxGetWinMaxY() * 2)) + AEGfxGetWinMinY() };

	} while (pos.x < spShip->posCurr.x + 200 && pos.x > spShip->posCurr.x - 200 || pos.y < spShip->posCurr.y + 200 && pos.y > spShip->posCurr.y - 200);
	vel = { (float)(rand() % 200) - 100.f,(float)(rand() % 200) - 100.f };
	scale = { (float)(rand() % (int)(ASTEROID_MAX_SCALE_X - ASTEROID_MIN_SCALE_X)) + ASTEROID_MIN_SCALE_X,(float)(rand() % (int)(ASTEROID_MAX_SCALE_Y - ASTEROID_MIN_SCALE_Y) + ASTEROID_MIN_SCALE_Y) };
	static int obj_id = 0;
	auto test = gameObjInstCreate(TYPE_ASTEROID, &scale, &pos, &vel, 0.0f);

	Asteroids temp;

	// Position
	temp.Position_x = pos.x;
	temp.Position_y = pos.y;

	// Velocity
	temp.Velocity_x = vel.x;
	temp.Velocity_y = vel.y;

	// Scale
	temp.Scale_x = scale.x;
	temp.Scale_y = scale.y;

	// Rotation
	temp.Rotation = 0.0f;

	// Time of creation
	temp.time_of_creation = get_TimeStamp();

	test->Object_ID = obj_id;
	Asteroid_map[test->Object_ID] = temp;

	sGameObjInstNum++;
	obj_id++;
}
static bool onValueChange = true;


/******************************************************************************/
/*!
	"Load" function of this state
*/
/******************************************************************************/
void GameStateAsteroidsLoad(void)
{
	// zero the game object array
	memset(sGameObjList, 0, sizeof(GameObj) * GAME_OBJ_NUM_MAX);
	// No game objects (shapes) at this point
	sGameObjNum = 0;

	// zero the game object instance array
	memset(sGameObjInstList, 0, sizeof(GameObjInst) * GAME_OBJ_INST_NUM_MAX);
	// No game object instances (sprites) at this point
	sGameObjInstNum = 0;

	// The ship object instance hasn't been created yet, so this "spShip" pointer is initialized to 0
	spShip = nullptr;

	// load/create the mesh data (game objects / Shapes)
	GameObj* pObj;

	// =====================
	// create the ship shape
	// =====================

	//Assign pObj to point to an element in the gameobject array. 
	//sGameObjNum increases everytime a game object is added to point to a new index.
	pObj = sGameObjList + sGameObjNum++;
	pObj->type = TYPE_SHIP; //0. Needs to be done in order of enum, otherwise objectInst function won't work.

	AEGfxMeshStart();
	AEGfxTriAdd(
		-0.5f, 0.5f, 0xFFFF0000, 0.0f, 0.0f,
		-0.5f, -0.5f, 0xFFFF0000, 0.0f, 0.0f,
		0.5f, 0.0f, 0xFFFFFFFF, 0.0f, 0.0f);

	pObj->pMesh = AEGfxMeshEnd();
	AE_ASSERT_MESG(pObj->pMesh, "fail to create object!!");


	// =======================
	// create the bullet shape
	// =======================

	pObj = sGameObjList + sGameObjNum++;
	pObj->type = TYPE_BULLET; //1

	AEGfxMeshStart();
	AEGfxTriAdd(
		-0.5f, -0.5f, 0xFFFFFF00, 0.0f, 0.0f,
		0.5f, 0.5f, 0xFFFFFF00, 0.0f, 0.0f,
		-0.5f, 0.5f, 0xFFFFFF00, 0.0f, 0.0f);
	AEGfxTriAdd(
		-0.5f, -0.5f, 0xFFFFFF00, 0.0f, 0.0f,
		0.5f, -0.5f, 0xFFFFFF00, 0.0f, 0.0f,
		0.5f, 0.5f, 0xFFFFFF00, 0.0f, 0.0f);

	pObj->pMesh = AEGfxMeshEnd();
	AE_ASSERT_MESG(pObj->pMesh, "fail to create object!!");

	// =========================
	// create the asteroid shape
	// =========================
	pObj = sGameObjList + sGameObjNum++;
	pObj->type = TYPE_ASTEROID; //2 

	AEGfxMeshStart();
	AEGfxTriAdd(
		-0.5f, -0.5f, 0xFFAAAAAA, 0.0f, 0.0f,
		0.5f, 0.5f, 0xFFAAAAAA, 0.0f, 0.0f,
		-0.5f, 0.5f, 0xFFAAAAAA, 0.0f, 0.0f);
	AEGfxTriAdd(
		-0.5f, -0.5f, 0xFFAAAAAA, 0.0f, 0.0f,
		0.5f, -0.5f, 0xFFAAAAAA, 0.0f, 0.0f,
		0.5f, 0.5f, 0xFFAAAAAA, 0.0f, 0.0f);

	pObj->pMesh = AEGfxMeshEnd();
	AE_ASSERT_MESG(pObj->pMesh, "fail to create object!!");

	// =========================
	// create the wall shape
	// =========================

	pObj = sGameObjList + sGameObjNum++;
	pObj->type = TYPE_WALL; //3

	AEGfxMeshStart();
	AEGfxTriAdd(
		-0.5f, -0.5f, 0x6600FF00, 0.0f, 0.0f,
		0.5f, 0.5f, 0x6600FF00, 0.0f, 0.0f,
		-0.5f, 0.5f, 0x6600FF00, 0.0f, 0.0f);
	AEGfxTriAdd(
		-0.5f, -0.5f, 0x6600FF00, 0.0f, 0.0f,
		0.5f, -0.5f, 0x6600FF00, 0.0f, 0.0f,
		0.5f, 0.5f, 0x6600FF00, 0.0f, 0.0f);

	pObj->pMesh = AEGfxMeshEnd();
	AE_ASSERT_MESG(pObj->pMesh, "fail to create object!!");
}

/******************************************************************************/
/*!
	"Initialize" function of this state
*/
/******************************************************************************/
void GameStateAsteroidsInit(void)
{

	///////////////////////////////////////////////
	//PLACE THE INPUT CONNECTION HERE OR PLACED THE CONNECTION IN THE LOAD
	//////////////////////////////////////////////

	//////////////////////////////////////////////
	//WAIT FOR THE SERVER TO SEND WHETHER THE CONNECTION IS GOOD OR SMTH/ (WE NEED SESSION ID)
	// 
	// 
	//              FEEL FREE TO CHANGE, JUST AN IDEA
	// 
	// 
	//IF NOT CLIENT JUST CONNECT IF GOOD, ELSE JUST QUIT IF THE CONNECTION IS LIKE BAD
	// OR PLACE A WHILE LOOP TO PROMPT THE CLIENT UNTIL SUCCESSFUL THEN MOVE ON
	////////////////////////////////////////////////////////////////////////////////


	// create the main ship
	AEVec2 scale;
	AEVec2Set(&scale, SHIP_SCALE_X, SHIP_SCALE_Y);
	//spShip = gameObjInstCreate(TYPE_SHIP, &scale, nullptr, nullptr, 0.0f);
	spShip = gameObjInstCreate(this_player.player_ID, -1, TYPE_SHIP, &scale, nullptr, nullptr, 0.0f);

	AE_ASSERT(spShip);
	sGameObjInstNum++;


	for (int i = 0; i < 4; i++)
	{
		AddNewAsteroid();
	}

	// create the static wall
	AEVec2Set(&scale, WALL_SCALE_X, WALL_SCALE_Y);
	AEVec2 position;
	AEVec2Set(&position, 300.0f, 150.0f);
	spWall = gameObjInstCreate(TYPE_WALL, &scale, &position, nullptr, 0.0f);
	AE_ASSERT(spWall);
	sGameObjInstNum++;

	//Test_ReadThenWriteBulletRoundTrip();

	// reset the score and the number of ships
	sScore = 0;
	sShipLives = SHIP_INITIAL_NUM;
	runGame = true;


	//SEND THE SHIP INFO TO THE SERVER
	///////////////////////////////////////////////////////////////////////////////
	//ONLY THE SHIP NEED TO BE SENT TO THE SERVER DURING INIT
	Player player;

	player.Position_X = 0.f;
	player.Position_Y = 0.f;
	player.Velocity_X = 0.f;
	player.Velocity_Y = 0.f;

	player.Acceleration_X = 0.f;
	player.Acceleration_Y = 0.f;

	player.Rotation = 0.f;

	players[this_player.player_ID] = player; //Add Players to the Map
	std::string buffer = Write_PlayerTransform(player);

	//Write_To_Socket(client_socket, message.size(), message.data());

	/////////////////////////////////////////
	
}

/******************************************************************************/
/*!
	"Update" function of this state
*/
/******************************************************************************/
void GameStateAsteroidsUpdate(void)
{
	static bool isGameStarted = false;
	static bool pressStartOnce = false;

	/*
		Wait for the game to start.
	*/
	if (!isGameStarted)
	{
		std::lock_guard<std::mutex> player_lock{ this_player_lock };
		//Press spacebar to send START_GAME command.
		if (AEInputCheckTriggered(AEVK_SPACE) && runGame == true && !pressStartOnce)
		{
			//Only send the start command once.
			pressStartOnce = true;
			//Send a Start Command to server when space is pressed.
			std::string start_game{ (char)START_GAME };
			this_player.SendLongMessage(start_game);
		}
		
		
		//==Check for a START_GAME command from server.
		//No message received yet.
		if (!this_player.is_recv_message_complete || this_player.recv_buffer.empty())
		{
			//So it doesn't hog the mutex while spinlocking.
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			return;
		}
		//Message received, check if it's the command.
		char command_ID = this_player.recv_buffer[0];
		if (command_ID != START_GAME)
		{
			//There shouldn't be any command that is not start game, so this is just in case.
			this_player.recv_buffer.clear();
			//Since buffer is cleared.
			this_player.is_recv_message_complete = false;
			return;
		}
		//Start game command received, clear buffers to ensure game starts afresh.
		this_player.recv_buffer.clear();
		//Since buffer is cleared.
		this_player.is_recv_message_complete = false;
		isGameStarted = true;
		{
			spShip->Player_ID = this_player.player_ID;
			spShip->Object_ID = 0;
		}
	}

	// =========================================================
	// update according to input
	// =========================================================

	// This input handling moves the ship without any velocity nor acceleration
	// It should be changed when implementing the Asteroids project
	//
	// Updating the velocity and position according to acceleration is 
	// done by using the following:
	// Pos1 = 1/2 * a*t*t + v0*t + Pos0
	//
	// In our case we need to divide the previous equation into two parts in order 
	// to have control over the velocity and that is done by:
	//
	// v1 = a*t + v0		//This is done when the UP or DOWN key is pressed 
	// Pos1 = v1*t + Pos0
	float deltaTime = (float)AEFrameRateControllerGetFrameTime();

	//RECALCULATE TIME STAMP AT THE START OF THE PROGRAM
	program_start = std::chrono::steady_clock::now(); // Starts at 0

	//Acceleration
	AEVec2 addedAccel{};

	if (sShipLives < 0 || sScore >= 5000) runGame = false;

	//For debug purposes only.
#ifdef _DEBUG
	if (AEInputCheckTriggered(AEVK_0) && runGame == true)
	{
		std::lock_guard<std::mutex> player_lock{ this_player_lock };
		std::string firefly_description =
			"Firefly is a young woman with long, silvery-blonde hair with a teal ombre that reaches her waist, very fair skin, and eyes that are a mix of deep ocean blue and sunset pink.\n\n"
			"She wears a brown blazer over a green and white dress with a yellow bow tied in the front. Her sleeves are detached and about wrist length, held with black bracelets — right side with a white flower decoration while the left is plain. She also wears a brown headband with a black bow on the left side of her head that she tore from a flag on the battlefield, along with two green feathers. On her legs she wears thigh-high stockings that fade from teal to a dark brown from top to bottom. The tops of the stockings are lined with gold, and her footwear consists of black heels with a base of white, as well as a pair of green gems in the center along with teal, ruffled collars that wrap around her ankles.\n\n"
			"A member of the Stellaron Hunters, clad in a set of mechanized armor known as \"SAM.\" Her character is marked by unwavering loyalty and steely resolve.\n"
			"Engineered as a weapon against the Swarm, she experiences accelerated growth, but a tragically shortened lifespan.\n"
			"She joined the Stellaron Hunters in a quest for a chance at \"life,\" seeking to defy her fated demise.\n\n"
			"Within the transparent incubation pod, she lay submerged in frigid artificial amniotic fluid, enclosed in a pristine white egg.\n"
			"As the container trembled, she floated, and instinctively reached the cold and soft edges. She presses against the pod's walls tightly, curled up in a corner, as if that would make her body feel warmer.\n\n"
			"She heard something heavy fall and the clamor of metal clashing. Intermittent haste-filled footsteps resounded, and the incubator started to shake...\n"
			"\"Warriors, it is time to awaken...\"\n"
			"\"For Her Majesty...\"\n"
			"A pair of mechanical hands scooped her up as blinding light rent the world asunder. She forgot to weep.\n"
			"\"Feel glory in your birth...\"\n"
			"\"For Her Majesty...\"\n"
			"She opened her eyes, yet failed to find the speaker.\n"
			"She rose up and advanced through heavy curtains, venturing deeper into the palace.\n"
			"\"Accept your honor, and your destiny...\"\n"
			"\"For Her Majesty...\"\n"
			"The cadence of footsteps in unison reverberated through the desolate palace.\n\n"
			"She traversed the unattended vast garden, navigating through colossal insectoid carcasses and numerous incubators... until finally, she arrived at the resplendent council chamber, where a woman with a blurry face was seated upon the throne, her hands hanging wearily.\n\n"
			"\"Don't look up.\"\n"
			"Someone approached her, whispering softly. The person bore an identification tag, AR-26702. What does that signify?\n"
			"She glanced at herself, AR-26710.\n\n"
			"\"Come closer... my child...\"\n"
			"A distant voice emanated from the depths of her mind, casting an inexplicable frenzy upon her consciousness.\n"
			"She obediently approached the Empress and knelt down, kissing her fingertips.\n\n"
			"The Empress's touch felt as icy and unyielding as solid ice, momentarily stirring a flicker of perplexity amidst her frenzy.\n"
			"\"Ignite yourself to the last moment, for the future of Glamoth...\"";
		this_player.SendLongMessage(firefly_description);
	}
#endif

	if (AEInputCheckCurr(AEVK_UP) && runGame == true)
	{
		//Normalized forwards direction.
		AEVec2 added;
		AEVec2Set(&added, cosf(spShip->dirCurr), sinf(spShip->dirCurr));

		//Set new velocity
		//AEVec2 addedAccel{};
		AEVec2Scale(&addedAccel, &added, deltaTime * SHIP_ACCEL_FORWARD);
		AEVec2Add(&spShip->velCurr, &spShip->velCurr, &addedAccel);

		//Limit after adding, otherwise it'll prevent ship from moving after being max speed.
		if (AEVec2Length(&spShip->velCurr) > 0.35 * BULLET_SPEED)
		{
			AEVec2Normalize(&spShip->velCurr, &spShip->velCurr);
			AEVec2Scale(&spShip->velCurr, &spShip->velCurr, 0.35 * BULLET_SPEED);
		}
	}

	if (AEInputCheckCurr(AEVK_DOWN) && runGame == true)
	{
		//Normalized backwards.
		AEVec2 added;
		AEVec2Set(&added, -cosf(spShip->dirCurr), -sinf(spShip->dirCurr));

		//idk what max speed for ship supposed to be.

		//Set new velocity
		//AEVec2 addedAccel{};
		AEVec2Scale(&addedAccel, &added, deltaTime * SHIP_ACCEL_BACKWARD);
		AEVec2Add(&spShip->velCurr, &spShip->velCurr, &addedAccel);

		//limit new velocity.
		if (AEVec2Length(&spShip->velCurr) > 0.35 * BULLET_SPEED)
		{
			AEVec2Normalize(&spShip->velCurr, &spShip->velCurr);
			AEVec2Scale(&spShip->velCurr, &spShip->velCurr, 0.35 * BULLET_SPEED);
		}
	}

	if (AEInputCheckCurr(AEVK_LEFT) && runGame == true)
	{
		spShip->dirCurr += SHIP_ROT_SPEED * (float)(AEFrameRateControllerGetFrameTime());
		spShip->dirCurr = AEWrap(spShip->dirCurr, -PI, PI);
	}

	if (AEInputCheckCurr(AEVK_RIGHT) && runGame == true)
	{
		spShip->dirCurr -= SHIP_ROT_SPEED * (float)(AEFrameRateControllerGetFrameTime());
		spShip->dirCurr = AEWrap(spShip->dirCurr, -PI, PI);
	}


	// Shoot a bullet if space is triggered (Create a new object instance)
	if (AEInputCheckTriggered(AEVK_SPACE) && runGame == true)
	{
		// Get the bullet's direction according to the ship's direction
		// Set the velocity
		// Create an instance, based on BULLET_SCALE_X and BULLET_SCALE_Y
		AEVec2 scale{ BULLET_SCALE_X, BULLET_SCALE_Y };
		AEVec2 vel{ cosf(spShip->dirCurr), sinf(spShip->dirCurr) };
		AEVec2Scale(&vel, &vel, BULLET_SPEED);

		//gameObjInstCreate(TYPE_BULLET, &scale, &spShip->posCurr, &vel, spShip->dirCurr);
		gameObjInstCreate(this_player.player_ID, bullet_ID, TYPE_BULLET, &scale, &spShip->posCurr, &vel, spShip->dirCurr);

		sGameObjInstNum++;

		//ADD THE NEW BULLETS TO THE NEW BULLET MAP
		Bullet bullet;
		bullet.Position_X = spShip->posCurr.x;
		bullet.Position_Y = spShip->posCurr.y;
		bullet.Rotation = spShip->dirCurr;
		bullet.Velocity_X = vel.x;
		bullet.Velocity_Y = vel.y;
		bullet.Time_Stamp = get_TimeStamp();

		new_bullets[bullet_ID] = bullet; //add it to the new bullet map to send to server
		all_bullets[this_player.player_ID][bullet_ID] = bullet; //add it to the the all_bullet map		
		bullet_ID++;

		{//just for printing
			std::lock_guard<std::mutex> player_lock{ this_player_lock };
			//std::cout << "YAY NEW BULLET: bullet size: " << new_bullets.size() << std::endl;
		}

	}


	// ======================================================================
	// Save previous positions
	//  -- For all instances
	// [DO NOT UPDATE THIS PARAGRAPH'S CODE]
	// ======================================================================
	for (unsigned long i = 0; i < GAME_OBJ_INST_NUM_MAX; i++)
	{
		GameObjInst* pInst = sGameObjInstList + i;

		// skip non-active object
		if ((pInst->flag & FLAG_ACTIVE) == 0)
			continue;

		pInst->posPrev.x = pInst->posCurr.x;
		pInst->posPrev.y = pInst->posCurr.y;
	}


	//////////////////////////////////////////////////
	//UPDATE EVERYTHING EXCEPT OTHER PLAYER TRANSFORM
	

	

	// ======================================================================
	// update physics of all active game object instances
	//  -- Calculate the AABB bounding rectangle of the active instance, using the starting position:
	//		boundingRect_min = -(BOUNDING_RECT_SIZE/2.0f) * instance->scale + instance->posPrev
	//		boundingRect_max = +(BOUNDING_RECT_SIZE/2.0f) * instance->scale + instance->posPrev
	//
	//	-- New position of the active instance is updated here with the velocity calculated earlier
	// ======================================================================
	for (int i = 0; i < GAME_OBJ_INST_NUM_MAX; i++)
	{
		GameObjInst& inst = sGameObjInstList[i];
		//Don't check inactive.
		if (!inst.flag) continue;
		//Update position.
		AEVec2 distance{};
		AEVec2Scale(&distance, &inst.velCurr, deltaTime);
		AEVec2Add(&inst.posCurr, &inst.posPrev, &distance);

		//update bounding box.
		inst.boundingBox.min = { inst.posPrev.x - inst.scale.x * BOUNDING_RECT_SIZE / 2.f, inst.posPrev.y - inst.scale.y * BOUNDING_RECT_SIZE / 2.f };
		inst.boundingBox.max = { inst.posPrev.x + inst.scale.x * BOUNDING_RECT_SIZE / 2.f, inst.posPrev.y + inst.scale.y * BOUNDING_RECT_SIZE / 2.f };
		if (inst.pObject->type == TYPE_SHIP)
		{
			AEVec2Scale(&inst.velCurr, &inst.velCurr, 0.995f); //"Friction"
		}
	}

	// ======================================================================
	// check for dynamic-static collisions (one case only: Ship vs Wall)
	// [DO NOT UPDATE THIS PARAGRAPH'S CODE]
	// ======================================================================
	Helper_Wall_Collision();





	// ======================================================================
	// check for dynamic-dynamic collisions
	// ======================================================================
	float tFirst{};
	/*
		Loop the array once for each object in the array(two for loops).
		Only check cases of collision with asteroids.
	*/
	for (int i = 0; i < GAME_OBJ_INST_NUM_MAX; i++)
	{
		GameObjInst& gameObj1 = sGameObjInstList[i];
		if (!gameObj1.flag) continue;
		for (int j = 0; j < GAME_OBJ_INST_NUM_MAX; j++)
		{
			GameObjInst& gameObj2 = sGameObjInstList[j];
			if (!gameObj2.flag) continue;
			if (gameObj1.pObject->type == TYPE_ASTEROID)
			{
				CollisionEvent temp{};

				switch (gameObj2.pObject->type)
				{
				case TYPE_SHIP: //Reduce life, move ship back to center, delete asteroid.
					if (sShipLives < 0 || !runGame) continue; //don't collide with a dead ship or a winning ship.
					//Static collision failed, check dynamic now.
					if (!CollisionIntersection_RectRect(gameObj1.boundingBox, gameObj1.velCurr, gameObj2.boundingBox, gameObj2.velCurr, tFirst))
					{
						//Will not collide within this frame.
						if (tFirst >= deltaTime)
						{
							continue;
						}
					}
					//Has collided or will collide within this frame. 
					//spShip->posCurr = spShip->posPrev = { 0, 0 };
					//spShip->velCurr = { 0, 0 };
					//sShipLives--;
					//gameObjInstDestroy(&gameObj1);
					//AddNewAsteroid();
					//sGameObjInstNum--;
					//sScore += 100;
					//onValueChange = true;

					temp.asteroid_ID = gameObj1.Object_ID;
					temp.object_ID = 0;
					temp.timestamp = get_TimeStamp();

					all_collisions.push_back(temp);
					break;
				case TYPE_BULLET:
					//Static collision failed, check dynamic now.
					if (!CollisionIntersection_RectRect(gameObj1.boundingBox, gameObj1.velCurr, gameObj2.boundingBox, gameObj2.velCurr, tFirst))
					{
						//Will not collide within this frame.
						if (tFirst >= deltaTime)
						{
							continue;
						}
					}
					////Has collided or will collide within this frame. 
					////delete both bullet and asteroid, then add 1-2 new asteroids.
					//gameObjInstDestroy(&gameObj1);
					//gameObjInstDestroy(&gameObj2);
					//sGameObjInstNum -= 2;
					//for (int k = 0; k < rand() % 2 + 1; k++)
					//{
					//	AddNewAsteroid();
					//}
					//sScore += 100;
					//onValueChange = true;

					temp.asteroid_ID = gameObj1.Object_ID;
					temp.object_ID = gameObj2.Object_ID;
					temp.timestamp = get_TimeStamp();

					all_collisions.push_back(temp);

					//std::cout << "Asteroid ID: " << gameObj1.Object_ID << ", Bullet ID: " << gameObj2.Object_ID << std::endl;
					break;

					default: continue; //If asteroid or wall, no need check collision.
				}
			}

		}
	}




	// ===================================================================
	// update active game object instances
	// Example:
	//		-- Wrap specific object instances around the world (Needed for the assignment)
	//		-- Removing the bullets as they go out of bounds (Needed for the assignment)
	//		-- If you have a homing missile for example, compute its new orientation 
	//			(Homing missiles are not required for the Asteroids project)
	//		-- Update a particle effect (Not required for the Asteroids project)
	// ===================================================================
	for (unsigned long i = 0; i < GAME_OBJ_INST_NUM_MAX; i++)
	{
		GameObjInst* pInst = sGameObjInstList + i;

		// skip non-active object
		if ((pInst->flag & FLAG_ACTIVE) == 0)
			continue;

		// check if the object is a ship (make sure the ship is yours only, not other player's)
		if (pInst->pObject->type == TYPE_SHIP && pInst->Player_ID ==this_player.player_ID)
		{
			// Wrap the ship from one end of the screen to the other
			pInst->posCurr.x = AEWrap(pInst->posCurr.x, AEGfxGetWinMinX() - SHIP_SCALE_X,
				AEGfxGetWinMaxX() + SHIP_SCALE_X);
			pInst->posCurr.y = AEWrap(pInst->posCurr.y, AEGfxGetWinMinY() - SHIP_SCALE_Y,
				AEGfxGetWinMaxY() + SHIP_SCALE_Y);
		}

		// Wrap asteroids here
		if (pInst->pObject->type == TYPE_ASTEROID)
		{
			// Wrap the ship from one end of the screen to the other
			pInst->posCurr.x = AEWrap(pInst->posCurr.x, AEGfxGetWinMinX() - pInst->scale.x,
				AEGfxGetWinMaxX() + pInst->scale.x);
			pInst->posCurr.y = AEWrap(pInst->posCurr.y, AEGfxGetWinMinY() - pInst->scale.y,
				AEGfxGetWinMaxY() + pInst->scale.y);
		}

		// Remove bullets that go out of bounds
		if (pInst->pObject->type == TYPE_BULLET)
		{
			if (pInst->posCurr.x < AEGfxGetWinMinX() ||
				pInst->posCurr.x > AEGfxGetWinMaxX() ||
				pInst->posCurr.y < AEGfxGetWinMinY() ||
				pInst->posCurr.y > AEGfxGetWinMaxY()
				)
			{
				gameObjInstDestroy(pInst);
				sGameObjInstNum--;
			}
		}
	}


	//////////////////////////////////////////////////
	//UPDATE THE NEW PLAYER VALUES TO THE PLAYERS MAP
	//Not sure about the position, if we should update now or update later together after receving?
	//read the prev pos, later after receiving, we will update together
	std::map<unsigned int, Player>::iterator it;
	
	{
		std::lock_guard<std::mutex> player_lock{ this_player_lock };
		it = players.find(this_player.player_ID);
	}
	

	if (it != players.end()) {

		it->second.Position_X = spShip->posCurr.x;
		it->second.Position_Y = spShip->posCurr.y;

		it->second.Velocity_X = spShip->velCurr.x;
		it->second.Velocity_Y = spShip->velCurr.y;
		it->second.Acceleration_X = addedAccel.x;
		it->second.Acceleration_Y = addedAccel.y;
		it->second.Rotation = spShip->dirCurr;

	}

	for (unsigned long i = 0; i < GAME_OBJ_INST_NUM_MAX; i++)
	{
		GameObjInst* pInst = sGameObjInstList + i;

		// skip non-active object
		if ((pInst->flag & FLAG_ACTIVE) == 0)
			continue;
		
		if (pInst->pObject->type == TYPE_ASTEROID) {
			auto it = Asteroid_map.find(pInst->Object_ID);

			if (it != Asteroid_map.end()) {

				// Get the asteroid obj inst
				auto astObj = it->second;

				// Position
				astObj.Position_x = pInst->posCurr.x;
				astObj.Position_y = pInst->posCurr.y;

				// Velocity
				astObj.Velocity_x = pInst->velCurr.x;
				astObj.Velocity_y = pInst->velCurr.y;

				// Scale ( Added in case, tho it shouldn't change )
				astObj.Scale_x = pInst->scale.x;
				astObj.Scale_y = pInst->scale.y;

				// Rotation
				astObj.Rotation = pInst->dirCurr;
			}
		}

		else if (pInst->pObject->type == TYPE_BULLET)
		{
			//update all bullet map
			auto it = all_bullets.find(pInst->Player_ID);

			if (it != all_bullets.end()) {

				//if that sepecific bullet exists in the map
				auto iter = it->second.find(pInst->Object_ID);
				if (iter != it->second.end()) {

					iter->second.Position_X = pInst->posCurr.x;
					iter->second.Position_Y = pInst->posCurr.y;

					iter->second.Velocity_X = pInst->velCurr.x;
					iter->second.Velocity_Y = pInst->velCurr.y;
					iter->second.Rotation = pInst->dirCurr;

				}
			}
			//update new bullet map
			auto it2 = new_bullets.find(pInst->Object_ID);
			if (it2 != new_bullets.end()) {

				//if that sepecific bullet exists in the map	
				it2->second.Position_X = pInst->posCurr.x;
				it2->second.Position_Y = pInst->posCurr.y;

				it2->second.Velocity_X = pInst->velCurr.x;
				it2->second.Velocity_Y = pInst->velCurr.y;
				it2->second.Rotation = pInst->dirCurr;

				
			}

		}
		else continue; //we are updating bullets & asteroids only
	}



	/////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////
	// WRITE TO SERVER
	// Pass player transform (YT)
	// Pass new bullet map (YT)
	// 
	// To SERVER
	////////////////////////////////////////////////////////


	//need a function to combine all the strings

	{
		std::lock_guard<std::mutex> player_lock{ this_player_lock };
		std::string message_to_SERVER{};
		message_to_SERVER += Write_PlayerTransform(players[this_player.player_ID]);


		if (new_bullets.size()) {
			message_to_SERVER += Write_NewBullet(this_player.player_ID, new_bullets);
		}

		if (all_collisions.size()) {
			message_to_SERVER += Write_AsteroidCollision(this_player.player_ID, all_collisions);
		}

		//std::cout << message_to_SERVER.c_str();

		this_player.SendLongMessage(message_to_SERVER);
	}

	/////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////
	// READING FROM SERVER
	////////////////////////////////////////////////////////

	std::string buffer{};
	//Spinlock until something is read.
	while (true)
	{
		
		{
			std::lock_guard<std::mutex> player_lock{ this_player_lock };
			if (!this_player.recv_buffer.empty() && this_player.is_recv_message_complete) {
				buffer = this_player.recv_buffer;
				this_player.recv_buffer.clear(); //Clear since it's been read.
				this_player.is_recv_message_complete = false; //Since buffer has been cleared.
				break;
			}
		}
		//Give other threads a chance to grab the mutex.
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		continue;
	}


	if (!buffer.empty()) {

		//we need to split first (ROLL EYE)
		//we need to do bytes checking, function should return the number of bytes read

		int bytes_read = 0;

		while (bytes_read < buffer.size()) {

			uint8_t Command_ID = buffer[bytes_read]; //lets say 0
			bytes_read++;
			// reads 5, read next command

			if (Command_ID == SERVER_PLAYER_TRANSFORM) { //server_player_transform
				if (bytes_read >= buffer.size()) break; //No more things to read.
				std::string result = buffer.substr(bytes_read); // Starts at index 1 and goes to the end
				//if (!result.size()) {
				//	continue;
				//}
				bytes_read += Read_PlayersTransform(result, players, new_players); //add to player map, lets say 5
				//so now bytes read will be 6

				//create new players
				for (unsigned int player : new_players) {

					auto it = players.find(player);


					if (it != players.end()) {

						AEVec2 scale;
						AEVec2 pos{ it->second.Position_X, it->second.Position_Y };
						AEVec2 vel{ it->second.Velocity_X, it->second.Velocity_Y };

						AEVec2Set(&scale, SHIP_SCALE_X, SHIP_SCALE_Y);
						gameObjInstCreate((int)player, -1, TYPE_SHIP, &scale, &pos, &vel, it->second.Rotation);
						sGameObjInstNum++;

					}
				}


			}
			else if (Command_ID == SERVER_BULLET_CREATION) { //server_bullet_transform
				if (bytes_read >= buffer.size()) break; //No more things to read.
				std::string result = buffer.substr(bytes_read); // Starts at index 1 and goes to the end
				// test if msg is empty
				if (!result.size()) {
					continue;
				}
				bytes_read += Read_New_Bullets(result, all_bullets, players, new_otherbullets);

				for (std::pair<unsigned int, unsigned int> one_bullet : new_otherbullets) {

					//check whether the bullet exisits in the all bullet map or not
					auto it = all_bullets.find(one_bullet.first);

					if (it != all_bullets.end()) {

						//if that sepecific bullet exists in the map
						auto iter = it->second.find(one_bullet.second);
						if (iter != it->second.end()) {

							AEVec2 scale{ BULLET_SCALE_X, BULLET_SCALE_Y };
							AEVec2 pos{ iter->second.Position_X, iter->second.Position_Y };
							AEVec2 vel{ iter->second.Velocity_X, iter->second.Velocity_Y };

							//gameObjInstCreate(TYPE_BULLET, &scale, &spShip->posCurr, &vel, spShip->dirCurr);
							gameObjInstCreate(this_player.player_ID, one_bullet.second, TYPE_BULLET, &scale, &pos, &vel, iter->second.Rotation);
							sGameObjInstNum++;

						}
					}

				}

			}
			else if (Command_ID == SERVER_ASTEROID_CREATION) {
				if (bytes_read >= buffer.size()) break; //No more things to read.
				std::string result = buffer.substr(bytes_read);
				bytes_read += Read_AsteroidCreations(result, Asteroid_map, new_asteroids);

				for (std::pair<unsigned int, Asteroids>& Asteroided : new_asteroids) {

					//check whether the Asteroid exisits in the all Asteroid map or not
					auto it = Asteroid_map.find(Asteroided.first);

					if (it != Asteroid_map.end()) {
						// Set variables
						auto temp = it->second;

						AEVec2	sca = { temp.Scale_x, temp.Scale_y },
							pos = { temp.Position_x , temp.Position_y  },
							vel = { temp.Velocity_x, temp.Velocity_y };
						std::cout << "Creating asteroid with position (" << pos.x << ", " << pos.y << ")" << std::endl;
						gameObjInstCreate(this_player.player_ID, Asteroided.first, TYPE_ASTEROID, &sca, &pos, &vel, 0.0f);
						sGameObjInstNum++;
					}
				}
			}
			else if (Command_ID == SERVER_COLLISION) {
				if (bytes_read >= buffer.size()) break; //No more things to read.
				std::string result = buffer.substr(bytes_read);
				if (!result.size()) {
					continue;
				}
				bytes_read += Read_AsteroidDestruction(result, all_bullets, Asteroid_map, bullet_destruction, asteroid_destruction);

				for (unsigned int& Asteroid_ID : asteroid_destruction) {
					DestroyInstanceByID(Asteroid_ID, TYPE_ASTEROID, this_player.player_ID);
					//std::cout << "Asteroid ID: " << Asteroid_ID << std::endl;
					onValueChange = true;
				}

				for (std::pair<unsigned int, unsigned int>& obj_ID : bullet_destruction) {
					DestroyInstanceByID(obj_ID.second, TYPE_BULLET, obj_ID.first);
					//std::cout << "Bullet ID: " << obj_ID.second << std::endl;
					onValueChange = true;
				}
			}
		}

	}
	
	/////////////////////////////////////////////////////////
	// RESPONSES                                           //
	/////////////////////////////////////////////////////////
	// Things to Note from here:
	// Pass player transform (YT)
	// Pass new bullet map (YT)
	// 
	// To SERVER
	////////////////////////////////////////////////////////


	//update other player transform (dont need to move, just assign values since every player only update 
	//                                themselves before send means that we will receive the updated version of them too)
	                         
	//update other player's bullet (player's own bullet is created then updated, before sending, so means that we will receive the updated version too)


	for (unsigned long i = 0; i < GAME_OBJ_INST_NUM_MAX; i++)
	{
		GameObjInst* pInst = sGameObjInstList + i;

		// skip non-active object
		if ((pInst->flag & FLAG_ACTIVE) == 0)
			continue;

		//if (pInst->Player_ID == this_player.player_ID) continue; //we are updating other people NEW stuff here

		

		//update existing other players
		// check if the object is a ship (make sure the ship is others, not yours)
		if (pInst->pObject->type == TYPE_SHIP && pInst!= spShip)
		{
			//double check the player exists or not
			auto it = players.find(pInst->Player_ID);
			if (it != players.end()) {

				AEVec2 pos{ it->second.Position_X, it->second.Position_Y };
				AEVec2 vel{ it->second.Velocity_X, it->second.Velocity_Y };

				pInst->posCurr.x = pos.x;
				pInst->posCurr.y = pos.y;

				pInst->velCurr.x = vel.x;
				pInst->velCurr.y = vel.y;
				pInst->dirCurr = it->second.Rotation;

			}

		}

		/*
			Response on ships that collided with asteroids.
		*/
		if (pInst->pObject->type == TYPE_SHIP)
		{
			for (unsigned int player_collision_id : player_hit)
			{
				//Collided with asteroid, so reset position.
				if (pInst->Player_ID == player_collision_id)
				{
					pInst->posCurr = pInst->posPrev = { 0, 0 };
					pInst->velCurr = { 0, 0 };
					//if my ship collided with an asteroid, decrement my life.
					if (pInst == spShip)
					{
						sShipLives--;
						PrintString("Collided");
					}
					break;
				}
			}
		}
		

		if (pInst->pObject->type == TYPE_BULLET && pInst->Player_ID != this_player.player_ID)
		{
			
			//check whether they exists in the new_bullets. 
			// we try to avoid updating other people exisitng bullet twice

			bool isit_new = false;
			for (std::pair<unsigned int, unsigned int> one_bullet : new_otherbullets) {
				if (pInst->Player_ID == one_bullet.first && pInst->Object_ID == one_bullet.second) {
					isit_new = true;
					break;
				}
			}

			//dont need care if its not new
			if (!isit_new) continue;
			
			//double check the bullet exists
			auto it = all_bullets.find(pInst->Player_ID);

			if (it == all_bullets.end()) {

				//if that sepecific bullet exists in the map
				auto iter = it->second.find(pInst->Object_ID);
				if (iter == it->second.end()) {

					AEVec2 scale{ BULLET_SCALE_X, BULLET_SCALE_Y };
					AEVec2 pos{ iter->second.Position_X, iter->second.Position_Y };
					AEVec2 vel{ iter->second.Velocity_X, iter->second.Velocity_Y };

					pInst->posCurr.x = pos.x;
					pInst->posCurr.y = pos.y;

					pInst->velCurr.x = vel.x;
					pInst->velCurr.y = vel.y;
					pInst->dirCurr = iter->second.Rotation;
					
				}
			}

		}

	}

	////clear the map for new studd since we already create and update the values already
	new_otherbullets.clear();
	new_players.clear();
	new_asteroids.clear();
	player_hit.clear();
	asteroid_destruction.clear();
	bullet_destruction.clear();
	all_collisions.clear();

	// =====================================================================
	// calculate the matrix for all objects
	// =====================================================================
	//update transform for each active gameobj instance.
	for (unsigned long i = 0; i < GAME_OBJ_INST_NUM_MAX; i++)
	{
		GameObjInst* pInst = sGameObjInstList + i;
		AEMtx33		 trans{}, rot{}, scale{};

		UNREFERENCED_PARAMETER(trans);
		UNREFERENCED_PARAMETER(rot);
		UNREFERENCED_PARAMETER(scale);

		// skip non-active object
		if ((pInst->flag & FLAG_ACTIVE) == 0)
			continue;

		AEMtx33Scale(&scale, pInst->scale.x, pInst->scale.y);
		AEMtx33Rot(&rot, pInst->dirCurr);
		AEMtx33Trans(&trans, pInst->posCurr.x, pInst->posCurr.y);

		//scale --> rotation --> translation
		AEMtx33Concat(&rot, &rot, &scale);
		AEMtx33Concat(&pInst->transform, &trans, &rot);
		// Compute the scaling matrix
		// Compute the rotation matrix 
		// Compute the translation matrix
		// Concatenate the 3 matrix in the correct order in the object instance's "transform" matrix
	}
}

/******************************************************************************/
/*!
	"Draw" function of the state. In control of graphics.
*/
/******************************************************************************/
void GameStateAsteroidsDraw(void)
{
	char strBuffer[1024];

	AEGfxSetRenderMode(AE_GFX_RM_COLOR);
	AEGfxTextureSet(NULL, 0, 0);


	// Set blend mode to AE_GFX_BM_BLEND
	// This will allow transparency.
	AEGfxSetBlendMode(AE_GFX_BM_BLEND);
	AEGfxSetTransparency(1.0f);


	// draw all object instances in the list
	for (unsigned long i = 0; i < GAME_OBJ_INST_NUM_MAX; i++)
	{
		GameObjInst& pInst = *(sGameObjInstList + i);

		// skip non-active object
		if ((pInst.flag & FLAG_ACTIVE) == 0)
			continue;
		AEGfxSetTransform(pInst.transform.m);
		AEGfxMeshDraw(pInst.pObject->pMesh, AE_GFX_MDM_TRIANGLES);

		// Set the current object instance's transform matrix using "AEGfxSetTransform"
		// Draw the shape used by the current object instance using "AEGfxMeshDraw"
	}

	//You can replace this condition/variable by your own data.
	//The idea is to display any of these variables/strings whenever a change in their value happens
	if (onValueChange)
	{
		sprintf_s(strBuffer, "Score: %d", sScore);
		//AEGfxPrint(10, 10, (u32)-1, strBuffer);
		printf("%s \n", strBuffer);

		sprintf_s(strBuffer, "Ship Left: %d", sShipLives >= 0 ? sShipLives : 0);
		//AEGfxPrint(600, 10, (u32)-1, strBuffer);
		printf("%s \n", strBuffer);

		// display the game over message
		if (sShipLives < 0)
		{
			//AEGfxPrint(280, 260, 0xFFFFFFFF, "       GAME OVER       ");
			printf("       GAME OVER       \n");
		}

		if (sScore >= 5000)
		{
			printf("       YOU ROCK       \n");
		}

		onValueChange = false;
	}
}

/******************************************************************************/
/*!
	Free function of the state.
*/
/******************************************************************************/
void GameStateAsteroidsFree(void)
{
	// kill all object instances in the array using "gameObjInstDestroy"
	for (int i = 0; i < GAME_OBJ_INST_NUM_MAX; i++)
	{
		gameObjInstDestroy(&sGameObjInstList[i]);
	}
	sGameObjInstNum = 0;
}

/******************************************************************************/
/*!
	"Unload" function of this state.
*/
/******************************************************************************/
void GameStateAsteroidsUnload(void)
{
	// free all mesh data (shapes) of each object using "AEGfxTriFree"
	for (unsigned int i = 0; i < sGameObjNum; i++)
	{
		AEGfxMeshFree(sGameObjList[i].pMesh);
		sGameObjList[i].type = TYPE_NUM; //idk if TYPE_NUM represents NONE, but i'll do it anyways.
	}
	sGameObjNum = 0;
}

/******************************************************************************/
/*!
	Add a gameobject instance to the gameobjectinstance array.
*/
/******************************************************************************/
GameObjInst* gameObjInstCreate(unsigned long type,
	AEVec2* scale,
	AEVec2* pPos,
	AEVec2* pVel,
	float dir)
{
	AEVec2 zero;
	AEVec2Zero(&zero);

	AE_ASSERT_PARM(type < sGameObjNum);

	// loop through the object instance list to find a non-used object instance
	for (unsigned long i = 0; i < GAME_OBJ_INST_NUM_MAX; i++)
	{
		GameObjInst* pInst = sGameObjInstList + i;

		// check if current instance is not used
		if (pInst->flag == 0)
		{
			// it is not used => use it to create the new instance
			pInst->pObject = sGameObjList + type;
			pInst->flag = FLAG_ACTIVE;
			pInst->scale = *scale;
			pInst->posCurr = pPos ? *pPos : zero;
			pInst->velCurr = pVel ? *pVel : zero;
			pInst->dirCurr = dir;

			// return the newly created instance
			return pInst;
		}
	}

	// cannot find empty slot => return 0
	return 0;
}


GameObjInst* gameObjInstCreate(int player_id, int object_id, unsigned long type,
	AEVec2* scale,
	AEVec2* pPos,
	AEVec2* pVel,
	float dir)
{
	AEVec2 zero;
	AEVec2Zero(&zero);

	AE_ASSERT_PARM(type < sGameObjNum);

	// loop through the object instance list to find a non-used object instance
	for (unsigned long i = 0; i < GAME_OBJ_INST_NUM_MAX; i++)
	{
		GameObjInst* pInst = sGameObjInstList + i;

		// check if current instance is not used
		if (pInst->flag == 0)
		{
			// it is not used => use it to create the new instance
			pInst->pObject = sGameObjList + type;
			pInst->flag = FLAG_ACTIVE;
			pInst->scale = *scale;
			pInst->posCurr = pPos ? *pPos : zero;
			pInst->velCurr = pVel ? *pVel : zero;
			pInst->dirCurr = dir;

			pInst->Object_ID = object_id;
			pInst->Player_ID = player_id;

			if (player_id == -1) {
				PrintString("OMG, the player ID is -1 means does not Exist");
				player_id = 28; //randomely assign first
			}

			if (object_id == -1) {
				//means that this is the player
				//player dont have object id, only player_id

				PrintString("A new player is being created!");

			}

			// return the newly created instance
			return pInst;
		}
	}

	// cannot find empty slot => return 0
	return 0;
}

/******************************************************************************/
/*!
	Sets gameObj passed in to be inactive, by setting its flag to be false.
*/
/******************************************************************************/
void gameObjInstDestroy(GameObjInst* pInst)
{
	// if instance is destroyed before, just return
	if (pInst->flag == 0)
		return;

	// zero out the flag
	pInst->flag = 0;
}

void DestroyInstanceByID(int objectID, unsigned long type, int player_ID)
{
	for (unsigned long i = 0; i < GAME_OBJ_INST_NUM_MAX; ++i)
	{
		GameObjInst* pInst = &sGameObjInstList[i];

		if ((pInst->flag & FLAG_ACTIVE) == 0)
			continue;
		if (type == TYPE_ASTEROID) {
			if (pInst->pObject->type == type && pInst->Object_ID == objectID)
			{
				gameObjInstDestroy(pInst);
				sGameObjInstNum--;
				return;
			}
		}
		else if (type == TYPE_BULLET) {
			if (pInst->pObject->type == type && pInst->Object_ID == objectID && pInst->Player_ID == player_ID)
			{
				gameObjInstDestroy(pInst);
				sGameObjInstNum--;
				return;
			}
		}
	}

	//std::cerr << "Warning: Object of type " << type << " with ID " << objectID << " not found.\n";
}


/******************************************************************************/
/*!
	check for collision between Ship and Wall and apply physics response on the Ship
		-- Apply collision response only on the "Ship" as we consider the "Wall" object is always stationary
		-- We'll check collision only when the ship is moving towards the wall!
	[DO NOT UPDATE THIS PARAGRAPH'S CODE]
*/
/******************************************************************************/
void Helper_Wall_Collision()
{
	//calculate the vectors between the previous position of the ship and the boundary of wall
	AEVec2 vec1;
	vec1.x = spShip->posPrev.x - spWall->boundingBox.min.x;
	vec1.y = spShip->posPrev.y - spWall->boundingBox.min.y;
	AEVec2 vec2;
	vec2.x = 0.0f;
	vec2.y = -1.0f;
	AEVec2 vec3;
	vec3.x = spShip->posPrev.x - spWall->boundingBox.max.x;
	vec3.y = spShip->posPrev.y - spWall->boundingBox.max.y;
	AEVec2 vec4;
	vec4.x = 1.0f;
	vec4.y = 0.0f;
	AEVec2 vec5;
	vec5.x = spShip->posPrev.x - spWall->boundingBox.max.x;
	vec5.y = spShip->posPrev.y - spWall->boundingBox.max.y;
	AEVec2 vec6;
	vec6.x = 0.0f;
	vec6.y = 1.0f;
	AEVec2 vec7;
	vec7.x = spShip->posPrev.x - spWall->boundingBox.min.x;
	vec7.y = spShip->posPrev.y - spWall->boundingBox.min.y;
	AEVec2 vec8;
	vec8.x = -1.0f;
	vec8.y = 0.0f;
	if (
		(AEVec2DotProduct(&vec1, &vec2) >= 0.0f) && (AEVec2DotProduct(&spShip->velCurr, &vec2) <= 0.0f) ||
		(AEVec2DotProduct(&vec3, &vec4) >= 0.0f) && (AEVec2DotProduct(&spShip->velCurr, &vec4) <= 0.0f) ||
		(AEVec2DotProduct(&vec5, &vec6) >= 0.0f) && (AEVec2DotProduct(&spShip->velCurr, &vec6) <= 0.0f) ||
		(AEVec2DotProduct(&vec7, &vec8) >= 0.0f) && (AEVec2DotProduct(&spShip->velCurr, &vec8) <= 0.0f)
		)
	{
		float firstTimeOfCollision = 0.0f;
		if (CollisionIntersection_RectRect(spShip->boundingBox,
			spShip->velCurr,
			spWall->boundingBox,
			spWall->velCurr,
			firstTimeOfCollision))
		{
			//re-calculating the new position based on the collision's intersection time
			spShip->posCurr.x = spShip->velCurr.x * (float)firstTimeOfCollision + spShip->posPrev.x;
			spShip->posCurr.y = spShip->velCurr.y * (float)firstTimeOfCollision + spShip->posPrev.y;

			//reset ship velocity
			spShip->velCurr.x = 0.0f;
			spShip->velCurr.y = 0.0f;
		}
	}
}





