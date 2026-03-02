#include "ui.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

#include "config.h"
#include "storage.h"

static Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

bool uiInit() {
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("OLED init failed!");
    return false;
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  return true;
}

void uiShowBootScreen() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Shield Key Ready");
  display.display();
}

void uiShowNoCredentials() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("No Credentials");
  display.display();
}

void uiShowCredentialList(int selectedIndex, int credentialCount) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Vault");

  const int maxVisible = 6;
  int startIndex = 0;

  if (selectedIndex >= maxVisible)
    startIndex = selectedIndex - maxVisible + 1;

  for (int i = 0; i < maxVisible; i++) {
    int credIndex = startIndex + i;

    if (credIndex >= credentialCount)
      break;

    String cred = getCredentialByIndex(credIndex);

    String service, username, password;
    if (!parseCredential(cred, service, username, password))
      continue;

    if (credIndex == selectedIndex)
      display.print("-> ");
    else
      display.print("   ");

    display.println(service);
  }

  display.display();
}

void uiShowCredentialDetail(int index) {
  String cred = getCredentialByIndex(index);

  String service, username, password;
  if (!parseCredential(cred, service, username, password))
    return;

  display.clearDisplay();
  display.setCursor(0, 0);

  display.println(service);
  display.println(username);
  display.println(password);

  display.display();
}

// ================= PIN Screens =================

void uiShowPinSetup() {
  display.clearDisplay();
  display.setCursor(0, 10);
  display.println("No PIN set.");
  display.println();
  display.println("Enter 4-digit PIN");
  display.println("via Serial Monitor");
  display.display();
}

void uiShowPinEntry() {
  display.clearDisplay();
  display.setCursor(0, 10);
  display.println("Device Locked");
  display.println();
  display.println("Enter PIN via");
  display.println("Serial Monitor");
  display.display();
}

void uiShowPinOk() {
  display.clearDisplay();
  display.setCursor(0, 20);
  display.setTextSize(2);
  display.println("Unlocked!");
  display.setTextSize(1);
  display.display();
}

void uiShowPinFail() {
  display.clearDisplay();
  display.setCursor(0, 20);
  display.setTextSize(2);
  display.println("Wrong PIN");
  display.setTextSize(1);
  display.display();
}

void uiShowPinCreated() {
  display.clearDisplay();
  display.setCursor(0, 20);
  display.setTextSize(2);
  display.println("PIN Set!");
  display.setTextSize(1);
  display.display();
}
