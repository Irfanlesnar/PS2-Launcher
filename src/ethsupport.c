#include "opl.h"
#include "include/ethsupport.h"

// Stubbed IRX symbol dependencies to satisfy linker without embedding binary objects
unsigned char smap_ingame_irx[1] = {0};
unsigned int size_smap_ingame_irx = 0;

unsigned char ingame_smstcpip_irx[1] = {0};
unsigned int size_ingame_smstcpip_irx = 0;

unsigned char smbinit_irx[1] = {0};
unsigned int size_smbinit_irx = 0;

unsigned char lwnbdsvr_irx[1] = {0};
unsigned int size_lwnbdsvr_irx = 0;

void ethInit(item_list_t *itemList) {}
void ethDeinitModules(void) {}
int ethLoadInitModules(void) { return -1; }
void ethDisplayErrorStatus(void) {}
int ethGetNetConfig(u8 *ip_address, u8 *netmask, u8 *gateway) { return 0; }
int ethApplyConfig(void) { return -1; }
int ethGetDHCPStatus(void) { return 0; }
item_list_t *ethGetObject(int initOnly) { return NULL; }
