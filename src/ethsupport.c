#include "opl.h"
#include "include/ethsupport.h"
#include "include/extern_irx.h"
#include "include/lang.h"
#include "include/renderman.h"
#include "include/system.h"
#include "httpclient.h"

// Stubbed IRX symbol dependencies to satisfy linker without embedding binary objects
void *smap_ingame_irx[1] = {0};
int size_smap_ingame_irx = 0;

void *ingame_smstcpip_irx[1] = {0};
int size_ingame_smstcpip_irx = 0;

void *smbinit_irx[1] = {0};
int size_smbinit_irx = 0;

void *lwnbdsvr_irx[1] = {0};
int size_lwnbdsvr_irx = 0;

static unsigned char ethModulesLoaded = 0;
static struct ip4_addr lastIP;
static struct ip4_addr lastNM;
static struct ip4_addr lastGW;
static int ethInitSemaID = -1;

static int ethWaitValidNetIFLinkState(void);
static int ethWaitValidDHCPState(void);
static int ethGetNetIFLinkStatus(void);
static int ethApplyNetIFConfig(void);
static int ethApplyIPConfig(void);
static int ethReadNetConfig(void);

static int ethInitSema(void)
{
    if (ethInitSemaID < 0) {
        if ((ethInitSemaID = sbCreateSemaphore()) < 0)
            return ethInitSemaID;
    }

    return 0;
}

static void EthStatusCheckCb(s32 alarm_id, u16 time, void *common)
{
    iSignalSema(*(int *)common);
}

static int WaitValidNetState(int (*checkingFunction)(void))
{
    int semaID, retryCycles;
    ee_sema_t semaData;

    semaData.option = semaData.attr = 0;
    semaData.init_count = 0;
    semaData.max_count = 1;
    if ((semaID = CreateSema(&semaData)) < 0)
        return semaID;

    for (retryCycles = 0; checkingFunction() == 0; retryCycles++) {
        SetAlarm(1000 * rmGetHsync(), &EthStatusCheckCb, &semaID);
        WaitSema(semaID);

        if (retryCycles >= 30) {
            DeleteSema(semaID);
            return -1;
        }
    }

    DeleteSema(semaID);
    return 0;
}

static int ethWaitValidNetIFLinkState(void)
{
    return WaitValidNetState(&ethGetNetIFLinkStatus);
}

static int ethWaitValidDHCPState(void)
{
    return WaitValidNetState(&ethGetDHCPStatus);
}

static int ethInitApplyConfig(void)
{
    do {
        if (ethWaitValidNetIFLinkState() != 0) {
            gNetworkStartup = ERROR_ETH_LINK_FAIL;
            return ERROR_ETH_LINK_FAIL;
        }
    } while (ethApplyNetIFConfig() != 0);

    if (ethWaitValidNetIFLinkState() != 0) {
        gNetworkStartup = ERROR_ETH_LINK_FAIL;
        return ERROR_ETH_LINK_FAIL;
    }

    ethApplyIPConfig();

    if (ps2_ip_use_dhcp && (ethWaitValidDHCPState() != 0)) {
        gNetworkStartup = ERROR_ETH_DHCP_FAIL;
        return ERROR_ETH_DHCP_FAIL;
    }

    return 0;
}

static int ethLoadModules(void)
{
    if (!ethModulesLoaded) {
        ethModulesLoaded = 1;

        sysInitDev9();

        if (sysLoadModuleBuffer(&netman_irx, size_netman_irx, 0, NULL) >= 0) {
            NetManInit();
            sysLoadModuleBuffer(&smsutils_irx, size_smsutils_irx, 0, NULL);
            if (sysLoadModuleBuffer(&smap_irx, size_smap_irx, 0, NULL) >= 0) {
                ethApplyNetIFConfig();
                if (sysLoadModuleBuffer(&ps2ip_irx, size_ps2ip_irx, 0, NULL) >= 0) {
                    sysLoadModuleBuffer(&ps2ips_irx, size_ps2ips_irx, 0, NULL);
                    sysLoadModuleBuffer(&httpclient_irx, size_httpclient_irx, 0, NULL);
                    ps2ip_init();
                    HttpInit();
                    return 0;
                }
            }
        }

        ethModulesLoaded = 0;
        gNetworkStartup = ERROR_ETH_MODULE_NETIF_FAILURE;
        return -1;
    }

    return 0;
}

void ethInit(item_list_t *itemList) {}
void ethDeinitModules(void)
{
    if (ethModulesLoaded) {
        if (ethInitSemaID >= 0)
            WaitSema(ethInitSemaID);

        HttpDeinit();
        NetManDeinit();
        ethModulesLoaded = 0;
        gNetworkStartup = ERROR_ETH_NOT_STARTED;

        if (ethInitSemaID >= 0) {
            DeleteSema(ethInitSemaID);
            ethInitSemaID = -1;
        }

        ethReadNetConfig();
        ps2ip_deinit();
    }
}

int ethLoadInitModules(void)
{
    int ret;

    if ((ret = ethInitSema()) < 0)
        return ret;

    WaitSema(ethInitSemaID);

    if ((ret = ethLoadModules()) == 0)
        ret = ethInitApplyConfig();

    SignalSema(ethInitSemaID);
    return ret;
}

void ethDisplayErrorStatus(void)
{
    switch (gNetworkStartup) {
        case 0:
            break;
        case ERROR_ETH_MODULE_NETIF_FAILURE:
            setErrorMessageWithCode(_STR_NETWORK_STARTUP_ERROR_NETIF, gNetworkStartup);
            break;
        case ERROR_ETH_LINK_FAIL:
            setErrorMessageWithCode(_STR_NETWORK_ERROR_LINK_FAIL, gNetworkStartup);
            break;
        case ERROR_ETH_DHCP_FAIL:
            setErrorMessageWithCode(_STR_NETWORK_ERROR_DHCP_FAIL, gNetworkStartup);
            break;
        default:
            setErrorMessageWithCode(_STR_NETWORK_STARTUP_ERROR, gNetworkStartup);
    }
}

int ethGetNetConfig(u8 *ip_address, u8 *netmask, u8 *gateway)
{
    int result;

    result = ethModulesLoaded ? ethReadNetConfig() : -1;
    ip_address[0] = ip4_addr1(&lastIP);
    ip_address[1] = ip4_addr2(&lastIP);
    ip_address[2] = ip4_addr3(&lastIP);
    ip_address[3] = ip4_addr4(&lastIP);

    netmask[0] = ip4_addr1(&lastNM);
    netmask[1] = ip4_addr2(&lastNM);
    netmask[2] = ip4_addr3(&lastNM);
    netmask[3] = ip4_addr4(&lastNM);

    gateway[0] = ip4_addr1(&lastGW);
    gateway[1] = ip4_addr2(&lastGW);
    gateway[2] = ip4_addr3(&lastGW);
    gateway[3] = ip4_addr4(&lastGW);

    return result;
}

int ethApplyConfig(void)
{
    int ret;

    if ((ret = ethInitSema()) < 0)
        return ret;

    WaitSema(ethInitSemaID);
    ret = ethInitApplyConfig();
    SignalSema(ethInitSemaID);

    return ret;
}

static int ethApplyNetIFConfig(void)
{
    int mode, result;
    static int currentMode = NETMAN_NETIF_ETH_LINK_MODE_AUTO;

    switch (gETHOpMode) {
        case ETH_OP_MODE_100M_FDX:
            mode = NETMAN_NETIF_ETH_LINK_MODE_100M_FDX;
            break;
        case ETH_OP_MODE_100M_HDX:
            mode = NETMAN_NETIF_ETH_LINK_MODE_100M_HDX;
            break;
        case ETH_OP_MODE_10M_FDX:
            mode = NETMAN_NETIF_ETH_LINK_MODE_10M_FDX;
            break;
        case ETH_OP_MODE_10M_HDX:
            mode = NETMAN_NETIF_ETH_LINK_MODE_10M_HDX;
            break;
        default:
            mode = NETMAN_NETIF_ETH_LINK_MODE_AUTO;
    }

    if (currentMode != mode) {
        if ((result = NetManSetLinkMode(mode)) == 0)
            currentMode = mode;
    } else
        result = 0;

    return result;
}

static int ethGetNetIFLinkStatus(void)
{
    return (NetManIoctl(NETMAN_NETIF_IOCTL_GET_LINK_STATUS, NULL, 0, NULL, 0) == NETMAN_NETIF_ETH_LINK_STATE_UP);
}

static int ethApplyIPConfig(void)
{
    t_ip_info ip_info;
    struct ip4_addr ipaddr, netmask, gw, dns;
    const struct ip4_addr *dnsCurrent;
    int result;

    if ((result = ps2ip_getconfig("sm0", &ip_info)) >= 0) {
        IP4_ADDR(&ipaddr, ps2_ip[0], ps2_ip[1], ps2_ip[2], ps2_ip[3]);
        IP4_ADDR(&netmask, ps2_netmask[0], ps2_netmask[1], ps2_netmask[2], ps2_netmask[3]);
        IP4_ADDR(&gw, ps2_gateway[0], ps2_gateway[1], ps2_gateway[2], ps2_gateway[3]);
        IP4_ADDR(&dns, ps2_dns[0], ps2_dns[1], ps2_dns[2], ps2_dns[3]);
        dnsCurrent = dns_getserver(0);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
        if ((ps2_ip_use_dhcp != ip_info.dhcp_enabled) || (!ps2_ip_use_dhcp &&
                                                          (!ip_addr_cmp(&ipaddr, (struct ip4_addr *)&ip_info.ipaddr) ||
                                                           !ip_addr_cmp(&netmask, (struct ip4_addr *)&ip_info.netmask) ||
                                                           !ip_addr_cmp(&gw, (struct ip4_addr *)&ip_info.gw) ||
                                                           !ip_addr_cmp(&dns, dnsCurrent)))) {
            if (ps2_ip_use_dhcp) {
                ip4_addr_set_zero((struct ip4_addr *)&ip_info.ipaddr);
                ip4_addr_set_zero((struct ip4_addr *)&ip_info.netmask);
                ip4_addr_set_zero((struct ip4_addr *)&ip_info.gw);
                ip4_addr_set_zero(&dns);
                ip_info.dhcp_enabled = 1;
            } else {
                ip_addr_set((struct ip4_addr *)&ip_info.ipaddr, &ipaddr);
                ip_addr_set((struct ip4_addr *)&ip_info.netmask, &netmask);
                ip_addr_set((struct ip4_addr *)&ip_info.gw, &gw);
                ip_info.dhcp_enabled = 0;
            }

            result = ps2ip_setconfig(&ip_info);
            if (!ps2_ip_use_dhcp)
                dns_setserver(0, &dns);
        } else
            result = 0;
#pragma GCC diagnostic pop
    }

    return result;
}

int ethGetDHCPStatus(void)
{
    t_ip_info ip_info;
    int result;

    if ((result = ps2ip_getconfig("sm0", &ip_info)) >= 0) {
        if (ip_info.dhcp_enabled)
            result = (ip_info.dhcp_status == DHCP_STATE_BOUND || (ip_info.dhcp_status == DHCP_STATE_OFF));
        else
            result = -1;
    }

    return result;
}

static int ethReadNetConfig(void)
{
    t_ip_info ip_info;
    int result;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
    if ((result = ps2ip_getconfig("sm0", &ip_info)) >= 0) {
        lastIP = *(struct ip4_addr *)&ip_info.ipaddr;
        lastNM = *(struct ip4_addr *)&ip_info.netmask;
        lastGW = *(struct ip4_addr *)&ip_info.gw;
    } else {
        ip4_addr_set_zero(&lastIP);
        ip4_addr_set_zero(&lastNM);
        ip4_addr_set_zero(&lastGW);
    }
#pragma GCC diagnostic pop

    return result;
}

int ethCheckInternet(void)
{
    int socket;

    if (ethLoadInitModules() != 0)
        return -1;

    socket = HttpEstabConnection("1.1.1.1", 80);
    if (socket < 0)
        return -1;

    HttpCloseConnection(socket);
    return 0;
}
item_list_t *ethGetObject(int initOnly) { return NULL; }
