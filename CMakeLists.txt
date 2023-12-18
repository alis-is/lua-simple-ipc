project(lua_simple_ipc)

file(GLOB lua_simple_ipc_sources ./src/**.c)
set(lua_simple_ipc ${lua_simple_ipc_sources})

add_library(lua_simple_ipc ${lua_simple_ipc})
target_link_libraries(lua_simple_ipc)