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

void initIndicator();       // initialize the indicator. This function should be called in the setup function of the program to ensure that the indicator is properly initialized before it is used. 
                            // It sets up the PWM for the LED pin and configures the PWM slice for use with the indicator.

void updateIndicator();     // update the indicator based on the current state of the 6502. This function should be called in the main loop of the program to ensure that the indicator is updated regularly.
