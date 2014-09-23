int connect_to_server(ProxyServer *p, Request *req, const char *host, int port);
int server_loop(ProxyServer *p);
void _info(const char* fmt, ...);
int shutdown_channel(ProxyServer *p, Request *req);
int add_client_fd(ProxyServer *p, int clientFd);
int handle_client_connect(ProxyServer *p, Request *req);
int handle_control_command(ProxyServer *p);
int handle_client_write(ProxyServer *p, int position);
int handle_client_read(ProxyServer *p, int position);
int handle_server_read(ProxyServer *p, int position);
int handle_server_write(ProxyServer *p, int position);
int disconnect_clients(ProxyServer *p);

#define DIE(p, value, msg) if (value < 0) {if (p->onError != NULL) {p->onError(msg);} return value;}
