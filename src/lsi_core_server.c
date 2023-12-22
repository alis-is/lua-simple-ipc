#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lauxlib.h"
#include "lsi_common.h"
#include "lsi_core_server.h"
#include "lsi_core_socket.h"
#include "lsi_errors.h"
#include "lua.h"
#include "lutil.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

static void
#ifdef _WIN32
push_client_from_server(lua_State* L, int serverindex, HANDLE id) {
#else
push_client_from_server(lua_State* L, int serverindex, int id) {
#endif
    lua_getiuservalue(L, serverindex, 1);
    lua_pushinteger(L, (int)id);
    lua_gettable(L, -2);
    lua_remove(L, -2);
}

static void
#ifdef _WIN32
remove_client_from_server(lua_State* L, int serverindex, HANDLE id) {
#else
remove_client_from_server(lua_State* L, int serverindex, int id) {
#endif
    lua_getiuservalue(L, serverindex, 1);
    lua_pushinteger(L, (int)id);
    lua_pushnil(L);
    lua_settable(L, -3);
    lua_pop(L, 1);
}

static void
push_cb_error_info(lua_State* L, const char* id, const char* info) {
    lua_pushstring(L, id);
    if (info == NULL) {
        lua_pushstring(L, strerror(errno));
    } else {
        lua_pushfstring(L, "%s: %s", info, strerror(errno));
    }
    lua_pushinteger(L, errno);
}

static void
#ifdef _WIN32
call_error_cb(lua_State* L, const char* id, int serverindex, int optionindex, HANDLE clientid, char* err) {
#else
call_error_cb(lua_State* L, const char* id, int serverindex, int optionindex, int clientid, char* err) {
#endif
    if (lua_type(L, optionindex) != LUA_TTABLE) {
        return;
    }
    if (lua_getfield(L, optionindex, "error") == LUA_TFUNCTION) {
        // call error handler with the error pushed by lua_pcall
        push_cb_error_info(L, id, err);
        push_client_from_server(L, serverindex, clientid);
        lua_pcall(L, 4, 0, 0);
    } else {
        lua_pop(L, 1); // discard nil
    }
}

static void
#ifdef _WIN32
call_cb(lua_State* L, const char* id, int serverindex, int optionindex, HANDLE clientid) {
#else
call_cb(lua_State* L, const char* id, int serverindex, int optionindex, int clientid) {
#endif
    if (lua_type(L, optionindex) != LUA_TTABLE) {
        return;
    }
    if (lua_getfield(L, 2, id) == LUA_TFUNCTION) {
        push_client_from_server(L, serverindex, clientid);
        if ((lua_pcall(L, 1, 0, 0) != LUA_OK)) {
            call_error_cb(L, id, serverindex, optionindex, clientid, ERROR_CALLBACK_FAILED);
        }
    } else {
        lua_pop(L, 1); // discard nil
    }
}

#ifdef _WIN32
static HANDLE
CreateNewPipeInstance(const lsi_server* server, PIPE_INSTANCE* pipeInst) {
    pipeInst->hPipe = CreateNamedPipe(server->path, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                      PIPE_TYPE_MESSAGE | PIPE_READMODE_BYTE | PIPE_WAIT, server->max_clients,
                                      server->buffer_size, server->buffer_size, PIPE_TIMEOUT, NULL);

    if (pipeInst->hPipe == INVALID_HANDLE_VALUE) {
        return INVALID_HANDLE_VALUE;
    }

    // Start an overlapped connection for this pipe instance
    BOOL fConnected =
        ConnectNamedPipe(pipeInst->hPipe, &pipeInst->connectOverlap) ? TRUE : (GetLastError() == ERROR_IO_PENDING);

    if (!fConnected && GetLastError() != ERROR_IO_PENDING && GetLastError() != ERROR_PIPE_CONNECTED) {
        CloseHandle(pipeInst->hPipe);
        return INVALID_HANDLE_VALUE;
    }

    return pipeInst->hPipe;
}

static HANDLE
DisconnectAndReconnect(PIPE_INSTANCE* pipeInst) {
    ResetEvent(pipeInst->connectOverlap.hEvent);
    free(pipeInst->buffer);
    // Disconnect the current pipe instance
    if (!DisconnectNamedPipe(pipeInst->hPipe)) {
        return INVALID_HANDLE_VALUE;
    }

    // Reconnect the pipe for a new client
    BOOL fConnected =
        ConnectNamedPipe(pipeInst->hPipe, &pipeInst->connectOverlap) ? FALSE : (GetLastError() == ERROR_IO_PENDING);
    if (!fConnected && GetLastError() != ERROR_IO_PENDING && GetLastError() != ERROR_PIPE_CONNECTED) {
        CloseHandle(pipeInst->hPipe);
        return INVALID_HANDLE_VALUE;
    }
    return pipeInst->hPipe;
}
#endif

int
lsi_server_process_events(lua_State* L) {
    lsi_server* server = (lsi_server*)lua_touserdata(L, 1);
    if (server == NULL) {
        return push_error(L, ERROR_SERVER_IS_NIL);
    }
    if (server->closed) {
        return push_error(L, ERROR_SERVER_CLOSED);
    }
    int hasOptions = lua_type(L, 2) == LUA_TTABLE;

    int timeout = 0;
    if (hasOptions) {
        lua_getfield(L, 2, "timeout");
        timeout = luaL_optinteger(L, -1, 0);
        lua_pop(L, 1);
    }
#ifdef _WIN32
    DWORD wait_res = WaitForMultipleObjects(server->max_clients, server->hEvents, FALSE, timeout);
    if (wait_res == WAIT_FAILED) {
        return push_error(L, ERROR_POLL_FAILED);
    }
    // connections
    for (int i = 0; i < server->max_clients; i++) {
        PIPE_INSTANCE* pipe = &server->instances[i];
        if (WaitForSingleObject(pipe->connectOverlap.hEvent, 0) == WAIT_OBJECT_0) {
            ResetEvent(pipe->connectOverlap.hEvent);
            lsi_socket* client = (lsi_socket*)lua_newuserdatauv(L, sizeof(lsi_socket), 0);
            if (client == NULL) {
                return push_error(L, "malloc failed");
            }
            memset(client, 0, sizeof(lsi_socket));
            luaL_getmetatable(L, LSI_SOCKET_METATABLE);
            lua_setmetatable(L, -2);
            client->server_owned = 1;
            client->hPipe = pipe->hPipe;
            int shouldAccept = 1;

            if (hasOptions) {
                if (lua_getfield(L, 2, "accept") == LUA_TFUNCTION) {
                    // push client userdata
                    lua_pushvalue(L, -2);
                    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
                        // call error handler
                        call_error_cb(L, "accept", 1, 2, pipe->hPipe, ERROR_CALLBACK_FAILED);
                        shouldAccept = 0;
                    } else if (lua_isboolean(L, -1) && !lua_toboolean(L, -1)) {
                        // if call returns false, close the connection
                        shouldAccept = 0;
                    }
                    lua_pop(L, 1); // discard return value
                } else {
                    lua_pop(L, 1); // discard nil
                }
            }

            if (shouldAccept) {
                pipe->buffer = (CHAR*)malloc(server->buffer_size * sizeof(CHAR));
                ReadFile(pipe->hPipe, pipe->buffer, server->buffer_size, &pipe->bytesRead, &pipe->dataOverlap);
                lua_getiuservalue(L, 1, 1);
                lua_pushinteger(L, (int)client->hPipe);
                lua_pushvalue(L, -3); // push client userdata
                lua_settable(L, -3);
                lua_pop(L, 1); // discard uv table
            } else {
                DisconnectAndReconnect(pipe);
                client->closed = 1;
            }
            lua_pop(L, 1); // discard client userdata
        }
    }
    // data
    for (int i = 0; i < server->max_clients; i++) {
        PIPE_INSTANCE* pipe = &server->instances[i];
        if (WaitForSingleObject(pipe->dataOverlap.hEvent, 0) == WAIT_OBJECT_0) {
            ResetEvent(pipe->dataOverlap.hEvent);

            BOOL fSuccess = GetOverlappedResult(pipe->hPipe, &pipe->dataOverlap, &pipe->bytesRead, FALSE);
            if (!fSuccess) {
                if (GetLastError() == ERROR_BROKEN_PIPE) {
                    // Client disconnected
                    call_cb(L, "disconnect", 1, 2, pipe->hPipe);
                    remove_client_from_server(L, 1, pipe->hPipe);
                    if (DisconnectAndReconnect(pipe) == INVALID_HANDLE_VALUE) {
                        call_error_cb(L, "internal", 1, 2, pipe->hPipe, ERROR_FAILED_TO_RECREATE_PIPE);
                    }
                } else {
                    call_error_cb(L, "read", 1, 2, pipe->hPipe, ERROR_READ_FAILED);
                    remove_client_from_server(L, 1, pipe->hPipe);
                    if (DisconnectAndReconnect(pipe) == INVALID_HANDLE_VALUE) {
                        call_error_cb(L, "internal", 1, 2, pipe->hPipe, ERROR_FAILED_TO_RECREATE_PIPE);
                    }
                }
            } else {
                if (pipe->bytesRead > 0) {
                    call_cb(L, "data", 1, 2, pipe->hPipe);
                    if (hasOptions) {
                        if (lua_getfield(L, 2, "data") == LUA_TFUNCTION) {
                            push_client_from_server(L, 1, pipe->hPipe);
                            lua_pushlstring(L, pipe->buffer, pipe->bytesRead);
                            if ((lua_pcall(L, 2, 0, 0) != LUA_OK)) {
                                call_error_cb(L, "data", 1, 2, pipe->hPipe, ERROR_CALLBACK_FAILED);
                            }
                        } else {
                            lua_pop(L, 1); // discard nil
                        }
                    }
                } else { // no data read, client may have disconnected
                    remove_client_from_server(L, 1, pipe->hPipe);
                    if (DisconnectAndReconnect(pipe) == INVALID_HANDLE_VALUE) {
                        call_error_cb(L, "internal", 1, 2, pipe->hPipe, ERROR_FAILED_TO_RECREATE_PIPE);
                    }
                }
            }
        }
    }
#else
    int ret = poll(server->fds, server->nfds, timeout);
    if (ret == -1) {
        return push_error(L, ERROR_POLL_FAILED);
    }

    // Check for new connection
    if (server->fds[0].revents & POLLIN) {
        // accept in while loop
        while (1) {
            lsi_socket* client = (lsi_socket*)lua_newuserdatauv(L, sizeof(lsi_socket), 0);
            if (client == NULL) {
                return push_error(L, "malloc failed");
            }
            memset(client, 0, sizeof(lsi_socket));
            luaL_getmetatable(L, LSI_SOCKET_METATABLE);
            lua_setmetatable(L, -2);
            client->fd = accept(server->fd, NULL, NULL);
            client->server_owned = 1;
            if (client->fd == -1) {
                client->closed = 1;
                lua_pop(L, 1); // discard client userdata
                break;
            }

            // Add new client to the poll fds
            if (server->client_count < server->max_clients) {
                int shouldAccept = 1;

                if (hasOptions) {
                    if (lua_getfield(L, 2, "accept") == LUA_TFUNCTION) {
                        // push client userdata
                        lua_pushvalue(L, -2);
                        if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
                            // call error handler
                            if (hasOptions) {
                                if (lua_getfield(L, 2, "error") == LUA_TFUNCTION) {
                                    // call error handler with the error pushed by lua_pcall
                                    push_cb_error_info(L, "accept", ERROR_CALLBACK_FAILED);
                                    lua_pushvalue(L, -4); // push client userdata
                                    lua_pcall(L, 4, 0, 0);
                                } else {
                                    lua_pop(L, 1); // discard nil
                                }
                            }
                            shouldAccept = 0;
                        } else if (lua_isboolean(L, -1) && !lua_toboolean(L, -1)) {
                            // if call returns false, close the connection
                            shouldAccept = 0;
                        }
                        lua_pop(L, 1); // discard return value
                    } else {
                        lua_pop(L, 1); // discard nil
                    }
                }
                if (shouldAccept) {
                    server->fds[server->nfds].fd = client->fd;
                    server->fds[server->nfds].events = POLLIN;
                    server->nfds++;
                    server->client_count++;
                    // add client to the uv table
                    lua_getiuservalue(L, 1, 1);
                    lua_pushinteger(L, client->fd);
                    lua_pushvalue(L, -3); // push client userdata
                    lua_settable(L, -3);
                    lua_pop(L, 1); // discard uv table
                } else {
                    close(client->fd);
                    client->closed = 1;
                }
                lua_pop(L, 1); // discard client userdata
            } else {
                if (hasOptions) {
                    if (lua_getfield(L, 2, "error") == LUA_TFUNCTION) {
                        lua_pushstring(L, "accept");
                        lua_pushstring(L, ERROR_CLIENT_LIMIT_REACHED);
                        lua_pcall(L, 2, 0, 0);
                    } else {
                        lua_pop(L, 1); // discard nil
                    }
                }
                close(client->fd);
                client->closed = 1;
                lua_pop(L, 1); // discard client userdata
                break;
            }
        }
    }
    // Check each client for data
    char* buffer = malloc(server->buffer_size * sizeof(char));
    for (int i = 1; i < server->nfds; i++) {
        if (server->fds[i].revents & POLLIN) {
            while (1) {
                ssize_t count = read(server->fds[i].fd, buffer, server->buffer_size);
                if (count == -1) {
                    if (hasOptions) {
                        if (lua_getfield(L, 2, "error") == LUA_TFUNCTION) {
                            push_cb_error_info(L, "read", "read failed");
                            push_client_from_server(L, 1, server->fds[i].fd);
                            lua_pcall(L, 4, 0, 0);
                        } else {
                            lua_pop(L, 1); // discard nil
                        }
                    }

                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        remove_client_from_server(L, 1, server->fds[i].fd);
                        server->fds[i].fd = -1;
                        server->client_count--;
                    }

                    break;
                } else if (count == 0) {
                    // Client disconnected
                    if (hasOptions) {
                        if (lua_getfield(L, 2, "disconnect") == LUA_TFUNCTION) {
                            lua_pushstring(L, "disconnect");
                            push_client_from_server(L, 1, server->fds[i].fd);
                            lua_pcall(L, 2, 0, 0);
                        } else {
                            lua_pop(L, 1); // discard nil
                        }
                    }
                    remove_client_from_server(L, 1, server->fds[i].fd);
                    server->fds[i].fd = -1;
                    server->client_count--;
                    break;
                } else {
                    // Process data
                    if (hasOptions) {
                        if (lua_getfield(L, 2, "data") == LUA_TFUNCTION) {
                            push_client_from_server(L, 1, server->fds[i].fd);
                            lua_pushlstring(L, buffer, count);
                            lua_pcall(L, 2, 0, 0);
                        } else {
                            lua_pop(L, 1); // discard nil
                        }
                    }
                }
            }
        }
    }
    free(buffer);
    // Compress the fds array
    for (int i = 0; i < server->nfds; i++) {
        if (server->fds[i].fd == -1) {
            for (int j = i; j < server->nfds - 1; j++) {
                server->fds[j] = server->fds[j + 1];
            }
            i--;
            server->nfds--;
        }
    }
#endif
    lua_pushboolean(L, 1);
    return 1;
}

lsi_server*
create_server_instance(lua_State* L) {
    size_t path_len;
    const char* path = (char*)luaL_checklstring(L, 1, &path_len);
    if (path == NULL) {
        return NULL;
    }
    lsi_server* server = (lsi_server*)lua_newuserdatauv(L, sizeof(lsi_server), 1);
    if (server == NULL) {
        return NULL;
    }
    // common
    memset(server, 0, sizeof(lsi_server));
    server->closed = 1;
    server->buffer_size = DEFAULT_BUFFER_SIZE;
    server->max_clients = DEFAULT_MAX_CLIENTS;
    luaL_getmetatable(L, LSI_SERVER_METATABLE);
    lua_setmetatable(L, -2);

    // path
    server->path = get_endpoint_path(path, &path_len);
    server->path_len = path_len;
    if (server->path == NULL) {
        return NULL;
    }

    // options
    if (lua_type(L, 2) == LUA_TTABLE) { // options table
        // get max clients
        lua_getfield(L, 2, "max_clients");
        server->max_clients = luaL_optinteger(L, -1, DEFAULT_MAX_CLIENTS);
        lua_pop(L, 1);

        // get max msg size
        lua_getfield(L, 2, "buffer_size");
        server->buffer_size = luaL_optinteger(L, -1, DEFAULT_BUFFER_SIZE);
        if (server->buffer_size < 1) {
            server->buffer_size = DEFAULT_BUFFER_SIZE;
        }
        lua_pop(L, 1);
    }
    // clients tables
    lua_newtable(L);
    lua_setiuservalue(L, -2, 1);

    return server;
}

#ifdef _WIN32
void
close_server_handles(lsi_server* server) {
    for (int j = 0; j < server->max_clients; j++) {
        if (server->hEvents[j * 2] != NULL) {
            CloseHandle(server->hEvents[j * 2]);
        }
        if (server->hEvents[j * 2 + 1] != NULL) {
            CloseHandle(server->hEvents[j * 2 + 1]);
        }
        if (server->instances[j].hPipe != NULL) {
            CloseHandle(server->instances[j].hPipe);
        }
    }
}
#endif

int
lsi_listen(lua_State* L) {
    lsi_server* server = create_server_instance(L);
    if (server == NULL) {
        return push_error(L, ERROR_FAILED_TO_CREATE_SERVER_INSTANCE);
    }

#ifdef _WIN32
    server->hEvents = (HANDLE*)malloc(server->max_clients * sizeof(HANDLE) * 2);
    if (server->hEvents == NULL) {
        return push_error(L, ERROR_FAILED_TO_CREATE_SERVER_INSTANCE);
    }
    memset(server->hEvents, 0, server->max_clients * sizeof(HANDLE) * 2);
    server->instances = (PIPE_INSTANCE*)malloc(server->max_clients * sizeof(PIPE_INSTANCE));
    if (server->instances == NULL) {
        return push_error(L, ERROR_FAILED_TO_CREATE_SERVER_INSTANCE);
    }
    memset(server->instances, 0, server->max_clients * sizeof(PIPE_INSTANCE));

    for (int i = 0; i < server->max_clients; ++i) {
        server->hEvents[i * 2] = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (server->hEvents[i * 2] == NULL) {
            close_server_handles(server);
            return push_error(L, ERROR_FAILED_TO_CREATE_SERVER_INSTANCE);
        }
        server->instances[i].connectOverlap.hEvent = server->hEvents[i * 2];
        server->hEvents[i * 2 + 1] = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (server->hEvents[i * 2 + 1] == NULL) {
            close_server_handles(server);
            return push_error(L, ERROR_FAILED_TO_CREATE_SERVER_INSTANCE);
        }
        server->instances[i].dataOverlap.hEvent = server->hEvents[i * 2 + 1];

        if (CreateNewPipeInstance(server, &server->instances[i]) == INVALID_HANDLE_VALUE) {
            close_server_handles(server);
            return push_error(L, ERROR_FAILED_TO_CREATE_SERVER_INSTANCE);
        }
    }
    server->closed = 0;
#else
    server->fds = malloc(sizeof(struct pollfd) * (server->max_clients + 1));
    if (server->fds == NULL) {
        return push_error(L, ERROR_FAILED_TO_CREATE_SERVER_INSTANCE);
    }
    for (size_t i = 0; i < server->max_clients + 1; i++) {
        server->fds[i].fd = -1;
        server->fds[i].events = POLLIN;
    }

    server->fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server->fd == -1) {
        return push_error(L, ERROR_FAILED_TO_CREATE_SERVER_INSTANCE);
    }
    server->closed = 0;
    // set non blocking
    int flags = fcntl(server->fd, F_GETFL, 0);
    if (flags == -1) {
        return push_error(L, ERROR_STATE_CHECK_FAILED);
    }
    if (fcntl(server->fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        return push_error(L, ERROR_STATE_CHECK_FAILED);
    }
    server->fds[0].fd = server->fd;
    server->nfds = 1;

    struct sockaddr_un server_addr;
    memset(&server_addr, 0, sizeof(struct sockaddr_un));
    server_addr.sun_family = AF_UNIX;
    if (memcpy((void*)server_addr.sun_path, server->path, server->path_len) == NULL) {
        return push_error(L, ERROR_FAILED_TO_CREATE_SERVER_INSTANCE);
    }

    if (unlink(server->path) == -1 && errno != ENOENT) {
        return push_error(L, ERROR_FAILED_TO_CREATE_SERVER_INSTANCE);
    }
    if (bind(server->fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        return push_error(L, ERROR_FAILED_TO_CREATE_SERVER_INSTANCE);
    }
    if (listen(server->fd, server->max_clients) == -1) {
        return push_error(L, ERROR_FAILED_TO_CREATE_SERVER_INSTANCE);
    }
#endif
    return 1;
}

int
lsi_server_clients(lua_State* L) {
    lsi_server* server = (lsi_server*)luaL_checkudata(L, 1, LSI_SERVER_METATABLE);
    if (server == NULL) {
        return push_error(L, ERROR_SERVER_IS_NIL);
    }
    lua_getiuservalue(L, 1, 1);
    // clone table to avoid modifying the original
    lua_newtable(L);               // t1 t2
    lua_pushnil(L);                // t1 t2 nil
    while (lua_next(L, -3) != 0) { // t1 t2 key value
        lua_pushvalue(L, -2);      // t1 t2 key value key
        lua_rotate(L, -2, 1);      // t1 t2 key key value
        lua_settable(L, -4);       // t1 t2 key
    }

    return 1;
}

int
lst_server_close(lua_State* L) {
    lsi_server* server = (lsi_server*)lua_touserdata(L, 1);
    if (server == NULL) {
        return 0;
    }
    if (server->closed) {
        return 0;
    }
    server->closed = 1;

#ifdef _WIN32
    for (int i = 0; i < server->max_clients; i++) {
        CloseHandle(server->hEvents[i * 2]);
        CloseHandle(server->hEvents[i * 2 + 1]);
        // we do not close the pipe handles here, because they are owned by the client userdata
    }
    free(server->hEvents);
    free(server->instances);
#else
    server->client_count = 0;
    server->nfds = 0;
    if (server->fds != NULL) {
        free(server->fds);
    }
    if (server->path != NULL) {
        unlink(server->path);
        free((void*)server->path);
    }
    if (server->fd != -1) {
        close(server->fd);
        server->fd = -1;
    }
#endif
    return 0;
}

int
lsi_server_tostring(lua_State* L) {
    lsi_server* server = (lsi_server*)luaL_checkudata(L, 1, LSI_SERVER_METATABLE);
    if (server == NULL) {
        return push_error(L, ERROR_SERVER_IS_NIL);
    }
#ifdef _WIN32
    lua_pushfstring(L, "server: %s", server->path);
#else
    lua_pushfstring(L, "server: %d (%s)", server->fd, server->path);
#endif
    return 1;
}

int
lsi_server_equals(lua_State* L) {
    lsi_server* server1 = (lsi_server*)luaL_checkudata(L, 1, LSI_SERVER_METATABLE);
    lsi_server* server2 = (lsi_server*)luaL_checkudata(L, 2, LSI_SERVER_METATABLE);
    if (server1 == NULL || server2 == NULL) {
        lua_pushboolean(L, 0);
        return 1;
    }
#ifdef _WIN32
    lua_pushboolean(L, server1->path == server2->path);
#else
    lua_pushboolean(L, server1->fd == server2->fd);
#endif
    return 1;
}

int
lsi_create_server_meta(lua_State* L) {
    luaL_newmetatable(L, LSI_SERVER_METATABLE);

    lua_newtable(L);

    lua_pushcfunction(L, lst_server_close);
    lua_setfield(L, -2, "close");
    lua_pushcfunction(L, lsi_server_process_events);
    lua_setfield(L, -2, "process_events");
    lua_pushcfunction(L, lsi_server_clients);
    lua_setfield(L, -2, "get_clients");
    lua_pushcfunction(L, lsi_server_tostring);
    lua_setfield(L, -2, "__tostring");
    lua_pushstring(L, LSI_SERVER_METATABLE);
    lua_setfield(L, -2, "__type");
    lua_pushcfunction(L, lsi_server_equals);
    lua_setfield(L, -2, "__eq");

    lua_setfield(L, -2, "__index");

    lua_pushcfunction(L, lst_server_close);
    lua_setfield(L, -2, "__gc");
    lua_pushcfunction(L, lst_server_close);
    lua_setfield(L, -2, "__close");

    lua_pop(L, 1);
    return 0;
}