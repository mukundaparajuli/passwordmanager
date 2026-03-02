#ifndef INPUT_H
#define INPUT_H

#include <Arduino.h>

// Initialize button pins
void inputInit();

// Read debounced button states
void readButtons(bool &upPressed, bool &downPressed);

// Check serial for ADD commands. If a credential is parsed, stores it and returns true.
bool handleSerialInput();

#endif
