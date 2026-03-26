#ifndef DEVICE_STATE_H
#define DEVICE_STATE_H

void deviceStateInit();

int deviceGetSelectedIndex();
void deviceSetSelectedIndex(int index);
void deviceClampSelectedIndex(int count);

bool deviceIsShowingDetail();
void deviceSetShowingDetail(bool showing);

#endif

