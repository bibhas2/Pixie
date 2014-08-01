#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <errno.h>
#include <fcntl.h>

#include "../Cute/String.h"
#include "Proxy.h"

#define DIE(p, value, msg) if (value < 0) {if (p->onError != NULL) {p->onError(msg);} return;}

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

void on_write_to_client_completed(ProxyServer *p, Request *req) {
}

void on_write_to_server_completed(ProxyServer *p, Request *req) {
}

void on_read_from_client_completed(ProxyServer *p, Request *req) {
}

void on_read_from_server_completed(ProxyServer *p, Request *req) {
}

void handle_client_disconnect(ProxyServer *p, Request *req) {
}

void handle_server_disconnect(ProxyServer *p, Request *req) {
}

int handle_client_connect(ProxyServer *p, Request *req) {
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
        if (req->clientWriteLength == 0) {
                _info("Read buffer not setup.");

                return -1;
        }
        if (req->clientWriteLength == req->clientWriteCompleted) {
                _info("Read was already completed.");

                return -1;
        }

        char *buffer_start = req->clientWriteBuffer + req->clientWriteCompleted;
        int bytesWritten = write(req->clientFd,
                buffer_start,
                req->clientWriteLength - req->clientWriteCompleted);

        _info("Write %d of %d bytes", bytesWrite, req->clientWriteLength);

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

	req->clientWriteCompleted += bytesWrite;

	if (req->clientWriteCompleted == req->clientWriteLength) {
		on_write_to_client_completed(p, req);
	}
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
        if (req->serverWriteLength == 0) {
                _info("Read buffer not setup.");

                return -1;
        }
        if (req->serverWriteLength == req->serverWriteCompleted) {
                _info("Read was already completed.");

                return -1;
        }

        char *buffer_start = req->serverWriteBuffer + req->serverWriteCompleted;
        int bytesWritten = write(req->serverFd,
                buffer_start,
                req->serverWriteLength - req->serverWriteCompleted);

        _info("Write %d of %d bytes", bytesWritten, req->serverWriteLength);

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

	if (req->serverWriteCompleted == req->serverWriteLength) {
		on_write_to_server_completed(p, req);
	}
}

/*
 * Client has written data. Let's read it.
 */
int handle_client_write(ProxyServer *p, int position) {
	Request *req = p->requests + position;

        if (!(req->clientIOFlag & RW_STATE_READ)) {
                _info("We are not trying to read from client socket.");

                return -1;
        }
        if (req->clientReadLength == 0) {
                _info("Read buffer not setup.");

                return -1;
        }
        if (req->clientReadLength == req->clientReadCompleted) {
                _info("Read was already completed.");

                return -1;
        }

        char *buffer_start = req->clientReadBuffer + req->clientReadCompleted;
        int bytesRead = read(req->clientFd,
                buffer_start,
                req->clientReadLength - req->clientReadCompleted);

        _info("Read %d of %d bytes", bytesRead, req->clientReadLength);

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

	req->clientReadCompleted += bytesRead;

	if (req->clientReadCompleted == req->clientReadLength) {
		on_read_from_client_completed(p, req);
	}
}

/*
 * Server has written data. Let's read it.
 */
int handle_server_write(ProxyServer *p, int position) {
	Request *req = p->requests + position;

        if (!(req->serverIOFlag & RW_STATE_READ)) {
                _info("We are not trying to read from server socket.");

                return -1;
        }
        if (req->serverReadLength == 0) {
                _info("Read buffer not setup.");

                return -1;
        }
        if (req->serverReadLength == req->serverReadCompleted) {
                _info("Read was already completed.");

                return -1;
        }

        char *buffer_start = req->serverReadBuffer + req->serverReadCompleted;
        int bytesRead = read(req->serverFd,
                buffer_start,
                req->serverReadLength - req->serverReadCompleted);

        _info("Read %d of %d bytes", bytesRead, req->serverReadLength);

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

	req->serverReadCompleted += bytesRead;

	if (req->serverReadCompleted == req->serverReadLength) {
		on_read_from_server_completed(p, req);
	}
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
                } else if (req->clientIOFlag & RW_STATE_WRITE) {
                        FD_SET(req->clientFd, pWriteFdSet);
                }

                if (req->serverFd == 0) {
                        continue;
                }

                if (req->serverIOFlag & RW_STATE_READ) {
                        FD_SET(req->serverFd, pReadFdSet);
                } else if (req->serverIOFlag & RW_STATE_WRITE) {
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
                        req->clientReadLength = 0;
                        req->clientWriteLength = 0;
                        req->clientReadCompleted = 0;
                        req->clientWriteCompleted = 0;
                        req->serverReadLength = 0;
                        req->serverWriteLength = 0;
                        req->serverReadCompleted = 0;
                        req->serverWriteCompleted = 0;

                        return i;
                }
        }

        return -1;
}

void
server_loop(ProxyServer *p) {
        fd_set readFdSet, writeFdSet;
        struct timeval timeout;

        while (1) {
                populate_fd_set(p, &readFdSet, &writeFdSet);

                timeout.tv_sec = 60;
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
}

ProxyServer* newProxyServer(int port) {
	ProxyServer* p = (ProxyServer*) calloc(1, sizeof(ProxyServer));

	p->port = port;
	p->onError = default_on_error;
}

void deleteProxyServer(ProxyServer *p) {
	free(p);
}


void proxyServerStart(ProxyServer* p) {
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
}

