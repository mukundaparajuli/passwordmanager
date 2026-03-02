#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>

#include "config.h"

// ================= OLED =================

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Preferences prefs;

// ================= Navigation State =================

int selectedIndex = 0;
int credentialCount = 0;

bool showingDetail = false;

// Press tracking
static int upPressCount = 0;
static int downPressCount = 0;

static unsigned long upLastPressTime = 0;
static unsigned long downLastPressTime = 0;

const unsigned long DOUBLE_CLICK_WINDOW = 400;

// ================= Storage =================

String getCredentialByIndex(int index) {
  prefs.begin("credentials", true);

  String key = "cred" + String(index);
  String value = prefs.getString(key.c_str(), "");

  prefs.end();

  return value;
}

// ================= UI Rendering =================

void showCredentialListUI() {
  display.clearDisplay();
  display.setCursor(0, 0);

  prefs.begin("credentials", true);
  credentialCount = prefs.getInt("count", 0);
  prefs.end();

  display.println("Vault");

  int maxVisible = 6;
  int startIndex = 0;

  if (selectedIndex >= maxVisible)
    startIndex = selectedIndex - maxVisible + 1;

  for (int i = 0; i < maxVisible; i++) {
    int credIndex = startIndex + i;

    if (credIndex >= credentialCount)
      break;

    String cred = getCredentialByIndex(credIndex);

    int p1 = cred.indexOf('|');

    if (p1 == -1)
      continue;

    String service = cred.substring(0, p1);

    if (credIndex == selectedIndex)
      display.print("-> ");
    else
      display.print("   ");

    display.println(service);
  }

  display.display();
}

// ================= Detail View =================

void showCredentialDetail(int index) {
  String cred = getCredentialByIndex(index);

  int p1 = cred.indexOf('|');
  int p2 = cred.indexOf('|', p1 + 1);

  if (p1 == -1 || p2 == -1)
    return;

  String service = cred.substring(0, p1);
  String username = cred.substring(p1 + 1, p2);
  String password = cred.substring(p2 + 1);

  display.clearDisplay();
  display.setCursor(0, 0);

  display.println(service);
  display.println(username);
  display.println(password);

  display.display();
}

// ================= Storage Write =================

void storeCredential(String service, String username, String password) {
  prefs.begin("credentials", false);

  int count = prefs.getInt("count", 0);

  String key = "cred" + String(count);
  String value = service + "|" + username + "|" + password;

  prefs.putString(key.c_str(), value.c_str());
  prefs.putInt("count", count + 1);

  prefs.end();

  Serial.println("Credential Stored: " + value);
}

// ================= Serial Input =================

void handleSerialInput() {
  if (!Serial.available())
    return;

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

    } else {
      Serial.println(
          "Invalid format. Use: ADD | service | username | password");
    }
  }
}

// ================= Button Reader =================

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

// ================= Setup =================

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n=== Shield Key Booting ===");

  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);

  pinMode(BTN_UP_OK, INPUT_PULLUP);
  pinMode(BTN_DOWN_BACK, INPUT_PULLUP);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("OLED init failed!");
    while (true)
      delay(1000);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.println("Shield Key Ready");
  display.display();

  delay(1000);
}

// ================= Loop =================

void loop() {

  handleSerialInput();

  bool upPressed;
  bool downPressed;

  readButtons(upPressed, downPressed);

  prefs.begin("credentials", true);
  credentialCount = prefs.getInt("count", 0);
  prefs.end();

  if (credentialCount == 0) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("No Credentials");
    display.display();
    delay(200);
    return;
  }

  // ================= UP BUTTON =================

  if (upPressed) {

    unsigned long now = millis();

    if (now - upLastPressTime > DOUBLE_CLICK_WINDOW)
      upPressCount = 0;

    upPressCount++;
    upLastPressTime = now;

    // Double click UP → detail view
    if (upPressCount == 2) {
      showingDetail = true;
      showCredentialDetail(selectedIndex);

      upPressCount = 0;
      delay(250);
      return;
    }

    // Single click UP → move up
    if (!showingDetail) {
      selectedIndex--;

      if (selectedIndex < 0)
        selectedIndex = credentialCount - 1;
    }

    delay(120);
  }

  // ================= DOWN BUTTON =================

  if (downPressed) {

    unsigned long now = millis();

    if (now - downLastPressTime > DOUBLE_CLICK_WINDOW)
      downPressCount = 0;

    downPressCount++;
    downLastPressTime = now;

    // Double click DOWN → back to list
    if (downPressCount == 2) {
      showingDetail = false;

      downPressCount = 0;
      delay(250);
      return;
    }

    // Single click DOWN → move down
    if (!showingDetail) {
      selectedIndex++;

      if (selectedIndex >= credentialCount)
        selectedIndex = 0;
    }

    delay(120);
  }

  if (showingDetail)
    showCredentialDetail(selectedIndex);
  else
    showCredentialListUI();
}
// ADD | facebook | user2 | mypassword123