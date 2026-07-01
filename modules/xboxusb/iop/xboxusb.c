#include "types.h"
#include "loadcore.h"
#include "sifrpc.h"
#include "stdio.h"
#include "sysclib.h"
#include "thbase.h"
#include "thsemap.h"
#include "usbd.h"
#include "usbd_macro.h"

IRX_ID("xboxusb", 1, 1);

#define XBOXUSB_BIND_RPC_ID 0x18E38791
#define XBOXUSB_GET_RAW     1

#define XBOX_VENDOR_MICROSOFT 0x045E
#define XBOXUSB_STATE_CONNECTED  0x01
#define XBOXUSB_STATE_CONFIGURED 0x02
#define XBOXUSB_STATE_RUNNING    0x04

#define RAW_REPORT_SIZE 64
#define RPC_REPORT_SIZE 72

typedef struct xboxusb_device
{
    int devId;
    int sema;
    int controlEndp;
    int interruptEndp;
    u16 vid;
    u16 pid;
    u8 status;
    u8 epIn;
    u8 packetSize;
    u8 reportLen;
    u8 report[RAW_REPORT_SIZE];
} xboxusb_device;

static xboxusb_device xboxpad;
static u8 usb_buf[RAW_REPORT_SIZE + 32] __attribute((aligned(4))) = {0};
static int usb_result = 1;
static int rpc_buf[RPC_REPORT_SIZE] __attribute((aligned(16)));

static int usb_probe(int devId);
static int usb_connect(int devId);
static int usb_disconnect(int devId);
static void usb_release(void);
static void usb_config_set(int result, int count, void *arg);
static void usb_data_cb(int resultCode, int bytes, void *arg);
static void xboxusb_get_raw(char *dst, int size);

static UsbDriver usb_driver = {NULL, NULL, "xboxusb", usb_probe, usb_connect, usb_disconnect};

static void xboxusb_memset(void *dst, int value, int size)
{
    u8 *out = dst;
    int i;

    for (i = 0; i < size; i++)
        out[i] = value;
}

static void xboxusb_memcpy(void *dst, const void *src, int size)
{
    u8 *out = dst;
    const u8 *in = src;
    int i;

    for (i = 0; i < size; i++)
        out[i] = in[i];
}

static int usb_probe(int devId)
{
    UsbDeviceDescriptor *device;

    device = (UsbDeviceDescriptor *)UsbGetDeviceStaticDescriptor(devId, NULL, USB_DT_DEVICE);
    if (device == NULL)
        return 0;

    return device->idVendor == XBOX_VENDOR_MICROSOFT;
}

static int usb_connect(int devId)
{
    UsbDeviceDescriptor *device;
    UsbConfigDescriptor *config;
    UsbInterfaceDescriptor *interface;
    UsbEndpointDescriptor *endpoint;
    int epCount;

    device = (UsbDeviceDescriptor *)UsbGetDeviceStaticDescriptor(devId, NULL, USB_DT_DEVICE);
    config = (UsbConfigDescriptor *)UsbGetDeviceStaticDescriptor(devId, device, USB_DT_CONFIG);
    if (device == NULL || config == NULL)
        return 1;

    WaitSema(xboxpad.sema);
    usb_release();

    xboxpad.devId = devId;
    xboxpad.vid = device->idVendor;
    xboxpad.pid = device->idProduct;
    xboxpad.status = XBOXUSB_STATE_CONNECTED;
    xboxpad.controlEndp = UsbOpenEndpoint(devId, NULL);

    interface = (UsbInterfaceDescriptor *)((char *)config + config->bLength);
    endpoint = (UsbEndpointDescriptor *)UsbGetDeviceStaticDescriptor(devId, NULL, USB_DT_ENDPOINT);
    epCount = interface != NULL ? interface->bNumEndpoints : 0;

    while (endpoint != NULL && epCount-- > 0) {
        if (endpoint->bmAttributes == USB_ENDPOINT_XFER_INT && (endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN) {
            xboxpad.interruptEndp = UsbOpenEndpointAligned(devId, endpoint);
            xboxpad.epIn = endpoint->bEndpointAddress;
            xboxpad.packetSize = endpoint->wMaxPacketSizeLB;
            break;
        }

        endpoint = (UsbEndpointDescriptor *)((char *)endpoint + endpoint->bLength);
    }

    if (xboxpad.controlEndp >= 0)
        UsbSetDeviceConfiguration(xboxpad.controlEndp, config->bConfigurationValue, usb_config_set, NULL);

    SignalSema(xboxpad.sema);
    return 0;
}

static int usb_disconnect(int devId)
{
    WaitSema(xboxpad.sema);

    if (xboxpad.devId == devId)
        usb_release();

    SignalSema(xboxpad.sema);
    return 0;
}

static void usb_release(void)
{
    if (xboxpad.interruptEndp >= 0)
        UsbCloseEndpoint(xboxpad.interruptEndp);
    if (xboxpad.controlEndp >= 0)
        UsbCloseEndpoint(xboxpad.controlEndp);

    xboxpad.devId = -1;
    xboxpad.controlEndp = -1;
    xboxpad.interruptEndp = -1;
    xboxpad.status = 0;
    xboxpad.epIn = 0;
    xboxpad.packetSize = 0;
    xboxpad.reportLen = 0;
    xboxusb_memset(xboxpad.report, 0, sizeof(xboxpad.report));
}

static void usb_config_set(int result, int count, void *arg)
{
    if (result == USB_RC_OK)
        xboxpad.status |= XBOXUSB_STATE_CONFIGURED | XBOXUSB_STATE_RUNNING;
}

static void usb_data_cb(int resultCode, int bytes, void *arg)
{
    usb_result = resultCode;
    SignalSema(xboxpad.sema);
}

static void xboxusb_get_raw(char *dst, int size)
{
    u8 *out = (u8 *)dst;
    int ret;
    int i;

    if (size < RPC_REPORT_SIZE)
        return;

    xboxusb_memset(dst, 0, size);

    WaitSema(xboxpad.sema);

    if (xboxpad.interruptEndp >= 0) {
        ret = UsbInterruptTransfer(xboxpad.interruptEndp, usb_buf, RAW_REPORT_SIZE, usb_data_cb, NULL);
        if (ret == USB_RC_OK) {
            WaitSema(xboxpad.sema);
            if (usb_result == USB_RC_OK) {
                xboxpad.reportLen = RAW_REPORT_SIZE;
                xboxusb_memcpy(xboxpad.report, usb_buf, RAW_REPORT_SIZE);
            }
            usb_result = 1;
        }
    }

    out[0] = xboxpad.vid & 0xFF;
    out[1] = (xboxpad.vid >> 8) & 0xFF;
    out[2] = xboxpad.pid & 0xFF;
    out[3] = (xboxpad.pid >> 8) & 0xFF;
    out[4] = xboxpad.status;
    out[5] = xboxpad.reportLen;
    out[6] = xboxpad.epIn;
    out[7] = xboxpad.packetSize;

    for (i = 0; i < RAW_REPORT_SIZE; i++)
        out[8 + i] = xboxpad.report[i];

    SignalSema(xboxpad.sema);
}

static void *rpc_server(int cmd, void *data, int size)
{
    switch (cmd) {
        case XBOXUSB_GET_RAW:
            xboxusb_get_raw((char *)data, RPC_REPORT_SIZE);
            break;
        default:
            break;
    }

    return data;
}

static void rpc_thread(void *arg)
{
    SifRpcDataQueue_t qd;
    SifRpcServerData_t sd;

    sceSifInitRpc(0);
    sceSifSetRpcQueue(&qd, GetThreadId());
    sceSifRegisterRpc(&sd, XBOXUSB_BIND_RPC_ID, rpc_server, rpc_buf, NULL, NULL, &qd);
    sceSifRpcLoop(&qd);
}

int _start(int argc, char *argv[])
{
    iop_thread_t thread;
    iop_sema_t sema;
    int tid;

    xboxusb_memset(&xboxpad, 0, sizeof(xboxpad));
    xboxpad.devId = -1;
    xboxpad.controlEndp = -1;
    xboxpad.interruptEndp = -1;

    sema.attr = 1;
    sema.initial = 1;
    sema.max = 1;
    sema.option = 0;
    xboxpad.sema = CreateSema(&sema);

    UsbRegisterDriver(&usb_driver);

    thread.attr = TH_C;
    thread.thread = rpc_thread;
    thread.priority = 40;
    thread.stacksize = 0x800;
    thread.option = 0;
    tid = CreateThread(&thread);
    if (tid > 0)
        StartThread(tid, NULL);

    return MODULE_RESIDENT_END;
}
