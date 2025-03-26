/* Start Header
*****************************************************************/
/*!
\file Utility.hpp
\author Yee Tong (yeetong.t\@digipen.edu)
\date 19 March 2025
\brief
This file implements a helper function that reads a length of data from file.

Copyright (C) 2025 DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the
prior written consent of DigiPen Institute of Technology is prohibited.
*/
/* End Header
*******************************************************************/
#ifndef UTILITY_HPP
#define UTILITY_HPP
#include <iostream>

/*
	\brief
	Reads a length of data from file, returning the bytes read.
*/
int GetDataFromFile(std::string filename, int offset, int bytes_to_read, char* buffer);
#endif