#ifndef FILE_DYNARRAY
#define FILE_DYNARRAY

#define Dynamic_Array_Def(Type_Inner, Array_Type_, name) \
typedef struct { \
    Type_Inner* elements; \
    size_t count; \
    size_t capacity; \
} Array_Type_; \
\
Array_Type_ name##new(size_t size); \
void name##append(Array_Type_* array, Type_Inner element); \
void name##clear(Array_Type_* array); \
void name##free(Array_Type_* array); \
Type_Inner name##get(Array_Type_* array, size_t index);

#define Dynamic_Array_Impl(Type_Inner, Array_Type_, name) \
Array_Type_ name##new(size_t size) { \
    return (Array_Type_) { \
        malloc(sizeof(Type_Inner) * size), \
        0, \
        size \
    }; \
} \
void name##append(Array_Type_* array, Type_Inner element) { \
    if (array->count == array->capacity) { \
        size_t new_capacity = array->capacity * 2; \
        Type_Inner* elements_new = (Type_Inner*) malloc(sizeof(Type_Inner) * new_capacity); \
        memcpy(elements_new, array->elements, sizeof(Type_Inner) * array->capacity); \
        free(array->elements); \
        array->elements = elements_new; \
        array->capacity = new_capacity; \
    } \
\
    array->elements[array->count] = element; \
    array->count++; \
} \
\
void name##clear(Array_Type_* array) { \
    memset(array->elements, 0, array->count); \
    array->count = 0; \
} \
\
void name##free(Array_Type_* array) { \
    free(array->elements); \
} \
\
Type_Inner name##get(Array_Type_* array, size_t index) { \
    return array->elements[index]; \
} \

#endif
