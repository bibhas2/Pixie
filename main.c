#include <stdio.h>
#include <unistd.h>
#include "Proxy.h"

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

	proxyServerStart(p);

	deleteProxyServer(p);
}
