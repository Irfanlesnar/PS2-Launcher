#include "include/opl.h"
#include "include/themes.h"
#include "include/util.h"
#include "include/gui.h"
#include "include/renderman.h"
#include "include/textures.h"
#include "include/ioman.h"
#include "include/fntsys.h"
#include "include/lang.h"
#include "include/pad.h"
#include "include/ps5covers.h"
#include "include/sound.h"
#include "include/bdmsupport.h"
#include "include/supportbase.h"
#include <fcntl.h>
#include <unistd.h>
#include <malloc.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdio.h>
#include <kernel.h>
#include "httpclient.h"
#include <stdarg.h>

#define MENU_POS_V     50
#define HINT_HEIGHT    32
#define DECORATOR_SIZE 20

extern const char conf_theme_OPL_cfg;
extern u16 size_conf_theme_OPL_cfg;

theme_t *gTheme;

static int screenWidth;
static int screenHeight;
static int guiThemeID = 0;

static int nThemes = 0;
static theme_file_t themes[THM_MAX_FILES];
static const char **guiThemesNames = NULL;

// Global data
theme_t *gTheme;



#define DISPLAY_ALWAYS  0
#define DISPLAY_DEFINED 1
#define DISPLAY_NEVER   2

#define SIZING_NONE -1
#define SIZING_CLIP 0
#define SIZING_WRAP 1

static const char *elementsType[ELEM_TYPE_COUNT] = {
    "AttributeText",
    "StaticText",
    "AttributeImage",
    "GameImage",
    "StaticImage",
    "Background",
    "MenuIcon",
    "MenuText",
    "ItemsList",
    "ItemIcon",
    "ItemCover",
    "ItemText",
    "HintText",
    "InfoHintText",
    "LoadingIcon",
    "BdmIndex",
    "GameCountText"};

// Common functions for Text ////////////////////////////////////////////////////////////////////////////////////////////////

static void endMutableText(theme_element_t *elem)
{
    mutable_text_t *mutableText = (mutable_text_t *)elem->extended;
    if (mutableText) {
        if (mutableText->value)
            free(mutableText->value);

        if (mutableText->alias)
            free(mutableText->alias);

        free(mutableText);
    }

    free(elem);
}

static mutable_text_t *initMutableText(const char *themePath, config_set_t *themeConfig, theme_t *theme, const char *name, int type, struct theme_element *elem, const char *value, const char *alias, int displayMode, int sizingMode)
{
    mutable_text_t *mutableText = (mutable_text_t *)malloc(sizeof(mutable_text_t));
    mutableText->currentConfigId = 0;
    mutableText->currentValue = NULL;
    mutableText->alias = NULL;

    char elemProp[64];

    snprintf(elemProp, sizeof(elemProp), "%s_display", name);
    configGetInt(themeConfig, elemProp, &displayMode);
    mutableText->displayMode = displayMode;

    int length = strlen(value) + 1;
    mutableText->value = (char *)malloc(length * sizeof(char));
    memcpy(mutableText->value, value, length);

    snprintf(elemProp, sizeof(elemProp), "%s_wrap", name);
    if (configGetInt(themeConfig, elemProp, &sizingMode)) {
        if (sizingMode > 0)
            sizingMode = SIZING_WRAP;
    }

    if ((elem->width != DIM_UNDEF) || (elem->height != DIM_UNDEF)) {
        if (sizingMode == SIZING_NONE)
            sizingMode = SIZING_CLIP;

        if (elem->width == DIM_UNDEF)
            elem->width = screenWidth;

        if (elem->height == DIM_UNDEF)
            elem->height = screenHeight;
    } else
        sizingMode = SIZING_NONE;
    mutableText->sizingMode = sizingMode;

    if (type == ELEM_TYPE_ATTRIBUTE_TEXT) {
        snprintf(elemProp, sizeof(elemProp), "%s_title", name);
        configGetStr(themeConfig, elemProp, &alias);
        if (!alias) {
            if (value[0] == '#')
                alias = &value[1];
            else
                alias = value;
        }

        char *temp;
        if (!strncmp(alias, "Title", 5))
            temp = _l(_STR_INFO_TITLE);
        else if (!strncmp(alias, "Genre", 5))
            temp = _l(_STR_INFO_GENRE);
        else if (!strncmp(alias, "Release", 7))
            temp = _l(_STR_INFO_RELEASE);
        else if (!strncmp(alias, "Developer", 9))
            temp = _l(_STR_INFO_DEVELOPER);
        else if (!strncmp(alias, "Size", 4))
            temp = _l(_STR_SIZE);
        else if (!strncmp(alias, "Description", 11))
            temp = _l(_STR_INFO_DESCRIPTION);
        else
            temp = (char *)alias;

        length = strlen(temp) + 1 + 2;
        mutableText->alias = (char *)calloc(length, sizeof(char));
        if (mutableText->sizingMode == SIZING_WRAP)
            snprintf(mutableText->alias, length, "%s:\n", temp);
        else
            snprintf(mutableText->alias, length, "%s: ", temp);
    } else {
        if (mutableText->sizingMode == SIZING_WRAP)
            fntFitString(elem->font, mutableText->value, elem->width);
    }

    return mutableText;
}

// StaticText ///////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void drawStaticText(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    mutable_text_t *mutableText = (mutable_text_t *)elem->extended;
    if (mutableText->sizingMode == SIZING_NONE)
        fntRenderString(elem->font, elem->posX, elem->posY, elem->aligned, 0, 0, mutableText->value, elem->color);
    else
        fntRenderString(elem->font, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, mutableText->value, elem->color);
}

static void initStaticText(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name)
{
    const char *value;
    char elemProp[64];

    snprintf(elemProp, sizeof(elemProp), "%s_value", name);
    configGetStr(themeConfig, elemProp, &value);
    if (value) {
        elem->extended = initMutableText(themePath, themeConfig, theme, name, ELEM_TYPE_STATIC_TEXT, elem, value, NULL, DISPLAY_ALWAYS, SIZING_NONE);
        elem->endElem = &endMutableText;
        elem->drawElem = &drawStaticText;
    } else
        LOG("THEMES StaticText %s: NO value, elem disabled !!\n", name);
}

// GameCountText ////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int getGameCount(void *support)
{
    item_list_t *list = (item_list_t *)support;
    return list->itemGetCount(list);
}

static void drawGameCountText(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    mutable_text_t *mutableText = (mutable_text_t *)elem->extended;

    if (config) {
        if (mutableText->currentConfigId != config->uid) {
            // force refresh
            mutableText->currentConfigId = config->uid;

            int count = getGameCount(menu->item->userdata);
            snprintf(mutableText->value, sizeof(char) * 60, _l(_STR_FILE_COUNT), count);
        }
    }

    fntRenderString(elem->font, elem->posX, elem->posY, elem->aligned, 0, 0, mutableText->value, elem->color);
}

static void initGameCountText(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name)
{
    int length = 60;
    char *countStr = (char *)malloc(length * sizeof(char));
    memset(countStr, 0, length * sizeof(char));

    elem->extended = initMutableText(themePath, themeConfig, theme, name, ELEM_TYPE_ATTRIBUTE_TEXT, elem, countStr, NULL, DISPLAY_ALWAYS, SIZING_NONE);
    elem->endElem = &endMutableText;
    elem->drawElem = &drawGameCountText;
}

// AttributeText ////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void drawAttributeText(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    mutable_text_t *mutableText = (mutable_text_t *)elem->extended;
    if (config) {
        if (mutableText->currentConfigId != config->uid) {
            // force refresh
            mutableText->currentConfigId = config->uid;
            mutableText->currentValue = NULL;
            if (configGetStr(config, mutableText->value, (const char **)&mutableText->currentValue)) {
                if (mutableText->sizingMode == SIZING_WRAP)
                    fntFitString(elem->font, mutableText->currentValue, elem->width);
            }
        }
        if (mutableText->currentValue) {
            char result[300];
            if (mutableText->displayMode == DISPLAY_NEVER) {
                if (!strncmp(mutableText->alias, _l(_STR_SIZE), strlen(_l(_STR_SIZE)))) {
                    snprintf(result, sizeof(result), "%s MiB", mutableText->currentValue);
                    if (mutableText->sizingMode == SIZING_NONE)
                        fntRenderString(elem->font, elem->posX, elem->posY, elem->aligned, 0, 0, result, elem->color);
                    else
                        fntRenderString(elem->font, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, result, elem->color);
                } else {
                    if (mutableText->sizingMode == SIZING_NONE)
                        fntRenderString(elem->font, elem->posX, elem->posY, elem->aligned, 0, 0, mutableText->currentValue, elem->color);
                    else
                        fntRenderString(elem->font, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, mutableText->currentValue, elem->color);
                }
            } else {
                if (!strncmp(mutableText->alias, _l(_STR_SIZE), strlen(_l(_STR_SIZE))))
                    snprintf(result, sizeof(result), "%s%s MiB", mutableText->alias, mutableText->currentValue);
                else
                    snprintf(result, sizeof(result), "%s%s", mutableText->alias, mutableText->currentValue);
                if (mutableText->sizingMode == SIZING_NONE)
                    fntRenderString(elem->font, elem->posX, elem->posY, elem->aligned, 0, 0, result, elem->color);
                else
                    fntRenderString(elem->font, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, result, elem->color);
            }
            return;
        }
    }
    if (mutableText->displayMode == DISPLAY_ALWAYS) {
        if (mutableText->sizingMode == SIZING_NONE)
            fntRenderString(elem->font, elem->posX, elem->posY, elem->aligned, 0, 0, mutableText->alias, elem->color);
        else
            fntRenderString(elem->font, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, mutableText->alias, elem->color);
    }
}

static void initAttributeText(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name)
{
    const char *attribute;
    char elemProp[64];

    snprintf(elemProp, sizeof(elemProp), "%s_attribute", name);
    configGetStr(themeConfig, elemProp, &attribute);
    if (attribute) {
        elem->extended = initMutableText(themePath, themeConfig, theme, name, ELEM_TYPE_ATTRIBUTE_TEXT, elem, attribute, NULL, DISPLAY_ALWAYS, SIZING_NONE);
        elem->endElem = &endMutableText;
        elem->drawElem = &drawAttributeText;
    } else
        LOG("THEMES AttributeText %s: NO attribute, elem disabled !!\n", name);
}

// Common functions for Image ///////////////////////////////////////////////////////////////////////////////////////////////

static void findDuplicate(theme_element_t *first, const char *cachePattern, const char *defaultTexture, const char *overlayTexture, mutable_image_t *target)
{
    theme_element_t *elem = first;
    while (elem) {
        if ((elem->type == ELEM_TYPE_STATIC_IMAGE) || (elem->type == ELEM_TYPE_ATTRIBUTE_IMAGE) || (elem->type == ELEM_TYPE_GAME_IMAGE) || (elem->type == ELEM_TYPE_BACKGROUND)) {
            mutable_image_t *source = (mutable_image_t *)elem->extended;

            if (cachePattern && source->cache && !strcmp(cachePattern, source->cache->suffix)) {
                target->cache = source->cache;
                target->cacheLinked = 1;
                LOG("THEMES Re-using a cache for pattern %s\n", cachePattern);
            }

            if (defaultTexture && source->defaultTexture && !strcmp(defaultTexture, source->defaultTexture->name)) {
                target->defaultTexture = source->defaultTexture;
                target->defaultTextureLinked = 1;
                LOG("THEMES Re-using the default texture for %s\n", defaultTexture);
            }

            if (overlayTexture && source->overlayTexture && !strcmp(overlayTexture, source->overlayTexture->name)) {
                target->overlayTexture = source->overlayTexture;
                target->overlayTextureLinked = 1;
                LOG("THEMES Re-using the overlay texture for %s\n", overlayTexture);
            }
        }

        elem = elem->next;
    }
}

static void freeImageTexture(image_texture_t *texture)
{
    if (texture) {
        if (texture->source.Mem) {
            rmUnloadTexture(&texture->source);
            free(texture->source.Mem);
            texture->source.Mem = NULL;
        }
        if (texture->source.Clut) {
            free(texture->source.Clut);
            texture->source.Clut = NULL;
        }
        if (texture->name) {
            free(texture->name);
            texture->name = NULL;
        }
        free(texture);
    }
}

static image_texture_t *initImageTexture(const char *themePath, config_set_t *themeConfig, const char *name, const char *imgName, int isOverlay)
{
    image_texture_t *texture = (image_texture_t *)malloc(sizeof(image_texture_t));
    texture->name = NULL;

    int texId = -1;
    int result = 0;

    if (themePath) {
        char path[256];
        snprintf(path, sizeof(path), "%s%s", themePath, imgName);
        if (texDiscoverLoad(&texture->source, path, texId) >= 0)
            ;
        result = 1;
    } else {
        texId = texLookupInternalTexId(imgName);
        if (texLoadInternal(&texture->source, texId) >= 0)
            ;
        result = 1;
    }

    if (result) {
        int length = strlen(imgName) + 1;
        texture->name = (char *)malloc(length * sizeof(char));
        memcpy(texture->name, imgName, length);

        if (isOverlay) {
            int intValue;
            char elemProp[64];
            snprintf(elemProp, sizeof(elemProp), "%s_overlay_ulx", name);
            if (configGetInt(themeConfig, elemProp, &intValue))
                texture->upperLeft_x = intValue;
            snprintf(elemProp, sizeof(elemProp), "%s_overlay_uly", name);
            if (configGetInt(themeConfig, elemProp, &intValue))
                texture->upperLeft_y = intValue;
            snprintf(elemProp, sizeof(elemProp), "%s_overlay_urx", name);
            if (configGetInt(themeConfig, elemProp, &intValue))
                texture->upperRight_x = intValue;
            snprintf(elemProp, sizeof(elemProp), "%s_overlay_ury", name);
            if (configGetInt(themeConfig, elemProp, &intValue))
                texture->upperRight_y = intValue;
            snprintf(elemProp, sizeof(elemProp), "%s_overlay_llx", name);
            if (configGetInt(themeConfig, elemProp, &intValue))
                texture->lowerLeft_x = intValue;
            snprintf(elemProp, sizeof(elemProp), "%s_overlay_lly", name);
            if (configGetInt(themeConfig, elemProp, &intValue))
                texture->lowerLeft_y = intValue;
            snprintf(elemProp, sizeof(elemProp), "%s_overlay_lrx", name);
            if (configGetInt(themeConfig, elemProp, &intValue))
                texture->lowerRight_x = intValue;
            snprintf(elemProp, sizeof(elemProp), "%s_overlay_lry", name);
            if (configGetInt(themeConfig, elemProp, &intValue))
                texture->lowerRight_y = intValue;
        }
    } else {
        freeImageTexture(texture);
        texture = NULL;
    }

    return texture;
}

static image_texture_t *initImageInternalTexture(config_set_t *themeConfig, const char *name)
{
    image_texture_t *texture = (image_texture_t *)malloc(sizeof(image_texture_t));
    texture->name = NULL;
    int result;

    if ((result = texLookupInternalTexId(name)) >= 0) {
        result = texLoadInternal(&texture->source, result);
        int length = strlen(name) + 1;
        texture->name = (char *)malloc(length * sizeof(char));
        memcpy(texture->name, name, length);
    }

    if (result < 0) {
        freeImageTexture(texture);
        texture = NULL;
    }

    return texture;
}

static void endMutableImage(struct theme_element *elem)
{
    mutable_image_t *mutableImage = (mutable_image_t *)elem->extended;
    if (mutableImage) {
        if (mutableImage->cache && !mutableImage->cacheLinked)
            cacheDestroyCache(mutableImage->cache);

        if (mutableImage->defaultTexture && !mutableImage->defaultTextureLinked)
            freeImageTexture(mutableImage->defaultTexture);

        if (mutableImage->overlayTexture && !mutableImage->overlayTextureLinked)
            freeImageTexture(mutableImage->overlayTexture);

        free(mutableImage);
    }

    free(elem);
}

static mutable_image_t *initMutableImage(const char *themePath, config_set_t *themeConfig, theme_t *theme, const char *name, int type, const char *cachePattern, int cacheCount, const char *defaultTexture, const char *overlayTexture)
{
    mutable_image_t *mutableImage = (mutable_image_t *)malloc(sizeof(mutable_image_t));
    mutableImage->currentUid = -1;
    mutableImage->currentConfigId = 0;
    mutableImage->currentValue = NULL;
    mutableImage->cache = NULL;
    mutableImage->cacheLinked = 0;
    mutableImage->defaultTexture = NULL;
    mutableImage->defaultTextureLinked = 0;
    mutableImage->overlayTexture = NULL;
    mutableImage->overlayTextureLinked = 0;

    char elemProp[64];

    if (type == ELEM_TYPE_ATTRIBUTE_IMAGE) {
        snprintf(elemProp, sizeof(elemProp), "%s_attribute", name);
        configGetStr(themeConfig, elemProp, &cachePattern);
        LOG("THEMES MutableImage %s: type: %s using cache pattern: %s\n", name, elementsType[type], cachePattern);
    } else if ((type == ELEM_TYPE_GAME_IMAGE) || (type == ELEM_TYPE_BACKGROUND)) {
        snprintf(elemProp, sizeof(elemProp), "%s_pattern", name);
        configGetStr(themeConfig, elemProp, &cachePattern);
        snprintf(elemProp, sizeof(elemProp), "%s_count", name);
        configGetInt(themeConfig, elemProp, &cacheCount);
        LOG("THEMES MutableImage %s: type: %s using cache pattern: %s count: %d\n", name, elementsType[type], cachePattern, cacheCount);
    }

    snprintf(elemProp, sizeof(elemProp), "%s_default", name);
    configGetStr(themeConfig, elemProp, &defaultTexture);

    if (type != ELEM_TYPE_BACKGROUND) {
        snprintf(elemProp, sizeof(elemProp), "%s_overlay", name);
        configGetStr(themeConfig, elemProp, &overlayTexture);
    }

    findDuplicate(theme->mainElems.first, cachePattern, defaultTexture, overlayTexture, mutableImage);
    findDuplicate(theme->infoElems.first, cachePattern, defaultTexture, overlayTexture, mutableImage);
    findDuplicate(theme->appsMainElems.first, cachePattern, defaultTexture, overlayTexture, mutableImage);
    findDuplicate(theme->appsInfoElems.first, cachePattern, defaultTexture, overlayTexture, mutableImage);

    if (cachePattern && !mutableImage->cache) {
        if (type == ELEM_TYPE_ATTRIBUTE_IMAGE)
            mutableImage->cache = cacheInitCache(-1, themePath, 0, cachePattern, 1);
        else
            mutableImage->cache = cacheInitCache(theme->gameCacheCount++, "ART", 1, cachePattern, cacheCount);
    }

    if (!themePath)
        if (defaultTexture && !mutableImage->defaultTexture)
            mutableImage->defaultTexture = initImageInternalTexture(themeConfig, defaultTexture);

    if (defaultTexture && !mutableImage->defaultTexture)
        mutableImage->defaultTexture = initImageTexture(themePath, themeConfig, name, defaultTexture, 0);

    if (overlayTexture && !mutableImage->overlayTexture)
        mutableImage->overlayTexture = initImageTexture(themePath, themeConfig, name, overlayTexture, 1);

    return mutableImage;
}

// StaticImage //////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void drawStaticImage(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    if (gPS5Mode) {
        if (elem->type == ELEM_TYPE_BACKGROUND) {
            guiDrawBGPlasma();
        }
        return;
    }

    mutable_image_t *staticImage = (mutable_image_t *)elem->extended;
    if (staticImage->overlayTexture) {
        rmDrawOverlayPixmap(&staticImage->overlayTexture->source, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, elem->scaled, gDefaultCol,
                            &staticImage->defaultTexture->source, staticImage->overlayTexture->upperLeft_x, staticImage->overlayTexture->upperLeft_y, staticImage->overlayTexture->upperRight_x, staticImage->overlayTexture->upperRight_y,
                            staticImage->overlayTexture->lowerLeft_x, staticImage->overlayTexture->lowerLeft_y, staticImage->overlayTexture->lowerRight_x, staticImage->overlayTexture->lowerRight_y);
    } else
        rmDrawPixmap(&staticImage->defaultTexture->source, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, elem->scaled, gDefaultCol);
}

static void initStaticImage(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name, const char *imageName)
{
    mutable_image_t *mutableImage = initMutableImage(themePath, themeConfig, theme, name, elem->type, NULL, 0, imageName, NULL);
    elem->extended = mutableImage;
    elem->endElem = &endMutableImage;

    if (mutableImage->defaultTexture)
        elem->drawElem = &drawStaticImage;
    else
        LOG("THEMES StaticImage %s: NO image name, elem disabled !!\n", name);
}

// GameImage ////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static item_list_t *resolveThemeGameItem(void *fallbackSupport, int itemId, int *sourceId)
{
    item_list_t *support = (item_list_t *)fallbackSupport;
    if (!oplResolveGameItem(itemId, support, &support, sourceId))
        return NULL;
    return support;
}

static int themeGameItemIsAvailable(void *fallbackSupport, int itemId)
{
    int sourceId;
    item_list_t *support = resolveThemeGameItem(fallbackSupport, itemId, &sourceId);
    opl_io_module_t *owner;
    int encodedItem = oplIsGameItemIdEncoded(itemId);

    if (support == NULL || !support->enabled || sourceId < 0)
        return 0;

    owner = (opl_io_module_t *)support->owner;
    if (owner == NULL)
        return 0;

    if (!encodedItem && owner->menuItem.visible == 0)
        return 0;

    if (support->itemGetCount && sourceId >= support->itemGetCount(support))
        return 0;

    return 1;
}

static GSTEXTURE *getGameImageTexture(image_cache_t *cache, void *support, struct submenu_item *item)
{
    if (gEnableArt) {
        int sourceId;
        item_list_t *list = resolveThemeGameItem(support, item->id, &sourceId);
        if (list != NULL) {
            char *startup = list->itemGetStartup(list, sourceId);
            return cacheGetTexture(cache, list, &item->cache_id[cache->userId], &item->cache_uid[cache->userId], startup);
        }
    }

    return NULL;
}

static void drawGameImage(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    if (gPS5Mode) {
        if (elem->type == ELEM_TYPE_BACKGROUND) {
            guiDrawBGPlasma();
        }
        return;
    }

    mutable_image_t *gameImage = (mutable_image_t *)elem->extended;
    if (item) {
        GSTEXTURE *texture = getGameImageTexture(gameImage->cache, menu->item->userdata, &item->item);
        if (!texture || !texture->Mem) {
            if (gameImage->defaultTexture)
                texture = &gameImage->defaultTexture->source;
            else {
                if (elem->type == ELEM_TYPE_BACKGROUND)
                    guiDrawBGPlasma();
                return;
            }
        }

        if (gameImage->overlayTexture) {
            rmDrawOverlayPixmap(&gameImage->overlayTexture->source, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, elem->scaled, gDefaultCol,
                                texture, gameImage->overlayTexture->upperLeft_x, gameImage->overlayTexture->upperLeft_y, gameImage->overlayTexture->upperRight_x, gameImage->overlayTexture->upperRight_y,
                                gameImage->overlayTexture->lowerLeft_x, gameImage->overlayTexture->lowerLeft_y, gameImage->overlayTexture->lowerRight_x, gameImage->overlayTexture->lowerRight_y);
        } else
            rmDrawPixmap(texture, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, elem->scaled, gDefaultCol);

    } else if (elem->type == ELEM_TYPE_BACKGROUND) {
        if (gameImage->defaultTexture)
            rmDrawPixmap(&gameImage->defaultTexture->source, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, elem->scaled, gDefaultCol);
        else
            guiDrawBGPlasma();
    }
}

static void initGameImage(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name, const char *pattern, int count, const char *texture, const char *overlay)
{
    mutable_image_t *mutableImage = initMutableImage(themePath, themeConfig, theme, name, elem->type, pattern, count, texture, overlay);
    elem->extended = mutableImage;
    elem->endElem = &endMutableImage;

    if (mutableImage->cache)
        elem->drawElem = &drawGameImage;
    else
        LOG("THEMES GameImage %s: NO pattern, elem disabled !!\n", name);
}

// AttributeImage ///////////////////////////////////////////////////////////////////////////////////////////////////////////

static void drawAttributeImage(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    mutable_image_t *attributeImage = (mutable_image_t *)elem->extended;
    if (config) {
        if (attributeImage->currentConfigId != config->uid) {
            // force refresh
            attributeImage->currentUid = -1;
            attributeImage->currentConfigId = config->uid;
            attributeImage->currentValue = NULL;
            configGetStr(config, attributeImage->cache->suffix, (const char **)&attributeImage->currentValue);
        }
        if (attributeImage->currentValue) {
            if (thmGetGuiValue() == 0) {
                int texId;
                char *seppos = strchr(attributeImage->currentValue, '/');
                if (!seppos)
                    texId = texLookupInternalTexId(attributeImage->currentValue);
                else {
                    char imgName[32];
                    snprintf(imgName, sizeof(imgName), "%s_%s", attributeImage->cache->suffix, &seppos[1]);
                    texId = texLookupInternalTexId(&imgName[0]);
                }
                GSTEXTURE *texture = thmGetTexture(texId);
                if (texture && texture->Mem)
                    rmDrawPixmap(texture, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, elem->scaled, gDefaultCol);

                return;
            } else {
                int posZ = 0;
                GSTEXTURE *texture = cacheGetTexture(attributeImage->cache, menu->item->userdata, &posZ, &attributeImage->currentUid, attributeImage->currentValue);
                if (texture && texture->Mem) {
                    if (attributeImage->overlayTexture) {
                        rmDrawOverlayPixmap(&attributeImage->overlayTexture->source, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, elem->scaled, gDefaultCol,
                                            texture, attributeImage->overlayTexture->upperLeft_x, attributeImage->overlayTexture->upperLeft_y, attributeImage->overlayTexture->upperRight_x, attributeImage->overlayTexture->upperRight_y,
                                            attributeImage->overlayTexture->lowerLeft_x, attributeImage->overlayTexture->lowerLeft_y, attributeImage->overlayTexture->lowerRight_x, attributeImage->overlayTexture->lowerRight_y);
                    } else
                        rmDrawPixmap(texture, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, elem->scaled, gDefaultCol);

                    return;
                }
            }
        }
    }
    if (attributeImage->defaultTexture)
        rmDrawPixmap(&attributeImage->defaultTexture->source, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, elem->scaled, gDefaultCol);
}

static void initAttributeImage(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name)
{
    mutable_image_t *mutableImage = initMutableImage(themePath, themeConfig, theme, name, elem->type, NULL, 1, NULL, NULL);
    elem->extended = mutableImage;
    elem->endElem = &endMutableImage;

    if (mutableImage->cache)
        elem->drawElem = &drawAttributeImage;
    else
        LOG("THEMES AttributeImage %s: NO attribute, elem disabled !!\n", name);
}

// BasicElement /////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void endBasic(theme_element_t *elem)
{
    if (elem->extended)
        free(elem->extended);

    free(elem);
}

static theme_element_t *initBasic(const char *themePath, config_set_t *themeConfig, theme_t *theme, const char *name, int type, int x, int y, short aligned, int w, int h, short scaled, u64 color, int font)
{
    int intValue;
    unsigned char charColor[3];
    const char *temp;
    char elemProp[64];

    theme_element_t *elem = (theme_element_t *)malloc(sizeof(theme_element_t));

    elem->type = type;
    elem->extended = NULL;
    elem->drawElem = NULL;
    elem->endElem = &endBasic;
    elem->next = NULL;

    snprintf(elemProp, sizeof(elemProp), "%s_x", name);
    if (configGetStr(themeConfig, elemProp, &temp)) {
        if (!strncmp(temp, "POS_MID", 7))
            x = screenWidth >> 1;
        else
            x = atoi(temp);
    }
    if (x < 0)
        elem->posX = screenWidth + x;
    else
        elem->posX = x;

    snprintf(elemProp, sizeof(elemProp), "%s_y", name);
    if (configGetStr(themeConfig, elemProp, &temp)) {
        if (!strncmp(temp, "POS_MID", 7))
            y = screenHeight >> 1;
        else
            y = atoi(temp);
    }
    if (y < 0)
        elem->posY = ceil((screenHeight + y) * theme->usedHeight / screenHeight);
    else
        elem->posY = y;

    snprintf(elemProp, sizeof(elemProp), "%s_width", name);
    if (configGetStr(themeConfig, elemProp, &temp)) {
        if (!strncmp(temp, "DIM_INF", 7))
            elem->width = screenWidth;
        else
            elem->width = atoi(temp);
    } else
        elem->width = w;

    snprintf(elemProp, sizeof(elemProp), "%s_height", name);
    if (configGetStr(themeConfig, elemProp, &temp)) {
        if (!strncmp(temp, "DIM_INF", 7))
            elem->height = screenHeight;
        else
            elem->height = atoi(temp);
    } else
        elem->height = h;

    snprintf(elemProp, sizeof(elemProp), "%s_aligned", name);
    if (configGetInt(themeConfig, elemProp, &intValue))
        elem->aligned = (intValue == 0) ? ALIGN_NONE : ALIGN_CENTER;
    else
        elem->aligned = aligned;

    snprintf(elemProp, sizeof(elemProp), "%s_scaled", name);
    if (configGetInt(themeConfig, elemProp, &intValue))
        elem->scaled = (intValue == 0) ? SCALING_NONE : SCALING_RATIO;
    else
        elem->scaled = scaled;

    snprintf(elemProp, sizeof(elemProp), "%s_color", name);
    if (configGetColor(themeConfig, elemProp, charColor))
        elem->color = GS_SETREG_RGBA(charColor[0], charColor[1], charColor[2], 0x80);
    else
        elem->color = color;

    elem->font = font;
    snprintf(elemProp, sizeof(elemProp), "%s_font", name);
    if (configGetInt(themeConfig, elemProp, &intValue)) {
        if (intValue > 0 && intValue < THM_MAX_FONTS)
            elem->font = theme->fonts[intValue];
    }

    return elem;
}

// Internal elements ////////////////////////////////////////////////////////////////////////////////////////////////////////
static void drawBackground(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    guiDrawBGPlasma();
}

static void initBackground(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name, const char *pattern, int count, const char *texture)
{
    mutable_image_t *mutableImage = initMutableImage(themePath, themeConfig, theme, name, elem->type, pattern, count, texture, NULL);
    elem->extended = mutableImage;
    elem->endElem = &endMutableImage;

    if (mutableImage->cache)
        elem->drawElem = &drawGameImage;
    else if (mutableImage->defaultTexture)
        elem->drawElem = &drawStaticImage;
    else
        elem->drawElem = &drawBackground;
}

static void drawMenuIcon(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    if (gPS5Mode)
        return;

    GSTEXTURE *menuIconTex = thmGetTexture(menu->item->icon_id);
    if (menuIconTex && menuIconTex->Mem)
        rmDrawPixmap(menuIconTex, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, elem->scaled, gDefaultCol);
}

static int findMenuNext(struct menu_list *menu)
{
    struct menu_list *next = menu->next;
    while (next != NULL && next->item->visible == 0)
        next = next->next;

    return next == NULL ? 0 : next->item->visible;
}

static int findMenuPrev(struct menu_list *menu)
{
    struct menu_list *prev = menu->prev;
    while (prev != NULL && prev->item->visible == 0)
        prev = prev->prev;

    return prev == NULL ? 0 : prev->item->visible;
}

static void drawMenuText(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    if (gPS5Mode)
        return;

    GSTEXTURE *leftIconTex = NULL, *rightIconTex = NULL;
    if (findMenuPrev(menu) != 0)
        leftIconTex = thmGetTexture(LEFT_ICON);
    if (findMenuNext(menu) != 0)
        rightIconTex = thmGetTexture(RIGHT_ICON);

    if (elem->aligned) {
        int offset = elem->width >> 1;
        if (leftIconTex && leftIconTex->Mem)
            rmDrawPixmap(leftIconTex, elem->posX - offset, elem->posY, elem->aligned, 20, 20, elem->scaled, gDefaultCol);
        if (rightIconTex && rightIconTex->Mem)
            rmDrawPixmap(rightIconTex, elem->posX + offset, elem->posY, elem->aligned, 20, 20, elem->scaled, gDefaultCol);
    } else {
        if (leftIconTex && leftIconTex->Mem)
            rmDrawPixmap(leftIconTex, elem->posX - leftIconTex->Width, elem->posY, elem->aligned, 20, 20, elem->scaled, gDefaultCol);
        if (rightIconTex && rightIconTex->Mem)
            rmDrawPixmap(rightIconTex, elem->posX + elem->width, elem->posY, elem->aligned, 20, 20, elem->scaled, gDefaultCol);
    }
    fntRenderString(elem->font, elem->posX, elem->posY, elem->aligned, 0, 0, menuItemGetText(menu->item), elem->color);
}

static void drawBDMIndex(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    item_list_t *itemList = menu->item->userdata;
    // Only render for bdm modes and if current mode is visible
    if (itemList->mode >= ETH_MODE || menu->item->visible == 0)
        return;

    // Only render if multiple mass devices are connected
    if (itemList->mode == 0 && menu->next->item->visible == 0)
        return;

    char imgName[32];
    snprintf(imgName, sizeof(imgName), "Index_%d", itemList->mode);

    GSTEXTURE *indexTex = thmGetTexture(texLookupInternalTexId(&imgName[0]));
    if (indexTex && indexTex->Mem)
        rmDrawPixmap(indexTex, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, elem->scaled, gDefaultCol);
}

static int gPS5RegFont = -1;
static int gPS5SemiBoldFont = -1;
static int gPS5BoldFont = -1;
static int gPS5HeaderFont = -1;
static int gPS5TitleFont = -1;
static int gPS5SmallFont = -1;
static float gPS5AnimPos = -1.0f;

int gPS5ActiveTab = 0; // 0 = Games, 1 = Settings, 2 = Apps
static int gPS5UserHasNavigated = 0;
GSTEXTURE gPS5InstagramTex;
int gPS5InstagramTexLoaded = 0;

static GSTEXTURE gPS5MaskTex;
static GSTEXTURE gPS5InvMaskTex;
static int gPS5MasksInitialized = 0;

extern void *roboto_regular_raw;
extern int size_roboto_regular_raw;
extern void *roboto_bold_raw;
extern int size_roboto_bold_raw;
extern void *roboto_semi_bold_raw;
extern int size_roboto_semi_bold_raw;

static void initPS5MaskTextures(void)
{
    if (gPS5MasksInitialized) return;

    // 1. Override slot 0 default font with embedded roboto_regular.ttf
    if (fntLoadDefaultMem(&roboto_regular_raw, size_roboto_regular_raw) == 0) {
        LOG("THEMES Overrode default slot 0 font with embedded roboto_regular\n");
    } else {
        LOG("THEMES Failed to override slot 0 with embedded roboto_regular!\n");
    }

    // Set default system theme font slot to 0
    if (gTheme) {
        gTheme->fonts[0] = 0;
    }

    // Set gPS5RegFont to use default slot 0 (Roboto Regular)
    gPS5RegFont = 0;

    // 2. Load embedded roboto_bold.ttf into Header and Title fonts
    gPS5HeaderFont = fntLoadFileMem(&roboto_bold_raw, size_roboto_bold_raw, 20);
    gPS5TitleFont = fntLoadFileMem(&roboto_bold_raw, size_roboto_bold_raw, 34);
    gPS5SmallFont = fntLoadFileMem(&roboto_regular_raw, size_roboto_regular_raw, 13);
    gPS5SemiBoldFont = fntLoadFileMem(&roboto_semi_bold_raw, size_roboto_semi_bold_raw, 16);
    gPS5BoldFont = gPS5HeaderFont; // Compatibility fallback
    if (gPS5HeaderFont != -1 && gPS5TitleFont != -1) {
        LOG("THEMES Loaded PS5 bold header (20) and title (34) fonts from embedded roboto_bold\n");
    } else {
        LOG("THEMES Failed to load embedded roboto_bold!\n");
        if (gPS5HeaderFont == -1) gPS5HeaderFont = 0;
        if (gPS5TitleFont == -1) gPS5TitleFont = 0;
        gPS5BoldFont = 0;
    }
    if (gPS5SemiBoldFont == -1)
        gPS5SemiBoldFont = gPS5RegFont;
    if (gPS5SmallFont == -1) {
        gPS5SmallFont = 0;
    }

    int width = 128;
    int height = 128;
    int size = gsKit_texture_size_ee(width, height, GS_PSM_CT32);

    // 3. Standard Mask Texture (128x128) - Nearest filtered to avoid segment seams
    gPS5MaskTex.Width = width;
    gPS5MaskTex.Height = height;
    gPS5MaskTex.PSM = GS_PSM_CT32;
    gPS5MaskTex.Filter = GS_FILTER_NEAREST;
    gPS5MaskTex.Delayed = 1;
    gPS5MaskTex.Mem = memalign(128, size);

    // 4. Inverse Mask Texture (128x128) - Nearest filtered to avoid segment seams
    gPS5InvMaskTex.Width = width;
    gPS5InvMaskTex.Height = height;
    gPS5InvMaskTex.PSM = GS_PSM_CT32;
    gPS5InvMaskTex.Filter = GS_FILTER_NEAREST;
    gPS5InvMaskTex.Delayed = 1;
    gPS5InvMaskTex.Mem = memalign(128, size);

    struct pixel_32 { u8 r, g, b, a; };
    struct pixel_32 *pixels = (struct pixel_32 *)gPS5MaskTex.Mem;
    struct pixel_32 *invPixels = (struct pixel_32 *)gPS5InvMaskTex.Mem;

    int tr = 32; // Corner radius inside 128x128 texture space
    int x, y;
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            int idx = y * width + x;
            
            pixels[idx].r = 255;
            pixels[idx].g = 255;
            pixels[idx].b = 255;

            invPixels[idx].r = 255;
            invPixels[idx].g = 255;
            invPixels[idx].b = 255;

            int dx = 0;
            int dy = 0;
            int isCorner = 0;

            // Determine if coordinate is inside one of the 4 corner quadrants
            if (x < tr && y < tr) {
                dx = tr - 1 - x;
                dy = tr - 1 - y;
                isCorner = 1;
            } else if (x >= width - tr && y < tr) {
                dx = x - (width - tr);
                dy = tr - 1 - y;
                isCorner = 1;
            } else if (x < tr && y >= height - tr) {
                dx = tr - 1 - x;
                dy = y - (height - tr);
                isCorner = 1;
            } else if (x >= width - tr && y >= height - tr) {
                dx = x - (width - tr);
                dy = y - (height - tr);
                isCorner = 1;
            }

            if (isCorner) {
                int dist2 = dx * dx + dy * dy;
                // Outer boundary radius = 32, inner boundary = 30
                if (dist2 <= 900) { // 30 * 30
                    pixels[idx].a = 0x80;
                    invPixels[idx].a = 0;
                } else if (dist2 >= 1024) { // 32 * 32
                    pixels[idx].a = 0;
                    invPixels[idx].a = 0x80;
                } else {
                    int diff = dist2 - 900;
                    u8 aVal = (u8)(0x80 - (diff * 0x80) / 124);
                    pixels[idx].a = aVal;
                    invPixels[idx].a = 0x80 - aVal;
                }
            } else {
                // Inside solid straight edges and center fill
                pixels[idx].a = 0x80;
                invPixels[idx].a = 0;
            }
        }
    }

    gPS5MasksInitialized = 1;
}

int thmGetPS5TitleFont(void)
{
    return gPS5TitleFont != -1 ? gPS5TitleFont : 0;
}

int thmGetPS5HeaderFont(void)
{
    return gPS5HeaderFont != -1 ? gPS5HeaderFont : 0;
}

int thmGetPS5SemiBoldFont(void)
{
    return gPS5SemiBoldFont != -1 ? gPS5SemiBoldFont : 0;
}

static void rmDrawRawQuad(GSTEXTURE *txt, int x1, int y1, int x2, int y2, int u1, int v1, int u2, int v2, u64 color)
{
    rm_quad_t q;
    q.ul.x = x1;
    q.ul.y = y1;
    q.br.x = x2;
    q.br.y = y2;
    q.color = color;
    q.txt = txt;
    q.ul.u = u1;
    q.ul.v = v1;
    q.br.u = u2;
    q.br.v = v2;
    rmDrawQuad(&q);
}

static void rmDraw9SliceRoundedRect(GSTEXTURE *txt, int x, int y, int w, int h, int r, u64 color)
{
    int R = r;
    int tw = txt->Width;
    int th = txt->Height;
    int tr = tw / 4; // Corner radius in texture coordinates (32)

    // 1. Scale key coordinate slices once to avoid floating point accumulated rounding gaps
    int X0 = rmScaleX(x);
    int X1 = rmScaleX(x + R);
    int X2 = rmScaleX(x + w - R);
    int X3 = rmScaleX(x + w);

    int Y0 = rmScaleY(y);
    int Y1 = rmScaleY(y + R);
    int Y2 = rmScaleY(y + h - R);
    int Y3 = rmScaleY(y + h);

    // 2. Render all 9 slices perfectly using adjacent shared-vertex boundaries
    // Top Row
    rmDrawRawQuad(txt, X0, Y0, X1, Y1, 0, 0, tr, tr, color);
    rmDrawRawQuad(txt, X1, Y0, X2, Y1, tr, 0, tw - tr, tr, color);
    rmDrawRawQuad(txt, X2, Y0, X3, Y1, tw - tr, 0, tw, tr, color);

    // Middle Row
    rmDrawRawQuad(txt, X0, Y1, X1, Y2, 0, tr, tr, th - tr, color);
    rmDrawRawQuad(txt, X1, Y1, X2, Y2, tr, tr, tw - tr, th - tr, color);
    rmDrawRawQuad(txt, X2, Y1, X3, Y2, tw - tr, tr, tw, th - tr, color);

    // Bottom Row
    rmDrawRawQuad(txt, X0, Y2, X1, Y3, 0, th - tr, tr, th, color);
    rmDrawRawQuad(txt, X1, Y2, X2, Y3, tr, th - tr, tw - tr, th, color);
    rmDrawRawQuad(txt, X2, Y2, X3, Y3, tw - tr, th - tr, tw, th, color);
}

static void rmDraw9SliceRoundedRectWide(GSTEXTURE *txt, int x, int y, int w, int h, int r, u64 color)
{
    int R = r;
    int tw = txt->Width;
    int th = txt->Height;
    int tr = tw / 4; // Corner radius in texture coordinates (32)

    // 1. Scale key coordinate slices once to avoid floating point accumulated rounding gaps
    int X0 = rmScaleX(rmWideScale(x));
    int X1 = rmScaleX(rmWideScale(x + R));
    int X2 = rmScaleX(rmWideScale(x + w - R));
    int X3 = rmScaleX(rmWideScale(x + w));

    int Y0 = rmScaleY(y);
    int Y1 = rmScaleY(y + R);
    int Y2 = rmScaleY(y + h - R);
    int Y3 = rmScaleY(y + h);

    // 2. Render all 9 slices perfectly using adjacent shared-vertex boundaries
    // Top Row
    rmDrawRawQuad(txt, X0, Y0, X1, Y1, 0, 0, tr, tr, color);
    rmDrawRawQuad(txt, X1, Y0, X2, Y1, tr, 0, tw - tr, tr, color);
    rmDrawRawQuad(txt, X2, Y0, X3, Y1, tw - tr, 0, tw, tr, color);

    // Middle Row
    rmDrawRawQuad(txt, X0, Y1, X1, Y2, 0, tr, tr, th - tr, color);
    rmDrawRawQuad(txt, X1, Y1, X2, Y2, tr, tr, tw - tr, th - tr, color);
    rmDrawRawQuad(txt, X2, Y1, X3, Y2, tw - tr, tr, tw, th - tr, color);

    // Bottom Row
    rmDrawRawQuad(txt, X0, Y2, X1, Y3, 0, th - tr, tr, th, color);
    rmDrawRawQuad(txt, X1, Y2, X2, Y3, tr, th - tr, tw - tr, th, color);
    rmDrawRawQuad(txt, X2, Y2, X3, Y3, tw - tr, th - tr, tw, th, color);
}

extern u8 gPS5BgColorR;
extern u8 gPS5BgColorG;
extern u8 gPS5BgColorB;
extern GSGLOBAL *gsGlobal;
extern float fRenderXOff;
extern float fRenderYOff;
extern int order;

void rmDrawRoundedRect(int x, int y, int w, int h, int r, u64 color)
{
    rmDraw9SliceRoundedRect(&gPS5MaskTex, x, y, w, h, r, color);
}

void rmDrawRoundedRectWide(int x, int y, int w, int h, int r, u64 color)
{
    rmDraw9SliceRoundedRectWide(&gPS5MaskTex, x, y, w, h, r, color);
}
void rmDrawRoundedCover(GSTEXTURE *cover, int x, int y, int w, int h, int r)
{
    cover->Filter = GS_FILTER_NEAREST;

    // Calculate aspect ratio of the cover image (defaulting to a beautiful 0.73 portrait DVD case)
    float imgAspect = 0.73f;
    if (cover->Width > 0 && cover->Height > 0) {
        float nativeAspect = (float)cover->Width / (float)cover->Height;
        if (nativeAspect < 1.0f) {
            imgAspect = nativeAspect;
        }
    }

    // Coordinates in the 640x480 space
    float x_draw = (float)x;
    float y_draw = (float)y;
    float w_draw = (float)w;
    float h_draw = (float)h;

    // Adjust width or height to preserve aspect ratio (assuming 1:1 screen mapping of units)
    if (imgAspect < 1.0f) { // Portrait image
        w_draw = (float)h * imgAspect;
        x_draw = (float)x + ((float)w - w_draw) / 2.0f;
    } else if (imgAspect > 1.0f) { // Landscape image
        h_draw = (float)w / imgAspect;
        y_draw = (float)y + ((float)h - h_draw) / 2.0f;
    }

    // Now scale the adjusted coordinates
    int X0 = rmScaleX(rmWideScale((int)x_draw));
    int X3 = rmScaleX(rmWideScale((int)(x_draw + w_draw)));
    int Y0 = rmScaleY((int)y_draw);
    int Y3 = rmScaleY((int)(y_draw + h_draw));

    if ((cover->PSM == GS_PSM_CT32) || (cover->Clut && cover->ClutPSM == GS_PSM_CT32)) {
        gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
        gsKit_set_test(gsGlobal, GS_ATEST_ON);
    } else {
        gsGlobal->PrimAlphaEnable = GS_SETTING_OFF;
        gsKit_set_test(gsGlobal, GS_ATEST_OFF);
    }

    gsKit_TexManager_bind(gsGlobal, cover);
    gsKit_prim_sprite_texture(gsGlobal, cover,
                              X0 + fRenderXOff, Y0 + fRenderYOff,
                              0, 0,
                              X3 + fRenderXOff, Y3 + fRenderYOff,
                              cover->Width, cover->Height, order, gDefaultCol);
    order++;

    // Overlay only the 4 inverse corners using gPS5InvMaskTex
    int R = r;
    int tw = gPS5InvMaskTex.Width;
    int th = gPS5InvMaskTex.Height;
    int tr = tw / 4;

    int X1 = rmScaleX(rmWideScale((int)(x_draw + R)));
    int X2 = rmScaleX(rmWideScale((int)(x_draw + w_draw - R)));

    int Y1 = rmScaleY((int)(y_draw + R));
    int Y2 = rmScaleY((int)(y_draw + h_draw - R));

    u64 colorTL = GS_SETREG_RGBA(0x00, 0x00, 0x00, 0x80);
    u64 colorTR = colorTL;
    u64 colorBL = colorTL;
    u64 colorBR = colorTL;

    rmDrawRawQuad(&gPS5InvMaskTex, X0, Y0, X1, Y1, 0, 0, tr, tr, colorTL);                     // Top-Left
    rmDrawRawQuad(&gPS5InvMaskTex, X2, Y0, X3, Y1, tw - tr, 0, tw, tr, colorTR);         // Top-Right
    rmDrawRawQuad(&gPS5InvMaskTex, X0, Y2, X1, Y3, 0, th - tr, tr, th, colorBL);         // Bottom-Left
    rmDrawRawQuad(&gPS5InvMaskTex, X2, Y2, X3, Y3, tw - tr, th - tr, tw, th, colorBR); // Bottom-Right
}

static void rmDrawRoundedSquareThumbnail(GSTEXTURE *texture, int x, int y, int size, int r, u64 color)
{
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1;
    float v1;
    int X0;
    int X1;
    int X2;
    int X3;
    int Y0;
    int Y1;
    int Y2;
    int Y3;
    int tw;
    int th;
    int tr;
    u64 maskColor = GS_SETREG_RGBA(0x00, 0x00, 0x00, 0x80);

    if (!texture || !texture->Mem || texture->Width <= 0 || texture->Height <= 0 || size <= 0)
        return;

    u1 = texture->Width;
    v1 = texture->Height;
    if (texture->Width > texture->Height) {
        float crop = texture->Height;
        u0 = (texture->Width - crop) * 0.5f;
        u1 = u0 + crop;
    } else if (texture->Height > texture->Width) {
        float crop = texture->Width;
        v0 = (texture->Height - crop) * 0.5f;
        v1 = v0 + crop;
    }

    if (r > size / 2)
        r = size / 2;

    rmDrawRoundedRect(x, y, size, size, r, GS_SETREG_RGBA(0x08, 0x0D, 0x14, 0x80));

    X0 = rmScaleX(x);
    X1 = rmScaleX(x + r);
    X2 = rmScaleX(x + size - r);
    X3 = rmScaleX(x + size);
    Y0 = rmScaleY(y);
    Y1 = rmScaleY(y + r);
    Y2 = rmScaleY(y + size - r);
    Y3 = rmScaleY(y + size);

    gsKit_TexManager_bind(gsGlobal, texture);
    gsKit_prim_sprite_texture(gsGlobal, texture,
                              X0 + fRenderXOff, Y0 + fRenderYOff,
                              u0, v0,
                              X3 + fRenderXOff, Y3 + fRenderYOff,
                              u1, v1, order, color);
    order++;

    tw = gPS5InvMaskTex.Width;
    th = gPS5InvMaskTex.Height;
    tr = tw / 4;

    rmDrawRawQuad(&gPS5InvMaskTex, X0, Y0, X1, Y1, 0, 0, tr, tr, maskColor);
    rmDrawRawQuad(&gPS5InvMaskTex, X2, Y0, X3, Y1, tw - tr, 0, tw, tr, maskColor);
    rmDrawRawQuad(&gPS5InvMaskTex, X0, Y2, X1, Y3, 0, th - tr, tr, th, maskColor);
    rmDrawRawQuad(&gPS5InvMaskTex, X2, Y2, X3, Y3, tw - tr, th - tr, tw, th, maskColor);
}

static void rmDrawRoundedSquareThumbnailWide(GSTEXTURE *texture, int x, int y, int w, int h, int r)
{
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1;
    float v1;
    int X0;
    int X1;
    int X2;
    int X3;
    int Y0;
    int Y1;
    int Y2;
    int Y3;
    int tw;
    int th;
    int tr;
    u64 maskColor = GS_SETREG_RGBA(0x00, 0x00, 0x00, 0x80);

    if (!texture || !texture->Mem || texture->Width <= 0 || texture->Height <= 0 || w <= 0 || h <= 0)
        return;

    u1 = texture->Width;
    v1 = texture->Height;
    if (texture->Width > texture->Height) {
        float crop = texture->Height;
        u0 = (texture->Width - crop) * 0.5f;
        u1 = u0 + crop;
    } else if (texture->Height > texture->Width) {
        float crop = texture->Width;
        v0 = (texture->Height - crop) * 0.5f;
        v1 = v0 + crop;
    }

    if (r > h / 2)
        r = h / 2;

    X0 = rmScaleX(rmWideScale(x));
    X1 = rmScaleX(rmWideScale(x + r));
    X2 = rmScaleX(rmWideScale(x + w - r));
    X3 = rmScaleX(rmWideScale(x + w));
    Y0 = rmScaleY(y);
    Y1 = rmScaleY(y + r);
    Y2 = rmScaleY(y + h - r);
    Y3 = rmScaleY(y + h);

    gsKit_TexManager_bind(gsGlobal, texture);
    gsKit_prim_sprite_texture(gsGlobal, texture,
                              X0 + fRenderXOff, Y0 + fRenderYOff,
                              u0, v0,
                              X3 + fRenderXOff, Y3 + fRenderYOff,
                              u1, v1, order, gDefaultCol);
    order++;

    tw = gPS5InvMaskTex.Width;
    th = gPS5InvMaskTex.Height;
    tr = tw / 4;

    rmDrawRawQuad(&gPS5InvMaskTex, X0, Y0, X1, Y1, 0, 0, tr, tr, maskColor);
    rmDrawRawQuad(&gPS5InvMaskTex, X2, Y0, X3, Y1, tw - tr, 0, tw, tr, maskColor);
    rmDrawRawQuad(&gPS5InvMaskTex, X0, Y2, X1, Y3, 0, th - tr, tr, th, maskColor);
    rmDrawRawQuad(&gPS5InvMaskTex, X2, Y2, X3, Y3, tw - tr, th - tr, tw, th, maskColor);
}

static int ps5CarouselSquareWidthForHeight(int height)
{
    int aspectWidth = rmGetAspectWidth();
    extern int gVMode;

    if (aspectWidth <= 0)
        return height;

    if (gVMode == 10 || gVMode == 11)
        return (height * 78 + 50) / 100;

    if (gVMode == 0)
        return (height * 108 + 50) / 100;

    return (height * 4 + (aspectWidth / 2)) / aspectWidth;
}

int gPS5SettingsPage = 0; // 0 = Main Settings list, 1 = Display Settings sub-menu
int gPS5SettingsSel = 0;

#define PS5_SMB_SETTINGS_COUNT 11

int gPS5TempVMode = 0;
int gPS5SubSel = 7;
unsigned int gPS5SaveNotifyFrame = 0; // Frame timing for toast popup
unsigned int gPS5SaveBusyFrame = 0;
unsigned int gPS5RefreshBusyFrame = 0;

typedef struct {
    char gameTitle[64];
    char cleanName[64]; char startup[32];
    int state; // 0 = idle, 1 = downloading, 2 = done, 3 = failed
    u8 cardR, cardG, cardB;
    u8 bgR, bgG, bgB;
    int hasColor;
    void *threadStack;
    char coverPath[256];
    GSTEXTURE coverTex;
    int hasTex; // 0 = not loaded, 1 = loaded, -1 = load failed
    char logoPath[256];
    GSTEXTURE logoTex;
    int hasLogoTex; // 0 = not loaded, 1 = loaded, -1 = load failed
    char devicePrefix[32];
    unsigned int lastCoverFrame;
    unsigned int lastLogoFrame;
} net_req_t;

#define NET_CACHE_INITIAL_CAPACITY 64
#define PS5_TEXTURE_KEEP_FRAMES 18000
#define PS5_MAX_TEXTURE_LOADS_PER_FRAME 2
#define PS5_LOGO_LOAD_DELAY_FRAMES 36
#define PS5_TEX_LOADING -2
#define PS5_TEX_LOAD_TIMEOUT_FRAMES 900
static net_req_t *gNetCache = NULL;
static int gNetCacheCount = 0;
static int gNetCacheCapacity = 0;
static unsigned int gPS5TextureFrame = 0;
static int gPS5TextureLoadsThisFrame = 0;
static unsigned int gPS5CacheGeneration = 1;
int gPS5CarouselNavInterrupt = 0;
static item_list_t *gPS5ArtworkResolveList = NULL;
static int gPS5ArtworkResolveSourceId = -1;
static int gPS5ArtworkResolveRunning = 0;
static volatile int gPS5ArtworkResolveDirty = 0;
static volatile int gPS5AsyncTexRunning = 0;
static volatile int gPS5AsyncTexDone = 0;
static int gPS5AsyncTexCacheIndex = -1;
static int gPS5AsyncTexIsLogo = 0;
static unsigned int gPS5AsyncTexGeneration = 0;
static unsigned int gPS5AsyncTexStartFrame = 0;
static int gPS5AsyncTexResult = -1;
static char gPS5AsyncTexPath[256];
static GSTEXTURE gPS5AsyncTex;
static u8 gPS5AsyncTexStack[64 * 1024] ALIGNED(16);
static s32 gPS5AsyncTexThreadId = -1;
static char gNetDebugMsg[256] = "Net Status: System ready.";

extern void *_gp;
extern void *focus_png;

static void clearNetCache(void);
static void findBuiltInCoverForGame(const char *gameTitle, char *matchedPath, int maxLen);

static GSTEXTURE gPS5FocusPointerTex;
static int gPS5FocusPointerLoaded = 0;

static GSTEXTURE *getPS5FocusPointerTexture(void)
{
    GSTEXTURE *themeFocus = thmGetTexture(FOCUS_ICON);
    if (themeFocus && themeFocus->Mem)
        return themeFocus;

    if (!gPS5FocusPointerLoaded) {
        memset(&gPS5FocusPointerTex, 0, sizeof(GSTEXTURE));
        if (texLoadMem(&gPS5FocusPointerTex, &focus_png) >= 0)
            gPS5FocusPointerLoaded = 1;
        else
            gPS5FocusPointerLoaded = -1;
    }

    return gPS5FocusPointerLoaded == 1 ? &gPS5FocusPointerTex : NULL;
}

void drawPS5FocusPointer(int x, int y)
{
    GSTEXTURE *focusTex = getPS5FocusPointerTexture();
    if (focusTex && focusTex->Mem)
        rmDrawPixmap(focusTex, x, y, ALIGN_LEFT | ALIGN_VCENTER, 16, 16, 1, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
}

static int loadPS5CoverTexture(GSTEXTURE *texture, const char *path)
{
    char pathNoExt[256];
    char *pDot;
    int i;

    if (!strncmp(path, "embedded:", 9)) {
        const char *coverName = path + 9;
        for (i = 0; i < gPS5CoverAssetCount; i++) {
            if (!strcmp(gPS5CoverAssets[i].name, coverName))
                return texLoadMem(texture, gPS5CoverAssets[i].png);
        }
        return -1;
    }

    strncpy(pathNoExt, path, sizeof(pathNoExt) - 1);
    pathNoExt[sizeof(pathNoExt) - 1] = '\0';

    pDot = strrchr(pathNoExt, '.');
    if (pDot)
        *pDot = '\0';

    return texDiscoverLoad(texture, pathNoExt, -1);
}

static void unloadPS5CoverTexture(net_req_t *entry)
{
    if (entry && entry->hasTex == 1) {
        rmUnloadTexture(&entry->coverTex);
        texFree(&entry->coverTex);
        entry->hasTex = 0;
    }
}

static void unloadPS5LogoTexture(net_req_t *entry)
{
    if (entry && entry->hasLogoTex == 1) {
        rmUnloadTexture(&entry->logoTex);
        texFree(&entry->logoTex);
        entry->hasLogoTex = 0;
    }
}

static void ps5FreeLoadedTexture(GSTEXTURE *texture)
{
    if (texture != NULL && texture->Mem != NULL) {
        rmUnloadTexture(texture);
        texFree(texture);
        memset(texture, 0, sizeof(GSTEXTURE));
    }
}

static void ps5LoadTextureTask(void)
{
    memset(&gPS5AsyncTex, 0, sizeof(gPS5AsyncTex));
    gPS5AsyncTexResult = loadPS5CoverTexture(&gPS5AsyncTex, gPS5AsyncTexPath);
    gPS5AsyncTexDone = 1;
    gPS5AsyncTexRunning = 0;
}

static void ps5LoadTextureThread(void *arg)
{
    ps5LoadTextureTask();
    ExitDeleteThread();
}

static void ps5CompleteAsyncTextureLoad(void)
{
    net_req_t *entry;
    int stale;

    if (!gPS5AsyncTexDone)
        return;

    stale = gPS5AsyncTexGeneration != gPS5CacheGeneration ||
            gPS5AsyncTexCacheIndex < 0 ||
            gPS5AsyncTexCacheIndex >= gNetCacheCount;

    if (!stale) {
        entry = &gNetCache[gPS5AsyncTexCacheIndex];
        if (gPS5AsyncTexIsLogo) {
            if (gPS5AsyncTexResult >= 0) {
                entry->logoTex = gPS5AsyncTex;
                entry->hasLogoTex = 1;
            } else {
                entry->hasLogoTex = -1;
            }
        } else {
            if (gPS5AsyncTexResult >= 0) {
                entry->coverTex = gPS5AsyncTex;
                entry->hasTex = 1;
            } else {
                char builtInPath[256];
                findBuiltInCoverForGame(entry->gameTitle, builtInPath, sizeof(builtInPath));
                if (builtInPath[0] != '\0' && strncmp(entry->coverPath, "embedded:", 9) != 0) {
                    strncpy(entry->coverPath, builtInPath, sizeof(entry->coverPath) - 1);
                    entry->coverPath[sizeof(entry->coverPath) - 1] = '\0';
                    entry->hasTex = 0;
                } else {
                    entry->hasTex = -1;
                }
            }
        }
    } else if (gPS5AsyncTexResult >= 0) {
        ps5FreeLoadedTexture(&gPS5AsyncTex);
    }

    memset(&gPS5AsyncTex, 0, sizeof(gPS5AsyncTex));
    gPS5AsyncTexPath[0] = '\0';
    gPS5AsyncTexCacheIndex = -1;
    gPS5AsyncTexDone = 0;
    gPS5AsyncTexRunning = 0;
    gPS5AsyncTexThreadId = -1;
}

static int ps5QueueTextureLoad(net_req_t *entry, int isLogo)
{
    int idx;
    int *state;
    const char *path;

    if (entry == NULL || gPS5AsyncTexRunning || gPS5AsyncTexDone)
        return 0;

    idx = entry - gNetCache;
    if (idx < 0 || idx >= gNetCacheCount)
        return 0;

    state = isLogo ? &entry->hasLogoTex : &entry->hasTex;
    path = isLogo ? entry->logoPath : entry->coverPath;
    if (*state != 0 || path == NULL || path[0] == '\0')
        return 0;

    strncpy(gPS5AsyncTexPath, path, sizeof(gPS5AsyncTexPath) - 1);
    gPS5AsyncTexPath[sizeof(gPS5AsyncTexPath) - 1] = '\0';
    gPS5AsyncTexCacheIndex = idx;
    gPS5AsyncTexIsLogo = isLogo;
    gPS5AsyncTexGeneration = gPS5CacheGeneration;
    gPS5AsyncTexStartFrame = gPS5TextureFrame;
    gPS5AsyncTexResult = -1;
    gPS5AsyncTexDone = 0;
    gPS5AsyncTexRunning = 1;

    {
        ee_thread_t thread;
        memset(&thread, 0, sizeof(thread));
        thread.attr = 0;
        thread.stack_size = sizeof(gPS5AsyncTexStack);
        thread.gp_reg = &_gp;
        thread.func = &ps5LoadTextureThread;
        thread.stack = gPS5AsyncTexStack;
        thread.initial_priority = 31;
        gPS5AsyncTexThreadId = CreateThread(&thread);
    }

    if (gPS5AsyncTexThreadId < 0 || StartThread(gPS5AsyncTexThreadId, NULL) < 0) {
        gPS5AsyncTexRunning = 0;
        gPS5AsyncTexCacheIndex = -1;
        *state = 0;
        gPS5AsyncTexThreadId = -1;
        gPS5AsyncTexPath[0] = '\0';
        return 0;
    }

    *state = PS5_TEX_LOADING;
    return 1;
}

static void ps5RecoverTimedOutTextureLoad(void)
{
    net_req_t *entry;
    int *state;

    if (!gPS5AsyncTexRunning || gPS5AsyncTexDone)
        return;

    if (gPS5TextureFrame - gPS5AsyncTexStartFrame < PS5_TEX_LOAD_TIMEOUT_FRAMES)
        return;

    if (gPS5AsyncTexGeneration == gPS5CacheGeneration &&
        gPS5AsyncTexCacheIndex >= 0 &&
        gPS5AsyncTexCacheIndex < gNetCacheCount) {
        entry = &gNetCache[gPS5AsyncTexCacheIndex];
        state = gPS5AsyncTexIsLogo ? &entry->hasLogoTex : &entry->hasTex;
        if (*state == PS5_TEX_LOADING)
            *state = 0;
    }

    gPS5AsyncTexStartFrame = gPS5TextureFrame;
}

static int ensureNetCacheCapacity(void)
{
    net_req_t *newCache;
    int newCapacity;

    if (gNetCacheCount < gNetCacheCapacity)
        return 1;

    newCapacity = gNetCacheCapacity ? gNetCacheCapacity * 2 : NET_CACHE_INITIAL_CAPACITY;
    newCache = (net_req_t *)realloc(gNetCache, sizeof(net_req_t) * newCapacity);
    if (!newCache)
        return 0;

    gNetCache = newCache;
    memset(&gNetCache[gNetCacheCapacity], 0, sizeof(net_req_t) * (newCapacity - gNetCacheCapacity));
    gNetCacheCapacity = newCapacity;
    return 1;
}

static int ps5CacheTextMatches(const char *cached, const char *value)
{
    return value == NULL || value[0] == '\0' || strcmp(cached, value) == 0;
}

static net_req_t *findNetCacheEntryForGame(const char *title, const char *startup, const char *devicePrefix)
{
    int i;

    if (!title)
        return NULL;

    for (i = 0; i < gNetCacheCount; i++) {
        if (strcmp(gNetCache[i].gameTitle, title) == 0 &&
            ps5CacheTextMatches(gNetCache[i].startup, startup) &&
            ps5CacheTextMatches(gNetCache[i].devicePrefix, devicePrefix))
            return &gNetCache[i];
    }

    return NULL;
}

static net_req_t *findNetCacheEntry(const char *title)
{
    return findNetCacheEntryForGame(title, NULL, NULL);
}

static void sweepPS5TextureCache(void)
{
    int i;

    for (i = 0; i < gNetCacheCount; i++) {
        if (gNetCache[i].hasTex == 1 &&
            gNetCache[i].lastCoverFrame + PS5_TEXTURE_KEEP_FRAMES < gPS5TextureFrame) {
            unloadPS5CoverTexture(&gNetCache[i]);
        }
        if (gNetCache[i].hasLogoTex == 1 &&
            gNetCache[i].lastLogoFrame + PS5_TEXTURE_KEEP_FRAMES < gPS5TextureFrame) {
            unloadPS5LogoTexture(&gNetCache[i]);
        }
    }
}

static void getCleanGameName(const char *src, char *dst, int max_len) {
    int i = 0, j = 0;
    while (src[i] != '\0' && j < max_len - 1) {
        char c = src[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') {
            dst[j++] = c;
        } else if (c == ' ') {
            dst[j++] = '_';
        }
        i++;
    }
    dst[j] = '\0';
}

static void joinPath(char *dst, size_t maxLen, const char *dir, const char *file) {
    char sep = '/';
    if (strncasecmp(dir, "host", 4) == 0) {
        sep = '\\';
    }
    snprintf(dst, maxLen, "%s%c%s", dir, sep, file);
}

static void getGameColors(const char *title, u8 *cardR, u8 *cardG, u8 *cardB, u8 *bgR, u8 *bgG, u8 *bgB);

static const char *stopwords[] = {
    "the", "and", "for", "with", "you", "are", "this", "that", "from", "der", "die", "das", "und", "ein", "eine", "of", "in", "on", "at", "by", "an", "to", "is", "a", "or", "as"
};

static int isStopword(const char *word) {
    int i;
    for (i = 0; i < sizeof(stopwords) / sizeof(stopwords[0]); i++) {
        if (strcmp(word, stopwords[i]) == 0) return 1;
    }
    return 0;
}

static int getTitleKeywords(const char *title, char keywords[16][32]) {
    int count = 0;
    char temp[128];
    int i;
    for (i = 0; title[i] && i < 127; i++) {
        char c = title[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            temp[i] = (c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c;
        } else {
            temp[i] = ' ';
        }
    }
    temp[i] = '\0';

    char *tok = strtok(temp, " ");
    while (tok && count < 16) {
        if (strlen(tok) >= 2 && !isStopword(tok)) {
            strncpy(keywords[count], tok, 31);
            keywords[count][31] = '\0';
            count++;
        }
        tok = strtok(NULL, " ");
    }
    return count;
}

static int countKeywordMatches(const char *fileNameLower, char keywords[16][32], int keywordCount) {
    int matches = 0;
    int i;
    for (i = 0; i < keywordCount; i++) {
        if (strstr(fileNameLower, keywords[i]) != NULL) {
            matches++;
        }
    }
    return matches;
}

static int hasKeywordRun(const char *fileNameLower, char keywords[16][32], int keywordCount, int runLength)
{
    int i;
    char pattern[128];

    if (keywordCount < runLength)
        return 0;

    for (i = 0; i <= keywordCount - runLength; i++) {
        int j;
        pattern[0] = '\0';
        for (j = 0; j < runLength; j++) {
            if (j > 0)
                strncat(pattern, "_", sizeof(pattern) - strlen(pattern) - 1);
            strncat(pattern, keywords[i + j], sizeof(pattern) - strlen(pattern) - 1);
        }
        if (strstr(fileNameLower, pattern) != NULL)
            return 1;
    }

    return 0;
}

static void normalizeAlphaNumLower(const char *src, char *dst, int maxLen)
{
    int i, j = 0;
    for (i = 0; src[i] && j < maxLen - 1; i++) {
        char c = src[i];
        if (c >= 'A' && c <= 'Z')
            c = c - 'A' + 'a';
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            dst[j++] = c;
    }
    dst[j] = '\0';
}

static int scoreCoverName(const char *coverName, char keywords[16][32], int keywordCount, const char *exactTitle)
{
    char coverAlpha[128];
    int matches;

    normalizeAlphaNumLower(coverName, coverAlpha, sizeof(coverAlpha));
    if (!strcmp(coverAlpha, exactTitle))
        return 1000;

    matches = countKeywordMatches(coverName, keywords, keywordCount);
    if (hasKeywordRun(coverName, keywords, keywordCount, 3) || matches >= 3)
        return 300 + matches;
    if (hasKeywordRun(coverName, keywords, keywordCount, 2) || matches >= 2)
        return 200 + matches;

    return 0;
}

static void findBuiltInCoverForGame(const char *gameTitle, char *matchedPath, int maxLen)
{
    char keywords[16][32];
    char exactTitle[128];
    int keywordCount = getTitleKeywords(gameTitle, keywords);
    int i;
    int bestScore = 0;
    int bestIdx = -1;

    normalizeAlphaNumLower(gameTitle, exactTitle, sizeof(exactTitle));

    for (i = 0; i < gPS5CoverAssetCount; i++) {
        int score = scoreCoverName(gPS5CoverAssets[i].name, keywords, keywordCount, exactTitle);
        if (score > bestScore) {
            bestScore = score;
            bestIdx = i;
        }
    }

    if (bestIdx >= 0)
        snprintf(matchedPath, maxLen, "embedded:%s", gPS5CoverAssets[bestIdx].name);
    else
        matchedPath[0] = '\0';
}

extern volatile unsigned int gBdmEventGeneration;

int debugOpenProbe(const char *path)
{
    extern int gNetworkStartup;

    if (path != NULL) {
        if (bdmIsUsbPathDisconnected(path))
            return -1;
        if (strncmp(path, "smb", 3) == 0 && gNetworkStartup != 0)
            return -1;
    }

    return open(path, O_RDONLY);
}

static void triggerNetFetch(const char *title, const char *startup, const char *devicePrefix, int allowDeviceProbe) {
    extern int gPS5ShowGamesLogo;
    char matchedPath[256];
    u8 cardR = 100, cardG = 100, cardB = 100;
    u8 bgR = 24, bgG = 24, bgB = 24;
    net_req_t *existing = findNetCacheEntryForGame(title, startup, devicePrefix);

    if (existing != NULL && existing->hasTex != 0 &&
        (!gPS5ShowGamesLogo || existing->logoPath[0] != '\0' || existing->hasLogoTex == -1))
        return;

    if (!ensureNetCacheCapacity())
        return;

    int idx;
    if (existing != NULL) {
        idx = existing - gNetCache;
        unloadPS5CoverTexture(existing);
        unloadPS5LogoTexture(existing);
        existing->coverPath[0] = '\0';
        existing->logoPath[0] = '\0';
        existing->hasTex = 0;
        existing->hasLogoTex = 0;
    } else {
        idx = gNetCacheCount++;
        memset(&gNetCache[idx], 0, sizeof(gNetCache[idx]));
    }
    strncpy(gNetCache[idx].gameTitle, title, 63);
    gNetCache[idx].gameTitle[63] = '\0';
    getCleanGameName(title, gNetCache[idx].cleanName, 64); if (startup) { strncpy(gNetCache[idx].startup, startup, sizeof(gNetCache[idx].startup) - 1); gNetCache[idx].startup[sizeof(gNetCache[idx].startup) - 1] = '\0'; } else { gNetCache[idx].startup[0] = '\0'; }
    if (devicePrefix) {
        strncpy(gNetCache[idx].devicePrefix, devicePrefix, sizeof(gNetCache[idx].devicePrefix) - 1);
        gNetCache[idx].devicePrefix[sizeof(gNetCache[idx].devicePrefix) - 1] = '\0';
    } else {
        gNetCache[idx].devicePrefix[0] = '\0';
    }
    gNetCache[idx].state = 1;
    gNetCache[idx].hasColor = 0;

    matchedPath[0] = '\0';
    if (allowDeviceProbe && startup && startup[0] != '\0') {
        char prefix[128];
        if (devicePrefix && devicePrefix[0] != '\0') {
            strncpy(prefix, devicePrefix, sizeof(prefix) - 1);
            prefix[sizeof(prefix) - 1] = '\0';
            int len = strlen(prefix);
            if (len > 0 && prefix[len - 1] == '/')
                prefix[len - 1] = '\0';
        } else {
            strcpy(prefix, "mass0:");
        }
        snprintf(matchedPath, sizeof(matchedPath), "%s/ART/%s_COV.png", prefix, startup);
    }
    if (matchedPath[0] == '\0') {
        findBuiltInCoverForGame(title, matchedPath, sizeof(matchedPath));
    }

    char logoPath[256] = {0};
    if (allowDeviceProbe && startup && startup[0] != '\0') {
        char prefix[128];
        if (devicePrefix && devicePrefix[0] != '\0') {
            strncpy(prefix, devicePrefix, sizeof(prefix) - 1);
            prefix[sizeof(prefix) - 1] = '\0';
            int len = strlen(prefix);
            if (len > 0 && prefix[len - 1] == '/')
                prefix[len - 1] = '\0';
        } else {
            strcpy(prefix, "mass0:");
        }
        snprintf(logoPath, sizeof(logoPath), "%s/LOGO/%s_LOGO.png", prefix, startup);
    }
    strncpy(gNetCache[idx].logoPath, logoPath, sizeof(gNetCache[idx].logoPath) - 1);
    gNetCache[idx].logoPath[sizeof(gNetCache[idx].logoPath) - 1] = '\0';
    if (gNetCache[idx].logoPath[0] == '\0') {
        gNetCache[idx].hasLogoTex = -1;
    } else {
        gNetCache[idx].hasLogoTex = 0;
    }

    getGameColors(title, &cardR, &cardG, &cardB, &bgR, &bgG, &bgB);
    gNetCache[idx].state = 2;
    gNetCache[idx].cardR = cardR;
    gNetCache[idx].cardG = cardG;
    gNetCache[idx].cardB = cardB;
    gNetCache[idx].bgR = bgR;
    gNetCache[idx].bgG = bgG;
    gNetCache[idx].bgB = bgB;
    gNetCache[idx].hasColor = 1;
    if (matchedPath[0] != '\0') {
        strncpy(gNetCache[idx].coverPath, matchedPath, sizeof(gNetCache[idx].coverPath) - 1);
        gNetCache[idx].coverPath[sizeof(gNetCache[idx].coverPath) - 1] = '\0';
        gNetCache[idx].hasTex = 0;
    } else {
        gNetCache[idx].coverPath[0] = '\0';
        gNetCache[idx].hasTex = -1;
    }
}

static int ps5StartupLooksResolved(const char *startup)
{
    return startup != NULL && strlen(startup) == GAME_STARTUP_MAX - 1 && startup[4] == '_' && startup[8] == '.';
}

static void ps5ResolveArtworkStartupTask(void)
{
    item_list_t *list = gPS5ArtworkResolveList;
    int sourceId = gPS5ArtworkResolveSourceId;
    base_game_info_t *game;
    char *prefix;

    if (list != NULL && sourceId >= 0 && list->mode == ETH_MODE && list->itemGet != NULL) {
        game = (base_game_info_t *)list->itemGet(list, sourceId);
        prefix = list->itemGetPrefix != NULL ? list->itemGetPrefix(list) : NULL;
        if (game != NULL && game->format == GAME_FORMAT_ISO && prefix != NULL && prefix[0] != '\0') {
            if (sbResolveISOStartup(game, prefix, "\\") == 0)
                gPS5ArtworkResolveDirty = 1;
        }
    }

    gPS5ArtworkResolveList = NULL;
    gPS5ArtworkResolveSourceId = -1;
    gPS5ArtworkResolveRunning = 0;
}

static void ps5QueueArtworkStartupResolve(item_list_t *list, int sourceId, const char *startup)
{
    if (gPS5ArtworkResolveRunning || gPS5CarouselNavInterrupt > 0)
        return;

    if (list == NULL || sourceId < 0 || list->mode != ETH_MODE || ps5StartupLooksResolved(startup))
        return;

    gPS5ArtworkResolveList = list;
    gPS5ArtworkResolveSourceId = sourceId;
    gPS5ArtworkResolveRunning = 1;
    if (ioPutRequest(IO_CUSTOM_SIMPLEACTION, &ps5ResolveArtworkStartupTask) < 0)
        gPS5ArtworkResolveRunning = 0;
}

static net_req_t *preparePS5CarouselCardMedia(struct menu_list *menu, submenu_list_t *currItem, const char *title, int isUnplugged, int allowDeviceProbe)
{
    extern int gPS5ShowCoverImages;
    extern int gPS5ShowGamesLogo;
    net_req_t *cacheEntry;
    char *prefix = "";
    const char *startup = NULL;
    int allowCardDeviceProbe = allowDeviceProbe;
    int sourceId;
    item_list_t *list = NULL;

    if (title == NULL || currItem == NULL || menu == NULL || menu->item == NULL || !(gPS5ShowCoverImages || gPS5ShowGamesLogo))
        return NULL;

    list = resolveThemeGameItem(menu->item->userdata, currItem->item.id, &sourceId);
    if (list != NULL) {
        int isBdmMode = list->mode >= BDM_MODE && list->mode < ETH_MODE;
        if (list->itemGetPrefix)
            prefix = list->itemGetPrefix(list);
        if (isBdmMode && (prefix == NULL || prefix[0] == '\0' || bdmIsUsbPathDisconnected(prefix)))
            allowCardDeviceProbe = 0;
        if (list->itemGetStartup)
            startup = list->itemGetStartup(list, sourceId);
    }

    cacheEntry = findNetCacheEntryForGame(title, startup, prefix);
    if (cacheEntry != NULL) {
        if (cacheEntry->state == 2 && (cacheEntry->coverPath[0] == '\0' || (gPS5ShowGamesLogo && cacheEntry->logoPath[0] == '\0')))
            ps5QueueArtworkStartupResolve(list, sourceId, startup);
        if (cacheEntry->state == 2 && cacheEntry->coverPath[0] != '\0') {
            cacheEntry->lastCoverFrame = gPS5TextureFrame;
            if (cacheEntry->hasTex == 0 && !isUnplugged && gPS5CarouselNavInterrupt <= 0 && gPS5TextureLoadsThisFrame < PS5_MAX_TEXTURE_LOADS_PER_FRAME) {
                gPS5TextureLoadsThisFrame++;
                ps5QueueTextureLoad(cacheEntry, 0);
            }
        }
        return cacheEntry;
    }

    if (!isUnplugged && gPS5CarouselNavInterrupt <= 0 && gPS5TextureLoadsThisFrame < PS5_MAX_TEXTURE_LOADS_PER_FRAME) {
        ps5QueueArtworkStartupResolve(list, sourceId, startup);
        gPS5TextureLoadsThisFrame++;
        triggerNetFetch(title, startup, prefix, allowCardDeviceProbe);
        cacheEntry = findNetCacheEntryForGame(title, startup, prefix);
    }

    return cacheEntry;
}

static void getGameColors(const char *title, u8 *cardR, u8 *cardG, u8 *cardB, u8 *bgR, u8 *bgG, u8 *bgB)
{
    int cacheIdx;
    for (cacheIdx = 0; cacheIdx < gNetCacheCount; cacheIdx++) {
        if (strcmp(gNetCache[cacheIdx].gameTitle, title) == 0) {
            if (gNetCache[cacheIdx].hasColor) {
                *cardR = gNetCache[cacheIdx].cardR;
                *cardG = gNetCache[cacheIdx].cardG;
                *cardB = gNetCache[cacheIdx].cardB;
                *bgR = gNetCache[cacheIdx].bgR;
                *bgG = gNetCache[cacheIdx].bgG;
                *bgB = gNetCache[cacheIdx].bgB;
                return;
            }
            break;
        }
    }
    if (strstr(title, "God of War")) {
        *cardR = 180; *cardG = 20;  *cardB = 30; // God of War Crimson
        *bgR = 48;   *bgG = 5;   *bgB = 8;
    } else if (strstr(title, "Grand Theft Auto") || strstr(title, "GTA")) {
        *cardR = 210; *cardG = 120; *cardB = 10; // San Andreas Orange
        *bgR = 56;   *bgG = 28;  *bgB = 3;
    } else if (strstr(title, "Gran Turismo")) {
        *cardR = 30;  *cardG = 100; *cardB = 210; // Racing Blue
        *bgR = 8;    *bgG = 24;  *bgB = 52;
    } else if (strstr(title, "Metal Gear")) {
        *cardR = 40;  *cardG = 110; *cardB = 50; // Jungle Green
        *bgR = 10;   *bgG = 28;  *bgB = 12;
    } else if (strstr(title, "Devil May Cry")) {
        *cardR = 90;  *cardG = 30;  *cardB = 180; // DMC Violet/Indigo
        *bgR = 24;   *bgG = 8;   *bgB = 48;
    } else if (strstr(title, "Final Fantasy")) {
        *cardR = 20;  *cardG = 150; *cardB = 160; // FF Cyan/Aqua
        *bgR = 5;    *bgG = 36;  *bgB = 40;
    } else if (strstr(title, "Shadow of the Colossus")) {
        *cardR = 120; *cardG = 120; *cardB = 125; // Shadow Stone Grey
        *bgR = 32;   *bgG = 32;  *bgB = 34;
    } else if (strstr(title, "Resident Evil")) {
        *cardR = 130; *cardG = 20;  *cardB = 50; // RE Blood Burgundy
        *bgR = 32;   *bgG = 5;   *bgB = 12;
    } else if (strstr(title, "Tekken")) {
        *cardR = 190; *cardG = 150; *cardB = 20; // Tekken Gold
        *bgR = 48;   *bgG = 38;  *bgB = 5;
    } else if (strstr(title, "Silent Hill")) {
        *cardR = 70;  *cardG = 95;  *cardB = 90; // Silent Teal/Misty Grey
        *bgR = 18;   *bgG = 24;  *bgB = 22;
    } else {
        // Generate a beautiful, unique dominant color for each game based on its title!
        unsigned int hash = 5381;
        const char *p;
        for (p = title; *p; p++) {
            hash = ((hash << 5) + hash) + (unsigned char)*p;
        }

        // Stabilize brightness so cards are visible but premium
        *cardR = 50 + (hash % 100);
        *cardG = 50 + ((hash >> 8) % 100);
        *cardB = 50 + ((hash >> 16) % 100);

        // Subdued background color for smooth transitions
        *bgR = *cardR / 4;
        *bgG = *cardG / 4;
        *bgB = *cardB / 4;
    }
}

typedef struct {
    const char *serial;     // Cleaned lowercase alphanumeric serial
    const char *cleanTitle; // Cleaned lowercase alphanumeric title
    const char *company;
} popular_game_t;

static const popular_game_t gPopularGames[] = {
    // GTA Series
    {"slus20946", "grandtheftautosanandreas", "Rockstar Games"},
    {"sles52541", "grandtheftautosanandreas", "Rockstar Games"},
    {"slpm66119", "grandtheftautosanandreas", "Rockstar Games"},
    {"slus20552", "grandtheftautovicecity", "Rockstar Games"},
    {"sles51061", "grandtheftautovicecity", "Rockstar Games"},
    {"slus20062", "grandtheftauto3", "Rockstar Games"},
    {"sles50330", "grandtheftauto3", "Rockstar Games"},
    {"slus20062", "grandtheftautoiii", "Rockstar Games"},
    {"sles50330", "grandtheftautoiii", "Rockstar Games"},

    // Gran Turismo
    {"scus97102", "granturismo3aspec", "Polyphony Digital"},
    {"sces50294", "granturismo3aspec", "Polyphony Digital"},
    {"scus97102", "granturismo3", "Polyphony Digital"},
    {"sces50294", "granturismo3", "Polyphony Digital"},
    {"scus97328", "granturismo4", "Polyphony Digital"},
    {"sces51719", "granturismo4", "Polyphony Digital"},

    // God of War
    {"scus97111", "godofwar", "Santa Monica Studio"},
    {"sces53081", "godofwar", "Santa Monica Studio"},
    {"scus97481", "godofwar2", "Santa Monica Studio"},
    {"sces54206", "godofwar2", "Santa Monica Studio"},
    {"scus97481", "godofwarii", "Santa Monica Studio"},
    {"sces54206", "godofwarii", "Santa Monica Studio"},

    // Metal Gear Solid
    {"slus20915", "metalgearsolid3snakeeter", "Konami"},
    {"sles52584", "metalgearsolid3snakeeter", "Konami"},
    {"slus20915", "metalgearsolid3", "Konami"},
    {"sles52584", "metalgearsolid3", "Konami"},
    {"slus20144", "metalgearsolid2sonsofliberty", "Konami"},
    {"sles50383", "metalgearsolid2sonsofliberty", "Konami"},
    {"slus20144", "metalgearsolid2", "Konami"},
    {"sles50383", "metalgearsolid2", "Konami"},

    // Final Fantasy
    {"slus20312", "finalfantasy10", "Square Enix"},
    {"sles50490", "finalfantasy10", "Square Enix"},
    {"slus20312", "finalfantasyx", "Square Enix"},
    {"sles50490", "finalfantasyx", "Square Enix"},
    {"slus20963", "finalfantasy12", "Square Enix"},
    {"sles54354", "finalfantasy12", "Square Enix"},
    {"slus20963", "finalfantasyxii", "Square Enix"},
    {"sles54354", "finalfantasyxii", "Square Enix"},

    // Resident Evil & Silent Hill
    {"slus21134", "residentevil4", "Capcom"},
    {"sles53702", "residentevil4", "Capcom"},
    {"slus20228", "silenthill2", "Konami"},
    {"sles50382", "silenthill2", "Konami"},
    {"sles51156", "silenthill2", "Konami"},
    {"slus20633", "silenthill3", "Konami"},
    {"sles51434", "silenthill3", "Konami"},
    {"slus20873", "silenthill4", "Konami"},
    {"sles52445", "silenthill4", "Konami"},
    {"slus20184", "residentevilcodeveronica", "Capcom"},
    {"sles50306", "residentevilcodeveronica", "Capcom"},
    {"slus20765", "residenteviloutbreak", "Capcom"},
    {"sles51586", "residenteviloutbreak", "Capcom"},

    // Kingdom Hearts
    {"slus20374", "kingdomhearts", "Square Enix"},
    {"sles51228", "kingdomhearts", "Square Enix"},
    {"slus21005", "kingdomhearts2", "Square Enix"},
    {"sles54114", "kingdomhearts2", "Square Enix"},
    {"slus21005", "kingdomheartsii", "Square Enix"},
    {"sles54114", "kingdomheartsii", "Square Enix"},

    // Others
    {"slus21207", "dragonquestviii", "Level-5"},
    {"sles53974", "dragonquestviii", "Level-5"},
    {"slus21207", "dragonquest8", "Level-5"},
    {"sles53974", "dragonquest8", "Level-5"},
    {"slus21361", "okami", "Capcom"},
    {"sles54439", "okami", "Capcom"},
    {"slus20022", "devilmaycry", "Capcom"},
    {"sles50386", "devilmaycry", "Capcom"},
    {"slus21132", "devilmaycry3", "Capcom"},
    {"sles53038", "devilmaycry3", "Capcom"},
    {"scus97472", "shadowofthecolossus", "Team Ico"},
    {"sces53326", "shadowofthecolossus", "Team Ico"},

    {"scus97124", "jakanddaxter", "Naughty Dog"},
    {"sces50361", "jakanddaxter", "Naughty Dog"},
    {"scus97265", "jak2", "Naughty Dog"},
    {"sces51608", "jak2", "Naughty Dog"},
    {"scus97330", "jak3", "Naughty Dog"},
    {"sces52460", "jak3", "Naughty Dog"},

    {"scus97199", "ratchetclank", "Insomniac Games"},
    {"sces50916", "ratchetclank", "Insomniac Games"},
    {"scus97155", "slycooper", "Sucker Punch"},
    {"sces51190", "slycooper", "Sucker Punch"},
    {"scus97316", "sly2", "Sucker Punch"},
    {"sces52529", "sly2", "Sucker Punch"},
    {"scus97464", "sly3", "Sucker Punch"},
    {"sces53845", "sly3", "Sucker Punch"},

    {"slus21050", "burnout3", "Criterion Games"},
    {"sles52585", "burnout3", "Criterion Games"},
    {"slus21242", "burnoutrevenge", "Criterion Games"},
    {"sles53506", "burnoutrevenge", "Criterion Games"},

    {"slus20811", "needforspeedunderground", "EA Games"},
    {"sles51967", "needforspeedunderground", "EA Games"},
    {"slus21065", "needforspeedunderground2", "EA Games"},
    {"sles52725", "needforspeedunderground2", "EA Games"},
    {"slus21244", "needforspeedmostwanted", "EA Games"},
    {"sles53507", "needforspeedmostwanted", "EA Games"},

    {"slus21269", "bully", "Rockstar Games"},
    {"sles54227", "bully", "Rockstar Games"},
    {"slus20743", "princeofpersia", "Ubisoft"},
    {"sles51918", "princeofpersia", "Ubisoft"},

    {"slus21022", "tekken5", "Namco"},
    {"sles53201", "tekken5", "Namco"},
    {"slus20015", "tekkentag", "Namco"},
    {"sles50001", "tekkentag", "Namco"},
    {"slus20643", "soulcalibur2", "Namco"},
    {"sles51702", "soulcalibur2", "Namco"},
    {"slus21223", "soulcalibur3", "Namco"},
    {"sles53312", "soulcalibur3", "Namco"},

    {"slus21004", "defjamfightforny", "EA Games"},
    {"sles52545", "defjamfightforny", "EA Games"},
    {"slus20565", "defjamvendetta", "EA Games"},
    {"sles51459", "defjamvendetta", "EA Games"},

    {"slus20322", "midnightclub2", "Rockstar Games"},
    {"sles51356", "midnightclub2", "Rockstar Games"},
    {"slus21029", "midnightclub3", "Rockstar Games"},
    {"sles53036", "midnightclub3", "Rockstar Games"},

    {"slus20041", "tonyhawksproskater3", "Activision"},
    {"sles50438", "tonyhawksproskater3", "Activision"},
    {"slus20504", "tonyhawksproskater4", "Activision"},
    {"sles51196", "tonyhawksproskater4", "Activision"},
    {"slus20729", "tonyhawksunderground", "Activision"},
    {"sles51882", "tonyhawksunderground", "Activision"},
    {"slus21020", "tonyhawksunderground2", "Activision"},
    {"sles52647", "tonyhawksunderground2", "Activision"},

    {"slus21376", "black", "Criterion Games"},
    {"sles53886", "black", "Criterion Games"},

    {"slus20881", "mortalkombatdeception", "Midway Games"},
    {"sles52724", "mortalkombatdeception", "Midway Games"},
    {"slus20423", "mortalkombatdeadlyalliance", "Midway Games"},
    {"sles51244", "mortalkombatdeadlyalliance", "Midway Games"},
    {"slus21087", "mortalkombatshaolinmonks", "Midway Games"},
    {"sles53524", "mortalkombatshaolinmonks", "Midway Games"},
    {"slus21410", "mortalkombatarmageddon", "Midway Games"},
    {"sles54316", "mortalkombatarmageddon", "Midway Games"},

    {"slus20724", "spiderman2", "Activision"},
    {"sles52372", "spiderman2", "Activision"},
    {"slus21240", "starwarsbattlefront2", "LucasArts"},
    {"sles53531", "starwarsbattlefront2", "LucasArts"},
    {"slus21240", "starwarsbattlefrontii", "LucasArts"},
    {"sles53531", "starwarsbattlefrontii", "LucasArts"},
    {"slus20898", "starwarsbattlefront", "LucasArts"},
    {"sles52450", "starwarsbattlefront", "LucasArts"},

    {"slus21106", "splintercellchaostheory", "Ubisoft"},
    {"sles53106", "splintercellchaostheory", "Ubisoft"},
    {"slus20321", "splintercell", "Ubisoft"},
    {"sles51256", "splintercell", "Ubisoft"},

    {"slus21153", "hitmanbloodmoney", "IO Interactive"},
    {"sles53656", "hitmanbloodmoney", "IO Interactive"},
    {"slus20144", "hitman2", "IO Interactive"},
    {"sles50703", "hitman2", "IO Interactive"},
    {"slus20882", "hitmancontracts", "IO Interactive"},
    {"sles52014", "hitmancontracts", "IO Interactive"},

    {"slus20216", "maxpayne", "Rockstar Games"},
    {"sles50277", "maxpayne", "Rockstar Games"},
    {"slus20728", "maxpayne2", "Rockstar Games"},
    {"sles52091", "maxpayne2", "Rockstar Games"},

    {"slus20018", "onimushawarlords", "Capcom"},
    {"sles50181", "onimushawarlords", "Capcom"},
    {"slus20393", "onimusha2", "Capcom"},
    {"sles50930", "onimusha2", "Capcom"},
    {"slus20694", "onimusha3", "Capcom"},
    {"sles52157", "onimusha3", "Capcom"},

    {"slus20204", "beyondgoodevil", "Ubisoft"},
    {"sles51916", "beyondgoodevil", "Ubisoft"},
    {"slus21008", "katamaridamacy", "Namco"},
    {"slus21237", "welovekatamari", "Namco"},
    {"sles54035", "welovekatamari", "Namco"},

    {"slus20326", "ssxtricky", "EA Sports"},
    {"sles50577", "ssxtricky", "EA Sports"},
    {"slus20772", "ssx3", "EA Sports"},
    {"sles51648", "ssx3", "EA Sports"},
    {"slus20650", "nbastreetvol2", "EA Sports"},
    {"sles51568", "nbastreetvol2", "EA Sports"},
    {"slus20968", "nbastreetv3", "EA Sports"},
    {"sles52956", "nbastreetv3", "EA Sports"},

    {"slus20979", "destroyallhumans", "THQ"},
    {"sles53160", "destroyallhumans", "THQ"},
    {"slus21437", "destroyallhumans2", "THQ"},
    {"sles54245", "destroyallhumans2", "THQ"},

    {"slus20624", "simpsonshitrun", "Vivendi Games"},
    {"sles51823", "simpsonshitrun", "Vivendi Games"},
    {"slus20199", "simpsonsroadrage", "Vivendi Games"},
    {"sles50460", "simpsonsroadrage", "Vivendi Games"},

    {"slus20238", "crashbandicoot", "Traveller's Tales"},
    {"sles50386", "crashbandicoot", "Traveller's Tales"},
    {"slus20909", "crashtwinsanity", "Traveller's Tales"},
    {"sles52568", "crashtwinsanity", "Traveller's Tales"},
    {"slus20453", "spyro", "Universal Interactive"},
    {"sles51153", "spyro", "Universal Interactive"},

    {"slus21224", "guitarhero", "RedOctane"},
    {"slus21443", "guitarhero2", "RedOctane"},
    {"sles54435", "guitarhero2", "RedOctane"},
    {"slus21671", "guitarhero3", "Activision"},
    {"sles54944", "guitarhero3", "Activision"},

    {"slus21569", "persona3", "Atlus"},
    {"sles55018", "persona3", "Atlus"},
    {"slus21782", "persona4", "Atlus"},
    {"sles55473", "persona4", "Atlus"},

    {"slus20685", "apeescape2", "Sony Computer Ent."},
    {"sces50964", "apeescape2", "Sony Computer Ent."},
    {"slus21177", "apeescape3", "Sony Computer Ent."},
    {"sces53642", "apeescape3", "Sony Computer Ent."},

    {"slus20827", "manhunt", "Rockstar Games"},
    {"sles52023", "manhunt", "Rockstar Games"},
    {"slus21613", "manhunt2", "Rockstar Games"},
    {"sles54819", "manhunt2", "Rockstar Games"},

    {"slus20565", "championsofnorrath", "Sony Online Ent."},
    {"sles52325", "championsofnorrath", "Sony Online Ent."},
    {"scus97111", "darkcloud", "Level-5"},
    {"sces50252", "darkcloud", "Level-5"},
    {"scus97213", "darkcloud2", "Level-5"},
    {"sces51624", "darkcloud2", "Level-5"},
    {"slus20469", "xenosaga", "Monolith Soft"},
    {"slus20666", "disgaeahourofdarkness", "Nippon Ichi"},
    {"sles52329", "disgaeahourofdarkness", "Nippon Ichi"},
    {"slus21218", "urbanreign", "Bandai Namco"},
    {"sles53553", "urbanreign", "Bandai Namco"}
};

static const char *getGameDeveloper(const char *startup, const char *title)
{
    int i;
    int numPopularGames = sizeof(gPopularGames) / sizeof(gPopularGames[0]);

    if (startup && startup[0] != '\0') {
        char cleanStartup[64];
        int sLen = 0;
        for (i = 0; startup[i] != '\0' && sLen < 63; i++) {
            char c = startup[i];
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
                cleanStartup[sLen++] = c;
            } else if (c >= 'A' && c <= 'Z') {
                cleanStartup[sLen++] = c + 32;
            }
        }
        cleanStartup[sLen] = '\0';

        // 1. Direct Game ID (Serial) matching (highest priority!)
        for (i = 0; i < numPopularGames; i++) {
            if (gPopularGames[i].serial && gPopularGames[i].serial[0] != '\0') {
                if (strcmp(cleanStartup, gPopularGames[i].serial) == 0) {
                    return gPopularGames[i].company;
                }
            }
        }
    }

    // 2. Title matching (fallback)
    static char cleanTitle[128];
    getCleanGameName(title, cleanTitle, sizeof(cleanTitle));

    // Convert cleanTitle to lowercase alphanumeric (remove underscores, etc.)
    char cleanTitleAlpha[128];
    int tLen = 0;
    for (i = 0; cleanTitle[i] != '\0' && tLen < 127; i++) {
        char c = cleanTitle[i];
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            cleanTitleAlpha[tLen++] = c;
        } else if (c >= 'A' && c <= 'Z') {
            cleanTitleAlpha[tLen++] = c + 32;
        }
    }
    cleanTitleAlpha[tLen] = '\0';

    for (i = 0; i < numPopularGames; i++) {
        if (gPopularGames[i].cleanTitle && gPopularGames[i].cleanTitle[0] != '\0') {
            if (strstr(cleanTitleAlpha, gPopularGames[i].cleanTitle) != NULL ||
                strstr(gPopularGames[i].cleanTitle, cleanTitleAlpha) != NULL) {
                return gPopularGames[i].company;
            }
        }
    }

    return "PlayStation 2";
}

void drawPS5GameHeaderArtwork(const char *title, int x, int y, int w, int h)
{
    net_req_t *entry = findNetCacheEntry(title);
    u8 cardR, cardG, cardB, bgR, bgG, bgB;
    extern int gVMode;
    int useWideSquare = (gVMode == 10 || gVMode == 11);
    int drawW = useWideSquare ? ((h * 78 + 50) / 100) : h;

    if (entry && entry->coverPath[0] != '\0') {
        entry->lastCoverFrame = gPS5TextureFrame;
        if (entry->hasTex == 0 && gPS5TextureLoadsThisFrame < PS5_MAX_TEXTURE_LOADS_PER_FRAME) {
            gPS5TextureLoadsThisFrame++;
            ps5QueueTextureLoad(entry, 0);
        }

        if (entry->hasTex == 1) {
            if (useWideSquare)
                rmDrawRoundedSquareThumbnailWide(&entry->coverTex, x, y, drawW, h, 4);
            else
                rmDrawRoundedSquareThumbnail(&entry->coverTex, x, y, h, 4, GS_SETREG_RGBA(0x80, 0x80, 0x80, 0x80));
            return;
        }
    }

    getGameColors(title, &cardR, &cardG, &cardB, &bgR, &bgG, &bgB);
    if (useWideSquare)
        rmDrawRoundedRectWide(x, y, drawW, h, 4, GS_SETREG_RGBA(cardR, cardG, cardB, 0x68));
    else
        rmDrawRoundedRect(x, y, h, h, 4, GS_SETREG_RGBA(cardR, cardG, cardB, 0x68));
}

static int drawPS5IconAndText(int iconId, const char *text, int font, int x, int y, u64 color)
{
    GSTEXTURE *iconTex = thmGetTexture(iconId);
    int w = 0;
    int h = 14;

    if (iconTex && iconTex->Mem) {
        w = (iconTex->Width * h) / iconTex->Height;
        rmDrawPixmap(iconTex, x, y, ALIGN_VCENTER | ALIGN_LEFT, w, h, 1, color);
        x += rmWideScale(w) + 6;
    }

    x = fntRenderString(font, x, y, ALIGN_VCENTER | ALIGN_LEFT, 0.65f, 0.65f, text, color);
    return x;
}

static int drawPS5RightIconAndText(int iconId, const char *text, int font, int rightX, int y, u64 color)
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

static void drawPS5SettingsText(int font, int x, int y, int align, const char *text, int focused)
{
    int renderFont = focused && gPS5SemiBoldFont >= 0 ? gPS5SemiBoldFont : font;

    if (focused && !(align & ALIGN_RIGHT))
        drawPS5FocusPointer(x - 22, y);

    fntRenderString(renderFont, x, y, align | ALIGN_VCENTER, 0, 0, text,
        focused ? GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80) : GS_SETREG_RGBA(0x58, 0x58, 0x58, 0x56));
}

static const char *getPS5DeviceLabel(item_list_t *support)
{
    if (support == NULL)
        return "";

    if (support->mode == HDD_MODE)
        return "Internal HDD APA";

    if (support->mode == ETH_MODE)
        return "SMB";

    if (support->mode >= BDM_MODE && support->mode < ETH_MODE) {
        bdm_device_data_t *bdmData = (bdm_device_data_t *)support->priv;
        if (bdmData != NULL) {
            switch (bdmData->bdmDeviceType) {
                case BDM_TYPE_USB:
                    return "USB";
                case BDM_TYPE_SDC:
                    return "MX4SIO";
                case BDM_TYPE_ATA:
                    return "Internal HDD GPT/MBR";
                case BDM_TYPE_ILINK:
                    return "iLink";
            }
        }
        return "BDM";
    }

    return "";
}

static void drawPS5DeviceLoadingOverlay(void)
{
    GSTEXTURE *loader = thmGetTexture(LOADER_ICON);
    int loaderSize = 14;
    int loaderX = (screenWidth - 20 - (loaderSize / 2)) * 4 / rmGetAspectWidth();
    int loaderY = screenHeight - 20;
    float angle = (float)guiFrameId * 0.08f;

    if (loader && loader->Mem)
        rmDrawRotatedPixmap(loader, loaderX, loaderY, loaderSize, loaderSize, angle, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x78));
}

static void drawPS5SmbDialogOverlay(void)
{
    extern int gPS5SmbDialogState;
    extern int gPS5SmbDialogFocus;
    extern char gPS5SmbDialogMessage[128];

    if (gPS5SmbDialogState == 0)
        return;

    int dlgW = 420;
    int dlgH = 178;
    int dlgX = (screenWidth - dlgW) / 2;
    int dlgY = (screenHeight - dlgH) / 2;
    int btnY = dlgY + dlgH - 34;

    rmDrawRect(0, 0, screenWidth, screenHeight, GS_SETREG_RGBA(0, 0, 0, 0x60));
    rmDrawRoundedRect(dlgX - 1, dlgY - 1, dlgW + 2, dlgH + 2, 8, GS_SETREG_RGBA(0x30, 0x30, 0x30, 0x80));
    rmDrawRoundedRect(dlgX, dlgY, dlgW, dlgH, 7, GS_SETREG_RGBA(0x08, 0x08, 0x08, 0xFA));

    fntRenderString(gPS5TitleFont, dlgX + 24, dlgY + 22, ALIGN_LEFT, 0, 0, "SMB Games", GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));

    if (gPS5SmbDialogState == 2) {
        GSTEXTURE *loader = thmGetTexture(LOADER_ICON);
        int loaderSize = 14;
        int loaderX = (dlgX + 32) * 4 / rmGetAspectWidth();
        int loaderY = dlgY + 84;
        float angle = (float)guiFrameId * 0.08f;
        if (loader && loader->Mem)
            rmDrawRotatedPixmap(loader, loaderX, loaderY, loaderSize, loaderSize, angle, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
        fntRenderString(gPS5RegFont, dlgX + 58, dlgY + 72, ALIGN_LEFT, dlgW - 82, 40, gPS5SmbDialogMessage, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
    } else {
        fntRenderString(gPS5RegFont, dlgX + 24, dlgY + 70, ALIGN_LEFT, dlgW - 48, 58, gPS5SmbDialogMessage, GS_SETREG_RGBA(0xEE, 0xEE, 0xEE, 0x80));
    }

    if (gPS5SmbDialogState == 1) {
        int yesX = dlgX + dlgW - 150;
        int noX = dlgX + dlgW - 74;
        fntRenderString(gPS5SmbDialogFocus ? gPS5SemiBoldFont : gPS5RegFont, yesX, btnY, ALIGN_CENTER | ALIGN_VCENTER, 0, 0, "Yes",
            gPS5SmbDialogFocus ? GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80) : GS_SETREG_RGBA(0x78, 0x78, 0x78, 0x70));
        fntRenderString(!gPS5SmbDialogFocus ? gPS5SemiBoldFont : gPS5RegFont, noX, btnY, ALIGN_CENTER | ALIGN_VCENTER, 0, 0, "No",
            !gPS5SmbDialogFocus ? GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80) : GS_SETREG_RGBA(0x78, 0x78, 0x78, 0x70));
    } else if (gPS5SmbDialogState == 3 || gPS5SmbDialogState == 4) {
        drawPS5RightIconAndText(CIRCLE_ICON, "Close", gPS5SemiBoldFont, dlgX + dlgW - 24, btnY, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
    }
}

static void drawPS5SmbCheckDialogOverlay(void)
{
    extern int gPS5SmbCheckDialogState;
    extern char gPS5SmbCheckDialogMessage[256];

    if (gPS5SmbCheckDialogState == 0)
        return;

    int dlgW = 460;
    int dlgH = 200;
    int dlgX = (screenWidth - dlgW) / 2;
    int dlgY = (screenHeight - dlgH) / 2;
    int btnY = dlgY + dlgH - 34;

    rmDrawRect(0, 0, screenWidth, screenHeight, GS_SETREG_RGBA(0, 0, 0, 0x60));
    rmDrawRoundedRect(dlgX - 1, dlgY - 1, dlgW + 2, dlgH + 2, 8, GS_SETREG_RGBA(0x30, 0x30, 0x30, 0x80));
    rmDrawRoundedRect(dlgX, dlgY, dlgW, dlgH, 7, GS_SETREG_RGBA(0x08, 0x08, 0x08, 0xFA));

    fntRenderString(gPS5TitleFont, dlgX + 24, dlgY + 22, ALIGN_LEFT, 0, 0, "Check SMB Connection", GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));

    if (gPS5SmbCheckDialogState == 1) {
        GSTEXTURE *loader = thmGetTexture(LOADER_ICON);
        int loaderSize = 14;
        int loaderX = (dlgX + dlgW - 20 - (loaderSize / 2)) * 4 / rmGetAspectWidth();
        int loaderY = dlgY + dlgH - 20 - (loaderSize / 2);
        float angle = (float)guiFrameId * 0.08f;
        if (loader && loader->Mem)
            rmDrawRotatedPixmap(loader, loaderX, loaderY, loaderSize, loaderSize, angle, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
        fntRenderString(gPS5RegFont, dlgX + 24, dlgY + 72, ALIGN_LEFT, dlgW - 48, 60, gPS5SmbCheckDialogMessage, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
    } else {
        fntRenderString(gPS5RegFont, dlgX + 24, dlgY + 70, ALIGN_LEFT, dlgW - 48, 80, gPS5SmbCheckDialogMessage, GS_SETREG_RGBA(0xEE, 0xEE, 0xEE, 0x80));
    }

    if (gPS5SmbCheckDialogState == 2 || gPS5SmbCheckDialogState == 3) {
        drawPS5RightIconAndText(CIRCLE_ICON, "Close", gPS5SemiBoldFont, dlgX + dlgW - 24, btnY, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
    }
}

static void drawPS5SaveBusyOverlay(void)
{
    GSTEXTURE *loader;
    int loaderSize;
    int loaderX;
    int loaderY;
    float angle;

    if (!gPS5SaveBusyFrame || guiFrameId < (int)gPS5SaveBusyFrame || guiFrameId - (int)gPS5SaveBusyFrame >= 45) {
        if (gPS5SaveBusyFrame && guiFrameId - (int)gPS5SaveBusyFrame >= 45)
            gPS5SaveBusyFrame = 0;
        return;
    }

    loader = thmGetTexture(LOADER_ICON);
    loaderSize = 12;
    loaderX = 24 * 4 / rmGetAspectWidth();
    loaderY = screenHeight - 20;
    angle = (float)guiFrameId * 0.08f;

    if (loader && loader->Mem)
        rmDrawRotatedPixmap(loader, loaderX, loaderY, loaderSize, loaderSize, angle, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x78));
}

static void clearNetCache(void)
{
    int i;
    gPS5CacheGeneration++;
    for (i = 0; i < gNetCacheCount; i++) {
        unloadPS5CoverTexture(&gNetCache[i]);
        unloadPS5LogoTexture(&gNetCache[i]);
    }
    gNetCacheCount = 0;
    gPS5UserHasNavigated = 0;
}

void ps5ClearCoverCache(void)
{
    clearNetCache();
}

void ps5RetryMissingCoverCache(void)
{
    int i;

    for (i = 0; i < gNetCacheCount; i++) {
        if (gNetCache[i].coverPath[0] != '\0' && gNetCache[i].hasTex != 1)
            gNetCache[i].hasTex = 0;
        if (gNetCache[i].logoPath[0] != '\0' && gNetCache[i].hasLogoTex != 1)
            gNetCache[i].hasLogoTex = 0;
    }
}

int gPS5AlphaIdx = 0; // Global alphabet index (starts at '#')

static int ps5TitleMatchesAlpha(const char *title, int alphaIdx)
{
    char firstChar;

    if (alphaIdx <= 0)
        return 1;
    if (title == NULL || title[0] == '\0')
        return 0;

    firstChar = title[0];
    if (firstChar >= 'a' && firstChar <= 'z')
        firstChar -= 32;

    return firstChar >= 'A' && firstChar <= 'Z' && (firstChar - 'A' + 1) == alphaIdx;
}

void ps5JumpToAlphabetGame(int targetIdx)
{
    extern menu_list_t *menuGetSelectedItem(void);
    menu_list_t *selectedItem = menuGetSelectedItem();
    if (!selectedItem || !selectedItem->item || !selectedItem->item->submenu)
        return;
        
    submenu_list_t *curr = selectedItem->item->submenu;
    submenu_list_t *match = NULL;

    while (curr) {
        const char *title = submenuItemGetText(&curr->item);
        if (themeGameItemIsAvailable(selectedItem->item->userdata, curr->item.id) && ps5TitleMatchesAlpha(title, targetIdx)) {
            match = curr;
            break;
        }
        curr = curr->next;
    }

    if (match) {
        selectedItem->item->current = match;
        selectedItem->item->pagestart = match;
    }
}

void ps5MoveAlphabetGame(int direction)
{
    extern menu_list_t *menuGetSelectedItem(void);
    extern int gPS5SortMode;
    menu_list_t *selectedItem = menuGetSelectedItem();
    int idx;

    if (!selectedItem || !selectedItem->item || !selectedItem->item->submenu)
        return;

    if (gPS5SortMode == 0) {
        idx = gPS5AlphaIdx + (direction > 0 ? 1 : -1);
        if (idx < 0)
            idx = 0;
        else if (idx > 26)
            idx = 26;
        gPS5AlphaIdx = idx;
        ps5JumpToAlphabetGame(idx);
        return;
    }

    if (direction > 0) {
        for (idx = gPS5AlphaIdx + 1; idx <= 26; idx++) {
            submenu_list_t *curr = selectedItem->item->submenu;
            while (curr) {
                if (themeGameItemIsAvailable(selectedItem->item->userdata, curr->item.id) && ps5TitleMatchesAlpha(submenuItemGetText(&curr->item), idx)) {
                    gPS5AlphaIdx = idx;
                    ps5JumpToAlphabetGame(idx);
                    return;
                }
                curr = curr->next;
            }
        }
    } else if (direction < 0) {
        if (gPS5AlphaIdx <= 1) {
            gPS5AlphaIdx = 0;
            ps5JumpToAlphabetGame(0);
            return;
        }

        for (idx = gPS5AlphaIdx - 1; idx >= 1; idx--) {
            submenu_list_t *curr = selectedItem->item->submenu;
            while (curr) {
                if (themeGameItemIsAvailable(selectedItem->item->userdata, curr->item.id) && ps5TitleMatchesAlpha(submenuItemGetText(&curr->item), idx)) {
                    gPS5AlphaIdx = idx;
                    ps5JumpToAlphabetGame(idx);
                    return;
                }
                curr = curr->next;
            }
        }

        gPS5AlphaIdx = 0;
        ps5JumpToAlphabetGame(0);
    }
}


static void drawPS5Launcher(struct menu_list *menu, struct submenu_list *item, struct theme_element *elem)
{
    // Initialize Fonts and Mask Textures if not loaded
    initPS5MaskTextures();
    gPS5TextureFrame++;
    gPS5TextureLoadsThisFrame = 0;
    ps5CompleteAsyncTextureLoad();
    ps5RecoverTimedOutTextureLoad();
    if (gPS5ArtworkResolveDirty) {
        gPS5ArtworkResolveDirty = 0;
        clearNetCache();
    }

    static unsigned int lastBdmEventGeneration = 0;
    static unsigned int pendingBdmEventGeneration = 0;
    static int bdmEventQuietFrames = 0;
    int bdmEventStable = 1;
    if (pendingBdmEventGeneration != gBdmEventGeneration) {
        pendingBdmEventGeneration = gBdmEventGeneration;
        bdmEventQuietFrames = 0;
        bdmEventStable = 0;
    } else if (lastBdmEventGeneration != pendingBdmEventGeneration) {
        if (bdmEventQuietFrames < 120) {
            bdmEventQuietFrames++;
            bdmEventStable = 0;
        } else {
            lastBdmEventGeneration = pendingBdmEventGeneration;
            clearNetCache();
        }
    }

    char *prefix = "";
    const char *deviceLabel = "";
    const char *selectedStartup = NULL;
    const char *selectedPrefix = "";
    int isUnplugged = 0;
    int allowDeviceProbe = 1;
    int selectedStableFrames = 0;
    if (item && gPS5ActiveTab == 0) {
        const char *startup = NULL;
        const char *selectedTitle = submenuItemGetText(&item->item);
        static char lastStableTitle[64] = "";
        static int stableFrameCounter = 0;
        int sourceId;
        item_list_t *list = resolveThemeGameItem(menu->item->userdata, item->item.id, &sourceId);
        if (list) {
            int isBdmMode = list->mode >= BDM_MODE && list->mode < ETH_MODE;
            deviceLabel = getPS5DeviceLabel(list);
            if (list->itemGetPrefix) {
                prefix = list->itemGetPrefix(list);
                selectedPrefix = prefix;
            }
            if (isBdmMode && (prefix == NULL || prefix[0] == '\0' || bdmIsUsbPathDisconnected(prefix))) {
                isUnplugged = 1;
            }
            if (!isUnplugged && list->itemGetStartup) {
                startup = list->itemGetStartup(list, sourceId);
                selectedStartup = startup;
            }
        }

        static int lastIsUnplugged = -1;
        if (isUnplugged != lastIsUnplugged) {
            lastIsUnplugged = isUnplugged;
        }

        if (strcmp(lastStableTitle, selectedTitle) != 0) {
            strncpy(lastStableTitle, selectedTitle, sizeof(lastStableTitle) - 1);
            lastStableTitle[sizeof(lastStableTitle) - 1] = '\0';
            stableFrameCounter = 0;
        } else if (stableFrameCounter < 90) {
            stableFrameCounter++;
        }
        selectedStableFrames = stableFrameCounter;

        if (list && bdmEventStable && list->itemGetCount(list) > 0 && !isUnplugged &&
            gPS5CarouselNavInterrupt <= 0 && selectedStableFrames >= PS5_LOGO_LOAD_DELAY_FRAMES) {
            triggerNetFetch(submenuItemGetText(&item->item), startup, prefix, allowDeviceProbe);
        }
    }

    // Header Navigation (Top Left & Top Right)
    const int gamesX = 50;
    const int appsX = 140;
    const int settingsX = 220;
    const int l1X = 30;
    int l1Width = rmUnScaleX(fntCalcDimensions(gPS5SmallFont, "L1"));
    int tabTextGap = gamesX - (l1X + l1Width);
    int settingsR1X = settingsX + rmUnScaleX(fntCalcDimensions(gPS5RegFont, "Settings")) + tabTextGap;

    fntRenderString(gPS5ActiveTab == 0 ? gPS5SemiBoldFont : gPS5RegFont, gamesX, 20, ALIGN_LEFT, 0, 0, "Games",
        gPS5ActiveTab == 0 ? GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80) : GS_SETREG_RGBA(0x58, 0x58, 0x58, 0x56));
    fntRenderString(gPS5ActiveTab == 2 ? gPS5SemiBoldFont : gPS5RegFont, appsX, 20, ALIGN_LEFT, 0, 0, "Apps",
        gPS5ActiveTab == 2 ? GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80) : GS_SETREG_RGBA(0x58, 0x58, 0x58, 0x56));
    fntRenderString(gPS5ActiveTab == 1 ? gPS5SemiBoldFont : gPS5RegFont, settingsX, 20, ALIGN_LEFT, 0, 0, "Settings",
        gPS5ActiveTab == 1 ? GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80) : GS_SETREG_RGBA(0x58, 0x58, 0x58, 0x56));
    // Small L1/R1 indicators (smaller font size, offset adjusted for alignment)
    fntRenderString(gPS5SemiBoldFont, l1X, 27, ALIGN_LEFT, 0.70f, 0.70f, "L1", GS_SETREG_RGBA(0x58, 0x58, 0x58, 0x50));
    fntRenderString(gPS5SemiBoldFont, settingsR1X, 27, ALIGN_LEFT, 0.70f, 0.70f, "R1", GS_SETREG_RGBA(0x58, 0x58, 0x58, 0x50));
    if (gPS5ActiveTab == 0 && deviceLabel[0] != '\0')
        fntRenderString(gPS5SemiBoldFont, screenWidth - 20, 27, ALIGN_RIGHT, 0.70f, 0.70f, deviceLabel, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));

    if (gPS5ActiveTab == 0) {
        int ps5FocusedGameAvailable = 0;

        // Detect if the game list has been refreshed, unmounted, or changed
        submenu_list_t *first_item = item;
        while (first_item && first_item->prev) {
            first_item = first_item->prev;
        }

        static char lastFirstItemTitle[64] = "";
        if (first_item) {
            const char *firstTitle = submenuItemGetText(&first_item->item);
            if (strcmp(lastFirstItemTitle, firstTitle) != 0) {
                clearNetCache();
                strncpy(lastFirstItemTitle, firstTitle, sizeof(lastFirstItemTitle) - 1);
                lastFirstItemTitle[sizeof(lastFirstItemTitle) - 1] = '\0';
            }
        } else {
            if (lastFirstItemTitle[0] != '\0') {
                clearNetCache();
                lastFirstItemTitle[0] = '\0';
            }
        }

        if (item) {
            // Load and render background game logo at bottom-right (smooth fade-in transition)
            static float logoAlpha = 0.0f;
            static char lastSelectedTitle[64] = "";
            const char *selTitle = submenuItemGetText(&item->item);
            int selectedVisibleInAlpha = ps5TitleMatchesAlpha(selTitle, gPS5AlphaIdx);
            extern int gPS5ShowCoverImages;
            extern int gPS5ShowGamesLogo;
            
            if (strcmp(lastSelectedTitle, selTitle) != 0) {
                logoAlpha = 0.0f;
                strncpy(lastSelectedTitle, selTitle, sizeof(lastSelectedTitle) - 1);
                lastSelectedTitle[sizeof(lastSelectedTitle) - 1] = '\0';
            } else if (gPS5CarouselNavInterrupt > 0) {
                logoAlpha = 0.0f;
            }

            if (gPS5ShowCoverImages) {
                net_req_t *selCache = findNetCacheEntryForGame(selTitle, selectedStartup, selectedPrefix);

                if (!gPS5ShowCoverImages || !selectedVisibleInAlpha) {
                    logoAlpha = 0.0f;
                } else if (selCache && selCache->logoPath[0] != '\0') {
                    selCache->lastLogoFrame = gPS5TextureFrame;
                    if (selCache->hasLogoTex == 0 && !isUnplugged && gPS5CarouselNavInterrupt <= 0 &&
                        selectedStableFrames >= PS5_LOGO_LOAD_DELAY_FRAMES && gPS5TextureLoadsThisFrame < PS5_MAX_TEXTURE_LOADS_PER_FRAME) {
                        gPS5TextureLoadsThisFrame++;
                        ps5QueueTextureLoad(selCache, 1);
                    }
                    
                    if (selCache->hasLogoTex == 1) {
                        // Smooth asymptotic ease-in-out transition over roughly 12 frames
                        logoAlpha += (1.0f - logoAlpha) * 0.08f;
                        
                        float logoAspect = 1.0f;
                        if (selCache->logoTex.Width > 0 && selCache->logoTex.Height > 0) {
                            logoAspect = (float)selCache->logoTex.Width / (float)selCache->logoTex.Height;
                        }
                        
                        int lw = 500;
                        int lh = 250;
                        if (logoAspect < 2.0f) {
                            lw = (int)((float)lh * logoAspect);
                        } else {
                            lh = (int)((float)lw / logoAspect);
                        }
                        
                        // Map logoAlpha float [0.0 - 1.0] to OPL Alpha channel byte [0 - 128 (solid)]
                        int alphaVal = (int)(logoAlpha * 128.0f);
                        if (alphaVal > 128) alphaVal = 128;
                        if (alphaVal < 0) alphaVal = 0;
                        
                        rmDrawPixmap(&selCache->logoTex, screenWidth, screenHeight, ALIGN_BOTTOM | ALIGN_RIGHT, lw, lh, SCALING_RATIO, GS_SETREG_RGBA(0x80, 0x80, 0x80, alphaVal));
                    }
                }
            }

            // 2. Horizontal Carousel of Cards
            submenu_list_t *first_item = item;
            while (first_item->prev) {
                first_item = first_item->prev;
            }

            submenu_list_t *count_curr = first_item;
            int selected_index = 0;
            int total_count = 0;
            submenu_list_t *selectedVisibleItem = NULL;

            while (count_curr) {
                const char *title = submenuItemGetText(&count_curr->item);
                if (themeGameItemIsAvailable(menu->item->userdata, count_curr->item.id) && ps5TitleMatchesAlpha(title, gPS5AlphaIdx)) {
                    if (count_curr == item) {
                        selected_index = total_count;
                        selectedVisibleItem = count_curr;
                    }
                    total_count++;
                }
                count_curr = count_curr->next;
            }

            if (selectedVisibleItem == NULL && total_count > 0) {
                count_curr = first_item;
                while (count_curr) {
                    const char *title = submenuItemGetText(&count_curr->item);
                    if (themeGameItemIsAvailable(menu->item->userdata, count_curr->item.id) && ps5TitleMatchesAlpha(title, gPS5AlphaIdx)) {
                        selectedVisibleItem = count_curr;
                        selected_index = 0;
                        break;
                    }
                    count_curr = count_curr->next;
                }
            }
            ps5FocusedGameAvailable = selectedVisibleItem != NULL && selectedVisibleItem == item;

            if (gPS5AnimPos < 0.0f) {
                gPS5AnimPos = selected_index;
            } else {
                static int lastSelectedIndex = -1;
                if (lastSelectedIndex != selected_index) {
                    lastSelectedIndex = selected_index;
                }

                if (total_count > 0 && gPS5AnimPos > (float)(total_count - 1))
                    gPS5AnimPos = (float)(total_count - 1);
                if (gPS5AnimPos < 0.0f)
                    gPS5AnimPos = 0.0f;
                gPS5AnimPos += (selected_index - gPS5AnimPos) * 0.36f;
                float diff = selected_index - gPS5AnimPos;
                if (diff < 0.0f) diff = -diff;
                if (diff < 0.0002f) {
                    gPS5AnimPos = selected_index;
                }
            }
            int centerIndex = (int)gPS5AnimPos;
            int renderStart = centerIndex - 9;
            int renderEnd = centerIndex + 9;
            int idx = 0;

            if (renderStart < 0)
                renderStart = 0;
            if (renderEnd >= total_count)
                renderEnd = total_count - 1;

            count_curr = first_item;
            while (count_curr && total_count > 0) {
                const char *gameTitleText = submenuItemGetText(&count_curr->item);
                if (themeGameItemIsAvailable(menu->item->userdata, count_curr->item.id) && ps5TitleMatchesAlpha(gameTitleText, gPS5AlphaIdx)) {
                    if (idx >= renderStart && idx <= renderEnd) {
                        float distSigned = idx - gPS5AnimPos;
                        float dist = distSigned;
                        if (dist < 0.0f)
                            dist = -dist;

                        float t = 1.0f - dist;
                        if (t < 0.0f)
                            t = 0.0f;
                        if (t > 1.0f)
                            t = 1.0f;
                        t = t * t * (3.0f - 2.0f * t);

                        int maxCardWidth = ps5CarouselSquareWidthForHeight(130);
                        float itemStep = (float)(maxCardWidth - 12);
                        float cx = 120.0f + distSigned * itemStep;
                        submenu_list_t *curr_item = count_curr;
                        net_req_t *cacheEntry = NULL;
                        if (cx > -220.0f && cx < (float)(screenWidth + 220))
                            cacheEntry = preparePS5CarouselCardMedia(menu, curr_item, gameTitleText, isUnplugged, allowDeviceProbe);
                        if (cx > -90.0f && cx < (float)(screenWidth + 90)) {
                            int height = (int)(80.0f + t * 50.0f);
                    int width = ps5CarouselSquareWidthForHeight(height);
                    int hw = width / 2;
                    int x1 = (int)cx - hw;
                    int y1 = 96; // Anchor at the top edge for downward scaling!

                    // Dynamic Alpha based on distance from focus
                    int cardAlpha = (int)(0x22 + t * (0x74 - 0x22));

                    // 2a. Card Body (with custom Corner Radius & Dynamic Colors / Covers!)
                    u8 cR, cG, cB, bR, bG, bB;
                    int hasCover = 0;

                    getGameColors(gameTitleText, &cR, &cG, &cB, &bR, &bG, &bB);

                    if (cacheEntry && cacheEntry->state == 2 && cacheEntry->coverPath[0] != '\0')
                        hasCover = (cacheEntry->hasTex == 1);

                    if (gPS5ShowGamesLogo && hasCover && cacheEntry) {
                        rmDrawRoundedRectWide(x1, y1, width, height, 12, GS_SETREG_RGBA(0x00, 0x00, 0x00, 0x80));
                        rmDrawRoundedSquareThumbnailWide(&cacheEntry->coverTex, x1, y1, width, height, 12);
                    } else {
                        // 1. Draw beautifully colored rounded card
                        rmDrawRoundedRectWide(x1, y1, width, height, 12, GS_SETREG_RGBA(cR, cG, cB, cardAlpha));

                        // 2. Extract initials of the game dynamically
                        char initials[8];
                        int initCount = 0;
                        const char *pStr = gameTitleText;
                        
                        // Skip system prefixes (e.g. SLES_525.41, SLUS_209.46)
                        if (strlen(pStr) > 5 && pStr[4] == '_') {
                            pStr += 5;
                            while (*pStr && (*pStr == '_' || *pStr == '-' || *pStr == '.' || (*pStr >= '0' && *pStr <= '9'))) {
                                pStr++;
                            }
                        }
                        
                        while (*pStr && initCount < 4) {
                            if ((*pStr >= 'A' && *pStr <= 'Z') || (*pStr >= '0' && *pStr <= '9')) {
                                initials[initCount++] = *pStr;
                            } else if (*pStr >= 'a' && *pStr <= 'z') {
                                if (pStr == gameTitleText || *(pStr - 1) == ' ' || *(pStr - 1) == '_' || *(pStr - 1) == '-') {
                                    initials[initCount++] = *pStr - 32; // Convert to Uppercase
                                }
                            }
                            pStr++;
                        }
                        
                        if (initCount == 0 && gameTitleText[0] != '\0') {
                            strncpy(initials, "PS2", sizeof(initials));
                            initCount = 3;
                        }
                        if (initCount == 0)
                            continue;
                        initials[initCount] = '\0';

                        // 3. Draw the game's initials elegantly in the center of the card
                        float fontScale = ((float)height / 130.0f) * 0.70f;
                        fntRenderString(gPS5BoldFont, cx, y1 + height / 2, ALIGN_CENTER | ALIGN_VCENTER, fontScale, fontScale, initials, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, cardAlpha * 2 / 3));
                    }
                        }
                    }
                    idx++;
                    if (idx > renderEnd)
                        break;
                }
                count_curr = count_curr->next;
            }

            // 3. Dynamic Background Color Smooth Transition
            u8 cardR, cardG, cardB, bgR, bgG, bgB;
            getGameColors(selectedVisibleItem ? submenuItemGetText(&selectedVisibleItem->item) : "", &cardR, &cardG, &cardB, &bgR, &bgG, &bgB);

            // Use a deterministic medium-dark solid color from the focused game.
            if (selectedVisibleItem) {
                bgR = 24 + (bgR % 56);
                bgG = 24 + (bgG % 56);
                bgB = 24 + (bgB % 56);
            } else {
                bgR = 0;
                bgG = 0;
                bgB = 0;
            }

            // If the color is grey/desaturated, shift it to a deep premium PS5 midnight blue
            int maxVal = bgR > bgG ? (bgR > bgB ? bgR : bgB) : (bgG > bgB ? bgG : bgB);
            int minVal = bgR < bgG ? (bgR < bgB ? bgR : bgB) : (bgG < bgB ? bgG : bgB);
            if (maxVal - minVal < 10) {
                bgR = 18;
                bgG = 30;
                bgB = 48;
            }

            gPS5BgColorR = bgR;
            gPS5BgColorG = bgG;
            gPS5BgColorB = bgB;

            if (selectedVisibleItem) {
                const char *fullTitle = submenuItemGetText(&selectedVisibleItem->item);
                fntRenderString(gPS5TitleFont, 50, 316, ALIGN_LEFT, 0, 0, fullTitle, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));

                // Developer name (using Game ID lookup)
                int sourceId;
                item_list_t *support = resolveThemeGameItem(menu->item->userdata, selectedVisibleItem->item.id, &sourceId);
                const char *startup = NULL;
                if (support && support->itemGetStartup && !isUnplugged) {
                    startup = support->itemGetStartup(support, sourceId);
                }
                fntRenderString(gPS5RegFont, 50, 354, ALIGN_LEFT, 0, 0, getGameDeveloper(startup, fullTitle), GS_SETREG_RGBA(0x58, 0x58, 0x58, 0x56));
            } else {
                fntRenderString(gPS5BoldFont, 320, 245, ALIGN_CENTER, 0, 0, "NO GAMES FOUND", GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x2C));
            }

        } else {
            // No games placeholder card/text
            // No games placeholder card/text
            fntRenderString(gPS5BoldFont, 320, 245, ALIGN_CENTER, 0, 0, "NO GAMES FOUND", GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x2C));
        }

        // Draw bottom helper buttons in Games list (very bottom-left with 20px margin)
        int helperY = screenHeight - 20;
        extern int gSelectButton;
        int playIcon = gSelectButton == KEY_CIRCLE ? CIRCLE_ICON : CROSS_ICON;
        u64 actionColor = ps5FocusedGameAvailable ? GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80) : GS_SETREG_RGBA(0x58, 0x58, 0x58, 0x50);
        int nextX = drawPS5IconAndText(playIcon, "Play", gPS5SemiBoldFont, 50, helperY, actionColor);
        nextX = drawPS5IconAndText(SQUARE_ICON, "Refresh", gPS5SemiBoldFont, nextX + 20, helperY, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
        drawPS5IconAndText(TRIANGLE_ICON, "Options", gPS5SemiBoldFont, nextX + 20, helperY, actionColor);

        // 5. Draw Vertical Alphabet Carousel at the right end of the screen
        if (item) {
            static const char *gPS5AlphaChars = "#ABCDEFGHIJKLMNOPQRSTUVWXYZ";
            extern int gPS5AlphaIdx;
            static float gPS5AlphaAnimPos = 0.0f;
            
            // Track if user has pressed any navigation keys to set the gPS5UserHasNavigated flag
            if (!gPS5UserHasNavigated) {
                if (getKeyPressed(KEY_UP) || getKeyPressed(KEY_DOWN) ||
                    getKeyPressed(KEY_LEFT) || getKeyPressed(KEY_RIGHT) ||
                    getKeyPressed(KEY_L1) || getKeyPressed(KEY_R1) ||
                    getKeyPressed(KEY_L2) || getKeyPressed(KEY_R2)) {
                    gPS5UserHasNavigated = 1;
                }
            }

            gPS5AlphaAnimPos += ((float)gPS5AlphaIdx - gPS5AlphaAnimPos) * 0.20f;
            
            int alphabetX = 612;
            int i;
            for (i = 0; i < 27; i++) {
                float diff = (float)i - gPS5AlphaAnimPos;
                float absDiff = fabsf(diff);
                
                if (absDiff < 3.5f) {
                    float charY = 240.0f + diff * 28.0f;
                    int alphaVal = (int)((1.0f - (absDiff / 3.5f)) * 128.0f);
                    if (alphaVal < 0) alphaVal = 0;
                    if (alphaVal > 128) alphaVal = 128;
                    
                    char letterStr[2] = { gPS5AlphaChars[i], '\0' };
                    
                    if (i == gPS5AlphaIdx) {
                        // Active letter: white, larger scale, bold
                        fntRenderString(gPS5HeaderFont, alphabetX, (int)charY, ALIGN_CENTER | ALIGN_VCENTER, 0.70f, 0.70f, letterStr, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, alphaVal));
                    } else {
                        // Unfocused letters: grey, smaller scale
                        fntRenderString(gPS5RegFont, alphabetX, (int)charY, ALIGN_CENTER | ALIGN_VCENTER, 0.50f, 0.50f, letterStr, GS_SETREG_RGBA(0x58, 0x58, 0x58, alphaVal));
                    }
                }
            }
        }
        if ((gPS5TextureFrame % 30) == 0)
            sweepPS5TextureCache();

        if (gPS5RefreshBusyFrame && guiFrameId - (int)gPS5RefreshBusyFrame >= 90)
            gPS5RefreshBusyFrame = 0;

        {
            extern volatile int gPS5SmbUiLoading;
            if (bdmIsDeviceLoading() || gPS5SmbUiLoading || (gPS5RefreshBusyFrame && guiFrameId >= (int)gPS5RefreshBusyFrame))
                drawPS5DeviceLoadingOverlay();
        }
        if (gPS5CarouselNavInterrupt > 0)
            gPS5CarouselNavInterrupt--;
        drawPS5SmbDialogOverlay();
        drawPS5SmbCheckDialogOverlay();
    } else if (gPS5ActiveTab == 2) {
        extern volatile int gPS5AppsLoading;
        extern int gPS5AppsSelected;
        extern int gPS5AppsItemCount;
        extern int gPS5AppsGroupCount;
        extern ps5_app_item_t gPS5Apps[PS5_APPS_MAX_ITEMS];
        extern ps5_app_group_t gPS5AppGroups[PS5_APPS_MAX_GROUPS];
        int headerBottom = 56;
        int footerTop = screenHeight - 44;
        int footerY = screenHeight - 20;
        int rowX = 72;
        int rowY = 86;
        int rowStep = 30;
        int rowIndex = 0;
        int selectedRow = 0;
        int scrollOffset = 0;
        int g, i;

        gPS5BgColorR = 0;
        gPS5BgColorG = 0;
        gPS5BgColorB = 0;
        rmDrawRect(0, 0, screenWidth, screenHeight, GS_SETREG_RGBA(0, 0, 0, 0x80));
        rmDrawRect(0, 0, screenWidth, headerBottom, GS_SETREG_RGBA(0, 0, 0, 0x80));
        rmDrawRect(0, footerTop, screenWidth, screenHeight - footerTop, GS_SETREG_RGBA(0, 0, 0, 0x80));
        rmDrawRect(0, headerBottom, screenWidth, 1, GS_SETREG_RGBA(0x38, 0x38, 0x38, 0x40));
        rmDrawRect(0, footerTop, screenWidth, 1, GS_SETREG_RGBA(0x38, 0x38, 0x38, 0x40));

        fntRenderString(gPS5RegFont, gamesX, 20, ALIGN_LEFT, 0, 0, "Games", GS_SETREG_RGBA(0x58, 0x58, 0x58, 0x56));
        fntRenderString(gPS5SemiBoldFont, appsX, 20, ALIGN_LEFT, 0, 0, "Apps", GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
        fntRenderString(gPS5RegFont, settingsX, 20, ALIGN_LEFT, 0, 0, "Settings", GS_SETREG_RGBA(0x58, 0x58, 0x58, 0x56));
        fntRenderString(gPS5SemiBoldFont, l1X, 27, ALIGN_LEFT, 0.70f, 0.70f, "L1", GS_SETREG_RGBA(0x58, 0x58, 0x58, 0x50));
        fntRenderString(gPS5SemiBoldFont, settingsR1X, 27, ALIGN_LEFT, 0.70f, 0.70f, "R1", GS_SETREG_RGBA(0x58, 0x58, 0x58, 0x50));

        for (g = 0; g < gPS5AppsGroupCount; g++) {
            if (gPS5AppGroups[g].count <= 0)
                continue;
            rowIndex++;
            for (i = 0; i < gPS5AppsItemCount; i++) {
                if (gPS5Apps[i].group == g) {
                    if (i == gPS5AppsSelected)
                        selectedRow = rowIndex;
                    rowIndex++;
                }
            }
        }

        if (rowY + selectedRow * rowStep > footerTop - 36)
            scrollOffset = rowY + selectedRow * rowStep - (footerTop - 36);
        else if (rowY + selectedRow * rowStep < headerBottom + 26)
            scrollOffset = rowY + selectedRow * rowStep - (headerBottom + 26);
        rowY -= scrollOffset;

        rowIndex = 0;
        for (g = 0; g < gPS5AppsGroupCount; g++) {
            int y;

            if (gPS5AppGroups[g].count <= 0)
                continue;

            y = rowY + rowIndex * rowStep;
            if (y >= headerBottom + 12 && y <= footerTop - 18)
                fntRenderString(gPS5SemiBoldFont, 50, y, ALIGN_LEFT | ALIGN_VCENTER, 0, 0, gPS5AppGroups[g].title, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
            rowIndex++;

            for (i = 0; i < gPS5AppsItemCount; i++) {
                if (gPS5Apps[i].group == g) {
                    int focused = i == gPS5AppsSelected;
                    char name[PS5_APPS_NAME_MAX];

                    y = rowY + rowIndex * rowStep;
                    if (y >= headerBottom + 12 && y <= footerTop - 18) {
                        strncpy(name, gPS5Apps[i].name, sizeof(name) - 1);
                        name[sizeof(name) - 1] = '\0';
                        fntFitString(focused ? gPS5SemiBoldFont : gPS5RegFont, name, screenWidth - rowX - 64);
                        if (focused)
                            drawPS5FocusPointer(rowX - 22, y);
                        fntRenderString(focused ? gPS5SemiBoldFont : gPS5RegFont, rowX, y, ALIGN_LEFT | ALIGN_VCENTER, 0, 0, name,
                            focused ? GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80) : GS_SETREG_RGBA(0xA0, 0xA0, 0xA0, 0x70));
                    }
                    rowIndex++;
                }
            }
        }

        if (!gPS5AppsLoading && gPS5AppsItemCount <= 0)
            fntRenderString(gPS5BoldFont, 320, 245, ALIGN_CENTER, 0, 0, "NO APPS FOUND", GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x2C));

        drawPS5IconAndText(CROSS_ICON, "Open", gPS5SemiBoldFont, 50, footerY,
            (!gPS5AppsLoading && gPS5AppsItemCount > 0) ? GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80) : GS_SETREG_RGBA(0x58, 0x58, 0x58, 0x50));
        drawPS5IconAndText(SQUARE_ICON, "Refresh", gPS5SemiBoldFont, 150, footerY, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));

        if (gPS5AppsLoading)
            drawPS5DeviceLoadingOverlay();
    } else {
        extern int gPS5SubSel;
        extern int gPS5TempVMode;
        extern int gPS5TempSelectButton;
        extern int gPS5TempUISound;
        extern int gPS5TempShowCoverImages;
        extern int gPS5TempShowGamesLogo;
        extern int gPS5TempSortMode;
        extern int gPS5TempControllerType;
        extern int gPS5SettingsSel;
        extern int gPS5SettingsPage;
        extern int gPS5SmbSettingsSel;
        extern int gPS5ControllerLogVisible;
        extern int gPS5ControllerLogStep;
        extern int gPS5ControllerLogCaptured;
        extern int gPS5ControllerLogNavTestEnabled;
        extern char gPS5ControllerLogDevice[96];
        extern char gPS5ControllerLogLatest[192];
        extern char gPS5ControllerLogPressed[128];
        extern char gPS5ControllerLogBridge[128];
        extern char gPS5ControllerLogStatus[96];
        extern char gPS5ControllerLogLines[20][192];
        extern int gPS5TempEthEnabled;
        extern int gPS5TempSmbAddressType;
        extern int gPS5TempSmbDhcp;
        extern int gPS5TempSmbPort;
        extern int gPS5TempSmbCache;
        extern char gPS5TempSmbIp[16];
        extern char gPS5TempSmbName[17];
        extern char gPS5TempSmbShare[32];
        extern char gPS5TempSmbUser[32];
        extern char gPS5TempSmbPassword[32];
        extern char gPS5TempSmbPrefix[32];
        extern unsigned int gPS5SaveNotifyFrame;
        extern int guiFrameId;

        int rowX = 64;
        int rowY = 88;
        const int rowStep = 42;
        const int cardGap = 16;
        const int cardW = (screenWidth - 96 - cardGap) / 2;
        const int cardH = 104;
        const int listTop = cardH + 34;
        int settingsFocusY = rowY;
        int settingsScrollOffset = 0;

        if (gPS5ControllerLogVisible) {
            const char *steps[] = {
                "IDLE", "DPAD_UP", "DPAD_DOWN", "DPAD_LEFT", "DPAD_RIGHT",
                "A_OR_CROSS", "B_OR_CIRCLE", "X_OR_SQUARE", "Y_OR_TRIANGLE",
                "LB_L1", "RB_R1", "LT_L2", "RT_R2", "VIEW_SELECT",
                "MENU_START", "L3", "R3", "LEFT_STICK_MOVE", "RIGHT_STICK_MOVE", NULL};
            int panelX = 48;
            int panelY = 70;
            int panelW = screenWidth - 96;
            int panelH = screenHeight - 126;
            int footerY = screenHeight - 20;
            char stepText[96];
            char capturedText[64];
            char latestLine[192];
            char pressedLine[128];
            char bridgeLine[128];
            char navLine[64];

            strncpy(latestLine, gPS5ControllerLogLatest, sizeof(latestLine) - 1);
            latestLine[sizeof(latestLine) - 1] = '\0';
            fntFitString(gPS5SmallFont, latestLine, panelW - 48);
            strncpy(pressedLine, gPS5ControllerLogPressed, sizeof(pressedLine) - 1);
            pressedLine[sizeof(pressedLine) - 1] = '\0';
            fntFitString(gPS5SmallFont, pressedLine, panelW - 48);
            strncpy(bridgeLine, gPS5ControllerLogBridge, sizeof(bridgeLine) - 1);
            bridgeLine[sizeof(bridgeLine) - 1] = '\0';
            fntFitString(gPS5SmallFont, bridgeLine, panelW - 48);

            rmDrawRect(0, 0, screenWidth, screenHeight, GS_SETREG_RGBA(0, 0, 0, 0x80));
            fntRenderString(gPS5SemiBoldFont, panelX, 24, ALIGN_LEFT, 0, 0, "Controller Log", GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));

            rmDrawRoundedRect(panelX - 1, panelY - 1, panelW + 2, panelH + 2, 8, GS_SETREG_RGBA(0x30, 0x30, 0x30, 0x80));
            rmDrawRoundedRect(panelX, panelY, panelW, panelH, 7, GS_SETREG_RGBA(0x08, 0x08, 0x08, 0xFA));

            snprintf(stepText, sizeof(stepText), "Step %d: %s", gPS5ControllerLogStep + 1, steps[gPS5ControllerLogStep] ? steps[gPS5ControllerLogStep] : "DONE");
            snprintf(capturedText, sizeof(capturedText), "Captured: %d", gPS5ControllerLogCaptured);
            snprintf(navLine, sizeof(navLine), "Nav Test: %s", gPS5ControllerLogNavTestEnabled ? "ON" : "OFF");

            fntRenderString(gPS5SemiBoldFont, panelX + 24, panelY + 26, ALIGN_LEFT, 0, 0, stepText, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
            fntRenderString(gPS5RegFont, panelX + panelW - 24, panelY + 26, ALIGN_RIGHT, 0, 0, capturedText, GS_SETREG_RGBA(0xA8, 0xA8, 0xA8, 0x70));
            fntRenderString(gPS5SmallFont, panelX + panelW - 24, panelY + 54, ALIGN_RIGHT, 0, 0, navLine, gPS5ControllerLogNavTestEnabled ? GS_SETREG_RGBA(0x90, 0xFF, 0xA8, 0x78) : GS_SETREG_RGBA(0xA8, 0xA8, 0xA8, 0x60));
            fntRenderString(gPS5SmallFont, panelX + 24, panelY + 70, ALIGN_LEFT, 0, 0, gPS5ControllerLogDevice, GS_SETREG_RGBA(0xD0, 0xD0, 0xD0, 0x78));
            fntRenderString(gPS5SmallFont, panelX + 24, panelY + 98, ALIGN_LEFT, 0, 0, pressedLine, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x78));
            fntRenderString(gPS5SmallFont, panelX + 24, panelY + 122, ALIGN_LEFT, 0, 0, bridgeLine, GS_SETREG_RGBA(0xB8, 0xD8, 0xFF, 0x78));
            fntRenderString(gPS5SmallFont, panelX + 24, panelY + 146, ALIGN_LEFT, 0, 0, latestLine, GS_SETREG_RGBA(0x88, 0x88, 0x88, 0x68));
            fntRenderString(gPS5RegFont, panelX + 24, panelY + 170, ALIGN_LEFT, 0, 0, "Hold the requested controller button, then press Cross on the PS2/working controller.", GS_SETREG_RGBA(0xC8, 0xC8, 0xC8, 0x78));
            fntRenderString(gPS5SmallFont, panelX + 24, panelY + 184, ALIGN_LEFT, 0, 0, gPS5ControllerLogStatus, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x78));

            if (gPS5ControllerLogLines[gPS5ControllerLogStep][0]) {
                char savedLine[192];
                strncpy(savedLine, gPS5ControllerLogLines[gPS5ControllerLogStep], sizeof(savedLine) - 1);
                savedLine[sizeof(savedLine) - 1] = '\0';
                fntFitString(gPS5SmallFont, savedLine, panelW - 48);
                fntRenderString(gPS5SmallFont, panelX + 24, panelY + 224, ALIGN_LEFT, 0, 0, savedLine, GS_SETREG_RGBA(0x78, 0x78, 0x78, 0x60));
            }

            drawPS5IconAndText(CROSS_ICON, "Capture", gPS5SemiBoldFont, 50, footerY, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
            drawPS5IconAndText(SQUARE_ICON, "Save", gPS5SemiBoldFont, 170, footerY, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
            drawPS5IconAndText(TRIANGLE_ICON, "Nav Test", gPS5SemiBoldFont, 270, footerY, gPS5ControllerLogNavTestEnabled ? GS_SETREG_RGBA(0x90, 0xFF, 0xA8, 0x80) : GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
            drawPS5RightIconAndText(CIRCLE_ICON, "Close", gPS5SemiBoldFont, screenWidth - 50, footerY, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
            return;
        }

        if (gPS5SettingsPage == 1) {
            int headerBottom = 56;
            int footerTop = screenHeight - 44;
            int smbRowY = 92;
            int smbRowStep = 38;
            int smbFocusY = smbRowY + gPS5SmbSettingsSel * smbRowStep;
            int smbScrollOffset = 0;
            int rightX = screenWidth - 64;
            int footerY = screenHeight - 20;
            int i;

            if (smbFocusY > footerTop - 54)
                smbScrollOffset = smbFocusY - (footerTop - 54);
            else if (smbFocusY < headerBottom + 36)
                smbScrollOffset = smbFocusY - (headerBottom + 36);
            smbRowY -= smbScrollOffset;

            rmDrawRect(0, 0, screenWidth, screenHeight, GS_SETREG_RGBA(0, 0, 0, 0x80));
            rmDrawRect(0, 0, screenWidth, headerBottom, GS_SETREG_RGBA(0, 0, 0, 0x80));
            rmDrawRect(0, footerTop, screenWidth, screenHeight - footerTop, GS_SETREG_RGBA(0, 0, 0, 0x80));
            rmDrawRect(0, headerBottom, screenWidth, 1, GS_SETREG_RGBA(0x38, 0x38, 0x38, 0x40));
            rmDrawRect(0, footerTop, screenWidth, 1, GS_SETREG_RGBA(0x38, 0x38, 0x38, 0x40));

            fntRenderString(gPS5SemiBoldFont, rowX, 20, ALIGN_LEFT, 0, 0, "SMB Settings", GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));

            for (i = 0; i < PS5_SMB_SETTINGS_COUNT; i++) {
                char value[96];
                const char *label = "";
                int y = smbRowY + i * smbRowStep;
                int focused = gPS5SmbSettingsSel == i;

                if (y < headerBottom + 8 || y > footerTop - 24)
                    continue;

                switch (i) {
                    case 0:
                        label = "SMB games";
                        snprintf(value, sizeof(value), "< %s >", gPS5TempEthEnabled ? "On" : "Off");
                        break;
                    case 1:
                        label = "Server IP";
                        snprintf(value, sizeof(value), "%s", gPS5TempSmbIp[0] ? gPS5TempSmbIp : "Not set");
                        break;
                    case 2:
                        label = "Share name";
                        snprintf(value, sizeof(value), "%s", gPS5TempSmbShare[0] ? gPS5TempSmbShare : "Not set");
                        break;
                    case 3:
                        label = "Username";
                        snprintf(value, sizeof(value), "%s", gPS5TempSmbUser[0] ? gPS5TempSmbUser : "Guest");
                        break;
                    case 4:
                        label = "Password";
                        snprintf(value, sizeof(value), "%s", gPS5TempSmbPassword[0] ? "********" : "None");
                        break;
                    case 5:
                        label = "Address type";
                        snprintf(value, sizeof(value), "< %s >", gPS5TempSmbAddressType ? "NetBIOS" : "IP");
                        break;
                    case 6:
                        label = "Server name";
                        snprintf(value, sizeof(value), "%s", gPS5TempSmbName[0] ? gPS5TempSmbName : "Not set");
                        break;
                    case 7:
                        label = "PS2 DHCP";
                        snprintf(value, sizeof(value), "< %s >", gPS5TempSmbDhcp ? "On" : "Off");
                        break;
                    case 8:
                        label = "Port";
                        snprintf(value, sizeof(value), "%d", gPS5TempSmbPort);
                        break;
                    case 9:
                        label = "Prefix";
                        snprintf(value, sizeof(value), "%s", gPS5TempSmbPrefix[0] ? gPS5TempSmbPrefix : "None");
                        break;
                    case 10:
                        label = "SMB cache";
                        snprintf(value, sizeof(value), "%d", gPS5TempSmbCache);
                        break;
                }

                fntFitString(focused ? gPS5SemiBoldFont : gPS5RegFont, value, screenWidth / 2);
                if (focused)
                    drawPS5FocusPointer(rowX - 22, y);
                fntRenderString(focused ? gPS5SemiBoldFont : gPS5RegFont, rowX, y, ALIGN_LEFT | ALIGN_VCENTER, 0, 0, label,
                    focused ? GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80) : GS_SETREG_RGBA(0x58, 0x58, 0x58, 0x56));
                fntRenderString(focused ? gPS5SemiBoldFont : gPS5RegFont, rightX, y, ALIGN_RIGHT | ALIGN_VCENTER, 0, 0, value,
                    focused ? GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80) : GS_SETREG_RGBA(0x58, 0x58, 0x58, 0x56));
            }

            int isToggle = (gPS5SmbSettingsSel == 0 || gPS5SmbSettingsSel == 5 || gPS5SmbSettingsSel == 7);
            u64 modifyColor = isToggle ? GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x20) : GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80);
            drawPS5IconAndText(CROSS_ICON, "Modify", gPS5SemiBoldFont, 50, footerY, modifyColor);

            int nextRightX = drawPS5RightIconAndText(CIRCLE_ICON, "Back", gPS5SemiBoldFont, screenWidth - 50, footerY, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
            int saveLeftX = drawPS5RightIconAndText(SQUARE_ICON, "Save", gPS5SemiBoldFont, nextRightX - 24, footerY, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
            drawPS5RightIconAndText(TRIANGLE_ICON, "Check Connection", gPS5SemiBoldFont, saveLeftX - 24, footerY, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));

            if (gPS5SaveNotifyFrame && guiFrameId >= (int)gPS5SaveNotifyFrame && guiFrameId - (int)gPS5SaveNotifyFrame < 120) {
                int toastW = 188;
                int toastH = 38;
                int toastX = (screenWidth - toastW) / 2;
                int toastY = 386;
                rmDrawRoundedRect(toastX - 1, toastY - 1, toastW + 2, toastH + 2, 8, GS_SETREG_RGBA(0x30, 0x30, 0x30, 0x80));
                rmDrawRoundedRect(toastX, toastY, toastW, toastH, 7, GS_SETREG_RGBA(0x08, 0x08, 0x08, 0xFA));
                fntRenderString(gPS5SemiBoldFont, toastX + toastW / 2, toastY + toastH / 2, ALIGN_CENTER | ALIGN_VCENTER, 0, 0, "Save Successful", GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
            } else if (gPS5SaveNotifyFrame && guiFrameId - (int)gPS5SaveNotifyFrame >= 120) {
                gPS5SaveNotifyFrame = 0;
            }

            drawPS5SaveBusyOverlay();
            drawPS5SmbDialogOverlay();
            drawPS5SmbCheckDialogOverlay();
            return;
        }

        if (gPS5SubSel == 7 || gPS5SubSel == 8)
            settingsFocusY = rowY + (cardH / 2);
        else
            settingsFocusY = rowY + listTop + (gPS5SubSel * rowStep);

        if (settingsFocusY > 360)
            settingsScrollOffset = settingsFocusY - 360;
        else if (settingsFocusY < 92)
            settingsScrollOffset = settingsFocusY - 92;
        rowY -= settingsScrollOffset;

        // 1. Draw Resolution label and bracketed value
        const char *resText = "Standard";
        if (gPS5TempVMode == 3) resText = "Progressive 480p";
        else if (gPS5TempVMode == 10) resText = "720p";
        else if (gPS5TempVMode == 11) resText = "1080i";

        char valStr[64];
        snprintf(valStr, sizeof(valStr), "< %s >", resText);

        int rightX = screenWidth - 64;

        drawPS5SettingsText(gPS5RegFont, rowX, rowY + listTop, ALIGN_LEFT, "Resolution", gPS5SubSel == 0);
        drawPS5SettingsText(gPS5RegFont, rightX, rowY + listTop, ALIGN_RIGHT, valStr, gPS5SubSel == 0);

        char selectButtonStr[64];
        snprintf(selectButtonStr, sizeof(selectButtonStr), "< %s >", gPS5TempSelectButton == KEY_CROSS ? "Cross" : "Circle");
        drawPS5SettingsText(gPS5RegFont, rowX, rowY + listTop + rowStep, ALIGN_LEFT, "Select button", gPS5SubSel == 1);
        drawPS5SettingsText(gPS5RegFont, rightX, rowY + listTop + rowStep, ALIGN_RIGHT, selectButtonStr, gPS5SubSel == 1);

        // 2. Draw UI Sound label and bracketed value
        char uiSoundStr[64];
        snprintf(uiSoundStr, sizeof(uiSoundStr), "< %s >", gPS5TempUISound ? "On" : "Off");

        drawPS5SettingsText(gPS5RegFont, rowX, rowY + listTop + (rowStep * 2), ALIGN_LEFT, "UI Sound", gPS5SubSel == 2);
        drawPS5SettingsText(gPS5RegFont, rightX, rowY + listTop + (rowStep * 2), ALIGN_RIGHT, uiSoundStr, gPS5SubSel == 2);

        char showCoverStr[64];
        snprintf(showCoverStr, sizeof(showCoverStr), "< %s >", gPS5TempShowCoverImages ? "On" : "Off");
        drawPS5SettingsText(gPS5RegFont, rowX, rowY + listTop + (rowStep * 3), ALIGN_LEFT, "Show cover images", gPS5SubSel == 3);
        drawPS5SettingsText(gPS5RegFont, rightX, rowY + listTop + (rowStep * 3), ALIGN_RIGHT, showCoverStr, gPS5SubSel == 3);

        char showLogoStr[64];
        snprintf(showLogoStr, sizeof(showLogoStr), "< %s >", gPS5TempShowGamesLogo ? "On" : "Off");
        drawPS5SettingsText(gPS5RegFont, rowX, rowY + listTop + (rowStep * 4), ALIGN_LEFT, "Show games logo", gPS5SubSel == 4);
        drawPS5SettingsText(gPS5RegFont, rightX, rowY + listTop + (rowStep * 4), ALIGN_RIGHT, showLogoStr, gPS5SubSel == 4);

        char sortStr[96];
        snprintf(sortStr, sizeof(sortStr), "< %s >", gPS5TempSortMode ? "Available games" : "Each letter");
        drawPS5SettingsText(gPS5RegFont, rowX, rowY + listTop + (rowStep * 5), ALIGN_LEFT, "Sorting games", gPS5SubSel == 5);
        drawPS5SettingsText(gPS5RegFont, rightX, rowY + listTop + (rowStep * 5), ALIGN_RIGHT, sortStr, gPS5SubSel == 5);

        char controllerStr[96];
        const char *controllerText = "PS2 DualShock 2";
        if (gPS5TempControllerType == 1)
            controllerText = "PS3/PS4 USB";
        else if (gPS5TempControllerType == 2)
            controllerText = "PS3/PS4 Bluetooth";
        else if (gPS5TempControllerType == 3)
            controllerText = "Xbox USB";
        snprintf(controllerStr, sizeof(controllerStr), "< %s >", controllerText);
        drawPS5SettingsText(gPS5RegFont, rowX, rowY + listTop + (rowStep * 6), ALIGN_LEFT, "Controller", gPS5SubSel == 6);
        drawPS5SettingsText(gPS5RegFont, rightX, rowY + listTop + (rowStep * 6), ALIGN_RIGHT, controllerStr, gPS5SubSel == 6);

        // 4. Draw quick action cards.
        char coverSummary[96];
        char smbSummary[96];
        char coverButton[32];
        int coverFocused = gPS5SubSel == 7;
        int smbFocused = gPS5SubSel == 8;
        u64 idleCardColor = GS_SETREG_RGBA(0x1C, 0x1C, 0x1C, 0x80);
        u64 focusedCardColor = GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80);
        u64 idleHeadingColor = GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80);
        u64 idleSubColor = GS_SETREG_RGBA(0xA0, 0xA0, 0xA0, 0x72);
        u64 focusedTextColor = GS_SETREG_RGBA(0x00, 0x00, 0x00, 0x80);
        u64 idleButtonColor = GS_SETREG_RGBA(0x30, 0x30, 0x30, 0x80);
        u64 focusedButtonColor = GS_SETREG_RGBA(0x00, 0x00, 0x00, 0x80);
        u64 buttonTextColor = GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80);

        snprintf(coverSummary, sizeof(coverSummary), "Missing covers? Download now.");
        snprintf(smbSummary, sizeof(smbSummary), "Connect network shared games.");
        if (gPS5CoverDownloadStatus == PS5_COVER_DOWNLOAD_WIP)
            snprintf(coverButton, sizeof(coverButton), "Downloading");
        else
            snprintf(coverButton, sizeof(coverButton), "Download");

        int cardX = rowX - 16;
        int smbCardX = cardX + cardW + cardGap;
        int cardY = rowY;
        int cardRadius = 7;
        
        rmDrawRoundedRect(cardX, cardY, cardW, cardH, cardRadius, coverFocused ? focusedCardColor : idleCardColor);
        fntRenderString(gPS5SemiBoldFont, cardX + 18, cardY + 14, ALIGN_LEFT, 0, 0, "Game Covers", coverFocused ? focusedTextColor : idleHeadingColor);
        fntRenderString(gPS5RegFont, cardX + 18, cardY + 34, ALIGN_LEFT, 0.72f, 0.72f, coverSummary, coverFocused ? focusedTextColor : idleSubColor);
        {
            int btnW = (gPS5CoverDownloadStatus == PS5_COVER_DOWNLOAD_WIP) ? 104 : 82;
            int btnH = 28;
            int btnX = cardX + 18;
            int btnY = cardY + 66;
            rmDrawRoundedRect(btnX, btnY, btnW, btnH, 5, coverFocused ? focusedButtonColor : idleButtonColor);
            fntRenderString(gPS5RegFont, btnX + (btnW / 2), btnY + 5, ALIGN_HCENTER, 0, 0, coverButton, buttonTextColor);
        }

        rmDrawRoundedRect(smbCardX, cardY, cardW, cardH, cardRadius, smbFocused ? focusedCardColor : idleCardColor);
        fntRenderString(gPS5SemiBoldFont, smbCardX + 18, cardY + 14, ALIGN_LEFT, 0, 0, "SMB Settings", smbFocused ? focusedTextColor : idleHeadingColor);
        fntRenderString(gPS5RegFont, smbCardX + 18, cardY + 34, ALIGN_LEFT, 0.72f, 0.72f, smbSummary, smbFocused ? focusedTextColor : idleSubColor);
        {
            const char *smbState = gPS5TempEthEnabled ? "On" : "Off";
            int smbBtnW = 82;
            int smbBtnH = 28;
            int smbBtnX = smbCardX + 18;
            int smbBtnY = cardY + 66;
            rmDrawRoundedRect(smbBtnX, smbBtnY, smbBtnW, smbBtnH, 5, smbFocused ? focusedButtonColor : idleButtonColor);
            fntRenderString(gPS5RegFont, smbBtnX + (smbBtnW / 2), smbBtnY + 5, ALIGN_HCENTER, 0, 0, smbState, buttonTextColor);
        }

        {
            int settingsHeaderBottom = 56;
            int settingsFooterTop = screenHeight - 44;
            rmDrawRect(0, 0, screenWidth, settingsHeaderBottom, GS_SETREG_RGBA(0, 0, 0, 0x80));
            rmDrawRect(0, settingsFooterTop, screenWidth, screenHeight - settingsFooterTop, GS_SETREG_RGBA(0, 0, 0, 0x80));
            rmDrawRect(0, settingsHeaderBottom, screenWidth, 1, GS_SETREG_RGBA(0x38, 0x38, 0x38, 0x40));
            rmDrawRect(0, settingsFooterTop, screenWidth, 1, GS_SETREG_RGBA(0x38, 0x38, 0x38, 0x40));
        }

        fntRenderString(gPS5RegFont, gamesX, 20, ALIGN_LEFT, 0, 0, "Games", GS_SETREG_RGBA(0x58, 0x58, 0x58, 0x56));
        fntRenderString(gPS5RegFont, appsX, 20, ALIGN_LEFT, 0, 0, "Apps", GS_SETREG_RGBA(0x58, 0x58, 0x58, 0x56));
        fntRenderString(gPS5SemiBoldFont, settingsX, 20, ALIGN_LEFT, 0, 0, "Settings", GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
        fntRenderString(gPS5SemiBoldFont, l1X, 27, ALIGN_LEFT, 0.70f, 0.70f, "L1", GS_SETREG_RGBA(0x58, 0x58, 0x58, 0x50));
        fntRenderString(gPS5SemiBoldFont, settingsR1X, 27, ALIGN_LEFT, 0.70f, 0.70f, "R1", GS_SETREG_RGBA(0x58, 0x58, 0x58, 0x50));

        // 4. Draw irfanmatheena in lowercase with Instagram icon at bottom-left (aligned vertically to Save center)
        extern GSTEXTURE gPS5InstagramTex;
        extern int gPS5InstagramTexLoaded;
        extern void *Instagram_icon_png;
        int footerY = screenHeight - 20;

        if (!gPS5InstagramTexLoaded) {
            memset(&gPS5InstagramTex, 0, sizeof(GSTEXTURE));
            if (texLoadMem(&gPS5InstagramTex, &Instagram_icon_png) >= 0) {
                gPS5InstagramTexLoaded = 1;
            } else {
                LOG("Failed to load Instagram icon from memory\n");
            }
        }

        int textStartX = 50;
        if (gPS5InstagramTexLoaded) {
            // Draw Instagram icon (16x16, vertically aligned to the footer baseline) with full opacity
            rmDrawPixmap(&gPS5InstagramTex, 50, footerY, ALIGN_LEFT | ALIGN_VCENTER, 16, 16, 1, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
            textStartX = 50 + 22;
        }

        fntRenderString(gPS5SemiBoldFont, textStartX, footerY, ALIGN_LEFT | ALIGN_VCENTER, 0, 0, "irfanmatheena", GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));

        // 5. Draw footer action helpers as measured groups, so icons and text cannot overlap.
        int saveGroupX = drawPS5RightIconAndText(SQUARE_ICON, "Save", gPS5SemiBoldFont, screenWidth - 50, footerY, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));

        if (gPS5SubSel == 0) { // Resolution focused
            u64 applyColor = (gVMode != gPS5TempVMode) ? GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80) : GS_SETREG_RGBA(0x58, 0x58, 0x58, 0x50);
            drawPS5RightIconAndText(CROSS_ICON, "Apply", gPS5SemiBoldFont, saveGroupX - 24, footerY, applyColor);
        }

        if (gPS5SaveNotifyFrame && guiFrameId >= (int)gPS5SaveNotifyFrame && guiFrameId - (int)gPS5SaveNotifyFrame < 120) {
            int toastW = 188;
            int toastH = 38;
            int toastX = (screenWidth - toastW) / 2;
            int toastY = 386;
            rmDrawRoundedRect(toastX - 1, toastY - 1, toastW + 2, toastH + 2, 8, GS_SETREG_RGBA(0x30, 0x30, 0x30, 0x80));
            rmDrawRoundedRect(toastX, toastY, toastW, toastH, 7, GS_SETREG_RGBA(0x08, 0x08, 0x08, 0xFA));
            fntRenderString(gPS5SemiBoldFont, toastX + toastW / 2, toastY + toastH / 2, ALIGN_CENTER | ALIGN_VCENTER, 0, 0, "Save Successful", GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
        } else if (gPS5SaveNotifyFrame && guiFrameId - (int)gPS5SaveNotifyFrame >= 120) {
            gPS5SaveNotifyFrame = 0;
        }

        drawPS5SaveBusyOverlay();

        // Draw cover download progress dialog if active
        extern int gPS5CoverDownloadStatus;
        if (gPS5CoverDownloadStatus != PS5_COVER_DOWNLOAD_IDLE) {
            extern int gPS5CoverDownloadPercent;
            extern int gPS5CoverDownloadCurrent;
            extern int gPS5CoverDownloadTotal;
            extern int gPS5CoverDownloadMode;
            extern char gPS5CoverDownloadTitle[96];
            extern char gPS5CoverDownloadUrl[768];

            int dlgW = 440;
            int dlgH = 260;
            int dlgX = (screenWidth - dlgW) / 2;
            int dlgY = (screenHeight - dlgH) / 2;

            // 1. Dark semi-transparent full screen overlay to dim background
            rmDrawRect(0, 0, screenWidth, screenHeight, GS_SETREG_RGBA(0, 0, 0, 0x60));

            // 2. Glassmorphic rounded modal container
            // Outer light border stroke for depth
            rmDrawRoundedRect(dlgX - 1, dlgY - 1, dlgW + 2, dlgH + 2, 8, GS_SETREG_RGBA(0x30, 0x30, 0x30, 0x80));
            // Container background
            rmDrawRoundedRect(dlgX, dlgY, dlgW, dlgH, 7, GS_SETREG_RGBA(0x08, 0x08, 0x08, 0xFA));

            // 3. Header title based on download status
            const char *headerTitleText = "Download Covers";
            fntRenderString(gPS5TitleFont, dlgX + 24, dlgY + 24, ALIGN_LEFT, 0, 0, headerTitleText, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));

            if (gPS5CoverDownloadStatus == PS5_COVER_DOWNLOAD_PROMPT) {
                int listX = dlgX + 74;
                int pointerX = dlgX + 42;
                int firstY = dlgY + 110;
                int rowStep = 26;
                int missingFocused = gPS5CoverDownloadMode == PS5_COVER_DOWNLOAD_MISSING;
                u64 focusedColor = GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80);
                u64 idleColor = GS_SETREG_RGBA(0x88, 0x88, 0x88, 0x68);

                drawPS5FocusPointer(pointerX, missingFocused ? firstY + rowStep : firstY);
                fntRenderString(!missingFocused ? gPS5SemiBoldFont : gPS5RegFont, listX, firstY, ALIGN_LEFT | ALIGN_VCENTER, 0, 0, "Full games", !missingFocused ? focusedColor : idleColor);
                fntRenderString(missingFocused ? gPS5SemiBoldFont : gPS5RegFont, listX, firstY + rowStep, ALIGN_LEFT | ALIGN_VCENTER, 0, 0, "Missing games", missingFocused ? focusedColor : idleColor);

                drawPS5RightIconAndText(CIRCLE_ICON, "Cancel", gPS5SemiBoldFont, dlgX + dlgW - 24, dlgY + dlgH - 32, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x78));
                drawPS5RightIconAndText(CROSS_ICON, "Start", gPS5SemiBoldFont, dlgX + dlgW - 132, dlgY + dlgH - 32, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x78));
                return;
            }

            // 4. Active title & multiline status
            if (gPS5CoverDownloadStatus == PS5_COVER_DOWNLOAD_WIP) {
                fntRenderString(gPS5RegFont, dlgX + 24, dlgY + 62, ALIGN_LEFT, 0, 0, gPS5CoverDownloadTitle, GS_SETREG_RGBA(0xEE, 0xEE, 0xEE, 0x80));
            } else {
                fntRenderString(gPS5RegFont, dlgX + 24, dlgY + 62, ALIGN_LEFT, 0, 0, gPS5CoverDownloadTitle, GS_SETREG_RGBA(0xEE, 0xEE, 0xEE, 0x80));
            }

            {
                char statusText[768];
                char line[160];
                char *lineStart;
                char *lineEnd;
                int lineIndex = 0;
                int lineY = dlgY + 88;

                strncpy(statusText, gPS5CoverDownloadUrl, sizeof(statusText) - 1);
                statusText[sizeof(statusText) - 1] = '\0';
                lineStart = statusText;

                while (lineStart && *lineStart && lineIndex < 7) {
                    lineEnd = strchr(lineStart, '\n');
                    if (lineEnd)
                        *lineEnd = '\0';

                    strncpy(line, lineStart, sizeof(line) - 1);
                    line[sizeof(line) - 1] = '\0';
                    fntFitString(gPS5SmallFont, line, dlgW - 48);
                    fntRenderString(gPS5SmallFont, dlgX + 24, lineY + lineIndex * 18, ALIGN_LEFT, 0, 0, line, GS_SETREG_RGBA(0x68, 0x70, 0x78, 0x80));

                    if (!lineEnd)
                        break;
                    lineStart = lineEnd + 1;
                    lineIndex++;
                }
            }

            // 5. Progress bar track and fill
            rmDrawRect(dlgX + 24, dlgY + 204, dlgW - 48, 6, GS_SETREG_RGBA(0x33, 0x33, 0x33, 0x80));
            int barW = ((dlgW - 48) * gPS5CoverDownloadPercent) / 100;
            if (barW > (dlgW - 48)) barW = dlgW - 48;
            if (barW > 0) {
                rmDrawRect(dlgX + 24, dlgY + 204, barW, 6, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
            }

            // Percentage / current count label
            char progressText[64];
            if (gPS5CoverDownloadStatus == PS5_COVER_DOWNLOAD_WIP) {
                snprintf(progressText, sizeof(progressText), "%d/%d Games  %d%%", gPS5CoverDownloadCurrent, gPS5CoverDownloadTotal, gPS5CoverDownloadPercent);
            } else {
                snprintf(progressText, sizeof(progressText), "%d%%", gPS5CoverDownloadPercent);
            }
            fntRenderString(gPS5SmallFont, dlgX + dlgW - 24, dlgY + 190, ALIGN_RIGHT, 0, 0, progressText, GS_SETREG_RGBA(0x58, 0x58, 0x58, 0x60));

            // 6. Action helper button hints at the bottom
            int btnY = dlgY + dlgH - 32;
            int btnRight = dlgX + dlgW - 24;
            u64 btnColor = GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x68);
            if (gPS5CoverDownloadStatus == PS5_COVER_DOWNLOAD_WIP) {
                drawPS5RightIconAndText(CIRCLE_ICON, "Cancel", gPS5SemiBoldFont, btnRight, btnY, btnColor);
            } else {
                drawPS5RightIconAndText(CIRCLE_ICON, "Close", gPS5SemiBoldFont, btnRight, btnY, btnColor);
            }
        }

        drawPS5SmbDialogOverlay();
        drawPS5SmbCheckDialogOverlay();
    }
}

static void drawItemsList(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    if (gPS5Mode) {
        drawPS5Launcher(menu, item, elem);
        return;
    }

    if (item) {

        items_list_t *itemsList = (items_list_t *)elem->extended;

        int posX = elem->posX, posY = elem->posY;
        if (elem->aligned) {
            posX -= elem->width >> 1;
            posY -= elem->height >> 1;
        }

        submenu_list_t *ps = menu->item->pagestart;
        int others = 0;
        u64 color;
        while (ps && (others++ < itemsList->displayedItems)) {
            if (ps == item)
                color = gTheme->selTextColor;
            else
                color = elem->color;

            if (itemsList->decoratorImage) {
                GSTEXTURE *itemIconTex = getGameImageTexture(itemsList->decoratorImage->cache, menu->item->userdata, &ps->item);
                if (itemIconTex && itemIconTex->Mem)
                    rmDrawPixmap(itemIconTex, posX, posY, elem->aligned, DECORATOR_SIZE, DECORATOR_SIZE, elem->scaled, gDefaultCol);
                else {
                    if (itemsList->decoratorImage->defaultTexture)
                        rmDrawPixmap(&itemsList->decoratorImage->defaultTexture->source, posX, posY, elem->aligned, DECORATOR_SIZE, DECORATOR_SIZE, elem->scaled, gDefaultCol);
                }
                fntRenderString(elem->font, elem->posX + DECORATOR_SIZE, posY, elem->aligned, elem->width, elem->height, submenuItemGetText(&ps->item), color);
            } else
                fntRenderString(elem->font, elem->posX, posY, elem->aligned, elem->width, elem->height, submenuItemGetText(&ps->item), color);

            posY += MENU_ITEM_HEIGHT;
            ps = ps->next;
        }
    }
}

static void initItemsList(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name, const char *decorator)
{
    char elemProp[64];

    items_list_t *itemsList = (items_list_t *)malloc(sizeof(items_list_t));

    if (elem->width == DIM_UNDEF)
        elem->width = screenWidth;

    if (elem->height == DIM_UNDEF)
        elem->height = theme->usedHeight - (MENU_POS_V + HINT_HEIGHT);

    itemsList->displayedItems = elem->height / MENU_ITEM_HEIGHT;
    LOG("THEMES ItemsList %s: displaying %d elems, item height: %d\n", name, itemsList->displayedItems, elem->height);

    itemsList->decorator = NULL;
    snprintf(elemProp, sizeof(elemProp), "%s_decorator", name);
    configGetStr(themeConfig, elemProp, &decorator);
    if (decorator)
        itemsList->decorator = decorator; // Will be used later (thmValidate)

    itemsList->decoratorImage = NULL;

    elem->extended = itemsList;
    // elem->endElem = &endBasic; does the job

    elem->drawElem = &drawItemsList;
}

static void drawItemText(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    if (item) {
        int sourceId;
        item_list_t *support = resolveThemeGameItem(menu->item->userdata, item->item.id, &sourceId);
        if (support != NULL)
            fntRenderString(elem->font, elem->posX, elem->posY, elem->aligned, 0, 0, support->itemGetStartup(support, sourceId), elem->color);
    }
}

static void drawHintText(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    if (gPS5Mode)
        return;

    menu_hint_item_t *hint = menu->item->hints;
    if (hint) {
        int x = elem->posX;

        if (elem->aligned)
            x = guiAlignMenuHints(hint, elem->font, elem->width);

        for (; hint; hint = hint->next) {
            x = guiDrawIconAndText(hint->icon_id, hint->text_id, elem->font, x, elem->posY, elem->color);
            x += elem->width;
        }
    }
}

static void drawInfoHintText(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    int infoHints[2] = {_STR_RUN, _STR_BACK};
    int infoIcons[2] = {CIRCLE_ICON, CROSS_ICON};
    int x = elem->posX;

    if (elem->aligned)
        x = guiAlignSubMenuHints(2, infoHints, infoIcons, elem->font, elem->width, 1);

    x = guiDrawIconAndText(gSelectButton == KEY_CIRCLE ? infoIcons[0] : infoIcons[1], infoHints[0], elem->font, x, elem->posY, elem->color);
    x += elem->width;
    x = guiDrawIconAndText(gSelectButton == KEY_CIRCLE ? infoIcons[1] : infoIcons[0], infoHints[1], elem->font, x, elem->posY, elem->color);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void validateBackgroundElems(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_elems_t *mainElems, theme_elems_t *infoElems)
{
    if (!mainElems->first || (mainElems->first->type != ELEM_TYPE_BACKGROUND)) {
        LOG("THEMES No valid background found for main, add default BG_ART\n");
        theme_element_t *backgroundElem = initBasic(themePath, themeConfig, theme, "bg", ELEM_TYPE_BACKGROUND, 0, 0, ALIGN_NONE, screenWidth, screenHeight, SCALING_NONE, gDefaultCol, theme->fonts[0]);
        initBackground(themePath, themeConfig, theme, backgroundElem, "bg", "BG", 1, NULL);
        backgroundElem->next = mainElems->first;
        mainElems->first = backgroundElem;
    }

    if (infoElems->first) {
        if (infoElems->first->type != ELEM_TYPE_BACKGROUND) {
            LOG("THEMES No valid background found for info, add default BG_ART\n");
            theme_element_t *backgroundElem = initBasic(themePath, themeConfig, theme, "bg", ELEM_TYPE_BACKGROUND, 0, 0, ALIGN_NONE, screenWidth, screenHeight, SCALING_NONE, gDefaultCol, theme->fonts[0]);
            initBackground(themePath, themeConfig, theme, backgroundElem, "bg", "BG", 1, NULL);
            backgroundElem->next = infoElems->first;
            infoElems->first = backgroundElem;
        }
    }
}

static void validateItemsList(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *list, theme_elems_t *mainElems)
{
    if (list) {
        items_list_t *itemsList = (items_list_t *)list->extended;
        if (itemsList->decorator) {
            // Second pass to find the decorator
            theme_element_t *decoratorElem = mainElems->first;
            while (decoratorElem) {
                if (decoratorElem->type == ELEM_TYPE_GAME_IMAGE) {
                    mutable_image_t *gameImage = (mutable_image_t *)decoratorElem->extended;
                    if (!strcmp(itemsList->decorator, gameImage->cache->suffix)) {
                        // if user want to cache less than displayed items, then disable itemslist icons, if not would load constantly
                        if (gameImage->cache->count >= itemsList->displayedItems)
                            itemsList->decoratorImage = gameImage;
                        break;
                    }
                }

                decoratorElem = decoratorElem->next;
            }
            itemsList->decorator = NULL;
        }
        if (!itemsList->decoratorImage) {
            theme_element_t *decoratorElem = mainElems->first;
            while (decoratorElem) {
                if (decoratorElem->type == ELEM_TYPE_GAME_IMAGE) {
                    mutable_image_t *gameImage = (mutable_image_t *)decoratorElem->extended;
                    if (gameImage->cache && (!strcmp(gameImage->cache->suffix, "COV") || !strcmp(gameImage->cache->suffix, "ICO"))) {
                        itemsList->decoratorImage = gameImage;
                        break;
                    }
                }
                decoratorElem = decoratorElem->next;
            }
        }
    } else {
        LOG("THEMES No itemsList found, adding a default one\n");
        list = initBasic(themePath, themeConfig, theme, "il", ELEM_TYPE_ITEMS_LIST, 42, 42, ALIGN_NONE, 373, 316, SCALING_RATIO, theme->textColor, theme->fonts[0]);
        initItemsList(themePath, themeConfig, theme, list, "il", NULL);
        list->next = mainElems->first->next; // Position the itemsList as second element (right after the Background)
        mainElems->first->next = list;
    }
}

static void validateGUIElems(const char *themePath, config_set_t *themeConfig, theme_t *theme)
{
    // 1. check we have a valid Background elements
    validateBackgroundElems(themePath, themeConfig, theme, &theme->mainElems, &theme->infoElems);
    validateBackgroundElems(themePath, themeConfig, theme, &theme->appsMainElems, &theme->appsInfoElems);

    // 2. check we have a valid ItemsList element, and link its decorator to the target element
    validateItemsList(themePath, themeConfig, theme, theme->gamesItemsList, &theme->mainElems);
    validateItemsList(themePath, themeConfig, theme, theme->appsItemsList, &theme->appsMainElems);
}

static int addGUIElem(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_elems_t *elems, const char *type, const char *name)
{
    int enabled = 1;
    char elemProp[64];
    theme_element_t *elem = NULL;

    snprintf(elemProp, sizeof(elemProp), "%s_enabled", name);
    configGetInt(themeConfig, elemProp, &enabled);

    if (enabled) {
        snprintf(elemProp, sizeof(elemProp), "%s_type", name);
        configGetStr(themeConfig, elemProp, &type);
        if (type) {
            if (!strcmp(elementsType[ELEM_TYPE_ATTRIBUTE_TEXT], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_ATTRIBUTE_TEXT, 0, 0, ALIGN_CENTER, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, theme->textColor, theme->fonts[0]);
                initAttributeText(themePath, themeConfig, theme, elem, name);
            } else if (!strcmp(elementsType[ELEM_TYPE_STATIC_TEXT], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_STATIC_TEXT, 0, 0, ALIGN_CENTER, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, theme->textColor, theme->fonts[0]);
                initStaticText(themePath, themeConfig, theme, elem, name);
            } else if (!strcmp(elementsType[ELEM_TYPE_GAME_COUNT_TEXT], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_STATIC_TEXT, 0, 0, ALIGN_CENTER, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, theme->textColor, theme->fonts[0]);
                initGameCountText(themePath, themeConfig, theme, elem, name);
            } else if (!strcmp(elementsType[ELEM_TYPE_ATTRIBUTE_IMAGE], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_ATTRIBUTE_IMAGE, 0, 0, ALIGN_CENTER, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, gDefaultCol, theme->fonts[0]);
                initAttributeImage(themePath, themeConfig, theme, elem, name);
            } else if (!strcmp(elementsType[ELEM_TYPE_GAME_IMAGE], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_GAME_IMAGE, 0, 0, ALIGN_CENTER, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, gDefaultCol, theme->fonts[0]);
                initGameImage(themePath, themeConfig, theme, elem, name, NULL, 1, NULL, NULL);
            } else if (!strcmp(elementsType[ELEM_TYPE_STATIC_IMAGE], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_STATIC_IMAGE, 0, 0, ALIGN_CENTER, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, gDefaultCol, theme->fonts[0]);
                initStaticImage(themePath, themeConfig, theme, elem, name, NULL);
            } else if (!strcmp(elementsType[ELEM_TYPE_BACKGROUND], type)) {
                if (!elems->first) { // Background elem can only be the first one
                    elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_BACKGROUND, 0, 0, ALIGN_NONE, screenWidth, screenHeight, SCALING_NONE, gDefaultCol, theme->fonts[0]);
                    initBackground(themePath, themeConfig, theme, elem, name, NULL, 1, NULL);
                }
            } else if (!strcmp(elementsType[ELEM_TYPE_MENU_ICON], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_MENU_ICON, screenWidth >> 1, 400, ALIGN_CENTER, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, gDefaultCol, theme->fonts[0]);
                elem->drawElem = &drawMenuIcon;
            } else if (!strcmp(elementsType[ELEM_TYPE_MENU_TEXT], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_MENU_TEXT, screenWidth >> 1, 20, ALIGN_CENTER, 200, 20, SCALING_RATIO, theme->textColor, theme->fonts[0]);
                elem->drawElem = &drawMenuText;
            } else if (!strcmp(elementsType[ELEM_TYPE_ITEMS_LIST], type)) {
                if (!theme->gamesItemsList) {
                    elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_ITEMS_LIST, 0, 0, ALIGN_NONE, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, theme->textColor, theme->fonts[0]);
                    initItemsList(themePath, themeConfig, theme, elem, name, NULL);
                    theme->gamesItemsList = elem;
                } else if (!theme->appsItemsList) {
                    elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_ITEMS_LIST, 42, 42, ALIGN_NONE, 400, 360, SCALING_RATIO, theme->textColor, theme->fonts[0]);
                    initItemsList(themePath, themeConfig, theme, elem, name, NULL);
                    theme->appsItemsList = elem;
                }
            } else if (!strcmp(elementsType[ELEM_TYPE_ITEM_ICON], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_GAME_IMAGE, 0, 0, ALIGN_CENTER, 64, 64, SCALING_RATIO, gDefaultCol, theme->fonts[0]);
                initGameImage(themePath, themeConfig, theme, elem, name, "ICO", 20, NULL, NULL);
            } else if (!strcmp(elementsType[ELEM_TYPE_ITEM_COVER], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_GAME_IMAGE, 0, 0, ALIGN_CENTER, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, gDefaultCol, theme->fonts[0]);
                initGameImage(themePath, themeConfig, theme, elem, name, "COV", 10, NULL, NULL);
            } else if (!strcmp(elementsType[ELEM_TYPE_ITEM_TEXT], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_ITEM_TEXT, 0, 0, ALIGN_CENTER, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, theme->textColor, theme->fonts[0]);
                elem->drawElem = &drawItemText;
            } else if (!strcmp(elementsType[ELEM_TYPE_HINT_TEXT], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_HINT_TEXT, 16, -HINT_HEIGHT, ALIGN_NONE, 12, 20, SCALING_RATIO, theme->textColor, theme->fonts[0]);
                elem->drawElem = &drawHintText;
            } else if (!strcmp(elementsType[ELEM_TYPE_INFO_HINT_TEXT], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_INFO_HINT_TEXT, 16, -HINT_HEIGHT, ALIGN_NONE, 12, 20, SCALING_RATIO, theme->textColor, theme->fonts[0]);
                elem->drawElem = &drawInfoHintText;
            } else if (!strcmp(elementsType[ELEM_TYPE_LOADING_ICON], type)) {
                if (!theme->loadingIcon)
                    theme->loadingIcon = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_LOADING_ICON, -40, -60, ALIGN_CENTER, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, gDefaultCol, theme->fonts[0]);
            } else if (!strcmp(elementsType[ELEM_TYPE_BDM_INDEX], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_BDM_INDEX, screenWidth >> 1, 355, ALIGN_CENTER, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, gDefaultCol, theme->fonts[0]);
                elem->drawElem = &drawBDMIndex;
            }

            if (elem) {
                if (!elems->first)
                    elems->first = elem;

                if (!elems->last)
                    elems->last = elem;
                else {
                    elems->last->next = elem;
                    elems->last = elem;
                }
            }
        } else
            return 0; // ends the reading of elements
    }

    return 1;
}

static void freeGUIElems(theme_elems_t *elems)
{
    theme_element_t *elem = elems->first;
    while (elem) {
        elems->first = elem->next;
        elem->endElem(elem);
        elem = elems->first;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

GSTEXTURE *thmGetTexture(unsigned int id)
{
    if (id >= TEXTURES_COUNT)
        return NULL;
    else {
        // see if the texture is valid
        GSTEXTURE *txt = &gTheme->textures[id];

        if (txt->Mem)
            return txt;
        else
            return NULL;
    }
}

static void thmFree(theme_t *theme)
{
    if (theme) {
        int i;
        for (i = 0; i < gNetCacheCount; i++) {
            unloadPS5CoverTexture(&gNetCache[i]);
            unloadPS5LogoTexture(&gNetCache[i]);
        }
        gNetCacheCount = 0;
        // free elements
        freeGUIElems(&theme->mainElems);
        freeGUIElems(&theme->infoElems);
        freeGUIElems(&theme->appsMainElems);
        freeGUIElems(&theme->appsInfoElems);

        // free textures
        GSTEXTURE *texture;
        int id = 0;
        for (; id < TEXTURES_COUNT; id++) {
            texture = &theme->textures[id];
            if (texture->Mem != NULL) {
                rmUnloadTexture(texture);
                texFree(texture);
            }
        }

        // free fonts
        for (id = 0; id < THM_MAX_FONTS; ++id)
            fntRelease(theme->fonts[id]);

        free(theme);
    }
}

static int thmReadEntry(int index, const char *path, const char *separator, const char *name, unsigned char d_type)
{
    if (d_type == DT_DIR && strstr(name, "thm_")) {
        theme_file_t *currTheme = &themes[nThemes + index];

        int length = strlen(name) - 4 + 1;
        currTheme->name = (char *)malloc(length * sizeof(char));
        memcpy(currTheme->name, name + 4, length);
        currTheme->name[length - 1] = '\0';

        length = strlen(path) + 1 + strlen(name) + 1 + 1;
        currTheme->filePath = (char *)malloc(length * sizeof(char));
        sprintf(currTheme->filePath, "%s%s%s%s", path, separator, name, separator);

        LOG("THEMES Theme found: %s\n", currTheme->filePath);

        index++;
    }
    return index;
}

/* themePath must contains the leading separator (as it is dependent of the device, we can't know here) */
static int thmLoadResource(GSTEXTURE *texture, int texId, const char *themePath, short psm, int useDefault)
{
    int success = -1;

    if (themePath != NULL)
        success = texDiscoverLoad(texture, themePath, texId); // only set success here

    if ((success < 0) && useDefault)
        texLoadInternal(texture, texId); // we don't mind the result of "default"

    return success;
}

static void thmSetColors(theme_t *theme)
{
    memcpy(theme->bgColor, gDefaultBgColor, 3);
    theme->textColor = GS_SETREG_RGBA(gDefaultTextColor[0], gDefaultTextColor[1], gDefaultTextColor[2], 0x80);
    theme->uiTextColor = GS_SETREG_RGBA(gDefaultUITextColor[0], gDefaultUITextColor[1], gDefaultUITextColor[2], 0x80);
    theme->selTextColor = GS_SETREG_RGBA(gDefaultSelTextColor[0], gDefaultSelTextColor[1], gDefaultSelTextColor[2], 0x80);

    theme_element_t *elem = theme->mainElems.first;
    while (elem) {
        elem->color = theme->textColor;
        elem = elem->next;
    }
}

static void thmLoadFonts(config_set_t *themeConfig, const char *themePath, theme_t *theme)
{
    int fntID; // theme side font id, not the fntSys handle
    for (fntID = 0; fntID < THM_MAX_FONTS; ++fntID) {
        // does the font by the key exist?
        char fntKey[16];

        if (fntID == 0) {
            snprintf(fntKey, sizeof(fntKey), "default_font");
            theme->fonts[0] = FNT_DEFAULT;
        } else {
            snprintf(fntKey, sizeof(fntKey), "font%d", fntID);
            theme->fonts[fntID] = theme->fonts[0];
        }

        char fullPath[128];
        const char *fntFile;
        if (configGetStr(themeConfig, fntKey, &fntFile)) {
            snprintf(fullPath, sizeof(fullPath), "%s%s", themePath, fntFile);

            int fontSize;
            char sizeKey[64];
            if (fntID == 0)
                snprintf(sizeKey, sizeof(sizeKey), "default_font_size");
            else
                snprintf(sizeKey, sizeof(sizeKey), "font%d_size", fntID);

            if (!configGetInt(themeConfig, sizeKey, &fontSize) || fontSize <= 0)
                fontSize = FNTSYS_DEFAULT_SIZE;

            int fntHandle = fntLoadFile(fullPath, fontSize);
            // Do we have a valid font? Assign the font handle to the theme font slot
            if (fntHandle != FNT_ERROR)
                theme->fonts[fntID] = fntHandle;
        }
    }
}

static void thmLoad(const char *themePath)
{
    LOG("THEMES Load theme path=%s\n", themePath);
    char path[256];
    theme_t *curT = gTheme;
    theme_t *newT = (theme_t *)malloc(sizeof(theme_t));
    memset(newT, 0, sizeof(theme_t));

    newT->useDefault = 1;
    newT->usedHeight = 480;
    thmSetColors(newT);
    newT->mainElems.first = NULL;
    newT->mainElems.last = NULL;
    newT->infoElems.first = NULL;
    newT->infoElems.last = NULL;
    newT->appsMainElems.first = NULL;
    newT->appsMainElems.last = NULL;
    newT->appsInfoElems.first = NULL;
    newT->appsInfoElems.last = NULL;
    newT->gameCacheCount = 0;
    newT->itemsList = NULL;
    newT->gamesItemsList = NULL;
    newT->appsItemsList = NULL;
    newT->loadingIconCount = 1;

    config_set_t *themeConfig = NULL;
    if (!themePath) {
        // No theme specified. Prepare and load the default theme.
        themeConfig = configAlloc(0, NULL, NULL);
        configReadBuffer(themeConfig, &conf_theme_OPL_cfg, size_conf_theme_OPL_cfg);
    } else {
        snprintf(path, sizeof(path), "%sconf_theme.cfg", themePath);
        themeConfig = configAlloc(0, NULL, path);
        configRead(themeConfig); // try to load the theme config file. If it does not exist, defaults will be used.
    }

    int intValue;
    if (configGetInt(themeConfig, "use_default", &intValue))
        newT->useDefault = intValue;

    if (configGetInt(themeConfig, "use_real_height", &intValue)) {
        if (intValue)
            newT->usedHeight = screenHeight;
    }

    configGetColor(themeConfig, "bg_color", newT->bgColor);

    unsigned char color[3];
    if (configGetColor(themeConfig, "text_color", color))
        newT->textColor = GS_SETREG_RGBA(color[0], color[1], color[2], 0x80);

    if (configGetColor(themeConfig, "ui_text_color", color))
        newT->uiTextColor = GS_SETREG_RGBA(color[0], color[1], color[2], 0x80);

    if (configGetColor(themeConfig, "sel_text_color", color))
        newT->selTextColor = GS_SETREG_RGBA(color[0], color[1], color[2], 0x80);

    // before loading the element definitions, we have to have the fonts prepared
    // for that, we load the fonts and a translation table
    if (themePath)
        thmLoadFonts(themeConfig, themePath, newT);

    int i = 1, j;
    snprintf(path, sizeof(path), "main0");
    while (addGUIElem(themePath, themeConfig, newT, &newT->mainElems, NULL, path))
        snprintf(path, sizeof(path), "main%d", i++);

    for (j = 0; j < i; j++) {
        snprintf(path, sizeof(path), "appsMain%d", j);

        if (addGUIElem(themePath, themeConfig, newT, &newT->appsMainElems, NULL, path))
            continue;
        else {
            snprintf(path, sizeof(path), "main%d", j);
            addGUIElem(themePath, themeConfig, newT, &newT->appsMainElems, NULL, path);
        }
    }

    i = 1;
    snprintf(path, sizeof(path), "info0");
    while (addGUIElem(themePath, themeConfig, newT, &newT->infoElems, NULL, path))
        snprintf(path, sizeof(path), "info%d", i++);

    for (j = 0; j < i; j++) {
        snprintf(path, sizeof(path), "appsInfo%d", j);

        if (addGUIElem(themePath, themeConfig, newT, &newT->appsInfoElems, NULL, path))
            continue;
        else {
            snprintf(path, sizeof(path), "info%d", j);
            addGUIElem(themePath, themeConfig, newT, &newT->appsInfoElems, NULL, path);
        }
    }

    if (themePath)
        validateGUIElems(themePath, themeConfig, newT);

    newT->itemsList = newT->gamesItemsList;

    configFree(themeConfig);

    LOG("THEMES Number of cache: %d\n", newT->gameCacheCount);
    LOG("THEMES Used height: %d\n", newT->usedHeight);

    // default all to not loaded...
    for (i = 0; i < TEXTURES_COUNT; i++)
        newT->textures[i].Mem = NULL;

    // LOGO, loaded here to avoid flickering during startup with device in AUTO + theme set
    texLoadInternal(&newT->textures[LOGO_PICTURE], LOGO_PICTURE);

    // First start with busy icon
    thmLoadResource(&newT->textures[LOADER_ICON], LOADER_ICON, themePath, GS_PSM_CT32, newT->useDefault);
    newT->loadingIconCount = 1;

    // Customizable icons
    for (i = BDM_ICON; i <= START_ICON; i++)
        thmLoadResource(&newT->textures[i], i, themePath, GS_PSM_CT32, newT->useDefault);

    /* Not customizable icons - currently unused.
    for (i = L1_ICON; i <= R3_ICON; i++)
        thmLoadResource(&newT->textures[i], i, NULL, GS_PSM_CT32, 1); */

    if (!themePath)
        for (i = ELF_FORMAT; i <= VMODE_PAL; i++)
            thmLoadResource(&newT->textures[i], i, NULL, GS_PSM_CT32, 1);

    gTheme = newT;
    thmFree(curT);
}

static void thmRebuildGuiNames(void)
{
    if (guiThemesNames)
        free(guiThemesNames);

    // build the themes name list
    guiThemesNames = (const char **)malloc((nThemes + 2) * sizeof(char **));

    // add default internal
    guiThemesNames[0] = "<OPL>";

    int i = 0;
    for (; i < nThemes; i++) {
        guiThemesNames[i + 1] = themes[i].name;
    }

    guiThemesNames[nThemes + 1] = NULL;
}

int thmAddElements(char *path, const char *separator, int forceRefresh)
{
    int result, i;

    result = listDir(path, separator, THM_MAX_FILES - nThemes, &thmReadEntry);
    nThemes += result;
    thmRebuildGuiNames();

    const char *temp;
    if (configGetStr(configGetByType(CONFIG_OPL), "theme", &temp)) {
        LOG("THEMES Trying to set again theme: %s\n", temp);
        if (thmSetGuiValue(thmFindGuiID(temp), 0) && forceRefresh) {
            for (i = 0; i < MODE_COUNT; i++)
                moduleUpdateMenu(i, 1, 0);
        }
    }

    return result;
}

void thmInit(void)
{
    LOG("THEMES Init\n");
    gTheme = NULL;

    thmReloadScreenExtents();

    // initialize default internal
    thmLoad(NULL);

    thmAddElements(gBaseMCDir, "/", 0);
}

void thmReinit(const char *path)
{
    if (path == NULL)
        return;

    thmLoad(NULL);
    guiThemeID = 0;

    int i = 0;
    while (i < nThemes) {
        if (strncmp(themes[i].filePath, path, strlen(path)) == 0) {
            LOG("THEMES Remove theme: %s\n", themes[i].filePath);
            nThemes--;
            free(themes[i].name);
            themes[i].name = themes[nThemes].name;
            themes[nThemes].name = NULL;
            free(themes[i].filePath);
            themes[i].filePath = themes[nThemes].filePath;
            themes[nThemes].filePath = NULL;
        } else
            i++;
    }

    thmRebuildGuiNames();
}

void thmReloadScreenExtents(void)
{
    rmGetScreenExtents(&screenWidth, &screenHeight);
}

const char *thmGetValue(void)
{
    return guiThemesNames[guiThemeID];
}

int thmSetGuiValue(int themeID, int reload)
{
    if (themeID != -1) {
        if (guiThemeID != themeID || reload) {
            thmLoad(themeID != 0 ? themes[themeID - 1].filePath : NULL);

            guiThemeID = themeID;
            return 1;
        } else if (guiThemeID == 0)
            thmSetColors(gTheme);
    }
    return 0;
}

int thmGetGuiValue(void)
{
    return guiThemeID;
}

int thmFindGuiID(const char *theme)
{
    if (theme) {
        int i = 0;
        for (; i < nThemes; i++) {
            if (strcasecmp(themes[i].name, theme) == 0)
                return i + 1;
        }
    }
    return 0;
}

const char **thmGetGuiList(void)
{
    return guiThemesNames;
}

char *thmGetFilePath(int themeID)
{
    theme_file_t *currTheme = &themes[themeID - 1];
    char *path = currTheme->filePath;

    return path;
}

void thmEnd(void)
{
    thmFree(gTheme);

    int i = 0;
    for (; i < nThemes; i++) {
        free(themes[i].name);
        free(themes[i].filePath);
    }

    free(guiThemesNames);

    extern GSTEXTURE gPS5InstagramTex;
    extern int gPS5InstagramTexLoaded;
    if (gPS5InstagramTexLoaded) {
        texFree(&gPS5InstagramTex);
        gPS5InstagramTexLoaded = 0;
    }
}

void playPS5LaunchTransition(const char *gameTitle)
{
    sfxPlay(SFX_GAME_LAUNCH);
    net_req_t *cacheEntry = NULL;
    u8 cR = 16, cG = 16, cB = 16;
    u8 bR = 16, bG = 16, bB = 16;

    extern int gPS5Mode;
    if (gPS5Mode) {
        getGameColors(gameTitle, &cR, &cG, &cB, &bR, &bG, &bB);
        cacheEntry = findNetCacheEntry(gameTitle);
    }

    int frame;
    const int total_frames = 35;
    for (frame = 0; frame <= total_frames; frame++) {
        float t = (float)frame / (float)total_frames;
        // Cubic easing out
        float ease = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);

        // Zoom from card position to full screen.
        float startX = 55.0f;
        float startY = 96.0f;
        float startSize = 130.0f;

        float currX = startX + ease * (0.0f - startX);
        float currY = startY + ease * (0.0f - startY);
        float currSizeW = startSize + ease * ((float)screenWidth - startSize);
        float currSizeH = startSize + ease * ((float)screenHeight - startSize);
        int currR = 12;

        guiStartFrame();

        // 1. Draw the gapless plasma background gradient (using the deep background colors of the game!)
        if (gPS5Mode) {
            extern u8 gPS5BgColorR;
            extern u8 gPS5BgColorG;
            extern u8 gPS5BgColorB;
            gPS5BgColorR = bR;
            gPS5BgColorG = bG;
            gPS5BgColorB = bB;
        }
        guiDrawBGPlasma();

        // 2. Draw the zooming card
        int hasCover = (cacheEntry && cacheEntry->hasTex == 1);
        if (hasCover) {
            rmDrawRoundedCover(&cacheEntry->coverTex, (int)currX, (int)currY, (int)currSizeW, (int)currSizeH, currR);
        } else {
            rmDrawRoundedRectWide((int)currX, (int)currY, (int)currSizeW, (int)currSizeH, currR, GS_SETREG_RGBA(cR, cG, cB, 0x80));
        }

        // 3. Draw full-screen black overlay fading to pure black (fade faster)
        float fadeT = t * 2.0f;
        if (fadeT > 1.0f) fadeT = 1.0f;
        int blackAlpha = (int)(fadeT * 255.0f);
        if (blackAlpha > 255) blackAlpha = 255;
        if (blackAlpha < 0) blackAlpha = 0;
        rmDrawRect(0, 0, screenWidth, screenHeight, GS_SETREG_RGBA(0, 0, 0, blackAlpha));

        guiEndFrame();
    }
}

void drawPS5LaunchLoadingFrame(unsigned int frame, int alpha)
{
    GSTEXTURE *loader = thmGetTexture(LOADER_ICON);
    int loaderSize = 14;
    int loaderX = (screenWidth - 20 - (loaderSize / 2)) * 4 / rmGetAspectWidth();
    int loaderY = screenHeight - 20 - (loaderSize / 2);
    float angle = (float)frame * 0.16f;

    if (alpha < 0)
        alpha = 0;
    else if (alpha > 0x80)
        alpha = 0x80;

    guiStartFrame();
    rmDrawRect(0, 0, screenWidth, screenHeight, GS_SETREG_RGBA(0, 0, 0, 0x80));
    if (loader != NULL && loader->Mem != NULL && alpha > 0)
        rmDrawRotatedPixmap(loader, loaderX, loaderY, loaderSize, loaderSize, angle, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, alpha));
    guiEndFrame();
}
