#include "input.h"

#include "config.h"
#include "storage.h"

void inputInit() {
  pinMode(BTN_UP_OK, INPUT_PULLUP);
  pinMode(BTN_DOWN_BACK, INPUT_PULLUP);
}

void readButtons(bool &upPressed, bool &downPressed) {
  static unsigned long lastDebounceTime = 0;
  const unsigned long debounceDelay = 50;

  upPressed = false;
  downPressed = false;

  if (millis() - lastDebounceTime > debounceDelay) {
    if (digitalRead(BTN_UP_OK) == LOW)
      upPressed = true;

    if (digitalRead(BTN_DOWN_BACK) == LOW)
      downPressed = true;

    lastDebounceTime = millis();
  }
}

bool handleSerialInput() {
  if (!Serial.available())
    return false;

  String input = Serial.readStringUntil('\n');
  input.trim();

  if (input.startsWith("ADD |")) {
    int p1 = input.indexOf('|');
    int p2 = input.indexOf('|', p1 + 1);
    int p3 = input.indexOf('|', p2 + 1);

    if (p1 != -1 && p2 != -1 && p3 != -1) {
      String service = input.substring(p1 + 1, p2);
      String username = input.substring(p2 + 1, p3);
      String password = input.substring(p3 + 1);

      service.trim();
      username.trim();
      password.trim();

      storeCredential(service, username, password);
      return true;
    } else {
      Serial.println("Invalid format. Use: ADD | service | username | password");
    }
  }

  return false;
}
