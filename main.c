#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "Proxy.h"

static void print_request_start(ProxyServer *p, Request *req) {
	printf("New request with ID: %s\n",
		stringAsCString(req->uniqueId));
}
static void print_request_end(ProxyServer *p, Request *req) {
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

	p->persistenceEnabled = 1;
	p->onBeginRequest = print_request_start;
	p->onEndRequest = print_request_end;

	proxyServerStartInBackground(p);
	
	char buff[128];
	while (1) {
		printf("Enter quit to stop server.\n");
		fgets(buff, sizeof(buff), stdin);

		if (strncmp(buff, "quit", 4) == 0) {
			printf("Stopping server...\n");
			proxyServerStop(p);
			puts("Hit enter to continue.");
			getchar();
			break;
		}
	}

	deleteProxyServer(p);
}
