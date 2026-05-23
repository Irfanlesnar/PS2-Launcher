#include "opl.h"
#include "httpclient.h"

int HttpInit(void) { return 0; }
void HttpDeinit(void) {}
int HttpEstabConnection(char *server, u16 port) { return -1; }
void HttpCloseConnection(s32 HttpSocket) {}
int HttpSendGetRequest(s32 HttpSocket, const char *UserAgent, const char *host, s8 *mode, const u8 *mtime, const char *uri, char *output, u16 *out_len) { return -1; }
