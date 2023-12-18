#ifndef LSI_CORE_H__
#define LSI_CORE_H__

#include "lsi_core_server.h"
#include "lsi_core_socket.h"
#include "lua.h"

#define DEFAULT_BUFFER_SIZE 1024 // 1 KB

int luaopen_lua_simple_ipc_core(lua_State* L);

#endif /* LSI_CORE_H__ */