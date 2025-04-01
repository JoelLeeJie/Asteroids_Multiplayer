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

/******************************************************************************/
/*!
	Defines constants used in the game.
*/
/******************************************************************************/
const unsigned int	GAME_OBJ_NUM_MAX		= 32;			// The total number of different objects (Shapes)
const unsigned int	GAME_OBJ_INST_NUM_MAX	= 2048;			// The total number of different game object instances


const unsigned int	SHIP_INITIAL_NUM		= 3;			// initial number of ship lives
const float			SHIP_SCALE_X			= 16.0f;		// ship scale x
const float			SHIP_SCALE_Y			= 16.0f;		// ship scale y
const float			BULLET_SCALE_X			= 20.0f;		// bullet scale x
const float			BULLET_SCALE_Y			= 3.0f;			// bullet scale y
const float			ASTEROID_MIN_SCALE_X	= 10.0f;		// asteroid minimum scale x
const float			ASTEROID_MAX_SCALE_X	= 60.0f;		// asteroid maximum scale x
const float			ASTEROID_MIN_SCALE_Y	= 10.0f;		// asteroid minimum scale y
const float			ASTEROID_MAX_SCALE_Y	= 60.0f;		// asteroid maximum scale y

const float			WALL_SCALE_X			= 64.0f;		// wall scale x
const float			WALL_SCALE_Y			= 164.0f;		// wall scale y

const float			SHIP_ACCEL_FORWARD		= 100.0f;		// ship forward acceleration (in m/s^2)
const float			SHIP_ACCEL_BACKWARD		= 100.0f;		// ship backward acceleration (in m/s^2)
const float			SHIP_ROT_SPEED			= (2.0f * PI);	// ship rotation speed (degree/second)

const float			BULLET_SPEED			= 400.0f;		// bullet speed (m/s)

const float         BOUNDING_RECT_SIZE      = 1.0f;         // this is the normalized bounding rectangle (width and height) sizes - AABB collision data

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

const unsigned long FLAG_ACTIVE				= 0x00000001;

/******************************************************************************/
/*!
	Struct/Class Definitions
*/
/******************************************************************************/

//Game object structure
struct GameObj
{
	unsigned long		type;		// object type
	AEGfxVertexList *	pMesh;		// This will hold the triangles which will form the shape of the object
};

// ---------------------------------------------------------------------------

//Game object instance structure
struct GameObjInst
{
	GameObj *			pObject;	// pointer to the 'original' shape
	unsigned long		flag;		// bit flag or-ed together
	AEVec2				scale;		// scaling value of the object instance
	AEVec2				posCurr;	// object current position

	AEVec2				posPrev;	// object previous position -> it's the position calculated in the previous loop

	AEVec2				velCurr;	// object current velocity
	float				dirCurr;	// object current direction. It is the rotation in radians, and not a vector(x,y).
	AABB				boundingBox;// object bouding box that encapsulates the object
	AEMtx33				transform;	// object transformation matrix: Each frame, 
									// calculate the object instance's transformation matrix and save it here

};

// bullet struct
struct Bullet {

	int objectID;
	AEVec2 pos;
	AEVec2 velocity;
	float rotation;
	float timeStamp;
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
static GameObjInst *		spShip;										// Pointer to the "Ship" game object instance

// pointer to the wall object
static GameObjInst *		spWall;										// Pointer to the "Wall" game object instance

// number of ship available (lives 0 = game over)
static long					sShipLives;									// The number of lives left

// the score = number of asteroid destroyed
static unsigned long		sScore;										// Current score

static bool runGame;

// map to store bullet info 
static std::map<unsigned short, std::vector<Bullet>> bulletMap;

static std::queue<GameObjInst*> newAsteroidQueue;

// ---------------------------------------------------------------------------

// functions to create/destroy a game object instance
GameObjInst *		gameObjInstCreate (unsigned long type, AEVec2* scale,
											   AEVec2 * pPos, AEVec2 * pVel, float dir);
void				gameObjInstDestroy(GameObjInst * pInst);

void				Helper_Wall_Collision();

void				ReadBullet(std::istream& input, unsigned short playerID);

void				WriteBullet(std::ostream& output);

void Test_ReadThenWriteBulletRoundTrip()
{
	// ---------------------
	// Setup: manually construct an input stream (mock from UDP)
	// ---------------------
	std::stringstream inputStream;

	unsigned short playerID = 123;
	unsigned short numBullets = 1;
	int objectID = 5;
	float posX = 100.0f, posY = 200.0f;
	float velX = 5.0f, velY = -3.0f;
	float rotation = 0.785f;
	float timestamp = static_cast<float>(AEGetTime(nullptr));

	// Manual binary write (as if it came from UDP message)
	inputStream.write(reinterpret_cast<char*>(&numBullets), sizeof(unsigned short));
	inputStream.write(reinterpret_cast<char*>(&objectID), sizeof(int));
	inputStream.write(reinterpret_cast<char*>(&posX), sizeof(float));
	inputStream.write(reinterpret_cast<char*>(&posY), sizeof(float));
	inputStream.write(reinterpret_cast<char*>(&velX), sizeof(float));
	inputStream.write(reinterpret_cast<char*>(&velY), sizeof(float));
	inputStream.write(reinterpret_cast<char*>(&rotation), sizeof(float));
	inputStream.write(reinterpret_cast<char*>(&timestamp), sizeof(float));

	// ---------------------
	// Step 1: Simulate server reading client message
	// ---------------------
	bulletMap.clear();  // ensure clean state
	ReadBullet(inputStream, playerID);

	assert(bulletMap.count(playerID) == 1);
	assert(bulletMap[playerID].size() == 1);

	GameObjInst* bullet = bulletMap[playerID][0];
	assert(std::abs(bullet->posCurr.x - posX) < epsilon);
	assert(std::abs(bullet->posCurr.y - posY) < epsilon);
	assert(std::abs(bullet->velCurr.x - velX) < epsilon);
	assert(std::abs(bullet->velCurr.y - velY) < epsilon);
	assert(std::abs(bullet->dirCurr - rotation) < epsilon);

	// ---------------------
	// Step 2: Simulate server writing message to clients
	// ---------------------
	std::stringstream outputStream;
	WriteBullet(outputStream);

	std::string written = outputStream.str();
	assert(!written.empty());

	// ---------------------
	// Step 3: Verify round-trip integrity (manual decode)
	// ---------------------
	std::stringstream verify(written);

	unsigned short numPlayersOut;
	verify.read(reinterpret_cast<char*>(&numPlayersOut), sizeof(unsigned short));
	assert(numPlayersOut == 1);

	unsigned short playerIDOut;
	verify.read(reinterpret_cast<char*>(&playerIDOut), sizeof(unsigned short));
	assert(playerIDOut == playerID);

	unsigned short numBulletsOut;
	verify.read(reinterpret_cast<char*>(&numBulletsOut), sizeof(unsigned short));
	assert(numBulletsOut == 1);

	int objectIDOut;
	float posXOut, posYOut, velXOut, velYOut, rotationOut, timestampOut;

	verify.read(reinterpret_cast<char*>(&objectIDOut), sizeof(int));
	verify.read(reinterpret_cast<char*>(&posXOut), sizeof(float));
	verify.read(reinterpret_cast<char*>(&posYOut), sizeof(float));
	verify.read(reinterpret_cast<char*>(&velXOut), sizeof(float));
	verify.read(reinterpret_cast<char*>(&velYOut), sizeof(float));
	verify.read(reinterpret_cast<char*>(&rotationOut), sizeof(float));
	verify.read(reinterpret_cast<char*>(&timestampOut), sizeof(float));

	assert(objectIDOut == objectID);  // If you're using index math to generate objectID

	assert(std::abs(posXOut - posX) < epsilon);
	assert(std::abs(posYOut - posY) < epsilon);
	assert(std::abs(velXOut - velX) < epsilon);
	assert(std::abs(velYOut - velY) < epsilon);
	assert(std::abs(rotationOut - rotation) < epsilon);
	assert(timestampOut >= timestamp);  // AEGetTime() slightly ahead

	printf("Test_ReadThenWriteBulletRoundTrip passed.\n");
}

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
		pos = { (float)(rand() % ((int)AEGfxGetWinMaxX()*2)) + AEGfxGetWinMinX(), (float)(rand() % ((int)AEGfxGetWinMaxY() * 2)) + AEGfxGetWinMinY() };

	} while (pos.x < spShip->posCurr.x + 200 && pos.x > spShip->posCurr.x - 200 || pos.y < spShip->posCurr.y + 200 && pos.y > spShip->posCurr.y - 200);
	vel = { (float)(rand() % 200) - 100.f,(float)(rand() % 200) - 100.f };
	scale = { (float)(rand() % (int)(ASTEROID_MAX_SCALE_X - ASTEROID_MIN_SCALE_X)) + ASTEROID_MIN_SCALE_X,(float)(rand() % (int)(ASTEROID_MAX_SCALE_Y - ASTEROID_MIN_SCALE_Y) + ASTEROID_MIN_SCALE_Y) };
	gameObjInstCreate(TYPE_ASTEROID, &scale, &pos, &vel, 0.0f);
	sGameObjInstNum++;
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
	GameObj * pObj;

	// =====================
	// create the ship shape
	// =====================

	//Assign pObj to point to an element in the gameobject array. 
	//sGameObjNum increases everytime a game object is added to point to a new index.
	pObj		= sGameObjList + sGameObjNum++;
	pObj->type	= TYPE_SHIP; //0. Needs to be done in order of enum, otherwise objectInst function won't work.

	AEGfxMeshStart();
	AEGfxTriAdd(
		-0.5f,  0.5f, 0xFFFF0000, 0.0f, 0.0f, 
		-0.5f, -0.5f, 0xFFFF0000, 0.0f, 0.0f,
		 0.5f,  0.0f, 0xFFFFFFFF, 0.0f, 0.0f );  

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
	// create the main ship
	AEVec2 scale;
	AEVec2Set(&scale, SHIP_SCALE_X, SHIP_SCALE_Y);
	spShip = gameObjInstCreate(TYPE_SHIP, &scale, nullptr, nullptr, 0.0f);
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

	Test_ReadThenWriteBulletRoundTrip();

	// reset the score and the number of ships
	sScore      = 0;
	sShipLives  = SHIP_INITIAL_NUM;
	runGame = true;
}

/******************************************************************************/
/*!
	"Update" function of this state
*/
/******************************************************************************/
void GameStateAsteroidsUpdate(void)
{
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
	if (sShipLives < 0 || sScore >= 5000) runGame = false;
	if (AEInputCheckCurr(AEVK_UP) && runGame == true)
	{
		//Normalized forwards direction.
		AEVec2 added;
		AEVec2Set(&added, cosf(spShip->dirCurr), sinf(spShip->dirCurr)); 

		//Set new velocity
		AEVec2 addedAccel{};
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
		AEVec2 addedAccel{};
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
		spShip->dirCurr += SHIP_ROT_SPEED * (float)(AEFrameRateControllerGetFrameTime ());
		spShip->dirCurr =  AEWrap(spShip->dirCurr, -PI, PI);
	}

	if (AEInputCheckCurr(AEVK_RIGHT) && runGame == true)
	{
		spShip->dirCurr -= SHIP_ROT_SPEED * (float)(AEFrameRateControllerGetFrameTime ());
		spShip->dirCurr =  AEWrap(spShip->dirCurr, -PI, PI);
	}


	// Shoot a bullet if space is triggered (Create a new object instance)
	if (AEInputCheckTriggered(AEVK_SPACE) && runGame == true)
	{
		// Get the bullet's direction according to the ship's direction
		// Set the velocity
		// Create an instance, based on BULLET_SCALE_X and BULLET_SCALE_Y
		AEVec2 scale{BULLET_SCALE_X, BULLET_SCALE_Y};
		AEVec2 vel{ cosf(spShip->dirCurr), sinf(spShip->dirCurr) };
		AEVec2Scale(&vel, &vel, BULLET_SPEED);

		gameObjInstCreate(TYPE_BULLET, &scale, &spShip->posCurr, &vel, spShip->dirCurr);
		sGameObjInstNum++;
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
		inst.boundingBox.min = {inst.posPrev.x - inst.scale.x * BOUNDING_RECT_SIZE/2.f, inst.posPrev.y - inst.scale.y * BOUNDING_RECT_SIZE / 2.f};
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
					spShip->posCurr = spShip->posPrev = { 0, 0 };
					spShip->velCurr = { 0, 0 };
					sShipLives--;
					gameObjInstDestroy(&gameObj1);
					AddNewAsteroid();
					sGameObjInstNum--;
					sScore += 100;
					onValueChange = true;
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
					//Has collided or will collide within this frame. 
					//delete both bullet and asteroid, then add 1-2 new asteroids.
					gameObjInstDestroy(&gameObj1);
					gameObjInstDestroy(&gameObj2);
					sGameObjInstNum-=2;
					for (int k = 0; k < rand() % 2 + 1; k++)
					{
						AddNewAsteroid();
					}
					sScore += 100;
					onValueChange = true;
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
		GameObjInst * pInst = sGameObjInstList + i;

		// skip non-active object
		if ((pInst->flag & FLAG_ACTIVE) == 0)
			continue;
		
		// check if the object is a ship
		if (pInst->pObject->type == TYPE_SHIP)
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



	

	// =====================================================================
	// calculate the matrix for all objects
	// =====================================================================
	//update transform for each active gameobj instance.
	for (unsigned long i = 0; i < GAME_OBJ_INST_NUM_MAX; i++)
	{
		GameObjInst * pInst = sGameObjInstList + i;
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
		GameObjInst  &pInst = *(sGameObjInstList + i);

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
	if(onValueChange)
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
GameObjInst * gameObjInstCreate(unsigned long type, 
							   AEVec2 * scale,
							   AEVec2 * pPos, 
							   AEVec2 * pVel, 
							   float dir)
{
	AEVec2 zero;
	AEVec2Zero(&zero);

	AE_ASSERT_PARM(type < sGameObjNum);
	
	// loop through the object instance list to find a non-used object instance
	for (unsigned long i = 0; i < GAME_OBJ_INST_NUM_MAX; i++)
	{
		GameObjInst * pInst = sGameObjInstList + i;

		// check if current instance is not used
		if (pInst->flag == 0)
		{
			// it is not used => use it to create the new instance
			pInst->pObject	= sGameObjList + type;
			pInst->flag		= FLAG_ACTIVE;
			pInst->scale	= *scale;
			pInst->posCurr	= pPos ? *pPos : zero;
			pInst->velCurr	= pVel ? *pVel : zero;
			pInst->dirCurr	= dir;
			
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
void gameObjInstDestroy(GameObjInst * pInst)
{
	// if instance is destroyed before, just return
	if (pInst->flag == 0)
		return;

	// zero out the flag
	pInst->flag = 0;
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

/******************************************************************************/
/*!
\brief
Reads bullet spawn message from the client and store them in the map to write 
back to the players
format: everything after command id
[2 bytes, number of bullets][4bytes, int Object ID][4 bytes, float X position]
[4 bytes, float Y position][8 bytes, vec2 velocity][4 bytes, float rotation]
[4 bytes, float timestamp]...
*/
/******************************************************************************/
void ReadBullet(std::istream& input, unsigned short playerID)
{
	unsigned short numBullets = 0;
	input.read(reinterpret_cast<char*>(&numBullets), sizeof(unsigned short));

	for (int i = 0; i < numBullets; ++i)
	{
		int objectID;
		float posX, posY;
		float velX, velY;
		float rotation;
		float timestamp;

		input.read(reinterpret_cast<char*>(&objectID), sizeof(int));
		input.read(reinterpret_cast<char*>(&posX), sizeof(float));
		input.read(reinterpret_cast<char*>(&posY), sizeof(float));
		input.read(reinterpret_cast<char*>(&velX), sizeof(float));
		input.read(reinterpret_cast<char*>(&velY), sizeof(float));
		input.read(reinterpret_cast<char*>(&rotation), sizeof(float));
		input.read(reinterpret_cast<char*>(&timestamp), sizeof(float));

		AEVec2 pos = { posX, posY };
		AEVec2 vel = { velX, velY };
		AEVec2 scale = { BULLET_SCALE_X, BULLET_SCALE_Y };
		Bullet newBullet = { objectID, pos, vel, rotation, timestamp };
		bulletMap[playerID].push_back(newBullet);
	}
}

/******************************************************************************/
/*!
\brief
writes the bullet message back into the output stream
format: 
[Player ID1][All the bullets of player 1][Player ID 2][All the bullets of player 2]...
*/
/******************************************************************************/
void WriteBullet(std::ostream& output)
{
	/*unsigned short numPlayers = static_cast<unsigned short>(bulletMap.size());
	output.write(reinterpret_cast<const char*>(&numPlayers), sizeof(unsigned short));*/

	for (const auto& [playerID, bullets] : bulletMap)
	{
		output.write(reinterpret_cast<const char*>(&playerID), sizeof(unsigned short));

		unsigned short numBullets = static_cast<unsigned short>(bullets.size());
		output.write(reinterpret_cast<const char*>(&numBullets), sizeof(unsigned short));

		for (Bullet bullet : bullets)
		{
			float timestamp = static_cast<float>(AEGetTime(nullptr));

			output.write(reinterpret_cast<const char*>(&bullet.objectID), sizeof(int));
			output.write(reinterpret_cast<const char*>(&bullet.pos.x), sizeof(float));
			output.write(reinterpret_cast<const char*>(&bullet.pos.y), sizeof(float));
			output.write(reinterpret_cast<const char*>(&bullet.velocity.x), sizeof(float));
			output.write(reinterpret_cast<const char*>(&bullet.velocity.y), sizeof(float));
			output.write(reinterpret_cast<const char*>(&bullet.rotation), sizeof(float));
			output.write(reinterpret_cast<const char*>(&bullet.timeStamp), sizeof(float));
		}
	}

	bulletMap.clear();
}


/******************************************************************************/
/*!
\brief
Create asteriods and push them into the queue for writing later
*/
/******************************************************************************/
void CreateNewAsteroid()
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

	} while (pos.x < spShip->posCurr.x + 200 && pos.x > spShip->posCurr.x - 200 
		|| pos.y < spShip->posCurr.y + 200 && pos.y > spShip->posCurr.y - 200);
	vel = { (float)(rand() % 200) - 100.f,(float)(rand() % 200) - 100.f };
	scale = { (float)(rand() % (int)(ASTEROID_MAX_SCALE_X - ASTEROID_MIN_SCALE_X)) 
		+ ASTEROID_MIN_SCALE_X,(float)(rand() % (int)(ASTEROID_MAX_SCALE_Y - ASTEROID_MIN_SCALE_Y) + ASTEROID_MIN_SCALE_Y) };

	GameObjInst* asteroid = gameObjInstCreate(TYPE_ASTEROID, &scale, &pos, &vel, 0.0f);
	sGameObjInstNum++;

	newAsteroidQueue.push(asteroid);
}


/******************************************************************************/
/*!
\brief
Create asteriods and push them into the queue for writing later
*/
/******************************************************************************/
void WriteNewAsteroids(std::ostream& output)
{
	unsigned short numAsteroids = static_cast<unsigned short>(newAsteroidQueue.size());
	output.write(reinterpret_cast<const char*>(&numAsteroids), sizeof(unsigned short));

	while (!newAsteroidQueue.empty())
	{
		GameObjInst* asteroid = newAsteroidQueue.front();
		newAsteroidQueue.pop();

		int objectID = static_cast<int>(asteroid - sGameObjInstList); // or use hash or internal ID

		float timestamp = static_cast<float>(AEGetTime(nullptr));

		output.write(reinterpret_cast<const char*>(&objectID), sizeof(int));
		output.write(reinterpret_cast<const char*>(&asteroid->posCurr.x), sizeof(float));
		output.write(reinterpret_cast<const char*>(&asteroid->posCurr.y), sizeof(float));
		output.write(reinterpret_cast<const char*>(&asteroid->velCurr.x), sizeof(float));
		output.write(reinterpret_cast<const char*>(&asteroid->velCurr.y), sizeof(float));
		output.write(reinterpret_cast<const char*>(&asteroid->dirCurr), sizeof(float));
		output.write(reinterpret_cast<const char*>(&asteroid->scale.x), sizeof(float));
		output.write(reinterpret_cast<const char*>(&asteroid->scale.y), sizeof(float));
		output.write(reinterpret_cast<const char*>(&timestamp), sizeof(float));
	}
}


