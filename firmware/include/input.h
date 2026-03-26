#ifndef INPUT_H
#define INPUT_H

#include <Arduino.h>

struct InputEvents {
    bool up_pressed = false;
    bool down_pressed = false;
    bool confirm_short = false;
    bool confirm_long = false;
};

void inputInit();
void inputPoll(InputEvents &events);

#endif
