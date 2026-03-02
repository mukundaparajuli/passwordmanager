#ifndef UI_H
#define UI_H

#include <Arduino.h>

// Initialize the OLED display. Returns true on success.
bool uiInit();

// Show the boot splash screen
void uiShowBootScreen();

// Show "No Credentials" message
void uiShowNoCredentials();

// Show the scrollable credential list
void uiShowCredentialList(int selectedIndex, int credentialCount);

// Show the detail view for a credential at the given index
void uiShowCredentialDetail(int index);

// ---- PIN screens ----

// Prompt user to set a new PIN via serial
void uiShowPinSetup();

// Prompt user to enter their PIN via serial
void uiShowPinEntry();

// Show PIN accepted feedback
void uiShowPinOk();

// Show wrong PIN feedback
void uiShowPinFail();

// Show PIN successfully set feedback
void uiShowPinCreated();

#endif
