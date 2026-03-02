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
