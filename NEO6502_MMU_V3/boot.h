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

// =====================================================
// Boot / Menu System Public Interface
// Depends on ini_parser for configuration data
// =====================================================

// Show serial menu and boot selected configuration
void bootSystemWithMenu();

// Activate a configuration by internal index
// Returns true on success
bool activateConfiguration(int cfgIndex);
