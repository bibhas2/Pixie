typedef struct _Request {
	int parseState;
	int clientFd;
	int serverFd;
	String *method;
	int contentLength;
	char clientReadBuffer[1024];
	char clientWriteBuffer[1024];
	char serverReadBuffer[1024];
	char serverWriteBuffer[1024];
	int clientIOFlag;
	int serverIOFlag;
	int clientReadLength;
	int clientWriteLength;
	int clientReadCompleted;
	int clientWriteCompleted;
	int serverReadLength;
	int serverWriteLength;
	int serverReadCompleted;
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
void proxyServerStart(ProxyServer* server);
void deleteProxyServer(ProxyServer* server);

