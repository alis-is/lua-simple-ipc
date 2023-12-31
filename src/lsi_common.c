#include "lsi_common.h"
#include <stdlib.h>
#include <string.h>

char*
get_endpoint_path(const char* endpoint, size_t* endpoint_len) {
    if (endpoint == NULL) {
        return NULL;
    }
#ifdef _WIN32
    int needsPrefix = endpoint_len <= PIPE_PREFIX_LEN || strncmp(endpoint, PIPE_PREFIX, PIPE_PREFIX_LEN) != 0;

    size_t pipe_prefix_len = needsPrefix ? PIPE_PREFIX_LEN : 0;
    size_t result_len = pipe_prefix_len + *endpoint_len;
#else
    size_t pipe_prefix_len = 0;
    size_t result_len = *endpoint_len;
#endif

    char* result = malloc(result_len + 1);
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
    if (memcpy((void*)(result + pipe_prefix_len), endpoint, *endpoint_len) == NULL) {
        free((void*)result);
        return NULL;
    }
    result[result_len] = '\0'; // null-terminate the string`
    *endpoint_len = result_len;
    return result;
}