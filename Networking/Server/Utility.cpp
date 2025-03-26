/* /* Start Header
*****************************************************************/
/*!
\file Utility.cpp
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
#include "Utility.hpp"
#include <fstream>

/*
	\brief
	Reads a length of data from file, returning the bytes read.
*/
int GetDataFromFile(std::string filename, int offset, int bytes_to_read, char* buffer) {

	std::ifstream FILE(filename, std::ios::binary);

	
	if (!FILE.is_open()) {
		
		throw std::runtime_error("FILE: " + filename + " could not be opened\n");
	}

	//go to the offset position
	FILE.seekg(offset);
	FILE.read(buffer, bytes_to_read);
	return (int)FILE.gcount();

}