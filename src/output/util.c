#include <assert.h>
#include <stddef.h>

#include "util.h"

size_t get_length(Ast_Type* type) {
    switch (type->kind) {
        case Type_Array: {
            Ast_Type_Array* array_type = &type->data.array;
            assert(array_type->has_size);
            return array_type->size_type->data.number.value;
        }
        case Type_Pointer: {
            return get_length(type->data.pointer.child);
        }
        case Type_TypeOf: {
            return get_length(type->data.type_of.computed_result_type);
        }
        default:
            assert(false);
    }
}
