#ifndef INPUT_H
#define INPUT_H

#include <Arduino.h>

struct InputEvents {
    bool next_pressed = false;    // Navigate to next credential
    bool confirm_pressed = false; // Select/confirm credential
};

void inputInit();
void inputPoll(InputEvents &events);

#endif
