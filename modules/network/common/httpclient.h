#define HTTP_CMODE_CLOSED     0
#define HTTP_CMODE_PERSISTENT 1

#define HTTP_CLIENT_RPC_ID 0x00001B20

#define HTTP_COVER_PHASE_IDLE       0
#define HTTP_COVER_PHASE_QUEUED     1
#define HTTP_COVER_PHASE_CONNECTING 2
#define HTTP_COVER_PHASE_GET        3
#define HTTP_COVER_PHASE_CLOSE      4
#define HTTP_COVER_PHASE_DONE       5

// EE-side only
int HttpInit(void);
void HttpDeinit(void);

int HttpEstabConnection(char *server, u16 port);
void HttpCloseConnection(s32 HttpSocket);
int HttpStartCoverApiRequest(char *server, u16 port, const char *UserAgent, const char *host, const char *uri);
int HttpStartCoverApiRequestAsync(char *server, u16 port, const char *UserAgent, const char *host, const char *uri);
int HttpPollCoverApiStartResult(int *done, int *startResult);
int HttpGetCoverApiStatus(char *output, u16 *out_len, int *done, int *phase);
int HttpGetCoverApiStatusAsync(char *output, u16 out_len);
int HttpPollCoverApiStatusResult(int *rpcDone, int *requestDone, int *phase, int *httpResult, u16 *out_len);

/*  mtime[0] = Years since year 2000
    mtime[1] = Month, 0-11
    mtime[2] = day in month, 0-30
    mtime[3] = Hour (0-23)
    mtime[4] = Minute (0-59)
    mtime[5] = Second (0-59)
*/

int HttpSendGetRequest(s32 HttpSocket, const char *UserAgent, const char *host, s8 *mode, const u8 *mtime, const char *uri, char *output, u16 *out_len);

#define HTTP_CLIENT_SERVER_NAME_MAX 30
#define HTTP_CLIENT_USER_AGENT_MAX  16
#define HTTP_CLIENT_URI_MAX         128

enum HTTP_CLIENT_CMD {
    HTTP_CLIENT_CMD_CONN_ESTAB,
    HTTP_CLIENT_CMD_CONN_CLOSE,
    HTTP_CLIENT_CMD_SEND_GET_REQ,
    HTTP_CLIENT_CMD_COVER_API_START_REQ,
    HTTP_CLIENT_CMD_COVER_API_STATUS_REQ,
};

struct HttpClientConnEstabArgs
{
    char server[HTTP_CLIENT_SERVER_NAME_MAX];
    u16 port;
};

struct HttpClientConnCloseArgs
{
    s32 socket;
};

struct HttpClientSendGetArgs
{
    s32 socket;
    char UserAgent[HTTP_CLIENT_USER_AGENT_MAX];
    char host[HTTP_CLIENT_SERVER_NAME_MAX];
    s8 mode;
    u8 hasMtime;
    u8 mtime[6];
    char uri[HTTP_CLIENT_URI_MAX];
    u16 out_len;
    void *output;
};

struct HttpClientCoverApiStartArgs
{
    char server[HTTP_CLIENT_SERVER_NAME_MAX];
    u16 port;
    char UserAgent[HTTP_CLIENT_USER_AGENT_MAX];
    char host[HTTP_CLIENT_SERVER_NAME_MAX];
    char uri[HTTP_CLIENT_URI_MAX];
};

struct HttpClientCoverApiStatusArgs
{
    u16 out_len;
    void *output;
};

struct HttpClientSendGetResult
{
    s32 result;
    s8 mode;
    u8 padding;
    u16 out_len;
};

struct HttpClientCoverApiResult
{
    s32 result;
    u16 out_len;
    u8 done;
    u8 phase;
};

#ifdef _IOP
#define httpc_IMPORTS_start DECLARE_IMPORT_TABLE(httpc, 1, 1)
#define httpc_IMPORTS_end   END_IMPORT_TABLE

#define I_HttpEstabConnection DECLARE_IMPORT(4, HttpEstabConnection)
#define I_HttpCloseConnection DECLARE_IMPORT(5, HttpCloseConnection)
#define I_HttpSendGetRequest  DECLARE_IMPORT(6, HttpSendGetRequest)
#endif
