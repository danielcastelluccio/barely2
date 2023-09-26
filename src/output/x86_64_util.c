#include <assert.h>

#include "x86_64_util.h"
#include "../processor.h"

#include "../ast_walk.h"

size_t get_size(Ast_Type* type_in, Generic_State* state) {
    Ast_Type type = evaluate_type_complete(type_in, state);
    switch (type.kind) {
        case Type_Array: {
            Ast_Type_Array* array = &type.data.array;

            if (array->has_size) {
                assert(array->size_type->kind == Type_Number);
                size_t size = array->size_type->data.number.value;
                return size * get_size(array->element_type, state);
            }
            break;
        }
        case Type_Internal: {
            Ast_Type_Internal* internal = &type.data.internal;

            switch (*internal) {
                case Type_UInt:
                case Type_UInt64:
                case Type_Float64:
                case Type_Ptr:
                    return 8;
                case Type_UInt32:
                    return 4;
                case Type_UInt16:
                    return 2;
                case Type_UInt8:
                case Type_Byte:
                    return 1;
                case Type_Bool:
                    return 1;
            }
            break;
        }
        case Type_Pointer: {
            return 8;
        }
        case Type_Struct: {
            size_t size = 0;
            Ast_Type_Struct* struct_ = &type.data.struct_;
            for (size_t i = 0; i < struct_->items.count; i++) {
                size += get_size(&struct_->items.elements[i]->type, state);
            }
            return size;
        }
        case Type_Union: {
            size_t size = 0;
            Ast_Type_Union* union_ = &type.data.union_;
            for (size_t i = 0; i < union_->items.count; i++) {
                size_t size_temp = get_size(&union_->items.elements[i]->type, state);
                if (size_temp > size) {
                    size = size_temp;
                }
            }
            return size;
        }
        case Type_Enum: {
            return 8;
        }
        default:
            assert(false);
    }
    assert(false);
}

Location_Size_Data get_parent_item_location_size(Ast_Type* parent_type, char* item_name, Generic_State* state) {
    Location_Size_Data result = {};

    if (strcmp(item_name, "*") == 0) {
        result.size = get_size(parent_type, state);
    } else {
        switch (parent_type->kind) {
            case Type_Basic: {
                Ast_Identifier identifier = parent_type->data.basic.identifier;
                Resolved resolved = resolve(state, identifier);

                assert(resolved.kind == Resolved_Item);

                Ast_Item* item = resolved.data.item;
                assert(item->kind == Item_Type);
                Ast_Type type = item->data.type.type;
                return get_parent_item_location_size(&type, item_name, state);
            }
            case Type_Struct: {
                Ast_Type_Struct* struct_type = &parent_type->data.struct_;
                for (size_t i = 0; i < struct_type->items.count; i++) {
                    Ast_Declaration* declaration = struct_type->items.elements[i];
                    size_t item_size = get_size(&declaration->type, state);
                    if (strcmp(declaration->name, item_name) == 0) {
                        result.size = item_size;
                        break;
                    }
                    result.location += item_size;
                }
                break;
            }
            case Type_Union: {
                Ast_Type_Union* union_type = &parent_type->data.union_;
                for (size_t i = 0; i < union_type->items.count; i++) {
                    Ast_Declaration* declaration = union_type->items.elements[i];
                    size_t item_size = get_size(&declaration->type, state);
                    if (strcmp(declaration->name, item_name) == 0) {
                        result.size = item_size;
                        break;
                    }
                }
                break;
            }
            case Type_RunMacro: {
                return get_parent_item_location_size(parent_type->data.run_macro.result.data.type, item_name, state);
            }
            default:
                assert(false);
        }
    }

    return result;
}

bool has_local_variable(char* name, Generic_State* state) {
    for (int i = state->current_declares.count - 1; i >= 0; i--) {
        Ast_Declaration* declaration = &state->current_declares.elements[i];
        if (strcmp(declaration->name, name) == 0) {
            return true;
        }
    }

    return false;
}

Location_Size_Data get_local_variable_location_size(char* name, Generic_State* state) {
    Location_Size_Data result = { .location = 8 };

    bool found = false;

    for (int i = state->current_declares.count - 1; i >= 0; i--) {
        Ast_Declaration* declaration = &state->current_declares.elements[i];
        size_t declaration_size = get_size(&declaration->type, state);

        if (found) {
            result.location += declaration_size;
        }

        if (!found && strcmp(declaration->name, name) == 0) {
            result.size = declaration_size;
            found = true;
        }
    }

    return result;
}

bool has_argument(char* name, Generic_State* state) {
    for (int i = state->current_arguments.count - 1; i >= 0; i--) {
        Ast_Declaration* declaration = &state->current_arguments.elements[i];
        if (strcmp(declaration->name, name) == 0) {
            return true;
        }
    }

    return false;
}

Location_Size_Data get_argument_location_size(char* name, Generic_State* state) {
    Location_Size_Data result = { .location = 8 };

    for (int i = state->current_arguments.count - 1; i >= 0; i--) {
        Ast_Declaration* declaration = &state->current_arguments.elements[i];
        size_t declaration_size = get_size(&declaration->type, state);
        if (strcmp(declaration->name, name) == 0) {
            result.size = declaration_size;
            break;
        }
        result.location += declaration_size;
    }
    return result;
}

typedef struct {
    size_t total;
    Generic_State* state;
} Locals_Walk_State;

void collect_statement_locals_size(Ast_Statement* statement, void* state_in) {
    Locals_Walk_State* state = state_in;
    switch (statement->kind) {
        case Statement_Declare: {
            size_t size = 0;
            for (size_t i = 0; i < statement->data.declare.declarations.count; i++) {
                Ast_Type type = statement->data.declare.declarations.elements[i].type;
                size += get_size(&type, state->state);
            }
            state->total += size;
        }
        default:
            break;
    }
}

size_t get_locals_size(Ast_Item_Procedure* procedure, Generic_State* state) {
    Locals_Walk_State locals_state = {
        .total = 8,
        .state = state,
    };
    Ast_Walk_State walk_state = {
        .expression_func = NULL,
        .statement_func = collect_statement_locals_size,
        .internal_state = &locals_state,
    };
    walk_expression(procedure->body, &walk_state);
    return locals_state.total;
}

size_t get_arguments_size(Generic_State* state) {
    size_t result = 0;
    for (size_t i = 0; i < state->current_arguments.count; i++) {
        result += get_size(&state->current_arguments.elements[i].type, state);
    }
    return result;
}

size_t get_returns_size(Generic_State* state) {
    size_t result = 0;
    for (size_t i = 0; i < state->current_returns.count; i++) {
        result += get_size(state->current_returns.elements[i], state);
    }
    return result;
}
