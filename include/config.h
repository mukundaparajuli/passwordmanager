#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// oled screen configuration

#define OLED_SDA_PIN 22
#define OLED_SCL_PIN 21

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

// buttons configuration
#define BTN_UP_OK 4
#define BTN_DOWN_BACK 5

#endif