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
#define DIE_IF(p, cond, msg) if (cond) {if (p->onError != NULL) {p->onError(msg);} return -1;}

#define RW_STATE_NONE 0
#define RW_STATE_READ 2
#define RW_STATE_WRITE 4

#ifdef _WIN32
#define IS_CLOSED(fd) (fd == INVALID_SOCKET)
#define IS_OPEN(fd) (fd != INVALID_SOCKET)
#define INVALID_PIPE INVALID_HANDLE_VALUE
int create_server_socket(ProxyServer *p, SOCKET *sock);
int os_close_socket(SOCKET s);
int os_close_pipe(HANDLE p);
int os_create_thread(HANDLE *thread, int (*start_routine)(void*), void *arg);
int os_join_thread(HANDLE thread);
int os_create_pipe(HANDLE fdList[2]);
int os_read_pipe(HANDLE fd, void *buffer, size_t size);
int os_write_pipe(HANDLE fd, void *buffer, size_t size);
#else
#define IS_CLOSED(fd) (fd < 0)
#define IS_OPEN(fd) (fd >= 0)
#define INVALID_SOCKET -1
#define INVALID_PIPE -1
int create_server_socket(ProxyServer *p, int *sock);
int os_close_socket(int s);
int os_close_pipe(int p);
int os_create_thread(pthread_t *thread, int (*start_routine)(void*), void *arg);
int os_join_thread(pthread_t thread);
int os_create_pipe(int fdList[2]);
int os_read_pipe(int fd, void *buffer, size_t size);
int os_write_pipe(int fd, void *buffer, size_t size);
#endif
