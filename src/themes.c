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
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <malloc.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdio.h>
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

static GSTEXTURE *getGameImageTexture(image_cache_t *cache, void *support, struct submenu_item *item)
{
    if (gEnableArt) {
        item_list_t *list = (item_list_t *)support;
        char *startup = list->itemGetStartup(list, item->id);
        return cacheGetTexture(cache, list, &item->cache_id[cache->userId], &item->cache_uid[cache->userId], startup);
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
static int gPS5BoldFont = -1;
static int gPS5HeaderFont = -1;
static int gPS5TitleFont = -1;
static int gPS5SmallFont = -1;
static float gPS5AnimPos = -1.0f;

int gPS5ActiveTab = 0; // 0 = Games, 1 = Settings
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
    gPS5BoldFont = gPS5HeaderFont; // Compatibility fallback
    if (gPS5HeaderFont != -1 && gPS5TitleFont != -1) {
        LOG("THEMES Loaded PS5 bold header (20) and title (34) fonts from embedded roboto_bold\n");
    } else {
        LOG("THEMES Failed to load embedded roboto_bold!\n");
        if (gPS5HeaderFont == -1) gPS5HeaderFont = 0;
        if (gPS5TitleFont == -1) gPS5TitleFont = 0;
        gPS5BoldFont = 0;
    }
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

    float factorTop = (float)y_draw / 480.0f;
    if (factorTop < 0.0f) factorTop = 0.0f;
    if (factorTop > 1.0f) factorTop = 1.0f;
    u8 rTop = (u8)(gPS5BgColorR * factorTop);
    u8 gTop = (u8)(gPS5BgColorG * factorTop);
    u8 bTop = (u8)(gPS5BgColorB * factorTop);
    u64 colorTL = GS_SETREG_RGBA(rTop, gTop, bTop, 0x80);
    u64 colorTR = GS_SETREG_RGBA(rTop, gTop, bTop, 0x80);
    
    float factorBottom = (float)(y_draw + h_draw) / 480.0f;
    if (factorBottom < 0.0f) factorBottom = 0.0f;
    if (factorBottom > 1.0f) factorBottom = 1.0f;
    u8 rBottom = (u8)(gPS5BgColorR * factorBottom);
    u8 gBottom = (u8)(gPS5BgColorG * factorBottom);
    u8 bBottom = (u8)(gPS5BgColorB * factorBottom);
    u64 colorBL = GS_SETREG_RGBA(rBottom, gBottom, bBottom, 0x80);
    u64 colorBR = GS_SETREG_RGBA(rBottom, gBottom, bBottom, 0x80);

    rmDrawRawQuad(&gPS5InvMaskTex, X0, Y0, X1, Y1, 0, 0, tr, tr, colorTL);                     // Top-Left
    rmDrawRawQuad(&gPS5InvMaskTex, X2, Y0, X3, Y1, tw - tr, 0, tw, tr, colorTR);         // Top-Right
    rmDrawRawQuad(&gPS5InvMaskTex, X0, Y2, X1, Y3, 0, th - tr, tr, th, colorBL);         // Bottom-Left
    rmDrawRawQuad(&gPS5InvMaskTex, X2, Y2, X3, Y3, tw - tr, th - tr, tw, th, colorBR); // Bottom-Right
}

int gPS5SettingsPage = 0; // 0 = Main Settings list, 1 = Display Settings sub-menu
int gPS5SettingsSel = 0;

int gPS5TempVMode = 0;
int gPS5SubSel = 0;
unsigned int gPS5SaveNotifyFrame = 0; // Frame timing for toast popup

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
} net_req_t;

#define MAX_NET_CACHED_GAMES 64
static net_req_t gNetCache[MAX_NET_CACHED_GAMES];
static int gNetCacheCount = 0;
static char gNetDebugMsg[256] = "Net Status: System ready.";

extern void *_gp;

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

static void updateCacheState(const char *title, int state, int hasColor, u8 cardR, u8 cardG, u8 cardB, u8 bgR, u8 bgG, u8 bgB, const char *coverPath) {
    int i;
    for (i = 0; i < gNetCacheCount; i++) {
        if (strcmp(gNetCache[i].gameTitle, title) == 0) {
            gNetCache[i].state = state;
            if (hasColor) {
                gNetCache[i].cardR = cardR;
                gNetCache[i].cardG = cardG;
                gNetCache[i].cardB = cardB;
                gNetCache[i].bgR = bgR;
                gNetCache[i].bgG = bgG;
                gNetCache[i].bgB = bgB;
                gNetCache[i].hasColor = 1;
            }
            if (coverPath) {
                strncpy(gNetCache[i].coverPath, coverPath, sizeof(gNetCache[i].coverPath) - 1);
                gNetCache[i].coverPath[sizeof(gNetCache[i].coverPath) - 1] = '\0';
                gNetCache[i].hasTex = 0;
            }
            break;
        }
    }
}

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

extern volatile int gBdmDisconnected;
extern volatile unsigned int gBdmEventGeneration;

int debugOpenProbe(const char *path) {
    if (gBdmDisconnected && bdmIsUsbPath(path)) {
        return -1; // Physically disconnected USB device. Do not block APA/ATA mass paths.
    }
    return open(path, O_RDONLY);
}

static void findCoverInCoversFolder(const char *gameTitle, const char *startup, const char *devicePrefix, char *matchedPath, int maxLen) {
    if (startup && startup[0] != '\0') {
        char prefix[128];
        if (devicePrefix && devicePrefix[0] != '\0') {
            strncpy(prefix, devicePrefix, sizeof(prefix) - 1);
            prefix[sizeof(prefix) - 1] = '\0';
            int len = strlen(prefix);
            if (len > 0 && prefix[len - 1] == '/') prefix[len - 1] = '\0';
        } else {
            strcpy(prefix, "mass0:");
        }

        // Try [prefix]/ART/[startup]_COV.png
        snprintf(matchedPath, maxLen, "%s/ART/%s_COV.png", prefix, startup);
        int fd = debugOpenProbe(matchedPath);
        if (fd >= 0) {
            close(fd);
            return;
        }

        // Try [prefix]/ART/[startup]_COV.jpg
        snprintf(matchedPath, maxLen, "%s/ART/%s_COV.jpg", prefix, startup);
        fd = debugOpenProbe(matchedPath);
        if (fd >= 0) {
            close(fd);
            return;
        }

        // Try lowercase
        char startupLower[32];
        int i;
        for (i = 0; startup[i]; i++) {
            startupLower[i] = (startup[i] >= 'A' && startup[i] <= 'Z') ? (startup[i] - 'A' + 'a') : startup[i];
        }
        startupLower[i] = '\0';

        snprintf(matchedPath, maxLen, "%s/ART/%s_COV.png", prefix, startupLower);
        fd = debugOpenProbe(matchedPath);
        if (fd >= 0) {
            close(fd);
            return;
        }

        snprintf(matchedPath, maxLen, "%s/ART/%s_COV.jpg", prefix, startupLower);
        fd = debugOpenProbe(matchedPath);
        if (fd >= 0) {
            close(fd);
            return;
        }
    }

    matchedPath[0] = '\0';
}

static void findLogoInLogosFolder(const char *startup, const char *devicePrefix, char *matchedPath, int maxLen) {
    if (!startup || startup[0] == '\0') {
        matchedPath[0] = '\0';
        return;
    }

    char prefix[128];
    if (devicePrefix && devicePrefix[0] != '\0') {
        strncpy(prefix, devicePrefix, sizeof(prefix) - 1);
        prefix[sizeof(prefix) - 1] = '\0';
        int len = strlen(prefix);
        if (len > 0 && prefix[len - 1] == '/') prefix[len - 1] = '\0';
    } else {
        strcpy(prefix, "mass0:");
    }

    char logoName[256];
    snprintf(logoName, sizeof(logoName), "%s_LOGO", startup);

    // Only try LOGO, logo, ART, art folders under the active prefix (maximum 16 extremely fast open() probes)
    const char *folders[] = {"LOGO", "logo", "ART", "art"};
    int fIdx;
    int fd;
    for (fIdx = 0; fIdx < 4; fIdx++) {
        // Try png
        snprintf(matchedPath, maxLen, "%s/%s/%s.png", prefix, folders[fIdx], logoName);
        fd = debugOpenProbe(matchedPath);
        if (fd >= 0) {
            close(fd);
            return;
        }

        // Try jpg
        snprintf(matchedPath, maxLen, "%s/%s/%s.jpg", prefix, folders[fIdx], logoName);
        fd = debugOpenProbe(matchedPath);
        if (fd >= 0) {
            close(fd);
            return;
        }

        // Try lowercase
        char logoNameLower[256];
        int i;
        for (i = 0; logoName[i]; i++) {
            logoNameLower[i] = (logoName[i] >= 'A' && logoName[i] <= 'Z') ? (logoName[i] - 'A' + 'a') : logoName[i];
        }
        logoNameLower[i] = '\0';

        snprintf(matchedPath, maxLen, "%s/%s/%s.png", prefix, folders[fIdx], logoNameLower);
        fd = debugOpenProbe(matchedPath);
        if (fd >= 0) {
            close(fd);
            return;
        }

        snprintf(matchedPath, maxLen, "%s/%s/%s.jpg", prefix, folders[fIdx], logoNameLower);
        fd = debugOpenProbe(matchedPath);
        if (fd >= 0) {
            close(fd);
            return;
        }
    }

    matchedPath[0] = '\0';
}

static void triggerNetFetch(const char *title, const char *startup, const char *devicePrefix, int allowDeviceProbe) {
    int i;
    char matchedPath[256];
    u8 cardR = 100, cardG = 100, cardB = 100;
    u8 bgR = 24, bgG = 24, bgB = 24;

    for (i = 0; i < gNetCacheCount; i++) {
        if (strcmp(gNetCache[i].gameTitle, title) == 0) {
            return;
        }
    }

    if (gNetCacheCount >= MAX_NET_CACHED_GAMES) return;

    int idx = gNetCacheCount++;
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
    if (allowDeviceProbe) {
        findCoverInCoversFolder(title, startup, devicePrefix, matchedPath, sizeof(matchedPath));
    }
    if (matchedPath[0] == '\0') {
        findBuiltInCoverForGame(title, matchedPath, sizeof(matchedPath));
    }

    char logoPath[256] = {0};
    if (allowDeviceProbe) {
        findLogoInLogosFolder(startup, devicePrefix, logoPath, sizeof(logoPath));
    }
    strncpy(gNetCache[idx].logoPath, logoPath, sizeof(gNetCache[idx].logoPath) - 1);
    gNetCache[idx].logoPath[sizeof(gNetCache[idx].logoPath) - 1] = '\0';
    gNetCache[idx].hasLogoTex = 0;

    getGameColors(title, &cardR, &cardG, &cardB, &bgR, &bgG, &bgB);
    updateCacheState(title, 2, 1, cardR, cardG, cardB, bgR, bgG, bgB, matchedPath);
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

static void clearNetCache(void)
{
    int i;
    for (i = 0; i < gNetCacheCount; i++) {
        if (gNetCache[i].hasTex == 1) {
            rmUnloadTexture(&gNetCache[i].coverTex);
            texFree(&gNetCache[i].coverTex);
            gNetCache[i].hasTex = 0;
        }
        if (gNetCache[i].hasLogoTex == 1) {
            rmUnloadTexture(&gNetCache[i].logoTex);
            texFree(&gNetCache[i].logoTex);
            gNetCache[i].hasLogoTex = 0;
        }
    }
    gNetCacheCount = 0;
    gPS5UserHasNavigated = 0;
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

    if (targetIdx <= 0) {
        selectedItem->item->current = curr;
        return;
    }

    while (curr) {
        const char *title = submenuItemGetText(&curr->item);
        if (ps5TitleMatchesAlpha(title, targetIdx)) {
            match = curr;
            break;
        }
        curr = curr->next;
    }

    if (match) {
        selectedItem->item->current = match;
    }
}


static void drawPS5Launcher(struct menu_list *menu, struct submenu_list *item, struct theme_element *elem)
{
    // Initialize Fonts and Mask Textures if not loaded
    initPS5MaskTextures();

    static unsigned int lastBdmEventGeneration = 0;
    if (lastBdmEventGeneration != gBdmEventGeneration) {
        lastBdmEventGeneration = gBdmEventGeneration;
        clearNetCache();
    }

    char *prefix = "";
    int isUnplugged = 0;
    int allowDeviceProbe = 1;
    if (item && gPS5ActiveTab == 0) {
        const char *startup = NULL;
        item_list_t *list = (item_list_t *)menu->item->userdata;
        if (list) {
            int isBdmMode = list->mode >= BDM_MODE && list->mode < ETH_MODE;
            if (list->itemGetPrefix) {
                prefix = list->itemGetPrefix(list);
            }
            if (isBdmMode && (prefix == NULL || prefix[0] == '\0' || (gBdmDisconnected && bdmIsUsbPath(prefix)))) {
                isUnplugged = 1;
            }
            if (!isUnplugged && list->itemGetStartup) {
                startup = list->itemGetStartup(list, item->item.id);
            }
        }

        static int lastIsUnplugged = -1;
        if (isUnplugged != lastIsUnplugged) {
            lastIsUnplugged = isUnplugged;
        }

        if (list && list->itemGetCount(list) > 0 && !isUnplugged) {
            triggerNetFetch(submenuItemGetText(&item->item), startup, prefix, allowDeviceProbe);
        }
    }

    // 1. Get PS2 Realtime clock
    time_t rawtime;
    struct tm *timeinfo;
    char timeStr[16];
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    if (timeinfo) {
        int hour = timeinfo->tm_hour;
        int minute = timeinfo->tm_min;
        const char *am_pm = "AM";
        if (hour >= 12) {
            am_pm = "PM";
            if (hour > 12) hour -= 12;
        }
        if (hour == 0) hour = 12;
        snprintf(timeStr, sizeof(timeStr), "%d:%02d %s", hour, minute, am_pm);
    } else {
        strncpy(timeStr, "12:00 PM", sizeof(timeStr));
    }

    // Header Navigation (Top Left & Top Right)
    if (gPS5ActiveTab == 0) {
        fntRenderString(gPS5RegFont, 50, 32, ALIGN_LEFT, 0, 0, "Games", GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
        fntRenderString(gPS5RegFont, 140, 32, ALIGN_LEFT, 0, 0, "Settings", GS_SETREG_RGBA(0x80, 0x80, 0x80, 0x40));
    } else {
        fntRenderString(gPS5RegFont, 50, 32, ALIGN_LEFT, 0, 0, "Games", GS_SETREG_RGBA(0x80, 0x80, 0x80, 0x40));
        fntRenderString(gPS5RegFont, 140, 32, ALIGN_LEFT, 0, 0, "Settings", GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
    }
    // Small L1/R1 indicators (smaller font size, offset adjusted for alignment)
    fntRenderString(gPS5SmallFont, 30, 39, ALIGN_LEFT, 0, 0, "L1", GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x20));
    fntRenderString(gPS5SmallFont, 212, 39, ALIGN_LEFT, 0, 0, "R1", GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x20));
    extern int gPS5ShowTime;
    if (gPS5ShowTime) {
        fntRenderString(gPS5RegFont, screenWidth - 50, 32, ALIGN_RIGHT, 0, 0, timeStr, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
    }

    if (gPS5ActiveTab == 0) {
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
            
            if (strcmp(lastSelectedTitle, selTitle) != 0) {
                logoAlpha = 0.0f;
                strncpy(lastSelectedTitle, selTitle, sizeof(lastSelectedTitle) - 1);
                lastSelectedTitle[sizeof(lastSelectedTitle) - 1] = '\0';
            }

            net_req_t *selCache = NULL;
            int cIdx;
            for (cIdx = 0; cIdx < gNetCacheCount; cIdx++) {
                if (strcmp(gNetCache[cIdx].gameTitle, selTitle) == 0) {
                    selCache = &gNetCache[cIdx];
                    break;
                }
            }

            if (selCache && selCache->logoPath[0] != '\0') {
                if (selCache->hasLogoTex == 0 && !isUnplugged) {
                    if (loadPS5CoverTexture(&selCache->logoTex, selCache->logoPath) >= 0) {
                        selCache->hasLogoTex = 1;
                        logoAlpha = 0.0f; // Reset to 0 to ensure clean fade-in after load completes
                    } else {
                        selCache->hasLogoTex = -1;
                    }
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
                    
                    // Render anchored at bottom-right (640, 480) with dynamic smooth fade-in
                    rmDrawPixmap(&selCache->logoTex, 640, 480, ALIGN_BOTTOM | ALIGN_RIGHT, lw, lh, SCALING_RATIO, GS_SETREG_RGBA(0x80, 0x80, 0x80, alphaVal));
                }
            }

            // 2. Horizontal Carousel of Cards
            submenu_list_t *first_item = item;
            while (first_item->prev) {
                first_item = first_item->prev;
            }

            static submenu_list_t *visibleItems[512];
            submenu_list_t *count_curr = first_item;
            int selected_index = 0;
            int total_count = 0;
            submenu_list_t *selectedVisibleItem = NULL;

            while (count_curr && total_count < 512) {
                const char *title = submenuItemGetText(&count_curr->item);
                if (ps5TitleMatchesAlpha(title, gPS5AlphaIdx)) {
                    if (count_curr == item) {
                        selected_index = total_count;
                        selectedVisibleItem = count_curr;
                    }
                    visibleItems[total_count++] = count_curr;
                }
                count_curr = count_curr->next;
            }

            if (selectedVisibleItem == NULL && total_count > 0) {
                selectedVisibleItem = visibleItems[0];
                selected_index = 0;
            }

            if (gPS5AnimPos < 0.0f) {
                gPS5AnimPos = selected_index;
            } else {
                if (total_count > 0 && gPS5AnimPos > (float)(total_count - 1))
                    gPS5AnimPos = (float)(total_count - 1);
                if (gPS5AnimPos < 0.0f)
                    gPS5AnimPos = 0.0f;
                gPS5AnimPos += (selected_index - gPS5AnimPos) * 0.36f;
                float diff = selected_index - gPS5AnimPos;
                if (diff < 0.0f) diff = -diff;
                if (diff < 0.002f) {
                    gPS5AnimPos = selected_index;
                }
            }

            static float x_centers[512];
            static float card_sizes[512];

            int i;
            for (i = 0; i < total_count; i++) {
                float dist = i - gPS5AnimPos;
                if (dist < 0.0f) dist = -dist;

                float t = 1.0f - dist;
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
                t = t * t * (3.0f - 2.0f * t);

                // Unselected size = 80, Fully selected size = 130
                card_sizes[i] = 80.0f + t * 50.0f;
            }

            if (total_count > 0) {
                x_centers[0] = 0.0f;
                for (i = 0; i < total_count - 1; i++) {
                    x_centers[i + 1] = x_centers[i] + ((card_sizes[i] + card_sizes[i + 1]) * 0.73f) / 2.0f + 16.0f;
                }
            }

            int k = (int)gPS5AnimPos;
            float frac = gPS5AnimPos - k;
            float x_focus = 0.0f;
            if (total_count > 0) {
                if (k < total_count - 1) {
                    x_focus = x_centers[k] + frac * (x_centers[k + 1] - x_centers[k]);
                } else {
                    x_focus = x_centers[total_count - 1];
                }
            }

            // Shift so that the focus point is anchored horizontally at 120.0f
            float shift = 120.0f - x_focus;

            int idx = 0;
            while (idx < total_count) {
                submenu_list_t *curr_item = visibleItems[idx];
                float cx = x_centers[idx] + shift;
                if (cx > -80.0f && cx < 720.0f) {
                    int height = (int)card_sizes[idx];
                    int width = (int)(height * 0.73f);
                    int hw = width / 2;
                    int x1 = (int)cx - hw;
                    int y1 = 96; // Anchor at the top edge for downward scaling!

                    // Dynamic Alpha based on distance from focus
                    float dist = idx - gPS5AnimPos;
                    if (dist < 0.0f) dist = -dist;
                    float t = 1.0f - dist;
                    if (t < 0.0f) t = 0.0f;
                    if (t > 1.0f) t = 1.0f;
                    t = t * t * (3.0f - 2.0f * t);
                    int cardAlpha = (int)(0x22 + t * (0x74 - 0x22));

                    // 2a. Card Body (with custom Corner Radius & Dynamic Colors / Covers!)
                    u8 cR, cG, cB, bR, bG, bB;
                    const char *gameTitleText = submenuItemGetText(&curr_item->item);
                    int hasCover = 0;
                    int cacheIdx;
                    net_req_t *cacheEntry = NULL;

                    getGameColors(gameTitleText, &cR, &cG, &cB, &bR, &bG, &bB);

                    for (cacheIdx = 0; cacheIdx < gNetCacheCount; cacheIdx++) {
                        if (strcmp(gNetCache[cacheIdx].gameTitle, gameTitleText) == 0) {
                            cacheEntry = &gNetCache[cacheIdx];
                            break;
                        }
                    }

                    if (cacheEntry) {
                        if (cacheEntry->state == 2 && cacheEntry->coverPath[0] != '\0') {
                            if (cacheEntry->hasTex == 0 && !isUnplugged) {
                                if (loadPS5CoverTexture(&cacheEntry->coverTex, cacheEntry->coverPath) >= 0)
                                    cacheEntry->hasTex = 1;
                                else
                                    cacheEntry->hasTex = -1;
                            }
                            hasCover = (cacheEntry->hasTex == 1);
                        }
                    } else if (!isUnplugged) {
                        char *prefix = "";
                        const char *startup = NULL;
                        int allowCardDeviceProbe = allowDeviceProbe;
                        item_list_t *list = (item_list_t *)menu->item->userdata;
                        if (list) {
                            int isBdmMode = list->mode >= BDM_MODE && list->mode < ETH_MODE;
                            if (list->itemGetPrefix) {
                                prefix = list->itemGetPrefix(list);
                            }
                            if (isBdmMode && (prefix == NULL || prefix[0] == '\0' || (gBdmDisconnected && bdmIsUsbPath(prefix))))
                                allowCardDeviceProbe = 0;
                            if (list->itemGetStartup) {
                                startup = list->itemGetStartup(list, curr_item->item.id);
                            }
                        }
                        triggerNetFetch(gameTitleText, startup, prefix, allowCardDeviceProbe);
                    }

                    if (hasCover && cacheEntry) {
                        rmDrawRoundedCover(&cacheEntry->coverTex, x1, y1, width, height, 12);
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
                        
                        if (initCount == 0) {
                            strncpy(initials, "PS2", sizeof(initials));
                            initCount = 3;
                        }
                        initials[initCount] = '\0';

                        // 3. Draw the game's initials elegantly in the center of the card
                        float fontScale = ((float)height / 130.0f) * 0.70f;
                        fntRenderString(gPS5BoldFont, cx, y1 + height / 2, ALIGN_CENTER | ALIGN_VCENTER, fontScale, fontScale, initials, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, cardAlpha * 2 / 3));
                    }
                }
                idx++;
            }

            // 3. Dynamic Background Color Smooth Transition
            u8 cardR, cardG, cardB, bgR, bgG, bgB;
            getGameColors(selectedVisibleItem ? submenuItemGetText(&selectedVisibleItem->item) : "", &cardR, &cardG, &cardB, &bgR, &bgG, &bgB);

            // Avoid white/grey/bright background colors for the gradient to prevent noise artifacts in 1080p
            // Cap background color channels at a maximum of 120 to guarantee dark, premium colors but allow rich vibrancy at the bottom
            if (bgR > 120) bgR = 120;
            if (bgG > 120) bgG = 120;
            if (bgB > 120) bgB = 120;

            // If the color is grey/desaturated, shift it to a deep premium PS5 midnight blue
            int maxVal = bgR > bgG ? (bgR > bgB ? bgR : bgB) : (bgG > bgB ? bgG : bgB);
            int minVal = bgR < bgG ? (bgR < bgB ? bgR : bgB) : (bgG < bgB ? bgG : bgB);
            if (maxVal - minVal < 10) {
                // It's grey/neutral. Replace with a beautiful cinematic deep dark blue/violet
                bgR = 10;
                bgG = 14;
                bgB = 28;
            }

            static float currentBgR = 16.0f;
            static float currentBgG = 16.0f;
            static float currentBgB = 16.0f;

            currentBgR += ((float)bgR - currentBgR) * 0.10f;
            currentBgG += ((float)bgG - currentBgG) * 0.10f;
            currentBgB += ((float)bgB - currentBgB) * 0.10f;

            gPS5BgColorR = (u8)currentBgR;
            gPS5BgColorG = (u8)currentBgG;
            gPS5BgColorB = (u8)currentBgB;

            if (selectedVisibleItem) {
                const char *fullTitle = submenuItemGetText(&selectedVisibleItem->item);
                fntRenderString(gPS5TitleFont, 50, 316, ALIGN_LEFT, 0, 0, fullTitle, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));

                // Developer name (using Game ID lookup)
                item_list_t *support = (item_list_t *)menu->item->userdata;
                const char *startup = NULL;
                if (support && support->itemGetStartup && !isUnplugged) {
                    startup = support->itemGetStartup(support, selectedVisibleItem->item.id);
                }
                fntRenderString(gPS5RegFont, 50, 354, ALIGN_LEFT, 0, 0, getGameDeveloper(startup, fullTitle), GS_SETREG_RGBA(0xF0, 0xF0, 0xF0, 0x56));
            } else {
                fntRenderString(gPS5BoldFont, 320, 245, ALIGN_CENTER, 0, 0, "NO GAMES FOUND", GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x2C));
            }

        } else {
            // No games placeholder card/text
            // No games placeholder card/text
            fntRenderString(gPS5BoldFont, 320, 245, ALIGN_CENTER, 0, 0, "NO GAMES FOUND", GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x2C));
        }

        // Draw bottom helper buttons in Games list (very bottom-left with 20px margin)
        int helperY = 428;
        int nextX = drawPS5IconAndText(SQUARE_ICON, "Refresh", gPS5RegFont, 50, helperY, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x40));
        drawPS5IconAndText(CROSS_ICON, "Play", gPS5RegFont, nextX + 20, helperY, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x40));

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

            // Sync active letter to selected game when game selection changes via Left/Right
            static submenu_list_t *lastItem = NULL;
            static int gPS5FirstStart = 1;
            if (item != lastItem) {
                lastItem = item;
                
                if (gPS5FirstStart) {
                    gPS5FirstStart = 0;
                } else if (gPS5UserHasNavigated) {
                    if (!getKeyPressed(KEY_UP) && !getKeyPressed(KEY_DOWN)) {
                        const char *selTitle = submenuItemGetText(&item->item);
                        char firstChar = '\0';
                        if (selTitle && selTitle[0] != '\0') {
                            firstChar = selTitle[0];
                            if (firstChar >= 'a' && firstChar <= 'z') firstChar -= 32;
                        }
                        int gameAlphaIdx = 0; // default to '#'
                        if (firstChar >= 'A' && firstChar <= 'Z') {
                            gameAlphaIdx = firstChar - 'A' + 1;
                        }
                        gPS5AlphaIdx = gameAlphaIdx;
                    }
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
                        fntRenderString(gPS5RegFont, alphabetX, (int)charY, ALIGN_CENTER | ALIGN_VCENTER, 0.50f, 0.50f, letterStr, GS_SETREG_RGBA(0x80, 0x80, 0x80, alphaVal));
                    }
                }
            }
        }
    } else {
        extern int gPS5SubSel;
        extern int gPS5TempVMode;
        extern int gPS5TempShowTime;
        extern int gPS5TempUISound;
        extern int gPS5SettingsSel;
        extern int gPS5InternetStatus;
        extern unsigned int gPS5SaveNotifyFrame;
        extern int guiFrameId;

        int rowX = 64;
        int rowY = 100;

        // 1. Draw Resolution label and bracketed value
        const char *resText = "Standard";
        if (gPS5TempVMode == 3) resText = "Progressive 480p";
        else if (gPS5TempVMode == 10) resText = "720p";
        else if (gPS5TempVMode == 11) resText = "1080p";

        char valStr[64];
        snprintf(valStr, sizeof(valStr), "< %s >", resText);

        int rightX = screenWidth - 64;

        if (gPS5SubSel == 0) { // Resolution focused
            fntRenderString(gPS5RegFont, rowX, rowY, ALIGN_LEFT, 0, 0, "Resolution", GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
            fntRenderString(gPS5RegFont, rightX, rowY, ALIGN_RIGHT, 0, 0, valStr, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
        } else { // Resolution unfocused
            fntRenderString(gPS5RegFont, rowX, rowY, ALIGN_LEFT, 0, 0, "Resolution", GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x30));
            fntRenderString(gPS5RegFont, rightX, rowY, ALIGN_RIGHT, 0, 0, valStr, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x30));
        }

        // 2. Draw Show Time label and bracketed value
        char showTimeStr[64];
        snprintf(showTimeStr, sizeof(showTimeStr), "< %s >", gPS5TempShowTime ? "On" : "Off");

        if (gPS5SubSel == 1) { // Show Time focused
            fntRenderString(gPS5RegFont, rowX, rowY + 36, ALIGN_LEFT, 0, 0, "Show Time", GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
            fntRenderString(gPS5RegFont, rightX, rowY + 36, ALIGN_RIGHT, 0, 0, showTimeStr, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
        } else { // Show Time unfocused
            fntRenderString(gPS5RegFont, rowX, rowY + 36, ALIGN_LEFT, 0, 0, "Show Time", GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x30));
            fntRenderString(gPS5RegFont, rightX, rowY + 36, ALIGN_RIGHT, 0, 0, showTimeStr, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x30));
        }

        // 3. Draw UI Sound label and bracketed value
        char uiSoundStr[64];
        snprintf(uiSoundStr, sizeof(uiSoundStr), "< %s >", gPS5TempUISound ? "On" : "Off");

        if (gPS5SubSel == 2) { // UI Sound focused
            fntRenderString(gPS5RegFont, rowX, rowY + 72, ALIGN_LEFT, 0, 0, "UI Sound", GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
            fntRenderString(gPS5RegFont, rightX, rowY + 72, ALIGN_RIGHT, 0, 0, uiSoundStr, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
        } else { // UI Sound unfocused
            fntRenderString(gPS5RegFont, rowX, rowY + 72, ALIGN_LEFT, 0, 0, "UI Sound", GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x30));
            fntRenderString(gPS5RegFont, rightX, rowY + 72, ALIGN_RIGHT, 0, 0, uiSoundStr, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x30));
        }

        // 4. Draw game cover downloader with rounded highlight card and white border stroke
        char coverSummary[96];
        char coverButton[32];
        int coverFocused = gPS5SubSel == 3;
        u64 strokeColor = coverFocused ? GS_SETREG_RGBA(0x2E, 0x5A, 0x84, 0x50) : GS_SETREG_RGBA(0x14, 0x28, 0x40, 0x30);
        u64 cardColor = coverFocused ? GS_SETREG_RGBA(0x07, 0x13, 0x26, 0x80) : GS_SETREG_RGBA(0x05, 0x0C, 0x18, 0x78);
        u64 coverColor = coverFocused ? GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80) : GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x30);
        u64 coverSubColor = coverFocused ? GS_SETREG_RGBA(0xB0, 0xB0, 0xB0, 0x60) : GS_SETREG_RGBA(0xB0, 0xB0, 0xB0, 0x24);

        snprintf(coverSummary, sizeof(coverSummary), "%d game covers missing", gPS5CoverMissingGames);
        if (gPS5CoverDownloadStatus == PS5_COVER_DOWNLOAD_WIP)
            snprintf(coverButton, sizeof(coverButton), "Downloading");
        else
            snprintf(coverButton, sizeof(coverButton), "Download");

        int cardX = rowX - 16;
        int cardY = rowY + 116;
        int cardW = screenWidth - 2 * cardX;
        int cardH = 68;
        
        // Draw 60% white border stroke
        rmDrawRoundedRect(cardX - 1, cardY - 1, cardW + 2, cardH + 2, 7, strokeColor);
        // Draw card background
        rmDrawRoundedRect(cardX, cardY, cardW, cardH, 6, cardColor);

        fntRenderString(gPS5RegFont, rowX, cardY + 15, ALIGN_LEFT, 0, 0, "Game Covers", coverColor);
        fntRenderString(gPS5SmallFont, rowX, cardY + 36, ALIGN_LEFT, 0, 0, coverSummary, coverSubColor);

        int btnW = (gPS5CoverDownloadStatus == PS5_COVER_DOWNLOAD_WIP) ? 120 : 100;
        int btnH = 28;
        int btnX = (screenWidth - cardX) - btnW - 16;
        int btnY = cardY + (cardH - btnH) / 2;

        if (coverFocused) {
            rmDrawRoundedRect(btnX, btnY, btnW, btnH, 4, GS_SETREG_RGBA(0x0B, 0x2A, 0x4A, 0x80));
            fntRenderString(gPS5RegFont, btnX + (btnW / 2), btnY + 5, ALIGN_HCENTER, 0, 0, coverButton, GS_SETREG_RGBA(0xF0, 0xF7, 0xFF, 0x80));
        } else {
            rmDrawRoundedRect(btnX, btnY, btnW, btnH, 4, GS_SETREG_RGBA(0x08, 0x18, 0x2A, 0x70));
            fntRenderString(gPS5RegFont, btnX + (btnW / 2), btnY + 5, ALIGN_HCENTER, 0, 0, coverButton, GS_SETREG_RGBA(0x70, 0x86, 0x9E, 0x80));
        }

        // 4. Draw irfanmatheena in lowercase with Instagram icon at bottom-left (aligned vertically to Save center)
        extern GSTEXTURE gPS5InstagramTex;
        extern int gPS5InstagramTexLoaded;
        extern void *Instagram_icon_png;

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
            // Draw Instagram icon (16x16, vertically aligned to Y=446) with full opacity
            rmDrawPixmap(&gPS5InstagramTex, 50, 446, ALIGN_LEFT | ALIGN_VCENTER, 16, 16, 1, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));
            textStartX = 50 + 22;
        }

        fntRenderString(gPS5RegFont, textStartX, 446, ALIGN_LEFT | ALIGN_VCENTER, 0, 0, "irfanmatheena", GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x30));

        // 5. Draw Save text at bottom-right (aligned perfectly with version text on the left)
        u64 saveTextColor;

        if (gPS5SubSel == 4) { // Save text focused
            // Full opacity white
            saveTextColor = GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80);
        } else { // Save text unfocused
            // Low opacity white
            saveTextColor = GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x28);
        }

        fntRenderString(gPS5RegFont, 590, 446, ALIGN_RIGHT | ALIGN_VCENTER, 0, 0, "Save", saveTextColor);

        if (gPS5SubSel == 0) { // Resolution focused
            u64 applyColor = (gVMode != gPS5TempVMode) ? GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80) : GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x24);
            drawPS5IconAndText(CROSS_ICON, "Apply", gPS5RegFont, 480, 446, applyColor);
        }

        // Draw cover download progress dialog if active
        extern int gPS5CoverDownloadStatus;
        if (gPS5CoverDownloadStatus != PS5_COVER_DOWNLOAD_IDLE) {
            extern int gPS5CoverDownloadPercent;
            extern int gPS5CoverDownloadCurrent;
            extern int gPS5CoverDownloadTotal;
            extern char gPS5CoverDownloadTitle[96];
            extern char gPS5CoverDownloadUrl[256];

            int dlgW = 380;
            int dlgH = 184;
            int dlgX = (screenWidth - dlgW) / 2;
            int dlgY = (screenHeight - dlgH) / 2;

            // 1. Dark semi-transparent full screen overlay to dim background
            rmDrawRect(0, 0, screenWidth, screenHeight, GS_SETREG_RGBA(0, 0, 0, 0x60));

            // 2. Glassmorphic rounded modal container
            // Outer light border stroke for depth
            rmDrawRoundedRect(dlgX - 1, dlgY - 1, dlgW + 2, dlgH + 2, 13, GS_SETREG_RGBA(0x26, 0x38, 0x50, 0x36));
            // Container background
            rmDrawRoundedRect(dlgX, dlgY, dlgW, dlgH, 12, GS_SETREG_RGBA(0x08, 0x0D, 0x14, 0x80));

            // 3. Header title based on download status
            const char *headerTitleText = "Download Covers";
            fntRenderString(gPS5TitleFont, dlgX + 24, dlgY + 24, ALIGN_LEFT, 0, 0, headerTitleText, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80));

            // 4. Active title & status/URL
            if (gPS5CoverDownloadStatus == PS5_COVER_DOWNLOAD_WIP) {
                fntRenderString(gPS5RegFont, dlgX + 24, dlgY + 62, ALIGN_LEFT, 0, 0, gPS5CoverDownloadTitle, GS_SETREG_RGBA(0xEE, 0xEE, 0xEE, 0x80));
                fntRenderString(gPS5SmallFont, dlgX + 24, dlgY + 86, ALIGN_LEFT, 0, 0, gPS5CoverDownloadUrl, GS_SETREG_RGBA(0x98, 0xAA, 0xBD, 0x80));
            } else {
                fntRenderString(gPS5RegFont, dlgX + 24, dlgY + 62, ALIGN_LEFT, 0, 0, gPS5CoverDownloadUrl, GS_SETREG_RGBA(0xDD, 0xDD, 0xDD, 0x80));
            }

            // 5. Progress bar track and fill
            rmDrawRect(dlgX + 24, dlgY + 114, dlgW - 48, 6, GS_SETREG_RGBA(0x1C, 0x26, 0x32, 0x80));
            int barW = ((dlgW - 48) * gPS5CoverDownloadPercent) / 100;
            if (barW > (dlgW - 48)) barW = dlgW - 48;
            if (barW > 0) {
                rmDrawRect(dlgX + 24, dlgY + 114, barW, 6, GS_SETREG_RGBA(0x64, 0x8E, 0xC8, 0x80));
            }

            // Percentage / current count label
            char progressText[64];
            if (gPS5CoverDownloadStatus == PS5_COVER_DOWNLOAD_WIP) {
                snprintf(progressText, sizeof(progressText), "%d/%d Games  %d%%", gPS5CoverDownloadCurrent, gPS5CoverDownloadTotal, gPS5CoverDownloadPercent);
            } else {
                snprintf(progressText, sizeof(progressText), "%d%%", gPS5CoverDownloadPercent);
            }
            fntRenderString(gPS5SmallFont, dlgX + dlgW - 24, dlgY + 100, ALIGN_RIGHT, 0, 0, progressText, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x40));

            // 6. Action helper button hints at the bottom
            int btnY = dlgY + dlgH - 32;
            if (gPS5CoverDownloadStatus == PS5_COVER_DOWNLOAD_WIP) {
                GSTEXTURE *circleTex = thmGetTexture(CIRCLE_ICON);
                int textRight = dlgX + dlgW - 24;
                int textW = 36;
                fntRenderString(gPS5RegFont, textRight, btnY, ALIGN_RIGHT | ALIGN_VCENTER, 0.65f, 0.65f, "Cancel", GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x68));
                if (circleTex && circleTex->Mem && circleTex->Height > 0) {
                    int iconH = 12;
                    int iconW = (circleTex->Width * iconH) / circleTex->Height;
                    rmDrawPixmap(circleTex, textRight - textW - iconW, btnY, ALIGN_VCENTER | ALIGN_LEFT, iconW, iconH, 1, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x68));
                }
            } else {
                GSTEXTURE *circleTex = thmGetTexture(CIRCLE_ICON);
                int textRight = dlgX + dlgW - 24;
                int textW = 28;
                fntRenderString(gPS5RegFont, textRight, btnY, ALIGN_RIGHT | ALIGN_VCENTER, 0.65f, 0.65f, "Close", GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x68));
                if (circleTex && circleTex->Mem && circleTex->Height > 0) {
                    int iconH = 12;
                    int iconW = (circleTex->Width * iconH) / circleTex->Height;
                    rmDrawPixmap(circleTex, textRight - textW - iconW, btnY, ALIGN_VCENTER | ALIGN_LEFT, iconW, iconH, 1, GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x68));
                }
            }
        }
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
        item_list_t *support = menu->item->userdata;
        fntRenderString(elem->font, elem->posX, elem->posY, elem->aligned, 0, 0, support->itemGetStartup(support, item->item.id), elem->color);
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
            if (gNetCache[i].hasTex == 1) {
                rmUnloadTexture(&gNetCache[i].coverTex);
                texFree(&gNetCache[i].coverTex);
                gNetCache[i].hasTex = 0;
            }
            if (gNetCache[i].hasLogoTex == 1) {
                rmUnloadTexture(&gNetCache[i].logoTex);
                texFree(&gNetCache[i].logoTex);
                gNetCache[i].hasLogoTex = 0;
            }
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
    int cacheIdx;
    net_req_t *cacheEntry = NULL;
    u8 cR = 16, cG = 16, cB = 16;
    u8 bR = 16, bG = 16, bB = 16;

    extern int gPS5Mode;
    if (gPS5Mode) {
        getGameColors(gameTitle, &cR, &cG, &cB, &bR, &bG, &bB);
        for (cacheIdx = 0; cacheIdx < gNetCacheCount; cacheIdx++) {
            if (strcmp(gNetCache[cacheIdx].gameTitle, gameTitle) == 0) {
                cacheEntry = &gNetCache[cacheIdx];
                break;
            }
        }
    }

    int frame;
    const int total_frames = 35;
    for (frame = 0; frame <= total_frames; frame++) {
        float t = (float)frame / (float)total_frames;
        // Cubic easing out
        float ease = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);

        // Zoom from card position (55, 96, 130, 130) to full screen (0, 0, 640, 480)
        float startX = 55.0f;
        float startY = 96.0f;
        float startSize = 130.0f;

        float currX = startX + ease * (0.0f - startX);
        float currY = startY + ease * (0.0f - startY);
        float currSizeW = startSize + ease * (640.0f - startSize);
        float currSizeH = startSize + ease * (480.0f - startSize);
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
        rmDrawRect(0, 0, 640, 480, GS_SETREG_RGBA(0, 0, 0, blackAlpha));

        guiEndFrame();
    }
}
