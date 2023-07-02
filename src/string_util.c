#include <stdlib.h>

#include "dynamic_array.h"
#include "string_util.h"

Dynamic_Array_Impl(char, String_Buffer, stringbuffer_)

char* copy_string(char* string) {
    size_t length = strlen(string);
    char* result = malloc(length + 1);
    memcpy(result, string, length);
    return result;
}
