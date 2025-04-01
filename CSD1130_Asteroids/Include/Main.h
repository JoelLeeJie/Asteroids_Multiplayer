/******************************************************************************/
/*!
\file		Main.h
\author 	Digipen, Joel Lee Jie
\par    	email: joeljie.lee\@digipen.edu 
\date   	February 08, 2024
\brief		Includes headers necessary in main.

Copyright (C) 20xx DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the
prior written consent of DigiPen Institute of Technology is prohibited.
 */
/******************************************************************************/


#ifndef CSD1130_MAIN_H_
#define CSD1130_MAIN_H_

//------------------------------------
// Globals

extern float	g_dt;
extern double	g_appTime;

// ---------------------------------------------------------------------------
// includes





#include "winsock2.h"		// ...or Winsock alone
#include "ws2tcpip.h"		// getaddrinfo()

 // Tell the Visual Studio linker to include the following library in linking.
 // Alternatively, we could add this file to the linker command-line parameters,
 // but including it in the source code simplifies the configuration.
#pragma comment(lib, "ws2_32.lib")
#include "Windows.h"		// Entire Win32 API...
#include <iostream>			// cout, cerr
#include <iomanip>

//For Multi-threading
#include <thread>
#include <mutex>
#include <chrono>
#include <filesystem>
#include <fstream>







#include "AEEngine.h"
#include "Math.h"

#include "GameStateMgr.h"
#include "GameState_Asteroids.h"
#include "Collision.h"
#include "Client.hpp"

#endif











