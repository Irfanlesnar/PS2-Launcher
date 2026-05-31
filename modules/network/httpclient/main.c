#include <errno.h>
#include <intrman.h>
#include <loadcore.h>
#include <sifcmd.h>
#include <sifman.h>
#include <stdio.h>
#include <sysclib.h>
#include <thbase.h>

#include <irx.h>

#include "httpclient.h"

IRX_ID("PS2HTTP10", 1, 1);

static SifRpcDataQueue_t SifQueueData;
static SifRpcServerData_t SifServerData;
static int RpcThreadID;
static int CoverApiWorkerThreadID;
static unsigned char SifServerRxBuffer[256] __attribute__((aligned(64)));
static unsigned char SifServerTxBuffer[16] __attribute__((aligned(64)));
static unsigned char DmaBuffer[512] __attribute__((aligned(64)));
static char CoverApiServer[HTTP_CLIENT_SERVER_NAME_MAX];
static char CoverApiUserAgent[HTTP_CLIENT_USER_AGENT_MAX];
static char CoverApiHost[HTTP_CLIENT_SERVER_NAME_MAX];
static char CoverApiUri[HTTP_CLIENT_URI_MAX];
static char CoverApiBuffer[512];
static u16 CoverApiResponseLen;
static u16 CoverApiPort;
static int CoverApiResult;
static volatile int CoverApiPending;
static volatile int CoverApiBusy;
static volatile int CoverApiDone;
static volatile int CoverApiPhase;

extern struct irx_export_table _exp_ps2h10;

static void CopyString(char *dst, int dstSize, const char *src)
{
    int i;

    if (src == NULL)
        src = "";

    for (i = 0; i < dstSize - 1 && src[i] != '\0'; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static void CoverApiWorkerThread(void *arg)
{
    int socket;
    s8 connMode;
    u16 length;

    while (1) {
        if (CoverApiPending) {
            CoverApiPending = 0;
            CoverApiResult = -EIO;
            CoverApiResponseLen = 0;

            CoverApiPhase = HTTP_COVER_PHASE_CONNECTING;
            socket = HttpEstabConnection(CoverApiServer, CoverApiPort);
            if (socket >= 0) {
                connMode = HTTP_CMODE_CLOSED;
                length = sizeof(CoverApiBuffer) - 1;
                CoverApiPhase = HTTP_COVER_PHASE_GET;
                CoverApiResult = HttpSendGetRequest(socket, CoverApiUserAgent, CoverApiHost, &connMode, NULL, CoverApiUri, CoverApiBuffer, &length);
                CoverApiPhase = HTTP_COVER_PHASE_CLOSE;
                HttpCloseConnection(socket);
                if (length >= sizeof(CoverApiBuffer))
                    length = sizeof(CoverApiBuffer) - 1;
                CoverApiBuffer[length] = '\0';
                CoverApiResponseLen = length;
            } else {
                CoverApiResult = socket;
            }

            CoverApiBusy = 0;
            CoverApiDone = 1;
            CoverApiPhase = HTTP_COVER_PHASE_DONE;
        }

        DelayThread(100000);
    }
}

static int StartCoverApiRequest(struct HttpClientCoverApiStartArgs *args)
{
    if (CoverApiWorkerThreadID <= 0)
        return -ENXIO;
    if (CoverApiBusy || CoverApiPending)
        return -EALREADY;

    CopyString(CoverApiServer, sizeof(CoverApiServer), args->server);
    CopyString(CoverApiUserAgent, sizeof(CoverApiUserAgent), args->UserAgent);
    CopyString(CoverApiHost, sizeof(CoverApiHost), args->host);
    CopyString(CoverApiUri, sizeof(CoverApiUri), args->uri);
    CoverApiPort = args->port;
    CoverApiResult = 0;
    CoverApiResponseLen = 0;
    CoverApiBuffer[0] = '\0';
    CoverApiDone = 0;
    CoverApiBusy = 1;
    CoverApiPhase = HTTP_COVER_PHASE_QUEUED;
    CoverApiPending = 1;

    return 0;
}

static void *SifRpc_handler(int fno, void *buffer, int nbytes)
{
    SifDmaTransfer_t dmat;
    int OldState;

    switch (fno) {
        case HTTP_CLIENT_CMD_CONN_ESTAB:
            *(int *)SifServerTxBuffer = HttpEstabConnection(((struct HttpClientConnEstabArgs *)buffer)->server, ((struct HttpClientConnEstabArgs *)buffer)->port);
            break;
        case HTTP_CLIENT_CMD_CONN_CLOSE:
            HttpCloseConnection(((struct HttpClientConnCloseArgs *)buffer)->socket);
            break;
        case HTTP_CLIENT_CMD_SEND_GET_REQ:
            if (((struct HttpClientSendGetArgs *)buffer)->out_len > sizeof(DmaBuffer)) {
                ((struct HttpClientSendGetArgs *)buffer)->out_len = sizeof(DmaBuffer);
            }

            ((struct HttpClientSendGetResult *)SifServerTxBuffer)->result = HttpSendGetRequest(((struct HttpClientSendGetArgs *)buffer)->socket, ((struct HttpClientSendGetArgs *)buffer)->UserAgent, ((struct HttpClientSendGetArgs *)buffer)->host, &((struct HttpClientSendGetArgs *)buffer)->mode, ((struct HttpClientSendGetArgs *)buffer)->hasMtime ? ((struct HttpClientSendGetArgs *)buffer)->mtime : NULL, ((struct HttpClientSendGetArgs *)buffer)->uri, (char *)DmaBuffer, &((struct HttpClientSendGetArgs *)buffer)->out_len);
            ((struct HttpClientSendGetResult *)SifServerTxBuffer)->mode = ((struct HttpClientSendGetArgs *)buffer)->mode;
            ((struct HttpClientSendGetResult *)SifServerTxBuffer)->out_len = ((struct HttpClientSendGetArgs *)buffer)->out_len;

            dmat.src = DmaBuffer;
            dmat.dest = ((struct HttpClientSendGetArgs *)buffer)->output;
            dmat.size = (((struct HttpClientSendGetArgs *)buffer)->out_len + 0xF) & ~0xF;
            dmat.attr = 0;

            CpuSuspendIntr(&OldState);
            while (sceSifSetDma(&dmat, 1) == 0)
                ;
            CpuResumeIntr(OldState);
            break;
        case HTTP_CLIENT_CMD_COVER_API_START_REQ:
            *(int *)SifServerTxBuffer = StartCoverApiRequest((struct HttpClientCoverApiStartArgs *)buffer);
            break;
        case HTTP_CLIENT_CMD_COVER_API_STATUS_REQ:
            if (((struct HttpClientCoverApiStatusArgs *)buffer)->out_len > sizeof(CoverApiBuffer))
                ((struct HttpClientCoverApiStatusArgs *)buffer)->out_len = sizeof(CoverApiBuffer);

            ((struct HttpClientCoverApiResult *)SifServerTxBuffer)->done = CoverApiDone;
            ((struct HttpClientCoverApiResult *)SifServerTxBuffer)->result = CoverApiDone ? CoverApiResult : 0;
            ((struct HttpClientCoverApiResult *)SifServerTxBuffer)->out_len = 0;
            ((struct HttpClientCoverApiResult *)SifServerTxBuffer)->phase = CoverApiPhase;

            if (CoverApiDone && CoverApiResponseLen > 0 && ((struct HttpClientCoverApiStatusArgs *)buffer)->output != NULL) {
                ((struct HttpClientCoverApiResult *)SifServerTxBuffer)->out_len = CoverApiResponseLen < ((struct HttpClientCoverApiStatusArgs *)buffer)->out_len ? CoverApiResponseLen : ((struct HttpClientCoverApiStatusArgs *)buffer)->out_len;

                dmat.src = CoverApiBuffer;
                dmat.dest = ((struct HttpClientCoverApiStatusArgs *)buffer)->output;
                dmat.size = (((struct HttpClientCoverApiResult *)SifServerTxBuffer)->out_len + 0xF) & ~0xF;
                dmat.attr = 0;

                CpuSuspendIntr(&OldState);
                while (sceSifSetDma(&dmat, 1) == 0)
                    ;
                CpuResumeIntr(OldState);
            }
            break;
        default:
            *(int *)SifServerTxBuffer = -ENXIO;
    }

    return SifServerTxBuffer;
}

static void RpcThread(void *arg)
{
    sceSifSetRpcQueue(&SifQueueData, GetThreadId());
    sceSifRegisterRpc(&SifServerData, HTTP_CLIENT_RPC_ID, &SifRpc_handler, SifServerRxBuffer, NULL, NULL, &SifQueueData);
    sceSifRpcLoop(&SifQueueData);
}

int _start(int argc, char *argv[])
{
    int result;
    iop_thread_t thread, coverThread;

    if (RegisterLibraryEntries(&_exp_ps2h10) == 0) {
        thread.attr = TH_C;
        thread.option = HTTP_CLIENT_RPC_ID;
        thread.thread = &RpcThread;
        thread.priority = 0x20;
        thread.stacksize = 0x2000;
        if ((RpcThreadID = CreateThread(&thread)) > 0) {
            StartThread(RpcThreadID, NULL);
            result = 0;

            coverThread.attr = TH_C;
            coverThread.option = 0;
            coverThread.thread = &CoverApiWorkerThread;
            coverThread.priority = 0x70;
            coverThread.stacksize = 0x3000;
            CoverApiWorkerThreadID = CreateThread(&coverThread);
            if (CoverApiWorkerThreadID > 0)
                StartThread(CoverApiWorkerThreadID, NULL);
        } else {
            result = RpcThreadID;
            ReleaseLibraryEntries(&_exp_ps2h10);
        }
    } else {
        result = -1;
    }

    return (result == 0 ? MODULE_RESIDENT_END : MODULE_NO_RESIDENT_END);
}

int _exit(int argc, char *argv[])
{
    ReleaseLibraryEntries(&_exp_ps2h10);

    if (CoverApiWorkerThreadID > 0) {
        TerminateThread(CoverApiWorkerThreadID);
        DeleteThread(CoverApiWorkerThreadID);
    }

    TerminateThread(RpcThreadID);
    DeleteThread(RpcThreadID);

    return MODULE_NO_RESIDENT_END;
}
