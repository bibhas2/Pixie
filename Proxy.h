#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/time.h>
#include <pthread.h>
#endif

#include "../Cute/String.h"
#include "../Cute/Array.h"
#include "../Cute/Buffer.h"

#ifdef _WIN32
//Due to limitation in wait for multiple events
#define MAX_CLIENTS 20
#else
#define MAX_CLIENTS 256
#endif

typedef struct _Request {
#ifdef _WIN32
	SOCKET clientFd;
	SOCKET serverFd;
	WSAEVENT clientEvent;
	WSAEVENT serverEvent;
#else
	int clientFd;
	int serverFd;
#endif
	String *uniqueId; //Every HTTP request gets a unique ID
	String *protocolLine;
	String *protocol;
	String *method;
	String *host;
	String *port;
	String *path;
	String *headerName;
	String *headerValue;
	String *responseStatusCode;
	String *responseStatusMessage;
	Buffer *requestBodyOverflowBuffer;
	Array *headerNames;
	Array *headerValues;
	int requestState;
	int responseHeaderParseState;
	int clientIOFlag;
	int serverIOFlag;
	int connectionEstablished;

	Buffer *requestBuffer;
	Buffer *responseBuffer;
	size_t clientWriteCompleted;
	size_t serverWriteCompleted;

	FILE *metaFile;
	FILE *requestFile;
	FILE *responseFile;

	//Timing
	struct timeval requestStartTime;
	struct timeval responseEndTime;
} Request;

typedef enum _RunStatus {
	STOPPED,
	RUNNING
} RunStatus;

typedef struct _ProxyServer {
#ifdef _WIN32
	HANDLE backgroundThreadId;
	HANDLE controlPipe[2];
	SOCKET serverSocket;
#else
	pthread_t backgroundThreadId;
	int controlPipe[2];
	int serverSocket;
#endif
	Request requests[MAX_CLIENTS];
	int persistenceEnabled;
	int port;
	String *persistenceFolder;
	int isInBackgroundMode;

	//Server control mechanism
	RunStatus runStatus;

	//Various event notification callbacks
	void (*onError)(const char* message);
	void (*onBeginRequest)(struct _ProxyServer *p, Request *req);
	void (*onRequestHeaderParsed)(struct _ProxyServer *p, Request *req);
	void (*onEndRequest)(struct _ProxyServer *p, Request *req);
	void (*onQueueWriteToServer)(struct _ProxyServer *p, Request *req);
	void (*onQueueWriteToClient)(struct _ProxyServer *p, Request *req);
} ProxyServer;

ProxyServer* newProxyServer(int port);
int proxyServerStart(ProxyServer* server);
int proxyServerStartInBackground(ProxyServer* server);
int proxyServerStop(ProxyServer* server);
void deleteProxyServer(ProxyServer* server);
void proxySetTrace(int t);
