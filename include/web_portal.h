#ifndef WEB_PORTAL_H
#define WEB_PORTAL_H

#include <Arduino.h>

// Start HTTP server and mDNS (http://water-rig.local) when Wi-Fi connects
void startWebPortal();

// Stop HTTP server cleanly when switching to Bluetooth Classic mode
void stopWebPortal();

// Handle incoming phone browser requests (call inside updateNetwork())
void handleWebPortal();

// Check if web server is currently active
bool isWebPortalRunning();

#endif // WEB_PORTAL_H
