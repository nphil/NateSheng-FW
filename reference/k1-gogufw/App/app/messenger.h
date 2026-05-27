#ifndef APP_MESSENGER_H
#define APP_MESSENGER_H
#include <stdbool.h>
#include "driver/keyboard.h"

void MSG_Init(void);
void MSG_Open(void);
void MSG_Tick(void);
void MSG_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld);
bool MSG_HasUnread(void);

#endif
