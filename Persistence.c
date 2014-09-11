#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>

#include "Proxy.h"
#include "Persistence.h"

#define DIE(p, value, msg) if (value < 0) {if (p->onError != NULL) {p->onError(msg);} return value;}

RequestRecord *requestRecordNew() {
	RequestRecord *rec = calloc(1, sizeof(RequestRecord));

	rec->host = newString();
	rec->port = newString();
	rec->method = newString();
	rec->protocol = newString();
	rec->path = newString();

	rec->parameterNames = newArray();
	rec->parameterValues = newArray();

	rec->fd = -1;

	return rec;
}

void reset_request_record(RequestRecord *rec) {
	rec->host->length = 0;
	rec->port->length = 0;
	rec->method->length = 0;
	rec->protocol->length = 0;
	rec->path->length = 0;

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
		MAP_FILE, rec->fd, 0);
	if (rec->map.buffer == MAP_FAILED) {
		DIE(p, errno, "Failed to map request file.");
	}

	//We are good to go. Start parsing.
}
