#include <string.h>
#include "app/messenger_t9.h"

#define MSG_T9_COMMIT_TICKS 80U

static const char *map_chars(KEY_Code_t key)
{
    switch (key) {
        case KEY_0: return " 0";
        case KEY_1: return ".,?!1";
        case KEY_2: return "ABC2";
        case KEY_3: return "DEF3";
        case KEY_4: return "GHI4";
        case KEY_5: return "JKL5";
        case KEY_6: return "MNO6";
        case KEY_7: return "PQRS7";
        case KEY_8: return "TUV8";
        case KEY_9: return "WXYZ9";
        default: return "";
    }
}

static char apply_case(char c, bool upper)
{
    if (!upper && c >= 'A' && c <= 'Z') c = (char)(c + ('a' - 'A'));
    return c;
}

void MSG_T9_Start(MSG_T9Editor_t *ed, char *buf, uint8_t max_len)
{
    ed->buffer = buf;
    ed->max_len = max_len;
    ed->len = strlen(buf);
    if (ed->len > max_len) ed->len = max_len;
    ed->upper = true;
    ed->mode = 0;
    ed->pending_key = KEY_INVALID;
    ed->cycle_index = 0;
    ed->pending_ticks = 0;
    ed->has_pending = false;
}

void MSG_T9_Commit(MSG_T9Editor_t *ed)
{
    if (!ed) return;
    ed->pending_key = KEY_INVALID;
    ed->cycle_index = 0;
    ed->pending_ticks = 0;
    ed->has_pending = false;
}

void MSG_T9_Tick(MSG_T9Editor_t *ed)
{
    if (!ed || !ed->has_pending) return;
    if (++ed->pending_ticks >= MSG_T9_COMMIT_TICKS) MSG_T9_Commit(ed);
}

bool MSG_T9_HandleKey(MSG_T9Editor_t *ed, KEY_Code_t key)
{
    if (!ed || !ed->buffer) return false;

    if (key == KEY_STAR) {
        MSG_T9_Commit(ed);
        ed->mode = (uint8_t)((ed->mode + 1U) % 3U);
        ed->upper = (ed->mode == 0U);
        return true;
    }

    if (key == KEY_F || key == KEY_EXIT) {
        if (ed->len > 0) ed->buffer[--ed->len] = 0;
        MSG_T9_Commit(ed);
        return true;
    }

    const char *chars = map_chars(key);
    uint8_t n = (uint8_t)strlen(chars);
    if (n == 0) return false;

    /* Numeric mode: STAR cycles B -> b -> 2.  In mode 2, each keypad
     * press inserts the digit immediately; no multi-tap pending state. */
    if (ed->mode == 2U) {
        MSG_T9_Commit(ed);
        if (ed->len >= ed->max_len) return true;
        if (key >= KEY_0 && key <= KEY_9) {
            ed->buffer[ed->len++] = (char)('0' + (uint8_t)key);
            ed->buffer[ed->len] = 0;
            return true;
        }
        return false;
    }

    if (ed->has_pending && ed->pending_key == key && ed->len > 0) {
        ed->cycle_index = (uint8_t)((ed->cycle_index + 1U) % n);
        ed->buffer[ed->len - 1U] = apply_case(chars[ed->cycle_index], ed->upper);
        ed->pending_ticks = 0;
        return true;
    }

    MSG_T9_Commit(ed);
    if (ed->len >= ed->max_len) return true;
    ed->cycle_index = 0;
    ed->pending_key = key;
    ed->has_pending = true;
    ed->pending_ticks = 0;
    ed->buffer[ed->len++] = apply_case(chars[0], ed->upper);
    ed->buffer[ed->len] = 0;
    return true;
}

bool MSG_T9_HandleLongKey(MSG_T9Editor_t *ed, KEY_Code_t key)
{
    if (!ed || !ed->buffer) return false;
    if (key < KEY_0 || key > KEY_9) return false;

    /* 0.2.2: while in B/b modes, long-pressing a numeric key inserts its
     * digit directly.  This preserves the existing B/b/2 modes but removes
     * the need to switch to numeric mode just to type one number. */
    MSG_T9_Commit(ed);
    if (ed->len >= ed->max_len) return true;
    ed->buffer[ed->len++] = (char)('0' + (uint8_t)key);
    ed->buffer[ed->len] = 0;
    return true;
}
