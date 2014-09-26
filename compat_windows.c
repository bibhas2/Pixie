#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>

#include "Proxy.h"
#include "private.h"
#include <Shlobj.h>

/*
 * Asynchronously connects to a server.
 * Returns 0 in case of success else an error status.
 */
int connect_to_server(ProxyServer *p, Request *req, const char *host, int port) {
	_info("Connecting to %s:%d for client %d", host, port, req->clientFd);

	assert(IS_CLOSED(req->serverFd));

	struct addrinfo hints, *res;
	char port_str[128];

	_snprintf_s(port_str, (size_t) sizeof(port_str), (size_t) sizeof(port_str), "%d", port);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	_info("Resolving name: %s.", host);
	int status = getaddrinfo(host, port_str, &hints, &res);
	DIE_IF(p, status != 0, "getaddrinfo() failed.");
	DIE_IF(p, res == NULL, "Failed to resolve address.");

	SOCKET sock;
	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	DIE_IF(p, sock == INVALID_SOCKET, "socket function failed");

	//Enable non-blocking connect
	u_long iMode = 1;
	status = ioctlsocket(sock, FIONBIO, &iMode);
	DIE_IF(p, status == SOCKET_ERROR, "Failed to set socket in non-blocking mode.");

	// Connect to server.
	status = connect(sock, res->ai_addr, res->ai_addrlen);
	if (status == SOCKET_ERROR) {
		if (WSAGetLastError() == WSAEWOULDBLOCK) {
			req->connectionEstablished = 0; //Just to be safe
			req->serverFd = sock;
			_info("Server connection is pending: %d", req->serverFd);
		}
		else {
			closesocket(sock);
			freeaddrinfo(res);
			DIE_IF(p, 1, "Failed to connect imemdiately.");
		}
	}
	else {
		req->connectionEstablished = 1;
		req->serverFd = sock;
		_info("Server connected immediately: %d", req->serverFd);
	}

	freeaddrinfo(res);

	return 0;
}

static int handle_server_connection_completed(ProxyServer *p, Request *req, int error) {
	assert (req->connectionEstablished == 0);
	_info("Asynch connection has completed. Client %d server %d.",
		req->clientFd, req->serverFd);
	//Non-blocking connection is now done. See if it worked.
	if (error == 1) {
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

static DWORD
populate_fd_set(ProxyServer *p, WSAEVENT *eventList) {
	DWORD count = 0;
	eventList[count++] = p->serverEvent;
	WSAEventSelect(p->serverSocket, p->serverEvent, FD_ACCEPT); //Register for accept event

	//Deal with pipes later
	//FD_SET(p->controlPipe[0], pReadFdSet);

	//Set the clients
	for (int i = 0; i < MAX_CLIENTS; ++i) {
		Request *req = p->requests + i;

		if (IS_CLOSED(req->clientFd)) {
			continue;
		}

		int setting = FD_CLOSE;

		if (req->clientIOFlag & RW_STATE_READ) {
			setting |= FD_READ;
		}
		if (req->clientIOFlag & RW_STATE_WRITE) {
			setting |= FD_WRITE;
		}

		WSAEventSelect(req->clientFd, req->clientEvent, setting); //Register for client socket event
		eventList[count++] = req->clientEvent;

		if (IS_CLOSED(req->serverFd)) {
			//No server connection yet. Skip the rest.
			continue;
		}

		setting = FD_CLOSE;

		if (req->serverIOFlag & RW_STATE_READ) {
			setting |= FD_READ;
		}
		if (req->serverIOFlag & RW_STATE_WRITE) {
			setting |= FD_WRITE;
		}
		if (req->connectionEstablished == 0) {
			setting |= FD_CONNECT;
		}
		WSAEventSelect(req->serverFd, req->serverEvent, setting); //Register for server socket event
		eventList[count++] = req->serverEvent;
	}

	return count;
}

int server_loop(ProxyServer *p) {
	struct timeval timeout;
	WSANETWORKEVENTS netEvent;

	p->runStatus = RUNNING;

	WSAEVENT eventList[MAX_CLIENTS + 1];

	while (p->runStatus == RUNNING) {
		DWORD count = populate_fd_set(p, eventList);

		timeout.tv_sec = 60 * 1;
		timeout.tv_usec = 0;

		int status = WSAWaitForMultipleEvents(count, eventList, FALSE, WSA_INFINITE, FALSE);
		DIE_IF(p, status == WSA_WAIT_FAILED, "Failed to wait for events.");

		//Make sense out of the event
		status = WSAEnumNetworkEvents(p->serverSocket, p->serverEvent, &netEvent);
		assert(status != SOCKET_ERROR);

		if (netEvent.lNetworkEvents & FD_ACCEPT) {
			_info("Client connected.");
			SOCKET clientFd = accept(p->serverSocket, NULL, NULL);
			DIE(p, clientFd, "accept() failed.");

			int position = add_client_fd(p, clientFd);

			u_long iMode = 1;
			status = ioctlsocket(clientFd, FIONBIO, &iMode);
			DIE_IF(p, status == SOCKET_ERROR, "Failed to set socket in non-blocking mode.");

			handle_client_connect(p, p->requests + position);
		}
		/*else if (FD_ISSET(p->controlPipe[0], &readFdSet)) {
			handle_control_command(p);
		}*/ else {
			for (int i = 0; i < MAX_CLIENTS; ++i) {
				Request *req = p->requests + i;

				if (IS_CLOSED(req->clientFd)) {
					//This channel is not in use
					continue;
				}

				status = WSAEnumNetworkEvents(req->clientFd, req->clientEvent, &netEvent);
				assert(status != SOCKET_ERROR);

				if (netEvent.lNetworkEvents & FD_READ) {
					handle_client_write(p, i);
				}
				if (netEvent.lNetworkEvents & FD_WRITE) {
					_info("*** Client is writable.");
					handle_client_read(p, i);
				}
				if (netEvent.lNetworkEvents & FD_CLOSE) {
					on_client_disconnect(p, req);
				}

				if (IS_CLOSED(p->requests[i].serverFd)) {
					//Server not connected yet
					continue;
				}

				status = WSAEnumNetworkEvents(req->serverFd, req->serverEvent, &netEvent);
				assert(status != SOCKET_ERROR);

				if (netEvent.lNetworkEvents & FD_CONNECT) {
					int error = netEvent.iErrorCode[FD_CONNECT_BIT] != 0;

					handle_server_connection_completed(p, req, error);
				}
				if (netEvent.lNetworkEvents & FD_WRITE) {
					handle_server_read(p, i);
				}
				if (netEvent.lNetworkEvents & FD_READ) {
					handle_server_write(p, i);
				}
				if (netEvent.lNetworkEvents & FD_CLOSE) {
					on_server_disconnect(p, req);
				}
			}
		}
	}
	_info("Server shutting down.");
	disconnect_clients(p);

	return 0;
}

int create_server_socket(ProxyServer *p) {
	SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	DIE_IF(p, sock == INVALID_SOCKET, "Failed to open socket.");

	struct sockaddr_in addr;

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(p->port);
	
	_info("Proxy server binding to port: %d", p->port);
	int status = bind(sock, (SOCKADDR *)&addr, sizeof(addr));
	DIE_IF(p, status == SOCKET_ERROR, "Failed to bind to port");
	status = listen(sock, 10);
	DIE_IF(p, status == SOCKET_ERROR, "Failed to listen on socket.");

	p->serverSocket = sock;

	return 0;
}

int compat_new_proxy_server(ProxyServer *p) {
	p->serverEvent = WSACreateEvent();

	return 0;
}

int compat_new_request(Request *req) {
	req->serverEvent = WSACreateEvent();
	req->clientEvent = WSACreateEvent();

	return 0;
}

int compat_delete_proxy_server(ProxyServer *p) {
	WSACloseEvent(p->serverEvent);

	return 0;
}

int compat_delete_request(Request *req) {
	WSACloseEvent(req->serverEvent);
	WSACloseEvent(req->clientEvent);

	return 0;
}

int os_close_socket(SOCKET s) {
	return closesocket(s) == 0 ? 0 : -1;
}

int os_close_pipe(HANDLE p) {
	return CloseHandle(p) == 0 ? -1 : 0;
}

int os_create_thread(HANDLE *thread, int (*start_routine)(void*), void *arg) {
	*thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE ) start_routine, arg, 0, NULL);
	return *thread == NULL ? -1 : 0;
}

int os_join_thread(HANDLE thread) {
	DWORD status = WaitForSingleObject(thread, INFINITE);
	CloseHandle(thread);

	return status == WAIT_FAILED ? -1 : 0;
}

int os_create_pipe(HANDLE fdList[2]) {
	return CreatePipe(fdList, fdList + 1, NULL, 0) == 0 ? -1 : 0;
}

int os_read_pipe(HANDLE fd, void *buffer, size_t size) {
	DWORD sizeRead;

	BOOL status = ReadFile(fd, buffer, size, &sizeRead, NULL);

	return status == 0 ? -1 : sizeRead;
}

int os_write_pipe(HANDLE fd, void *buffer, size_t size) {
	DWORD sizeWritten;

	BOOL status = WriteFile(fd, buffer, size, &sizeWritten, NULL);

	return status == 0 ? -1 : sizeWritten;
}
//Stolen from glibc for MingW
int os_gettimeofday(struct timeval* p) {
	union {
		long long ns100; /*time since 1 Jan 1601 in 100ns units */
		FILETIME ft;
	} now;

	GetSystemTimeAsFileTime(&(now.ft));
	p->tv_usec = (long)((now.ns100 / 10LL) % 1000000LL);
	p->tv_sec = (long)((now.ns100 - (116444736000000000LL)) / 10000000LL);
	
	return 0;
}

int os_mkdir(const char *dir) {
	if (CreateDirectoryA(dir, NULL) == 0) {
		return GetLastError() == ERROR_ALREADY_EXISTS ? 0 : -1;
	}

	return 0;
}

void os_get_home_directory(String *path) {
	char buff[MAX_PATH];

	if (SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, buff) == S_OK) {
		stringAppendCString(path, buff);
	}
}