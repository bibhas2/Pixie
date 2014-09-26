#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef _WIN32
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>

#include "Proxy.h"
#include "private.h"

#define REQ_STATE_NONE 0
#define REQ_PARSE_PROTOCOL 1
#define REQ_PARSE_HEADER_NAME 2
#define REQ_PARSE_HEADER_VALUE 3
#define REQ_READ_BODY 4
#define REQ_READ_RESPONSE 5
#define REQ_CONNECT_TUNNEL_MODE 6

#define RES_HEADER_STATE_PROTOCOL 0
#define RES_HEADER_STATE_STATUS_CODE 1
#define RES_HEADER_STATE_STATUS_MSG 2
#define RES_HEADER_STATE_DONE 5

#define CMD_NONE 0
#define CMD_STOP 1

static int bTrace = 0;

static void default_on_error(const char *msg) {
	perror(msg);
}

void proxySetTrace(int t) {
	bTrace = t;
}

void
_info(const char* fmt, ...) {
	if (bTrace == 0) {
		return;
	}

	va_list ap;

	va_start(ap, fmt);

	printf("INFO: ");
	vprintf(fmt, ap);
	printf("\n");
}

int disconnect_clients(ProxyServer *p) {

	return 0;
}

/*
 * Initialize all request state data to default so that
 * the request object can be reused again for another TCP connection.
 */
static void reset_request_state(Request *req) {
	req->clientFd = INVALID_SOCKET;
	req->serverFd = INVALID_SOCKET;

	//Free all header strings
	for (size_t j = 0; j < req->headerNames->length; ++j) {
		deleteString(arrayGet(req->headerNames, j));
	}
	req->headerNames->length = 0;

	for (size_t j = 0; j < req->headerValues->length; ++j) {
		deleteString(arrayGet(req->headerValues, j));
	}
	req->headerValues->length = 0;

	req->clientIOFlag = RW_STATE_NONE;
	req->serverIOFlag = RW_STATE_NONE;
	req->clientWriteCompleted = 0;
	req->serverWriteCompleted = 0;
	req->uniqueId->length = 0;
	req->protocolLine->length = 0;
	req->protocol->length = 0;
	req->method->length = 0;
	req->host->length = 0;
	req->port->length = 0;
	req->path->length = 0;
	req->responseStatusMessage->length = 0;
	req->responseStatusCode->length = 0;
	req->headerName = NULL;
	req->headerValue = NULL;
	req->requestState = REQ_STATE_NONE;
	req->connectionEstablished = 0;
	
	req->requestStartTime.tv_sec = 0;
	req->requestStartTime.tv_usec = 0;
	req->responseEndTime.tv_sec = 0;
	req->responseEndTime.tv_usec = 0;
	
	req->responseHeaderParseState = RES_HEADER_STATE_PROTOCOL;
}

static void on_begin_request(ProxyServer *p, Request *req) {
	/*
	 * Store the request start time. Also use it to generate a unique ID for
	 * the request.
	 */
	assert(os_gettimeofday(&req->requestStartTime) == 0);

	char uid[256];

	int sz = snprintf(uid, sizeof(uid), "%lu-%lu-%d",
		(unsigned long)req->requestStartTime.tv_sec,
		(unsigned long)req->requestStartTime.tv_usec,
		req->clientFd);
	assert(sz > 0);

	req->uniqueId->length = 0; //Rest old value
	stringAppendBuffer(req->uniqueId, uid, sz);

	//Open the files if persistence is enabled
	if (p->persistenceEnabled == 1) {
		_info("Opening files for: %.*s",
			req->uniqueId->length, req->uniqueId->buffer);

		char file_name[512];

		snprintf(file_name, sizeof(file_name), "%s/%s.meta",
			stringAsCString(p->persistenceFolder), uid);
		req->metaFile = fopen(file_name, "w");
		snprintf(file_name, sizeof(file_name), "%s/%s.req",
			stringAsCString(p->persistenceFolder), uid);
		req->requestFile = fopen(file_name, "w");
		snprintf(file_name, sizeof(file_name), "%s/%s.res",
			stringAsCString(p->persistenceFolder), uid);
		req->responseFile = fopen(file_name, "w");

		if (req->metaFile == NULL || req->requestFile == NULL || req->responseFile == NULL) {
			_info("Failed to save HTTP data files.");
			if (p->onError != NULL) {
				p->onError("Failed to save HTTP data files.");
			}
		}
	}

	if (p->onBeginRequest != NULL) {
		p->onBeginRequest(p, req);
	}
}

static void on_end_request(ProxyServer *p, Request *req) {
	//Close all files
	if (p->persistenceEnabled == 1) {
		//Write the meta data about this request.
		if (req->metaFile != NULL) {
			fprintf(req->metaFile, "protocol-line\n%.*s\n",
				(int)req->protocolLine->length, req->protocolLine->buffer);
			fprintf(req->metaFile, "protocol\n%.*s\n",
				(int)req->protocol->length, req->protocol->buffer);
			fprintf(req->metaFile, "host\n%.*s\n",
				(int)req->host->length, req->host->buffer);
			fprintf(req->metaFile, "port\n%.*s\n",
				(int)req->port->length, req->port->buffer);
			fprintf(req->metaFile, "path\n%.*s\n",
				(int)req->path->length, req->path->buffer);
			fprintf(req->metaFile, "request-start-seconds\n%lu\n",
				(unsigned long)req->requestStartTime.tv_sec);
			fprintf(req->metaFile, "request-start-microseconds\n%lu\n",
				(unsigned long)req->requestStartTime.tv_usec);
			fprintf(req->metaFile, "response-end-seconds\n%lu\n",
				(unsigned long)req->responseEndTime.tv_sec);
			fprintf(req->metaFile, "response-end-microseconds\n%lu\n",
				(unsigned long)req->responseEndTime.tv_usec);
			fprintf(req->metaFile, "response-status-code\n%.*s\n",
				(int)req->responseStatusCode->length,
				req->responseStatusCode->buffer);
			fprintf(req->metaFile, "response-status-message\n%.*s\n",
				(int)req->responseStatusMessage->length,
				req->responseStatusMessage->buffer);
		}

		_info("Closing files for: %.*s",
			req->uniqueId->length, req->uniqueId->buffer);
		if (req->metaFile != NULL) {
			fclose(req->metaFile);
			req->metaFile = NULL;
		}
		if (req->requestFile != NULL) {
			fclose(req->requestFile);
			req->requestFile = NULL;
		}
		if (req->responseFile != NULL) {
			fclose(req->responseFile);
			req->responseFile = NULL;
		}
	}
	if (p->onEndRequest != NULL) {
		p->onEndRequest(p, req);
	}
}

int shutdown_channel(ProxyServer *p, Request *req) {
	_info("Shutting down channel. Client %d server %d",
		req->clientFd, req->serverFd);

	if (IS_OPEN(req->clientFd)) {
		os_close_socket(req->clientFd);
	}
	if (IS_OPEN(req->serverFd)) {
		os_close_socket(req->serverFd);
	}

	/*
	 * Mark the request as ended. This may be a successful end
	 * or an end due to some kind of error in the network, client or server.
	 * Also, at times browser opens connections to proxy server that are never used
	 * to send request. If that happened then don't call onEndRequest when channel
	 * is disconnected.
	 */
	if (req->requestState != REQ_STATE_NONE) {
		on_end_request(p, req);
	}

	reset_request_state(req);

	return 0;
}

static void persist_request_buffer(ProxyServer *p, Request *req) {
	if (p->persistenceEnabled == 1 && req->requestFile != NULL) {
		size_t sz = fwrite(req->requestBuffer->buffer,
			req->requestBuffer->length,
			1,
			req->requestFile);
		if (sz == 0) {
			_info("Failed to write request data to file.");
		}
	}
}

static void parse_response_header(ProxyServer *p, Request *req) {
	assert(req->responseHeaderParseState != RES_HEADER_STATE_DONE);

	for (size_t i = 0; i < req->responseBuffer->length; ++i) {
		char ch = req->responseBuffer->buffer[i];

		if (ch == '\r') {
			continue;
		}

		if (req->responseHeaderParseState == RES_HEADER_STATE_PROTOCOL) {
			if (ch == ' ') {
				req->responseHeaderParseState = RES_HEADER_STATE_STATUS_CODE;
				continue;
			}
			//Don't really store protocol name anywhere
		}
		if (req->responseHeaderParseState == RES_HEADER_STATE_STATUS_CODE) {
			if (ch == ' ') {
				req->responseHeaderParseState = RES_HEADER_STATE_STATUS_MSG;
				continue;
			}
			stringAppendChar(req->responseStatusCode, ch);
			continue;
		}
		if (req->responseHeaderParseState == RES_HEADER_STATE_STATUS_MSG) {
			if (ch == '\n') {
				req->responseHeaderParseState = RES_HEADER_STATE_DONE;
				continue;
			}
			stringAppendChar(req->responseStatusMessage, ch);
			continue;
		}
	}

}

int schedule_write_to_client(ProxyServer *p, Request *req) {
	assert(IS_OPEN(req->clientFd));

	if (req->clientIOFlag & RW_STATE_WRITE) {
		//Already writing
		_info("Write to client is already scheduled.");

		return -2;
	}
	_info("Scheduling write to client: %d", req->clientFd);

	/*
	 * If we have not finished parsing header keep parsing it.
	 */
	if (req->responseHeaderParseState != RES_HEADER_STATE_DONE) {
		parse_response_header(p, req);
	}

	req->clientWriteCompleted = 0;
	req->clientIOFlag |= RW_STATE_WRITE;

	//Save the response data
	if (p->persistenceEnabled == 1 && req->responseFile != NULL) {
		size_t sz = fwrite(req->responseBuffer->buffer,
			req->responseBuffer->length,
			1,
			req->responseFile);
		if (sz == 0) {
			_info("Failed to write response data.");
		}
	}

	if (p->onQueueWriteToClient != NULL) {
		p->onQueueWriteToClient(p, req);
	}
	//Write immdiately 
	return handle_client_read(p, req);
}

int schedule_write_to_server(ProxyServer *p, Request *req) {
	assert(IS_OPEN(req->serverFd));

	if (req->serverIOFlag & RW_STATE_WRITE) {
		//Already writing
		_info("Write to server is already scheduled.");

		return -2;
	}

	_info("Scheduling write to server: %d", req->serverFd);

	req->serverWriteCompleted = 0;
	req->serverIOFlag |= RW_STATE_WRITE;

	//Save the request data
	if (p->persistenceEnabled == 1 && req->requestFile != NULL) {
		size_t sz = fwrite(req->requestBuffer->buffer,
			req->requestBuffer->length,
			1,
			req->requestFile);
		if (sz == 0) {
			_info("Failed to write request data to file.");
		}
	}

	if (p->onQueueWriteToServer != NULL) {
		p->onQueueWriteToServer(p, req);
	}

	//If connection is made, write immediately
	if (req->connectionEstablished == 1) {
		handle_server_read(p, req);
	}

	return 0;
}

#define PROT_NONE 0
#define PROT_METHOD 1
#define PROT_PROTOCOL 2
#define PROT_HOST 3
#define PROT_PORT 4
#define PROT_PATH 5

static void output_headers(ProxyServer *p, Request *req) {
	//Parse the protocol line
	int state = PROT_NONE;

	//Reset all parts
	req->method->length = 0;
	req->protocol->length = 0;
	req->host->length = 0;
	req->port->length = 0;
	req->path->length = 0;

	int firstSlash = 1;

	for (size_t i = 0; i < req->protocolLine->length; ++i) {
		char ch = stringGetChar(req->protocolLine, i);

		if (state == PROT_NONE) {
			state = PROT_METHOD;
		}

		if (state == PROT_METHOD) {
			if (ch == ' ') {
				state = PROT_PROTOCOL;
				//Determine if we need to do CONNECT tunneling
				if (strcmp(stringAsCString(req->method),
					"CONNECT") == 0) {
					req->requestState = REQ_CONNECT_TUNNEL_MODE;
					//In CONNECT request, protocol is not mentioned in URL
					//We go straight to host. Ex: example.com:80.
					state = PROT_HOST;
				}
				continue;
			}
			stringAppendChar(req->method, ch);
		}
		if (state == PROT_PROTOCOL) {
			if (ch == ':') {
				continue;
			}
			if (ch == '/') {
				if (firstSlash == 0) {
					state = PROT_HOST;
				}
				firstSlash = 0;
				continue;
			}
			stringAppendChar(req->protocol, ch);
		}
		if (state == PROT_HOST) {
			if (ch == ':') {
				state = PROT_PORT;
				continue;
			}
			if (ch == '/' || ch == ' ') {
				//End of host name
				state = PROT_PATH;
				continue;
			}
			stringAppendChar(req->host, ch);
		}
		if (state == PROT_PORT) {
			if (ch == '/' || ch == ' ') {
				//End of host name
				state = PROT_PATH;

				continue;
			}
			stringAppendChar(req->port, ch);
		}
		if (state == PROT_PATH) {
			if (req->path->length == 0) {
				//Add the leading '/' to path
				stringAppendChar(req->path, '/');
			}
			stringAppendChar(req->path, ch);
		}
	}

	//If we have already read a bit of body save it in overflow buffer
	req->requestBodyOverflowBuffer->length = 0;
	if (req->requestBuffer->position < req->requestBuffer->length) {
		bufferAppendBytes(req->requestBodyOverflowBuffer,
			req->requestBuffer->buffer + req->requestBuffer->position,
			req->requestBuffer->length - req->requestBuffer->position);
	}

	//Save the header and any overflow in the request buffer
	//and then schedule it for writing
	req->requestBuffer->length = 0;
	const char *space = " ";
	const char *newLine = "\r\n";

	bufferAppendBytes(req->requestBuffer,
		req->method->buffer, req->method->length);
	bufferAppendBytes(req->requestBuffer, space, 1);
	bufferAppendBytes(req->requestBuffer,
		req->path->buffer, req->path->length);
	bufferAppendBytes(req->requestBuffer, newLine, 2);
	for (size_t i = 0; i < req->headerNames->length; ++i) {
		String *name = arrayGet(req->headerNames, i);
		String *value = arrayGet(req->headerValues, i);

		bufferAppendBytes(req->requestBuffer,
			name->buffer, name->length);
		bufferAppendBytes(req->requestBuffer, ": ", 2);
		bufferAppendBytes(req->requestBuffer,
			value->buffer, value->length);
		bufferAppendBytes(req->requestBuffer, newLine, 2);
	}

	bufferAppendBytes(req->requestBuffer, newLine, 2);

	//Any overflow from body?
	if (req->requestBodyOverflowBuffer->length > 0) {
		bufferAppendBytes(req->requestBuffer,
			req->requestBodyOverflowBuffer->buffer,
			req->requestBodyOverflowBuffer->length);
	}

	//Notify listener
	if (p->onRequestHeaderParsed != NULL) {
		p->onRequestHeaderParsed(p, req);
	}

	//Connect to server if we haven't already
	if (IS_CLOSED(req->serverFd)) {
		int port = -1;
		int isHTTPS = strcmp("https",
			stringAsCString(req->protocol)) == 0;

		sscanf(stringAsCString(req->port),
			"%d", &port);
		if (port == -1) {
			port = isHTTPS ? 443 : 80;
		}

		int status = connect_to_server(p, req, stringAsCString(req->host), port);

		if (status < 0) {
			_info("Failed to connect to server. Disconnecting.");
			//Set the response status message
			req->responseStatusMessage->length = 0;
			stringAppendCString(req->responseStatusMessage,
				"Failed to connect to server.");
			/*
			 * Since the request can not be written to the server, we
			 * must manually save it in the request file. Otherwise, the
			 * request will not eb logged.
			 */
			persist_request_buffer(p, req);
			shutdown_channel(p, req);

			return;
		}

		assert(IS_OPEN(req->serverFd));

		//Enable read from server
		req->serverIOFlag |= RW_STATE_READ;
	}
	if (req->requestState != REQ_CONNECT_TUNNEL_MODE) {
		schedule_write_to_server(p, req);
	} else {
		//Return HTTP/1.0 200 Connection established to client.
		const char *response =
			"HTTP/1.0 200 Connection established\r\n\r\n";
		req->responseBuffer->length = 0;
		bufferAppendBytes(req->responseBuffer,
			response, strlen(response));
		schedule_write_to_client(p, req);
	}
}

/**
 * This method accumulates request header information by
 * parsing client request buffer.
 */
void read_request_header(ProxyServer *p, Request *req) {
	if (req->requestState == REQ_STATE_NONE) {
		req->protocolLine->length = 0;
		req->headerNames->length = 0;
		req->headerValues->length = 0;
		req->headerName = NULL;
		req->headerValue = NULL;
		req->requestState = REQ_PARSE_PROTOCOL;
	}

	//Reset the position
	req->requestBuffer->position = 0;

	for (size_t i = 0; i < req->requestBuffer->length; ++i) {
		char ch = bufferNextByte(req->requestBuffer);
		if (req->requestState == REQ_PARSE_PROTOCOL) {
			if (ch == '\r') {
				continue;
			}
			if (ch == '\n') {
				//We are done with protocol line
				req->requestState = REQ_PARSE_HEADER_NAME;
				req->headerName = NULL;
				req->headerValue = NULL;
				req->headerNames->length = 0;
				req->headerValues->length = 0;

				continue;
			}
			stringAppendChar(req->protocolLine, ch);
		} else if (req->requestState == REQ_PARSE_HEADER_NAME) {
			if (ch == '\r') {
				continue;
			}
			if (ch == '\n') {
				if (req->headerName == NULL) {
					//We are done parsing headers
					req->requestState = REQ_READ_BODY;
					output_headers(p, req);

					break;
				}
				continue;
			}
			if (ch == ':') {
				req->requestState = REQ_PARSE_HEADER_VALUE;
				arrayAdd(req->headerNames, req->headerName);
				req->headerName = NULL;

				continue;
			}

			if (req->headerName == NULL) {
				req->headerName = newString();
			}

			stringAppendChar(req->headerName, ch);
		} else if (req->requestState == REQ_PARSE_HEADER_VALUE) {
			if (ch == '\r') {
				continue;
			}
			if (ch == '\n') {
				req->requestState = REQ_PARSE_HEADER_NAME;
				arrayAdd(req->headerValues, req->headerValue);
				req->headerValue = NULL;

				continue;
			}
			if (req->headerValue == NULL) {
				req->headerValue = newString();
			}
			if ((req->headerValue->length == 0) &&
				ch == ' ') {
				//Skip leading space
				continue;
			}
			stringAppendChar(req->headerValue, ch);
		}
	}
}

/*
 * This function is called after any data is read from the client.
 * This function either parses the data as request header or
 * send the data as is to server if header is already parsed or
 * if the request is in CONNECT tunnel mode.
 */
int transfer_request_to_server(ProxyServer *p, Request *req) {
	if (req->requestState == REQ_READ_RESPONSE) {
		_info("A new request using an old connection.");
		//Mark the old request as has ended.
		on_end_request(p, req);

		req->requestState = REQ_STATE_NONE;
	}
	if (req->requestState == REQ_STATE_NONE) {
		//We starting a brand new request.
		on_begin_request(p, req);
	}

	if (req->requestState < REQ_READ_BODY) {
		read_request_header(p, req);
	} else {
		/*
		 * Transfer request data as is if header is already parsed
		 * or if we are in tunnel mode.
		 */
		return schedule_write_to_server(p, req);
	}

	return 0;
}

int on_client_disconnect(ProxyServer *p, Request *req) {
	shutdown_channel(p, req);

	return 0;
}

int on_server_disconnect(ProxyServer *p, Request *req) {
	shutdown_channel(p, req);

	return 0;
}

/*
 * Client has finished reading data. Let's write more if needed.
 */
int handle_client_read(ProxyServer *p, Request *req) {
	if (!(req->clientIOFlag & RW_STATE_WRITE)) {
		_info("We are not trying to write to client socket.");

		return -1;
	}
	if (req->responseBuffer->length == 0) {
		_info("Request buffer empty.");

		return -1;
	}
	if (req->responseBuffer->length == req->clientWriteCompleted) {
		_info("Write to client was already completed.");

		return -1;
	}

	char *buffer_start =
		req->responseBuffer->buffer + req->clientWriteCompleted;
	int bytesWritten = send(req->clientFd,
		buffer_start,
		req->responseBuffer->length - req->clientWriteCompleted, 0);

	_info("Written to client (%d) %d of %d bytes", req->clientFd,
		bytesWritten, req->responseBuffer->length);

	//DIE(p, bytesWritten, "Write to client failed.");

	if (bytesWritten < 0) {
		return bytesWritten;
	}

	if (bytesWritten == 0) {
		//Client has disconnected. We convert that to an error.
		_info("Client seems to have disconnected: %d", req->clientFd);

		return -1;
	}

	req->clientWriteCompleted += bytesWritten;

	if (req->clientWriteCompleted == req->responseBuffer->length) {
		//Clear flag
		req->clientIOFlag = req->clientIOFlag & (~RW_STATE_WRITE);
	}

	return 0;
}

/*
 * Server has read data. Let's write some more if needed.
 */
int handle_server_read(ProxyServer *p, Request *req) {
	if (!(req->serverIOFlag & RW_STATE_WRITE)) {
		_info("We are not trying to write to the server socket.");

		return -1;
	}
	if (req->requestBuffer->length == 0) {
		_info("Request buffer is empty.");

		return -1;
	}

	char *buffer_start = req->requestBuffer->buffer + req->serverWriteCompleted;
	int bytesWritten = send(req->serverFd,
		buffer_start,
		req->requestBuffer->length - req->serverWriteCompleted, 0);

	_info("Written to server (%d) %d of %d bytes",
		req->serverFd, bytesWritten,
		req->requestBuffer->length);

	if (bytesWritten < 0) {
		return bytesWritten;
	}
	if (bytesWritten == 0) {
		//Client has disconnected. We convert that to an error.
		return -1;
	}


	req->serverWriteCompleted += bytesWritten;

	if (req->serverWriteCompleted == req->requestBuffer->length) {
		//Clear flag
		req->serverIOFlag = req->serverIOFlag & (~RW_STATE_WRITE);
	}

	return 0;
}

/*
 * Client has written data. Let's read it.
 */
int handle_client_write(ProxyServer *p, Request *req) {
	//Check to see if there is any pending write to the server
	//If so, do not read the data from the client now.
	if (req->serverIOFlag & RW_STATE_WRITE) {
		_info("Write to server is pending. Will not read from client.");
#ifdef _WIN32
		//In Windows cause the FD_READ event to fire again
		//by doing a 0 byte dummy read. In UNIX the event keeps firing as long as there's data to read.
		recv(req->clientFd,
			req->requestBuffer->buffer,
			0, 0);
#endif
		return -1;
	}

	req->requestBuffer->length = 0;

	int bytesRead = recv(req->clientFd,
		req->requestBuffer->buffer,
		req->requestBuffer->capacity, 0);

	_info("Read request from client (%d) %d bytes",
		req->clientFd, bytesRead);

	if (bytesRead < 0) {
		return bytesRead;
	}
	if (bytesRead == 0) {
		//Client has disconnected. Only happens in UNIX.
		on_client_disconnect(p, req);

		return -1;
	}

	//fwrite(req->requestBuffer->buffer, 1, bytesRead, stdout);
	req->requestBuffer->length = bytesRead;
	transfer_request_to_server(p, req);

	return 0;
}

/*
 * Server has written data. Let's read it.
 */
int handle_server_write(ProxyServer *p, Request *req) {
	//Check to see if there is any pending write to the client
	//If so, do not read the data from the server now.
	if (req->clientIOFlag & RW_STATE_WRITE) {
		_info("Write to client is pending. Will not read from server.");
#ifdef _WIN32
		//In windows reenable the FD_READ event by doing a dummy read of 0 bytes
		recv(req->serverFd,
			req->responseBuffer->buffer,
			0, 0);
#endif
		return -1;
	}

	req->responseBuffer->length = 0;

	int bytesRead = recv(req->serverFd,
		req->responseBuffer->buffer,
		req->responseBuffer->capacity, 0);

	_info("Read response from server (%d) %d bytes",
		req->serverFd, bytesRead);

	/*
	 * We update the response end time here even though we are most likely
	 * in the middle of a response. This is needed because there is currently
	 * no reliable way to accurately determine when the response ended.
	 * The on_end_request may not get called for a long time for the last request
	 * in a keep alive situation.
	 *
	 * The current approach gives us the accuracy at the expense of calling
	 * gettimeofday() unnecessarily too many times.
	 */
	assert(os_gettimeofday(&req->responseEndTime) == 0);

	if (bytesRead < 0) {
		return bytesRead;
	}
	if (bytesRead == 0) {
		//Server has disconnected. Only happens in UNIX.
		on_server_disconnect(p, req);

		return -1;
	}

	//fwrite(req->responseBuffer->buffer, 1, bytesRead, stdout);
	/*
	 * In tunnel mode we stay in that model until connection is severed. Else,
	 * we move forward to REQ_READ_RESPONSE mode.
	 */
	if (req->requestState != REQ_CONNECT_TUNNEL_MODE) {
		req->requestState = REQ_READ_RESPONSE;
	}
	req->responseBuffer->length = bytesRead;
	schedule_write_to_client(p, req);

	return 0;
}

int add_client_fd(ProxyServer *p, int clientFd) {
	for (int i = 0; i < MAX_CLIENTS; ++i) {
		if (IS_CLOSED(p->requests[i].clientFd)) {
			//We have a free slot
			Request *req = p->requests + i;

			reset_request_state(req);

			req->clientFd = clientFd;

			return i;
		}
	}

	return 0;
}

int handle_client_connect(ProxyServer *p, Request *req) {
	//Start reading from client
	req->clientIOFlag = RW_STATE_READ;

	_info("Client connected: %d\n", req->clientFd);

	return 0;
}

int handle_control_command(ProxyServer *p) {
	char buff[5];

	int sz = os_read_pipe(p->controlPipe[0], buff, sizeof(buff));
	DIE(p, sz, "Failed to read control command.");

	_info("Received control command: %.*s", sz, buff);
	if (strncmp(buff, "Q", 1) == 0) {
		_info("Received stop control command.");
		p->runStatus = STOPPED;
	}

	return 0;
}

ProxyServer* newProxyServer(int port) {
	ProxyServer* p = (ProxyServer*)calloc(1, sizeof(ProxyServer));

	p->port = port;
	p->onError = default_on_error;
	p->persistenceFolder = newString();
	p->runStatus = STOPPED;
	p->serverSocket = INVALID_SOCKET;
	p->controlPipe[0] = p->controlPipe[1] = INVALID_PIPE; //Reset

	compat_new_proxy_server(p);

	for (int i = 0; i < MAX_CLIENTS; ++i) {
		Request *req = p->requests + i;

		req->uniqueId = newString();
		req->protocolLine = newString();
		req->method = newString();
		req->protocol = newString();
		req->host = newString();
		req->port = newString();
		req->path = newString();
		req->responseStatusMessage = newString();
		req->responseStatusCode = newStringWithCapacity(4);
		req->headerNames = newArray(10);
		req->headerValues = newArray(10);
		req->requestBuffer = newBufferWithCapacity(512);
		req->responseBuffer = newBufferWithCapacity(1024);
		req->requestBodyOverflowBuffer = newBufferWithCapacity(256);

		compat_new_request(req);

		reset_request_state(req);
	}

	return p;
}

void deleteProxyServer(ProxyServer *p) {
	for (int i = 0; i < MAX_CLIENTS; ++i) {
		Request *req = p->requests + i;

		deleteString(req->uniqueId);
		deleteString(req->protocolLine);
		deleteString(req->method);
		deleteString(req->protocol);
		deleteString(req->host);
		deleteString(req->port);
		deleteString(req->path);
		deleteString(req->responseStatusCode);
		deleteString(req->responseStatusMessage);

		deleteBuffer(req->requestBuffer);
		deleteBuffer(req->responseBuffer);
		deleteBuffer(req->requestBodyOverflowBuffer);

		deleteArray(req->headerNames);

		deleteArray(req->headerValues);

		compat_delete_request(req);
	}

	deleteString(p->persistenceFolder);

	compat_delete_proxy_server(p);

	free(p);
}

static int configure_persistence_folder(ProxyServer *p) {
	os_get_home_directory(p->persistenceFolder);
	
	stringAppendCString(p->persistenceFolder, "/.pixie");

	const char *dir = stringAsCString(p->persistenceFolder);

	_info("Creating persistence folder: %s", dir);

	int bExists;
	int status = os_mkdir(dir);
	DIE(p, status, "Failed to create persistence folder.");

	return 0;
}

int proxyServerStart(ProxyServer* p) {
	if (p->runStatus == RUNNING) {
		DIE(p, -1, "Server is already running.");
	}

	int status;

	//Get the folder to persist data
	configure_persistence_folder(p);

	//Create the server control pipes
	status = os_create_pipe(p->controlPipe);
	DIE(p, status, "Failed to create server control pipe.");

	status = create_server_socket(p);
	DIE(p, status, "Failed to create server socket.");

	server_loop(p);

	os_close_socket(p->serverSocket);
	p->serverSocket = INVALID_SOCKET;

	os_close_pipe(p->controlPipe[0]);
	os_close_pipe(p->controlPipe[1]);
	p->controlPipe[0] = p->controlPipe[1] = INVALID_PIPE; //Reset

	//Close any open network connections
	for (int i = 0; i < MAX_CLIENTS; ++i) {
		Request *req = p->requests + i;

		if (IS_OPEN(req->clientFd) || IS_OPEN(req->serverFd)) {
			shutdown_channel(p, req);
		}
	}

	//Reset all server state
	p->isInBackgroundMode = 0;
	p->runStatus = STOPPED;
	p->persistenceFolder->length = 0;

	return 0;
}

int send_control_command(ProxyServer *p, char *cmd, int len) {
	int sz = os_write_pipe(p->controlPipe[1], cmd, len);

	DIE(p, sz, "Failed to write control command.");

	return 0;
}

int proxyServerStop(ProxyServer *p) {
	if (p->runStatus == STOPPED) {
		DIE(p, -1, "Server is already stopped.");
	}

	int status = send_control_command(p, "Q", 1);

	if (p->isInBackgroundMode != 1) {
		return 0;
	}

	if (status >= 0) {
		//Wait for the thread to end.
		_info("Waiting for background thread to end.");
		status = os_join_thread(p->backgroundThreadId);
		DIE(p, status, "Failed to wait for background thread to end.");
		_info("Done waiting for background thread to end.");
	} else {
		//May be try to kill the thread
	}

	return 0;
}

static int _bgStartHelper(void *p) {
	proxyServerStart((ProxyServer*)p);

	_info("Background thread exiting.");
	return 0;
}

int proxyServerStartInBackground(ProxyServer* server) {
	_info("Creating background thread to run the server.");

	int status = os_create_thread(&(server->backgroundThreadId),
		_bgStartHelper, server);

	DIE(server, status, "Failed to create background thread.");

	server->isInBackgroundMode = 1;

	return 0;
}
