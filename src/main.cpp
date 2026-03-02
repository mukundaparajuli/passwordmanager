#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>

#include "config.h"

// OLED display
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Storage
Preferences prefs;

// Button states
bool upPressed = false;
bool downPressed = false;

// ----------------------------------------------------
// Storage Helper
// ----------------------------------------------------

void saveCredentialPlain(String service, String username, String password) {
  prefs.begin("credentials", false);

  int count = prefs.getInt("count", 0);

  String key = "cred" + String(count);

  String value = service + "|" + username + "|" + password;

  prefs.putString(key.c_str(), value);
  prefs.putInt("count", count + 1);

  prefs.end();

  Serial.println("Credential stored");
}

// ----------------------------------------------------
// Serial Provisioning Handler
// ----------------------------------------------------

void handleSerialProvisioning() {
  if (!Serial.available())
    return;

  String line = Serial.readStringUntil('\n');
  line.trim();

  if (!line.startsWith("ADD|"))
    return;

  int p1 = line.indexOf('|');
  int p2 = line.indexOf('|', p1 + 1);
  int p3 = line.indexOf('|', p2 + 1);

  if (p1 == -1 || p2 == -1 || p3 == -1)
    return;

  String service = line.substring(p1 + 1, p2);
  String username = line.substring(p2 + 1, p3);
  String password = line.substring(p3 + 1);

  saveCredentialPlain(service, username, password);

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Stored Credential");
  display.display();
}

// ----------------------------------------------------
// Setup
// ----------------------------------------------------

void setup() {
  Serial.begin(115200);

  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);

  pinMode(BTN_UP_OK, INPUT_PULLUP);
  pinMode(BTN_DOWN_BACK, INPUT_PULLUP);

  prefs.begin("credentials", false);
  prefs.end();

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("OLED init failed");
    while (true)
      ;
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("Shield Key Ready");
  display.display();

  delay(1000);
}

// ----------------------------------------------------
// Button Reader
// ----------------------------------------------------

void readButtons() {
  static unsigned long lastDebounceTime = 0;
  const unsigned long debounceDelay = 50;

  if (millis() - lastDebounceTime > debounceDelay) {
    upPressed = (digitalRead(BTN_UP_OK) == LOW);
    downPressed = (digitalRead(BTN_DOWN_BACK) == LOW);

    lastDebounceTime = millis();
  }
}

// ----------------------------------------------------
// Loop
// ----------------------------------------------------

void loop() {
  handleSerialProvisioning();
  readButtons();

  display.clearDisplay();
  display.setCursor(0, 0);

  int count;

  prefs.begin("credentials", true);
  count = prefs.getInt("count", 0);

  display.print("Credentials: ");
  display.println(count);

  prefs.end();

  if (upPressed)
    display.println("UP Pressed");

  if (downPressed)
    display.println("DOWN Pressed");

  display.display();

  delay(20);
}