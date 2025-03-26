/******************************************************************************/
/*!
\file		Collision.h
\author 	Digipen, Joel Lee Jie
\par    	email: joeljie.lee\@digipen.edu  
\date   	February 08, 2024
\brief		Declares struct and function to check for collisions using AABB dynamic collision technique.
Note that function CollisionIntersection_RectRect checks for collision using x,y axes only. 

Copyright (C) 20xx DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the
prior written consent of DigiPen Institute of Technology is prohibited.
 */
/******************************************************************************/

#ifndef CSD1130_COLLISION_H_
#define CSD1130_COLLISION_H_

#include "AEEngine.h"

/**************************************************************************/
/*!

	*/
/**************************************************************************/
struct AABB
{
	AEVec2	min;
	AEVec2	max;
};

bool CollisionIntersection_RectRect(const AABB& aabb1,            //Input
									const AEVec2& vel1,           //Input 
									const AABB& aabb2,            //Input 
									const AEVec2& vel2,           //Input
									float& firstTimeOfCollision); //Output: the calculated value of tFirst, must be returned here


#endif // CSD1130_COLLISION_H_