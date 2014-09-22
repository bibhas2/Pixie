#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>
#include <sys/mman.h>
#include <dirent.h>

#include "Proxy.h"
#include "Persistence.h"

#define DIE(p, value, msg) if (value < 0) {if (p->onError != NULL) {p->onError(msg);} return value;}

#define PARSE_METHOD 0
#define PARSE_PROTOCOL_VERSION 1
#define PARSE_CONNECT_ADDRESS 2
#define PARSE_PATH 3
#define PARSE_QUERY_STRING 4
#define PARSE_HEADER_NAME 5
#define PARSE_HEADER_VALUE 6
#define PARSE_STATUS_CODE 7
#define PARSE_STATUS_MESSAGE 8
#define PARSE_DONE 9

#define FORM_ENC "application/x-www-form-urlencoded"
#define CONTENT_TYPE "Content-Type"

RequestRecord *newRequestRecord() {
	RequestRecord *rec = calloc(1, sizeof(RequestRecord));

	rec->host = newString();
	rec->port = newString();
	rec->method = newString();
	rec->protocol = newString();
	rec->path = newString();
	rec->queryString = newString();

	rec->headerNames = newArray(10);
	rec->headerValues = newArray(10);
	rec->parameterNames = newArray(10);
	rec->parameterValues = newArray(10);

	rec->fd = -1;

	return rec;
}

ResponseRecord *newResponseRecord() {
	ResponseRecord *rec = calloc(1, sizeof(ResponseRecord));

	rec->statusCode = newString();
	rec->statusMessage = newString();

	rec->headerNames = newArray(10);
	rec->headerValues = newArray(10);

	rec->fd = -1;

	return rec;
}

void reset_request_record(RequestRecord *rec) {
	rec->host->length = 0;
	rec->port->length = 0;
	rec->method->length = 0;
	rec->protocol->length = 0;
	rec->path->length = 0;
	rec->queryString->length = 0;

	//Delete all header strings
	for (size_t i = 0; i < rec->headerNames->length; ++i) {
		deleteString(arrayGet(rec->headerNames, i));
		arraySet(rec->headerNames, i, NULL);
	}
	for (size_t i = 0; i < rec->headerValues->length; ++i) {
		deleteString(arrayGet(rec->headerValues, i));
		arraySet(rec->headerValues, i, NULL);
	}
	rec->headerNames->length = 0;
	rec->headerValues->length = 0;

	//Delete all parameter strings
	for (size_t i = 0; i < rec->parameterNames->length; ++i) {
		deleteString(arrayGet(rec->parameterNames, i));
		arraySet(rec->parameterNames, i, NULL);
	}
	for (size_t i = 0; i < rec->parameterValues->length; ++i) {
		deleteString(arrayGet(rec->parameterValues, i));
		arraySet(rec->parameterValues, i, NULL);
	}
	rec->parameterNames->length = 0;
	rec->parameterValues->length = 0;

	if (rec->map.buffer != NULL && rec->map.buffer != MAP_FAILED) {
		munmap(rec->map.buffer, rec->map.length);

		rec->map.buffer = NULL;
		rec->map.length = 0;
	}

	if (rec->fd >= 0) {
		close(rec->fd);	
		rec->fd = -1;
	}

	rec->headerBuffer.length = 0;
	rec->bodyBuffer.length = 0;
}

void reset_response_record(ResponseRecord *rec) {
	rec->statusCode->length = 0;
	rec->statusMessage->length = 0;

	//Delete all header strings
	for (size_t i = 0; i < rec->headerNames->length; ++i) {
		deleteString(arrayGet(rec->headerNames, i));
		arraySet(rec->headerNames, i, NULL);
	}
	for (size_t i = 0; i < rec->headerValues->length; ++i) {
		deleteString(arrayGet(rec->headerValues, i));
		arraySet(rec->headerValues, i, NULL);
	}
	rec->headerNames->length = 0;
	rec->headerValues->length = 0;

	if (rec->map.buffer != NULL && rec->map.buffer != MAP_FAILED) {
		munmap(rec->map.buffer, rec->map.length);

		rec->map.buffer = NULL;
		rec->map.length = 0;
	}
	if (rec->fd >= 0) {
		close(rec->fd);	
		rec->fd = -1;
	}
	rec->headerBuffer.length = 0;
	rec->bodyBuffer.length = 0;
}

void deleteRequestRecord(RequestRecord *rec) {
	reset_request_record(rec); 

	deleteString(rec->host);
	deleteString(rec->port);
	deleteString(rec->method);
	deleteString(rec->protocol);
	deleteString(rec->path);
	deleteString(rec->queryString);

	deleteArray(rec->headerNames);
	deleteArray(rec->headerValues);
	deleteArray(rec->parameterNames);
	deleteArray(rec->parameterValues);

	free(rec);
}

void deleteResponseRecord(ResponseRecord *rec) {
	reset_response_record(rec); 

	deleteString(rec->statusCode);
	deleteString(rec->statusMessage);

	deleteArray(rec->headerNames);
	deleteArray(rec->headerValues);

	free(rec);
}

static void parse_url_params(const char *buffer, size_t length, 
	Array *names, Array *values) {

	int is_name = 1;
	String *name = NULL, *value = NULL;

	for (size_t i = 0; i < length; ++i) {
		char ch = buffer[i];

		if (is_name == 1) {
			if (name == NULL) {
				name = newString();
			}
			if (ch == '=') {
				is_name = 0;
				arrayAdd(names, name);
				name = NULL;

				continue;
			}
			if (ch == '&') {
				//Empty value
				is_name = 1;
				arrayAdd(names, name);
				arrayAdd(values, newString());
				name = NULL;

				continue;
			}
			stringAppendChar(name, ch);
			continue;
		}
		if (is_name == 0) {
			if (value == NULL) {
				value = newString();
			}

			if (ch == '&') {
				is_name = 1;
				arrayAdd(values, value);
				value = NULL;

				continue;
			}
			stringAppendChar(value, ch);
			continue;
		}
	}
	if (is_name == 1 && name != NULL) {
		//Query ended with a name
		arrayAdd(names, name);
		arrayAdd(values, newString());
	} else if (is_name == 0 && value != NULL) {
		//Query ended with a value
		arrayAdd(values, value);
	} else if (is_name == 0 && value == NULL) {
		//Query ended with an empty value
		arrayAdd(values, newString());
	}
}

static String *get_header_value(const char *name, RequestRecord *rec) {
	for (size_t i = 0; i < rec->headerNames->length; ++i) {
		String *thisName = arrayGet(rec->headerNames, i);
		if (stringEqualsCString(thisName, name)) {
			return arrayGet(rec->headerValues, i);
		}
	}

	return NULL;
}

String *responseRecordGetHeader(ResponseRecord *rec, const char *name) {
	for (size_t i = 0; i < rec->headerNames->length; ++i) {
		String *thisName = arrayGet(rec->headerNames, i);
		if (stringEqualsCString(thisName, name)) {
			return arrayGet(rec->headerValues, i);
		}
	}

	return NULL;
}

int proxyServerLoadRequest(ProxyServer *p, const char *uniqueId,
        RequestRecord *rec) {

	reset_request_record(rec);

	char file_name[512];
	struct stat stat_buf;
	snprintf(file_name, sizeof(file_name), "%s/%s.req",
		stringAsCString(p->persistenceFolder), uniqueId);

	rec->fd = open(file_name, O_RDONLY);
	DIE(p, rec->fd, "Failed to open request file.");

	int status = fstat(rec->fd, &stat_buf);
	DIE(p, status, "Failed to get request file size.");

	rec->map.length = stat_buf.st_size; //Save the size

	//If file size is 0 then just return
	if (rec->map.length == 0) {
		return 0;
	}

	rec->map.buffer = mmap(NULL, stat_buf.st_size, PROT_READ,
		MAP_SHARED, rec->fd, 0);
	if (rec->map.buffer == MAP_FAILED) {
		DIE(p, -1, "Failed to map request file.");
	}

	//We are good to go. Start parsing.
	String *str = newString();
	int state = PARSE_METHOD;
	for (size_t i = 0; i < rec->map.length; ++i) {
		char ch = rec->map.buffer[i];

		//printf("[%c %d]", ch, state);
		if (ch == '\r') {
			continue; //Ignored
		}
		if (state == PARSE_METHOD) {
			if (ch == ' ') {
				if (strcmp("CONNECT", 
					stringAsCString(rec->method)) == 0) {
					state = PARSE_CONNECT_ADDRESS;
				} else {
					state = PARSE_PATH;
				}
				continue;
			}
			stringAppendChar(rec->method, ch);
			continue;
		}
		if (state == PARSE_PATH) {
			if (ch == '?') {
				state = PARSE_QUERY_STRING;
				continue;
			}
			if (ch == ' ') {
				state = PARSE_PROTOCOL_VERSION;
				continue;
			}
			stringAppendChar(rec->path, ch);
			continue;
		}
		if (state == PARSE_QUERY_STRING) {
			if (ch == ' ') {
				state = PARSE_PROTOCOL_VERSION;
				continue;
			}
			stringAppendChar(rec->queryString, ch);
			continue;
		}
		if (state == PARSE_PROTOCOL_VERSION) {
			if (ch == '\n') {
				state = PARSE_HEADER_NAME;
				continue;
			}
			//Don't store version
			continue;
		}
		if (state == PARSE_CONNECT_ADDRESS) {
			if (ch == ' ') {
				state = PARSE_PROTOCOL_VERSION;
				continue;
			}
			//Don't store address
			continue;
		}
		if (state == PARSE_HEADER_NAME) {
			if (ch == ':') {
				arrayAdd(rec->headerNames, 
					newStringWithString(str));
				str->length = 0;
				state = PARSE_HEADER_VALUE;
				continue;
			}
			if (ch == '\n') {
				//End of request headers
				rec->headerBuffer.buffer = 
					rec->map.buffer;
				rec->headerBuffer.length = i + 1;

				rec->bodyBuffer.buffer =
					rec->map.buffer + i + 1;
				rec->bodyBuffer.length = 
					rec->map.length - 
					rec->headerBuffer.length;
				
				//End parsing for now
				break;
			}
			stringAppendChar(str, ch);
			continue;
		}
		if (state == PARSE_HEADER_VALUE) {
			if (ch == '\n') {
				arrayAdd(rec->headerValues, 
					newStringWithString(str));
				str->length = 0;
				state = PARSE_HEADER_NAME;
				continue;
			}
			if ((str->length == 0) && ch == ' ') {
				//Skip leading space.
				continue;
			}
			stringAppendChar(str, ch);
			continue;
		}
	}
	
	deleteString(str);

	//Parse URL parameters
	parse_url_params(rec->queryString->buffer, rec->queryString->length, 
		rec->parameterNames, rec->parameterValues);

	//If form post then parse request body for parameters
	String *contentType = get_header_value(CONTENT_TYPE, rec);
	if (contentType != NULL && 
	    stringEqualsCString(contentType, FORM_ENC) &&
	    rec->bodyBuffer.length > 0) {
		parse_url_params(rec->bodyBuffer.buffer, 
			rec->bodyBuffer.length, 
			rec->parameterNames, 
			rec->parameterValues);
	}

	//It is safe to close the file now. It will 
	//closed anyway by reset method. 
	close(rec->fd);
	rec->fd = -1;

	return 0;
}

int proxyServerLoadResponse(ProxyServer *p, const char *uniqueId,
        ResponseRecord *rec) {

	reset_response_record(rec);

	char file_name[512];
	struct stat stat_buf;
	snprintf(file_name, sizeof(file_name), "%s/%s.res",
		stringAsCString(p->persistenceFolder), uniqueId);

	rec->fd = open(file_name, O_RDONLY);
	DIE(p, rec->fd, "Failed to open response file.");

	int status = fstat(rec->fd, &stat_buf);
	DIE(p, status, "Failed to get response file size.");

	rec->map.length = stat_buf.st_size; //Save the size

	//If file size is 0 then just return
	if (rec->map.length == 0) {
		return 0;
	}

	rec->map.buffer = mmap(NULL, stat_buf.st_size, PROT_READ,
		MAP_SHARED, rec->fd, 0);
	if (rec->map.buffer == MAP_FAILED) {
		DIE(p, -1, "Failed to map response file.");
	}

	//We are good to go. Start parsing header.
	String *str = newString();
	int state = PARSE_PROTOCOL_VERSION;
	for (size_t i = 0; i < rec->map.length; ++i) {
		char ch = rec->map.buffer[i];

		//printf("[%c %d]", ch, state);
		if (ch == '\r') {
			continue; //Ignored
		}

		if (state == PARSE_PROTOCOL_VERSION) {
			if (ch == ' ') {
				state = PARSE_STATUS_CODE;
				continue;
			}
			//Don't store version
			continue;
		}
		if (state == PARSE_STATUS_CODE) {
			if (ch == ' ') {
				state = PARSE_STATUS_MESSAGE;
				continue;
			}
			stringAppendChar(rec->statusCode, ch);
			continue;
		}
		if (state == PARSE_STATUS_MESSAGE) {
			if (ch == '\n') {
				state = PARSE_HEADER_NAME;
				continue;
			}
			stringAppendChar(rec->statusMessage, ch);
			continue;
		}
		if (state == PARSE_HEADER_NAME) {
			if (ch == ':') {
				arrayAdd(rec->headerNames, 
					newStringWithString(str));
				str->length = 0;
				state = PARSE_HEADER_VALUE;
				continue;
			}
			if (ch == '\n') {
				//End of request headers
				rec->headerBuffer.buffer = 
					rec->map.buffer;
				rec->headerBuffer.length = i + 1;

				rec->bodyBuffer.buffer =
					rec->map.buffer + i + 1;
				rec->bodyBuffer.length = 
					rec->map.length - 
					rec->headerBuffer.length;
				
				//End parsing for now
				break;
			}
			stringAppendChar(str, ch);
			continue;
		}
		if (state == PARSE_HEADER_VALUE) {
			if (ch == '\n') {
				arrayAdd(rec->headerValues, 
					newStringWithString(str));
				str->length = 0;
				state = PARSE_HEADER_NAME;
				continue;
			}
			if ((str->length == 0) && ch == ' ') {
				//Skip leading space.
				continue;
			}
			stringAppendChar(str, ch);
			continue;
		}
	}
	
	deleteString(str);

	//It is safe to close the file now. It will 
	//closed anyway by reset method. 
	close(rec->fd);
	rec->fd = -1;

	return 0;
}

int proxyServerResetRecords(ProxyServer *p, RequestRecord *reqRec, 
        ResponseRecord *resRec) {
	if (reqRec != NULL) {
		reset_request_record(reqRec);
	}
	if (resRec != NULL) {
		reset_response_record(resRec);
	}

	return 0;
}

int proxyServerDeleteRecord(ProxyServer *p, const char *uniqueId) {
	int status, res = 0;
	char file_name[512];

	snprintf(file_name, sizeof(file_name), "%s/%s.meta",
		stringAsCString(p->persistenceFolder), uniqueId);
	status = unlink(file_name);
	res = status < 0 ? status : res;

	snprintf(file_name, sizeof(file_name), "%s/%s.res",
		stringAsCString(p->persistenceFolder), uniqueId);
	status = unlink(file_name);
	res = status < 0 ? status : res;

	snprintf(file_name, sizeof(file_name), "%s/%s.req",
		stringAsCString(p->persistenceFolder), uniqueId);
	status = unlink(file_name);
	res = status < 0 ? status : res;

	return res;
}

int proxyServerSaveBuffer(ProxyServer *p, 
	const char *fileName, 
	Buffer *buffer) {

	FILE *file = fopen(fileName, "w");

	if (file == NULL) {
		DIE(p, -1, "Failed to save buffer.");
	}

	size_t sz = fwrite(buffer->buffer, sizeof(char), buffer->length, file);
	fclose(file);

	if (sz != buffer->length) {
		DIE(p, -1, "Failed to save buffer properly.");
	}

	return 0;
}

/*
 * Reads one line into the supplied String.
 * Returns 1 if there is more data left to be read
 * in the file. In case of end of file or error, returns 0.
 */
int read_line(FILE *file, String *line, char stopChar) {
	int ch, stopped = 0;

	line->length = 0;

	while ((ch = fgetc(file)) != EOF) {
		if (ch == '\r') {
			continue;
		}
		if (ch == '\n') {
			break;
		}

		stopped = stopped == 1 || ch == stopChar;

		if (stopped == 0) {
			stringAppendChar(line, (char) ch);
		}
	}

	return ch != EOF;
}

int proxyServerLoadMeta(ProxyServer *p,
	const char *uniqueId, RequestRecord* req, ResponseRecord *res) {
	char file_name[512];

	proxyServerResetRecords(p, req, res);

	snprintf(file_name, sizeof(file_name), "%s/%s.meta",
		stringAsCString(p->persistenceFolder), uniqueId);
	
	FILE *file = fopen(file_name, "r");

	if (file == NULL) {
		DIE(p, -1, "Failed to open meta file.");
	}

	String *name = newString();
	int incompleteFile = 1;

	for (int hasMore = 1; hasMore == 1; ) {
		hasMore = read_line(file, name, '\0');

		if (hasMore == 0) {
			break;
		}

		//Must have a valid name
		assert(name->length > 0);

		incompleteFile = 0;

		const char *nameStr = stringAsCString(name);

		if (strcmp(nameStr, "protocol-line") == 0) {
			hasMore = read_line(file, req->method, ' ');
		} else if (strcmp(nameStr, "protocol") == 0) {
			hasMore = read_line(file, req->protocol, '\0');
		} else if (strcmp(nameStr, "host") == 0) {
			hasMore = read_line(file, req->host, '\0');
		} else if (strcmp(nameStr, "port") == 0) {
			hasMore = read_line(file, req->port, '\0');
		} else if (strcmp(nameStr, "path") == 0) {
			//Deal with query string
			hasMore = read_line(file, req->path, ' ');
		} else if (strcmp(nameStr, "response-status-code") == 0) {
			hasMore = read_line(file, res->statusCode, '\0');
		} else if (strcmp(nameStr, "response-status-message") == 0) {
			hasMore = read_line(file, res->statusMessage, '\0');
		}
	}

	deleteString(name);

	fclose(file);

	return incompleteFile == 1 ? -2 : 0;
}

int proxyServerLoadHistory(ProxyServer *p, 
	void *contextData,
	void (*callback)(void *, const char*, RequestRecord*, ResponseRecord*)) {

	if (callback == NULL) {
		return 0; //What's the point?
	}

	DIR *dir = opendir(stringAsCString(p->persistenceFolder));
	if (dir == NULL) {
		DIE(p, -1, "Failed to get listing of persistence directory.");
	}
	
	const char *ext = ".meta";
	int extLen = strlen(ext);
	struct dirent *ent = NULL;
	int status = 0;
	RequestRecord *req = newRequestRecord();
	ResponseRecord *res = newResponseRecord();
	String *uniqueId = newString();

	while ((ent = readdir(dir)) != NULL) {
		/*
 		 * Don't use d_namlen which is available in Mac but
 		 * not standard compiant and not there in Linux.
 		 */
		size_t name_len = strlen(ent->d_name);
		//Look for .meta extension
		if (name_len < extLen) {
			//Discard
			continue;
		}
		if (strcmp(ent->d_name + name_len - extLen, ext) != 0) {
			continue;
		}
		//We have a meta file
		uniqueId->length = 0;
		stringAppendBuffer(uniqueId, ent->d_name,
			name_len - extLen);
		status = proxyServerLoadMeta(p, 
			stringAsCString(uniqueId), req, res);

		if (status < 0) {
			continue;
		}

		callback(contextData, stringAsCString(uniqueId), req, res);
	}

	//Clean up
	closedir(dir);
	deleteRequestRecord(req);
	deleteResponseRecord(res);
	deleteString(uniqueId);

	DIE(p, status, "Failed to load some meta files.");

	return 0;
}
