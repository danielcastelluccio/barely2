#include <string.h>

#include "dynamic_array.h"

Dynamic_Array_Def(char, String_Buffer, stringbuffer_)

char* copy_string(char* string);
void stringbuffer_appendstring(String_Buffer* buffer, char* string);
