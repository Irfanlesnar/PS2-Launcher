#include <string.h>
#include <errno.h>
#include <kernel.h>
#include <sifrpc.h>
#include "httpclient.h"
#include "ioman.h"

static SifRpcClientData_t SifRpcClient;
static unsigned char RpcTxBuffer[sizeof(struct HttpClientSendPostJsonArgs)] ALIGNED(64);
static unsigned char RpcRxBuffer[64] ALIGNED(64);
static int LastStatusCode;
static int LastContentLength;
static int LastRequestLength;
static int LastSendLength;
static int LastSelectResult;
static int LastRecvResult;
static int LastSocketError;

static void HttpAsyncRpcEnd(void *arg)
{
    (void)arg;
}

int HttpInit(void)
{
    while (SifBindRpc(&SifRpcClient, HTTP_CLIENT_RPC_ID, 0) < 0 || SifRpcClient.server == NULL) {
        nopdelay();
    }

    return 0;
}

void HttpDeinit(void)
{
    memset(&SifRpcClient, 0, sizeof(SifRpcClientData_t));
}

int HttpGetLastStatusCode(void)
{
    return LastStatusCode;
}

int HttpGetLastContentLength(void)
{
    return LastContentLength;
}

int HttpGetLastRequestLength(void)
{
    return LastRequestLength;
}

int HttpGetLastSendLength(void)
{
    return LastSendLength;
}

int HttpGetLastSelectResult(void)
{
    return LastSelectResult;
}

int HttpGetLastRecvResult(void)
{
    return LastRecvResult;
}

int HttpGetLastSocketError(void)
{
    return LastSocketError;
}

int HttpEstabConnection(char *server, u16 port)
{
    int result;

    strncpy(((struct HttpClientConnEstabArgs *)RpcTxBuffer)->server, server, HTTP_CLIENT_SERVER_NAME_MAX - 1);
    ((struct HttpClientConnEstabArgs *)RpcTxBuffer)->server[HTTP_CLIENT_SERVER_NAME_MAX - 1] = '\0';
    ((struct HttpClientConnEstabArgs *)RpcTxBuffer)->port = port;

    if ((result = SifCallRpc(&SifRpcClient, HTTP_CLIENT_CMD_CONN_ESTAB, 0, RpcTxBuffer, sizeof(struct HttpClientConnEstabArgs), RpcRxBuffer, sizeof(s32), NULL, NULL)) >= 0)
        result = *(s32 *)RpcRxBuffer;

    return result;
}

void HttpCloseConnection(s32 HttpSocket)
{
    ((struct HttpClientConnCloseArgs *)RpcTxBuffer)->socket = HttpSocket;
    SifCallRpc(&SifRpcClient, HTTP_CLIENT_CMD_CONN_CLOSE, 0, RpcTxBuffer, sizeof(struct HttpClientConnCloseArgs), NULL, 0, NULL, NULL);
}

int HttpSendGetRequest(s32 HttpSocket, const char *UserAgent, const char *host, s8 *mode, const u8 *mtime, const char *uri, char *output, u16 *out_len)
{
    int result;

    ((struct HttpClientSendGetArgs *)RpcTxBuffer)->socket = HttpSocket;
    strncpy(((struct HttpClientSendGetArgs *)RpcTxBuffer)->UserAgent, UserAgent, HTTP_CLIENT_USER_AGENT_MAX - 1);
    ((struct HttpClientSendGetArgs *)RpcTxBuffer)->UserAgent[HTTP_CLIENT_USER_AGENT_MAX - 1] = '\0';
    strncpy(((struct HttpClientSendGetArgs *)RpcTxBuffer)->host, host, HTTP_CLIENT_SERVER_NAME_MAX - 1);
    ((struct HttpClientSendGetArgs *)RpcTxBuffer)->host[HTTP_CLIENT_SERVER_NAME_MAX - 1] = '\0';
    ((struct HttpClientSendGetArgs *)RpcTxBuffer)->mode = *mode;
    if (mtime != NULL) {
        memcpy(((struct HttpClientSendGetArgs *)RpcTxBuffer)->mtime, mtime, sizeof(((struct HttpClientSendGetArgs *)RpcTxBuffer)->mtime));
        ((struct HttpClientSendGetArgs *)RpcTxBuffer)->hasMtime = 1;
    } else {
        memset(((struct HttpClientSendGetArgs *)RpcTxBuffer)->mtime, 0, sizeof(((struct HttpClientSendGetArgs *)RpcTxBuffer)->mtime));
        ((struct HttpClientSendGetArgs *)RpcTxBuffer)->hasMtime = 0;
    }
    strncpy(((struct HttpClientSendGetArgs *)RpcTxBuffer)->uri, uri, HTTP_CLIENT_URI_MAX - 1);
    ((struct HttpClientSendGetArgs *)RpcTxBuffer)->uri[HTTP_CLIENT_URI_MAX - 1] = '\0';
    ((struct HttpClientSendGetArgs *)RpcTxBuffer)->output = output;
    ((struct HttpClientSendGetArgs *)RpcTxBuffer)->out_len = *out_len;

    if (!IS_UNCACHED_SEG(output))
        SifWriteBackDCache(output, *out_len);

    if ((result = SifCallRpc(&SifRpcClient, HTTP_CLIENT_CMD_SEND_GET_REQ, 0, RpcTxBuffer, sizeof(struct HttpClientSendGetArgs), RpcRxBuffer, sizeof(struct HttpClientSendGetResult), NULL, NULL)) >= 0) {
        result = ((struct HttpClientSendGetResult *)RpcRxBuffer)->result;
        LastStatusCode = ((struct HttpClientSendGetResult *)RpcRxBuffer)->statusCode;
        LastContentLength = ((struct HttpClientSendGetResult *)RpcRxBuffer)->contentLength;
        LastRequestLength = ((struct HttpClientSendGetResult *)RpcRxBuffer)->requestLength;
        LastSendLength = ((struct HttpClientSendGetResult *)RpcRxBuffer)->sendLength;
        LastSelectResult = ((struct HttpClientSendGetResult *)RpcRxBuffer)->selectResult;
        LastRecvResult = ((struct HttpClientSendGetResult *)RpcRxBuffer)->recvResult;
        LastSocketError = ((struct HttpClientSendGetResult *)RpcRxBuffer)->socketError;
        *mode = ((struct HttpClientSendGetResult *)RpcRxBuffer)->mode;
        *out_len = ((struct HttpClientSendGetResult *)RpcRxBuffer)->out_len;
    }

    return result;
}

int HttpSendGetRequestRange(s32 HttpSocket, const char *UserAgent, const char *host, s8 *mode, const char *uri, u32 rangeStart, u32 rangeEnd, char *output, u16 *out_len)
{
    int result;

    ((struct HttpClientSendGetRangeArgs *)RpcTxBuffer)->socket = HttpSocket;
    strncpy(((struct HttpClientSendGetRangeArgs *)RpcTxBuffer)->UserAgent, UserAgent, HTTP_CLIENT_USER_AGENT_MAX - 1);
    ((struct HttpClientSendGetRangeArgs *)RpcTxBuffer)->UserAgent[HTTP_CLIENT_USER_AGENT_MAX - 1] = '\0';
    strncpy(((struct HttpClientSendGetRangeArgs *)RpcTxBuffer)->host, host, HTTP_CLIENT_SERVER_NAME_MAX - 1);
    ((struct HttpClientSendGetRangeArgs *)RpcTxBuffer)->host[HTTP_CLIENT_SERVER_NAME_MAX - 1] = '\0';
    ((struct HttpClientSendGetRangeArgs *)RpcTxBuffer)->mode = *mode;
    strncpy(((struct HttpClientSendGetRangeArgs *)RpcTxBuffer)->uri, uri, HTTP_CLIENT_URI_MAX - 1);
    ((struct HttpClientSendGetRangeArgs *)RpcTxBuffer)->uri[HTTP_CLIENT_URI_MAX - 1] = '\0';
    ((struct HttpClientSendGetRangeArgs *)RpcTxBuffer)->rangeStart = rangeStart;
    ((struct HttpClientSendGetRangeArgs *)RpcTxBuffer)->rangeEnd = rangeEnd;
    ((struct HttpClientSendGetRangeArgs *)RpcTxBuffer)->output = output;
    ((struct HttpClientSendGetRangeArgs *)RpcTxBuffer)->out_len = *out_len;

    if (!IS_UNCACHED_SEG(output))
        SifWriteBackDCache(output, *out_len);

    if ((result = SifCallRpc(&SifRpcClient, HTTP_CLIENT_CMD_SEND_GET_RANGE_REQ, 0, RpcTxBuffer, sizeof(struct HttpClientSendGetRangeArgs), RpcRxBuffer, sizeof(struct HttpClientSendGetResult), NULL, NULL)) >= 0) {
        result = ((struct HttpClientSendGetResult *)RpcRxBuffer)->result;
        LastStatusCode = ((struct HttpClientSendGetResult *)RpcRxBuffer)->statusCode;
        LastContentLength = ((struct HttpClientSendGetResult *)RpcRxBuffer)->contentLength;
        LastRequestLength = ((struct HttpClientSendGetResult *)RpcRxBuffer)->requestLength;
        LastSendLength = ((struct HttpClientSendGetResult *)RpcRxBuffer)->sendLength;
        LastSelectResult = ((struct HttpClientSendGetResult *)RpcRxBuffer)->selectResult;
        LastRecvResult = ((struct HttpClientSendGetResult *)RpcRxBuffer)->recvResult;
        LastSocketError = ((struct HttpClientSendGetResult *)RpcRxBuffer)->socketError;
        *mode = ((struct HttpClientSendGetResult *)RpcRxBuffer)->mode;
        *out_len = ((struct HttpClientSendGetResult *)RpcRxBuffer)->out_len;
    }

    return result;
}

int HttpSendPostJsonRequest(s32 HttpSocket, const char *UserAgent, const char *host, s8 *mode, const char *uri, const char *body, u16 body_len, char *output, u16 *out_len)
{
    int result;

    if (body_len > HTTP_CLIENT_POST_BODY_MAX)
        return -EINVAL;

    ((struct HttpClientSendPostJsonArgs *)RpcTxBuffer)->socket = HttpSocket;
    strncpy(((struct HttpClientSendPostJsonArgs *)RpcTxBuffer)->UserAgent, UserAgent, HTTP_CLIENT_USER_AGENT_MAX - 1);
    ((struct HttpClientSendPostJsonArgs *)RpcTxBuffer)->UserAgent[HTTP_CLIENT_USER_AGENT_MAX - 1] = '\0';
    strncpy(((struct HttpClientSendPostJsonArgs *)RpcTxBuffer)->host, host, HTTP_CLIENT_SERVER_NAME_MAX - 1);
    ((struct HttpClientSendPostJsonArgs *)RpcTxBuffer)->host[HTTP_CLIENT_SERVER_NAME_MAX - 1] = '\0';
    ((struct HttpClientSendPostJsonArgs *)RpcTxBuffer)->mode = *mode;
    strncpy(((struct HttpClientSendPostJsonArgs *)RpcTxBuffer)->uri, uri, HTTP_CLIENT_URI_MAX - 1);
    ((struct HttpClientSendPostJsonArgs *)RpcTxBuffer)->uri[HTTP_CLIENT_URI_MAX - 1] = '\0';
    ((struct HttpClientSendPostJsonArgs *)RpcTxBuffer)->body_len = body_len;
    memcpy(((struct HttpClientSendPostJsonArgs *)RpcTxBuffer)->body, body, body_len);
    ((struct HttpClientSendPostJsonArgs *)RpcTxBuffer)->output = output;
    ((struct HttpClientSendPostJsonArgs *)RpcTxBuffer)->out_len = *out_len;

    if (!IS_UNCACHED_SEG(output))
        SifWriteBackDCache(output, *out_len);

    if ((result = SifCallRpc(&SifRpcClient, HTTP_CLIENT_CMD_SEND_POST_JSON_REQ, 0, RpcTxBuffer, sizeof(struct HttpClientSendPostJsonArgs), RpcRxBuffer, sizeof(struct HttpClientSendGetResult), NULL, NULL)) >= 0) {
        result = ((struct HttpClientSendGetResult *)RpcRxBuffer)->result;
        LastStatusCode = ((struct HttpClientSendGetResult *)RpcRxBuffer)->statusCode;
        LastContentLength = ((struct HttpClientSendGetResult *)RpcRxBuffer)->contentLength;
        LastRequestLength = ((struct HttpClientSendGetResult *)RpcRxBuffer)->requestLength;
        LastSendLength = ((struct HttpClientSendGetResult *)RpcRxBuffer)->sendLength;
        LastSelectResult = ((struct HttpClientSendGetResult *)RpcRxBuffer)->selectResult;
        LastRecvResult = ((struct HttpClientSendGetResult *)RpcRxBuffer)->recvResult;
        LastSocketError = ((struct HttpClientSendGetResult *)RpcRxBuffer)->socketError;
        *mode = ((struct HttpClientSendGetResult *)RpcRxBuffer)->mode;
        *out_len = ((struct HttpClientSendGetResult *)RpcRxBuffer)->out_len;
    }

    return result;
}

int HttpStartCoverApiRequest(char *server, u16 port, const char *UserAgent, const char *host, const char *uri)
{
    int result;

    strncpy(((struct HttpClientCoverApiStartArgs *)RpcTxBuffer)->server, server, HTTP_CLIENT_SERVER_NAME_MAX - 1);
    ((struct HttpClientCoverApiStartArgs *)RpcTxBuffer)->server[HTTP_CLIENT_SERVER_NAME_MAX - 1] = '\0';
    ((struct HttpClientCoverApiStartArgs *)RpcTxBuffer)->port = port;
    strncpy(((struct HttpClientCoverApiStartArgs *)RpcTxBuffer)->UserAgent, UserAgent, HTTP_CLIENT_USER_AGENT_MAX - 1);
    ((struct HttpClientCoverApiStartArgs *)RpcTxBuffer)->UserAgent[HTTP_CLIENT_USER_AGENT_MAX - 1] = '\0';
    strncpy(((struct HttpClientCoverApiStartArgs *)RpcTxBuffer)->host, host, HTTP_CLIENT_SERVER_NAME_MAX - 1);
    ((struct HttpClientCoverApiStartArgs *)RpcTxBuffer)->host[HTTP_CLIENT_SERVER_NAME_MAX - 1] = '\0';
    strncpy(((struct HttpClientCoverApiStartArgs *)RpcTxBuffer)->uri, uri, HTTP_CLIENT_URI_MAX - 1);
    ((struct HttpClientCoverApiStartArgs *)RpcTxBuffer)->uri[HTTP_CLIENT_URI_MAX - 1] = '\0';

    if ((result = SifCallRpc(&SifRpcClient, HTTP_CLIENT_CMD_COVER_API_START_REQ, 0, RpcTxBuffer, sizeof(struct HttpClientCoverApiStartArgs), RpcRxBuffer, sizeof(s32), NULL, NULL)) >= 0)
        result = *(s32 *)RpcRxBuffer;

    return result;
}

int HttpStartCoverApiRequestAsync(char *server, u16 port, const char *UserAgent, const char *host, const char *uri)
{
    strncpy(((struct HttpClientCoverApiStartArgs *)RpcTxBuffer)->server, server, HTTP_CLIENT_SERVER_NAME_MAX - 1);
    ((struct HttpClientCoverApiStartArgs *)RpcTxBuffer)->server[HTTP_CLIENT_SERVER_NAME_MAX - 1] = '\0';
    ((struct HttpClientCoverApiStartArgs *)RpcTxBuffer)->port = port;
    strncpy(((struct HttpClientCoverApiStartArgs *)RpcTxBuffer)->UserAgent, UserAgent, HTTP_CLIENT_USER_AGENT_MAX - 1);
    ((struct HttpClientCoverApiStartArgs *)RpcTxBuffer)->UserAgent[HTTP_CLIENT_USER_AGENT_MAX - 1] = '\0';
    strncpy(((struct HttpClientCoverApiStartArgs *)RpcTxBuffer)->host, host, HTTP_CLIENT_SERVER_NAME_MAX - 1);
    ((struct HttpClientCoverApiStartArgs *)RpcTxBuffer)->host[HTTP_CLIENT_SERVER_NAME_MAX - 1] = '\0';
    strncpy(((struct HttpClientCoverApiStartArgs *)RpcTxBuffer)->uri, uri, HTTP_CLIENT_URI_MAX - 1);
    ((struct HttpClientCoverApiStartArgs *)RpcTxBuffer)->uri[HTTP_CLIENT_URI_MAX - 1] = '\0';

    return SifCallRpc(&SifRpcClient, HTTP_CLIENT_CMD_COVER_API_START_REQ, SIF_RPC_M_NOWAIT, RpcTxBuffer, sizeof(struct HttpClientCoverApiStartArgs), RpcRxBuffer, sizeof(s32), &HttpAsyncRpcEnd, NULL);
}

int HttpPollCoverApiStartResult(int *done, int *startResult)
{
    if (SifCheckStatRpc(&SifRpcClient)) {
        *done = 0;
        return 0;
    }

    *done = 1;
    *startResult = *(s32 *)RpcRxBuffer;
    return 0;
}

int HttpGetCoverApiStatus(char *output, u16 *out_len, int *done, int *phase)
{
    int result;

    ((struct HttpClientCoverApiStatusArgs *)RpcTxBuffer)->output = output;
    ((struct HttpClientCoverApiStatusArgs *)RpcTxBuffer)->out_len = *out_len;

    if (!IS_UNCACHED_SEG(output))
        SifWriteBackDCache(output, *out_len);

    if ((result = SifCallRpc(&SifRpcClient, HTTP_CLIENT_CMD_COVER_API_STATUS_REQ, 0, RpcTxBuffer, sizeof(struct HttpClientCoverApiStatusArgs), RpcRxBuffer, sizeof(struct HttpClientCoverApiResult), NULL, NULL)) >= 0) {
        result = ((struct HttpClientCoverApiResult *)RpcRxBuffer)->result;
        *out_len = ((struct HttpClientCoverApiResult *)RpcRxBuffer)->out_len;
        *done = ((struct HttpClientCoverApiResult *)RpcRxBuffer)->done;
        if (phase != NULL)
            *phase = ((struct HttpClientCoverApiResult *)RpcRxBuffer)->phase;
    }

    return result;
}

int HttpGetCoverApiStatusAsync(char *output, u16 out_len)
{
    ((struct HttpClientCoverApiStatusArgs *)RpcTxBuffer)->output = output;
    ((struct HttpClientCoverApiStatusArgs *)RpcTxBuffer)->out_len = out_len;

    if (!IS_UNCACHED_SEG(output))
        SifWriteBackDCache(output, out_len);

    return SifCallRpc(&SifRpcClient, HTTP_CLIENT_CMD_COVER_API_STATUS_REQ, SIF_RPC_M_NOWAIT, RpcTxBuffer, sizeof(struct HttpClientCoverApiStatusArgs), RpcRxBuffer, sizeof(struct HttpClientCoverApiResult), &HttpAsyncRpcEnd, NULL);
}

int HttpPollCoverApiStatusResult(int *rpcDone, int *requestDone, int *phase, int *httpResult, u16 *out_len)
{
    if (SifCheckStatRpc(&SifRpcClient)) {
        *rpcDone = 0;
        return 0;
    }

    *rpcDone = 1;
    *httpResult = ((struct HttpClientCoverApiResult *)RpcRxBuffer)->result;
    *out_len = ((struct HttpClientCoverApiResult *)RpcRxBuffer)->out_len;
    *requestDone = ((struct HttpClientCoverApiResult *)RpcRxBuffer)->done;
    if (phase != NULL)
        *phase = ((struct HttpClientCoverApiResult *)RpcRxBuffer)->phase;

    return 0;
}
