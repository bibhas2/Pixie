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
	Buffer *requestBodyOverflowBuffer;
	Array *headerNames;
	Array *headerValues;
	int requestState;
	int clientIOFlag;
	int serverIOFlag;
	int connectionEstablished;

	Buffer *requestBuffer;
	Buffer *responseBuffer;
	size_t clientWriteCompleted;
	size_t serverWriteCompleted;
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
void proxySetTrace(int t);
