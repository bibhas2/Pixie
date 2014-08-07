typedef struct _Request {
	int clientFd;
	int serverFd;
	//String *method;
	int clientIOFlag;
	int serverIOFlag;

	char requestBuffer[1024];
	int requestSize;
	char responseBuffer[1024];
	int responseSize;
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

