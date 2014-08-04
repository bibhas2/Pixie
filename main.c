#include "Proxy.h"

int main() {
	ProxyServer *p = newProxyServer(8080);

	proxyServerStart(p);

	deleteProxyServer(p);
}
