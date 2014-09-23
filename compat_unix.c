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

#include "Proxy.h"
#include "private.h"


/*
 * Asynchronously connects to a server.
 * Returns 0 in case of success else an error status.
 */
int connect_to_server(ProxyServer *p, Request *req, const char *host, int port) {
	_info("Connecting to %s:%d for client %d", host, port, req->clientFd);

	assert(IS_CLOSED(req->serverFd));

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

static int handle_server_connection_completed(ProxyServer *p, Request *req) {
	assert (req->connectionEstablished == 0);
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
		//Set the response status message
		req->responseStatusMessage->length = 0;
		stringAppendCString(req->responseStatusMessage,
			"Failed to connect to server.");
		shutdown_channel(p, req);
		DIE(p, -1, "Failed to connect to server.");
	}
	//Connection was successful
	_info("Connection was successful.");
	req->connectionEstablished = 1;

	return 0;
}

static void
populate_fd_set(ProxyServer *p, fd_set *pReadFdSet, fd_set *pWriteFdSet) {
	FD_ZERO(pReadFdSet);
	FD_ZERO(pWriteFdSet);

	//Set the server socket
	FD_SET(p->serverSocket, pReadFdSet);
	FD_SET(p->controlPipe[0], pReadFdSet);

	//Set the clients
	for (int i = 0; i < MAX_CLIENTS; ++i) {
		Request *req = p->requests + i;

		if (IS_CLOSED(req->clientFd)) {
			continue;
		}

		if (req->clientIOFlag & RW_STATE_READ) {
			FD_SET(req->clientFd, pReadFdSet);
		}
		if (req->clientIOFlag & RW_STATE_WRITE) {
			FD_SET(req->clientFd, pWriteFdSet);
		}

		if (IS_CLOSED(req->serverFd)) {
			//No server connection yet. Skip the rest.
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

int server_loop(ProxyServer *p) {
	fd_set readFdSet, writeFdSet;
	struct timeval timeout;

	p->runStatus = RUNNING;

	while (p->runStatus == RUNNING) {
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
		}
		else if (FD_ISSET(p->controlPipe[0], &readFdSet)) {
			handle_control_command(p);
		} else {
			for (int i = 0; i < MAX_CLIENTS; ++i) {
				if (IS_CLOSED(p->requests[i].clientFd)) {
					//This channel is not in use
					continue;
				}
				if (FD_ISSET(p->requests[i].clientFd, &readFdSet)) {
					handle_client_write(p, i);
				} else if (FD_ISSET(p->requests[i].clientFd, &writeFdSet)) {
					handle_client_read(p, i);
				}
				/*
				 * We need to try to write to a server first before we try to read
				 * to catch any asynch connection error. If we read first,
				 * system seems to be overwriting the connection error and we
				 * can't detect error using getsockopt(SO_ERROR) any more.
				 */
				if (IS_CLOSED(p->requests[i].serverFd)) {
					//Server not connected yet
					continue;
				}
				if (FD_ISSET(p->requests[i].serverFd, &writeFdSet)) {
					if (p->requests[i].connectionEstablished == 0) {
						handle_server_connection_completed(p, p->requests + i);
					} else {
						handle_server_read(p, i);
					}
				} else if (FD_ISSET(p->requests[i].serverFd, &readFdSet)) {
					handle_server_write(p, i);
				}
			}
		}
	}
	_info("Server shutting down.");
	disconnect_clients(p);

	return 0;
}

int close_socket(int s) {
	return close(s);
}

int close_pipe(int p) {
	return close(p);
}
