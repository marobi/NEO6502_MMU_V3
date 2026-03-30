/*
This software is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This software is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

*/
#pragma once

#define CI_GROUP_UNK  0
#define CI_GROUP_VDU  1
#define CI_GROUP_GDU  2
#define CI_GROUP_SND  3

#define CPU_REGISTER_BASE 0xCF80
#define MAX_REGISTERS  32

void initCPInterface();
