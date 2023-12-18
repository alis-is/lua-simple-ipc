#include "lsi_core.h"
#include "lauxlib.h"
#include "lua.h"
#include "lutil.h"

static const struct luaL_Reg lsiCore[] = {
    {"listen", lsi_listen},
    {"connect", lsi_socket_connect},
    {NULL, NULL},
};

int
luaopen_lua_simple_ipc_core(lua_State* L) {
    lsi_create_server_meta(L);
    lsi_create_socket_meta(L);

    lua_newtable(L);
    luaL_setfuncs(L, lsiCore, 0);
    return 1;
}