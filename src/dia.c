/*
  Copyright 2009-2010 volca
  Licenced under Academic Free License version 3.0
  Review OpenUsbLd README & LICENSE files for further details.
*/

#include "include/opl.h"
#include "include/dia.h"
#include "include/gui.h"
#include "include/lang.h"
#include "include/pad.h"
#include "include/renderman.h"
#include "include/fntsys.h"
#include "include/themes.h"
#include "include/textures.h"
#include "include/util.h"
#include "include/sound.h"

// UI spacing of the dialogues (pixels between consecutive items)
#define UI_SPACING_H      10
#define UI_SPACING_V      2
// spacer ui element width (simulates tab)
#define UI_SPACER_WIDTH   50
// minimal pixel width of spacer
#define UI_SPACER_MINIMAL 30
// length of breaking line in pixels
#define UI_BREAK_LEN      600
// scroll speed (delay in ms!) when in dialogs
#define DIA_SCROLL_SPEED  300
// scroll speed (delay in ms!) when setting int value
#define DIA_INT_SET_SPEED 100
#define DIA_PS5_IP_SET_SPEED_SLOW   120
#define DIA_PS5_IP_SET_SPEED_MEDIUM 70
#define DIA_PS5_IP_SET_SPEED_FAST   35

static int screenWidth;
static int screenHeight;

extern void *focus_png;
extern void *roboto_bold_raw;
extern int size_roboto_bold_raw;
static GSTEXTURE gDiaPS5FocusTex;
static int gDiaPS5FocusTexLoaded = 0;
static int gDiaPS5IpFont = -1;

// Utility stuff
#define KEYB_MODE   2
#define KEYB_WIDTH  12
#define KEYB_HEIGHT 4
#define KEYB_ITEMS  (KEYB_WIDTH * KEYB_HEIGHT)

static void diaDrawBoundingBox(int x, int y, int w, int h, int focus)
{
    u64 color = focus ? gTheme->selTextColor : gTheme->textColor;

    color |= GS_SETREG_RGBA(0, 0, 0, 0xFF);
    color &= gColFocus;

    rmDrawRect(x - 5, y, w + 10, h + 10, color);
}

static void diaUpdateMask(char *mask, int maxLen, int len)
{
    if (mask != NULL) {
        int maskLen = len < maxLen ? len : maxLen - 1;
        memset(mask, '*', maskLen);
        mask[maskLen] = '\0';
    }
}

typedef struct
{
    int x;
    int y;
    int w;
    int h;
    int row;
    char ch;
    const char *label;
    int action;
} ps5_key_t;

enum {
    PS5_KEY_CHAR = 0,
    PS5_KEY_BACKSPACE,
    PS5_KEY_SPACE,
    PS5_KEY_DONE,
    PS5_KEY_CAPS
};

static int diaFindNearestKey(ps5_key_t *keys, int count, int current, int direction)
{
    int i;
    int best = current;
    int bestScore = 0x7FFFFFFF;
    int cx = keys[current].x + keys[current].w / 2;
    int cy = keys[current].y + keys[current].h / 2;

    if (direction == KEY_LEFT || direction == KEY_RIGHT) {
        for (i = 0; i < count; i++) {
            int ix, dx;
            if (i == current || keys[i].row != keys[current].row)
                continue;

            ix = keys[i].x + keys[i].w / 2;
            dx = ix - cx;
            if ((direction == KEY_LEFT && dx >= 0) || (direction == KEY_RIGHT && dx <= 0))
                continue;
            if (abs(dx) < bestScore) {
                bestScore = abs(dx);
                best = i;
            }
        }

        if (best != current)
            return best;

        for (i = 0; i < count; i++) {
            int ix;
            if (i == current || keys[i].row != keys[current].row)
                continue;

            ix = keys[i].x + keys[i].w / 2;
            if (best == current ||
                (direction == KEY_RIGHT && ix < (keys[best].x + keys[best].w / 2)) ||
                (direction == KEY_LEFT && ix > (keys[best].x + keys[best].w / 2))) {
                best = i;
            }
        }

        return best;
    }

    for (i = 0; i < count; i++) {
        int ix, iy, dx, dy, score;
        if (i == current)
            continue;

        ix = keys[i].x + keys[i].w / 2;
        iy = keys[i].y + keys[i].h / 2;
        dx = ix - cx;
        dy = iy - cy;

        if ((direction == KEY_LEFT && dx >= 0) || (direction == KEY_RIGHT && dx <= 0) ||
            (direction == KEY_UP && dy >= 0) || (direction == KEY_DOWN && dy <= 0))
            continue;

        if (direction == KEY_LEFT || direction == KEY_RIGHT)
            score = (abs(dx) * 8) + abs(dy);
        else
            score = (abs(dy) * 8) + abs(dx);

        if (score < bestScore) {
            bestScore = score;
            best = i;
        }
    }

    return best;
}

static void diaDrawPS5Triangle(int cx, int cy, int size, int up, u64 color)
{
    int row;

    for (row = 0; row < size; row++) {
        int width = (row * 2) + 1;
        int y = up ? cy + row : cy - row;
        rmDrawRect(cx - row, y, width, 1, color);
    }
}

static void diaDrawPS5FocusArrow(int cx, int cy, int size, int up)
{
    GSTEXTURE *focus = thmGetTexture(FOCUS_ICON);
    float angle = up ? -1.5707963f : 1.5707963f;

    if ((focus == NULL || focus->Mem == NULL) && !gDiaPS5FocusTexLoaded) {
        memset(&gDiaPS5FocusTex, 0, sizeof(GSTEXTURE));
        gDiaPS5FocusTexLoaded = texLoadMem(&gDiaPS5FocusTex, &focus_png) >= 0 ? 1 : -1;
    }
    if (focus == NULL || focus->Mem == NULL)
        focus = gDiaPS5FocusTexLoaded == 1 ? &gDiaPS5FocusTex : NULL;

    if (focus != NULL && focus->Mem != NULL)
        rmDrawRotatedPixmap(focus, cx, cy, size, size, angle, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
    else
        diaDrawPS5Triangle(cx, cy - (up ? 0 : size / 2), size / 2, up, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
}

static void diaDrawPS5FooterIconText(int iconId, const char *text, int x, int y, int font)
{
    GSTEXTURE *icon = thmGetTexture(iconId);
    int iconSize = 14;
    int gap = 6;

    if (icon != NULL && icon->Mem != NULL)
        rmDrawPixmap(icon, x, y, ALIGN_LEFT | ALIGN_VCENTER, iconSize, iconSize, 1, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
    fntRenderString(font, x + iconSize + gap, y, ALIGN_LEFT | ALIGN_VCENTER, 0, 0, text, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
}

static int diaParseIp(const char *text, int *parts)
{
    int a = 192, b = 168, c = 0, d = 0;

    if (text == NULL || sscanf(text, "%d.%d.%d.%d", &a, &b, &c, &d) != 4) {
        a = 192;
        b = 168;
        c = 0;
        d = 0;
    }

    parts[0] = a < 0 ? 0 : (a > 255 ? 255 : a);
    parts[1] = b < 0 ? 0 : (b > 255 ? 255 : b);
    parts[2] = c < 0 ? 0 : (c > 255 ? 255 : c);
    parts[3] = d < 0 ? 0 : (d > 255 ? 255 : d);
    return 0;
}

static int diaNormalizeIpText(const char *input, char *output, int maxLen)
{
    int parts[4] = {0, 0, 0, 0};
    int part = 0;
    int value = -1;
    const char *p;

    if (input == NULL || output == NULL || maxLen <= 0)
        return 0;

    for (p = input; *p != '\0'; p++) {
        if (*p >= '0' && *p <= '9') {
            if (value < 0)
                value = 0;
            value = (value * 10) + (*p - '0');
            if (value > 255)
                value = 255;
        } else if (*p == '.') {
            if (part >= 4 || value < 0)
                return 0;
            parts[part++] = value;
            value = -1;
        } else {
            return 0;
        }
    }

    if (part != 3 || value < 0)
        return 0;

    parts[part] = value;
    snprintf(output, maxLen, "%d.%d.%d.%d", parts[0], parts[1], parts[2], parts[3]);
    return 1;
}

int diaShowIpEditor(char *text, int maxLen)
{
    int parts[4];
    int selected = 2;
    int pressAnim = 0;
    int pressDir = 0;
    int ipPadSettings[16];
    int ipHoldDirection = 0;
    clock_t ipHoldStarted = 0;

    if (!gPS5Mode)
        return diaShowKeyb(text, maxLen, 0, "SMB Server IP");

    padStoreSettings(ipPadSettings);
    setButtonDelay(KEY_LEFT, DIA_PS5_IP_SET_SPEED_SLOW);
    setButtonDelay(KEY_RIGHT, DIA_PS5_IP_SET_SPEED_SLOW);
    setButtonDelay(KEY_UP, DIA_PS5_IP_SET_SPEED_SLOW);
    setButtonDelay(KEY_DOWN, DIA_PS5_IP_SET_SPEED_SLOW);

    diaParseIp(text, parts);
    rmGetScreenExtents(&screenWidth, &screenHeight);
    if (gDiaPS5IpFont < 0)
        gDiaPS5IpFont = fntLoadFileMem(&roboto_bold_raw, size_roboto_bold_raw, 48);

    while (1) {
        int font = gDiaPS5IpFont >= 0 ? gDiaPS5IpFont : thmGetPS5TitleFont();
        int footerFont = thmGetPS5SemiBoldFont();
        int centerY = screenHeight / 2;
        int arrowSize = 27;
        int tapOffset = pressAnim > 0 ? (pressAnim * 2) : 0;
        int arrowX;
        int segCenter[4];
        int segW[4];
        int i;
        char values[4][8];
        int dotW;
        int dotPad = 0;  // pixels of padding on each side of the dot
        int numPad = 0;  // minimal padding around each number
        int totalW;
        int curX;

        // Measure the dot width
        dotW = rmUnScaleX(fntCalcDimensions(font, "."));

        // Format and measure each octet
        for (i = 0; i < 4; i++) {
            snprintf(values[i], sizeof(values[i]), "%d", parts[i]);
            segW[i] = rmUnScaleX(fntCalcDimensions(font, values[i])) + numPad * 2;
        }

        // Calculate total width: all segments + dots between them
        totalW = 0;
        for (i = 0; i < 4; i++)
            totalW += segW[i];
        totalW += 3 * (dotW + dotPad * 2); // 3 dots with padding

        curX = (screenWidth - totalW) / 2;

        // Calculate center positions for each segment and dot
        for (i = 0; i < 4; i++) {
            segCenter[i] = curX + segW[i] / 2;
            curX += segW[i];
            if (i < 3) {
                curX += dotPad;
                curX += dotW + dotPad;
            }
        }

        readPads();

        {
            int heldDirection = getKeyPressed(KEY_UP) ? -1 : (getKeyPressed(KEY_DOWN) ? 1 : 0);
            int repeatDelay = DIA_PS5_IP_SET_SPEED_SLOW;

            if (heldDirection == 0) {
                ipHoldDirection = 0;
                ipHoldStarted = 0;
            } else {
                clock_t now = clock();
                clock_t heldFor;

                if (heldDirection != ipHoldDirection) {
                    ipHoldDirection = heldDirection;
                    ipHoldStarted = now;
                }

                heldFor = now - ipHoldStarted;
                if (heldFor >= 6 * CLOCKS_PER_SEC)
                    repeatDelay = DIA_PS5_IP_SET_SPEED_FAST;
                else if (heldFor >= 3 * CLOCKS_PER_SEC)
                    repeatDelay = DIA_PS5_IP_SET_SPEED_MEDIUM;
            }

            setButtonDelay(KEY_UP, repeatDelay);
            setButtonDelay(KEY_DOWN, repeatDelay);
        }

        rmStartFrame();
        rmDrawRect(0, 0, screenWidth, screenHeight, GS_SETREG_RGBA(0, 0, 0, 0x80));

        arrowX = segCenter[selected];
        diaDrawPS5FocusArrow(arrowX, centerY - 32 + (pressDir < 0 ? tapOffset : 0), arrowSize, 1);
        diaDrawPS5FocusArrow(arrowX, centerY + 38 - (pressDir > 0 ? tapOffset : 0), arrowSize, 0);

        // Re-walk the layout to draw numbers and dots
        curX = (screenWidth - totalW) / 2;
        for (i = 0; i < 4; i++) {
            int cx = curX + segW[i] / 2;
            fntRenderString(font, cx, centerY, ALIGN_HCENTER | ALIGN_VCENTER, 0, 0, values[i], GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
            curX += segW[i];
            if (i < 3) {
                curX += dotPad;
                int dotX = curX + dotW / 2;
                fntRenderString(font, dotX, centerY + 6, ALIGN_HCENTER | ALIGN_VCENTER, 0, 0, ".", GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
                curX += dotW + dotPad;
            }
        }

        diaDrawPS5FooterIconText(TRIANGLE_ICON, "Show Keyboard", screenWidth - 360, screenHeight - 20, footerFont);
        diaDrawPS5FooterIconText(SQUARE_ICON, "Save", screenWidth - 206, screenHeight - 20, footerFont);
        diaDrawPS5FooterIconText(CIRCLE_ICON, "Cancel", screenWidth - 106, screenHeight - 20, footerFont);

        rmEndFrame();
        if (pressAnim > 0)
            pressAnim--;

        if (getKey(KEY_LEFT)) {
            sfxPlay(SFX_CURSOR);
            selected = selected > 0 ? selected - 1 : 3;
        } else if (getKey(KEY_RIGHT)) {
            sfxPlay(SFX_CURSOR);
            selected = selected < 3 ? selected + 1 : 0;
        } else if (getKey(KEY_UP)) {
            sfxPlay(SFX_CURSOR);
            if (parts[selected] < 255) {
                parts[selected]++;
                pressAnim = 4;
                pressDir = -1;
            }
        } else if (getKey(KEY_DOWN)) {
            sfxPlay(SFX_CURSOR);
            if (parts[selected] > 0) {
                parts[selected]--;
                pressAnim = 4;
                pressDir = 1;
            }
        } else if (getKeyOn(KEY_SQUARE)) {
            sfxPlay(SFX_CONFIRM);
            snprintf(text, maxLen, "%d.%d.%d.%d", parts[0], parts[1], parts[2], parts[3]);
            padRestoreSettings(ipPadSettings);
            return 1;
        } else if (getKeyOn(KEY_TRIANGLE)) {
            char tmp[32];
            char normalized[16];

            sfxPlay(SFX_CONFIRM);
            snprintf(tmp, sizeof(tmp), "%d.%d.%d.%d", parts[0], parts[1], parts[2], parts[3]);
            if (diaShowKeyb(tmp, sizeof(tmp), 0, "SMB Server IP")) {
                if (diaNormalizeIpText(tmp, normalized, sizeof(normalized))) {
                    strncpy(text, normalized, maxLen - 1);
                    text[maxLen - 1] = '\0';
                } else {
                    snprintf(text, maxLen, "%d.%d.%d.%d", parts[0], parts[1], parts[2], parts[3]);
                }
                padRestoreSettings(ipPadSettings);
                return 1;
            }
        } else if (getKeyOn(KEY_CIRCLE)) {
            sfxPlay(SFX_CANCEL);
            padRestoreSettings(ipPadSettings);
            return 0;
        }
    }
}

static int diaShowPS5Keyb(char *text, int maxLen, int hide_text, const char *title)
{
    int shift = 0;
    int len = strlen(text);
    int selected = 0;
    char *mask = NULL;

    if (hide_text) {
        mask = malloc(maxLen);
        if (mask == NULL)
            return 0;
        diaUpdateMask(mask, maxLen, len);
    }

    rmGetScreenExtents(&screenWidth, &screenHeight);

    while (1) {
        ps5_key_t keys[80];
        int keyCount = 0;
        int i, keyW, keyH, gap, startX, titleY, inputY, startY, footerY;
        int unitW, wideW, doneX, spaceX, spaceW;
        const char *display = hide_text ? mask : text;
        int titleFont = thmGetPS5TitleFont();
        int semiFont = thmGetPS5SemiBoldFont();
        int headerFont = thmGetPS5HeaderFont();
        GSTEXTURE *circleIcon;

        readPads();

        rmStartFrame();
        rmDrawRect(0, 0, screenWidth, screenHeight, GS_SETREG_RGBA(0, 0, 0, 0x80));

        startX = screenWidth * 75 / 1000;
        gap = screenWidth >= 960 ? screenWidth * 5 / 1920 : 2;
        if (gap < 2)
            gap = 2;
        keyW = (screenWidth - (startX * 2) - (gap * 15)) / 16;
        keyH = keyW;
        unitW = keyW + gap;
        wideW = (keyW * 2) + gap;
        titleY = screenHeight * 62 / 1000;
        inputY = screenHeight * 258 / 1000;
        startY = screenHeight * 327 / 1000;
        footerY = screenHeight - 20;

#define ADD_KEY(_row, _x, _y, _w, _label, _action, _ch) \
        do { \
            keys[keyCount].x = (_x); \
            keys[keyCount].y = (_y); \
            keys[keyCount].w = (_w); \
            keys[keyCount].h = keyH; \
            keys[keyCount].row = (_row); \
            keys[keyCount].label = (_label); \
            keys[keyCount].action = (_action); \
            keys[keyCount].ch = (_ch); \
            keyCount++; \
        } while (0)

#define KEY_X(_col) (startX + (_col) * unitW)
#define KEY_Y(_row) (startY + (_row) * (keyH + gap))
#define KEY_W(_cols) ((keyW * (_cols)) + (gap * ((_cols) - 1)))

        for (i = 0; i < 10; i++) {
            static const char row0[] = "1234567890";
            ADD_KEY(0, KEY_X(i), KEY_Y(0), keyW, NULL, PS5_KEY_CHAR, row0[i]);
        }
        ADD_KEY(0, KEY_X(10), KEY_Y(0), keyW, NULL, PS5_KEY_CHAR, '+');
        ADD_KEY(0, KEY_X(11), KEY_Y(0), KEY_W(5), "Backspace", PS5_KEY_BACKSPACE, 0);

        for (i = 0; i < 8; i++) {
            static const char row1[] = "!@#$%^&(";
            ADD_KEY(1, KEY_X(i), KEY_Y(1), keyW, NULL, PS5_KEY_CHAR, row1[i]);
        }
        ADD_KEY(1, KEY_X(8), KEY_Y(1), KEY_W(2), ")", PS5_KEY_CHAR, ')');
        ADD_KEY(1, KEY_X(10), KEY_Y(1), keyW, NULL, PS5_KEY_CHAR, '-');
        ADD_KEY(1, KEY_X(11), KEY_Y(1), KEY_W(2), "_", PS5_KEY_CHAR, '_');
        ADD_KEY(1, KEY_X(13), KEY_Y(1), keyW, NULL, PS5_KEY_CHAR, '{');
        ADD_KEY(1, KEY_X(14), KEY_Y(1), keyW, NULL, PS5_KEY_CHAR, '}');
        ADD_KEY(1, KEY_X(15), KEY_Y(1), keyW, NULL, PS5_KEY_CHAR, '/');

        ADD_KEY(2, KEY_X(0), KEY_Y(2), KEY_W(2), "?", PS5_KEY_CHAR, '?');
        for (i = 0; i < 10; i++) {
            static const char row2[] = "qwertyuiop";
            ADD_KEY(2, KEY_X(i + 2), KEY_Y(2), keyW, NULL, PS5_KEY_CHAR, row2[i]);
        }
        ADD_KEY(2, KEY_X(12), KEY_Y(2), keyW, NULL, PS5_KEY_CHAR, '[');
        ADD_KEY(2, KEY_X(13), KEY_Y(2), keyW, NULL, PS5_KEY_CHAR, ']');
        ADD_KEY(2, KEY_X(14), KEY_Y(2), keyW, NULL, PS5_KEY_CHAR, '\\');
        ADD_KEY(2, KEY_X(15), KEY_Y(2), keyW, "\"", PS5_KEY_CHAR, '"');

        ADD_KEY(3, KEY_X(0), KEY_Y(3), keyW, NULL, PS5_KEY_CHAR, '<');
        ADD_KEY(3, KEY_X(1), KEY_Y(3), keyW, NULL, PS5_KEY_CHAR, '>');
        for (i = 0; i < 9; i++) {
            static const char row3[] = "asdfghjkl";
            ADD_KEY(3, KEY_X(i + 2), KEY_Y(3), keyW, NULL, PS5_KEY_CHAR, row3[i]);
        }
        ADD_KEY(3, KEY_X(11), KEY_Y(3), keyW, NULL, PS5_KEY_CHAR, ':');
        ADD_KEY(3, KEY_X(12), KEY_Y(3), KEY_W(2), ";", PS5_KEY_CHAR, ';');
        ADD_KEY(3, KEY_X(14), KEY_Y(3), keyW, NULL, PS5_KEY_CHAR, ',');
        ADD_KEY(3, KEY_X(15), KEY_Y(3), keyW, NULL, PS5_KEY_CHAR, '.');

        ADD_KEY(4, KEY_X(0), KEY_Y(4), KEY_W(2), "Caps", PS5_KEY_CAPS, 0);

        for (i = 0; i < 7; i++) {
            static const char row3[] = "zxcvbnm";
            ADD_KEY(4, KEY_X(i + 2), KEY_Y(4), keyW, NULL, PS5_KEY_CHAR, row3[i]);
        }

        doneX = KEY_X(14);
        spaceX = KEY_X(9);
        spaceW = doneX - gap - spaceX;
        ADD_KEY(4, spaceX, KEY_Y(4), spaceW, "Spacebar", PS5_KEY_SPACE, 0);
        ADD_KEY(4, doneX, KEY_Y(4), wideW, "Done", PS5_KEY_DONE, 0);

#undef KEY_W
#undef KEY_Y
#undef KEY_X
#undef ADD_KEY

        if (selected >= keyCount)
            selected = keyCount - 1;

        fntRenderString(titleFont, startX, titleY, ALIGN_LEFT, 0, 0, title != NULL ? title : "Keyboard", GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
        fntRenderString(headerFont, startX + keyW / 25, inputY, ALIGN_LEFT, 0, 0, display, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));

        for (i = 0; i < keyCount; i++) {
            int focused = i == selected;
            char label[16];
            const char *displayLabel = keys[i].label;
            u64 keyColor = focused ? GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80) : GS_SETREG_RGBA(0x1C, 0x1C, 0x1C, 0x80);
            u64 textColor = focused ? GS_SETREG_RGBA(0, 0, 0, 0x80) : GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80);

            if (displayLabel == NULL) {
                label[0] = keys[i].ch;
                if (shift && label[0] >= 'a' && label[0] <= 'z')
                    label[0] -= 32;
                label[1] = '\0';
                displayLabel = label;
            }

            rmDrawRoundedRect(keys[i].x, keys[i].y, keys[i].w, keys[i].h, 2, keyColor);
            fntRenderString(semiFont, keys[i].x + keys[i].w / 2, keys[i].y + keys[i].h / 2, ALIGN_CENTER | ALIGN_VCENTER, 0, 0, displayLabel, textColor);
        }

        circleIcon = thmGetTexture(CIRCLE_ICON);
        if (circleIcon && circleIcon->Mem)
            rmDrawPixmap(circleIcon, screenWidth - startX - 52, footerY, ALIGN_LEFT | ALIGN_VCENTER, 14, 14, 1, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
        fntRenderString(semiFont, screenWidth - startX - 32, footerY, ALIGN_LEFT | ALIGN_VCENTER, 0, 0, "Cancel", GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));

        rmEndFrame();

        if (getKey(KEY_LEFT)) {
            int next = diaFindNearestKey(keys, keyCount, selected, KEY_LEFT);
            sfxPlay(SFX_CURSOR);
            selected = next != selected ? next : selected;
        } else if (getKey(KEY_RIGHT)) {
            int next = diaFindNearestKey(keys, keyCount, selected, KEY_RIGHT);
            sfxPlay(SFX_CURSOR);
            selected = next != selected ? next : selected;
        } else if (getKey(KEY_UP)) {
            int next = diaFindNearestKey(keys, keyCount, selected, KEY_UP);
            sfxPlay(SFX_CURSOR);
            selected = next != selected ? next : selected;
        } else if (getKey(KEY_DOWN)) {
            int next = diaFindNearestKey(keys, keyCount, selected, KEY_DOWN);
            sfxPlay(SFX_CURSOR);
            selected = next != selected ? next : selected;
        } else if (getKeyOn(gSelectButton)) {
            if (keys[selected].action == PS5_KEY_CHAR) {
                if (len < maxLen - 1) {
                    char c = keys[selected].ch;
                    if (shift && c >= 'a' && c <= 'z')
                        c -= 32;
                    text[len++] = c;
                    text[len] = '\0';
                    diaUpdateMask(mask, maxLen, len);
                    sfxPlay(SFX_CONFIRM);
                }
            } else if (keys[selected].action == PS5_KEY_BACKSPACE) {
                if (len > 0) {
                    text[--len] = '\0';
                    diaUpdateMask(mask, maxLen, len);
                }
                sfxPlay(SFX_CANCEL);
            } else if (keys[selected].action == PS5_KEY_SPACE) {
                if (len < maxLen - 1) {
                    text[len++] = ' ';
                    text[len] = '\0';
                    diaUpdateMask(mask, maxLen, len);
                }
                sfxPlay(SFX_CONFIRM);
            } else if (keys[selected].action == PS5_KEY_DONE) {
                sfxPlay(SFX_CONFIRM);
                if (mask != NULL)
                    free(mask);
                return 1;
            } else if (keys[selected].action == PS5_KEY_CAPS) {
                shift = !shift;
                sfxPlay(SFX_CONFIRM);
            }
        } else if (getKey(KEY_SQUARE)) {
            if (len > 0) {
                text[--len] = '\0';
                diaUpdateMask(mask, maxLen, len);
            }
            sfxPlay(SFX_CANCEL);
        } else if (getKey(KEY_TRIANGLE)) {
            if (len < maxLen - 1) {
                text[len++] = ' ';
                text[len] = '\0';
                diaUpdateMask(mask, maxLen, len);
            }
            sfxPlay(SFX_CONFIRM);
        } else if (getKeyOn(KEY_SELECT)) {
            shift = !shift;
            sfxPlay(SFX_CONFIRM);
        } else if (getKeyOn(KEY_START)) {
            sfxPlay(SFX_CONFIRM);
            if (mask != NULL)
                free(mask);
            return 1;
        } else if (getKey(gSelectButton == KEY_CIRCLE ? KEY_CROSS : KEY_CIRCLE)) {
            sfxPlay(SFX_CANCEL);
            break;
        }
    }

    if (mask != NULL)
        free(mask);

    return 0;
}

int diaShowKeyb(char *text, int maxLen, int hide_text, const char *title)
{
    int i, j, len = strlen(text), selkeyb = 0, x, w;
    int selchar = 0, selcommand = -1;
    char c[2] = "\0\0", *mask_buffer;
    static const char keyb0[KEYB_ITEMS] = {
        '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=',
        'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']',
        'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '\\',
        'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', '`', ':'};

    static const char keyb1[KEYB_ITEMS] = {
        '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+',
        'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}',
        'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '|',
        'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', '~', '.'};
    const char *keyb = keyb0;

    char *commands[KEYB_HEIGHT] = {_l(_STR_BACKSPACE), _l(_STR_SPACE), _l(_STR_ENTER), _l(_STR_MODE)};
    GSTEXTURE *cmdicons[KEYB_HEIGHT];
    cmdicons[0] = thmGetTexture(SQUARE_ICON);
    cmdicons[1] = thmGetTexture(TRIANGLE_ICON);
    cmdicons[2] = thmGetTexture(START_ICON);
    cmdicons[3] = thmGetTexture(SELECT_ICON);

    if (gPS5Mode)
        return diaShowPS5Keyb(text, maxLen, hide_text, title);

    rmGetScreenExtents(&screenWidth, &screenHeight);

    if (hide_text) {
        if ((mask_buffer = malloc(maxLen)) != NULL) {
            memset(mask_buffer, '*', len);
            mask_buffer[len] = '\0';
        }
    } else {
        mask_buffer = NULL;
    }

    while (1) {
        readPads();

        rmStartFrame();
        guiDrawBGPlasma();
        rmDrawRect(0, 0, screenWidth, screenHeight, gColDarker);

        // Title
        if (title != NULL) {
            fntRenderString(gTheme->fonts[0], 25, 20, ALIGN_NONE, 0, 0, title, gTheme->textColor);
            // separating line
            rmDrawLine(25, 38, 615, 38, gColWhite);
        }

        // Text
        fntRenderString(gTheme->fonts[0], 50, 120, ALIGN_NONE, 0, 0, hide_text ? mask_buffer : text, gTheme->textColor);

        // separating line for simpler orientation
        rmDrawLine(25, 138, 615, 138, gColWhite);

        for (j = 0; j < KEYB_HEIGHT; j++) {
            for (i = 0; i < KEYB_WIDTH; i++) {
                c[0] = keyb[i + j * KEYB_WIDTH];

                x = 50 + i * 31;
                w = fntRenderString(gTheme->fonts[0], x, 170 + 3 * UI_SPACING_H * j, ALIGN_NONE, 0, 0, c, gTheme->uiTextColor) - x;
                if ((i + j * KEYB_WIDTH) == selchar)
                    diaDrawBoundingBox(x, 170 + 3 * UI_SPACING_H * j, w, UI_SPACING_H, 0);
            }
        }

        // Commands
        for (i = 0; i < KEYB_HEIGHT; i++) {
            if (cmdicons[i]) {
                int w = (cmdicons[i]->Width * 20) / cmdicons[i]->Height;
                int h = 20;
                rmDrawPixmap(cmdicons[i], 436, 170 + 3 * UI_SPACING_H * i, ALIGN_NONE, w, h, SCALING_RATIO, gDefaultCol);
            }

            x = 477;
            w = fntRenderString(gTheme->fonts[0], x, 170 + 3 * UI_SPACING_H * i, ALIGN_NONE, 0, 0, commands[i], gTheme->uiTextColor) - x;
            if (i == selcommand)
                diaDrawBoundingBox(x, 170 + 3 * UI_SPACING_H * i, w, UI_SPACING_H, 0);
        }

        guiDrawIconAndText(gSelectButton == KEY_CIRCLE ? CROSS_ICON : CIRCLE_ICON, _STR_CANCEL, gTheme->fonts[0], 500, 417, gTheme->selTextColor);

        rmEndFrame();

        if (getKey(KEY_LEFT)) {
            sfxPlay(SFX_CURSOR);
            if (selchar > -1) {
                if (selchar % KEYB_WIDTH)
                    selchar--;
                else {
                    selcommand = selchar / KEYB_WIDTH;
                    selchar = -1;
                }
            } else {
                selchar = (selcommand + 1) * KEYB_WIDTH - 1;
                selcommand = -1;
            }
        } else if (getKey(KEY_RIGHT)) {
            sfxPlay(SFX_CURSOR);
            if (selchar > -1) {
                if ((selchar + 1) % KEYB_WIDTH)
                    selchar++;
                else {
                    selcommand = selchar / KEYB_WIDTH;
                    selchar = -1;
                }
            } else {
                selchar = selcommand * KEYB_WIDTH;
                selcommand = -1;
            }
        } else if (getKey(KEY_UP)) {
            sfxPlay(SFX_CURSOR);
            if (selchar > -1)
                selchar = (selchar + KEYB_ITEMS - KEYB_WIDTH) % KEYB_ITEMS;
            else
                selcommand = (selcommand + KEYB_HEIGHT - 1) % KEYB_HEIGHT;
        } else if (getKey(KEY_DOWN)) {
            sfxPlay(SFX_CURSOR);
            if (selchar > -1)
                selchar = (selchar + KEYB_WIDTH) % KEYB_ITEMS;
            else
                selcommand = (selcommand + 1) % KEYB_HEIGHT;
        } else if (getKeyOn(gSelectButton)) {
            if (len < (maxLen - 1) && selchar > -1) {
                sfxPlay(SFX_CONFIRM);
                if (mask_buffer != NULL) {
                    mask_buffer[len] = '*';
                    mask_buffer[len + 1] = '\0';
                }

                len++;
                c[0] = keyb[selchar];
                strcat(text, c);
            } else if (selcommand == 0) {
                sfxPlay(SFX_CANCEL);
                if (len > 0) { // BACKSPACE
                    len--;
                    text[len] = 0;
                    if (mask_buffer != NULL)
                        mask_buffer[len] = '\0';
                }
            } else if (selcommand == 1) {
                sfxPlay(SFX_CONFIRM);
                if (len < (maxLen - 1)) { // SPACE
                    if (mask_buffer != NULL) {
                        mask_buffer[len] = '*';
                        mask_buffer[len + 1] = '\0';
                    }

                    len++;
                    c[0] = ' ';
                    strcat(text, c);
                }
            } else if (selcommand == 2) {
                sfxPlay(SFX_CONFIRM);
                if (mask_buffer != NULL)
                    free(mask_buffer);
                return 1; // ENTER
            } else if (selcommand == 3) {
                sfxPlay(SFX_CONFIRM);
                selkeyb = (selkeyb + 1) % KEYB_MODE; // MODE
                if (selkeyb == 0)
                    keyb = keyb0;
                if (selkeyb == 1)
                    keyb = keyb1;
            }
        } else if (getKey(KEY_SQUARE)) {
            if (len > 0) { // BACKSPACE
                sfxPlay(SFX_CANCEL);
                len--;
                text[len] = 0;
                if (mask_buffer != NULL)
                    mask_buffer[len] = '\0';
            }
        } else if (getKey(KEY_TRIANGLE)) {
            if (len < (maxLen - 1) && selchar > -1) { // SPACE
                sfxPlay(SFX_CONFIRM);
                if (mask_buffer != NULL) {
                    mask_buffer[len] = '*';
                    mask_buffer[len + 1] = '\0';
                }

                len++;
                c[0] = ' ';
                strcat(text, c);
            }
        } else if (getKeyOn(KEY_START)) {
            sfxPlay(SFX_CONFIRM);
            if (mask_buffer != NULL)
                free(mask_buffer);
            return 1; // ENTER
        } else if (getKeyOn(KEY_SELECT)) {
            selkeyb = (selkeyb + 1) % KEYB_MODE; // MODE
            sfxPlay(SFX_CONFIRM);
            if (selkeyb == 0)
                keyb = keyb0;
            if (selkeyb == 1)
                keyb = keyb1;
        }

        if (getKey(gSelectButton == KEY_CIRCLE ? KEY_CROSS : KEY_CIRCLE)) {
            sfxPlay(SFX_CANCEL);
            break;
        }
    }


    if (mask_buffer != NULL)
        free(mask_buffer);

    return 0;
}

static int colPadSettings[16];

static int diaShowColSel(unsigned char *r, unsigned char *g, unsigned char *b)
{
    int selc = 0;
    int ret = 0;
    unsigned char col[3];

    padStoreSettings(colPadSettings);

    col[0] = *r;
    col[1] = *g;
    col[2] = *b;
    setButtonDelay(KEY_LEFT, 1);
    setButtonDelay(KEY_RIGHT, 1);

    while (1) {
        readPads();

        rmStartFrame();
        guiDrawBGPlasma();
        rmDrawRect(0, 0, screenWidth, screenHeight, gColDarker);

        // "Color selection"
        fntRenderString(gTheme->fonts[0], 50, 50, ALIGN_NONE, 0, 0, _l(_STR_COLOR_SELECTION), GS_SETREG_RGBA(0x60, 0x60, 0x60, 0x80));

        // 3 bars representing the colors...
        size_t co;
        int x, y;

        for (co = 0; co < 3; ++co) {
            unsigned char cc[3] = {0, 0, 0};
            cc[co] = col[co];

            x = 75;
            y = 75 + co * 25;

            u64 dcol = GS_SETREG_RGBA(cc[0], cc[1], cc[2], 0x80);

            if (selc == co)
                rmDrawRect(x, y, 200, 20, GS_SETREG_RGBA(0x60, 0x60, 0x60, 0x80));
            else
                rmDrawRect(x, y, 200, 20, GS_SETREG_RGBA(0x20, 0x20, 0x20, 0x80));

            rmDrawRect(x + 2, y + 2, 190.0f * (cc[co] * 100 / 255) / 100, 16, dcol);
        }

        // target color itself
        u64 dcol = GS_SETREG_RGBA(col[0], col[1], col[2], 0x80);

        x = 300;
        y = 75;

        rmDrawRect(x, y, 70, 70, GS_SETREG_RGBA(0x60, 0x60, 0x60, 0x80));
        rmDrawRect(x + 5, y + 5, 60, 60, dcol);

        guiDrawIconAndText(gSelectButton == KEY_CIRCLE ? CIRCLE_ICON : CROSS_ICON, _STR_OK, gTheme->fonts[0], 420, 417, gTheme->selTextColor);
        guiDrawIconAndText(gSelectButton == KEY_CIRCLE ? CROSS_ICON : CIRCLE_ICON, _STR_CANCEL, gTheme->fonts[0], 500, 417, gTheme->selTextColor);

        rmEndFrame();

        if (getKey(KEY_LEFT)) {
            if (col[selc] > 0) {
                col[selc]--;
                sfxPlay(SFX_CURSOR);
            }
        } else if (getKey(KEY_RIGHT)) {
            if (col[selc] < 255) {
                col[selc]++;
                sfxPlay(SFX_CURSOR);
            }
        } else if (getKey(KEY_UP)) {
            if (selc > 0) {
                selc--;
                sfxPlay(SFX_CURSOR);
            }
        } else if (getKey(KEY_DOWN)) {
            if (selc < 2) {
                selc++;
                sfxPlay(SFX_CURSOR);
            }
        } else if (getKeyOn(gSelectButton)) {
            sfxPlay(SFX_CONFIRM);
            *r = col[0];
            *g = col[1];
            *b = col[2];
            ret = 1;
            break;
        } else if (getKeyOn(gSelectButton == KEY_CIRCLE ? KEY_CROSS : KEY_CIRCLE)) {
            sfxPlay(SFX_CANCEL);
            ret = 0;
            break;
        }
    }

    padRestoreSettings(colPadSettings);
    return ret;
}



// ----------------------------------------------------------------------------
// --------------------------- Dialogue handling ------------------------------
// ----------------------------------------------------------------------------
static const char *diaGetLocalisedText(const char *def, int id)
{
    if (id >= 0)
        return _l(id);

    return def;
}

/// returns true if the item is controllable (e.g. a value can be changed on it)
static int diaIsControllable(struct UIItem *ui)
{
    return (ui->enabled && ui->visible && (ui->type >= UI_OK));
}

/// returns true if the given item should be preceded with nextline
static int diaShouldBreakLine(struct UIItem *ui)
{
    return (ui->type == UI_SPLITTER || ui->type == UI_OK || ui->type == UI_BREAK);
}

/// returns true if the given item should be superseded with nextline
static int diaShouldBreakLineAfter(struct UIItem *ui)
{
    return (ui->type == UI_SPLITTER);
}

static void diaDrawHint(int text_id)
{
    int x, y;
    char *text = _l(text_id);

    x = screenWidth - rmUnScaleX(fntCalcDimensions(gTheme->fonts[0], text)) - 10;
    y = gTheme->usedHeight - 62;

    // render hint on the lower side of the screen.
    rmDrawRect(x, y, screenWidth - x, MENU_ITEM_HEIGHT + 10, gColDarker);
    fntRenderString(gTheme->fonts[0], x + 5, y + 5, ALIGN_NONE, 0, 0, text, gTheme->textColor);
}

/// renders an ui item (either selected or not)
/// sets width and height of the render into the parameters
static void diaRenderItem(int x, int y, struct UIItem *item, int selected, int haveFocus, int *w, int *h)
{
    // Don't draw controllable items that are not visible.
    if (!item->visible && item->type >= UI_LABEL)
        return;

    *h = UI_SPACING_H;

    // all texts are rendered up from the given point!
    u64 txtcol;

    if (diaIsControllable(item))
        txtcol = gTheme->uiTextColor;
    else
        txtcol = gTheme->textColor;

    // let's see what do we have here?
    switch (item->type) {
        case UI_TERMINATOR:
            return;

        case UI_BUTTON:
        case UI_LABEL: {
            // width is text length in pixels...
            const char *txt = diaGetLocalisedText(item->label.text, item->label.stringId);
            if (txt && strlen(txt))
                *w = fntRenderString(gTheme->fonts[0], x, y, ALIGN_NONE, 0, 0, txt, txtcol) - x;
            else
                *w = fntRenderString(gTheme->fonts[0], x, y, ALIGN_NONE, 0, 0, _l(_STR_NOT_SET), txtcol) - x;

            break;
        }

        case UI_SPLITTER: {
            // a line. Thanks to the font rendering, we need to shift up by one font line
            *w = 0;                          // nothing to render at all
            int ypos = y - UI_SPACING_V / 2; //  gsFont->CharHeight +

            // to ODD lines
            ypos &= ~1;

            rmDrawLine(x, ypos, x + UI_BREAK_LEN, ypos, gColWhite);
            break;
        }

        case UI_BREAK:
            *w = 0; // nothing to render at all
            break;

        case UI_SPACER: {
            // next column divisible by spacer
            *w = (UI_SPACER_WIDTH - x % UI_SPACER_WIDTH);

            if (*w < UI_SPACER_MINIMAL) {
                x += *w + UI_SPACER_MINIMAL;
                *w += (UI_SPACER_WIDTH - x % UI_SPACER_WIDTH);
            }

            *h = 0;
            break;
        }

        case UI_OK: {
            const char *txt = _l(_STR_OK);
            *w = fntRenderString(gTheme->fonts[0], x, y, ALIGN_NONE, 0, 0, txt, txtcol) - x;
            break;
        }

        case UI_INT: {
            char tmp[10];

            snprintf(tmp, sizeof(tmp), "%d", item->intvalue.current);
            *w = fntRenderString(gTheme->fonts[0], x, y, ALIGN_NONE, 0, 0, tmp, txtcol) - x;
            break;
        }

        case UI_STRING: {
            if (strlen(item->stringvalue.text))
                *w = fntRenderString(gTheme->fonts[0], x, y, ALIGN_NONE, 0, 0, item->stringvalue.text, txtcol) - x;
            else
                *w = fntRenderString(gTheme->fonts[0], x, y, ALIGN_NONE, 0, 0, _l(_STR_NOT_SET), txtcol) - x;
            break;
        }

        case UI_PASSWORD: {
            char stars[32];
            int i;
            int len;

            if (strlen(item->stringvalue.text)) {
                len = min(strlen(item->stringvalue.text), sizeof(stars) - 1);
                for (i = 0; i < len; ++i)
                    stars[i] = '*';

                stars[i] = '\0';
                *w = fntRenderString(gTheme->fonts[0], x, y, ALIGN_NONE, 0, 0, stars, txtcol) - x;
            } else
                *w = fntRenderString(gTheme->fonts[0], x, y, ALIGN_NONE, 0, 0, _l(_STR_NOT_SET), txtcol) - x;
            break;
        }

        case UI_BOOL: {
            const char *txtval = _l((item->intvalue.current) ? _STR_ON : _STR_OFF);
            *w = fntRenderString(gTheme->fonts[0], x, y, ALIGN_NONE, 0, 0, txtval, txtcol) - x;
            break;
        }

        case UI_ENUM: {
            const char *tv = item->intvalue.enumvalues[item->intvalue.current];

            if (!tv)
                tv = _l(_STR_NO_ITEMS);

            *w = fntRenderString(gTheme->fonts[0], x, y, ALIGN_NONE, 0, 0, tv, txtcol) - x;
            break;
        }

        case UI_COLOUR: {
            *w = rmWideScale(25);
            *h = 17;

            // Align to the right
            x -= *w;

            rmDrawRect(x, y + 3, *w, *h, txtcol);
            u64 dcol = GS_SETREG_RGBA(item->colourvalue.r, item->colourvalue.g, item->colourvalue.b, 0x80);
            rmDrawRect(x + 2, y + 5, *w - 4, *h - 4, dcol);

            break;
        }
    }

    if (selected)
        diaDrawBoundingBox(x, y, *w, *h, haveFocus);

    if (item->fixedWidth != 0) {
        int newSize;
        if (item->fixedWidth < 0)
            newSize = item->fixedWidth * screenWidth / -100;
        else
            newSize = item->fixedWidth;
        if (*w < newSize)
            *w = newSize;
    }

    if (item->fixedHeight != 0) {
        int newSize;
        if (item->fixedHeight < 0)
            newSize = item->fixedHeight * screenHeight / -100;
        else
            newSize = item->fixedHeight;
        if (*h < newSize)
            *h = newSize;
    }
}

/// renders whole ui screen (for given dialog setup)
void diaRenderUI(struct UIItem *ui, short inMenu, struct UIItem *cur, int haveFocus)
{
    guiDrawBGPlasma();

    int x0 = 20;
    int y0 = 20;

    // render all items
    struct UIItem *rc = ui;
    int x = x0, y = y0, hmax = 0;

    while (rc->type != UI_TERMINATOR) {
        int w = 0, h = 0;

        if (diaShouldBreakLine(rc)) {
            x = x0;

            if (hmax > 0)
                y += hmax + UI_SPACING_H;

            hmax = 0;
        }

        diaRenderItem(x, y, rc, rc == cur, haveFocus, &w, &h);

        if (w > 0)
            x += w + UI_SPACING_V;

        hmax = (h > hmax) ? h : hmax;

        if (diaShouldBreakLineAfter(rc)) {
            x = x0;

            if (hmax > 0)
                y += hmax + UI_SPACING_H;

            hmax = 0;
        }

        rc++;
    }

    if ((cur != NULL) && (!haveFocus) && (cur->hintId != -1)) {
        diaDrawHint(cur->hintId);
    }

    int uiHints[2] = {_STR_SELECT, _STR_BACK};
    int uiIcons[2] = {CIRCLE_ICON, CROSS_ICON};
    int uiY = gTheme->usedHeight - 32;
    int uiX = guiAlignSubMenuHints(2, uiHints, uiIcons, gTheme->fonts[0], 12, 2);

    uiX = guiDrawIconAndText(gSelectButton == KEY_CIRCLE ? uiIcons[0] : uiIcons[1], uiHints[0], gTheme->fonts[0], uiX, uiY, gTheme->textColor);
    uiX += 12;
    uiX = guiDrawIconAndText(gSelectButton == KEY_CIRCLE ? uiIcons[1] : uiIcons[0], uiHints[1], gTheme->fonts[0], uiX, uiY, gTheme->textColor);
}

/// sets the ui item value to the default again
static void diaResetValue(struct UIItem *item)
{
    switch (item->type) {
        case UI_INT:
        case UI_BOOL:
            item->intvalue.current = item->intvalue.def;
            return;
        case UI_STRING:
        case UI_PASSWORD:
            strncpy(item->stringvalue.text, item->stringvalue.def, sizeof(item->stringvalue.text));
            return;
        default:
            return;
    }
}

static int diaHandleInput(struct UIItem *item, int *modified)
{
    // circle loses focus, sets old values first
    if (getKeyOn(gSelectButton == KEY_CIRCLE ? KEY_CROSS : KEY_CIRCLE)) {
        diaResetValue(item);
        sfxPlay(SFX_CONFIRM);
        return 0;
    }

    // cross loses focus without setting default
    if (getKeyOn(gSelectButton)) {
        sfxPlay(SFX_CONFIRM);
        *modified = 0;
        return 0;
    }

    // UI item type dependant part:
    if (item->type == UI_BOOL) {
        // a trick. Set new value, lose focus
        item->intvalue.current = !item->intvalue.current;
        return 0;
    }
    if (item->type == UI_INT) {
        // to be sure
        setButtonDelay(KEY_UP, DIA_INT_SET_SPEED);
        setButtonDelay(KEY_DOWN, DIA_INT_SET_SPEED);

        // up and down
        if (getKey(KEY_UP)) {
            sfxPlay(SFX_CURSOR);
            if (item->intvalue.current < item->intvalue.max) {
                item->intvalue.current++;
            } else {
                item->intvalue.current = item->intvalue.min; // was "= 0;"
            }
        } else if (getKey(KEY_DOWN)) {
            sfxPlay(SFX_CURSOR);
            if (item->intvalue.current > item->intvalue.min) {
                item->intvalue.current--;
            } else {
                item->intvalue.current = item->intvalue.max;
            }
        } else
            *modified = 0;
    } else if ((item->type == UI_STRING) || (item->type == UI_PASSWORD)) {
        char tmp[32];
        strncpy(tmp, item->stringvalue.text, sizeof(tmp));

        if (item->stringvalue.handler) {
            if (item->stringvalue.handler(tmp, sizeof(tmp)))
                strncpy(item->stringvalue.text, tmp, sizeof(item->stringvalue.text));
        } else {
            if (diaShowKeyb(tmp, sizeof(tmp), item->type == UI_PASSWORD, NULL))
                strncpy(item->stringvalue.text, tmp, sizeof(item->stringvalue.text));
        }

        return 0;
    } else if (item->type == UI_ENUM) {
        int cur = item->intvalue.current;

        if (item->intvalue.enumvalues[cur] == NULL) {
            if (cur > 0)
                item->intvalue.current--;
            else
                return 0;
        }

        if (getKey(KEY_UP) && (item->intvalue.current > 0)) {
            item->intvalue.current--;
            sfxPlay(SFX_CURSOR);
        } else if (getKey(KEY_DOWN) && (item->intvalue.enumvalues[item->intvalue.current + 1] != NULL)) {
            item->intvalue.current++;
            sfxPlay(SFX_CURSOR);
        }

        else {
            *modified = 0;
        }

    } else if (item->type == UI_COLOUR) {
        if (!diaShowColSel(&item->colourvalue.r, &item->colourvalue.g, &item->colourvalue.b))
            *modified = 0;

        return 0;
    }

    return 1;
}

static struct UIItem *diaGetFirstControl(struct UIItem *ui)
{
    struct UIItem *cur = ui;

    while (!diaIsControllable(cur)) {
        if (cur->type == UI_TERMINATOR)
            return ui;

        cur++;
    }

    return cur;
}

static struct UIItem *diaGetLastControl(struct UIItem *ui)
{
    struct UIItem *last = diaGetFirstControl(ui);
    struct UIItem *cur = last;

    while (cur->type != UI_TERMINATOR) {
        cur++;

        if (diaIsControllable(cur))
            last = cur;
    }

    return last;
}

static struct UIItem *diaGetNextControl(struct UIItem *cur, struct UIItem *dflt)
{
    while (cur->type != UI_TERMINATOR) {
        cur++;

        if (diaIsControllable(cur))
            return cur;
    }

    return dflt;
}

static struct UIItem *diaGetPrevControl(struct UIItem *cur, struct UIItem *ui)
{
    struct UIItem *newf = cur;

    while (newf != ui) {
        newf--;

        if (diaIsControllable(newf))
            return newf;
    }

    return cur;
}

/// finds first control on previous line...
static struct UIItem *diaGetPrevLine(struct UIItem *cur, struct UIItem *ui)
{
    struct UIItem *newf = cur;

    int lb = 0;
    int hadCtrl = 0; // had the scanned line any control?

    while (newf != ui) {
        newf--;

        if ((lb > 0) && (diaIsControllable(newf)))
            hadCtrl = 1;

        if (diaShouldBreakLine(newf)) { // is this a line break?
            if (hadCtrl || lb == 0) {
                lb++;
                hadCtrl = 0;
            }
        }

        // twice the break? find first control
        if (lb == 2)
            return diaGetFirstControl(newf);
    }

    return cur;
}

static struct UIItem *diaGetNextLine(struct UIItem *cur, struct UIItem *ui)
{
    struct UIItem *newf = cur;

    int lb = 0;

    while (newf->type != UI_TERMINATOR) {
        newf++;

        if (diaShouldBreakLine(newf)) { // is this a line break?
            lb++;
        }

        if (lb == 1)
            return diaGetNextControl(newf, cur);
    }

    return cur;
}

static int diaPadSettings[16];

static void diaStoreScrollSpeed(void)
{
    padStoreSettings(diaPadSettings);
}

static void diaRestoreScrollSpeed(void)
{
    padRestoreSettings(diaPadSettings);
}

static struct UIItem *diaFindByID(struct UIItem *ui, int id)
{
    while (ui->type != UI_TERMINATOR) {
        if (ui->id == id)
            return ui;

        ui++;
    }

    return NULL;
}

int diaExecuteDialog(struct UIItem *ui, int uiId, short inMenu, int (*updater)(int modified))
{
    rmGetScreenExtents(&screenWidth, &screenHeight);

    struct UIItem *cur = NULL;
    if (uiId != -1)
        cur = diaFindByID(ui, uiId);

    if (!cur)
        cur = diaGetFirstControl(ui);

    // what? no controllable item? Exit!
    if (cur == ui)
        return -1;

    int haveFocus = 0, modified;

    diaStoreScrollSpeed();

    // slower controls for dialogs
    setButtonDelay(KEY_UP, DIA_SCROLL_SPEED);
    setButtonDelay(KEY_DOWN, DIA_SCROLL_SPEED);

    // okay, we have the first selectable item
    // we can proceed with rendering etc. etc.
    while (1) {
        rmStartFrame();
        diaRenderUI(ui, inMenu, cur, haveFocus);
        rmEndFrame();

        readPads();

        if (haveFocus) {
            modified = 1;
            haveFocus = diaHandleInput(cur, &modified);

            if (!haveFocus) {
                setButtonDelay(KEY_UP, DIA_SCROLL_SPEED);
                setButtonDelay(KEY_DOWN, DIA_SCROLL_SPEED);
            }
        } else {
            modified = 0;
            struct UIItem *newf = cur;

            if (getKey(KEY_LEFT)) {
                newf = diaGetPrevControl(cur, ui);
                if (newf == cur)
                    newf = diaGetLastControl(ui);
            }

            if (getKey(KEY_RIGHT)) {
                newf = diaGetNextControl(cur, cur);
                if (newf == cur)
                    newf = diaGetFirstControl(ui);
            }

            if (getKey(KEY_UP)) {
                newf = diaGetPrevLine(cur, ui);
                if (newf == cur)
                    newf = diaGetLastControl(ui);
            }

            if (getKey(KEY_DOWN)) {
                newf = diaGetNextLine(cur, ui);
                if (newf == cur)
                    newf = diaGetFirstControl(ui);
            }

            if (newf != cur) {
                // Navigation change detected
                sfxPlay(SFX_CURSOR);
                cur = newf;
            }

            // Cancel button breaks focus or exits with false result
            if (getKeyOn(gSelectButton == KEY_CIRCLE ? KEY_CROSS : KEY_CIRCLE)) {
                diaRestoreScrollSpeed();
                sfxPlay(SFX_CANCEL);
                return UIID_BTN_CANCEL;
            }

            // see what key events we have
            if (getKeyOn(gSelectButton)) {
                haveFocus = 1;
                sfxPlay(SFX_CONFIRM);

                if (cur->type == UI_BUTTON) {
                    diaRestoreScrollSpeed();
                    return cur->id;
                }

                if (cur->type == UI_OK) {
                    diaRestoreScrollSpeed();
                    return UIID_BTN_OK;
                }
            }
        }

        if (updater) {
            int updResult = updater(modified);
            if (updResult)
                return updResult;
        }
    }
}

void diaSetEnabled(struct UIItem *ui, int id, int enabled)
{
    struct UIItem *item = diaFindByID(ui, id);

    if (!item)
        return;

    item->enabled = enabled;
}

void diaSetVisible(struct UIItem *ui, int id, int visible)
{
    struct UIItem *item = diaFindByID(ui, id);

    if (!item)
        return;

    item->visible = visible;
}

void diaSetItemType(struct UIItem *ui, int id, UIItemType type)
{
    struct UIItem *item = diaFindByID(ui, id);

    if (!item)
        return;

    item->type = type;
}

int diaGetInt(struct UIItem *ui, int id, int *value)
{
    struct UIItem *item = diaFindByID(ui, id);

    if (!item)
        return 0;

    if ((item->type == UI_INT) || (item->type == UI_BOOL) || (item->type == UI_ENUM)) {
        *value = item->intvalue.current;
        return 1;
    }

    return 0;
}

int diaSetInt(struct UIItem *ui, int id, int value)
{
    struct UIItem *item = diaFindByID(ui, id);

    if (!item)
        return 0;

    if ((item->type == UI_INT) || (item->type == UI_BOOL) || (item->type == UI_ENUM)) {
        item->intvalue.def = value;
        item->intvalue.current = value;
        return 1;
    }

    return 0;
}

int diaGetString(struct UIItem *ui, int id, char *value, int length)
{
    struct UIItem *item = diaFindByID(ui, id);

    if (!item)
        return 0;

    if ((item->type == UI_STRING) || (item->type == UI_PASSWORD)) {
        strncpy(value, item->stringvalue.text, length);
        return 1;
    }

    return 0;
}

int diaSetString(struct UIItem *ui, int id, const char *text)
{
    struct UIItem *item = diaFindByID(ui, id);

    if (!item)
        return 0;

    if ((item->type == UI_STRING) || (item->type == UI_PASSWORD)) {
        strncpy(item->stringvalue.def, text, sizeof(item->stringvalue.def));
        item->stringvalue.def[sizeof(item->stringvalue.def) - 1] = '\0';
        strncpy(item->stringvalue.text, text, sizeof(item->stringvalue.text));
        item->stringvalue.text[sizeof(item->stringvalue.text) - 1] = '\0';
        return 1;
    }

    return 0;
}

int diaGetColor(struct UIItem *ui, int id, unsigned char *col)
{
    struct UIItem *item = diaFindByID(ui, id);

    if (!item)
        return 0;

    if (item->type != UI_COLOUR)
        return 0;

    col[0] = item->colourvalue.r;
    col[1] = item->colourvalue.g;
    col[2] = item->colourvalue.b;
    return 1;
}

int diaSetColor(struct UIItem *ui, int id, const unsigned char *col)
{
    struct UIItem *item = diaFindByID(ui, id);

    if (!item)
        return 0;

    if (item->type != UI_COLOUR)
        return 0;

    item->colourvalue.r = col[0];
    item->colourvalue.g = col[1];
    item->colourvalue.b = col[2];
    return 1;
}

int diaSetU64Color(struct UIItem *ui, int id, u64 col)
{
    struct UIItem *item = diaFindByID(ui, id);

    if (!item)
        return 0;

    if (item->type != UI_COLOUR)
        return 0;

    item->colourvalue.r = col & 0xFF;
    col >>= 8;
    item->colourvalue.g = col & 0xFF;
    col >>= 8;
    item->colourvalue.b = col & 0xFF;
    return 1;
}

int diaSetLabel(struct UIItem *ui, int id, const char *text)
{
    struct UIItem *item = diaFindByID(ui, id);

    if (!item)
        return 0;

    if ((item->type == UI_LABEL) || (item->type == UI_BUTTON)) {
        item->label.text = text;
        return 1;
    }

    return 0;
}

int diaSetEnum(struct UIItem *ui, int id, const char **enumvals)
{
    struct UIItem *item = diaFindByID(ui, id);

    if (!item)
        return 0;

    if (item->type != UI_ENUM)
        return 0;

    item->intvalue.enumvalues = enumvals;
    return 1;
}
