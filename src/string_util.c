#include <stdlib.h>
#include <string.h>

#include "dynamic_array.h"
#include "string_util.h"

Dynamic_Array_Impl(char, String_Buffer, stringbuffer_)

char* copy_string(char* string) {
    size_t length = strlen(string);
    char* result = malloc(length + 1);
    memcpy(result, string, length);
    return result;
}

void stringbuffer_appendstring(String_Buffer* buffer, char* string) {
    for (size_t i = 0; i < strlen(string); i++) {
        stringbuffer_append(buffer, string[i]);
    }
}
