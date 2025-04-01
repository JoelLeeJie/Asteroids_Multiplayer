/******************************************************************************/
/*!
\file		Collision.cpp
\author 	Digipen, Joel Lee Jie
\par    	email: joeljie.lee\@digipen.edu
\date   	February 08, 2024
\brief		Defines function to check for collisions using AABB dynamic collision technique.
CollisionIntersection_RectRect can check for both static and dynamic collision between rectangles.

Copyright (C) 20xx DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the
prior written consent of DigiPen Institute of Technology is prohibited.
 */
/******************************************************************************/

#include "main.h"

/**************************************************************************/
/*!
\brief Checks for collision between two (moving) rectangles.
\param[in] aabb1
Bounding box for object 1.
\param[in] vel1
Vec2 velocity for object 1.
\param[in] aabb2
Bounding box for object 2.
\param[in] vel2
Vec2 velocity for object 2.
\param[out] firstTimeOfCollision
Stores the result of first time of collision. If no collision, will be set to a very large number(10000). 
Only valid if function returns false.
\return 
true if collision this instant, false otherwise(in which case check firstTimeOfCollision to see if it'll collide within this frame). 
	*/
/**************************************************************************/
bool CollisionIntersection_RectRect(const AABB & aabb1,          //Input
									const AEVec2 & vel1,         //Input 
									const AABB & aabb2,          //Input 
									const AEVec2 & vel2,         //Input
									float& firstTimeOfCollision) //Output: the calculated value of tFirst, below, must be returned here
{
	UNREFERENCED_PARAMETER(aabb1);
	UNREFERENCED_PARAMETER(vel1);
	UNREFERENCED_PARAMETER(aabb2);
	UNREFERENCED_PARAMETER(vel2);
	UNREFERENCED_PARAMETER(firstTimeOfCollision);

	//check if already colliding.
	if (aabb1.min.x < aabb2.max.x &&
		aabb1.max.x > aabb2.min.x && 
		aabb1.min.y < aabb2.max.y && 
		aabb1.max.y > aabb2.min.y) return true;

	//Not colliding yet.
	
	//tFirst is the latest time of x or y that both objects intersect on the same axis.
	// tlast is the earliest time of either x or y that both objects stop intersecting on the same axis. 
	float tFirst{}, tLast{}; 

	//x and y axis. Done this way cause want to try proper SAT technique.
	AEVec2 axes[]{ {0, 1}, {1, 0} };

	//Iterate for each axis.
	for (int i = 0; i < 2; i++)
	{
		float min1{ 10000 }, max1{ -10000 }, min2{ 10000 }, max2{ -10000 };
		float distance;

		/*
			Pretend that I don't know the axis, and i'm checking all 4 corners(over here it's just 2 spots)
			I'm checking the min and max distance along the axis for each object 1 and 2. 
		*/
		AEVec2 temp{ aabb1.min };
		distance = AEVec2DotProduct(&temp, &axes[i]) / AEVec2Length(&axes[i]);
		min1 = (distance < min1) ? distance : min1;
		max1 = (distance > max1) ? distance : max1;
		temp = { aabb1.max };
		distance = AEVec2DotProduct(&temp, &axes[i]) / AEVec2Length(&axes[i]);
		min1 = (distance < min1) ? distance : min1;
		max1 = (distance > max1) ? distance : max1;
		
		temp = { aabb2.min };
		distance = AEVec2DotProduct(&temp, &axes[i]) / AEVec2Length(&axes[i]);
		min2 = (distance < min2) ? distance : min2;
		max2 = (distance > max2) ? distance : max2;
	    temp = { aabb2.max };
		distance = AEVec2DotProduct(&temp, &axes[i]) / AEVec2Length(&axes[i]);
		min2 = (distance < min2) ? distance : min2;
		max2 = (distance > max2) ? distance : max2;

		/*
			Finding relative velocity along the axis.
		*/
		float velocity1, velocity2;
		temp = { vel1 };
		velocity1 = AEVec2DotProduct(&temp, &axes[i]) / AEVec2Length(&axes[i]);
		temp = { vel2 };
		velocity2 = AEVec2DotProduct(&temp, &axes[i]) / AEVec2Length(&axes[i]);
		
		//Find the relative velocity of 1, making 2 "stationary" in comparison.
		velocity1 = velocity1 - velocity2;

		//If no possibility of ever colliding, early return.
		//Remember that 1 is moving, 2 is stationary.
		if (velocity1 > 0 && min1 > max2)
		{
			//some nonsense number here
			firstTimeOfCollision = 1000000;
			return false;
		}
		if (velocity1 < 0 && max1 < min2)
		{
			//some nonsense number here
			firstTimeOfCollision = 1000000;
			return false;
		}
		if (velocity1 == 0) //moving parallel or stationary.
		{
			//Not overlapping on this axis, and no longer moving on this axis either, cfm will not collide.
			if (min1 > max2 || max1 < min2)
			{
				//some nonsense number here
				firstTimeOfCollision = 1000000;
				return false;
			}
		}

		/*
		* Early return done already. Now cfm will collide, it's just a matter of when.
			time first and time last for this axis only.
			Calculated by distance in between along this axis, divided by relative velocity.
		*/
		float temp_tFirst, temp_tLast;
		if (velocity1>0) //moving right, so object 1 should be on the left, after early return.
		{
			//if -ve, that means they already overlap.
			temp_tFirst = (min2 - max1) / velocity1;

			temp_tLast = (max2 - min1) / velocity1;
		}
		else //moving left
		{
			//max2-min1 cause velocity is -ve, so -ve/-ve is +ve. 
			//If still -ve, that means they already overlapping.
			temp_tFirst = (max2-min1) / velocity1;

			temp_tLast = (min2 - max1) / velocity1;
		}
		//tFirst takes the longest time, tLast takes the shorter time.
		tFirst = (temp_tFirst > tFirst) ? temp_tFirst : tFirst;
		tLast = (temp_tLast < tLast) ? temp_tLast : tLast;

	}
	//After getting tfirst and tlast from all axes, compare.
	
	//if tlast comes first, that means it never collides.
	if (tFirst > tLast)
	{
		//nonsense number again.
		firstTimeOfCollision = 10000;
		return false;
	}
	
	//correct number now, it'll collide in __tFirst__ amount of time.
	firstTimeOfCollision = tFirst;
	return false; //still return false cause never collide in this instance. Need to check tFirst against delta time outside the function.
}