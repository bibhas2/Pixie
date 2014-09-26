int connect_to_server(ProxyServer *p, Request *req, const char *host, int port);
int server_loop(ProxyServer *p);
int create_server_socket(ProxyServer *p);
int compat_new_proxy_server(ProxyServer *p);
int compat_new_request(Request *req);
int compat_delete_proxy_server(ProxyServer *p);
int compat_delete_request(Request *req);

void _info(const char* fmt, ...);
int shutdown_channel(ProxyServer *p, Request *req);
int add_client_fd(ProxyServer *p, int clientFd);
int handle_client_connect(ProxyServer *p, Request *req);
int handle_control_command(ProxyServer *p);
int handle_client_write(ProxyServer *p, Request *req);
int handle_client_read(ProxyServer *p, Request *req);
int handle_server_read(ProxyServer *p, Request *req);
int handle_server_write(ProxyServer *p, Request *req);
int disconnect_clients(ProxyServer *p);
int on_client_disconnect(ProxyServer *p, Request *req);
int on_server_disconnect(ProxyServer *p, Request *req);

#define DIE(p, value, msg) if (value < 0) {if (p->onError != NULL) {p->onError(msg);} return value;}
#define DIE_IF(p, cond, msg) if (cond) {if (p->onError != NULL) {p->onError(msg);} return -1;}

#define RW_STATE_NONE 0
#define RW_STATE_READ 2
#define RW_STATE_WRITE 4

#ifdef _WIN32
#define IS_CLOSED(fd) (fd == INVALID_SOCKET)
#define IS_OPEN(fd) (fd != INVALID_SOCKET)
#define INVALID_PIPE INVALID_HANDLE_VALUE
#define snprintf _snprintf

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
int os_close_socket(int s);
int os_close_pipe(int p);
int os_create_thread(pthread_t *thread, int (*start_routine)(void*), void *arg);
int os_join_thread(pthread_t thread);
int os_create_pipe(int fdList[2]);
int os_read_pipe(int fd, void *buffer, size_t size);
int os_write_pipe(int fd, void *buffer, size_t size);
#endif

int os_gettimeofday(struct timeval* p);
int os_mkdir(const char *dir); //Returns 0 if failed because existing directory
void os_get_home_directory(String *path);