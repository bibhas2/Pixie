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

#ifdef _WIN32
#define IS_CLOSED(fd) (fd == INVALID_SOCKET)
#define IS_OPEN(fd) (fd != INVALID_SOCKET)
#define INVALID_PIPE INVALID_HANDLE
int close_socket(SOCKET s);
int close_pipe(HANDLE p);
#else
#define IS_CLOSED(fd) (fd < 0)
#define IS_OPEN(fd) (fd >= 0)
#define INVALID_SOCKET -1
#define INVALID_PIPE -1
int close_socket(int s);
int close_pipe(int p);
#endif
