#include <stdbool.h>
#include <string.h>

#include "dynamic_array.h"

Dynamic_Array_Def(char*, Array_String, array_string_)
Dynamic_Array_Def(char, String_Buffer, stringbuffer_)

bool string_contains(char* string, char character);
int string_index(char* string, char character);
char* string_substring(char* string, size_t start, size_t end);
char* copy_string(char* string);
void stringbuffer_appendstring(String_Buffer* buffer, char* string);
