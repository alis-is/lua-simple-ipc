
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lauxlib.h"
#include "lsi_common.h"
#include "lsi_core_socket.h"
#include "lsi_errors.h"
#include "lua.h"
#include "lerror.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#endif

int lsi_socket_connect(lua_State *L)
{
	size_t endpoint_len;
	const char *path = luaL_checklstring(L, 1, &endpoint_len);
	char *endpoint = get_endpoint_path(path, &endpoint_len);
	if (endpoint == NULL) {
		return push_error(L, ERROR_PATH_IS_NIL);
	}

	lsi_socket *sock =
		(lsi_socket *)lua_newuserdatauv(L, sizeof(lsi_socket), 0);
	if (sock == NULL) {
		return push_error(L, ERROR_FAILED_TO_CREATE_SOCKET_INSTANCE);
	}
	memset(sock, 0, sizeof(lsi_socket));
	luaL_getmetatable(L, LSI_SOCKET_METATABLE);
	lua_setmetatable(L, -2);

#ifdef _WIN32
	sock->hPipe = CreateFile(endpoint, GENERIC_READ | GENERIC_WRITE, 0,
				 NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED,
				 NULL);
	if (sock->hPipe == INVALID_HANDLE_VALUE) {
		sock->closed = 1;
		return push_error(L, ERROR_FAILED_TO_CONNECT);
	}
#else
	sock->fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sock->fd == -1) {
		sock->closed = 1;
		return push_error(L, ERROR_FAILED_TO_CREATE_SOCKET_INSTANCE);
	}

	struct sockaddr_un server_addr;
	memset(&server_addr, 0, sizeof(struct sockaddr_un));
	server_addr.sun_family = AF_UNIX;
	if (memcpy((void *)server_addr.sun_path, endpoint, endpoint_len + 1) ==
	    NULL) {
		return push_error(L, ERROR_FAILED_TO_CONNECT);
	}

	if (connect(sock->fd, (struct sockaddr *)&server_addr,
		    sizeof(server_addr)) == -1) {
		return push_error(L, ERROR_FAILED_TO_CONNECT);
	}
#endif
	return 1;
}

int lsi_socket_close(lua_State *L)
{
	lsi_socket *sock =
		(lsi_socket *)luaL_checkudata(L, 1, LSI_SOCKET_METATABLE);
	if (sock == NULL) {
		return 0;
	}
	if (sock->closed) {
		return 0;
	}
#ifdef _WIN32
	if (sock->hPipe != INVALID_HANDLE_VALUE) {
		CloseHandle(sock->hPipe);
		sock->hPipe = INVALID_HANDLE_VALUE;
	}
#else
	if (sock->fd != -1) {
		close(sock->fd);
		sock->fd = -1;
	}
#endif
	sock->closed = 1;
	return 0;
}

int lsi_socket_write(lua_State *L)
{
	lsi_socket *sock =
		(lsi_socket *)luaL_checkudata(L, 1, LSI_SOCKET_METATABLE);
	if (sock == NULL) {
		return push_error(L, ERROR_SOCKET_IS_NIL);
	}
	if (sock->closed) {
		return push_error(L, ERROR_SOCKET_CLOSED);
	}
	size_t datasize;
	const char *data = luaL_checklstring(L, 2, &datasize);
#ifdef _WIN32
	DWORD bytes_written;
	if (WriteFile(sock->hPipe, data, datasize, &bytes_written, NULL) == 0) {
		return push_error(L, ERROR_WRITE_FAILED);
	}
#else
	if (write(sock->fd, data, datasize) == -1) {
		return push_error(L, ERROR_WRITE_FAILED);
	}
#endif
	lua_pushboolean(L, 1);
	return 1;
}

int lsi_socket_read(lua_State *L)
{
	lsi_socket *sock =
		(lsi_socket *)luaL_checkudata(L, 1, LSI_SOCKET_METATABLE);
	if (sock == NULL) {
		return push_error(L, ERROR_SOCKET_IS_NIL);
	}
	if (sock->closed) {
		return push_error(L, ERROR_SOCKET_CLOSED);
	}
	// read options table - may buffer size and timeout
	int timeout = -1;
	int buffer_size = DEFAULT_BUFFER_SIZE;
	if (lua_istable(L, 2)) {
		lua_getfield(L, 2, "buffer_size");
		buffer_size = luaL_optinteger(L, -1, DEFAULT_BUFFER_SIZE);
		lua_pop(L, 1);

		lua_getfield(L, 2, "timeout");
		timeout = luaL_optinteger(L, -1, -1);
		lua_pop(L, 1);
	}

#ifdef _WIN32
	DWORD bytes_read;
	char *buffer = (char *)malloc(buffer_size);
	if (timeout >= 0) {
		OVERLAPPED overlapped;
		memset(&overlapped, 0, sizeof(overlapped));
		overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		if (overlapped.hEvent == NULL) {
			return push_error(L, ERROR_READ_FAILED);
		}
		if (ReadFile(sock->hPipe, buffer, buffer_size, &bytes_read,
			     &overlapped) == 0) {
			if (GetLastError() != ERROR_IO_PENDING) {
				CloseHandle(overlapped.hEvent);
				free((void *)buffer);
				return push_error(L, ERROR_READ_FAILED);
			}
			DWORD wait_res =
				WaitForSingleObject(overlapped.hEvent, timeout);
			if (wait_res == WAIT_FAILED) {
				CloseHandle(overlapped.hEvent);
				free((void *)buffer);
				return push_error(L, ERROR_POLL_FAILED);
			}
			if (wait_res == WAIT_TIMEOUT) {
				CloseHandle(overlapped.hEvent);
				free((void *)buffer);
				lua_pushnil(L);
				lua_pushstring(L, "timeout");
				return 2;
			}
			if (GetOverlappedResult(sock->hPipe, &overlapped,
						&bytes_read, FALSE) == 0) {
				CloseHandle(overlapped.hEvent);
				free((void *)buffer);
				return push_error(L, ERROR_READ_FAILED);
			}
		}
		CloseHandle(overlapped.hEvent);
	} else {
		if (ReadFile(sock->hPipe, buffer, buffer_size, &bytes_read,
			     NULL) == 0) {
			free((void *)buffer);
			return push_error(L, ERROR_READ_FAILED);
		}
	}

	lua_pushlstring(L, buffer, bytes_read);
#else
	if (timeout >= 0) {
		struct pollfd fds[1];
		fds[0].fd = sock->fd;
		fds[0].events = POLLIN;
		int poll_res = poll(fds, 1, timeout);
		if (poll_res == -1) {
			return push_error(L, ERROR_POLL_FAILED);
		}
		if (poll_res == 0) {
			lua_pushnil(L);
			lua_pushstring(L, ERROR_TIMEOUT);
			return 2;
		}
	}
	char *buffer = (char *)malloc(buffer_size);
	if (buffer == NULL) {
		return push_error(L, ERROR_READ_FAILED);
	}

	int read_size = read(sock->fd, buffer, buffer_size);
	if (read_size == -1) {
		free((void *)buffer);
		return push_error(L, ERROR_READ_FAILED);
	}
	lua_pushlstring(L, buffer, read_size);
#endif

	free((void *)buffer);
	return 1;
}

int lsi_socket_is_nonblocking(lua_State *L)
{
	lsi_socket *sock =
		(lsi_socket *)luaL_checkudata(L, 1, LSI_SOCKET_METATABLE);
	if (sock == NULL) {
		return push_error(L, ERROR_SOCKET_IS_NIL);
	}
	if (sock->closed) {
		return push_error(L, ERROR_SOCKET_CLOSED);
	}
#ifdef _WIN32
	DWORD flags;
	if (GetNamedPipeHandleState(sock->hPipe, &flags, NULL, NULL, NULL, NULL,
				    0) == 0) {
		return push_error(L, ERROR_STATE_CHECK_FAILED);
	}
	lua_pushboolean(L, flags & PIPE_NOWAIT);
#else
	int flags = fcntl(sock->fd, F_GETFL, 0);
	if (flags == -1) {
		return push_error(L, ERROR_STATE_CHECK_FAILED);
	}
	lua_pushboolean(L, flags & O_NONBLOCK);
#endif
	return 1;
}

int lsi_socket_set_nonblocking(lua_State *L)
{
	lsi_socket *sock =
		(lsi_socket *)luaL_checkudata(L, 1, LSI_SOCKET_METATABLE);
	if (sock == NULL) {
		return push_error(L, ERROR_SOCKET_IS_NIL);
	}
	if (sock->closed) {
		return push_error(L, ERROR_SOCKET_CLOSED);
	}
	if (sock->server_owned) {
		return push_error(L, ERROR_SERVER_OWNED_SOCKET);
	}

	int nonblocking = lua_isboolean(L, 2) ? lua_toboolean(L, 2) : 1;
#ifdef _WIN32
	DWORD flags;
	if (GetNamedPipeHandleState(sock->hPipe, &flags, NULL, NULL, NULL, NULL,
				    0) == 0) {
		return push_error(L, ERROR_STATE_CHECK_FAILED);
	}
	if (nonblocking) {
		flags |= PIPE_NOWAIT;
	} else {
		flags &= ~PIPE_NOWAIT;
	}
	if (SetNamedPipeHandleState(sock->hPipe, &flags, NULL, NULL) == 0) {
		return push_error(L, ERROR_SET_STATE_FAILED);
	}
	lua_pushboolean(L, 1);
#else
	int flags = fcntl(sock->fd, F_GETFL, 0);
	if (flags == -1) {
		return push_error(L, ERROR_STATE_CHECK_FAILED);
	}
	if (fcntl(sock->fd, F_SETFL, flags | O_NONBLOCK) == -1) {
		return push_error(L, ERROR_SET_STATE_FAILED);
	}
	lua_pushboolean(L, 1);
#endif
	return 1;
}

#ifndef _WIN32
void push_address(lua_State *L, struct sockaddr *addr)
{
	switch (addr->sa_family) {
	case AF_INET: {
		struct sockaddr_in *s = (struct sockaddr_in *)&addr;
		lua_pushstring(L, inet_ntoa(s->sin_addr));
		lua_pushinteger(L, ntohs(s->sin_port));
		break;
	}
	case AF_INET6: {
		struct sockaddr_in6 *s = (struct sockaddr_in6 *)&addr;
		char ipstr[INET6_ADDRSTRLEN];
		inet_ntop(AF_INET6, &s->sin6_addr, ipstr, sizeof ipstr);
		lua_pushstring(L, ipstr);
		lua_pushinteger(L, ntohs(s->sin6_port));
		break;
	}
	case AF_UNIX: {
		struct sockaddr_un *s = (struct sockaddr_un *)&addr;
		lua_pushstring(L, s->sun_path);
		lua_pushinteger(L, 0);
		break;
	}
	default:
		lua_pushnil(L);
		lua_pushnil(L);
	}
}
#endif

int lsi_socket_get_peer_name(lua_State *L)
{
	lsi_socket *sock =
		(lsi_socket *)luaL_checkudata(L, 1, LSI_SOCKET_METATABLE);
	if (sock == NULL) {
		return push_error(L, ERROR_SOCKET_IS_NIL);
	}
	if (sock->closed) {
		return push_error(L, ERROR_SOCKET_CLOSED);
	}

#ifdef _WIN32
	lua_pushstring(L, "pipe");
#else
	struct sockaddr addr;
	socklen_t addr_len = sizeof(addr);
	if (getpeername(sock->fd, &addr, &addr_len) == -1) {
		return push_error(L, ERROR_STATE_CHECK_FAILED);
	}
	push_address(L, &addr);
#endif
	return 2;
}

int lsi_socket_equals(lua_State *L)
{
	lsi_socket *sock1 =
		(lsi_socket *)luaL_checkudata(L, 1, LSI_SOCKET_METATABLE);
	lsi_socket *sock2 =
		(lsi_socket *)luaL_checkudata(L, 2, LSI_SOCKET_METATABLE);
	if (sock1 == NULL || sock2 == NULL) {
		lua_pushboolean(L, 0);
		return 1;
	}
#ifdef _WIN32
	lua_pushboolean(L, sock1->hPipe == sock2->hPipe);
#else
	lua_pushboolean(L, sock1->fd == sock2->fd);
#endif
	return 1;
}

int lsi_socket_tostring(lua_State *L)
{
	lsi_socket *sock =
		(lsi_socket *)luaL_checkudata(L, 1, LSI_SOCKET_METATABLE);
	if (sock == NULL) {
		return push_error(L, ERROR_SOCKET_IS_NIL);
	}
#ifdef _WIN32
	lua_pushfstring(L, "socket(%p)", sock->hPipe);
#else
	lua_pushfstring(L, "socket(%d)", sock->fd);
#endif
	return 1;
}

int lsi_create_socket_meta(lua_State *L)
{
	luaL_newmetatable(L, LSI_SOCKET_METATABLE);

	lua_newtable(L);
	lua_pushcfunction(L, lsi_socket_close);
	lua_setfield(L, -2, "close");
	lua_pushcfunction(L, lsi_socket_write);
	lua_setfield(L, -2, "write");
	lua_pushcfunction(L, lsi_socket_read);
	lua_setfield(L, -2, "read");
	lua_pushcfunction(L, lsi_socket_is_nonblocking);
	lua_setfield(L, -2, "is_nonblocking");
	lua_pushcfunction(L, lsi_socket_set_nonblocking);
	lua_setfield(L, -2, "set_nonblocking");
	lua_pushcfunction(L, lsi_socket_get_peer_name);
	lua_setfield(L, -2, "get_peer_name");
	lua_pushcfunction(L, lsi_socket_tostring);
	lua_setfield(L, -2, "__tostring");
	lua_pushcfunction(L, lsi_socket_equals);
	lua_setfield(L, -2, "__eq");
	lua_pushstring(L, LSI_SOCKET_METATABLE);
	lua_setfield(L, -2, "__type");

	lua_setfield(L, -2, "__index");

	lua_pushcfunction(L, lsi_socket_close);
	lua_setfield(L, -2, "__gc");
	lua_pushcfunction(L, lsi_socket_close);
	lua_setfield(L, -2, "__close");

	lua_pop(L, 1);
	return 0;
}