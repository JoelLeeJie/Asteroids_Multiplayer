 /* 
Start Header
 *****************************************************************
 /
 /* 
! \file README.txt
 \author Joel Lee Jie
 \par joeljie.lee\@digipen.edu
 \date 19 March 2025
 \ brief Copyright (C) 2025 DigiPen Institute of Technology.
 Reproduction or disclosure of this file or its contents without the prior
 written consent of DigiPen Institute of Technology is prohibited . *
 /
 /* 
End Header
 *******************************************************************
 /

Server Program can be found under Server/
Client Program can be found under Client/

Before starting the program, important parameters can be edited in Server/config.txt
--> First line is udp_data_packet_size, 512 by default.
--> Second line is timeout length, 0.5 by default.

On the server/client side, entering the relative download directory is enough.
For example: "Path       : Downloads"

If it cannot compile, switch the program to use c++17 standard.

If server.exe crashes, please ensure Config.txt is copied to that directory. Find under Server/config.txt