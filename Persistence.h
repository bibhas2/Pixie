//Structure to store either request or response header data
typedef struct _RequestRecord {
	//Private stuff
	int fd;
	Buffer map;
	//Public stuff
	String *host;
	String *port;
	String *method;
	String *protocol;
	String *path;
	String *queryString;
	Array *parameterNames;
	Array *parameterValues;
	Buffer headerBuffer;
	Buffer bodyBuffer;	
} RequestRecord;

typedef struct _ResponseRecord {
	//Private stuff
	int fd;
	Buffer map;
	//Public stuff
	String *statusCode;
	String *statusMessage;
	Buffer headerBuffer;
	Buffer bodyBuffer;	
} ResponseRecord;

RequestRecord *newRequestRecord();
void deleteRequestRecord(RequestRecord *rec);
RequestRecord *newResponseRecord();
void deleteResponseRecord(ResponseRecord *rec);

int proxyServerLoadRequest(ProxyServer *p, const char *uniqueId, 
	RequestRecord *rec);
int proxyServerLoadResponse(ProxyServer *p, const char *uniqueId, 
	ResponseRecord *rec);
