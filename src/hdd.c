#include "include/opl.h"
#include "include/hdd.h"
#include "include/ioman.h"
#include "include/hddsupport.h"

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>

typedef struct // size = 1024
{
    u32 checksum; // HDL uses 0xdeadfeed magic here
    u32 magic;
    char gamename[160];
    u8 hdl_compat_flags;
    u8 ops2l_compat_flags;
    u8 dma_type;
    u8 dma_mode;
    char startup[60];
    u32 layer1_start;
    u32 discType;
    int num_partitions;
    struct
    {
        u32 part_offset; // in MB
        u32 data_start;  // in sectors
        u32 part_size;   // in KB
    } part_specs[65];
} hdl_apa_header;

#define HDL_GAME_DATA_OFFSET 0x100000 // Sector 0x800 in the extended attribute area.
#define HDL_FS_MAGIC         0x1337

u8 IOBuffer[2048] ALIGNED(64); // one sector

//-------------------------------------------------------------------------
int hddCheck(void)
{
    int ret;

    ret = fileXioDevctl("hdd0:", HDIOC_STATUS, NULL, 0, NULL, 0);
    LOG("HDD: Status is %d\n", ret);
    // 0 = HDD connected and formatted, 1 = not formatted, 2 = HDD not usable, 3 = HDD not connected.
    if ((ret >= 3) || (ret < 0))
        return -1;

    return ret;
}

//-------------------------------------------------------------------------
u32 hddGetTotalSectors(void)
{
    return fileXioDevctl("hdd0:", HDIOC_TOTALSECTOR, NULL, 0, NULL, 0);
}

//-------------------------------------------------------------------------
int hddIs48bit(void)
{
    return fileXioDevctl("xhdd0:", ATA_DEVCTL_IS_48BIT, NULL, 0, NULL, 0);
}

//-------------------------------------------------------------------------
int hddSetTransferMode(int type, int mode)
{
    hddAtaSetMode_t *args = (hddAtaSetMode_t *)IOBuffer;

    args->type = type;
    args->mode = mode;

    return fileXioDevctl("xhdd0:", ATA_DEVCTL_SET_TRANSFER_MODE, args, sizeof(hddAtaSetMode_t), NULL, 0);
}

//-------------------------------------------------------------------------
void hddSetIdleTimeout(int timeout)
{
    // From hdparm man:
    // A value of zero means "timeouts  are  disabled":  the
    // device will not automatically enter standby mode.  Values from 1
    // to 240 specify multiples of 5 seconds, yielding timeouts from  5
    // seconds to 20 minutes.  Values from 241 to 251 specify from 1 to
    // 11 units of 30 minutes, yielding timeouts from 30 minutes to 5.5
    // hours.   A  value  of  252  signifies a timeout of 21 minutes. A
    // value of 253 sets a vendor-defined timeout period between 8  and
    // 12  hours, and the value 254 is reserved.  255 is interpreted as
    // 21 minutes plus 15 seconds.  Note that  some  older  drives  may
    // have very different interpretations of these values.

    u8 standbytimer = (u8)timeout;

    fileXioDevctl("hdd0:", HDIOC_IDLE, &standbytimer, 1, NULL, 0);
    fileXioDevctl("hdd1:", HDIOC_IDLE, &standbytimer, 1, NULL, 0);
}

void hddSetIdleImmediate(void)
{
    fileXioDevctl("hdd0:", HDIOC_IDLEIMM, NULL, 0, NULL, 0);
    fileXioDevctl("hdd1:", HDIOC_IDLEIMM, NULL, 0, NULL, 0);
}

//-------------------------------------------------------------------------
int hddReadSectors(u32 lba, u32 nsectors, void *buf)
{
    hddAtaTransfer_t *args = (hddAtaTransfer_t *)IOBuffer;

    args->lba = lba;
    args->size = nsectors;

    if (fileXioDevctl("hdd0:", HDIOC_READSECTOR, args, sizeof(hddAtaTransfer_t), buf, nsectors * 512) != 0)
        return -1;

    return 0;
}

//-------------------------------------------------------------------------
static int hddWriteSectors(u32 lba, u32 nsectors, const void *buf)
{
    static u8 WriteBuffer[2 * 512 + sizeof(hddAtaTransfer_t)] ALIGNED(64); // Has to be a different buffer from IOBuffer (input can be in IOBuffer).
    int argsz;
    hddAtaTransfer_t *args = (hddAtaTransfer_t *)WriteBuffer;

    if (nsectors > 2) // Sanity check
        return -ENOMEM;

    args->lba = lba;
    args->size = nsectors;
    memcpy(args->data, buf, nsectors * 512);

    argsz = sizeof(hddAtaTransfer_t) + (nsectors * 512);

    if (fileXioDevctl("hdd0:", HDIOC_WRITESECTOR, args, argsz, NULL, 0) != 0)
        return -1;

    return 0;
}

//-------------------------------------------------------------------------
struct GameDataEntry
{
    u32 lba, size;
    struct GameDataEntry *next;
    char id[APA_IDMAX + 1];
};

static int hddGetHDLGameInfo(struct GameDataEntry *game, hdl_game_info_t *ginfo)
{
    int ret;

    APA_TRACE("APA_TRACE hddGetHDLGameInfo: read partition='%s' lba=%lu accumulated_size=%lu\n",
        game->id, (unsigned long)game->lba, (unsigned long)game->size);

    ret = hddReadSectors(game->lba, 2, IOBuffer);
    if (ret == 0) {

        hdl_apa_header *hdl_header = (hdl_apa_header *)IOBuffer;

        APA_TRACE("APA_TRACE hddGetHDLGameInfo: header partition='%s' magic=0x%08lx game='%s' startup='%s' discType=%lu parts=%d layer1=%lu\n",
            game->id, (unsigned long)hdl_header->magic, hdl_header->gamename, hdl_header->startup,
            (unsigned long)hdl_header->discType, hdl_header->num_partitions, (unsigned long)hdl_header->layer1_start);

        strncpy(ginfo->partition_name, game->id, APA_IDMAX);
        ginfo->partition_name[APA_IDMAX] = '\0';
        strncpy(ginfo->name, hdl_header->gamename, HDL_GAME_NAME_MAX);
        ginfo->name[HDL_GAME_NAME_MAX] = '\0';
        strncpy(ginfo->startup, hdl_header->startup, sizeof(ginfo->startup) - 1);
        ginfo->startup[sizeof(ginfo->startup) - 1] = '\0';
        ginfo->hdl_compat_flags = hdl_header->hdl_compat_flags;
        ginfo->ops2l_compat_flags = hdl_header->ops2l_compat_flags;
        ginfo->dma_type = hdl_header->dma_type;
        ginfo->dma_mode = hdl_header->dma_mode;
        ginfo->layer_break = hdl_header->layer1_start;
        ginfo->disctype = (u8)hdl_header->discType;
        ginfo->start_sector = game->lba;
        ginfo->total_size_in_kb = game->size * 2; // size * 2048 / 1024 = 2x
        APA_TRACE("APA_TRACE hddGetHDLGameInfo: created game partition='%s' name='%s' startup='%s' start_sector=%lu total_kb=%lu\n",
            ginfo->partition_name, ginfo->name, ginfo->startup,
            (unsigned long)ginfo->start_sector, (unsigned long)ginfo->total_size_in_kb);
    } else {
        APA_TRACE("APA_TRACE hddGetHDLGameInfo: reject partition='%s' reason=read_failed ret=%d lba=%lu\n",
            game->id, ret, (unsigned long)game->lba);
        ret = -1;
    }

    return ret;
}

//-------------------------------------------------------------------------
static struct GameDataEntry *GetGameListRecord(struct GameDataEntry *head, const char *partition)
{
    struct GameDataEntry *current;

    for (current = head; current != NULL; current = current->next) {
        if (!strncmp(current->id, partition, APA_IDMAX)) {
            return current;
        }
    }

    return NULL;
}

int hddGetHDLGamelist(hdl_games_list_t *game_list)
{
    struct GameDataEntry *head, *current, *next, *pGameEntry;
    unsigned int count, i;
    iox_dirent_t dirent;
    int dread, fd, ret;

    APA_TRACE("APA_TRACE hddGetHDLGamelist: begin output_list=%p existing_count=%lu\n",
        game_list, (unsigned long)game_list->count);
    hddFreeHDLGamelist(game_list);

    ret = 0;
    if ((fd = fileXioDopen("hdd0:")) >= 0) {
        APA_TRACE("APA_TRACE hddGetHDLGamelist: opened hdd0 fd=%d\n", fd);
        head = current = NULL;
        count = 0;
        while ((dread = fileXioDread(fd, &dirent)) > 0) {
            APA_TRACE("APA_TRACE hddGetHDLGamelist: partition name='%s' mode=0x%04x attr=0x%04x size=%lu lba=%lu\n",
                dirent.name, (unsigned int)dirent.stat.mode, (unsigned int)dirent.stat.attr,
                (unsigned long)dirent.stat.size, (unsigned long)dirent.stat.private_5);
            if (dirent.stat.mode == HDL_FS_MAGIC) {
                APA_TRACE("APA_TRACE hddGetHDLGamelist: accept partition='%s' reason=mode_matches_hdl_magic\n", dirent.name);
                if ((pGameEntry = GetGameListRecord(head, dirent.name)) == NULL) {
                    if (head == NULL) {
                        current = head = malloc(sizeof(struct GameDataEntry));
                    } else {
                        current = current->next = malloc(sizeof(struct GameDataEntry));
                    }

                    if (current == NULL) {
                        APA_TRACE("APA_TRACE hddGetHDLGamelist: reject partition='%s' reason=malloc_failed\n", dirent.name);
                        break;
                    }

                    strncpy(current->id, dirent.name, APA_IDMAX);
                    current->id[APA_IDMAX] = '\0';
                    count++;
                    current->next = NULL;
                    current->size = 0;
                    current->lba = 0;
                    pGameEntry = current;
                    APA_TRACE("APA_TRACE hddGetHDLGamelist: created record partition='%s' current_count=%lu\n",
                        current->id, (unsigned long)count);
                } else {
                    APA_TRACE("APA_TRACE hddGetHDLGamelist: merge partition='%s' into existing record\n", dirent.name);
                }

                if (!(dirent.stat.attr & APA_FLAG_SUB)) {
                    // Note: The APA specification states that there is a 4KB area used for storing the partition's information, before the extended attribute area.
                    pGameEntry->lba = dirent.stat.private_5 + (HDL_GAME_DATA_OFFSET + 4096) / 512;
                    APA_TRACE("APA_TRACE hddGetHDLGamelist: primary partition='%s' data_lba=%lu\n",
                        dirent.name, (unsigned long)pGameEntry->lba);
                } else {
                    APA_TRACE("APA_TRACE hddGetHDLGamelist: sub partition='%s' added_to_size\n", dirent.name);
                }

                pGameEntry->size += (dirent.stat.size / 4); // size in HDD sectors * (512 / 2048) = 0.25x
                APA_TRACE("APA_TRACE hddGetHDLGamelist: size partition='%s' accumulated_size=%lu\n",
                    pGameEntry->id, (unsigned long)pGameEntry->size);
            } else {
                APA_TRACE("APA_TRACE hddGetHDLGamelist: reject partition='%s' reason=mode_mismatch expected=0x%04x actual=0x%04x\n",
                    dirent.name, HDL_FS_MAGIC, (unsigned int)dirent.stat.mode);
            }
        }

        if (dread < 0)
            APA_TRACE("APA_TRACE hddGetHDLGamelist: dread_error ret=%d\n", dread);
        else
            APA_TRACE("APA_TRACE hddGetHDLGamelist: dread_complete ret=%d candidate_count=%lu\n", dread, (unsigned long)count);

        fileXioDclose(fd);

        if (head != NULL) {
            APA_TRACE("APA_TRACE hddGetHDLGamelist: allocate game_list count=%lu\n", (unsigned long)count);
            if ((game_list->games = malloc(sizeof(hdl_game_info_t) * count)) != NULL) {
                memset(game_list->games, 0, sizeof(hdl_game_info_t) * count);

                for (i = 0, current = head; i < count; i++, current = current->next) {
                    APA_TRACE("APA_TRACE hddGetHDLGamelist: populate index=%lu partition='%s' lba=%lu size=%lu\n",
                        (unsigned long)i, current->id, (unsigned long)current->lba, (unsigned long)current->size);
                    if ((ret = hddGetHDLGameInfo(current, &game_list->games[i])) != 0)
                        break;
                }

                if (ret) {
                    APA_TRACE("APA_TRACE hddGetHDLGamelist: discard list reason=game_info_failed ret=%d index=%lu\n",
                        ret, (unsigned long)i);
                    free(game_list->games);
                    game_list->games = NULL;
                } else {
                    game_list->count = count;
                    APA_TRACE("APA_TRACE hddGetHDLGamelist: final count=%lu\n", (unsigned long)game_list->count);
                }
            } else {
                ret = ENOMEM;
                APA_TRACE("APA_TRACE hddGetHDLGamelist: malloc game_list failed count=%lu\n", (unsigned long)count);
            }

            for (current = head; current != NULL; current = next) {
                next = current->next;
                free(current);
            }
        } else {
            APA_TRACE("APA_TRACE hddGetHDLGamelist: no HDL partitions accepted\n");
        }
    } else {
        ret = fd;
        APA_TRACE("APA_TRACE hddGetHDLGamelist: fileXioDopen hdd0 failed ret=%d\n", ret);
    }

    APA_TRACE("APA_TRACE hddGetHDLGamelist: end ret=%d final_count=%lu games=%p\n",
        ret, (unsigned long)game_list->count, game_list->games);
    return ret;
}

//-------------------------------------------------------------------------
void hddFreeHDLGamelist(hdl_games_list_t *game_list)
{
    if (game_list->games != NULL) {
        free(game_list->games);
        game_list->games = NULL;
        game_list->count = 0;
    }
}

//-------------------------------------------------------------------------
int hddSetHDLGameInfo(hdl_game_info_t *ginfo)
{
    if (hddReadSectors(ginfo->start_sector, 2, IOBuffer) != 0)
        return -EIO;

    hdl_apa_header *hdl_header = (hdl_apa_header *)IOBuffer;

    // just change game name and compat flags !!!
    strncpy(hdl_header->gamename, ginfo->name, sizeof(hdl_header->gamename));
    hdl_header->gamename[sizeof(hdl_header->gamename) - 1] = '\0';
    // hdl_header->hdl_compat_flags = ginfo->hdl_compat_flags;
    hdl_header->ops2l_compat_flags = ginfo->ops2l_compat_flags;
    hdl_header->dma_type = ginfo->dma_type;
    hdl_header->dma_mode = ginfo->dma_mode;

    if (hddWriteSectors(ginfo->start_sector, 2, IOBuffer) != 0)
        return -EIO;

    return 0;
}

//-------------------------------------------------------------------------
int hddDeleteHDLGame(hdl_game_info_t *ginfo)
{
    char path[38];

    LOG("HDD Delete game: '%s'\n", ginfo->name);

    sprintf(path, "hdd0:%s", ginfo->partition_name);

    return unlink(path);
}

//-------------------------------------------------------------------------
int hddGetPartitionInfo(const char *name, apa_sub_t *parts)
{
    u32 lba;
    iox_stat_t stat;
    apa_header_t *header;
    int result, i;

    if ((result = fileXioGetStat(name, &stat)) >= 0) {
        lba = stat.private_5;
        header = (apa_header_t *)IOBuffer;

        if (hddReadSectors(lba, sizeof(apa_header_t) / 512, header) == 0) {
            parts[0].start = header->start;
            parts[0].length = header->length;

            for (i = 0; i < header->nsub; i++)
                parts[1 + i] = header->subs[i];

            result = header->nsub + 1;
        } else
            result = -EIO;
    }

    return result;
}

//-------------------------------------------------------------------------
int hddGetFileBlockInfo(const char *name, const apa_sub_t *subs, pfs_blockinfo_t *blocks, int max)
{
    u32 lba;
    iox_stat_t stat;
    pfs_inode_t *inode;
    int result;

    if ((result = fileXioGetStat(name, &stat)) >= 0) {
        lba = subs[stat.private_4].start + stat.private_5;
        inode = (pfs_inode_t *)IOBuffer;

        if (hddReadSectors(lba, sizeof(pfs_inode_t) / 512, inode) == 0) {
            if (inode->number_data < max) {
                memcpy(blocks, inode->data, max * sizeof(pfs_blockinfo_t));
                result = inode->number_data;
            } else
                result = -ENOMEM;
        } else
            result = -EIO;
    }

    return result;
}
