#ifndef LSI_ERRORS_H__
#define LSI_ERRORS_H__

#define ERROR_FAILED_TO_CREATE_SERVER_INSTANCE "failed to create server instance"
#define ERROR_FAILED_TO_CREATE_SOCKET_INSTANCE "failed to create socket instance"
#define ERROR_FAILED_TO_CONNECT                "failed to connect"
#define ERROR_SERVER_IS_NIL                    "server is nil"
#define ERROR_SERVER_CLOSED                    "server is closed"
#define ERROR_SOCKET_IS_NIL                    "socket is nil"
#define ERROR_SOCKET_CLOSED                    "socket is closed"
#define ERROR_PATH_IS_NIL                      "path is nil"
#define ERROR_TIMEOUT                          "timeout"
#define ERROR_POLL_FAILED                      "poll failed"
#define ERROR_WRITE_FAILED                     "write failed"
#define ERROR_READ_FAILED                      "read failed"
#define ERROR_STATE_CHECK_FAILED               "state check failed"
#define ERROR_SET_STATE_FAILED                 "set state failed"
#define ERROR_SERVER_OWNED_SOCKET              "server owned socket"
#define ERROR_CLIENT_LIMIT_REACHED             "client limit reached"
#define ERROR_CALLBACK_FAILED                  "accept callback failed"
#define ERROR_FAILED_TO_RECREATE_PIPE          "failed to create pipe"

#endif /* LSI_ERRORS_H__ */
