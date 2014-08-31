#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>

#include "Proxy.h"

#define DIE(p, value, msg) if (value < 0) {if (p->onError != NULL) {p->onError(msg);} return value;}

#define RW_STATE_NONE 0
#define RW_STATE_READ 2
#define RW_STATE_WRITE 4
 
#define REQ_STATE_NONE 0
#define REQ_PARSE_PROTOCOL 1
#define REQ_PARSE_HEADER_NAME 2
#define REQ_PARSE_HEADER_VALUE 3
#define REQ_READ_BODY 4
#define REQ_READ_RESPONSE 5
#define REQ_CONNECT_TUNNEL_MODE 6

#define CMD_NONE 0
#define CMD_STOP 1

static int bTrace = 0;

static void default_on_error(const char *msg) {
	perror(msg);
}

void proxySetTrace(int t) {
	bTrace = t;
}

static void
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
	req->clientFd = 0;
	req->serverFd = 0;

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
	req->headerNames->length = 0;
	req->headerValues->length = 0;
	req->headerName = NULL;
	req->headerValue = NULL;
	req->requestState = REQ_STATE_NONE;
	req->connectionEstablished = 0;
	req->requestStartTime.tv_sec = 0;
	req->requestStartTime.tv_usec = 0;
	req->responseEndTime.tv_sec = 0;
	req->responseEndTime.tv_usec = 0;
}

static void on_begin_request(ProxyServer *p, Request *req) {
	/*
	 * Store the request start time. Also use it to generate a unique ID for
	 * the request.
	 */
	assert(gettimeofday(&req->requestStartTime, NULL) == 0);

	char uid[256];

	int sz = snprintf(uid, sizeof(uid), "%lu-%lu-%d",
		(unsigned long) req->requestStartTime.tv_sec,
		(unsigned long) req->requestStartTime.tv_usec,
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
				req->protocolLine->length, req->protocolLine->buffer);
			fprintf(req->metaFile, "protocol\n%.*s\n", 
				req->protocol->length, req->protocol->buffer);
			fprintf(req->metaFile, "host\n%.*s\n", 
				req->host->length, req->host->buffer);
			fprintf(req->metaFile, "port\n%.*s\n", 
				req->port->length, req->port->buffer);
			fprintf(req->metaFile, "path\n%.*s\n", 
				req->path->length, req->path->buffer);
			fprintf(req->metaFile, "request-start-seconds\n%lu\n",
				(unsigned long) req->requestStartTime.tv_sec);
			fprintf(req->metaFile, "request-start-microseconds\n%lu\n",
				(unsigned long) req->requestStartTime.tv_usec);
			fprintf(req->metaFile, "response-end-seconds\n%lu\n",
				(unsigned long) req->responseEndTime.tv_sec);
			fprintf(req->metaFile, "response-end-microseconds\n%lu\n",
				(unsigned long) req->responseEndTime.tv_usec);
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

	if (req->clientFd > 0) {
		close(req->clientFd);
	}
	if (req->serverFd > 0) {
		close(req->serverFd);
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

/*
 * Asynchronously connects to a server.
 * Returns 0 in case of success else an error status.
 */
int connect_to_server(ProxyServer *p, Request *req, const char *host, int port) {
        _info("Connecting to %s:%d for client %d", host, port, req->clientFd);

	assert(req->serverFd == 0);

        char port_str[128];

        snprintf(port_str, sizeof(port_str), "%d", port);

        struct addrinfo hints, *res;

        memset(&hints, 0, sizeof(hints));
        hints.ai_family = PF_INET;
        hints.ai_socktype = SOCK_STREAM;
        _info("Resolving name: %s.", host);
        int status = getaddrinfo(host, port_str, &hints, &res);
        DIE(p, status, "getaddrinfo() failed.");
        if (res == NULL) {
                _info("Failed to resolve address: %s", host);
                DIE(p, -1, "Failed to resolve address");
        }

        int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        DIE(p, sock, "Failed to open socket.");

	//Enable non-blocking I/O and connect
        status = fcntl(sock, F_SETFL, O_NONBLOCK);
        DIE(p, status, "Failed to set non blocking mode for socket.");
	//Connect in a non-blocking manner
        status = connect(sock, res->ai_addr, res->ai_addrlen);
	if (status < 0) {
		if (errno != EINPROGRESS) {
			close(sock);
			freeaddrinfo(res);

			DIE(p, status, "Failed to connect to server.");
		} else {
			req->connectionEstablished = 0; //Just to be safe
			req->serverFd = sock;
			_info("Server connection is pending: %d", req->serverFd);
		}
	} else {
		req->connectionEstablished = 1;
		req->serverFd = sock;
		_info("Server connected immediately: %d", req->serverFd);
	}

        freeaddrinfo(res);

	return 0;
}

int schedule_write_to_client(ProxyServer *p, Request *req) {
        if (req->clientFd <= 0) {
		_info("Can not write to invalid client socket.");

                return -1;
        }
        if (req->clientIOFlag & RW_STATE_WRITE) {
                //Already writing
		_info("Write to client is already scheduled.");

                return -2;
        }
        _info("Scheduling write to client: %d", req->clientFd);

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

        return 0;
}

int schedule_write_to_server(ProxyServer *p, Request *req) {
        if (req->serverFd <= 0) {
		_info("Can not write to invalid server socket.");

                return -1;
        }
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

	for (int i = 0; i < req->protocolLine->length; ++i) {
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

/*
	_info("Protcol line [%s]\n", stringAsCString(req->protocolLine));
	_info("Method [%s]\n", stringAsCString(req->method));
	_info("Protocol [%s]\n", stringAsCString(req->protocol));
	_info("Host [%s]\n", stringAsCString(req->host));
	_info("Port [%s]\n", stringAsCString(req->port));
	_info("Path [%s]\n", stringAsCString(req->path));

	for (int i = 0; i < req->headerNames->length; ++i) {
		String *name = arrayGet(req->headerNames, i);
		String *value = arrayGet(req->headerValues, i);

		_info("Header [%s][%s]\n",
			stringAsCString(name),
			stringAsCString(value));
	}
*/


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
	for (int i = 0; i < req->headerNames->length; ++i) {
		String *name = arrayGet(req->headerNames, i);
		String *value = arrayGet(req->headerValues, i);

		bufferAppendBytes(req->requestBuffer,
			name->buffer, name->length);
		bufferAppendBytes(req->requestBuffer, ":", 1);
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

	if (req->serverFd == 0) {
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
			/*
			 * Since the request can not be written to the server, we
			 * must manually save it in the request file. Otherwise, the 
			 * request will not eb logged.
			 */
			persist_request_buffer(p, req);
			shutdown_channel(p, req);

			return;
		}

		assert(req->serverFd > 0);

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

	for (int i = 0; i < req->requestBuffer->length; ++i) {
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
int handle_client_read(ProxyServer *p, int position) {
	Request *req = p->requests + position;

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
        int bytesWritten = write(req->clientFd,
                buffer_start,
                req->responseBuffer->length - req->clientWriteCompleted);

        _info("Written to client (%d) %d of %d bytes", req->clientFd, 
		bytesWritten, req->responseBuffer->length);

        if (bytesWritten < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        return -1;
                }
                //Read will block. Not an error.
                _info("Write block detected.");

                return 0;
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
int handle_server_read(ProxyServer *p, int position) {
	Request *req = p->requests + position;

	if (req->connectionEstablished == 0) {
		_info("Asynch connection has completed. Client %d server %d.",
			req->clientFd, req->serverFd);
		//Non-blocking connection is now done. See if it worked.
		int valopt;
		socklen_t lon = sizeof(int);

		int status = getsockopt(req->serverFd, SOL_SOCKET, SO_ERROR, (void*)(&valopt), &lon);
		if (status < 0) {
			shutdown_channel(p, req);
			DIE(p, status, "Error in getsockopt()");
		}
		//Check the value of valopt
		_info("SOL_SOCKET: %d", valopt);
		if (valopt) {
			//Connection failed
			shutdown_channel(p, req);
			DIE(p, -1, "Failed to connect to server.");
		}
		//Connection was successful
		_info("Connection was successful.");
		req->connectionEstablished = 1;

		return 0;
	}

        if (!(req->serverIOFlag & RW_STATE_WRITE)) {
                _info("We are not trying to write to the server socket.");

                return -1;
        }
        if (req->requestBuffer->length == 0) {
                _info("Request buffer is empty.");

                return -1;
        }

        char *buffer_start = req->requestBuffer->buffer + req->serverWriteCompleted;
        int bytesWritten = write(req->serverFd,
                buffer_start,
                req->requestBuffer->length - req->serverWriteCompleted);

        _info("Written to server (%d) %d of %d bytes", 
		req->serverFd, bytesWritten, 
		req->requestBuffer->length);

        if (bytesWritten < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        return -1;
                }
                //Read will block. Not an error.
                _info("Write block detected.");

                return 0;
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
int handle_client_write(ProxyServer *p, int position) {
	Request *req = p->requests + position;

	//Check to see if there is any pending write to the server
	//If so, do not read the data from the client now.
	if (req->serverIOFlag & RW_STATE_WRITE) {
		_info("Write to server is pending. Will not read from client.");
		
		return -1;
	}

	req->requestBuffer->length = 0;

        int bytesRead = read(req->clientFd,
                req->requestBuffer->buffer,
                req->requestBuffer->capacity);

        _info("Read request from client (%d) %d bytes", 
		req->clientFd, bytesRead);

        if (bytesRead < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        return -1;
                }
                //Read will block. Not an error.
                _info("Read block detected.");
                return 0;
        }
        if (bytesRead == 0) {
                //Client has disconnected. 
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
int handle_server_write(ProxyServer *p, int position) {
	Request *req = p->requests + position;

	//Check to see if there is any pending write to the client
	//If so, do not read the data from the server now.
	if (req->clientIOFlag & RW_STATE_WRITE) {
		_info("Write to client is pending. Will not read from server.");
		
		return -1;
	}

	req->responseBuffer->length = 0;

        int bytesRead = read(req->serverFd,
                req->responseBuffer->buffer,
                req->responseBuffer->capacity);

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
	assert(gettimeofday(&req->responseEndTime, NULL) == 0);

        if (bytesRead < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        return -1;
                }
                //Read will block. Not an error.
                _info("Read block detected.");
                return 0;
        }
        if (bytesRead == 0) {
                //Server has disconnected. 
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

void
populate_fd_set(ProxyServer *p, fd_set *pReadFdSet, fd_set *pWriteFdSet) {
        FD_ZERO(pReadFdSet);
        FD_ZERO(pWriteFdSet);

        //Set the server socket
        FD_SET(p->serverSocket, pReadFdSet);
        FD_SET(p->controlPipe[0], pReadFdSet);

        //Set the clients
        for (int i = 0; i < MAX_CLIENTS; ++i) {
                Request *req = p->requests + i;

                if (req->clientFd == 0) {
                        continue;
                }

                if (req->clientIOFlag & RW_STATE_READ) {
                        FD_SET(req->clientFd, pReadFdSet);
                } 
		if (req->clientIOFlag & RW_STATE_WRITE) {
                        FD_SET(req->clientFd, pWriteFdSet);
                }

                if (req->serverFd == 0) {
                        continue;
                }

                if (req->serverIOFlag & RW_STATE_READ) {
                        FD_SET(req->serverFd, pReadFdSet);
                } 
		if ((req->serverIOFlag & RW_STATE_WRITE) ||
			(req->connectionEstablished == 0)) {
                        FD_SET(req->serverFd, pWriteFdSet);
                }
        }
}

int add_client_fd(ProxyServer *p, int clientFd) {
        for (int i = 0; i < MAX_CLIENTS; ++i) {
                if (p->requests[i].clientFd == 0) {
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

	int sz = read(p->controlPipe[0], buff, sizeof(buff));
	DIE(p, sz, "Failed to read control command.");

	_info("Received control command: %.*s", sz, buff);
	if (strncmp(buff, "Q", 1) == 0) {
		_info("Received stop control command.");
		p->continueOperation = 0;
	}
}

int server_loop(ProxyServer *p) {
        fd_set readFdSet, writeFdSet;
        struct timeval timeout;

	p->continueOperation = 1;

        while (p->continueOperation == 1) {
                populate_fd_set(p, &readFdSet, &writeFdSet);

                timeout.tv_sec = 60 * 1;
                timeout.tv_usec = 0;

                int numEvents = select(FD_SETSIZE, &readFdSet, &writeFdSet, NULL, &timeout);
                DIE(p, numEvents, "select() failed.");

                if (numEvents == 0) {
                        _info("select() timed out. Looping back.");

                        continue;
                }
                //Make sense out of the event
                if (FD_ISSET(p->serverSocket, &readFdSet)) {
                        _info("Client connected.");
                        int clientFd = accept(p->serverSocket, NULL, NULL);

                        DIE(p, clientFd, "accept() failed.");

                        int position = add_client_fd(p, clientFd);

                        int status = fcntl(clientFd, F_SETFL, O_NONBLOCK);
                        DIE(p, status, 
				"Failed to set non blocking mode for client socket.");
			handle_client_connect(p, p->requests + position);
                } else if (FD_ISSET(p->controlPipe[0], &readFdSet)) {
			handle_control_command(p);
		} else {
			for (int i = 0; i < MAX_CLIENTS; ++i) {
				if (FD_ISSET(p->requests[i].clientFd, &readFdSet)) {
					handle_client_write(p, i);
				}
				if (FD_ISSET(p->requests[i].clientFd, &writeFdSet)) {
					handle_client_read(p, i);
				}
				/*
				 * We need to try to write a server first before we try to read
				 * to catch any asynch connection error. If we read first,
				 * system seems to be overwriting the connection error and we 
				 * can't detect error using getsockopt(SO_ERROR) any more.
				 */
				if (FD_ISSET(p->requests[i].serverFd, &writeFdSet)) {
					handle_server_read(p, i);
				}
				if (FD_ISSET(p->requests[i].serverFd, &readFdSet)) {
					handle_server_write(p, i);
				}
			}
		}
	}
	_info("Server shutting down.");
	disconnect_clients(p);

	return 0;
}

ProxyServer* newProxyServer(int port) {
	ProxyServer* p = (ProxyServer*) calloc(1, sizeof(ProxyServer));

	p->port = port;
	p->onError = default_on_error;
	p->persistenceFolder = newString();

	for (int i = 0; i < MAX_CLIENTS; ++i) {
		Request *req = p->requests + i;
		
		req->uniqueId = newString();
		req->protocolLine = newString();
		req->method = newString();
		req->protocol = newString();
		req->host = newString();
		req->port = newString();
		req->path = newString();
		req->headerNames = newArray(10);
		req->headerValues = newArray(10);
		req->requestBuffer = newBufferWithCapacity(512);
		req->responseBuffer = newBufferWithCapacity(1024);
		req->requestBodyOverflowBuffer = newBufferWithCapacity(256);

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

		deleteBuffer(req->requestBuffer);
		deleteBuffer(req->responseBuffer);
		deleteBuffer(req->requestBodyOverflowBuffer);

		for (int j = 0; j < req->headerNames->length; ++j) {
			deleteString(arrayGet(req->headerNames, j));
		}
		deleteArray(req->headerNames);

		for (int j = 0; j < req->headerValues->length; ++j) {
			deleteString(arrayGet(req->headerValues, j));
		}
		deleteArray(req->headerValues);
	}

	deleteString(p->persistenceFolder);

	free(p);
}

static int configure_persistence_folder(ProxyServer *p) {
	const char *home = getenv("HOME");

	if (home != NULL) {
		stringAppendCString(p->persistenceFolder, home);
	}

	stringAppendCString(p->persistenceFolder, "/.pixie");
	
	const char *dir = stringAsCString(p->persistenceFolder);

	_info("Creating persistence folder: %s", dir);

	int status = mkdir(dir, 0700);
	if (status < 0) {
		if (errno == EEXIST) {
			//Path already exists. It may not be a folder. For now do nothing.
			_info("Persistence folder already exists.");
		} else {
			DIE(p, status, "Failed to create persistence folder.");
		}
	}

	return 0;
}

int proxyServerStart(ProxyServer* p) {
        int status;

	//Get the folder to persist data
	configure_persistence_folder(p);

	//Create the server control pipes
	status = pipe(p->controlPipe);
	DIE(p, status, "Failed to create server control pipe.");

        int sock = socket(PF_INET, SOCK_STREAM, 0);

        DIE(p, sock, "Failed to open socket.");

        status = fcntl(sock, F_SETFL, O_NONBLOCK);
        DIE(p, status, 
		"Failed to set non blocking mode for server listener socket.");

        int reuse = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);

        struct sockaddr_in addr;

        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(p->port);

	_info("Proxy server binding to port: %d", p->port);
        status = bind(sock, (struct sockaddr*) &addr, sizeof(addr));

        DIE(p, status, "Failed to bind to port.");

        _info("Calling listen.");
        status = listen(sock, 10);
        _info("listen returned.");

        DIE(p, status, "Failed to listen.");

        p->serverSocket = sock;

        server_loop(p);

        close(sock);

        p->serverSocket = 0;

	close(p->controlPipe[0]);
	close(p->controlPipe[1]);

	return 0;
}

int send_control_command(ProxyServer *p, const char *cmd, int len) {
	int sz = write(p->controlPipe[1], cmd, len);

	DIE(p, sz, "Failed to write cotrol command.");
}

int proxyServerStop(ProxyServer *p) {
	send_control_command(p, "Q", 1);
}

static void * _bgStartHelper(void *p) {
	proxyServerStart((ProxyServer*) p);

	_info("Background thread exiting.");
	return NULL;
}

int proxyServerStartInBackground(ProxyServer* server) {
	_info("Creating background thread to run the server.");
	pthread_t t;
	int status = pthread_create(&t, NULL, _bgStartHelper, server);

	DIE(server, status, "Failed to create background thread.");
}
