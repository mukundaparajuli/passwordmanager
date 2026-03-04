#ifndef UI_H
#define UI_H

#include <Arduino.h>

bool uiInit();
void uiShowBootScreen();
void uiShowNoCredentials();
void uiShowCredentialList(int selectedIndex, int credentialCount);
void uiShowCredentialDetail(int index);
void uiShowPinSetup();
void uiShowPinEntry();
void uiShowPinOk();
void uiShowPinFail();
void uiShowPinCreated();

#endif
