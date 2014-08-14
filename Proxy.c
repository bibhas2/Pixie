#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/time.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>

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
 * Initialize all request state flags to default so that
 * the request can be reused again for another TCP connection.
 */
static void reset_request_state(Request *req) {
	req->clientIOFlag = RW_STATE_NONE;
	req->serverIOFlag = RW_STATE_NONE;
	req->clientWriteCompleted = 0;
	req->serverWriteCompleted = 0;
	req->headerNames->length = 0;
	req->headerValues->length = 0;
	req->headerName = NULL;
	req->headerValue = NULL;
	req->requestState = REQ_STATE_NONE;
	req->connectionEstablished = 0;
}

int shutdown_channel(ProxyServer *p, Request *req) {
	_info("Shutting down channel. Client %d server %d",
		req->clientFd, req->serverFd);

	close(req->clientFd);
	close(req->serverFd);
	req->clientFd = 0;
	req->serverFd = 0;

	reset_request_state(req);

	return 0;
}

int connect_to_server(ProxyServer *p, const char *host, int port) {
        _info("Non-blocking connect to %s:%d", host, port);

        char port_str[128];

        snprintf(port_str, sizeof(port_str), "%d", port);

        struct addrinfo hints, *res;

        memset(&hints, 0, sizeof(hints));
        hints.ai_family = PF_INET;
        hints.ai_socktype = SOCK_STREAM;
        _info("Resolving name...");
        int status = getaddrinfo(host, port_str, &hints, &res);
        DIE(p, status, "Failed to resolve address.");
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
	if (status < 0 && errno != EINPROGRESS) {
		close(sock);
		freeaddrinfo(res);

		DIE(p, status, "Failed to connect to port.");
	}

        freeaddrinfo(res);

	return sock;
}

int schedule_write_to_client(Request *req) {
        if (req->clientFd <= 0) {
		_info("Can not write to invalid client socket.");

                return -1;
        }
        if (req->clientIOFlag & RW_STATE_WRITE) {
                //Already writing
		_info("Write to client is already scheduled.");

                return -2;
        }
        req->clientWriteCompleted = 0;
        req->clientIOFlag |= RW_STATE_WRITE;

        _info("Scheduling write to client: %d", req->clientFd);

        return 0;
}

int schedule_write_to_server(Request *req) {
        if (req->serverFd <= 0) {
		_info("Can not write to invalid server socket.");

                return -1;
        }
        if (req->serverIOFlag & RW_STATE_WRITE) {
                //Already writing
		_info("Write to server is already scheduled.");

                return -2;
        }
        req->serverWriteCompleted = 0;
        req->serverIOFlag |= RW_STATE_WRITE;

        _info("Scheduling write to server: %d", req->serverFd);

        return 0;
}

#define PROT_NONE 0
#define PROT_METHOD 1
#define PROT_PROTOCOL 2
#define PROT_HOST 3
#define PROT_PORT 4
#define PROT_PATH 5

void output_headers(ProxyServer *p, Request *req) {
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

	if (req->serverFd == 0) {
		int port = -1;

		sscanf(stringAsCString(req->port),
			"%d", &port);
		if (port == -1) {
			port = 80;
		}

		req->serverFd = connect_to_server( 
			p, stringAsCString(req->host), port);

		if (req->serverFd < 0) {
			_info("Failed to connect to server. Disconnecting.");
			shutdown_channel(p, req);

			return;
		}
		//Enable read from server
		req->serverIOFlag |= RW_STATE_READ;
	}
	schedule_write_to_server(req);
}

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

int transfer_request_to_server(ProxyServer *p, Request *req) {
	if (req->requestState == REQ_READ_RESPONSE) {
		_info("A new request using an old connection.");
		req->requestState = REQ_STATE_NONE;
	}

	if (req->requestState < REQ_READ_BODY) {
		read_request_header(p, req);
	} else {
		return schedule_write_to_server(req);
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

        _info("Written to client %d of %d bytes", bytesWritten, req->responseBuffer->length);

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
		_info("Asynch connection has completed.");
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

        _info("Written to server %d of %d bytes", bytesWritten, 
		req->requestBuffer->length);

        if (bytesWritten < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        return -1;
                }
                //Read will block. Not an error.
                _info("Read block detected.");

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

        _info("Read request from client %d bytes", bytesRead);

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

	fwrite(req->requestBuffer->buffer, 1, bytesRead, stdout);
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

        _info("Read response from server %d bytes", bytesRead);

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
	req->requestState = REQ_READ_RESPONSE;
	req->responseBuffer->length = bytesRead;
	schedule_write_to_client(req);

	return 0;
}

void
populate_fd_set(ProxyServer *p, fd_set *pReadFdSet, fd_set *pWriteFdSet) {
        FD_ZERO(pReadFdSet);
        FD_ZERO(pWriteFdSet);

        //Set the server socket
        FD_SET(p->serverSocket, pReadFdSet);

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

                        req->clientFd = clientFd;
                        req->serverFd = 0;

			reset_request_state(req);

                        return i;
                }
        }

        return 0;
}

int handle_client_connect(ProxyServer *p, Request *req) {
	//Start reading from client
	req->clientIOFlag = RW_STATE_READ;

	return 0;
}

int server_loop(ProxyServer *p) {
        fd_set readFdSet, writeFdSet;
        struct timeval timeout;

        while (1) {
                populate_fd_set(p, &readFdSet, &writeFdSet);

                timeout.tv_sec = 60 * 1;
                timeout.tv_usec = 0;

                int numEvents = select(FD_SETSIZE, &readFdSet, &writeFdSet, NULL, &timeout);
                DIE(p, numEvents, "select() failed.");

                if (numEvents == 0) {
                        _info("select() timed out.");

                        break;
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
                } else {
			for (int i = 0; i < MAX_CLIENTS; ++i) {
				if (FD_ISSET(p->requests[i].clientFd, &readFdSet)) {
					handle_client_write(p, i);
				}
				if (FD_ISSET(p->requests[i].clientFd, &writeFdSet)) {
					handle_client_read(p, i);
				}
				if (FD_ISSET(p->requests[i].serverFd, &readFdSet)) {
					handle_server_write(p, i);
				}
				if (FD_ISSET(p->requests[i].serverFd, &writeFdSet)) {
					handle_server_read(p, i);
				}
			}
		}
	}
	disconnect_clients(p);

	return 0;
}

ProxyServer* newProxyServer(int port) {
	ProxyServer* p = (ProxyServer*) calloc(1, sizeof(ProxyServer));

	p->port = port;
	p->onError = default_on_error;

	for (int i = 0; i < MAX_CLIENTS; ++i) {
		Request *req = p->requests + i;
		
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

	free(p);
}


int proxyServerStart(ProxyServer* p) {
        int status;

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

	return 0;
}


