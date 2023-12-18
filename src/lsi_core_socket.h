#ifndef LSI_CORE_SOCKET_H__
#define LSI_CORE_SOCKET_H__

#include "lsi_core.h"
#include "lua.h"

#define LSI_SOCKET_METATABLE "LSI_SOCKET"

typedef struct lsi_socket {
#ifdef _WIN32
    HANDLE hPipe;
#else
    int fd;
#endif
    int server_owned; // if server_owned the non-blocking mode can not be changed
    int closed;
} lsi_socket;

int lsi_create_socket_meta(lua_State* L);
int lsi_socket_connect(lua_State* L);

#endif /* LSI_CORE_SOCKET_H__ */