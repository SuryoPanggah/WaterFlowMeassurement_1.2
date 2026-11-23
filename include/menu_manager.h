#ifndef MENU_MANAGER_H
#define MENU_MANAGER_H

#include <Arduino.h>

// Initialize menu state variables
void initMenuManager();

// Process button presses (pass true on the single frame a button is pressed)
void handleMenuInput(bool upPressed, bool downPressed, bool selectPressed, bool backPressed);

// Render the active menu screen onto the 128x64 OLED display
void renderMenu();

// Remote control hooks for the Mobile Web Portal (keeps OLED & Web UI synced)
void remoteSetSiteAddress(const String& site);
String remoteGetSiteAddress();
bool remoteStartTest(uint8_t mode, uint16_t durationMinutes);
void remoteStopAndSaveTest();
void remoteAbortTest();
bool remoteIsTestRunning();
uint32_t remoteGetRemainingOrElapsedSec();

// Two-way synchronized UI language state (false = English [default], true = Bahasa Indonesia)
bool isLanguageIndonesian();
void setLanguageIndonesian(bool isId);

#endif // MENU_MANAGER_H
