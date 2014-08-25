#include <stdio.h>
#include <unistd.h>
#include "Proxy.h"

static void print_request_start(ProxyServer *p, Request *req) {
	printf("*** Request is starting. client: %d server: %d\n", req->clientFd, req->serverFd);
}
static void print_request_end(ProxyServer *p, Request *req) {
	printf("*** Request has finished. client: %d server: %d Host: %.*s\n", 
		req->clientFd, req->serverFd,
		req->host->length, req->host->buffer);
	if (req->serverFd == 0) {
		printf("Request buffer size: %d\n", req->requestBuffer->length);
	}
}

int main(int argc, char **argv) {
	int port = 8080;
	
	int c;

	while ((c = getopt(argc, argv, "vp:")) != -1) {
		if (c == 'v') {
			proxySetTrace(1);
		} else if (c == 'p') {
			if (optarg != NULL) {
				sscanf(optarg, "%d", &port);
			}
		}
	}

	ProxyServer *p = newProxyServer(port);

	p->onBeginRequest = print_request_start;
	p->onEndRequest = print_request_end;

	proxyServerStart(p);

	deleteProxyServer(p);
}
