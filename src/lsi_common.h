#ifndef LSI_COMMON_H__
#define LSI_COMMON_H__

#include <stdlib.h>

#define PIPE_PREFIX "\\\\.\\pipe\\"

char* get_endpoint_path(const char* endpoint, size_t* endpoint_len);

#endif /* LSI_COMMON_H__ */