#include <string.h>
#include "app/messenger.h"
#include "app/messenger_store.h"
#include "app/messenger_t9.h"
#include "app/messenger_rf.h"
#include "audio.h"
#include "ui/ui.h"
#include "misc.h"

typedef enum {
    MSG_SCREEN_HOME = 0,
    MSG_SCREEN_INBOX,
    MSG_SCREEN_OUTBOX,
    MSG_SCREEN_DRAFTS,
    MSG_SCREEN_COMPOSE,
    MSG_SCREEN_READ,
    MSG_SCREEN_SETTINGS,
    MSG_SCREEN_CALLSIGN,
} MSG_Screen_t;

MSG_Screen_t gMsgScreen = MSG_SCREEN_HOME;
uint8_t gMsgCursor;
uint8_t gMsgScroll;
uint8_t gMsgHomeCursor;
char gMsgComposeBuf[MSG_TEXT_LEN + 1];
MSG_T9Editor_t gMsgEditor;
MSG_T9Editor_t gMsgCallsignEditor;
char gMsgCallsignBuf[MSG_CALLSIGN_EDIT_LEN + 1];
uint8_t gMsgReadIndex;
uint8_t gMsgReadSource;
uint8_t gMsgSettingsCursor;
static bool gMsgComposeIsDraftEdit;
static uint8_t gMsgComposeDraftIndex;

void MSG_Init(void)
{
    MSG_STORE_Init();
}

void MSG_Open(void)
{
    MSG_Init();
    gMsgScreen = MSG_SCREEN_HOME;
    gMsgCursor = 0;
    gMsgScroll = 0;
    gMsgHomeCursor = 0;
    gRequestDisplayScreen = DISPLAY_MESSENGER;
}

void MSG_Tick(void)
{
    if (gMsgScreen == MSG_SCREEN_COMPOSE) MSG_T9_Tick(&gMsgEditor);
    else if (gMsgScreen == MSG_SCREEN_CALLSIGN) MSG_T9_Tick(&gMsgCallsignEditor);
}
bool MSG_HasUnread(void) { return MSG_STORE_HasUnread(); }

static uint8_t current_count(void)
{
    if (gMsgScreen == MSG_SCREEN_INBOX) return MSG_STORE_CountInbox();
    if (gMsgScreen == MSG_SCREEN_OUTBOX) return MSG_STORE_CountOutbox();
    if (gMsgScreen == MSG_SCREEN_DRAFTS) return MSG_STORE_CountDrafts();
    return 0;
}

static void list_move(int8_t dir)
{
    uint8_t count = current_count();
    if (count == 0) return;
    int16_t next = (int16_t)gMsgCursor + dir;
    if (next < 0) next = count - 1;
    if (next >= count) next = 0;
    gMsgCursor = (uint8_t)next;
    if (gMsgCursor < gMsgScroll) gMsgScroll = gMsgCursor;
    if (gMsgCursor >= gMsgScroll + 6) gMsgScroll = gMsgCursor - 5;
}

static void go_home(void)
{
    gMsgScreen = MSG_SCREEN_HOME;
    gMsgCursor = 0;
    gMsgScroll = 0;
}

static void open_list(MSG_Screen_t screen)
{
    gMsgScreen = screen;
    gMsgCursor = 0;
    gMsgScroll = 0;
}

static void open_sent_after_send(void)
{
    gMsgScreen = MSG_SCREEN_OUTBOX;
    gMsgCursor = 0;
    gMsgScroll = 0;
}

static void open_compose(const char *seed)
{
    gMsgComposeIsDraftEdit = false;
    gMsgComposeDraftIndex = 0;
    memset(gMsgComposeBuf, 0, sizeof(gMsgComposeBuf));
    if (seed) strncpy(gMsgComposeBuf, seed, MSG_TEXT_LEN);
    gMsgComposeBuf[MSG_TEXT_LEN] = 0;
    MSG_T9_Start(&gMsgEditor, gMsgComposeBuf, MSG_TEXT_LEN);
    gMsgScreen = MSG_SCREEN_COMPOSE;
}

static void open_draft_edit(uint8_t index)
{
    if (index >= MSG_DRAFT_CAPACITY) index = 0;
    gMsgComposeIsDraftEdit = true;
    gMsgComposeDraftIndex = index;
    memset(gMsgComposeBuf, 0, sizeof(gMsgComposeBuf));
    strncpy(gMsgComposeBuf, gMessengerConfig.drafts[index], MSG_TEXT_LEN);
    gMsgComposeBuf[MSG_TEXT_LEN] = 0;
    MSG_T9_Start(&gMsgEditor, gMsgComposeBuf, MSG_TEXT_LEN);
    gMsgScreen = MSG_SCREEN_COMPOSE;
}

static uint8_t read_count(void)
{
    return (gMsgReadSource == MSG_SCREEN_OUTBOX) ? MSG_STORE_CountOutbox() : MSG_STORE_CountInbox();
}

static MSG_Message_t *read_message(void)
{
    if (gMsgReadSource == MSG_SCREEN_OUTBOX) {
        if (gMsgReadIndex >= MSG_STORE_CountOutbox()) return 0;
        return &gMessengerOutbox[gMsgReadIndex];
    }
    if (gMsgReadIndex >= MSG_STORE_CountInbox()) return 0;
    return &gMessengerInbox[gMsgReadIndex];
}

static void read_move(int8_t dir)
{
    uint8_t count = read_count();
    if (count == 0) return;
    int16_t next = (int16_t)gMsgReadIndex + dir;
    if (next < 0) next = count - 1;
    if (next >= count) next = 0;
    gMsgReadIndex = (uint8_t)next;
    if (gMsgReadSource == MSG_SCREEN_INBOX) MSG_STORE_MarkInboxRead(gMsgReadIndex);
}

void MSG_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
    if (bKeyHeld) {
        if (bKeyPressed && gMsgScreen == MSG_SCREEN_COMPOSE && Key >= KEY_0 && Key <= KEY_9) {
            MSG_T9_HandleLongKey(&gMsgEditor, Key);
            gUpdateDisplay = true;
        }
        return;
    }
    if (bKeyPressed) return;

    switch (gMsgScreen) {
        case MSG_SCREEN_HOME:
            if (Key == KEY_UP || Key == KEY_DOWN) {
                gMsgHomeCursor = (uint8_t)((gMsgHomeCursor + (Key == KEY_UP ? 3 : 1)) % 4);
            } else if (Key == KEY_MENU) {
                switch (gMsgHomeCursor) {
                    case 0: open_list(MSG_SCREEN_INBOX); break;
                    case 1: open_compose(""); break;
                    case 2: open_list(MSG_SCREEN_OUTBOX); break;
                    case 3: open_list(MSG_SCREEN_DRAFTS); break;
                }
            } else if (Key == KEY_EXIT) {
                gRequestDisplayScreen = DISPLAY_MAIN;
            } else if (Key == KEY_1) {
                MSG_STORE_InjectNativePacket("DEMO INBOX MESSAGE");
            }
            break;

        case MSG_SCREEN_INBOX:
        case MSG_SCREEN_OUTBOX:
        case MSG_SCREEN_DRAFTS:
            if (Key == KEY_UP || Key == KEY_DOWN) list_move(Key == KEY_UP ? -1 : 1);
            else if (Key == KEY_MENU) {
                if (gMsgScreen == MSG_SCREEN_DRAFTS) open_draft_edit(gMsgCursor);
                else if (current_count() > 0) {
                    bool was_inbox = (gMsgScreen == MSG_SCREEN_INBOX);
                    gMsgReadSource = (uint8_t)gMsgScreen;
                    gMsgReadIndex = gMsgCursor;
                    gMsgScreen = MSG_SCREEN_READ;
                    if (was_inbox) MSG_STORE_MarkInboxRead(gMsgReadIndex);
                }
            } else if (Key == KEY_EXIT) go_home();
            else if (Key == KEY_F) { if (gMsgScreen == MSG_SCREEN_INBOX) MSG_STORE_DeleteInbox(gMsgCursor); else if (gMsgScreen == MSG_SCREEN_OUTBOX) MSG_STORE_DeleteOutbox(gMsgCursor); }
            break;

        case MSG_SCREEN_READ:
            if (Key == KEY_UP) {
                read_move(-1);
            } else if (Key == KEY_DOWN) {
                read_move(1);
            } else if (Key == KEY_MENU) {
                if (gMsgReadSource == MSG_SCREEN_OUTBOX) {
                    MSG_Message_t *m = read_message();
                    if (m) {
                        if (!MSG_RF_SendText(m->text)) MSG_STORE_AddOutboxDemo(m->text);
                    }
                    open_sent_after_send();
                } else {
                    open_compose("REPLY ");
                }
            } else if (Key == KEY_F) {
                if (gMsgReadSource == MSG_SCREEN_OUTBOX) {
                    MSG_STORE_DeleteOutbox(gMsgReadIndex);
                    open_list(MSG_SCREEN_OUTBOX);
                } else {
                    MSG_STORE_DeleteInbox(gMsgReadIndex);
                    open_list(MSG_SCREEN_INBOX);
                }
            } else if (Key == KEY_EXIT) {
                open_list((gMsgReadSource == MSG_SCREEN_OUTBOX) ? MSG_SCREEN_OUTBOX : MSG_SCREEN_INBOX);
            }
            break;

        case MSG_SCREEN_COMPOSE:
            if (Key == KEY_MENU) {
                MSG_T9_Commit(&gMsgEditor);
                if (gMsgComposeIsDraftEdit) {
                    /* Drafts are quick-message templates: MENU saves the edited
                     * draft persistently and immediately sends the same text.
                     * This avoids needing an extra key combination on UV-K1.
                     */
                    MSG_STORE_SetDraft(gMsgComposeDraftIndex, gMsgComposeBuf);
                    const char *send_text = gMsgComposeBuf[0] ? gMsgComposeBuf : "EMPTY";
                    if (!MSG_RF_SendText(send_text)) MSG_STORE_AddOutboxDemo(send_text);
                    open_sent_after_send();
                } else {
                    const char *send_text = gMsgComposeBuf[0] ? gMsgComposeBuf : "EMPTY";
                    if (!MSG_RF_SendText(send_text)) MSG_STORE_AddOutboxDemo(send_text);
                    open_sent_after_send();
                }
            }
            else if (Key == KEY_EXIT) { MSG_T9_Commit(&gMsgEditor); go_home(); }
            else MSG_T9_HandleKey(&gMsgEditor, Key);
            break;

        case MSG_SCREEN_CALLSIGN:
            /* Public 0.2.0: Messenger-local settings screen is hidden.
             * Callsign editing remains available from the main radio menu. */
            if (Key == KEY_EXIT || Key == KEY_MENU) go_home();
            break;

        case MSG_SCREEN_SETTINGS:
            /* Hidden in public builds; keep a safe escape in case stale state is entered. */
            go_home();
            break;
    }
    gUpdateDisplay = true;
}
