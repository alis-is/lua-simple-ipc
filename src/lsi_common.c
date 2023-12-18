#include "lsi_common.h"
#include <stdlib.h>
#include <string.h>

char*
get_endpoint_path(const char* endpoint, size_t* endpoint_len) {
    if (endpoint == NULL) {
        return NULL;
    }
#ifdef _WIN32
    int needsPrefix = strncmp(endpoint, PIPE_PREFIX, strlen(PIPE_PREFIX)) != 0;

    size_t pipe_prefix_len = needsPrefix ? strlen(PIPE_PREFIX) : 0;
    size_t result_len = pipe_prefix_len + *endpoint_len + 1;
#else
    size_t pipe_prefix_len = 0;
    size_t result_len = *endpoint_len + 1;
#endif
    *endpoint_len = result_len - 1;

    char* result = malloc(result_len);
    if (result == NULL) {
        return NULL;
    }
#ifdef _WIN32
    if (needsPrefix) {
        if (memcpy((void*)result, PIPE_PREFIX, pipe_prefix_len) == NULL) {
            free((void*)result);
            return NULL;
        }
    }
#endif
    if (memcpy((void*)(result + pipe_prefix_len), endpoint, *endpoint_len + 1) == NULL) {
        free((void*)result);
        return NULL;
    }
    return result;
}