#include "types.h"
#include "loadcore.h"
#include "sifrpc.h"
#include "stdio.h"
#include "sysclib.h"
#include "thbase.h"
#include "thsemap.h"
#include "usbd.h"
#include "usbd_macro.h"
#include "ds34common.h"

IRX_ID("xboxusb", 1, 1);

#define XBOXUSB_BIND_RPC_ID 0x18E38791
#define XBOXUSB_GET_RAW     1
#define XBOXUSB_GET_DATA    2

#define XBOX_VENDOR_MICROSOFT 0x045E
#define XBOXUSB_STATE_CONNECTED  0x01
#define XBOXUSB_STATE_CONFIGURED 0x02
#define XBOXUSB_STATE_RUNNING    0x04
#define XBOXUSB_STATE_INIT_SENT  0x08

#define RAW_REPORT_SIZE 64
#define RPC_REPORT_SIZE 72
#define XBOXUSB_INPUT_PACKET 0x20

typedef struct xboxusb_device
{
    int devId;
    int sema;
    int transferSema;
    int controlEndp;
    int interruptEndp;
    int interruptOutEndp;
    u16 vid;
    u16 pid;
    u8 status;
    u8 epIn;
    u8 epOut;
    u8 packetSize;
    u8 reportLen;
    u8 seq;
    u8 report[RAW_REPORT_SIZE];
    struct ds2report ds2;
} xboxusb_device;

static xboxusb_device xboxpad;
static u8 usb_buf[RAW_REPORT_SIZE + 32] __attribute((aligned(4))) = {0};
static int usb_result = 1;
static int rpc_buf[RPC_REPORT_SIZE] __attribute((aligned(16)));

static const u8 xboxone_power_on[] = {0x05, 0x20, 0x00, 0x01, 0x00};
static const u8 xboxone_s_init[] = {0x05, 0x20, 0x00, 0x0F, 0x06};
static const u8 xboxone_led_on[] = {0x0A, 0x20, 0x00, 0x03, 0x00, 0x01, 0x14};
static const u8 xboxone_auth_done[] = {0x06, 0x20, 0x00, 0x02, 0x01, 0x00};

static int usb_probe(int devId);
static int usb_connect(int devId);
static int usb_disconnect(int devId);
static void xboxusb_reset_ds2(void);
static void usb_release(void);
static void usb_config_set(int result, int count, void *arg);
static void usb_data_cb(int resultCode, int bytes, void *arg);
static void xboxusb_reset_transfer(void);
static int xboxusb_wait_transfer(int timeoutMs);
static int xboxusb_send_packet(const u8 *data, int len);
static void xboxusb_send_init(void);
static void xboxusb_poll_thread(void *arg);
static int xboxusb_axis_to_ds2(u8 low, u8 high);
static void xboxusb_translate_input(const u8 *in, struct ds2report *out);
static void xboxusb_get_raw(char *dst, int size);
static void xboxusb_get_data(char *dst, int size);

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
    xboxusb_reset_ds2();

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
        } else if (endpoint->bmAttributes == USB_ENDPOINT_XFER_INT && (endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT) {
            xboxpad.interruptOutEndp = UsbOpenEndpointAligned(devId, endpoint);
            xboxpad.epOut = endpoint->bEndpointAddress;
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

static void xboxusb_reset_ds2(void)
{
    xboxpad.ds2.nButtonState = 0xFFFF;
    xboxpad.ds2.RightStickX = 0x7F;
    xboxpad.ds2.RightStickY = 0x7F;
    xboxpad.ds2.LeftStickX = 0x7F;
    xboxpad.ds2.LeftStickY = 0x7F;
    xboxpad.ds2.PressureRight = 0;
    xboxpad.ds2.PressureLeft = 0;
    xboxpad.ds2.PressureUp = 0;
    xboxpad.ds2.PressureDown = 0;
    xboxpad.ds2.PressureTriangle = 0;
    xboxpad.ds2.PressureCircle = 0;
    xboxpad.ds2.PressureCross = 0;
    xboxpad.ds2.PressureSquare = 0;
    xboxpad.ds2.PressureL1 = 0;
    xboxpad.ds2.PressureR1 = 0;
    xboxpad.ds2.PressureL2 = 0;
    xboxpad.ds2.PressureR2 = 0;
}

static void usb_release(void)
{
    if (xboxpad.interruptEndp >= 0)
        UsbCloseEndpoint(xboxpad.interruptEndp);
    if (xboxpad.interruptOutEndp >= 0)
        UsbCloseEndpoint(xboxpad.interruptOutEndp);
    if (xboxpad.controlEndp >= 0)
        UsbCloseEndpoint(xboxpad.controlEndp);

    xboxpad.devId = -1;
    xboxpad.controlEndp = -1;
    xboxpad.interruptEndp = -1;
    xboxpad.interruptOutEndp = -1;
    xboxpad.status = 0;
    xboxpad.epIn = 0;
    xboxpad.epOut = 0;
    xboxpad.packetSize = 0;
    xboxpad.reportLen = 0;
    xboxpad.seq = 0;
    xboxusb_memset(xboxpad.report, 0, sizeof(xboxpad.report));
    xboxusb_reset_ds2();
    xboxusb_reset_transfer();
}

static void usb_config_set(int result, int count, void *arg)
{
    if (result == USB_RC_OK)
        xboxpad.status |= XBOXUSB_STATE_CONFIGURED | XBOXUSB_STATE_RUNNING;
}

static void usb_data_cb(int resultCode, int bytes, void *arg)
{
    usb_result = resultCode;
    SignalSema(xboxpad.transferSema);
}

static void xboxusb_reset_transfer(void)
{
    while (PollSema(xboxpad.transferSema) == 0) {
    }
    usb_result = 1;
}

static int xboxusb_wait_transfer(int timeoutMs)
{
    int i;

    for (i = 0; i < timeoutMs; i++) {
        if (PollSema(xboxpad.transferSema) == 0)
            return 1;
        DelayThread(1000);
    }

    return 0;
}

static int xboxusb_send_packet(const u8 *data, int len)
{
    int ret;

    if (xboxpad.interruptOutEndp < 0 || len <= 0 || len > RAW_REPORT_SIZE)
        return 0;

    xboxusb_memset(usb_buf, 0, sizeof(usb_buf));
    xboxusb_memcpy(usb_buf, data, len);
    usb_buf[2] = xboxpad.seq++;
    xboxusb_reset_transfer();

    ret = UsbInterruptTransfer(xboxpad.interruptOutEndp, usb_buf, len, usb_data_cb, NULL);
    if (ret != USB_RC_OK)
        return 0;

    if (!xboxusb_wait_transfer(250))
        return 0;

    ret = usb_result == USB_RC_OK;
    usb_result = 1;

    return ret;
}

static void xboxusb_send_init(void)
{
    if ((xboxpad.status & XBOXUSB_STATE_CONFIGURED) == 0 || (xboxpad.status & XBOXUSB_STATE_INIT_SENT))
        return;

    xboxusb_send_packet(xboxone_power_on, sizeof(xboxone_power_on));
    if (xboxpad.vid == 0x045E && xboxpad.pid == 0x02EA)
        xboxusb_send_packet(xboxone_s_init, sizeof(xboxone_s_init));
    xboxusb_send_packet(xboxone_led_on, sizeof(xboxone_led_on));
    xboxusb_send_packet(xboxone_auth_done, sizeof(xboxone_auth_done));

    xboxpad.status |= XBOXUSB_STATE_INIT_SENT;
}

static void xboxusb_poll_thread(void *arg)
{
    int ret;

    while (1) {
        if (xboxpad.interruptEndp >= 0 && (xboxpad.status & XBOXUSB_STATE_CONFIGURED)) {
            xboxusb_send_init();
            xboxusb_reset_transfer();

            ret = UsbInterruptTransfer(xboxpad.interruptEndp, usb_buf, RAW_REPORT_SIZE, usb_data_cb, NULL);
            if (ret == USB_RC_OK) {
                if (!xboxusb_wait_transfer(250)) {
                    usb_result = 1;
                    continue;
                }

                if (usb_result == USB_RC_OK && usb_buf[0] == XBOXUSB_INPUT_PACKET) {
                    WaitSema(xboxpad.sema);
                    xboxpad.reportLen = RAW_REPORT_SIZE;
                    xboxusb_memcpy(xboxpad.report, usb_buf, RAW_REPORT_SIZE);
                    xboxusb_translate_input(xboxpad.report, &xboxpad.ds2);
                    SignalSema(xboxpad.sema);
                }
                usb_result = 1;
            }
        } else {
            DelayThread(10000);
        }
    }
}

static int xboxusb_axis_to_ds2(u8 low, u8 high)
{
    int value = (short)((high << 8) | low);

    if (value > -4096 && value < 4096)
        value = 0;

    value = (value >> 8) + 128;
    if (value < 0)
        value = 0;
    if (value > 255)
        value = 255;

    return value;
}

static void xboxusb_translate_input(const u8 *in, struct ds2report *out)
{
    u16 buttons = 0;
    u16 lt = in[6] | (in[7] << 8);
    u16 rt = in[8] | (in[9] << 8);

    if (in[5] & 0x01)
        buttons |= DS2ButtonUp;
    if (in[5] & 0x02)
        buttons |= DS2ButtonDown;
    if (in[5] & 0x04)
        buttons |= DS2ButtonLeft;
    if (in[5] & 0x08)
        buttons |= DS2ButtonRight;
    if (in[5] & 0x10)
        buttons |= DS2ButtonL1;
    if (in[5] & 0x20)
        buttons |= DS2ButtonR1;
    if (in[5] & 0x40)
        buttons |= DS2ButtonL3;
    if (in[5] & 0x80)
        buttons |= DS2ButtonR3;

    if (in[4] & 0x04)
        buttons |= DS2ButtonStart;
    if (in[4] & 0x08)
        buttons |= DS2ButtonSelect;
    if (in[4] & 0x10)
        buttons |= DS2ButtonCross;
    if (in[4] & 0x20)
        buttons |= DS2ButtonCircle;
    if (in[4] & 0x40)
        buttons |= DS2ButtonSquare;
    if (in[4] & 0x80)
        buttons |= DS2ButtonTriangle;

    if (lt > 0)
        buttons |= DS2ButtonL2;
    if (rt > 0)
        buttons |= DS2ButtonR2;

    out->nButtonState = ~buttons;
    out->LeftStickX = xboxusb_axis_to_ds2(in[10], in[11]);
    out->LeftStickY = 255 - xboxusb_axis_to_ds2(in[12], in[13]);
    out->RightStickX = xboxusb_axis_to_ds2(in[14], in[15]);
    out->RightStickY = 255 - xboxusb_axis_to_ds2(in[16], in[17]);

    out->PressureUp = (buttons & DS2ButtonUp) ? 0xFF : 0x00;
    out->PressureDown = (buttons & DS2ButtonDown) ? 0xFF : 0x00;
    out->PressureLeft = (buttons & DS2ButtonLeft) ? 0xFF : 0x00;
    out->PressureRight = (buttons & DS2ButtonRight) ? 0xFF : 0x00;
    out->PressureTriangle = (buttons & DS2ButtonTriangle) ? 0xFF : 0x00;
    out->PressureCircle = (buttons & DS2ButtonCircle) ? 0xFF : 0x00;
    out->PressureCross = (buttons & DS2ButtonCross) ? 0xFF : 0x00;
    out->PressureSquare = (buttons & DS2ButtonSquare) ? 0xFF : 0x00;
    out->PressureL1 = (buttons & DS2ButtonL1) ? 0xFF : 0x00;
    out->PressureR1 = (buttons & DS2ButtonR1) ? 0xFF : 0x00;
    out->PressureL2 = lt > 0x3FF ? 0xFF : (lt >> 2);
    out->PressureR2 = rt > 0x3FF ? 0xFF : (rt >> 2);
}

static void xboxusb_get_raw(char *dst, int size)
{
    u8 *out = (u8 *)dst;
    int i;

    if (size < RPC_REPORT_SIZE)
        return;

    xboxusb_memset(dst, 0, size);

    WaitSema(xboxpad.sema);

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

static void xboxusb_get_data(char *dst, int size)
{
    if (size < 18)
        return;

    WaitSema(xboxpad.sema);
    xboxusb_memcpy(dst, &xboxpad.ds2, 18);
    SignalSema(xboxpad.sema);
}

static void *rpc_server(int cmd, void *data, int size)
{
    switch (cmd) {
        case XBOXUSB_GET_RAW:
            xboxusb_get_raw((char *)data, RPC_REPORT_SIZE);
            break;
        case XBOXUSB_GET_DATA:
            xboxusb_get_data((char *)data, 18);
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
    xboxpad.interruptOutEndp = -1;
    xboxusb_reset_ds2();

    sema.attr = 1;
    sema.initial = 1;
    sema.max = 1;
    sema.option = 0;
    xboxpad.sema = CreateSema(&sema);

    sema.initial = 0;
    xboxpad.transferSema = CreateSema(&sema);

    UsbRegisterDriver(&usb_driver);

    thread.attr = TH_C;
    thread.thread = rpc_thread;
    thread.priority = 40;
    thread.stacksize = 0x800;
    thread.option = 0;
    tid = CreateThread(&thread);
    if (tid > 0)
        StartThread(tid, NULL);

    thread.thread = xboxusb_poll_thread;
    thread.priority = 39;
    thread.stacksize = 0x800;
    tid = CreateThread(&thread);
    if (tid > 0)
        StartThread(tid, NULL);

    return MODULE_RESIDENT_END;
}
