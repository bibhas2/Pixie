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
	Array *headerNames;
	Array *headerValues;
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
	Array *headerNames;
	Array *headerValues;
	Buffer headerBuffer;
	Buffer bodyBuffer;	
} ResponseRecord;

RequestRecord *newRequestRecord();
void deleteRequestRecord(RequestRecord *rec);
ResponseRecord *newResponseRecord();
void deleteResponseRecord(ResponseRecord *rec);

int proxyServerLoadRequest(ProxyServer *p, const char *uniqueId, 
	RequestRecord *rec);
int proxyServerLoadResponse(ProxyServer *p, const char *uniqueId, 
	ResponseRecord *rec);
int proxyServerResetRecords(ProxyServer *p, RequestRecord *reqRec, 
	ResponseRecord *resRec);
int proxyServerDeleteRecord(ProxyServer *p, const char *uniqueId);
