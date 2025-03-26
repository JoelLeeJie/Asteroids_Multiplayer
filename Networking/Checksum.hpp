/* /* Start Header
*****************************************************************/
/*!
\file Checksum.hpp
\author Yee Tong (yeetong.t\@digipen.edu)
\date 19 March 2025
\brief
This file declares the functions to calculate and/or validate checksum given a length of data.

Copyright (C) 2025 DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the
prior written consent of DigiPen Institute of Technology is prohibited.
*/
/* End Header
*******************************************************************/
#include <iostream>
#include <cstdint>
#include <cstddef>


/*
    \brief
    Calculates checksum given the data and its length.
    Returns the computed checksum.
*/
uint16_t CalculateChecksum(size_t length_of_data, void* data);
/*
    \brief
    Validates checksum against data passed in.
    Returns true if successful, false if checksum fails.
*/
bool ValidateChecksum(size_t length_of_data, void* data, uint16_t checksum_val);