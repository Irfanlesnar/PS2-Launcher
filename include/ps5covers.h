#ifndef __PS5COVERS_H
#define __PS5COVERS_H

typedef struct
{
    const char *name;
    void **png;
} ps5_cover_asset_t;

extern const ps5_cover_asset_t gPS5CoverAssets[];
extern const int gPS5CoverAssetCount;

#endif
