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
		if (strncmp(name, thisName->buffer, thisName->length) == 0) {
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
	int skip_space = 1;
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
				skip_space = 1;
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
			if (skip_space && ch == ' ') {
				//Skip leading space.
				continue;
			}
			skip_space = 0;
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
	    strncmp(FORM_ENC, contentType->buffer, contentType->length) == 0 &&
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
