#ifndef LSI_COMMON_H__
#define LSI_COMMON_H__

#include <stdlib.h>

#define PIPE_PREFIX     "\\\\.\\pipe\\"
#define PIPE_PREFIX_LEN 9

#ifdef _WIN32
#define MAX_PATH_LEN 256
#else
#define MAX_PATH_LEN 108
#endif

char* get_endpoint_path(const char* endpoint, size_t* endpoint_len);

#endif /* LSI_COMMON_H__ */