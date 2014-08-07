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

#include "../Cute/String.h"
#include "Proxy.h"

#define DIE(p, value, msg) if (value < 0) {if (p->onError != NULL) {p->onError(msg);} return value;}

#define RW_STATE_NONE 0
#define RW_STATE_READ 2
#define RW_STATE_WRITE 4
 
static void default_on_error(const char *msg) {
	perror(msg);
}

static void
_info(const char* fmt, ...) {
        va_list ap;

        va_start(ap, fmt);

        printf("INFO: ");
        vprintf(fmt, ap);
        printf("\n");
}

int disconnect_clients(ProxyServer *p) {

	return 0;
}

int connect_to_server(ProxyServer *p, const char *host, int port) {
        _info("Connecting to %s:%d", host, port);

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

        status = connect(sock, res->ai_addr, res->ai_addrlen);
        DIE(p, status, "Failed to connect to port.");

        status = fcntl(sock, F_SETFL, O_NONBLOCK);
        DIE(p, status, "Failed to set non blocking mode for socket.");

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

int transfer_request_to_server(ProxyServer *p, Request *req) {
	if (req->serverFd == 0) {
		req->serverFd = connect_to_server(p, 
			"localhost", 80);

		req->serverIOFlag |= RW_STATE_READ;
	}
	return schedule_write_to_server(req);
}

int shutdown_channel(ProxyServer *p, Request *req) {
	_info("Shutting down channel. Client %d server %d",
		req->clientFd, req->serverFd);

	close(req->clientFd);
	close(req->serverFd);

	req->clientFd = 0;
	req->serverFd = 0;
	req->clientIOFlag = RW_STATE_NONE;
	req->serverIOFlag = RW_STATE_NONE;
	req->requestSize = 0;
	req->responseSize = 0;
	req->clientWriteCompleted = 0;
	req->serverWriteCompleted = 0;

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
        if (req->responseSize == 0) {
                _info("Request buffer not setup.");

                return -1;
        }
        if (req->responseSize == req->clientWriteCompleted) {
                _info("Write to client was already completed.");

                return -1;
        }

        char *buffer_start = req->responseBuffer + req->clientWriteCompleted;
        int bytesWritten = write(req->clientFd,
                buffer_start,
                req->responseSize - req->clientWriteCompleted);

        _info("Written to client %d of %d bytes", bytesWritten, req->responseSize);

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

	if (req->clientWriteCompleted == req->responseSize) {
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

        if (!(req->serverIOFlag & RW_STATE_WRITE)) {
                _info("We are not trying to write to the server socket.");

                return -1;
        }
        if (req->requestSize == 0) {
                _info("request buffer not setup.");

                return -1;
        }

        char *buffer_start = req->requestBuffer + req->serverWriteCompleted;
        int bytesWritten = write(req->serverFd,
                buffer_start,
                req->requestSize - req->serverWriteCompleted);

        _info("Written to server %d of %d bytes", bytesWritten, req->requestSize);

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

	if (req->serverWriteCompleted == req->requestSize) {
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

	req->requestSize = 0;

        int bytesRead = read(req->clientFd,
                req->requestBuffer,
                sizeof(req->requestBuffer));

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

	req->requestSize = bytesRead;
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

	req->responseSize = 0;

        int bytesRead = read(req->serverFd,
                req->responseBuffer,
                sizeof(req->responseBuffer));

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

	req->responseSize = bytesRead;
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
			_info("Adding server to read FD");
                        FD_SET(req->serverFd, pReadFdSet);
                } 
		if (req->serverIOFlag & RW_STATE_WRITE) {
			_info("Adding server to write FD");
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
                        req->clientIOFlag = RW_STATE_NONE;
                        req->serverIOFlag = RW_STATE_NONE;
                        req->requestSize = 0;
                        req->responseSize = 0;
                        req->clientWriteCompleted = 0;
                        req->serverWriteCompleted = 0;

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

                timeout.tv_sec = 60 * 5;
                timeout.tv_usec = 0;

                int numEvents = select(FD_SETSIZE, &readFdSet, &writeFdSet, NULL, &timeout);
                DIE(p, numEvents, "select() failed.");

                if (numEvents == 0) {
                        _info("select() timed out.");

                        break;
                }
                //Make sense out of the event
                if (FD_ISSET(p->serverSocket, &readFdSet)) {
                        _info("Client is connecting...");
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

	return p;
}

void deleteProxyServer(ProxyServer *p) {
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


