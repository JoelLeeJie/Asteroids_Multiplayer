/* /* Start Header
*****************************************************************/
/*!
\file Checksum.cpp
\author Yee Tong (yeetong.t\@digipen.edu)
\date 19 March 2025
\brief
This file implements the functions to calculate and/or validate checksum given a length of data.

Copyright (C) 2025 DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the
prior written consent of DigiPen Institute of Technology is prohibited.
*/
/* End Header
*******************************************************************/

#include "Checksum.hpp"

/*
    \brief
    Calculates checksum given the data and its length.
    Returns the computed checksum.
*/
uint16_t CalculateChecksum(size_t length_of_data, void* data) {

    uint32_t checksum = 0; //checksum starts at 0
  

    // If data length is odd, copy and pad with 0
    if (length_of_data % 2 == 1) {
        ((uint8_t*)data)[length_of_data] = 0x00; // if the raw bytes is odd number, we add padding behind
        length_of_data++; // as we add a new byte, we need to increase the length as well
    }

    uint8_t* buffer = static_cast<uint8_t*>(data); //initialise the raw data and cast it to 8 bits (1 byte) //we doing bytes by bytes & parse the raw data in bytes (updated)

    /*
        Treat data as sequence of 16 bit integers, adding it up.
    */
    for (size_t i = 0; i < length_of_data; i += 2) {
        uint16_t hex = (static_cast<uint16_t>(buffer[i]) << 8) | buffer[i + 1];
        checksum += hex; //add the raw hex btyes (2 bytes)
    }

    //after adding carry the bit forward
    while (checksum >> 16) {
        checksum = (checksum & 0xFFFF) + (checksum >> 16);
    }

    uint16_t result = ~checksum;
    return result; //in checksum, we need to return the one's complement

}

/*
    \brief
    Validates checksum against data passed in.
    Returns true if successful, false if checksum fails.
*/
bool ValidateChecksum(size_t length_of_data, void* data, uint16_t checksum_val) {

    uint16_t checking_sum = CalculateChecksum(length_of_data, data);
    //what we want to do here is to check the received data, but we do not want the ones complement, hence,
    //we need to reverse it
    checking_sum = ~checking_sum;

    //add the 2 sum together, one is checksum, one is just original sum
    uint32_t sum = static_cast<uint32_t>(checking_sum) + static_cast<uint32_t>(checksum_val);

    /*std::cout << "Computed: 0x" << std::hex << checking_sum
        << " + Received: 0x" << checksum_val
        << " = 0x" << (sum & 0xFFFF) << std::endl;*/

    return (sum & 0xFFFF) == 0xFFFF;  // check whether they are equals to FFFF
}