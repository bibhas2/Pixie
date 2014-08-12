#include "../Cute/String.h"
#include "../Cute/Array.h"
#include "../Cute/Buffer.h"

typedef struct _Request {
	int clientFd;
	int serverFd;
	String *protocolLine;
	String *protocol;
	String *method;
	String *host;
	String *port;
	String *path;
	String *headerName;
	String *headerValue;
	Array *headerNames;
	Array *headerValues;
	int requestParseState;
	int clientIOFlag;
	int serverIOFlag;

	Buffer *requestBuffer;
	Buffer *responseBuffer;
	int clientWriteCompleted;
	int serverWriteCompleted;
} Request;

#define MAX_CLIENTS 256

typedef struct _ProxyServer {
	Request requests[MAX_CLIENTS];
	int port;
	int serverSocket;
	void (*onError)(const char* message);
} ProxyServer;

ProxyServer* newProxyServer(int port);
int proxyServerStart(ProxyServer* server);
void deleteProxyServer(ProxyServer* server);

