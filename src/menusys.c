/*
  Copyright 2009, Ifcaro & volca
  Licenced under Academic Free License version 3.0
  Review OpenUsbLd README & LICENSE files for further details.
*/

#include "include/opl.h"
#include "include/menusys.h"
#include "include/iosupport.h"
#include "include/renderman.h"
#include "include/fntsys.h"
#include "include/lang.h"
#include "include/themes.h"
#include "include/textures.h"
#include "include/pad.h"
#include "include/gui.h"
#include "include/guigame.h"
#include "include/dia.h"
#include "include/system.h"
#include "include/ioman.h"
#include "include/sound.h"
#include "include/ethsupport.h"
#include "include/config.h"
#include "include/util.h"
#include "include/bdmsupport.h"
#include "include/hddsupport.h"
#include <assert.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum MENU_IDs {
    MENU_SETTINGS = 0,
    MENU_GFX_SETTINGS,
    MENU_AUDIO_SETTINGS,
    MENU_CONTROLLER_SETTINGS,
    MENU_OSD_LANGUAGE_SETTINGS,
    MENU_PARENTAL_LOCK,
    MENU_NET_CONFIG,
    MENU_NET_UPDATE,
    MENU_START_NBD,
    MENU_ABOUT,
    MENU_SAVE_CHANGES,
    MENU_EXIT,
    MENU_POWER_OFF
};

enum GAME_MENU_IDs {
    GAME_COMPAT_SETTINGS = 0,
    GAME_CHEAT_SETTINGS,
    GAME_GSM_SETTINGS,
    GAME_VMC_SETTINGS,
#ifdef PADEMU
    GAME_PADEMU_SETTINGS,
    GAME_PADMACRO_SETTINGS,
#endif
    GAME_OSD_LANGUAGE_SETTINGS,
    GAME_SAVE_CHANGES,
    GAME_TEST_CHANGES,
    GAME_REMOVE_CHANGES,
    GAME_RENAME_GAME,
    GAME_DELETE_GAME,
    GAME_PS5_MODE_BASE = 100,
    GAME_PS5_GSM_RESOLUTION = 120,
};

#define PS5_SMB_SETTINGS_COUNT 11
#define PS5_CAROUSEL_REPEAT_DELAY 120
#define PS5_CAROUSEL_REPEAT_DELAY_FAST 100
#define PS5_APPS_SCAN_MAX_DEPTH 8

static int ps5CarouselHoldDirection = 0;
static clock_t ps5CarouselHoldStarted = 0;

// global menu variables
static menu_list_t *menu;
static menu_list_t *selected_item;

static int actionStatus;
static int itemConfigId;
static int itemConfigSourceId;
static config_set_t *itemConfig;
static item_list_t *itemConfigSupport;
static char itemConfigStartup[GAME_STARTUP_MAX];

static u8 parentalLockCheckEnabled = 1;

// "main menu submenu"
static submenu_list_t *mainMenu;
// active item in the main menu
static submenu_list_t *mainMenuCurrent;

// "game settings submenu"
static submenu_list_t *gameMenu;
// active item in game settings
static submenu_list_t *gameMenuCurrent;

static int ps5GameCompatMode;
static int ps5GameGSMResolution;
extern int gPS5CarouselNavInterrupt;

static const char *ps5GameResolutionNames[] = {"Standard", "720p", "1080i"};
static const int ps5GameResolutionGSMModes[] = {0, 10, 11};
#define PS5_GAME_RESOLUTION_COUNT 3

extern void rmDrawRoundedRect(int x, int y, int w, int h, int r, u64 color);
extern void rmDrawRoundedRectWide(int x, int y, int w, int h, int r, u64 color);

static submenu_list_t *appMenu;
static submenu_list_t *appMenuCurrent;

static s32 menuSemaId;
static s32 menuListSemaId = -1;
static ee_sema_t menuSema;

static void ps5GameOptionsLoad(config_set_t *configSet)
{
    int source = SETTINGS_GLOBAL;
    int enableGSM = 0;
    int gsmMode = 0;

    ps5GameCompatMode = 0;
    ps5GameGSMResolution = 0;

    if (configSet != NULL)
        configGetInt(configSet, CONFIG_ITEM_COMPAT, &ps5GameCompatMode);

    if (configSet != NULL)
        configGetInt(configSet, CONFIG_ITEM_GSMSOURCE, &source);
    if (configSet != NULL && source == SETTINGS_PERGAME) {
        configGetInt(configSet, CONFIG_ITEM_ENABLEGSM, &enableGSM);
        configGetInt(configSet, CONFIG_ITEM_GSMVMODE, &gsmMode);
        if (enableGSM) {
            if (gsmMode == 10)
                ps5GameGSMResolution = 1;
            else if (gsmMode == 11)
                ps5GameGSMResolution = 2;
        }
    }
}

static int drawPS5GameIconAndText(int iconId, const char *text, int font, int x, int y, u64 color)
{
    GSTEXTURE *iconTex = thmGetTexture(iconId);
    int iconW = 0;
    int iconH = 14;
    int gap = 6;

    if (iconTex && iconTex->Mem && iconTex->Height > 0) {
        iconW = (iconTex->Width * iconH) / iconTex->Height;
        rmDrawPixmap(iconTex, x, y, ALIGN_VCENTER | ALIGN_LEFT, iconW, iconH, 1, color);
        x += rmWideScale(iconW) + gap;
    }

    return fntRenderString(font, x, y, ALIGN_VCENTER | ALIGN_LEFT, 0.65f, 0.65f, text, color);
}

static int drawPS5GameRightIconAndText(int iconId, const char *text, int font, int rightX, int y, u64 color)
{
    GSTEXTURE *iconTex = thmGetTexture(iconId);
    int iconW = 0;
    int iconH = 14;
    int gap = 6;
    int textW = rmUnScaleX(fntCalcDimensions(font, text));
    int groupX;

    if (iconTex && iconTex->Mem && iconTex->Height > 0)
        iconW = (iconTex->Width * iconH) / iconTex->Height;

    groupX = rightX - iconW - gap - textW;

    if (iconW > 0)
        rmDrawPixmap(iconTex, groupX, y, ALIGN_VCENTER | ALIGN_LEFT, iconW, iconH, 1, color);

    fntRenderString(font, groupX + iconW + gap, y, ALIGN_LEFT | ALIGN_VCENTER, 0.65f, 0.65f, text, color);
    return groupX;
}

static void drawPS5GameFocusIndicator(int labelX, int rowY)
{
    drawPS5FocusPointer(labelX - 22, rowY);
}

static void ps5DrawLaunchLoadingTransition(void)
{
    GSTEXTURE *loader = thmGetTexture(LOADER_ICON);
    int screenW, screenH;
    int frame;

    rmGetScreenExtents(&screenW, &screenH);

    for (frame = 0; frame < 32; frame++) {
        int alpha;
        int loaderSize = 14;
        int loaderX = (screenW - 20 - (loaderSize / 2)) * 4 / rmGetAspectWidth();
        int loaderY = screenH - 20;
        float angle = (float)frame * 0.22f;

        if (frame < 10)
            alpha = (0x80 * frame) / 9;
        else if (frame < 20)
            alpha = 0x80;
        else
            alpha = 0x80 - ((0x80 * (frame - 20)) / 11);

        guiStartFrame();
        rmDrawRect(0, 0, screenW, screenH, GS_SETREG_RGBA(0, 0, 0, alpha));
        if (loader && loader->Mem && alpha > 0)
            rmDrawRotatedPixmap(loader, loaderX, loaderY, loaderSize, loaderSize, angle, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, alpha));
        guiEndFrame();
    }

    for (frame = 0; frame < 8; frame++) {
        guiStartFrame();
        rmDrawRect(0, 0, screenW, screenH, GS_SETREG_RGBA(0, 0, 0, 0x80));
        guiEndFrame();
    }
}

static void ps5DrawAppLaunchTransition(void)
{
    GSTEXTURE *loader = thmGetTexture(LOADER_ICON);
    int screenW, screenH;
    int frame;

    rmGetScreenExtents(&screenW, &screenH);

    for (frame = 0; frame < 60; frame++) {
        int alpha;
        int loaderAlpha;
        int loaderSize = 14;
        int loaderX = (screenW - 20 - (loaderSize / 2)) * 4 / rmGetAspectWidth();
        int loaderY = screenH - 20;
        float angle = (float)frame * 0.22f;

        if (frame < 12)
            alpha = (0x80 * frame) / 11;
        else
            alpha = 0x80;

        if (frame < 12)
            loaderAlpha = (0xFF * frame) / 11;
        else if (frame < 48)
            loaderAlpha = 0xFF;
        else
            loaderAlpha = 0xFF - ((0xFF * (frame - 48)) / 11);

        guiStartFrame();
        rmDrawRect(0, 0, screenW, screenH, GS_SETREG_RGBA(0, 0, 0, alpha));
        if (loader && loader->Mem && loaderAlpha > 0)
            rmDrawRotatedPixmap(loader, loaderX, loaderY, loaderSize, loaderSize, angle, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, loaderAlpha));
        guiEndFrame();
    }
}

static void ps5GameOptionsSave(config_set_t *configSet)
{
    if (configSet == NULL)
        return;

    if (ps5GameCompatMode != 0)
        configSetInt(configSet, CONFIG_ITEM_COMPAT, ps5GameCompatMode);
    else
        configRemoveKey(configSet, CONFIG_ITEM_COMPAT);

    if (ps5GameGSMResolution == 0) {
        configRemoveKey(configSet, CONFIG_ITEM_GSMSOURCE);
        configRemoveKey(configSet, CONFIG_ITEM_ENABLEGSM);
        configRemoveKey(configSet, CONFIG_ITEM_GSMVMODE);
        configRemoveKey(configSet, CONFIG_ITEM_GSMXOFFSET);
        configRemoveKey(configSet, CONFIG_ITEM_GSMYOFFSET);
        configRemoveKey(configSet, CONFIG_ITEM_GSMFIELDFIX);
    } else {
        configSetInt(configSet, CONFIG_ITEM_GSMSOURCE, SETTINGS_PERGAME);
        configSetInt(configSet, CONFIG_ITEM_ENABLEGSM, 1);
        configSetInt(configSet, CONFIG_ITEM_GSMVMODE, ps5GameResolutionGSMModes[ps5GameGSMResolution]);
        configRemoveKey(configSet, CONFIG_ITEM_GSMXOFFSET);
        configRemoveKey(configSet, CONFIG_ITEM_GSMYOFFSET);
        configRemoveKey(configSet, CONFIG_ITEM_GSMFIELDFIX);
    }

    configSetInt(configSet, CONFIG_ITEM_CONFIGSOURCE, CONFIG_SOURCE_USER);
}

static void menuRenameGame(submenu_list_t **submenu)
{
    if (!selected_item->item->current) {
        return;
    }

    if (!gEnableWrite)
        return;

    item_list_t *support = selected_item->item->userdata;

    if (support) {
        if (support->itemRename) {
            if (menuCheckParentalLock() == 0) {
                sfxPlay(SFX_MESSAGE);
                int nameLength = support->itemGetNameLength(support, selected_item->item->current->item.id);
                char newName[nameLength];
                strncpy(newName, selected_item->item->current->item.text, nameLength);
                if (guiShowKeyboard(newName, nameLength)) {
                    guiSwitchScreen(GUI_SCREEN_MAIN);
                    submenuDestroy(submenu);

                    // Only rename the file if the name changed; trying to rename a file with a file name that hasn't changed can cause the file
                    // to be deleted on certain file systems.
                    if (strcmp(newName, selected_item->item->current->item.text) != 0) {
                        support->itemRename(support, selected_item->item->current->item.id, newName);
                        ioPutRequest(IO_MENU_UPDATE_DEFFERED, &support->mode);
                    }
                }
            }
        }
    } else
        guiMsgBox("NULL Support object. Please report", 0, NULL);
}

static void menuDeleteGame(submenu_list_t **submenu)
{
    if (!selected_item->item->current)
        return;

    if (!gEnableWrite)
        return;

    item_list_t *support = selected_item->item->userdata;

    if (support) {
        if (support->itemDelete) {
            if (menuCheckParentalLock() == 0) {
                if (guiMsgBox(_l(_STR_DELETE_WARNING), 1, NULL)) {
                    guiSwitchScreen(GUI_SCREEN_MAIN);
                    submenuDestroy(submenu);
                    support->itemDelete(support, selected_item->item->current->item.id);
                    ioPutRequest(IO_MENU_UPDATE_DEFFERED, &support->mode);
                }
            }
        }
    } else
        guiMsgBox("NULL Support object. Please report", 0, NULL);
}

static void _menuLoadConfig()
{
    WaitSema(menuSemaId);
    if (!itemConfig) {
        item_list_t *list = itemConfigSupport != NULL ? itemConfigSupport : selected_item->item->userdata;
        itemConfig = list->itemGetConfig(list, itemConfigSourceId);
    }
    actionStatus = 0;
    SignalSema(menuSemaId);
}

static void _menuSaveConfig()
{
    int result;

    WaitSema(menuSemaId);
    result = configWrite(itemConfig);
    itemConfigId = -1; // to invalidate cache and force reload
    itemConfigSourceId = -1;
    itemConfigSupport = NULL;
    itemConfigStartup[0] = '\0';
    actionStatus = 0;
    SignalSema(menuSemaId);

    if (!result)
        setErrorMessage(_STR_ERROR_SAVING_SETTINGS);
}

static void _menuRequestConfig()
{
    WaitSema(menuSemaId);
    if (selected_item->item->current != NULL && itemConfigId != selected_item->item->current->item.id) {
        int resolvedId;
        item_list_t *resolvedSupport;
        const char *startup;

        if (itemConfig) {
            configFree(itemConfig);
            itemConfig = NULL;
        }
        resolvedSupport = selected_item->item->userdata;
        if (!oplResolveGameItem(selected_item->item->current->item.id, resolvedSupport, &resolvedSupport, &resolvedId) || resolvedSupport == NULL) {
            actionStatus = 0;
            SignalSema(menuSemaId);
            return;
        }
        if (itemConfigId == -1 || guiInactiveFrames >= resolvedSupport->delay) {
            itemConfigId = selected_item->item->current->item.id;
            itemConfigSourceId = resolvedId;
            itemConfigSupport = resolvedSupport;
            startup = resolvedSupport->itemGetStartup != NULL ? resolvedSupport->itemGetStartup(resolvedSupport, resolvedId) : NULL;
            if (startup != NULL) {
                strncpy(itemConfigStartup, startup, sizeof(itemConfigStartup) - 1);
                itemConfigStartup[sizeof(itemConfigStartup) - 1] = '\0';
            } else {
                itemConfigStartup[0] = '\0';
            }
            ioPutRequest(IO_CUSTOM_SIMPLEACTION, &_menuLoadConfig);
        }
    } else if (itemConfig)
        actionStatus = 0;

    SignalSema(menuSemaId);
}

config_set_t *menuLoadConfig()
{
    actionStatus = 1;
    itemConfigId = -1;
    itemConfigSourceId = -1;
    itemConfigSupport = NULL;
    itemConfigStartup[0] = '\0';
    guiHandleDeferedIO(&actionStatus, _l(_STR_LOADING_SETTINGS), IO_CUSTOM_SIMPLEACTION, &_menuRequestConfig);
    return itemConfig;
}

// we don't want a pop up when transitioning to or refreshing Game Menu gui.
config_set_t *gameMenuLoadConfig(struct UIItem *ui)
{
    actionStatus = 1;
    itemConfigId = -1;
    itemConfigSourceId = -1;
    itemConfigSupport = NULL;
    itemConfigStartup[0] = '\0';
    guiGameHandleDeferedIO(&actionStatus, ui, IO_CUSTOM_SIMPLEACTION, &_menuRequestConfig);
    if (gPS5Mode)
        ps5GameOptionsLoad(itemConfig);
    return itemConfig;
}

void menuSaveConfig()
{
    actionStatus = 1;
    guiHandleDeferedIO(&actionStatus, _l(_STR_SAVING_SETTINGS), IO_CUSTOM_SIMPLEACTION, &_menuSaveConfig);
}

static void menuInitMainMenu(void)
{
    if (mainMenu)
        submenuDestroy(&mainMenu);

    // initialize the menu
    submenuAppendItem(&mainMenu, -1, NULL, MENU_SETTINGS, _STR_SETTINGS);
    submenuAppendItem(&mainMenu, -1, NULL, MENU_GFX_SETTINGS, _STR_GFX_SETTINGS);
    submenuAppendItem(&mainMenu, -1, NULL, MENU_AUDIO_SETTINGS, _STR_AUDIO_SETTINGS);
    submenuAppendItem(&mainMenu, -1, NULL, MENU_CONTROLLER_SETTINGS, _STR_CONTROLLER_SETTINGS);
    submenuAppendItem(&mainMenu, -1, NULL, MENU_OSD_LANGUAGE_SETTINGS, _STR_OSD_SETTINGS);
    submenuAppendItem(&mainMenu, -1, NULL, MENU_PARENTAL_LOCK, _STR_PARENLOCKCONFIG);
    submenuAppendItem(&mainMenu, -1, NULL, MENU_NET_CONFIG, _STR_NETCONFIG);
    submenuAppendItem(&mainMenu, -1, NULL, MENU_NET_UPDATE, _STR_NET_UPDATE);
    submenuAppendItem(&mainMenu, -1, NULL, MENU_START_NBD, _STR_STARTNBD);
    submenuAppendItem(&mainMenu, -1, NULL, MENU_ABOUT, _STR_ABOUT);
    submenuAppendItem(&mainMenu, -1, NULL, MENU_SAVE_CHANGES, _STR_SAVE_CHANGES);
    submenuAppendItem(&mainMenu, -1, NULL, MENU_EXIT, _STR_EXIT);
    submenuAppendItem(&mainMenu, -1, NULL, MENU_POWER_OFF, _STR_POWEROFF);

    mainMenuCurrent = mainMenu;
}

void menuReinitMainMenu(void)
{
    menuInitMainMenu();
}

void menuInitGameMenu(void)
{
    if (gameMenu)
        submenuDestroy(&gameMenu);

    if (gPS5Mode) {
        submenuAppendItem(&gameMenu, -1, "Resolution", GAME_PS5_GSM_RESOLUTION, -1);
        submenuAppendItem(&gameMenu, -1, "Mode 1", GAME_PS5_MODE_BASE, -1);
        submenuAppendItem(&gameMenu, -1, "Mode 2", GAME_PS5_MODE_BASE + 1, -1);
        submenuAppendItem(&gameMenu, -1, "Mode 3", GAME_PS5_MODE_BASE + 2, -1);
        submenuAppendItem(&gameMenu, -1, "Mode 4", GAME_PS5_MODE_BASE + 3, -1);
        submenuAppendItem(&gameMenu, -1, "Mode 5", GAME_PS5_MODE_BASE + 4, -1);
        submenuAppendItem(&gameMenu, -1, "Mode 6", GAME_PS5_MODE_BASE + 5, -1);
        gameMenuCurrent = gameMenu;
        return;
    }

    // initialize the menu
    submenuAppendItem(&gameMenu, -1, NULL, GAME_COMPAT_SETTINGS, _STR_COMPAT_SETTINGS);
    submenuAppendItem(&gameMenu, -1, NULL, GAME_CHEAT_SETTINGS, _STR_CHEAT_SETTINGS);
    submenuAppendItem(&gameMenu, -1, NULL, GAME_GSM_SETTINGS, _STR_GSCONFIG);
    submenuAppendItem(&gameMenu, -1, NULL, GAME_VMC_SETTINGS, _STR_VMC_SCREEN);
#ifdef PADEMU
    submenuAppendItem(&gameMenu, -1, NULL, GAME_PADEMU_SETTINGS, _STR_PADEMUCONFIG);
    submenuAppendItem(&gameMenu, -1, NULL, GAME_PADMACRO_SETTINGS, _STR_PADMACROCONFIG);
#endif
    submenuAppendItem(&gameMenu, -1, NULL, GAME_OSD_LANGUAGE_SETTINGS, _STR_OSD_SETTINGS);
    submenuAppendItem(&gameMenu, -1, NULL, GAME_SAVE_CHANGES, _STR_SAVE_CHANGES);
    submenuAppendItem(&gameMenu, -1, NULL, GAME_TEST_CHANGES, _STR_TEST);
    submenuAppendItem(&gameMenu, -1, NULL, GAME_REMOVE_CHANGES, _STR_REMOVE_ALL_SETTINGS);
    if (gEnableWrite) {
        submenuAppendItem(&gameMenu, -1, NULL, GAME_RENAME_GAME, _STR_RENAME);
        submenuAppendItem(&gameMenu, -1, NULL, GAME_DELETE_GAME, _STR_DELETE);
    }

    gameMenuCurrent = gameMenu;
}

void menuInitAppMenu(void)
{
    if (appMenu)
        submenuDestroy(&appMenu);

    // initialize the menu
    submenuAppendItem(&appMenu, -1, NULL, 0, _STR_RENAME);
    submenuAppendItem(&appMenu, -1, NULL, 1, _STR_DELETE);

    appMenuCurrent = appMenu;
}

// -------------------------------------------------------------------------------------------
// ---------------------------------------- Menu manipulation --------------------------------
// -------------------------------------------------------------------------------------------
void menuInit()
{
    menu = NULL;
    selected_item = NULL;
    itemConfigId = -1;
    itemConfig = NULL;
    mainMenu = NULL;
    mainMenuCurrent = NULL;
    gameMenu = NULL;
    gameMenuCurrent = NULL;
    appMenu = NULL;
    appMenuCurrent = NULL;
    menuInitMainMenu();

    menuSema.init_count = 1;
    menuSema.max_count = 1;
    menuSema.option = 0;
    menuSemaId = CreateSema(&menuSema);
    if (menuListSemaId < 0) {
        menuListSemaId = sbCreateSemaphore();
    }
}

void menuEnd()
{
    // destroy menu
    menu_list_t *cur = menu;

    while (cur) {
        menu_list_t *td = cur;
        cur = cur->next;

        if (td->item)
            submenuDestroy(&(td->item->submenu));

        menuRemoveHints(td->item);

        free(td);
    }

    submenuDestroy(&mainMenu);
    submenuDestroy(&gameMenu);
    submenuDestroy(&appMenu);

    if (itemConfig) {
        configFree(itemConfig);
        itemConfig = NULL;
    }

    DeleteSema(menuSemaId);
    DeleteSema(menuListSemaId);
    menuListSemaId = -1;
}

static menu_list_t *AllocMenuItem(menu_item_t *item)
{
    menu_list_t *it;

    it = malloc(sizeof(menu_list_t));

    it->prev = NULL;
    it->next = NULL;
    it->item = item;

    return it;
}

void menuAppendItem(menu_item_t *item)
{
    assert(item);

    WaitSema(menuListSemaId);

    if (menu == NULL) {
        menu = AllocMenuItem(item);
        selected_item = menu;
    } else {
        menu_list_t *cur = menu;

        // traverse till the end
        while (cur->next)
            cur = cur->next;

        // create new item
        menu_list_t *newitem = AllocMenuItem(item);

        // link
        cur->next = newitem;
        newitem->prev = cur;
    }

    SignalSema(menuListSemaId);
}

static void refreshMenuPosition(void)
{
    // Find the first menu in the list that is visible and set it as the active menu.
    if (menu == NULL)
        return;

    menu_list_t *cur = menu;
    while (cur->item->visible == 0 && cur->next)
        cur = cur->next;

    if (cur->item->visible == 0) {
        // No visible menu was found, just set the current menu to the first one in the list.
        selected_item = menu;
    } else
        selected_item = cur;
}

void submenuRebuildCache(submenu_list_t *submenu)
{
    while (submenu) {
        if (submenu->item.cache_id)
            free(submenu->item.cache_id);
        if (submenu->item.cache_uid)
            free(submenu->item.cache_uid);

        int size = gTheme->gameCacheCount * sizeof(int);
        submenu->item.cache_id = malloc(size);
        memset(submenu->item.cache_id, -1, size);
        submenu->item.cache_uid = malloc(size);
        memset(submenu->item.cache_uid, -1, size);

        submenu = submenu->next;
    }
}

static submenu_list_t *submenuAllocItem(int icon_id, char *text, int id, int text_id)
{
    submenu_list_t *it = (submenu_list_t *)malloc(sizeof(submenu_list_t));
    char *textCopy = NULL;

    if (text != NULL) {
        textCopy = (char *)malloc(strlen(text) + 1);
        if (textCopy != NULL)
            strcpy(textCopy, text);
    }

    it->prev = NULL;
    it->next = NULL;
    it->item.icon_id = icon_id;
    it->item.text = textCopy;
    it->item.text_id = text_id;
    it->item.id = id;
    it->item.cache_id = NULL;
    it->item.cache_uid = NULL;
    submenuRebuildCache(it);

    return it;
}

submenu_list_t *submenuAppendItem(submenu_list_t **submenu, int icon_id, char *text, int id, int text_id)
{
    if (*submenu == NULL) {
        *submenu = submenuAllocItem(icon_id, text, id, text_id);
        return *submenu;
    }

    submenu_list_t *cur = *submenu;

    // traverse till the end
    while (cur->next)
        cur = cur->next;

    // create new item
    submenu_list_t *newitem = submenuAllocItem(icon_id, text, id, text_id);

    // link
    cur->next = newitem;
    newitem->prev = cur;

    return newitem;
}

static void submenuDestroyItem(submenu_list_t *submenu)
{
    free(submenu->item.cache_id);
    free(submenu->item.cache_uid);
    free(submenu->item.text);

    free(submenu);
}

void submenuRemoveItem(submenu_list_t **submenu, int id)
{
    submenu_list_t *cur = *submenu;
    submenu_list_t *prev = NULL;

    while (cur) {
        if (cur->item.id == id) {
            submenu_list_t *next = cur->next;

            if (prev)
                prev->next = cur->next;

            if (*submenu == cur)
                *submenu = next;

            submenuDestroyItem(cur);

            cur = next;
        } else {
            prev = cur;
            cur = cur->next;
        }
    }
}

void submenuDestroy(submenu_list_t **submenu)
{
    // destroy sub menu
    submenu_list_t *cur = *submenu;

    while (cur) {
        submenu_list_t *td = cur;
        cur = cur->next;

        submenuDestroyItem(td);
    }

    *submenu = NULL;
}

void menuAddHint(menu_item_t *menu, int text_id, int icon_id)
{
    // allocate a new hint item
    menu_hint_item_t *hint = malloc(sizeof(menu_hint_item_t));

    hint->text_id = text_id;
    hint->icon_id = icon_id;
    hint->next = NULL;

    if (menu->hints) {
        menu_hint_item_t *top = menu->hints;

        // rewind to end
        for (; top->next; top = top->next)
            ;

        top->next = hint;
    } else {
        menu->hints = hint;
    }
}

void menuRemoveHints(menu_item_t *menu)
{
    while (menu->hints) {
        menu_hint_item_t *hint = menu->hints;
        menu->hints = hint->next;
        free(hint);
    }
}

char *menuItemGetText(menu_item_t *it)
{
    if (it->text_id >= 0)
        return _l(it->text_id);
    else
        return it->text;
}

char *submenuItemGetText(submenu_item_t *it)
{
    if (it->text_id >= 0)
        return _l(it->text_id);
    else
        return it->text;
}

static void swap(submenu_list_t *a, submenu_list_t *b)
{
    submenu_list_t *pa, *nb;
    pa = a->prev;
    nb = b->next;

    a->next = nb;
    b->prev = pa;
    b->next = a;
    a->prev = b;

    if (pa)
        pa->next = b;

    if (nb)
        nb->prev = a;
}

// Sorts the given submenu by comparing the on-screen titles
void submenuSort(submenu_list_t **submenu)
{
    // a simple bubblesort
    // *submenu = mergeSort(*submenu);
    submenu_list_t *head;
    int sorted = 0;

    if ((submenu == NULL) || (*submenu == NULL) || ((*submenu)->next == NULL))
        return;

    head = *submenu;

    while (!sorted) {
        sorted = 1;

        submenu_list_t *tip = head;

        while (tip->next) {
            submenu_list_t *nxt = tip->next;

            char *txt1 = submenuItemGetText(&tip->item);
            char *txt2 = submenuItemGetText(&nxt->item);

            int cmp = strcasecmp(txt1, txt2);

            if (cmp > 0) {
                swap(tip, nxt);

                if (tip == head)
                    head = nxt;

                sorted = 0;
            } else {
                tip = tip->next;
            }
        }
    }

    *submenu = head;
}

static int ps5MenuTitleMatchesAlpha(const char *title)
{
    extern int gPS5AlphaIdx;
    char firstChar;

    if (gPS5AlphaIdx <= 0)
        return 1;
    if (title == NULL || title[0] == '\0')
        return 0;

    firstChar = title[0];
    if (firstChar >= 'a' && firstChar <= 'z')
        firstChar -= 32;

    return firstChar >= 'A' && firstChar <= 'Z' && (firstChar - 'A' + 1) == gPS5AlphaIdx;
}

static int ps5MenuGameIsVisible(submenu_list_t *entry)
{
    item_list_t *support;
    int sourceId;
    opl_io_module_t *owner;
    int encodedItem;

    if (selected_item == NULL || selected_item->item == NULL || entry == NULL)
        return 0;

    support = (item_list_t *)selected_item->item->userdata;
    if (!oplResolveGameItem(entry->item.id, support, &support, &sourceId))
        return 0;

    if (support == NULL || !support->enabled || sourceId < 0)
        return 0;

    owner = (opl_io_module_t *)support->owner;
    if (owner == NULL)
        return 0;

    encodedItem = oplIsGameItemIdEncoded(entry->item.id);
    if (!encodedItem && owner->menuItem.visible == 0)
        return 0;

    if (support->itemGetCount != NULL && sourceId >= support->itemGetCount(support))
        return 0;

    return ps5MenuTitleMatchesAlpha(submenuItemGetText(&entry->item));
}

static submenu_list_t *ps5MenuFindVisibleGameByIndex(int targetIndex)
{
    submenu_list_t *cur;
    int index = 0;

    if (selected_item == NULL || selected_item->item == NULL || targetIndex < 0)
        return NULL;

    cur = selected_item->item->submenu;
    while (cur != NULL) {
        if (ps5MenuGameIsVisible(cur)) {
            if (index == targetIndex)
                return cur;
            index++;
        }
        cur = cur->next;
    }

    return NULL;
}

static submenu_list_t *ps5MenuGetActionGame(void)
{
    submenu_list_t *cur;

    if (selected_item == NULL || selected_item->item == NULL)
        return NULL;

    if (ps5MenuGameIsVisible(selected_item->item->current))
        return selected_item->item->current;

    cur = selected_item->item->submenu;
    while (cur != NULL) {
        if (ps5MenuGameIsVisible(cur)) {
            selected_item->item->current = cur;
            selected_item->item->pagestart = cur;
            return cur;
        }
        cur = cur->next;
    }

    return NULL;
}

static void ps5MenuMoveGame(int direction)
{
    submenu_list_t *cur;
    submenu_list_t *target;
    int visibleCount = 0;
    int currentIndex = -1;
    int targetIndex;
    int index = 0;

    if (selected_item == NULL || selected_item->item == NULL || selected_item->item->submenu == NULL)
        return;

    cur = selected_item->item->submenu;
    while (cur != NULL) {
        if (ps5MenuGameIsVisible(cur)) {
            if (cur == selected_item->item->current)
                currentIndex = index;
            visibleCount++;
            index++;
        }
        cur = cur->next;
    }

    if (visibleCount <= 0)
        return;

    if (currentIndex < 0) {
        targetIndex = direction > 0 ? 0 : visibleCount - 1;
    } else {
        targetIndex = currentIndex + (direction > 0 ? 1 : -1);
        if (targetIndex >= visibleCount)
            targetIndex = 0;
        else if (targetIndex < 0)
            targetIndex = visibleCount - 1;
    }

    target = ps5MenuFindVisibleGameByIndex(targetIndex);
    if (target == NULL || target == selected_item->item->current)
        return;

    selected_item->item->current = target;
    selected_item->item->pagestart = target;
    gPS5CarouselNavInterrupt = 4;
    sfxPlay(SFX_CURSOR);
}

static void menuNextH()
{
    struct menu_list *next = selected_item->next;
    while (next != NULL && next->item->visible == 0)
        next = next->next;

    // If we found a valid menu transition to it.
    if (next != NULL) {
        selected_item = next;
        itemConfigId = -1;
        sfxPlay(SFX_CURSOR);
    }
}

static void menuPrevH()
{
    struct menu_list *prev = selected_item->prev;
    while (prev != NULL && prev->item->visible == 0)
        prev = prev->prev;

    if (prev != NULL) {
        selected_item = prev;
        itemConfigId = -1;
        sfxPlay(SFX_CURSOR);
    }
}

static void menuFirstPage()
{
    submenu_list_t *cur = selected_item->item->current;
    if (cur) {
        if (cur->prev) {
            sfxPlay(SFX_CURSOR);
        }

        selected_item->item->current = selected_item->item->submenu;
        selected_item->item->pagestart = selected_item->item->current;
    }
}

static void menuLastPage()
{
    submenu_list_t *cur = selected_item->item->current;
    if (cur) {
        if (cur->next) {
            sfxPlay(SFX_CURSOR);
        }
        while (cur->next)
            cur = cur->next; // go to end

        selected_item->item->current = cur;

        int itms = ((items_list_t *)gTheme->itemsList->extended)->displayedItems;
        while (--itms && cur->prev) // and move back to have a full page
            cur = cur->prev;

        selected_item->item->pagestart = cur;
    }
}

static void menuNextV()
{
    submenu_list_t *cur = selected_item->item->current;

    if (cur && cur->next) {
        selected_item->item->current = cur->next;
        sfxPlay(SFX_CURSOR);

        // if the current item is beyond the page start, move the page start one page down
        cur = selected_item->item->pagestart;
        int itms = ((items_list_t *)gTheme->itemsList->extended)->displayedItems + 1;
        while (--itms && cur)
            if (selected_item->item->current == cur)
                return;
            else
                cur = cur->next;

        selected_item->item->pagestart = selected_item->item->current;
    } else { // wrap to start
        menuFirstPage();
    }
}

static void menuPrevV()
{
    submenu_list_t *cur = selected_item->item->current;

    if (cur && cur->prev) {
        selected_item->item->current = cur->prev;
        sfxPlay(SFX_CURSOR);

        // if the current item is on the page start, move the page start one page up
        if (selected_item->item->pagestart == cur) {
            int itms = ((items_list_t *)gTheme->itemsList->extended)->displayedItems + 1; // +1 because the selection will move as well
            while (--itms && selected_item->item->pagestart->prev)
                selected_item->item->pagestart = selected_item->item->pagestart->prev;
        }
    } else { // wrap to end
        menuLastPage();
    }
}

static void menuNextPage()
{
    submenu_list_t *cur = selected_item->item->pagestart;

    if (cur && cur->next) {
        int itms = ((items_list_t *)gTheme->itemsList->extended)->displayedItems + 1;
        sfxPlay(SFX_CURSOR);

        while (--itms && cur->next)
            cur = cur->next;

        selected_item->item->current = cur;
        selected_item->item->pagestart = selected_item->item->current;
    } else { // wrap to start
        menuFirstPage();
    }
}

static void menuPrevPage()
{
    submenu_list_t *cur = selected_item->item->pagestart;

    if (cur && cur->prev) {
        int itms = ((items_list_t *)gTheme->itemsList->extended)->displayedItems + 1;
        sfxPlay(SFX_CURSOR);

        while (--itms && cur->prev)
            cur = cur->prev;

        selected_item->item->current = cur;
        selected_item->item->pagestart = selected_item->item->current;
    } else { // wrap to end
        menuLastPage();
    }
}

void menuSetSelectedItem(menu_item_t *item)
{
    menu_list_t *itm = menu;

    while (itm) {
        if (itm->item == item) {
            selected_item = itm;
            return;
        }

        itm = itm->next;
    }
}

void menuRenderMenu()
{
    guiDrawBGPlasma();

    if (!mainMenu)
        return;

    // draw the animated menu
    if (!mainMenuCurrent)
        mainMenuCurrent = mainMenu;

    submenu_list_t *it = mainMenu;

    // calculate the number of items
    int count = 0;
    int sitem = 0;
    for (; it; count++, it = it->next) {
        if (it == mainMenuCurrent)
            sitem = count;
    }

    int spacing = 25;
    int y = (gTheme->usedHeight >> 1) - (spacing * (count >> 1));
    int cp = 0; // current position
    for (it = mainMenu; it; it = it->next, cp++) {
        // render, advance
        fntRenderString(gTheme->fonts[0], 320, y, ALIGN_CENTER, 0, 0, submenuItemGetText(&it->item), (cp == sitem) ? gTheme->selTextColor : gTheme->textColor);
        y += spacing;
        if (cp == (MENU_ABOUT - 1))
            y += spacing / 2;
    }

    // hints
    guiDrawSubMenuHints();
}

int menuSetParentalLockCheckState(int enabled)
{
    int wasEnabled;

    wasEnabled = parentalLockCheckEnabled;
    parentalLockCheckEnabled = enabled ? 1 : 0;

    return wasEnabled;
}

int menuCheckParentalLock(void)
{
    const char *parentalLockPassword;
    char password[CONFIG_KEY_VALUE_LEN];
    int result;

    result = 0; // Default to unlocked.
    if (parentalLockCheckEnabled) {
        config_set_t *configOPL = configGetByType(CONFIG_OPL);

        // Prompt for password, only if one was set.
        if (configGetStr(configOPL, CONFIG_OPL_PARENTAL_LOCK_PWD, &parentalLockPassword) && (parentalLockPassword[0] != '\0')) {
            password[0] = '\0';
            if (diaShowKeyb(password, CONFIG_KEY_VALUE_LEN, 1, _l(_STR_PARENLOCK_ENTER_PASSWORD_TITLE))) {
                if (strncmp(parentalLockPassword, password, CONFIG_KEY_VALUE_LEN) == 0) {
                    result = 0;
                    parentalLockCheckEnabled = 0; // Stop asking for the password.
                } else if (strncmp(OPL_PARENTAL_LOCK_MASTER_PASS, password, CONFIG_KEY_VALUE_LEN) == 0) {
                    guiMsgBox(_l(_STR_PARENLOCK_DISABLE_WARNING), 0, NULL);

                    configRemoveKey(configOPL, CONFIG_OPL_PARENTAL_LOCK_PWD);
                    saveConfig(CONFIG_OPL, 1);

                    result = 0;
                    parentalLockCheckEnabled = 0; // Stop asking for the password.
                } else {
                    guiMsgBox(_l(_STR_PARENLOCK_PASSWORD_INCORRECT), 0, NULL);
                    result = EACCES;
                }
            } else // User aborted.
                result = EACCES;
        }
    }

    return result;
}

int gPS5SavedVMode = -1;
int gPS5SavedUISound = -1;
int gPS5TempUISound = 1;
int gPS5SavedShowCoverImages = -1;
int gPS5TempShowCoverImages = 1;
int gPS5SavedShowGamesLogo = -1;
int gPS5TempShowGamesLogo = 1;
int gPS5SavedSortMode = -1;
int gPS5TempSortMode = 1;
int gPS5SavedSelectButton = -1;
int gPS5TempSelectButton = KEY_CIRCLE;
int gPS5SavedControllerType = -1;
int gPS5TempControllerType = 0;
int gPS5ControllerLogVisible = 0;
int gPS5ControllerLogStep = 0;
int gPS5ControllerLogCaptured = 0;
int gPS5ControllerLogNavTestEnabled = 0;
unsigned int gPS5ControllerLogBridgePadData = 0;
int gPS5XboxNavEnabled = 0;
unsigned int gPS5XboxNavPadData = 0;
char gPS5ControllerLogDevice[96] = "DEVICE: not detected";
char gPS5ControllerLogLatest[192] = "Latest: no report";
char gPS5ControllerLogPressed[128] = "Pressed: no input found";
char gPS5ControllerLogBridge[128] = "Bridge: no valid input packet";
char gPS5ControllerLogRaw[160] = "Raw: no input";
char gPS5ControllerLogDs2[160] = "DS2: no data";
char gPS5ControllerLogStatus[96] = "Cross: capture  Square: save  Triangle: nav test  Circle: close";
char gPS5ControllerLogLines[20][192];
int gPS5SmbSettingsSel = 0;
int gPS5TempEthEnabled = 0;
int gPS5TempSmbAddressType = 0;
int gPS5TempSmbDhcp = 1;
int gPS5TempSmbPort = 445;
int gPS5TempSmbCache = 16;
char gPS5TempSmbIp[16] = "192.168.1.10";
char gPS5TempSmbName[17] = "";
char gPS5TempSmbShare[32] = "";
char gPS5TempSmbUser[32] = "";
char gPS5TempSmbPassword[32] = "";
char gPS5TempSmbPrefix[32] = "";
static volatile int gPS5SmbPromptState = 0; // 0 = can ask, 1 = answered, 2 = loading
static volatile int gPS5SmbLoadStatus = 0;
static volatile int gPS5SmbRefreshQueued = 0;
static int gPS5SmbHadGamesBeforeLoad = 0;
static unsigned int gPS5SmbAutoStartFrame = 0;
static unsigned int gPS5SmbManualRefreshFrame = 0;
static unsigned int gPS5SmbRetryFrame = 0;
volatile int gPS5SmbUiLoading = 0;

volatile int gPS5AppsLoading = 0;
int gPS5AppsScanned = 0;
int gPS5AppsSelected = 0;
int gPS5AppsItemCount = 0;
int gPS5AppsGroupCount = 0;
ps5_app_item_t gPS5Apps[PS5_APPS_MAX_ITEMS];
ps5_app_group_t gPS5AppGroups[PS5_APPS_MAX_GROUPS];
static volatile int gPS5AppsScanQueued = 0;

static void ps5CheckSmbConnectionTask(void);
int gPS5SmbDialogState = 0; // 0 = hidden, 1 = confirm, 2 = loading, 3 = info, 4 = error
int gPS5SmbDialogFocus = 1; // 1 = Yes, 0 = No
char gPS5SmbDialogMessage[128] = "";

int gPS5SmbCheckDialogState = 0; // 0 = hidden, 1 = testing, 2 = success, 3 = error
char gPS5SmbCheckDialogMessage[256] = "";

static void ps5LoadSmbGamesTask(void);
extern int bdmIsDeviceLoading(void);

static const char *ps5ControllerLogSteps[] = {
    "IDLE",
    "DPAD_UP",
    "DPAD_DOWN",
    "DPAD_LEFT",
    "DPAD_RIGHT",
    "A_OR_CROSS",
    "B_OR_CIRCLE",
    "X_OR_SQUARE",
    "Y_OR_TRIANGLE",
    "LB_L1",
    "RB_R1",
    "LT_L2",
    "RT_R2",
    "VIEW_SELECT",
    "MENU_START",
    "L3",
    "R3",
    "LEFT_STICK_MOVE",
    "RIGHT_STICK_MOVE",
    NULL};

#define XBOXUSB_BIND_RPC_ID 0x18E38791
#define XBOXUSB_GET_RAW     1
#define XBOXUSB_GET_DATA    2

static SifRpcClientData_t gXboxUsbRpc;
static u8 gXboxUsbRpcReady = 0;
static u8 gXboxUsbRpcBuf[72] __attribute__((aligned(64)));
static u8 gXboxUsbDataRpcBuf[18] __attribute__((aligned(64)));

static int ps5BindXboxUsbRpc(void)
{
#ifdef PADEMU
    int retries;

    if (gXboxUsbRpcReady)
        return 1;

    memset(&gXboxUsbRpc, 0, sizeof(gXboxUsbRpc));
    for (retries = 0; retries < 8; retries++) {
        if (SifBindRpc(&gXboxUsbRpc, XBOXUSB_BIND_RPC_ID, 0) >= 0 && gXboxUsbRpc.server != NULL) {
            gXboxUsbRpcReady = 1;
            return 1;
        }
        nopdelay();
    }
#endif

    return 0;
}

static int ps5GetXboxUsbRawReport(u8 *raw)
{
#ifdef PADEMU
    if (!ps5BindXboxUsbRpc())
        return 0;

    memset(gXboxUsbRpcBuf, 0, sizeof(gXboxUsbRpcBuf));
    if (SifCallRpc(&gXboxUsbRpc, XBOXUSB_GET_RAW, 0, gXboxUsbRpcBuf, 1, gXboxUsbRpcBuf, sizeof(gXboxUsbRpcBuf), NULL, NULL) != 0)
        return 0;

    memcpy(raw, gXboxUsbRpcBuf, sizeof(gXboxUsbRpcBuf));
    return 1;
#else
    return 0;
#endif
}

static int ps5GetXboxUsbDs2Report(u8 *ds2)
{
#ifdef PADEMU
    if (!ps5BindXboxUsbRpc())
        return 0;

    memset(gXboxUsbDataRpcBuf, 0, sizeof(gXboxUsbDataRpcBuf));
    if (SifCallRpc(&gXboxUsbRpc, XBOXUSB_GET_DATA, 0, gXboxUsbDataRpcBuf, 1, gXboxUsbDataRpcBuf, sizeof(gXboxUsbDataRpcBuf), NULL, NULL) != 0)
        return 0;

    memcpy(ds2, gXboxUsbDataRpcBuf, sizeof(gXboxUsbDataRpcBuf));
    return 1;
#else
    return 0;
#endif
}

static int ps5AppsNameEndsWithElf(const char *name)
{
    int len;

    if (name == NULL)
        return 0;

    len = strlen(name);
    if (len < 4)
        return 0;

    name += len - 4;
    return name[0] == '.' &&
           (name[1] == 'e' || name[1] == 'E') &&
           (name[2] == 'l' || name[2] == 'L') &&
           (name[3] == 'f' || name[3] == 'F');
}

static const char *ps5AppsBaseName(const char *path)
{
    const char *base = path;
    const char *p;

    if (path == NULL)
        return "";

    for (p = path; *p; p++) {
        if (*p == '/' || *p == '\\' || *p == ':')
            base = p + 1;
    }

    return base;
}

static void ps5AppsJoinPath(char *dst, int dstSize, const char *dir, const char *name)
{
    int len;

    if (dstSize <= 0)
        return;

    len = strlen(dir);
    if (len > 0 && (dir[len - 1] == '/' || dir[len - 1] == '\\' || (dir[len - 1] == ':' && strncmp(dir, "pfs", 3) == 0)))
        snprintf(dst, dstSize, "%s%s", dir, name);
    else
        snprintf(dst, dstSize, "%s/%s", dir, name);
}

static int ps5AppsFindOrAddGroup(const char *title)
{
    int i;

    for (i = 0; i < gPS5AppsGroupCount; i++) {
        if (strcmp(gPS5AppGroups[i].title, title) == 0)
            return i;
    }

    if (gPS5AppsGroupCount >= PS5_APPS_MAX_GROUPS)
        return -1;

    i = gPS5AppsGroupCount++;
    memset(&gPS5AppGroups[i], 0, sizeof(gPS5AppGroups[i]));
    strncpy(gPS5AppGroups[i].title, title, sizeof(gPS5AppGroups[i].title) - 1);
    return i;
}

static void ps5AppsAddItem(int group, const char *path)
{
    ps5_app_item_t *app;

    if (group < 0 || gPS5AppsItemCount >= PS5_APPS_MAX_ITEMS || path == NULL || path[0] == '\0')
        return;

    app = &gPS5Apps[gPS5AppsItemCount++];
    memset(app, 0, sizeof(*app));
    strncpy(app->path, path, sizeof(app->path) - 1);
    strncpy(app->name, ps5AppsBaseName(path), sizeof(app->name) - 1);
    app->group = group;
    gPS5AppGroups[group].count++;
}

static void ps5AppsScanDirectory(const char *dirPath, int group, int depth)
{
    DIR *dir;
    struct dirent *entry;

    if (dirPath == NULL || depth > PS5_APPS_SCAN_MAX_DEPTH || gPS5AppsItemCount >= PS5_APPS_MAX_ITEMS)
        return;

    dir = opendir(dirPath);
    if (dir == NULL)
        return;

    while ((entry = readdir(dir)) != NULL && gPS5AppsItemCount < PS5_APPS_MAX_ITEMS) {
        char path[PS5_APPS_PATH_MAX];

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        ps5AppsJoinPath(path, sizeof(path), dirPath, entry->d_name);

        if (entry->d_type == DT_DIR)
            ps5AppsScanDirectory(path, group, depth + 1);
        else if (ps5AppsNameEndsWithElf(entry->d_name))
            ps5AppsAddItem(group, path);
    }

    closedir(dir);
}

static void ps5AppsScanRoot(const char *title, const char *root)
{
    DIR *dir;
    int group;

    if (root == NULL || root[0] == '\0')
        return;

    dir = opendir(root);
    if (dir == NULL)
        return;
    closedir(dir);

    group = ps5AppsFindOrAddGroup(title);
    if (group >= 0)
        ps5AppsScanDirectory(root, group, 0);
}

static const char *ps5AppsBdmDeviceLabel(item_list_t *support)
{
    bdm_device_data_t *bdmData;

    if (support == NULL || support->priv == NULL)
        return "USB";

    bdmData = (bdm_device_data_t *)support->priv;
    switch (bdmData->bdmDeviceType) {
        case BDM_TYPE_SDC:
            return "MX4SIO";
        case BDM_TYPE_ATA:
            return "Internal HDD";
        case BDM_TYPE_ILINK:
            return "iLink";
        case BDM_TYPE_USB:
        default:
            return "USB";
    }
}

static void ps5AppsScanTask(void)
{
    int i;

    gPS5AppsLoading = 1;
    gPS5AppsScanned = 1;
    gPS5AppsItemCount = 0;
    gPS5AppsGroupCount = 0;
    gPS5AppsSelected = 0;
    memset(gPS5Apps, 0, sizeof(gPS5Apps));
    memset(gPS5AppGroups, 0, sizeof(gPS5AppGroups));

    ps5AppsScanRoot("Memory Card 1", "mc0:/");
    ps5AppsScanRoot("Memory Card 2", "mc1:/");

    for (i = 0; i < MAX_BDM_DEVICES; i++) {
        item_list_t *support = bdmGetDeviceObject(i);
        char root[16];

        snprintf(root, sizeof(root), "mass%d:/", i);
        if (support != NULL && support->enabled)
            ps5AppsScanRoot(ps5AppsBdmDeviceLabel(support), root);
        else
            ps5AppsScanRoot("USB", root);
    }

    ps5AppsScanRoot("Internal HDD", "pfs0:");

    if (gPS5AppsSelected >= gPS5AppsItemCount)
        gPS5AppsSelected = gPS5AppsItemCount > 0 ? gPS5AppsItemCount - 1 : 0;

    gPS5AppsLoading = 0;
    gPS5AppsScanQueued = 0;
}

void ps5QueueAppsScan(int force)
{
    if (gPS5AppsLoading || gPS5AppsScanQueued)
        return;

    if (!force && gPS5AppsScanned)
        return;

    gPS5AppsScanQueued = 1;
    gPS5AppsLoading = 1;
    if (ioPutRequest(IO_CUSTOM_SIMPLEACTION, &ps5AppsScanTask) < 0) {
        gPS5AppsLoading = 0;
        gPS5AppsScanQueued = 0;
    }
}

void ps5LaunchSelectedApp(void)
{
    if (gPS5AppsLoading || gPS5AppsItemCount <= 0 || gPS5AppsSelected < 0 || gPS5AppsSelected >= gPS5AppsItemCount)
        return;

    ps5DrawAppLaunchTransition();
    sysExecElf(gPS5Apps[gPS5AppsSelected].path);
}

static void ps5FormatControllerBytes(char *dst, int dstSize, const u8 *data, int len)
{
    int i;
    int pos = 0;

    dst[0] = '\0';
    if (len > 64)
        len = 64;

    for (i = 0; i < len && pos < dstSize - 4; i++)
        pos += snprintf(dst + pos, dstSize - pos, "%02X%s", data[i], i == len - 1 ? "" : " ");
}

static int ps5ReadControllerRaw(u8 *raw)
{
    if (ps5GetXboxUsbRawReport(raw)) {
        unsigned int vid = raw[0] | (raw[1] << 8);
        unsigned int pid = raw[2] | (raw[3] << 8);
        unsigned int status = raw[4];
        unsigned int reportLen = raw[5];
        unsigned int ep = raw[6];
        unsigned int packet = raw[7];
        char bytes[170];

        ps5FormatControllerBytes(bytes, sizeof(bytes), raw + 8, reportLen ? reportLen : 64);

        if (vid || pid)
            snprintf(gPS5ControllerLogDevice, sizeof(gPS5ControllerLogDevice), "DEVICE VID=%04X PID=%04X STATUS=%02X EP_IN=%02X PACKET=%u REPORT_LEN=%u", vid, pid, status, ep, packet, reportLen);
        else
            snprintf(gPS5ControllerLogDevice, sizeof(gPS5ControllerLogDevice), "DEVICE: not detected");

        snprintf(gPS5ControllerLogLatest, sizeof(gPS5ControllerLogLatest), "Latest: %s", bytes[0] ? bytes : "no report");
        return vid || pid || reportLen;
    }
    memset(raw, 0, 72);
    snprintf(gPS5ControllerLogDevice, sizeof(gPS5ControllerLogDevice), "DEVICE: Xbox USB logger not ready");
    snprintf(gPS5ControllerLogLatest, sizeof(gPS5ControllerLogLatest), "Latest: no report");
    return 0;
}

static void ps5AppendPressed(char *dst, int dstSize, int *first, const char *name)
{
    int len = strlen(dst);

    if (len >= dstSize - 2)
        return;

    snprintf(dst + len, dstSize - len, "%s%s", *first ? "" : " + ", name);
    *first = 0;
}

static void ps5UpdateControllerBridgeStatus(const u8 *data)
{
    unsigned int padData = 0;
    u16 lt, rt;
    int lx, ly;

    if (data[0] != 0x20) {
        gPS5ControllerLogBridgePadData = 0;
        if (!gPS5ControllerLogVisible)
            gPS5XboxNavPadData = 0;
        snprintf(gPS5ControllerLogBridge, sizeof(gPS5ControllerLogBridge), "Bridge: ignored non-input packet");
        return;
    }

    if (data[5] & 0x01)
        padData |= PAD_UP;
    if (data[5] & 0x02)
        padData |= PAD_DOWN;
    if (data[5] & 0x04)
        padData |= PAD_LEFT;
    if (data[5] & 0x08)
        padData |= PAD_RIGHT;
    if (data[5] & 0x10)
        padData |= PAD_L1;
    if (data[5] & 0x20)
        padData |= PAD_R1;
    if (data[5] & 0x40)
        padData |= PAD_L3;
    if (data[5] & 0x80)
        padData |= PAD_R3;

    if (data[4] & 0x04)
        padData |= PAD_START;
    if (data[4] & 0x08)
        padData |= PAD_SELECT;
    if (data[4] & 0x10)
        padData |= PAD_CROSS;
    if (data[4] & 0x20)
        padData |= PAD_CIRCLE;
    if (data[4] & 0x40)
        padData |= PAD_SQUARE;
    if (data[4] & 0x80)
        padData |= PAD_TRIANGLE;

    lt = data[6] | (data[7] << 8);
    rt = data[8] | (data[9] << 8);
    if (lt > 0)
        padData |= PAD_L2;
    if (rt > 0)
        padData |= PAD_R2;

    lx = (short)((data[11] << 8) | data[10]);
    ly = (short)((data[13] << 8) | data[12]);
    if (lx < -8192)
        padData |= PAD_LEFT;
    else if (lx > 8192)
        padData |= PAD_RIGHT;
    if (ly < -8192)
        padData |= PAD_UP;
    else if (ly > 8192)
        padData |= PAD_DOWN;

    gPS5ControllerLogBridgePadData = padData;
    if (!gPS5ControllerLogVisible)
        gPS5XboxNavPadData = padData;
    if (padData)
        snprintf(gPS5ControllerLogBridge, sizeof(gPS5ControllerLogBridge), "Bridge: launcher paddata=0x%04X%s", padData & 0xFFFF, gPS5ControllerLogNavTestEnabled ? " (nav on)" : "");
    else
        snprintf(gPS5ControllerLogBridge, sizeof(gPS5ControllerLogBridge), "Bridge: valid input packet, neutral%s", gPS5ControllerLogNavTestEnabled ? " (nav on)" : "");
}

static void ps5UpdateControllerLiveStatus(void)
{
    u8 raw[72];
    u8 ds2[18];
    u8 *data;
    int first = 1;
    int hasSignal = 0;
    u16 lt, rt;
    int lx, ly, rx, ry;

    if (!ps5ReadControllerRaw(raw)) {
        gPS5ControllerLogBridgePadData = 0;
        if (!gPS5ControllerLogVisible)
            gPS5XboxNavPadData = 0;
        snprintf(gPS5ControllerLogPressed, sizeof(gPS5ControllerLogPressed), "Pressed: no input found");
        snprintf(gPS5ControllerLogBridge, sizeof(gPS5ControllerLogBridge), "Bridge: no raw report");
        snprintf(gPS5ControllerLogRaw, sizeof(gPS5ControllerLogRaw), "Raw: no input");
        snprintf(gPS5ControllerLogDs2, sizeof(gPS5ControllerLogDs2), "DS2: no data");
        return;
    }

    data = raw + 8;
    ps5UpdateControllerBridgeStatus(data);
    if (data[0] != 0x20) {
        if (data[0] || data[1] || data[2] || data[3])
            snprintf(gPS5ControllerLogPressed, sizeof(gPS5ControllerLogPressed), "Pressed: non-input packet b0-b9=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                     data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], data[8], data[9]);
        else
            snprintf(gPS5ControllerLogPressed, sizeof(gPS5ControllerLogPressed), "Pressed: no input found");
        snprintf(gPS5ControllerLogRaw, sizeof(gPS5ControllerLogRaw), "Raw: packet b0-b9=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                 data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], data[8], data[9]);
        snprintf(gPS5ControllerLogDs2, sizeof(gPS5ControllerLogDs2), "DS2: waiting for input packet");
        return;
    }

    snprintf(gPS5ControllerLogPressed, sizeof(gPS5ControllerLogPressed), "Pressed: ");

    if (data[5] & 0x01)
        ps5AppendPressed(gPS5ControllerLogPressed, sizeof(gPS5ControllerLogPressed), &first, "Dpad Up");
    if (data[5] & 0x02)
        ps5AppendPressed(gPS5ControllerLogPressed, sizeof(gPS5ControllerLogPressed), &first, "Dpad Down");
    if (data[5] & 0x04)
        ps5AppendPressed(gPS5ControllerLogPressed, sizeof(gPS5ControllerLogPressed), &first, "Dpad Left");
    if (data[5] & 0x08)
        ps5AppendPressed(gPS5ControllerLogPressed, sizeof(gPS5ControllerLogPressed), &first, "Dpad Right");
    if (data[5] & 0x10)
        ps5AppendPressed(gPS5ControllerLogPressed, sizeof(gPS5ControllerLogPressed), &first, "L1/LB");
    if (data[5] & 0x20)
        ps5AppendPressed(gPS5ControllerLogPressed, sizeof(gPS5ControllerLogPressed), &first, "R1/RB");
    if (data[5] & 0x40)
        ps5AppendPressed(gPS5ControllerLogPressed, sizeof(gPS5ControllerLogPressed), &first, "L3");
    if (data[5] & 0x80)
        ps5AppendPressed(gPS5ControllerLogPressed, sizeof(gPS5ControllerLogPressed), &first, "R3");

    if (data[4] & 0x04)
        ps5AppendPressed(gPS5ControllerLogPressed, sizeof(gPS5ControllerLogPressed), &first, "Start/Menu");
    if (data[4] & 0x08)
        ps5AppendPressed(gPS5ControllerLogPressed, sizeof(gPS5ControllerLogPressed), &first, "Select/View");
    if (data[4] & 0x10)
        ps5AppendPressed(gPS5ControllerLogPressed, sizeof(gPS5ControllerLogPressed), &first, "Cross/A");
    if (data[4] & 0x20)
        ps5AppendPressed(gPS5ControllerLogPressed, sizeof(gPS5ControllerLogPressed), &first, "Circle/B");
    if (data[4] & 0x40)
        ps5AppendPressed(gPS5ControllerLogPressed, sizeof(gPS5ControllerLogPressed), &first, "Square/X");
    if (data[4] & 0x80)
        ps5AppendPressed(gPS5ControllerLogPressed, sizeof(gPS5ControllerLogPressed), &first, "Triangle/Y");

    lt = data[6] | (data[7] << 8);
    rt = data[8] | (data[9] << 8);
    if (lt > 0)
        ps5AppendPressed(gPS5ControllerLogPressed, sizeof(gPS5ControllerLogPressed), &first, "L2/LT");
    if (rt > 0)
        ps5AppendPressed(gPS5ControllerLogPressed, sizeof(gPS5ControllerLogPressed), &first, "R2/RT");

    lx = (short)((data[11] << 8) | data[10]);
    ly = (short)((data[13] << 8) | data[12]);
    rx = (short)((data[15] << 8) | data[14]);
    ry = (short)((data[17] << 8) | data[16]);
    snprintf(gPS5ControllerLogRaw, sizeof(gPS5ControllerLogRaw), "Raw: b4=%02X b5=%02X LT=%u RT=%u LX=%d LY=%d RX=%d RY=%d",
             data[4], data[5], lt, rt, lx, ly, rx, ry);
    if (ps5GetXboxUsbDs2Report(ds2)) {
        u16 buttons = ds2[0] | (ds2[1] << 8);
        snprintf(gPS5ControllerLogDs2, sizeof(gPS5ControllerLogDs2), "DS2: btn=%04X LX=%u LY=%u RX=%u RY=%u L2=%u R2=%u",
                 buttons, ds2[4], ds2[5], ds2[2], ds2[3], ds2[16], ds2[17]);
    } else {
        snprintf(gPS5ControllerLogDs2, sizeof(gPS5ControllerLogDs2), "DS2: data not ready");
    }
    if (lx < -8192 || lx > 8192 || ly < -8192 || ly > 8192)
        ps5AppendPressed(gPS5ControllerLogPressed, sizeof(gPS5ControllerLogPressed), &first, "Left Stick");
    if (rx < -8192 || rx > 8192 || ry < -8192 || ry > 8192)
        ps5AppendPressed(gPS5ControllerLogPressed, sizeof(gPS5ControllerLogPressed), &first, "Right Stick");

    if (first) {
        hasSignal = data[4] || data[5] || data[6] || data[7] || data[8] || data[9] ||
                    lx < -8192 || lx > 8192 || ly < -8192 || ly > 8192 ||
                    rx < -8192 || rx > 8192 || ry < -8192 || ry > 8192;
        if (hasSignal)
            snprintf(gPS5ControllerLogPressed, sizeof(gPS5ControllerLogPressed), "Pressed: signal b0-b9=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                     data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], data[8], data[9]);
        else
            snprintf(gPS5ControllerLogPressed, sizeof(gPS5ControllerLogPressed), "Pressed: no input found");
    }
}

static void ps5PollXboxNavigation(void)
{
    u8 raw[72];

    gPS5XboxNavEnabled = (gPS5TempControllerType == 3 || guiGameGetPadEmuGlobalController() == 3);
    if (!gPS5XboxNavEnabled || gPS5ControllerLogVisible) {
        if (!gPS5ControllerLogVisible)
            gPS5XboxNavPadData = 0;
        return;
    }

    if (!ps5ReadControllerRaw(raw)) {
        gPS5XboxNavPadData = 0;
        return;
    }

    ps5UpdateControllerBridgeStatus(raw + 8);
}

static void ps5CaptureControllerLogStep(void)
{
    u8 raw[72];
    unsigned int vid, pid, reportLen;
    char bytes[170];

    ps5ReadControllerRaw(raw);
    vid = raw[0] | (raw[1] << 8);
    pid = raw[2] | (raw[3] << 8);
    reportLen = raw[5] ? raw[5] : 64;
    ps5FormatControllerBytes(bytes, sizeof(bytes), raw + 8, reportLen);

    snprintf(gPS5ControllerLogLines[gPS5ControllerLogStep], sizeof(gPS5ControllerLogLines[gPS5ControllerLogStep]),
             "%s:\nVID=%04X PID=%04X LEN=%u\n%s",
             ps5ControllerLogSteps[gPS5ControllerLogStep], vid, pid, reportLen, bytes);

    if (gPS5ControllerLogStep + 1 > gPS5ControllerLogCaptured)
        gPS5ControllerLogCaptured = gPS5ControllerLogStep + 1;

    if (ps5ControllerLogSteps[gPS5ControllerLogStep + 1] != NULL)
        gPS5ControllerLogStep++;

    snprintf(gPS5ControllerLogStatus, sizeof(gPS5ControllerLogStatus), "Captured. Cross: next  Square: save  Circle: close");
}

static int ps5WriteControllerLogPath(const char *path)
{
    int fd;
    int i;
    char line[256];
    char writablePath[64];

    snprintf(writablePath, sizeof(writablePath), "%s", path);
    fd = openFile(writablePath, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0)
        return 0;

    snprintf(line, sizeof(line), "PS2L Controller Raw Log\n%s\n\n", gPS5ControllerLogDevice);
    write(fd, line, strlen(line));

    for (i = 0; ps5ControllerLogSteps[i] != NULL; i++) {
        if (gPS5ControllerLogLines[i][0]) {
            write(fd, gPS5ControllerLogLines[i], strlen(gPS5ControllerLogLines[i]));
            write(fd, "\n\n", 2);
        }
    }

    close(fd);
    return 1;
}

static void ps5SaveControllerLog(void)
{
    mkdir("mc0:/PS2L", 0777);
    if (ps5WriteControllerLogPath("mc0:/PS2L/CTRL_LOG.TXT")) {
        snprintf(gPS5ControllerLogStatus, sizeof(gPS5ControllerLogStatus), "Saved: mc0:/PS2L/CTRL_LOG.TXT");
        return;
    }

    mkdir("mc1:/PS2L", 0777);
    if (ps5WriteControllerLogPath("mc1:/PS2L/CTRL_LOG.TXT")) {
        snprintf(gPS5ControllerLogStatus, sizeof(gPS5ControllerLogStatus), "Saved: mc1:/PS2L/CTRL_LOG.TXT");
        return;
    }

    snprintf(gPS5ControllerLogStatus, sizeof(gPS5ControllerLogStatus), "Save failed. Check memory card.");
}

static void ps5LoadSmbGamesTask(void)
{
    item_list_t *eth = ethGetObject(0);

    if (eth != NULL) {
        if (eth->enabled && !ethIsNetworkLinkUp()) {
            ethClearGameList();
            gNetworkStartup = ERROR_ETH_LINK_FAIL;
            gPS5SmbLoadStatus = 2;
            return;
        }
        if (eth->itemInit != NULL && (!eth->enabled || gNetworkStartup != 0))
            eth->itemInit(eth);
        if (!ethIsNetworkLinkUp()) {
            ethClearGameList();
            gNetworkStartup = ERROR_ETH_LINK_FAIL;
            gPS5SmbLoadStatus = 2;
            return;
        }
        if (eth->itemUpdate != NULL)
            eth->itemUpdate(eth);
    }

    gPS5SmbLoadStatus = 2;
}

static void ps5QueueSmbLoad(void)
{
    item_list_t *eth = ethGetObject(0);

    if (gPS5SmbPromptState == 2 || gPS5SmbLoadStatus == 1)
        return;

    if (eth == NULL) {
        gPS5SmbPromptState = 1;
        gPS5SmbDialogState = 0;
        gPS5SmbUiLoading = 0;
        gPS5SmbHadGamesBeforeLoad = 0;
        return;
    }

    gPS5SmbHadGamesBeforeLoad = eth->itemGetCount != NULL ? eth->itemGetCount(eth) > 0 : 0;

    if (gNetworkStartup == 0) {
        gNetworkStartup = ERROR_ETH_SMB_CONN;
    }

    gPS5SmbPromptState = 2;
    gPS5SmbLoadStatus = 1;
    gPS5SmbRefreshQueued = 0;
    gPS5SmbUiLoading = 1;
    gPS5SmbRetryFrame = 0;
    gPS5SmbDialogState = 0;
    if (ioPutRequest(IO_CUSTOM_SIMPLEACTION, &ps5LoadSmbGamesTask) < 0) {
        gPS5SmbPromptState = 1;
        gPS5SmbLoadStatus = 0;
        gPS5SmbRefreshQueued = 0;
        gPS5SmbUiLoading = 0;
        gPS5SmbHadGamesBeforeLoad = 0;
    }
}

static int ps5SmbRefreshIsBusy(void)
{
    return gPS5SmbPromptState == 2 || gPS5SmbLoadStatus == 1 || gPS5SmbRefreshQueued || gPS5SmbUiLoading || ioHasPendingRequests();
}

static int ps5SmbKnownLinkDown(void)
{
    item_list_t *eth = ethGetObject(0);

    return eth != NULL && eth->enabled && gNetworkStartup == ERROR_ETH_LINK_FAIL && !ethIsNetworkLinkUp();
}

static int ps5SmbOperationActive(void)
{
    return gPS5SmbPromptState == 2 || gPS5SmbLoadStatus == 1 || gPS5SmbRefreshQueued || gPS5SmbUiLoading;
}

static int ps5SmbConsumeInputWhileBusy(void)
{
    return 0;
}

static int ps5CanStartSmbLoad(void)
{
    extern int guiFrameId;

    if (bdmIsDeviceLoading() || ioHasPendingRequests())
        return 0;

    if (gPS5SmbRetryFrame && guiFrameId - (int)gPS5SmbRetryFrame < 180)
        return 0;

    if (selected_item != NULL && selected_item->item != NULL && selected_item->item->current != NULL)
        return 1;

    if (gPS5SmbAutoStartFrame == 0)
        gPS5SmbAutoStartFrame = guiFrameId;

    return guiFrameId >= (int)gPS5SmbAutoStartFrame && guiFrameId - (int)gPS5SmbAutoStartFrame > 180;
}

static void ps5UpdateSmbDialog(void)
{
    extern int guiFrameId;

    if (gPS5SmbPromptState != 2 || gPS5SmbLoadStatus == 1)
        return;

    if (gPS5SmbLoadStatus == 2 && gNetworkStartup == 0 && !gPS5SmbRefreshQueued) {
        oplRefreshMergedGameList();
        gPS5SmbRefreshQueued = 1;
        return;
    }

    if (gPS5SmbRefreshQueued && ioHasPendingRequests())
        return;

    if (gPS5SmbLoadStatus == 2 && gNetworkStartup != 0) {
        if (ioHasPendingRequests())
            return;

        if (gPS5SmbHadGamesBeforeLoad && !gPS5SmbRefreshQueued) {
            oplRefreshMergedGameList();
            gPS5SmbRefreshQueued = 1;
            return;
        }

        gPS5SmbDialogState = 0;
        int shouldRetry = 0;
        if (gPS5SmbManualRefreshFrame != 0) {
            shouldRetry = 0;
            gPS5SmbManualRefreshFrame = 0;
        }
        gPS5SmbPromptState = shouldRetry ? 0 : 1;
        gPS5SmbLoadStatus = 0;
        gPS5SmbRefreshQueued = 0;
        gPS5SmbHadGamesBeforeLoad = 0;
        gPS5SmbUiLoading = 0;
        gPS5SmbRetryFrame = shouldRetry ? guiFrameId : 0;
        return;
    }

    gPS5SmbDialogState = 0;
    gPS5SmbPromptState = 1;
    gPS5SmbLoadStatus = 0;
    gPS5SmbRefreshQueued = 0;
    gPS5SmbHadGamesBeforeLoad = 0;
    gPS5SmbUiLoading = 0;
    gPS5SmbRetryFrame = 0;
    gPS5SmbManualRefreshFrame = 0;
}

static void ps5CheckSmbConnectionTask(void)
{
    item_list_t *eth = ethGetObject(0);
    if (eth != NULL) {
        if (eth->itemCleanUp != NULL)
            eth->itemCleanUp(eth, 0);
        gNetworkStartup = ERROR_ETH_NOT_STARTED;
        if (eth->itemInit != NULL)
            eth->itemInit(eth);
    }
}

static void ps5UpdateSmbCheckDialog(void)
{
    if (gPS5SmbCheckDialogState != 1)
        return;

    if (ioHasPendingRequests())
        return;

    if (gNetworkStartup == 0) {
        gPS5SmbCheckDialogState = 2; // Success
        snprintf(gPS5SmbCheckDialogMessage, sizeof(gPS5SmbCheckDialogMessage),
                 "Connection successful!\n\nFound %d games on the SMB share.", ethGetObject(0) ? ethGetObject(0)->itemGetCount(ethGetObject(0)) : 0);
    } else {
        gPS5SmbCheckDialogState = 3; // Error
        char errorDetail[128];
        switch (gNetworkStartup) {
            case ERROR_ETH_MODULE_NETIF_FAILURE:
                strncpy(errorDetail, "Network interface failure.", sizeof(errorDetail));
                break;
            case ERROR_ETH_MODULE_SMBMAN_FAILURE:
                strncpy(errorDetail, "Failed to load SMB modules.", sizeof(errorDetail));
                break;
            case ERROR_ETH_SMB_CONN:
                strncpy(errorDetail, "Cannot connect to SMB server. Check IP and port.", sizeof(errorDetail));
                break;
            case ERROR_ETH_SMB_LOGON:
                strncpy(errorDetail, "Logon failed. Check username and password.", sizeof(errorDetail));
                break;
            case ERROR_ETH_SMB_OPENSHARE:
                strncpy(errorDetail, "Cannot open share. Check share name.", sizeof(errorDetail));
                break;
            case ERROR_ETH_SMB_LISTSHARES:
                strncpy(errorDetail, "Cannot list shares on server.", sizeof(errorDetail));
                break;
            case ERROR_ETH_SMB_LISTGAMES:
                strncpy(errorDetail, "Cannot list games in share.", sizeof(errorDetail));
                break;
            case ERROR_ETH_LINK_FAIL:
                strncpy(errorDetail, "Ethernet link down. Check cable.", sizeof(errorDetail));
                break;
            case ERROR_ETH_DHCP_FAIL:
                strncpy(errorDetail, "DHCP configuration failed.", sizeof(errorDetail));
                break;
            default:
                snprintf(errorDetail, sizeof(errorDetail), "Unknown error code: %d", gNetworkStartup);
                break;
        }
        snprintf(gPS5SmbCheckDialogMessage, sizeof(gPS5SmbCheckDialogMessage),
                 "Connection failed!\n\nDetails: %s", errorDetail);
    }
}

static int ps5HandleSmbCheckDialogInput(void)
{
    ps5UpdateSmbCheckDialog();
    if (gPS5SmbCheckDialogState == 0)
        return 0;

    if (gPS5SmbCheckDialogState == 2 || gPS5SmbCheckDialogState == 3) {
        if (getKeyOn(KEY_CIRCLE)) {
            sfxPlay(SFX_CANCEL);
            gPS5SmbCheckDialogState = 0;
            oplRefreshMergedGameList();
        }
    }
    return 1;
}

static int ps5HandleSmbDialogInput(void)
{
    extern int gSelectButton;

    ps5UpdateSmbDialog();
    if (gPS5SmbDialogState == 0)
        return 0;

    if (gPS5SmbDialogState == 1) {
        if (getKeyOn(KEY_LEFT) || getKeyOn(KEY_RIGHT)) {
            sfxPlay(SFX_CURSOR);
            gPS5SmbDialogFocus = !gPS5SmbDialogFocus;
        }
        if (getKeyOn(KEY_CROSS) || getKeyOn(gSelectButton)) {
            if (gPS5SmbDialogFocus) {
                sfxPlay(SFX_CONFIRM);
                ps5QueueSmbLoad();
            } else {
                sfxPlay(SFX_CANCEL);
                gPS5SmbPromptState = 1;
                gPS5SmbDialogState = 0;
            }
        } else if (getKeyOn(KEY_CIRCLE) || getKeyOn(KEY_TRIANGLE)) {
            sfxPlay(SFX_CANCEL);
            gPS5SmbPromptState = 1;
            gPS5SmbDialogState = 0;
        }
    } else if (gPS5SmbDialogState == 3 || gPS5SmbDialogState == 4) {
        if (getKeyOn(KEY_CROSS) || getKeyOn(KEY_CIRCLE) || getKeyOn(KEY_TRIANGLE) || getKeyOn(gSelectButton)) {
            sfxPlay(SFX_CONFIRM);
            gPS5SmbDialogState = 0;
        }
    }

    return 1;
}

static void ps5CopySmbSettingsToTemp(void)
{
    snprintf(gPS5TempSmbIp, sizeof(gPS5TempSmbIp), "%d.%d.%d.%d", pc_ip[0], pc_ip[1], pc_ip[2], pc_ip[3]);
    strncpy(gPS5TempSmbName, gPCShareNBAddress, sizeof(gPS5TempSmbName) - 1);
    gPS5TempSmbName[sizeof(gPS5TempSmbName) - 1] = '\0';
    strncpy(gPS5TempSmbShare, gPCShareName, sizeof(gPS5TempSmbShare) - 1);
    gPS5TempSmbShare[sizeof(gPS5TempSmbShare) - 1] = '\0';
    strncpy(gPS5TempSmbUser, gPCUserName, sizeof(gPS5TempSmbUser) - 1);
    gPS5TempSmbUser[sizeof(gPS5TempSmbUser) - 1] = '\0';
    strncpy(gPS5TempSmbPassword, gPCPassword, sizeof(gPS5TempSmbPassword) - 1);
    gPS5TempSmbPassword[sizeof(gPS5TempSmbPassword) - 1] = '\0';
    strncpy(gPS5TempSmbPrefix, gETHPrefix, sizeof(gPS5TempSmbPrefix) - 1);
    gPS5TempSmbPrefix[sizeof(gPS5TempSmbPrefix) - 1] = '\0';
    gPS5TempEthEnabled = gETHStartMode != START_MODE_DISABLED;
    gPS5TempSmbAddressType = gPCShareAddressIsNetBIOS ? 1 : 0;
    gPS5TempSmbDhcp = ps2_ip_use_dhcp ? 1 : 0;
    gPS5TempSmbPort = gPCPort > 0 ? gPCPort : 445;
    gPS5TempSmbCache = smbCacheSize;
}

static void ps5ApplyTempSmbSettings(void)
{
    int ip0 = 0, ip1 = 0, ip2 = 0, ip3 = 0;

    if (sscanf(gPS5TempSmbIp, "%d.%d.%d.%d", &ip0, &ip1, &ip2, &ip3) == 4) {
        pc_ip[0] = ip0;
        pc_ip[1] = ip1;
        pc_ip[2] = ip2;
        pc_ip[3] = ip3;
    }

    gETHStartMode = gPS5TempEthEnabled ? START_MODE_AUTO : START_MODE_DISABLED;
    gPCShareAddressIsNetBIOS = gPS5TempSmbAddressType ? 1 : 0;
    ps2_ip_use_dhcp = gPS5TempSmbDhcp ? 1 : 0;
    gPCPort = gPS5TempSmbPort > 0 ? gPS5TempSmbPort : 445;
    smbCacheSize = gPS5TempSmbCache;

    strncpy(gPCShareNBAddress, gPS5TempSmbName, sizeof(gPCShareNBAddress) - 1);
    gPCShareNBAddress[sizeof(gPCShareNBAddress) - 1] = '\0';
    strncpy(gPCShareName, gPS5TempSmbShare, sizeof(gPCShareName) - 1);
    gPCShareName[sizeof(gPCShareName) - 1] = '\0';
    strncpy(gPCUserName, gPS5TempSmbUser, sizeof(gPCUserName) - 1);
    gPCUserName[sizeof(gPCUserName) - 1] = '\0';
    strncpy(gPCPassword, gPS5TempSmbPassword, sizeof(gPCPassword) - 1);
    gPCPassword[sizeof(gPCPassword) - 1] = '\0';
    strncpy(gETHPrefix, gPS5TempSmbPrefix, sizeof(gETHPrefix) - 1);
    gETHPrefix[sizeof(gETHPrefix) - 1] = '\0';
}

static void ps5EditSmbText(char *value, int maxLen, int hideText, const char *title)
{
    char tmp[CONFIG_KEY_VALUE_LEN];

    strncpy(tmp, value, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    if (diaShowKeyb(tmp, maxLen, hideText, title)) {
        strncpy(value, tmp, maxLen - 1);
        value[maxLen - 1] = '\0';
    }
}

static void ps5EditSmbInt(int *value, int min, int max, const char *title)
{
    char tmp[16];
    int parsed;

    snprintf(tmp, sizeof(tmp), "%d", *value);
    if (diaShowKeyb(tmp, sizeof(tmp), 0, title)) {
        parsed = atoi(tmp);
        if (parsed < min)
            parsed = min;
        else if (parsed > max)
            parsed = max;
        *value = parsed;
    }
}

static void ps5StartSmbConnectionCheck(void)
{
    if (gPS5SmbCheckDialogState == 1)
        return;

    ps5ApplyTempSmbSettings();
    gPS5SmbCheckDialogState = 1;
    strncpy(gPS5SmbCheckDialogMessage, "Connecting to SMB server...", sizeof(gPS5SmbCheckDialogMessage));
    gPS5SmbCheckDialogMessage[sizeof(gPS5SmbCheckDialogMessage) - 1] = '\0';
    ioPutRequest(IO_CUSTOM_SIMPLEACTION, &ps5CheckSmbConnectionTask);
}

static void ps5SaveSettings(void)
{
    extern int gPS5TempVMode;
    extern int gVMode;
    extern unsigned int gPS5SaveNotifyFrame;
    extern unsigned int gPS5SaveBusyFrame;
    extern int guiFrameId;
    extern int gPS5UISound;
    extern int gPS5ShowCoverImages;
    extern int gPS5ShowGamesLogo;
    extern int gPS5SortMode;
    extern int gSelectButton;

    if (gVMode != gPS5TempVMode) {
        if (!guiConfirmVideoModeChange())
            return;

        int oldMode = gVMode;
        gVMode = gPS5TempVMode;
        applyConfig(-1, -1, 0);

        extern int guiConfirmVideoMode(void);
        if (guiConfirmVideoMode() == 0) {
            gVMode = oldMode;
            gPS5TempVMode = oldMode;
            applyConfig(-1, -1, 0);
            return;
        }
    }

    gPS5UISound = gPS5TempUISound;
    gPS5ShowCoverImages = gPS5TempShowCoverImages;
    gPS5ShowGamesLogo = gPS5TempShowGamesLogo;
    gPS5SortMode = gPS5TempSortMode;
    gSelectButton = gPS5TempSelectButton;
#ifdef PADEMU
    guiGameSetPadEmuGlobalController(gPS5TempControllerType);
#endif
    ps5ApplyTempSmbSettings();

    gPS5SavedUISound = gPS5UISound;
    gPS5SavedShowCoverImages = gPS5ShowCoverImages;
    gPS5SavedShowGamesLogo = gPS5ShowGamesLogo;
    gPS5SavedSortMode = gPS5SortMode;
    gPS5SavedSelectButton = gSelectButton;
#ifdef PADEMU
    gPS5SavedControllerType = gPS5TempControllerType;
#endif
    gPS5SavedVMode = gVMode;

    gPS5SaveBusyFrame = guiFrameId;
    saveConfigQuiet(CONFIG_OPL | CONFIG_NETWORK | CONFIG_GAME);
    gPS5SmbPromptState = 1;
    gPS5SmbLoadStatus = 0;
    gPS5SmbRefreshQueued = 0;
    gPS5SmbHadGamesBeforeLoad = 0;
    gPS5SmbUiLoading = 0;
    gPS5SmbRetryFrame = 0;
    gPS5SmbManualRefreshFrame = 0;
    gPS5SaveNotifyFrame = guiFrameId;
}

void menuHandleInputMenu()
{
    if (gPS5Mode) {
        extern int gPS5TempVMode;
        extern int gPS5SubSel;
        extern int gVMode;
        extern int gSelectButton;
        extern unsigned int gPS5SaveNotifyFrame;
        extern int guiFrameId;
        extern int gPS5ActiveTab;
        extern int gPS5UISound;
        extern int gPS5ShowCoverImages;
        extern int gPS5ShowGamesLogo;
        extern int gPS5SortMode;

        if (gPS5SavedVMode == -1) {
            gPS5SavedVMode = gVMode;
            gPS5TempVMode = gVMode;
            gPS5SavedSelectButton = gSelectButton;
            gPS5TempSelectButton = gSelectButton;
            gPS5SavedUISound = gPS5UISound;
            gPS5TempUISound = gPS5UISound;
            gPS5SavedShowCoverImages = gPS5ShowCoverImages;
            gPS5TempShowCoverImages = gPS5ShowCoverImages;
            gPS5SavedShowGamesLogo = gPS5ShowGamesLogo;
            gPS5TempShowGamesLogo = gPS5ShowGamesLogo;
            gPS5SavedSortMode = gPS5SortMode;
            gPS5TempSortMode = gPS5SortMode;
#ifdef PADEMU
            gPS5SavedControllerType = guiGameGetPadEmuGlobalController();
            gPS5TempControllerType = gPS5SavedControllerType;
#endif
            ps5CopySmbSettingsToTemp();
        }
        if (gPS5SubSel > 8)
            gPS5SubSel = 8;

        if (gPS5ControllerLogVisible) {
            ps5UpdateControllerLiveStatus();

            if (getKeyOn(KEY_CIRCLE)) {
                sfxPlay(SFX_CANCEL);
                gPS5ControllerLogVisible = 0;
                gPS5ControllerLogNavTestEnabled = 0;
                gPS5ControllerLogBridgePadData = 0;
                return;
            }
            if (getKeyOn(KEY_TRIANGLE)) {
                sfxPlay(SFX_CONFIRM);
                gPS5ControllerLogNavTestEnabled = !gPS5ControllerLogNavTestEnabled;
                gPS5ControllerLogBridgePadData = 0;
                snprintf(gPS5ControllerLogStatus, sizeof(gPS5ControllerLogStatus), "Nav test %s. Xbox input is active only on this screen.", gPS5ControllerLogNavTestEnabled ? "enabled" : "disabled");
                return;
            }
            if (getKeyOn(KEY_UP)) {
                sfxPlay(SFX_CURSOR);
                gPS5ControllerLogStep = gPS5ControllerLogStep > 0 ? gPS5ControllerLogStep - 1 : gPS5ControllerLogStep;
                return;
            }
            if (getKeyOn(KEY_DOWN)) {
                sfxPlay(SFX_CURSOR);
                if (ps5ControllerLogSteps[gPS5ControllerLogStep + 1] != NULL)
                    gPS5ControllerLogStep++;
                return;
            }
            if (getKeyOn(KEY_CROSS) || getKeyOn(gSelectButton)) {
                sfxPlay(SFX_CONFIRM);
                ps5CaptureControllerLogStep();
                return;
            }
            if (getKeyOn(KEY_SQUARE)) {
                sfxPlay(SFX_CONFIRM);
                ps5SaveControllerLog();
                return;
            }
            return;
        }

        if (ps5HandleSmbDialogInput()) {
            return;
        }
        if (ps5HandleSmbCheckDialogInput()) {
            return;
        }
        if (ps5SmbConsumeInputWhileBusy())
            return;

        if (gPS5CoverDownloadStatus == PS5_COVER_DOWNLOAD_PROMPT) {
            if (getKeyOn(KEY_UP) || getKeyOn(KEY_DOWN)) {
                extern int gPS5CoverDownloadMode;
                sfxPlay(SFX_CURSOR);
                gPS5CoverDownloadMode = (gPS5CoverDownloadMode == PS5_COVER_DOWNLOAD_MISSING) ? PS5_COVER_DOWNLOAD_FULL : PS5_COVER_DOWNLOAD_MISSING;
            }
            if (getKeyOn(KEY_CROSS) || getKeyOn(gSelectButton)) {
                extern int gPS5CoverDownloadMode;
                int downloadMode = gPS5CoverDownloadMode;
                sfxPlay(SFX_CONFIRM);
                gPS5CoverDownloadStatus = PS5_COVER_DOWNLOAD_IDLE;
                oplStartGameCoverDownload(downloadMode);
            }
            if (getKeyOn(KEY_CIRCLE)) {
                sfxPlay(SFX_CANCEL);
                gPS5CoverDownloadStatus = PS5_COVER_DOWNLOAD_IDLE;
            }
            return;
        }

        if (gPS5CoverDownloadStatus == PS5_COVER_DOWNLOAD_WIP) {
            if (getKeyOn(KEY_CIRCLE)) {
                sfxPlay(SFX_CANCEL);
                oplAbortGameCoverDownload();
            }
            return;
        }
        if (gPS5CoverDownloadStatus != PS5_COVER_DOWNLOAD_IDLE) {
            if (getKeyOn(KEY_CIRCLE)) {
                sfxPlay(SFX_CONFIRM);
                gPS5CoverDownloadStatus = PS5_COVER_DOWNLOAD_IDLE;
            }
            return;
        }

        {
            extern int gPS5SettingsPage;
            if (gPS5SettingsPage == 1) {
                if (getKeyOn(KEY_CIRCLE)) {
                    sfxPlay(SFX_CANCEL);
                    gPS5SettingsPage = 0;
                    return;
                }

                if (getKeyOn(KEY_TRIANGLE)) {
                    sfxPlay(SFX_CONFIRM);
                    ps5StartSmbConnectionCheck();
                    return;
                }

                if (getKeyOn(KEY_SQUARE)) {
                    sfxPlay(SFX_CONFIRM);
                    ps5SaveSettings();
                    return;
                }

                if (getKey(KEY_UP)) {
                    sfxPlay(SFX_CURSOR);
                    gPS5SmbSettingsSel = gPS5SmbSettingsSel > 0 ? gPS5SmbSettingsSel - 1 : PS5_SMB_SETTINGS_COUNT - 1;
                } else if (getKey(KEY_DOWN)) {
                    sfxPlay(SFX_CURSOR);
                    gPS5SmbSettingsSel = gPS5SmbSettingsSel < PS5_SMB_SETTINGS_COUNT - 1 ? gPS5SmbSettingsSel + 1 : 0;
                }

                if (getKeyOn(KEY_LEFT) || getKeyOn(KEY_RIGHT)) {
                    sfxPlay(SFX_CURSOR);
                    switch (gPS5SmbSettingsSel) {
                        case 0:
                            gPS5TempEthEnabled = !gPS5TempEthEnabled;
                            break;
                        case 5:
                            gPS5TempSmbAddressType = !gPS5TempSmbAddressType;
                            break;
                        case 7:
                            gPS5TempSmbDhcp = !gPS5TempSmbDhcp;
                            break;
                    }
                }

                if (getKeyOn(KEY_CROSS) || getKeyOn(gSelectButton)) {
                    sfxPlay(SFX_CONFIRM);
                    switch (gPS5SmbSettingsSel) {
                        case 0:
                            gPS5TempEthEnabled = !gPS5TempEthEnabled;
                            break;
                        case 1:
                            diaShowIpEditor(gPS5TempSmbIp, sizeof(gPS5TempSmbIp));
                            break;
                        case 2:
                            ps5EditSmbText(gPS5TempSmbShare, sizeof(gPS5TempSmbShare), 0, "SMB Share");
                            break;
                        case 3:
                            ps5EditSmbText(gPS5TempSmbUser, sizeof(gPS5TempSmbUser), 0, "SMB Username");
                            break;
                        case 4:
                            ps5EditSmbText(gPS5TempSmbPassword, sizeof(gPS5TempSmbPassword), 1, "SMB Password");
                            break;
                        case 5:
                            gPS5TempSmbAddressType = !gPS5TempSmbAddressType;
                            break;
                        case 6:
                            ps5EditSmbText(gPS5TempSmbName, sizeof(gPS5TempSmbName), 0, "SMB Server Name");
                            break;
                        case 7:
                            gPS5TempSmbDhcp = !gPS5TempSmbDhcp;
                            break;
                        case 8:
                            ps5EditSmbInt(&gPS5TempSmbPort, 0, 65353, "SMB Port");
                            break;
                        case 9:
                            ps5EditSmbText(gPS5TempSmbPrefix, sizeof(gPS5TempSmbPrefix), 0, "SMB Prefix");
                            break;
                        case 10:
                            ps5EditSmbInt(&gPS5TempSmbCache, 0, 32, "SMB Cache");
                            break;
                    }
                }
                return;
            }
        }

        // Triangle key: Back to Games tab, revert changes if not saved
        if (getKeyOn(KEY_TRIANGLE)) {
            sfxPlay(SFX_CANCEL);
            {
                extern int gPS5SettingsPage;
                gPS5SettingsPage = 0;
            }
            if (gVMode != gPS5SavedVMode) {
                gVMode = gPS5SavedVMode;
                applyConfig(-1, -1, 0);
            }
            gPS5UISound = gPS5SavedUISound;
            gPS5ShowCoverImages = gPS5SavedShowCoverImages;
            gPS5ShowGamesLogo = gPS5SavedShowGamesLogo;
            gPS5SortMode = gPS5SavedSortMode;
            gSelectButton = gPS5SavedSelectButton;
            gPS5TempVMode = gPS5SavedVMode;
            gPS5TempSelectButton = gPS5SavedSelectButton;
            gPS5TempUISound = gPS5SavedUISound;
            gPS5TempShowCoverImages = gPS5SavedShowCoverImages;
            gPS5TempShowGamesLogo = gPS5SavedShowGamesLogo;
            gPS5TempSortMode = gPS5SavedSortMode;
#ifdef PADEMU
            gPS5TempControllerType = gPS5SavedControllerType;
#endif
            {
                extern int gPS5SettingsPage;
                gPS5SettingsPage = 0;
            }
            gPS5ActiveTab = 0;
            return;
        }

        if (getKeyOn(KEY_SQUARE)) {
            sfxPlay(SFX_CONFIRM);
            ps5SaveSettings();
            return;
        }

        if (gPS5SubSel == 0) { // Focus is on Resolution selection line
            if (getKeyOn(KEY_LEFT)) {
                sfxPlay(SFX_CURSOR);
                if (gPS5TempVMode == 0) gPS5TempVMode = 11;
                else if (gPS5TempVMode == 11) gPS5TempVMode = 10;
                else if (gPS5TempVMode == 10) gPS5TempVMode = 3;
                else gPS5TempVMode = 0;
            }
            if (getKeyOn(KEY_RIGHT)) {
                sfxPlay(SFX_CURSOR);
                if (gPS5TempVMode == 0) gPS5TempVMode = 3;
                else if (gPS5TempVMode == 3) gPS5TempVMode = 10;
                else if (gPS5TempVMode == 10) gPS5TempVMode = 11;
                else gPS5TempVMode = 0;
            }
            if (getKeyOn(KEY_CROSS) || getKeyOn(gSelectButton)) {
                sfxPlay(SFX_CONFIRM);
                if (gVMode != gPS5TempVMode) {
                    if (!guiConfirmVideoModeChange())
                        return;

                    int oldMode = gVMode;
                    gVMode = gPS5TempVMode;
                    applyConfig(-1, -1, 0); // Apply explicitly when CROSS is pressed!
                    
                    extern int guiConfirmVideoMode(void);
                    if (guiConfirmVideoMode() == 0) {
                        // Revert back on cancel/timeout
                        gVMode = oldMode;
                        gPS5TempVMode = oldMode;
                        applyConfig(-1, -1, 0);
                    } else {
                        // User confirmed, save!
                        extern int gPS5SavedVMode;
                        gPS5SavedVMode = gVMode;
                        saveConfig(CONFIG_OPL, 0);
                    }
                }
            }
            if (getKey(KEY_UP)) {
                sfxPlay(SFX_CURSOR);
                gPS5SubSel = 7; // Move up to Game Covers at the top
            }
            if (getKey(KEY_DOWN)) {
                sfxPlay(SFX_CURSOR);
                gPS5SubSel = 1; // Move down to Select Button
            }
        } else if (gPS5SubSel == 1) { // Focus is on Select Button
            if (getKeyOn(KEY_LEFT) || getKeyOn(KEY_RIGHT)) {
                sfxPlay(SFX_CURSOR);
                gPS5TempSelectButton = gPS5TempSelectButton == KEY_CIRCLE ? KEY_CROSS : KEY_CIRCLE;
            }
            if (getKey(KEY_UP)) {
                sfxPlay(SFX_CURSOR);
                gPS5SubSel = 0;
            }
            if (getKey(KEY_DOWN)) {
                sfxPlay(SFX_CURSOR);
                gPS5SubSel = 2;
            }
        } else if (gPS5SubSel == 2) { // Focus is on UI Sound
            if (getKeyOn(KEY_LEFT) || getKeyOn(KEY_RIGHT)) {
                sfxPlay(SFX_CURSOR);
                gPS5TempUISound = !gPS5TempUISound; // Toggle On/Off
            }
            if (getKey(KEY_UP)) {
                sfxPlay(SFX_CURSOR);
                gPS5SubSel = 1;
            }
            if (getKey(KEY_DOWN)) {
                sfxPlay(SFX_CURSOR);
                gPS5SubSel = 3;
            }
        } else if (gPS5SubSel == 3) { // Focus is on Show Cover Images
            if (getKeyOn(KEY_LEFT) || getKeyOn(KEY_RIGHT)) {
                sfxPlay(SFX_CURSOR);
                gPS5TempShowCoverImages = !gPS5TempShowCoverImages;
                gPS5ShowCoverImages = gPS5TempShowCoverImages;
            }
            if (getKey(KEY_UP)) {
                sfxPlay(SFX_CURSOR);
                gPS5SubSel = 2;
            }
            if (getKey(KEY_DOWN)) {
                sfxPlay(SFX_CURSOR);
                gPS5SubSel = 4;
            }
        } else if (gPS5SubSel == 4) { // Focus is on Show Games Logo
            if (getKeyOn(KEY_LEFT) || getKeyOn(KEY_RIGHT)) {
                sfxPlay(SFX_CURSOR);
                gPS5TempShowGamesLogo = !gPS5TempShowGamesLogo;
                gPS5ShowGamesLogo = gPS5TempShowGamesLogo;
            }
            if (getKey(KEY_UP)) {
                sfxPlay(SFX_CURSOR);
                gPS5SubSel = 3;
            }
            if (getKey(KEY_DOWN)) {
                sfxPlay(SFX_CURSOR);
                gPS5SubSel = 5;
            }
        } else if (gPS5SubSel == 5) { // Focus is on Sorting Games
            if (getKeyOn(KEY_LEFT) || getKeyOn(KEY_RIGHT)) {
                sfxPlay(SFX_CURSOR);
                gPS5TempSortMode = !gPS5TempSortMode;
                gPS5SortMode = gPS5TempSortMode;
            }
            if (getKey(KEY_UP)) {
                sfxPlay(SFX_CURSOR);
                gPS5SubSel = 4;
            }
            if (getKey(KEY_DOWN)) {
                sfxPlay(SFX_CURSOR);
                gPS5SubSel = 6;
            }
        } else if (gPS5SubSel == 6) { // Focus is on Controller
            if (getKeyOn(KEY_LEFT)) {
                sfxPlay(SFX_CURSOR);
                gPS5TempControllerType = gPS5TempControllerType > 0 ? gPS5TempControllerType - 1 : 3;
            }
            if (getKeyOn(KEY_RIGHT)) {
                sfxPlay(SFX_CURSOR);
                gPS5TempControllerType = gPS5TempControllerType < 3 ? gPS5TempControllerType + 1 : 0;
            }
            if (getKeyOn(KEY_CROSS) || getKeyOn(gSelectButton)) {
                int i;
                u8 raw[72];
                sfxPlay(SFX_CONFIRM);
                gPS5ControllerLogVisible = 1;
                gPS5ControllerLogStep = 0;
                gPS5ControllerLogCaptured = 0;
                gPS5ControllerLogNavTestEnabled = 0;
                gPS5ControllerLogBridgePadData = 0;
                for (i = 0; i < 20; i++)
                    gPS5ControllerLogLines[i][0] = '\0';
                snprintf(gPS5ControllerLogStatus, sizeof(gPS5ControllerLogStatus), "Cross: capture  Square: save  Triangle: nav test  Circle: close");
                ps5ReadControllerRaw(raw);
            }
            if (getKey(KEY_UP)) {
                sfxPlay(SFX_CURSOR);
                gPS5SubSel = 5;
            }
            if (getKey(KEY_DOWN)) {
                sfxPlay(SFX_CURSOR);
                gPS5SubSel = 7;
            }
        } else if (gPS5SubSel == 7) { // Focus is on Game Covers
            if (getKeyOn(KEY_LEFT) || getKeyOn(KEY_RIGHT)) {
                sfxPlay(SFX_CURSOR);
                gPS5SubSel = 8;
            }
            if (getKey(KEY_DOWN)) {
                sfxPlay(SFX_CURSOR);
                gPS5SubSel = 0;
            }
            if (getKey(KEY_UP)) {
                sfxPlay(SFX_CURSOR);
                gPS5SubSel = 6;
            }
            if (getKeyOn(KEY_CROSS) || getKeyOn(gSelectButton)) {
                sfxPlay(SFX_CONFIRM);
                gPS5CoverDownloadMode = PS5_COVER_DOWNLOAD_MISSING;
                gPS5CoverDownloadStatus = PS5_COVER_DOWNLOAD_PROMPT;
            }
        } else if (gPS5SubSel == 8) { // Focus is on SMB Settings
            if (getKeyOn(KEY_LEFT) || getKeyOn(KEY_RIGHT)) {
                sfxPlay(SFX_CURSOR);
                gPS5SubSel = 7;
            }
            if (getKey(KEY_DOWN)) {
                sfxPlay(SFX_CURSOR);
                gPS5SubSel = 0;
            }
            if (getKey(KEY_UP)) {
                sfxPlay(SFX_CURSOR);
                gPS5SubSel = 0;
            }
            if (getKeyOn(KEY_CROSS) || getKeyOn(gSelectButton)) {
                extern int gPS5SettingsPage;
                sfxPlay(SFX_CONFIRM);
                gPS5SettingsPage = 1;
                gPS5SmbSettingsSel = 0;
            }
        }
        return;
    }

    if (!mainMenu)
        return;

    if (!mainMenuCurrent)
        mainMenuCurrent = mainMenu;

    if (getKey(KEY_UP)) {
        sfxPlay(SFX_CURSOR);
        if (mainMenuCurrent->prev)
            mainMenuCurrent = mainMenuCurrent->prev;
        else // rewind to the last item
            while (mainMenuCurrent->next)
                mainMenuCurrent = mainMenuCurrent->next;
    }

    if (getKey(KEY_DOWN)) {
        sfxPlay(SFX_CURSOR);
        if (mainMenuCurrent->next)
            mainMenuCurrent = mainMenuCurrent->next;
        else
            mainMenuCurrent = mainMenu;
    }

    if (getKeyOn(gSelectButton)) {
        // execute the item via looking at the id of it
        int id = mainMenuCurrent->item.id;

        sfxPlay(SFX_CONFIRM);

        if (id == MENU_SETTINGS) {
            if (menuCheckParentalLock() == 0)
                guiShowConfig();
        } else if (id == MENU_GFX_SETTINGS) {
            if (menuCheckParentalLock() == 0)
                guiShowUIConfig();
        } else if (id == MENU_AUDIO_SETTINGS) {
            if (menuCheckParentalLock() == 0)
                guiShowAudioConfig();
        } else if (id == MENU_CONTROLLER_SETTINGS) {
            if (menuCheckParentalLock() == 0)
                guiShowControllerConfig();
        } else if (id == MENU_OSD_LANGUAGE_SETTINGS) {
            if (menuCheckParentalLock() == 0)
                guiGameShowOSDLanguageConfig(1);
        } else if (id == MENU_PARENTAL_LOCK) {
            if (menuCheckParentalLock() == 0)
                guiShowParentalLockConfig();
        } else if (id == MENU_NET_CONFIG) {
            if (menuCheckParentalLock() == 0)
                guiShowNetConfig();
        } else if (id == MENU_NET_UPDATE) {
            if (menuCheckParentalLock() == 0)
                guiShowNetCompatUpdate();
        } else if (id == MENU_START_NBD) {
            if (menuCheckParentalLock() == 0)
                handleLwnbdSrv();
        } else if (id == MENU_ABOUT) {
            guiShowAbout();
        } else if (id == MENU_SAVE_CHANGES) {
            if (menuCheckParentalLock() == 0) {
                guiGameSaveOSDLanguageGlobalConfig(configGetByType(CONFIG_GAME));
#ifdef PADEMU
                guiGameSavePadEmuGlobalConfig(configGetByType(CONFIG_GAME));
                guiGameSavePadMacroGlobalConfig(configGetByType(CONFIG_GAME));
#endif
                saveConfig(CONFIG_OPL | CONFIG_NETWORK | CONFIG_GAME, 1);
                menuSetParentalLockCheckState(1); // Re-enable parental lock check.
            }
        } else if (id == MENU_EXIT) {
            if (guiMsgBox(_l(_STR_CONFIRMATION_EXIT), 1, NULL))
                sysExecExit();
        } else if (id == MENU_POWER_OFF) {
            if (guiMsgBox(_l(_STR_CONFIRMATION_POFF), 1, NULL))
                sysPowerOff();
        }

        // so the exit press wont propagate twice
        readPads();
    }
}

static void menuRenderElements(theme_element_t *elem)
{
    // selected_item can't be NULL here as we only allow to switch to "Main" rendering when there is at least one device activated
    _menuRequestConfig();

    WaitSema(menuSemaId);

    while (elem) {
        if (elem->drawElem) {
            if (gPS5Mode) {
                if (elem->type == ELEM_TYPE_BACKGROUND || elem->type == ELEM_TYPE_ITEMS_LIST) {
                    elem->drawElem(selected_item, selected_item->item->current, itemConfig, elem);
                }
            } else {
                elem->drawElem(selected_item, selected_item->item->current, itemConfig, elem);
            }
        }

        elem = elem->next;
    }
    SignalSema(menuSemaId);
}

void menuRenderMain(void)
{
    item_list_t *list = selected_item->item->userdata;

    if (list->mode == APP_MODE) {
        menuRenderElements(gTheme->appsMainElems.first);
        gTheme->itemsList = gTheme->appsItemsList;
    } else {
        menuRenderElements(gTheme->mainElems.first);
        gTheme->itemsList = gTheme->gamesItemsList;
    }
}

void menuHandleInputMain()
{
    if (gPS5Mode) {
        extern int gVMode;
        extern int gPS5TempVMode;
        extern int gPS5UISound;
        extern int gPS5ShowCoverImages;
        extern int gPS5ShowGamesLogo;
        extern int gPS5SortMode;
        if (ps5HandleSmbDialogInput()) {
            return;
        }
        if (ps5HandleSmbCheckDialogInput()) {
            return;
        }
        if (ps5SmbConsumeInputWhileBusy())
            return;
        ps5PollXboxNavigation();
        if (getKeyOn(KEY_L1)) {
            sfxPlay(SFX_CURSOR);
            if (gPS5ActiveTab == 1) {
                if (gVMode != gPS5SavedVMode) {
                    gVMode = gPS5SavedVMode;
                    applyConfig(-1, -1, 0);
                }
                gPS5UISound = gPS5SavedUISound;
                gPS5ShowCoverImages = gPS5SavedShowCoverImages;
                gPS5ShowGamesLogo = gPS5SavedShowGamesLogo;
                gPS5SortMode = gPS5SavedSortMode;
            }
            gPS5TempVMode = gPS5SavedVMode;
            gPS5TempUISound = gPS5SavedUISound;
            gPS5TempShowCoverImages = gPS5SavedShowCoverImages;
            gPS5TempShowGamesLogo = gPS5SavedShowGamesLogo;
            gPS5TempSortMode = gPS5SavedSortMode;
            gPS5ActiveTab = gPS5ActiveTab == 1 ? 2 : 0;
            if (gPS5ActiveTab == 2)
                ps5QueueAppsScan(0);
            return;
        } else if (getKeyOn(KEY_R1)) {
            sfxPlay(SFX_CURSOR);
            if (gPS5ActiveTab == 0) {
                gPS5ActiveTab = 2;
                ps5QueueAppsScan(0);
                return;
            }
            if (gPS5ActiveTab == 2) {
                extern int gPS5SubSel;
                gPS5SavedVMode = gVMode; // Backup current resolution when switching to settings
                gPS5TempVMode = gVMode;
                gPS5SavedUISound = gPS5UISound;
                gPS5TempUISound = gPS5UISound;
                gPS5SavedShowCoverImages = gPS5ShowCoverImages;
                gPS5TempShowCoverImages = gPS5ShowCoverImages;
                gPS5SavedShowGamesLogo = gPS5ShowGamesLogo;
                gPS5TempShowGamesLogo = gPS5ShowGamesLogo;
                gPS5SavedSortMode = gPS5SortMode;
                gPS5TempSortMode = gPS5SortMode;
                ps5CopySmbSettingsToTemp();
                {
                    extern int gPS5SettingsPage;
                    gPS5SettingsPage = 0;
                }
                gPS5SubSel = 7;
            }
            gPS5ActiveTab = 1;
            // Initialize mainMenuCurrent if needed
            if (!menuGetMainMenuCurrent()) {
                menuSetMainMenuCurrent(menuGetMainMenu());
            }
            return;
        }

        if (gPS5ActiveTab == 0 && gETHStartMode != START_MODE_DISABLED && gPS5SmbPromptState == 0 && ps5CanStartSmbLoad()) {
            item_list_t *eth = ethGetObject(0);
            if (eth != NULL && (!eth->enabled || gNetworkStartup != 0)) {
                ps5QueueSmbLoad();
            }
        }

        if (gPS5ActiveTab == 1) {
            menuHandleInputMenu();
            return;
        }

        if (gPS5ActiveTab == 2) {
            if (!gPS5AppsScanned && !gPS5AppsLoading)
                ps5QueueAppsScan(0);

            setButtonDelay(KEY_UP, 120);
            setButtonDelay(KEY_DOWN, 120);

            if (getKey(KEY_UP)) {
                if (gPS5AppsItemCount > 0) {
                    sfxPlay(SFX_CURSOR);
                    gPS5AppsSelected = gPS5AppsSelected > 0 ? gPS5AppsSelected - 1 : gPS5AppsItemCount - 1;
                }
            } else if (getKey(KEY_DOWN)) {
                if (gPS5AppsItemCount > 0) {
                    sfxPlay(SFX_CURSOR);
                    gPS5AppsSelected = gPS5AppsSelected + 1 < gPS5AppsItemCount ? gPS5AppsSelected + 1 : 0;
                }
            } else if (getKeyOn(KEY_SQUARE)) {
                if (!gPS5AppsLoading) {
                    sfxPlay(SFX_CONFIRM);
                    ps5QueueAppsScan(1);
                } else {
                    sfxPlay(SFX_MESSAGE);
                }
            } else if (getKeyOn(KEY_CROSS)) {
                if (gPS5AppsItemCount > 0 && !gPS5AppsLoading) {
                    sfxPlay(SFX_CONFIRM);
                    ps5LaunchSelectedApp();
                } else {
                    sfxPlay(SFX_MESSAGE);
                }
            }
            return;
        }

        {
            int heldDirection = getKeyPressed(KEY_LEFT) ? -1 : (getKeyPressed(KEY_RIGHT) ? 1 : 0);
            int repeatDelay = PS5_CAROUSEL_REPEAT_DELAY;

            if (heldDirection == 0) {
                ps5CarouselHoldDirection = 0;
                ps5CarouselHoldStarted = 0;
            } else {
                clock_t now = clock();

                if (heldDirection != ps5CarouselHoldDirection) {
                    ps5CarouselHoldDirection = heldDirection;
                    ps5CarouselHoldStarted = now;
                } else if (now - ps5CarouselHoldStarted >= 5 * CLOCKS_PER_SEC) {
                    repeatDelay = PS5_CAROUSEL_REPEAT_DELAY_FAST;
                }
            }

            setButtonDelay(KEY_LEFT, repeatDelay);
            setButtonDelay(KEY_RIGHT, repeatDelay);
        }
        if (getKey(KEY_LEFT)) {
            ps5MenuMoveGame(-1);
        } else if (getKey(KEY_RIGHT)) {
            ps5MenuMoveGame(1);
        } else if (getKey(KEY_UP)) {
            extern void ps5MoveAlphabetGame(int direction);
            sfxPlay(SFX_CURSOR);
            gPS5CarouselNavInterrupt = 4;
            ps5MoveAlphabetGame(-1);
        } else if (getKey(KEY_DOWN)) {
            extern void ps5MoveAlphabetGame(int direction);
            sfxPlay(SFX_CURSOR);
            gPS5CarouselNavInterrupt = 4;
            ps5MoveAlphabetGame(1);
        }
    } else {
        if (getKey(KEY_LEFT)) {
            menuPrevH();
        } else if (getKey(KEY_RIGHT)) {
            menuNextH();
        } else if (getKey(KEY_UP)) {
            menuPrevV();
        } else if (getKey(KEY_DOWN)) {
            menuNextV();
        }
    }
    if (getKeyOn(KEY_CROSS)) {
        if (gPS5Mode && gPS5ActiveTab == 0 && ps5MenuGetActionGame() == NULL) {
            sfxPlay(SFX_MESSAGE);
        } else if (selected_item && selected_item->item && selected_item->item->execCross) {
            selected_item->item->execCross(selected_item->item);
        }
    } else if (getKeyOn(KEY_TRIANGLE)) {
        if (gPS5Mode && gPS5ActiveTab == 0 && ps5MenuGetActionGame() == NULL) {
            sfxPlay(SFX_MESSAGE);
        } else if (selected_item && selected_item->item && selected_item->item->execTriangle) {
            selected_item->item->execTriangle(selected_item->item);
        }
    } else if (getKeyOn(KEY_CIRCLE)) {
        if (selected_item && selected_item->item && selected_item->item->execCircle)
            selected_item->item->execCircle(selected_item->item);
    } else if (getKeyOn(KEY_SQUARE)) {
        if (gPS5Mode) {
            extern int gPS5ActiveTab;
            if (gPS5ActiveTab == 0) {
                extern unsigned int gPS5RefreshBusyFrame;
                extern int guiFrameId;
                if (gETHStartMode != START_MODE_DISABLED) {
                    if (ps5SmbRefreshIsBusy() || bdmIsDeviceLoading() || ioHasPendingRequests() || (gPS5RefreshBusyFrame && guiFrameId - (int)gPS5RefreshBusyFrame < 120) || (gPS5SmbManualRefreshFrame && guiFrameId - (int)gPS5SmbManualRefreshFrame < 120)) {
                        sfxPlay(SFX_MESSAGE);
                    } else {
                        sfxPlay(SFX_CONFIRM);
                        gPS5RefreshBusyFrame = guiFrameId;
                        if (!ps5SmbKnownLinkDown()) {
                            oplRefreshMergedGameList();
                            gPS5SmbManualRefreshFrame = guiFrameId;
                            gPS5SmbAutoStartFrame = guiFrameId;
                            gPS5SmbPromptState = 0;
                            ps5QueueSmbLoad();
                        } else {
                            ps5RetryMissingCoverCache();
                            gPS5SmbPromptState = 1;
                            gPS5SmbLoadStatus = 0;
                            gPS5SmbRefreshQueued = 0;
                            gPS5SmbHadGamesBeforeLoad = 0;
                            gPS5SmbUiLoading = 0;
                        }
                    }
                } else {
                    if (bdmIsDeviceLoading() || ioHasPendingRequests() || (gPS5RefreshBusyFrame && guiFrameId - (int)gPS5RefreshBusyFrame < 120)) {
                        sfxPlay(SFX_MESSAGE);
                    } else {
                        sfxPlay(SFX_CONFIRM);
                        gPS5RefreshBusyFrame = guiFrameId;
                        oplRefreshMergedGameList();
                    }
                }
            }
        } else {
            if (selected_item && selected_item->item && selected_item->item->execSquare)
                selected_item->item->execSquare(selected_item->item);
        }
    } else if (getKeyOn(KEY_START)) {
        if (!gPS5Mode) {
            // reinit main menu - show/hide items valid in the active context
            menuInitMainMenu();
            guiSwitchScreen(GUI_SCREEN_MENU);
        }
    } else if (getKeyOn(KEY_SELECT)) {
        if (!gPS5Mode && selected_item && selected_item->item && selected_item->item->refresh)
            selected_item->item->refresh(selected_item->item);
    } else if (getKey(KEY_L1)) {
        menuPrevPage();
    } else if (getKey(KEY_R1)) {
        menuNextPage();
    }

    // Last Played Auto Start
    if (!gPS5Mode && RemainSecs < 0) {
        DisableCron = 1; // Disable Counter
        if (gSelectButton == KEY_CIRCLE)
            selected_item->item->execCircle(selected_item->item);
        else
            selected_item->item->execCross(selected_item->item);
    }
}

void menuRenderInfo(void)
{
    item_list_t *list = selected_item->item->userdata;

    if (list->mode == APP_MODE) {
        menuRenderElements(gTheme->appsInfoElems.first);
        gTheme->itemsList = gTheme->appsItemsList;
    } else {
        menuRenderElements(gTheme->infoElems.first);
        gTheme->itemsList = gTheme->gamesItemsList;
    }
}

void menuHandleInputInfo()
{
    if (getKeyOn(KEY_CROSS)) {
        if (gSelectButton == KEY_CIRCLE)
            guiSwitchScreen(GUI_SCREEN_MAIN);
        else
            selected_item->item->execCross(selected_item->item);
    } else if (getKey(KEY_UP)) {
        menuPrevV();
    } else if (getKey(KEY_DOWN)) {
        menuNextV();
    } else if (getKeyOn(KEY_CIRCLE)) {
        if (gSelectButton == KEY_CROSS)
            guiSwitchScreen(GUI_SCREEN_MAIN);
        else
            selected_item->item->execCircle(selected_item->item);
    } else if (getKey(KEY_L1)) {
        menuPrevPage();
    } else if (getKey(KEY_R1)) {
        menuNextPage();
    }
}

static const char *ps5GameOptionValue(int menuID, char *buffer, int size)
{
    if (menuID >= GAME_PS5_MODE_BASE && menuID < GAME_PS5_MODE_BASE + COMPAT_MODE_COUNT) {
        snprintf(buffer, size, "%s", (ps5GameCompatMode & (1 << (menuID - GAME_PS5_MODE_BASE))) ? "On" : "Off");
        return buffer;
    }

    switch (menuID) {
        case GAME_PS5_GSM_RESOLUTION:
            if (ps5GameGSMResolution < 0 || ps5GameGSMResolution >= PS5_GAME_RESOLUTION_COUNT)
                ps5GameGSMResolution = 0;
            snprintf(buffer, size, "%s", ps5GameResolutionNames[ps5GameGSMResolution]);
            break;
        default:
            buffer[0] = '\0';
            break;
    }

    return buffer;
}

static int ps5ResolveCachedGameOptionSource(item_list_t **support, int *sourceId)
{
    item_list_t *cachedSupport = itemConfigSupport;
    const char *startup;
    int count, i;

    if (cachedSupport == NULL || !cachedSupport->enabled || sourceId == NULL || support == NULL || cachedSupport->itemGetCount == NULL)
        return 0;

    count = cachedSupport->itemGetCount(cachedSupport);
    if (itemConfigSourceId >= 0 && itemConfigSourceId < count) {
        startup = cachedSupport->itemGetStartup != NULL ? cachedSupport->itemGetStartup(cachedSupport, itemConfigSourceId) : NULL;
        if (itemConfigStartup[0] == '\0' || (startup != NULL && strcmp(startup, itemConfigStartup) == 0)) {
            *support = cachedSupport;
            *sourceId = itemConfigSourceId;
            return 1;
        }
    }

    if (itemConfigStartup[0] == '\0' || cachedSupport->itemGetStartup == NULL)
        return 0;

    for (i = 0; i < count; i++) {
        startup = cachedSupport->itemGetStartup(cachedSupport, i);
        if (startup != NULL && strcmp(startup, itemConfigStartup) == 0) {
            itemConfigSourceId = i;
            *support = cachedSupport;
            *sourceId = i;
            return 1;
        }
    }

    return 0;
}

static int ps5GameOptionsSourceIsAvailable(void)
{
    int sourceId;
    item_list_t *sourceSupport;
    opl_io_module_t *sourceModule;
    int encodedItem;

    if (itemConfigSupport != NULL && itemConfigSourceId >= 0) {
        if (!ps5ResolveCachedGameOptionSource(&sourceSupport, &sourceId))
            return 0;
        encodedItem = 1;
    } else {
        if (selected_item == NULL || selected_item->item == NULL || selected_item->item->current == NULL)
            return 0;

        if (selected_item->item->visible == 0)
            return 0;

        sourceId = selected_item->item->current->item.id;
        encodedItem = oplIsGameItemIdEncoded(sourceId);
        sourceSupport = selected_item->item->userdata;
        if (sourceSupport == NULL || sourceId < 0)
            return 0;

        if (!oplResolveGameItem(sourceId, sourceSupport, &sourceSupport, &sourceId))
            return 0;
    }
    if (sourceSupport == NULL || sourceId < 0)
        return 0;

    sourceModule = (opl_io_module_t *)sourceSupport->owner;
    if (sourceModule == NULL)
        return 0;

    if (!encodedItem && sourceModule->menuItem.visible == 0)
        return 0;

    if (sourceSupport->itemGetCount && sourceId >= sourceSupport->itemGetCount(sourceSupport))
        return 0;

    return 1;
}

void menuRenderGameMenu()
{
    extern int gPS5Mode;

    if (gPS5Mode) {
        submenu_list_t *it;
        int count = 0, selected = 0, index = 0;
        int ps5Width, ps5Height;
        char value[64];
        int listX, labelX, listY, rowW, footerY;
        int rowStep = 42;
        int headerH = 58;
        int footerTop;
        int listTop = 88;
        int listBottom = 390;
        int listScrollOffset = 0;
        int focusY = 0;
        u64 footerColor = GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80);
        u64 focusedColor = GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80);
        u64 rowColor = GS_SETREG_RGBA(0x58, 0x58, 0x58, 0x56);
        int semiBoldFont = thmGetPS5SemiBoldFont();

        rmGetScreenExtents(&ps5Width, &ps5Height);
        footerTop = ps5Height - 44;
        listX = 64;
        listY = listTop;
        labelX = listX + 36;
        rowW = ps5Width - labelX - 64;
        footerY = ps5Height - 20;

        rmDrawRect(0, 0, ps5Width, ps5Height, GS_SETREG_RGBA(0, 0, 0, 0x80));

        if (!ps5GameOptionsSourceIsAvailable()) {
            extern int gPS5ActiveTab;
            gPS5ActiveTab = 0;
            gameMenuCurrent = gameMenu;
            guiSwitchScreen(GUI_SCREEN_MAIN);
            return;
        }

        if (!gameMenu || selected_item->item->current == NULL)
            return;

        if (!gameMenuCurrent)
            gameMenuCurrent = gameMenu;

        for (it = gameMenu; it; it = it->next, count++) {
            if (it == gameMenuCurrent)
                selected = index;
            index++;
        }

        focusY = listTop + (selected * rowStep);
        if (focusY > listBottom)
            listScrollOffset = focusY - listBottom;
        else if (focusY < listTop)
            listScrollOffset = focusY - listTop;
        listY -= listScrollOffset;

        rmDrawRect(0, 0, ps5Width, headerH, GS_SETREG_RGBA(0, 0, 0, 0x80));
        rmDrawRect(0, footerTop, ps5Width, ps5Height - footerTop, GS_SETREG_RGBA(0, 0, 0, 0x80));
        rmDrawRect(0, headerH, ps5Width, 1, GS_SETREG_RGBA(0x38, 0x38, 0x38, 0x40));
        rmDrawRect(0, footerTop, ps5Width, 1, GS_SETREG_RGBA(0x38, 0x38, 0x38, 0x40));
        {
            const char *gameTitle = submenuItemGetText(&selected_item->item->current->item);
            int artW = 42;
            int artH = 42;
            int artX = listX;
            int artY = 12;
            int titleX = artX + artW + 8;

            drawPS5GameHeaderArtwork(gameTitle, artX, artY, artW, artH);
            fntRenderString(thmGetPS5HeaderFont(), titleX, 12, ALIGN_LEFT, ps5Width - titleX - 64, 0, gameTitle, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
            fntRenderString(gTheme->fonts[1], titleX, 36, ALIGN_LEFT, 0.78f, 0.78f, "Game Options", GS_SETREG_RGBA(0x58, 0x58, 0x58, 0x56));
        }

        {
            int resY = listY;
            int focused = selected == 0;
            int rowFont = focused ? semiBoldFont : gTheme->fonts[1];
            if (resY >= listTop - rowStep && resY <= listBottom + rowStep) {
                if (focused)
                    drawPS5GameFocusIndicator(labelX, resY);
                fntRenderString(rowFont, labelX, resY, ALIGN_LEFT | ALIGN_VCENTER, 0, 0, "Resolution", focused ? focusedColor : rowColor);
                ps5GameOptionValue(GAME_PS5_GSM_RESOLUTION, value, sizeof(value));
                {
                    char resValue[80];
                    snprintf(resValue, sizeof(resValue), "<%s>", value);
                    fntRenderString(rowFont, labelX + rowW, resY, ALIGN_RIGHT | ALIGN_VCENTER, 0, 0, resValue, focused ? focusedColor : rowColor);
                }
            }
        }

        for (index = 0; index < COMPAT_MODE_COUNT; index++) {
            int y = listY + (index + 1) * rowStep;
            int focused = selected == index + 1;
            int enabled = (ps5GameCompatMode & (1 << index)) != 0;
            u64 text = focused ? focusedColor : rowColor;
            u64 status = focused ? focusedColor : rowColor;
            int rowFont = focused ? semiBoldFont : gTheme->fonts[1];

            if (y < listTop - rowStep || y > listBottom + rowStep)
                continue;
            snprintf(value, sizeof(value), "Mode %d", index + 1);
            if (focused)
                drawPS5GameFocusIndicator(labelX, y);
            fntRenderString(rowFont, labelX, y, ALIGN_LEFT | ALIGN_VCENTER, 0, 0, value, text);
            fntRenderString(rowFont, labelX + rowW, y, ALIGN_RIGHT | ALIGN_VCENTER, 0, 0, enabled ? "<On>" : "<Off>", status);
        }

        {
            int nextX = drawPS5GameIconAndText(CROSS_ICON, "Play", semiBoldFont, listX, footerY, footerColor);
            drawPS5GameIconAndText(SQUARE_ICON, "Save", semiBoldFont, nextX + 22, footerY, footerColor);
            drawPS5GameRightIconAndText(CIRCLE_ICON, "Close", semiBoldFont, ps5Width - 64, footerY, footerColor);
        }
        return;
    }

    guiDrawBGPlasma();

    if (!gameMenu)
        return;

    // If the device menu that has the selected game suddenly goes invisible (device was removed), switch
    // back to the game list menu.
    if (selected_item->item->visible == 0) {
        guiSwitchScreen(GUI_SCREEN_MAIN);
        return;
    }

    // If we enter the game settings menu and there's no selected item bail out. I'm not entirely sure how we get into
    // this state but it seems to happen on some consoles when transitioning from the game settings menu back to the game
    // list menu.
    if (selected_item->item->current == NULL)
        return;

    // draw the animated menu
    if (!gameMenuCurrent)
        gameMenuCurrent = gameMenu;

    submenu_list_t *it = gameMenu;

    // calculate the number of items
    int count = 0;
    int sitem = 0;
    for (; it; count++, it = it->next) {
        if (it == gameMenuCurrent)
            sitem = count;
    }

    int spacing = 25;
    int y = (gTheme->usedHeight >> 1) - (spacing * (count >> 1));
    int cp = 0; // current position

    // game title
    fntRenderString(gTheme->fonts[0], 320, 20, ALIGN_CENTER, 0, 0, selected_item->item->current->item.text, gTheme->selTextColor);

    // config source
    char *cfgSource = gameConfigSource();
    fntRenderString(gTheme->fonts[0], 320, 40, ALIGN_CENTER, 0, 0, cfgSource, gTheme->textColor);

    // settings list
    for (it = gameMenu; it; it = it->next, cp++) {
        // render, advance
        fntRenderString(gTheme->fonts[0], 320, y, ALIGN_CENTER, 0, 0, submenuItemGetText(&it->item), (cp == sitem) ? gTheme->selTextColor : gTheme->textColor);
        y += spacing;
        if (cp == (GAME_SAVE_CHANGES - 1) || cp == (GAME_REMOVE_CHANGES - 1))
            y += spacing / 2;
    }

    // hints
    guiDrawSubMenuHints();
}

static void ps5GameOptionChange(int menuID, int direction)
{
    if (menuID >= GAME_PS5_MODE_BASE && menuID < GAME_PS5_MODE_BASE + COMPAT_MODE_COUNT) {
        ps5GameCompatMode ^= (1 << (menuID - GAME_PS5_MODE_BASE));
        return;
    }

    switch (menuID) {
        case GAME_PS5_GSM_RESOLUTION:
            ps5GameGSMResolution += direction;
            if (ps5GameGSMResolution < 0)
                ps5GameGSMResolution = PS5_GAME_RESOLUTION_COUNT - 1;
            else if (ps5GameGSMResolution >= PS5_GAME_RESOLUTION_COUNT)
                ps5GameGSMResolution = 0;
            break;
    }
}

void menuHandleInputGameMenu()
{
    if (!gameMenu)
        return;

    if (!gameMenuCurrent)
        gameMenuCurrent = gameMenu;

    if (gPS5Mode) {
        int menuID;
        int sourceId = selected_item->item->current != NULL ? selected_item->item->current->item.id : -1;
        item_list_t *sourceSupport = selected_item->item->userdata;

        if (!ps5GameOptionsSourceIsAvailable()) {
            extern int gPS5ActiveTab;
            gPS5ActiveTab = 0;
            gameMenuCurrent = gameMenu;
            guiSwitchScreen(GUI_SCREEN_MAIN);
            return;
        }

        if (itemConfigSupport != NULL && itemConfigSourceId >= 0) {
            if (!ps5ResolveCachedGameOptionSource(&sourceSupport, &sourceId))
                return;
        } else if (sourceSupport != NULL && sourceId >= 0) {
            if (!oplResolveGameItem(sourceId, sourceSupport, &sourceSupport, &sourceId))
                return;
        }

        if (getKey(KEY_UP)) {
            sfxPlay(SFX_CURSOR);
            if (gameMenuCurrent->prev)
                gameMenuCurrent = gameMenuCurrent->prev;
            else
                while (gameMenuCurrent->next)
                    gameMenuCurrent = gameMenuCurrent->next;
        }

        if (getKey(KEY_DOWN)) {
            sfxPlay(SFX_CURSOR);
            if (gameMenuCurrent->next)
                gameMenuCurrent = gameMenuCurrent->next;
            else
                gameMenuCurrent = gameMenu;
        }

        menuID = gameMenuCurrent->item.id;
        if (getKey(KEY_LEFT)) {
            sfxPlay(SFX_CURSOR);
            ps5GameOptionChange(menuID, -1);
        } else if (getKey(KEY_RIGHT)) {
            sfxPlay(SFX_CURSOR);
            ps5GameOptionChange(menuID, 1);
        } else if (getKeyOn(KEY_CROSS)) {
            sfxPlay(SFX_CONFIRM);
            ps5GameOptionsSave(itemConfig);
            if (sourceSupport != NULL && sourceId >= 0) {
                if (!(sourceSupport->mode >= BDM_MODE && sourceSupport->mode < ETH_MODE))
                    ps5DrawLaunchLoadingTransition();
                sourceSupport->itemLaunch(sourceSupport, sourceId, itemConfig);
            }
            readPads();
        } else if (getKeyOn(KEY_SQUARE)) {
            sfxPlay(SFX_CONFIRM);
            ps5GameOptionsSave(itemConfig);
            menuSaveConfig();
            guiMsgBox(_l(_STR_GAME_SETTINGS_SAVED), 0, NULL);
            ps5GameOptionsLoad(gameMenuLoadConfig(NULL));
            readPads();
        }

        if (getKeyOn(KEY_CIRCLE) || getKeyOn(KEY_START))
            guiSwitchScreen(GUI_SCREEN_MAIN);

        return;
    }

    if (getKey(KEY_UP)) {
        sfxPlay(SFX_CURSOR);
        if (gameMenuCurrent->prev)
            gameMenuCurrent = gameMenuCurrent->prev;
        else // rewind to the last item
            while (gameMenuCurrent->next)
                gameMenuCurrent = gameMenuCurrent->next;
    }

    if (getKey(KEY_DOWN)) {
        sfxPlay(SFX_CURSOR);
        if (gameMenuCurrent->next)
            gameMenuCurrent = gameMenuCurrent->next;
        else
            gameMenuCurrent = gameMenu;
    }

    if (getKeyOn(gSelectButton)) {
        // execute the item via looking at the id of it
        int menuID = gameMenuCurrent->item.id;
        int sourceId = selected_item->item->current->item.id;
        item_list_t *sourceSupport = selected_item->item->userdata;

        if (!oplResolveGameItem(sourceId, sourceSupport, &sourceSupport, &sourceId)) {
            sfxPlay(SFX_MESSAGE);
            return;
        }

        sfxPlay(SFX_CONFIRM);

        if (menuID == GAME_COMPAT_SETTINGS) {
            guiGameShowCompatConfig(sourceId, sourceSupport, itemConfig);
        } else if (menuID == GAME_CHEAT_SETTINGS) {
            guiGameShowCheatConfig();
        } else if (menuID == GAME_GSM_SETTINGS) {
            guiGameShowGSConfig();
        } else if (menuID == GAME_VMC_SETTINGS) {
            guiGameShowVMCMenu(sourceId, sourceSupport);
#ifdef PADEMU
        } else if (menuID == GAME_PADEMU_SETTINGS) {
            guiGameShowPadEmuConfig(0);
        } else if (menuID == GAME_PADMACRO_SETTINGS) {
            guiGameShowPadMacroConfig(0);
#endif
        } else if (menuID == GAME_OSD_LANGUAGE_SETTINGS) {
            guiGameShowOSDLanguageConfig(0);
        } else if (menuID == GAME_SAVE_CHANGES) {
            if (guiGameSaveConfig(itemConfig, sourceSupport))
                configSetInt(itemConfig, CONFIG_ITEM_CONFIGSOURCE, CONFIG_SOURCE_USER);
            menuSaveConfig();
            saveConfig(CONFIG_GAME, 0);
            guiMsgBox(_l(_STR_GAME_SETTINGS_SAVED), 0, NULL);
            guiGameLoadConfig(sourceSupport, gameMenuLoadConfig(NULL));
        } else if (menuID == GAME_TEST_CHANGES) {
            guiGameTestSettings(sourceId, sourceSupport, itemConfig);
        } else if (menuID == GAME_REMOVE_CHANGES) {
            if (guiGameShowRemoveSettings(itemConfig, configGetByType(CONFIG_GAME))) {
                guiGameLoadConfig(sourceSupport, gameMenuLoadConfig(NULL));
            }
        } else if (menuID == GAME_RENAME_GAME) {
            menuRenameGame(&gameMenu);
        } else if (menuID == GAME_DELETE_GAME) {
            menuDeleteGame(&gameMenu);
        }
        // so the exit press wont propagate twice
        readPads();
    }

    if (getKeyOn(KEY_START) || getKeyOn(gSelectButton == KEY_CIRCLE ? KEY_CROSS : KEY_CIRCLE)) {
        guiSwitchScreen(GUI_SCREEN_MAIN);
    }
}

void menuRenderAppMenu()
{
    guiDrawBGPlasma();

    if (!appMenu)
        return;

    // draw the animated menu
    if (!appMenuCurrent)
        appMenuCurrent = appMenu;

    submenu_list_t *it = appMenu;

    // calculate the number of items
    int count = 0;
    int sitem = 0;
    for (; it; count++, it = it->next) {
        if (it == appMenuCurrent)
            sitem = count;
    }

    int spacing = 25;
    int y = (gTheme->usedHeight >> 1) - (spacing * (count >> 1));
    int cp = 0; // current position

    // app title
    fntRenderString(gTheme->fonts[0], 320, 20, ALIGN_CENTER, 0, 0, selected_item->item->current->item.text, gTheme->selTextColor);

    for (it = appMenu; it; it = it->next, cp++) {
        // render, advance
        fntRenderString(gTheme->fonts[0], 320, y, ALIGN_CENTER, 0, 0, submenuItemGetText(&it->item), (cp == sitem) ? gTheme->selTextColor : gTheme->textColor);
        y += spacing;
    }

    // hints
    guiDrawSubMenuHints();
}

void menuHandleInputAppMenu()
{
    if (!appMenu)
        return;

    if (!appMenuCurrent)
        appMenuCurrent = appMenu;

    if (getKey(KEY_UP)) {
        sfxPlay(SFX_CURSOR);
        if (appMenuCurrent->prev)
            appMenuCurrent = appMenuCurrent->prev;
        else // rewind to the last item
            while (appMenuCurrent->next)
                appMenuCurrent = appMenuCurrent->next;
    }

    if (getKey(KEY_DOWN)) {
        sfxPlay(SFX_CURSOR);
        if (appMenuCurrent->next)
            appMenuCurrent = appMenuCurrent->next;
        else
            appMenuCurrent = appMenu;
    }

    if (getKeyOn(gSelectButton)) {
        // execute the item via looking at the id of it
        int menuID = appMenuCurrent->item.id;

        sfxPlay(SFX_CONFIRM);

        if (menuID == 0) {
            menuRenameGame(&appMenu);
        } else if (menuID == 1) {
            menuDeleteGame(&appMenu);
        }
        // so the exit press wont propagate twice
        readPads();
    }

    if (getKeyOn(KEY_START) || getKeyOn(gSelectButton == KEY_CIRCLE ? KEY_CROSS : KEY_CIRCLE)) {
        guiSwitchScreen(GUI_SCREEN_MAIN);
    }
}

submenu_list_t *menuGetMainMenu(void)
{
    return mainMenu;
}

submenu_list_t *menuGetMainMenuCurrent(void)
{
    return mainMenuCurrent;
}

void menuSetMainMenuCurrent(submenu_list_t *item)
{
    mainMenuCurrent = item;
}

menu_list_t *menuGetSelectedItem(void)
{
    return selected_item;
}
