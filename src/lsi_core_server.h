#ifndef LSI_CORE_SERVER_H__
#define LSI_CORE_SERVER_H__

#include "lsi_core.h"
#include "lua.h"

#define DEFAULT_MAX_CLIENTS  5

#define LSI_SERVER_METATABLE "LSI_SERVER"

#ifdef _WIN32
#define PIPE_TIMEOUT 5000

typedef struct {
    HANDLE hPipe;
    OVERLAPPED connectOverlap;
    OVERLAPPED dataOverlap;
    CHAR* buffer;
    DWORD bytesRead;
} PIPE_INSTANCE;
#endif

typedef struct lsi_server {
#ifndef _WIN32
    int fd;
#endif
    const char* path;
    size_t path_len;
    size_t max_clients;
    size_t buffer_size;
#ifdef _WIN32
    HANDLE* hEvents;
    PIPE_INSTANCE* instances;
#else
    size_t client_count;
    struct pollfd* fds;
    size_t nfds;
#endif
    int closed;
} lsi_server;

int lsi_listen(lua_State* L);
int lsi_create_server_meta(lua_State* L);

#endif /* LSI_CORE_SERVER_H__ */