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
#define PARSE_DONE 7

RequestRecord *newRequestRecord() {
	RequestRecord *rec = calloc(1, sizeof(RequestRecord));

	rec->host = newString();
	rec->port = newString();
	rec->method = newString();
	rec->protocol = newString();
	rec->path = newString();
	rec->queryString = newString();

	rec->parameterNames = newArray(10);
	rec->parameterValues = newArray(10);

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
}

void deleteRequestRecord(RequestRecord *rec) {
	reset_request_record(rec); 

	deleteString(rec->host);
	deleteString(rec->port);
	deleteString(rec->method);
	deleteString(rec->protocol);
	deleteString(rec->path);
	deleteString(rec->queryString);

	deleteArray(rec->parameterNames);
	deleteArray(rec->parameterValues);
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
			continue;
		}
		if (state == PARSE_HEADER_VALUE) {
			if (ch == '\n') {
				state = PARSE_HEADER_NAME;
				continue;
			}
			continue;
		}
	}

	//It is safe to close the file now. It will 
	//closed anyway by reset method. So no biggie.

	return 0;
}
