#include "include/cheatman.h"

cheat_entry_t gCheats[MAX_CODES];

void InitCheatsConfig(config_set_t *configSet) {}
int GetCheatsEnabled(void) { return 0; }
const u32 *GetCheatsList(void) { return NULL; }
int load_cheats(const char *cheatfile) { return -1; }
void set_cheats_list(void) {}
