#ifndef MENU_MANAGER_H
#define MENU_MANAGER_H

#include <Arduino.h>

// Initialize menu state variables
void initMenuManager();

// Process button presses (pass true on the single frame a button is pressed)
void handleMenuInput(bool upPressed, bool downPressed, bool selectPressed, bool backPressed);

// Render the active menu screen onto the 128x64 OLED display
void renderMenu();

#endif // MENU_MANAGER_H
