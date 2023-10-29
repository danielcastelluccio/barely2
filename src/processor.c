#include <assert.h>
#include <stdio.h>

#include "ast_clone.h"
#include "ast_walk.h"
#include "file_util.h"
#include "processor.h"

Dynamic_Array_Impl(Ast_Type, Stack_Ast_Type, stack_type_)
Dynamic_Array_Impl(size_t, Array_Size, array_size_)

void stack_type_push(Stack_Ast_Type* stack, Ast_Type type) {
    stack_type_append(stack, type);
}

bool is_number_type(Ast_Type* type) {
    if (type->kind == Type_Internal) {
        Ast_Type_Internal internal = type->data.internal;

        if (internal == Type_UInt || internal == Type_UInt64 || internal == Type_UInt32 || internal == Type_UInt16 || internal == Type_UInt8 || internal == Type_Float64 || internal == Type_Byte) {
            return true;
        }
    }

    return false;
}

bool is_pointer_type(Ast_Type* type) {
    return (type->kind == Type_Internal && type->data.internal == Type_Ptr) || type->kind == Type_Pointer;
}

char* get_item_name(Ast_Item* item) {
    switch (item->kind) {
        case Item_Procedure:
            return item->data.procedure.name;
        case Item_Macro:
            return item->data.macro.name;
        case Item_Type:
            return item->data.type.name;
        case Item_Global:
            return item->data.global.name;
        case Item_Constant:
            return item->data.constant.name;
    }
    assert(false);
}

Resolved resolve(Generic_State* state, Ast_Identifier data) {
    Ast_Identifier initial_search = {};
    initial_search = data;

    Resolved result = { NULL, Unresolved, {} };
    for (size_t j = 0; j < state->program->count; j++) {
        Ast_File* file_node = &state->program->elements[j];
        bool stop = false;
        for (size_t i = 0; i < file_node->items.count; i++) {
            Ast_Item* item = &file_node->items.elements[i];
            if (strcmp(get_item_name(item), initial_search.name) == 0) {
                result = (Resolved) { file_node, Resolved_Item, { .item = item } };
                stop = true;
                break;
            }
        }

        if (stop) {
            break;
        }
    }

    return result;
}

Ast_Type create_basic_single_type(char* name) {
    Ast_Type type = { .directives = array_ast_directive_new(1) };
    Ast_Type_Basic basic = { .identifier = { .name = name } };

    type.kind = Type_Basic;
    type.data.basic = basic;

    return type;
}

Ast_Type create_internal_type(Ast_Type_Internal internal_type) {
    Ast_Type type = { .directives = array_ast_directive_new(1) };

    type.kind = Type_Internal;
    type.data.internal = internal_type;

    return type;
}

Ast_Type create_array_type(Ast_Type child) {
    Ast_Type type = { .directives = array_ast_directive_new(1) };
    Ast_Type_Array array = {};

    Ast_Type* child_allocated = malloc(sizeof(*child_allocated));
    *child_allocated = child;
    array.element_type = child_allocated;

    type.kind = Type_Array;
    type.data.array = array;

    return type;
}

Ast_Type create_pointer_type(Ast_Type child) {
    Ast_Type type = { .directives = array_ast_directive_new(1) };
    Ast_Type_Pointer pointer;

    Ast_Type* child_allocated = malloc(sizeof(*child_allocated));
    *child_allocated = child;
    pointer.child = child_allocated;

    type.kind = Type_Pointer;
    type.data.pointer = pointer;

    return type;
}

Ast_Type evaluate_type(Ast_Type* type) {
    switch (type->kind) {
        case Type_TypeOf: {
            assert(type->data.type_of.computed_result_type);
            return *type->data.type_of.computed_result_type;
        }
        case Type_RunMacro: {
            assert(type->data.run_macro.result.data.type != NULL);
            return *type->data.run_macro.result.data.type;
        }
        default:
            return *type;
    }
}

Ast_Type evaluate_type_complete(Ast_Type* type_in, Generic_State* state) {
    Ast_Type type = evaluate_type(type_in);
    switch (type.kind) {
        case Type_Basic: {
            Ast_Type_Basic* basic = &type.data.basic;
            Ast_Identifier identifier = basic->identifier;
            Resolved resolved = resolve(state, identifier);
            if (resolved.kind == Resolved_Item) {
                Ast_Item* item = resolved.data.item;
                assert(item->kind == Item_Type);
                Ast_Item_Type* type_node = &item->data.type;
                Ast_Type type_result = type_node->type;
                return evaluate_type_complete(&type_result, state);
            }
            break;
        }
        default:
            return type;
    }
    return type;
}

void process_expression(Ast_Expression* expression, Process_State* state);
void process_type(Ast_Type* type, Process_State* state);
void process_item_reference(Ast_Item* item, Process_State* state);

bool is_type(Ast_Type* wanted_in, Ast_Type* given_in, Process_State* state) {
    Ast_Type wanted = evaluate_type(wanted_in);
    Ast_Type given = evaluate_type(given_in);

    if (wanted.kind == Type_RegisterSize || given.kind == Type_RegisterSize) {
        Ast_Type to_check;
        if (wanted.kind == Type_RegisterSize) to_check = given;
        if (given.kind == Type_RegisterSize) to_check = wanted;

        if (to_check.kind == Type_Pointer) return true;
        if (to_check.kind == Type_Internal && (to_check.data.internal == Type_UInt || to_check.data.internal == Type_Ptr)) return true;

        return false;
    }

    if (wanted.kind != given.kind) {
        return false;
    }

    if (wanted.kind == Type_Pointer) {
        return is_type(wanted.data.pointer.child, given.data.pointer.child, state);
    }

    if (wanted.kind == Type_Basic) {
        return strcmp(wanted.data.basic.identifier.name, given.data.basic.identifier.name) == 0;
    }

    if (wanted.kind == Type_Array) {
        if (wanted.data.array.has_size) {
            if (!given.data.array.has_size) return false;
            if (!is_type(wanted.data.array.size_type, given.data.array.size_type, state)) return false;
        }
        return is_type(wanted.data.array.element_type, given.data.array.element_type, state);
    }

    if (wanted.kind == Type_Internal) {
        return wanted.data.internal == given.data.internal;
    }

    if (wanted.kind == Type_Procedure) {
        Ast_Type_Procedure* wanted_proc = &wanted.data.procedure;
        Ast_Type_Procedure* given_proc = &given.data.procedure;

        bool is_valid = true;
        if (wanted_proc->arguments.count != given_proc->arguments.count) is_valid = false;
        if (wanted_proc->returns.count != given_proc->returns.count) is_valid = false;

        if (is_valid) {
            for (size_t i = 0; i < wanted_proc->arguments.count; i++) {
                if (!is_type(array_ast_type_get(&wanted_proc->arguments, i), given_proc->arguments.elements[i], state)) {
                    is_valid = false;
                }
            }
        }

        if (is_valid) {
            for (size_t i = 0; i < wanted_proc->returns.count; i++) {
                if (!is_type(wanted_proc->returns.elements[i], given_proc->returns.elements[i], state)) {
                    is_valid = false;
                }
            }
        }

        return is_valid;
    }

    if (wanted.kind == Type_Struct) {
        Ast_Type_Struct* wanted_struct = &wanted.data.struct_;
        Ast_Type_Struct* given_struct = &given.data.struct_;

        bool is_valid = true;
        if (wanted_struct->items.count != given_struct->items.count) is_valid = false;

        if (is_valid) {
            for (size_t i = 0; i < wanted_struct->items.count; i++) {
                if (!is_type(&wanted_struct->items.elements[i]->type, &given_struct->items.elements[i]->type, state)) {
                    is_valid = false;
                }
            }
        }

        return is_valid;
    }

    if (wanted.kind == Type_Enum) {
        Ast_Type_Enum* wanted_enum = &wanted.data.enum_;
        Ast_Type_Enum* given_enum = &given.data.enum_;

        bool is_valid = true;
        if (wanted_enum->items.count != given_enum->items.count) is_valid = false;
        if (wanted_enum->items.count != given_enum->items.count) is_valid = false;

        for (size_t i = 0; i < wanted_enum->items.count; i++) {
            if (strcmp(given_enum->items.elements[i], given_enum->items.elements[i]) != 0) {
                is_valid = false;
            }
        }

        return is_valid;
    }

    if (wanted.kind == Type_Number) {
        if (wanted.data.number.value == given.data.number.value) return true;
    }

    return false;
}

bool is_internal_type(Ast_Type_Internal wanted, Ast_Type* given) {
    return given->kind == Type_Internal && given->data.internal == wanted;
}

void print_type_inline(Ast_Type* type) {
    switch (type->kind) {
        case Type_Basic: {
            Ast_Type_Basic* basic = &type->data.basic;

            printf("%s", basic->identifier.name);
            break;
        }
        case Type_Pointer: {
            Ast_Type_Pointer* pointer = &type->data.pointer;
            printf("*");
            print_type_inline(pointer->child);
            break;
        }
        case Type_Array: {
            Ast_Type_Array* array = &type->data.array;
            printf("[");
            if (array->has_size) {
                print_type_inline(array->size_type);
            }
            printf("]");
            print_type_inline(array->element_type);
            break;
        }
        case Type_Procedure: {
            Ast_Type_Procedure* procedure = &type->data.procedure;
            printf("proc(");
            for (size_t i = 0; i < procedure->arguments.count; i++) {
                print_type_inline(procedure->arguments.elements[i]);
                if (i < procedure->arguments.count - 1) {
                    printf(", ");
                }
            }
            printf(")");
            if (procedure->returns.count > 0) {
                printf(": ");

                for (size_t i = 0; i < procedure->returns.count; i++) {
                    print_type_inline(procedure->returns.elements[i]);
                    if (i < procedure->returns.count - 1) {
                        printf(", ");
                    }
                }
            }
            break;
        }
        case Type_Internal: {
            Ast_Type_Internal* internal = &type->data.internal;

            switch (*internal) {
                case Type_UInt:
                    printf("uint");
                    break;
                case Type_UInt64:
                    printf("uint64");
                    break;
                case Type_UInt32:
                    printf("uint32");
                    break;
                case Type_UInt16:
                    printf("uint16");
                    break;
                case Type_UInt8:
                    printf("uint8");
                    break;
                case Type_Float64:
                    printf("float64");
                    break;
                case Type_Byte:
                    printf("byte");
                    break;
                case Type_Ptr:
                    printf("ptr");
                    break;
                case Type_Bool:
                    printf("bool");
                    break;
            }
            break;
        }
        case Type_Struct:
        case Type_Union: {
            Array_Ast_Declaration_Pointer* items;
            if (type->kind == Type_Struct) {
                Ast_Type_Struct* struct_type = &type->data.struct_;
                items = &struct_type->items;
                printf("struct { ");
            } else if (type->kind == Type_Union) {
                Ast_Type_Union* union_type = &type->data.union_;
                items = &union_type->items;
                printf("union { ");
            }
            for (size_t i = 0; i < items->count; i++) {
                Ast_Declaration* declaration = items->elements[i];
                printf("%s: ", declaration->name);
                print_type_inline(&declaration->type);
                if (i < items->count - 1) {
                    printf(", ");
                }
            }
            printf(" }");
            break;
        }
        case Type_Enum: {
            Ast_Type_Enum* enum_type = &type->data.enum_;
            printf("enum { ");
            for (size_t i = 0; i < enum_type->items.count; i++) {
                printf("%s", enum_type->items.elements[i]);
                if (i < enum_type->items.count - 1) {
                    printf(", ");
                }
            }
            printf(" }");
            break;
        }
        case Type_RegisterSize: {
            printf("size");
            break;
        }
        case Type_Number: {
            printf("%zu", type->data.number.value);
            break;
        }
        case Type_TypeOf: {
            printf("@typeof");
            break;
        }
        case Type_RunMacro: {
            printf("RunMacro");
            break;
        }
        default:
            printf("%i\n", type->kind);
            assert(false);
    }
}

void print_error_stub(Location* location) {
    printf("%s:%zu:%zu: ", location->file, location->row, location->col);
}

bool consume_in_reference(Generic_State* state) {
    bool cached = state->in_reference;
    state->in_reference = false;
    return cached;
}

Ast_Type_Basic identifier_to_basic_type(Ast_Identifier identifier) {
    Ast_Type_Basic basic = { .identifier = { .name = identifier.name } };
    return basic;
}

static Ast_Type* _usize_type;

Ast_Type* usize_type() {
    if (_usize_type == NULL) {
        Ast_Type* usize = malloc(sizeof(*usize));
        *usize = create_internal_type(Type_UInt);
        _usize_type = usize;
    }
    return _usize_type;
}

typedef struct {
    Array_String bindings;
    bool varargs;
    Array_Ast_Macro_Syntax_Data values;
} Apply_Macro_Walk_State;

void apply_macro_values_expression(Ast_Expression* expression, void* state_in) {
    Apply_Macro_Walk_State* state = state_in;
    switch (expression->kind) {
        case Expression_Retrieve: {
            Ast_Expression_Retrieve* retrieve = &expression->data.retrieve;

            switch (retrieve->kind) {
                case Retrieve_Assign_Identifier: {
                    Ast_Identifier identifier = retrieve->data.identifier;

                    for (size_t i = 0; i < state->bindings.count; i++) {
                        if (strcmp(state->bindings.elements[i], identifier.name) == 0) {
                            assert(state->values.elements[i]->kind == Macro_Expression);
                            assert(state->values.elements[i]->data.expression->kind != Expression_Multiple);
                            *expression = *state->values.elements[i]->data.expression;
                        }
                    }

                    break;
                }
                default:
                    break;
            }
            break;
        }
        case Expression_RunMacro: {
            Array_Ast_Macro_Syntax_Data datas = array_ast_macro_syntax_data_new(expression->data.run_macro.arguments.count);
            for (size_t i = 0; i < expression->data.run_macro.arguments.count; i++) {
                Ast_Macro_Syntax_Data* syntax_data = expression->data.run_macro.arguments.elements[i];

                bool handled = false;
                if (syntax_data->kind == Macro_Expression && syntax_data->data.expression->kind == Expression_Retrieve &&
                        syntax_data->data.expression->data.retrieve.kind == Retrieve_Assign_Identifier) {
                    Ast_Identifier identifier = syntax_data->data.expression->data.retrieve.data.identifier;
                    for (size_t j = 0; j < state->bindings.count; j++) {
                        if (strcmp(state->bindings.elements[j], identifier.name) == 0) {
                            if (j + 1 == state->bindings.count && state->varargs) {
                                for (size_t k = j; k < state->values.count; k++) {
                                    array_ast_macro_syntax_data_append(&datas, state->values.elements[k]);
                                }
                                handled = true;
                            }
                        }
                    }
                }

                if (!handled) {
                    array_ast_macro_syntax_data_append(&datas, syntax_data);
                }
            }

            expression->data.run_macro.arguments = datas;
            break;
        }
        default:
            break;
    }
}

void apply_macro_values_type(Ast_Type* type, void* state_in) {
    Apply_Macro_Walk_State* state = state_in;

    switch (type->kind) {
        case Type_Basic: {
            Ast_Type_Basic* basic = &type->data.basic;
            for (size_t i = 0; i < state->bindings.count; i++) {
                if (strcmp(state->bindings.elements[i], basic->identifier.name) == 0) {
                    *type = *state->values.elements[i]->data.type;
                    break;
                }
            }
            break;
        }
        default:
            break;
    }
}

void process_run_macro(Ast_RunMacro* run_macro, Ast_Macro_Syntax_Kind kind, Process_State* state) {
    Resolved resolved = resolve(&state->generic, run_macro->identifier);
    assert(resolved.kind == Resolved_Item && resolved.data.item->kind == Item_Macro);
    Ast_Item_Macro* macro = &resolved.data.item->data.macro;

    assert(macro->return_.kind == kind);

    size_t current_macro_argument_index = 0;
    for (size_t i = 0; i < run_macro->arguments.count; i++) {
        Ast_Macro_Argument current_macro_argument = macro->arguments.elements[current_macro_argument_index];
        Ast_Macro_Syntax_Kind given_argument = run_macro->arguments.elements[i]->kind;
        if (current_macro_argument.kind != given_argument) {
            print_error_stub(&run_macro->location);
            printf("Macro invocation with wrong type!\n");
            exit(1);
        }

        if (!current_macro_argument.multiple) {
            current_macro_argument_index++;
        }
    }

    Ast_Macro_Variant variant;
    bool matched = false;
    for (size_t i = 0; i < macro->variants.count; i++) {
        bool matches = true;

        Ast_Macro_Variant* current_variant = &macro->variants.elements[i];
        size_t argument_index = 0;
        for (size_t j = 0; j < current_variant->bindings.count; j++) {
            if (j + 1 == current_variant->bindings.count && current_variant->varargs) {
                if (argument_index == run_macro->arguments.count) {
                    matches = false;
                    break;
                }
                argument_index = run_macro->arguments.count;
            } else {
                if (argument_index == run_macro->arguments.count) {
                    matches = false;
                    break;
                }
                argument_index++;
            }
        }

        if (argument_index < run_macro->arguments.count) {
            matches = false;
        }

        if (matches) {
            matched = true;
            variant = macro->variants.elements[i];
            break;
        }
    }

    assert(matched);
    assert(variant.data.kind == kind);

    Apply_Macro_Walk_State locals_state = {
        .bindings = variant.bindings,
        .varargs = variant.varargs,
        .values = run_macro->arguments,
    };
    Ast_Walk_State walk_state = {
        .expression_func = apply_macro_values_expression,
        .type_func = apply_macro_values_type,
        .internal_state = &locals_state,
    };

    switch (kind) {
        case Macro_Expression: {
            Ast_Expression cloned = clone_expression(*variant.data.data.expression);
            walk_expression(&cloned, &walk_state);

            run_macro->result.kind = Macro_Expression;
            run_macro->result.data.expression = malloc(sizeof(*run_macro->result.data.expression));
            *run_macro->result.data.expression = cloned;

            process_expression(run_macro->result.data.expression, state);
            break;
        }
        case Macro_Type: {
            Ast_Type cloned = clone_type(*variant.data.data.type);
            walk_type(&cloned, &walk_state);

            run_macro->result.kind = Macro_Type;
            run_macro->result.data.type = malloc(sizeof(*run_macro->result.data.type));
            *run_macro->result.data.type = cloned;

            process_type(run_macro->result.data.type, state);
            break;
        }
        default:
            assert(false);
    }
}

void process_type(Ast_Type* type, Process_State* state) {
    switch (type->kind) {
        case Type_Basic: {
            Ast_Type_Basic* basic = &type->data.basic;
            Ast_Identifier identifier = basic->identifier;
            Resolved resolved = resolve(&state->generic, identifier);
            if (resolved.kind == Resolved_Item) {
                Ast_Item* item = resolved.data.item;
                process_item_reference(item, state);
            }
            break;
        }
        case Type_Struct: {
            Ast_Type_Struct* struct_ = &type->data.struct_;
            for (size_t i = 0; i < struct_->items.count; i++) {
                process_type(&struct_->items.elements[i]->type, state);
            }
            break;
        }
        case Type_TypeOf: {
            Ast_Type_TypeOf* type_of = &type->data.type_of;
            Ast_Expression* expression = type_of->expression;
            size_t stack_initial = state->stack.count;
            process_expression(expression, state);

            Ast_Type* type = malloc(sizeof(*type));
            *type = stack_type_pop(&state->stack);
            type_of->computed_result_type = type;

            assert(state->stack.count == stack_initial);
            break;
        }
        case Type_RunMacro: {
            Ast_RunMacro* run_macro = &type->data.run_macro;
            process_run_macro(run_macro, Macro_Type, state);
            break;
        }
        default:
            break;
    }
}

Ast_Type get_parent_item_type(Ast_Type* parent_type_in, char* item_name, Generic_State* state) {
    Ast_Type parent_type = evaluate_type_complete(parent_type_in, state);
    Ast_Type result = { .kind = Type_None };

    if (strcmp(item_name, "*") == 0) {
        result = *parent_type_in;
    } else {
        switch (parent_type.kind) {
            case Type_Struct:
            case Type_Union: {
                Array_Ast_Declaration_Pointer* items;
                if (parent_type.kind == Type_Struct) {
                    Ast_Type_Struct* struct_ = &parent_type.data.struct_;
                    items = &struct_->items;
                } else if (parent_type.kind == Type_Union) {
                    Ast_Type_Union* union_ = &parent_type.data.union_;
                    items = &union_->items;
                } else {
                    assert(false);
                }

                for (size_t i = 0; i < items->count; i++) {
                    Ast_Declaration* declaration = items->elements[i];
                    if (strcmp(declaration->name, item_name) == 0) {
                        result = declaration->type;
                    }
                }
                break;
            }
            case Type_RunMacro: {
                assert(parent_type.data.run_macro.result.data.type != NULL);
                return get_parent_item_type(parent_type.data.run_macro.result.data.type, item_name, state);
            }
            default:
                assert(false);
        }
    }

    return result;
}

typedef enum {
    Operating_System_Linux,
    Operating_System_Windows,
} Operating_System;

typedef struct {
    enum {
        Evaluation_Boolean,
        Evaluation_Operating_System,
    } kind;
    union {
        bool boolean;
        Operating_System operating_system;
    } data;
} Evaluation_Value;

Evaluation_Value evaluate_if_directive_expression(Ast_Expression* expression, Process_State* state) {
    switch (expression->kind) {
        case Expression_Retrieve: {
            if (expression->data.retrieve.kind == Retrieve_Assign_Identifier) {
                char* identifier = expression->data.retrieve.data.identifier.name;

                if (strcmp(identifier, "@os")) {
                    return (Evaluation_Value) { .kind = Evaluation_Operating_System, .data = { .operating_system = Operating_System_Linux } };
                } else if (strcmp(identifier, "@linux")) {
                    return (Evaluation_Value) { .kind = Evaluation_Operating_System, .data = { .operating_system = Operating_System_Linux } };
                }
            }
            break;
        }
        case Expression_Invoke: {
            Ast_Expression_Invoke* invoke = &expression->data.invoke;
            if (invoke->kind == Invoke_Operator) {
                Operator* operator = &invoke->data.operator_.operator_;
                Evaluation_Value left = evaluate_if_directive_expression(invoke->arguments.elements[0], state);
                Evaluation_Value right = evaluate_if_directive_expression(invoke->arguments.elements[1], state);

                switch (*operator) {
                    case Operator_Equal: {
                        if (left.kind == Evaluation_Operating_System && right.kind == Evaluation_Operating_System) {
                            bool result = left.data.operating_system == right.data.operating_system;
                            return (Evaluation_Value) { .kind = Evaluation_Boolean, .data = { .boolean = result } };
                        }
                        break;
                    }
                    case Operator_NotEqual: {
                        if (left.kind == Evaluation_Operating_System && right.kind == Evaluation_Operating_System) {
                            bool result = left.data.operating_system != right.data.operating_system;
                            return (Evaluation_Value) { .kind = Evaluation_Boolean, .data = { .boolean = result } };
                        }
                        break;
                    }
                    default:
                        assert(false);
                }
            }
            break;
        }
        case Expression_Boolean: {
            return (Evaluation_Value) { .kind = Evaluation_Boolean, .data = { .boolean = expression->data.boolean.value } };
        }
        case Expression_IsType: {
            Ast_Expression_IsType* is_type_node = &expression->data.is_type;
            process_type(&is_type_node->given, state);
            process_type(&is_type_node->wanted, state);
            return (Evaluation_Value) { .kind = Evaluation_Boolean, .data = { .boolean = is_type(&is_type_node->given, &is_type_node->wanted, state) } };
        }
        default:
            break;
    }
    assert(false);
}

bool evaluate_if_directive(Ast_Directive_If* if_node, Process_State* state) {
    Evaluation_Value value = evaluate_if_directive_expression(if_node->expression, state);
    assert(value.kind == Evaluation_Boolean);
    return value.data.boolean;
}

void process_assign(Ast_Statement_Assign* assign, Process_State* state) {
    Array_Ast_Type wanted_types = array_ast_type_new(1);
    for (size_t i = 0; i < assign->parts.count; i++) {
        Statement_Assign_Part* assign_part = &assign->parts.elements[i];
        Ast_Type* wanted_type = malloc(sizeof(*wanted_type));
        bool found = false;

        if (assign_part->kind == Retrieve_Assign_Array) {
            process_expression(assign_part->data.array.expression_outer, state);
            Ast_Type array_type = stack_type_pop(&state->stack);

            Ast_Type* array_ast_type_raw;
            if (array_type.kind == Type_Pointer) {
                array_ast_type_raw = array_type.data.pointer.child;
            } else {
                array_ast_type_raw = &array_type;
            }

            if (array_ast_type_raw->kind != Type_Array) {
                print_error_stub(&assign_part->location);
                printf("Type '");
                print_type_inline(array_ast_type_raw);
                printf("' is not an array\n");
                exit(1);
            }

            wanted_type = array_ast_type_raw->data.array.element_type;
            found = true;
        }

        if (assign_part->kind == Retrieve_Assign_Identifier) {
            char* name = assign_part->data.identifier.name;

            Ast_Type* type = malloc(sizeof(*type));
            for (size_t j = 0; j < state->generic.current_declares.count; j++) {
                if (strcmp(state->generic.current_declares.elements[j].name, name) == 0) {
                    *type = state->generic.current_declares.elements[j].type;
                    found = true;
                    break;
                }
            }

            if (!found) {
                Resolved resolved = resolve(&state->generic, assign_part->data.identifier);
                if (resolved.kind == Resolved_Item) {
                    if (resolved.data.item->kind == Item_Global) {
                        *type = resolved.data.item->data.global.type;
                        found = true;
                    }
                }
            }

            wanted_type = type;
        }

        if (assign_part->kind == Retrieve_Assign_Identifier) {
            Resolved resolved = resolve(&state->generic, assign_part->data.identifier);
            if (resolved.kind == Resolved_Item) {
                if (resolved.data.item->kind == Item_Global) {
                    *wanted_type = resolved.data.item->data.global.type;
                    found = true;
                }
            }
        }

        if (assign_part->kind == Retrieve_Assign_Parent) {
            process_expression(assign_part->data.parent.expression, state);
            Ast_Type parent_type = stack_type_pop(&state->stack);

            Ast_Type* parent_type_raw;
            if (parent_type.kind == Type_Pointer) {
                parent_type_raw = parent_type.data.pointer.child;
            } else {
                parent_type_raw = &parent_type;
                assign_part->data.parent.needs_reference = true;
            }

            assign_part->data.parent.computed_parent_type = *parent_type_raw;

            Ast_Type resolved = get_parent_item_type(parent_type_raw, assign_part->data.parent.name, &state->generic);
            if (resolved.kind != Type_None) {
                *wanted_type = resolved;
                found = true;
            }
        }

        if (!found) {
            print_error_stub(&assign_part->location);
            printf("Assign not found\n");
            exit(1);
        }

        array_ast_type_append(&wanted_types, wanted_type);
    }

    if (assign->expression->kind == Expression_Multiple) {
        size_t assign_index = 0;
        for (size_t i = 0; i < assign->expression->data.multiple.expressions.count; i++) {
            size_t stack_start = state->stack.count;
            state->wanted_type = wanted_types.elements[assign_index];
            process_expression(assign->expression->data.multiple.expressions.elements[i], state);
            assign_index += state->stack.count - stack_start;
        }
    } else {
        state->wanted_type = wanted_types.elements[wanted_types.count - 1];
        process_expression(assign->expression, state);
    }

    for (int i = assign->parts.count - 1; i >= 0; i--) {
        Statement_Assign_Part* assign_part = &assign->parts.elements[i];

        if (assign_part->kind == Retrieve_Assign_Array) {
            process_expression(assign_part->data.array.expression_outer, state);
            Ast_Type array_type = stack_type_pop(&state->stack);

            Ast_Type* element_type = wanted_types.elements[assign->parts.count - i - 1];

            assign_part->data.array.computed_array_type = array_type;

            state->wanted_type = usize_type();

            process_expression(assign_part->data.array.expression_inner, state);

            Ast_Type array_index_type = stack_type_pop(&state->stack);
            if (!is_internal_type(Type_UInt, &array_index_type)) {
                print_error_stub(&assign_part->location);
                printf("Type '");
                print_type_inline(&array_index_type);
                printf("' cannot be used to access array\n");
                exit(1);
            }

            if (state->stack.count == 0) {
                print_error_stub(&assign_part->location);
                printf("Ran out of values for assignment\n");
                exit(1);
            }

            Ast_Type right_side_given_type = stack_type_pop(&state->stack);
            if (!is_type(element_type, &right_side_given_type, state)) {
                print_error_stub(&assign_part->location);
                printf("Type '");
                print_type_inline(&right_side_given_type);
                printf("' is not assignable to index of array of type '");
                print_type_inline(element_type);
                printf("'\n");
                exit(1);
            }
        }

        if (assign_part->kind == Retrieve_Assign_Identifier) {
            Ast_Type* variable_type = wanted_types.elements[assign->parts.count - i - 1];

            if (state->stack.count == 0) {
                print_error_stub(&assign_part->location);
                printf("Ran out of values for declaration assignment\n");
                exit(1);
            }

            Ast_Type popped = stack_type_pop(&state->stack);
            if (!is_type(variable_type, &popped, state)) {
                print_error_stub(&assign_part->location);
                printf("Type '");
                print_type_inline(&popped);
                printf("' is not assignable to variable of type '");
                print_type_inline(variable_type);
                printf("'\n");
                exit(1);
            }
        }

        if (assign_part->kind == Retrieve_Assign_Parent) {
            Ast_Type* item_type = wanted_types.elements[assign->parts.count - i - 1];

            if (state->stack.count == 0) {
                print_error_stub(&assign_part->location);
                printf("Ran out of values for item assignment\n");
                exit(1);
            }

            Ast_Type right_side_given_type = stack_type_pop(&state->stack);
            if (!is_type(item_type, &right_side_given_type, state)) {
                print_error_stub(&assign_part->location);
                printf("Type '");
                print_type_inline(&right_side_given_type);
                printf("' is not assignable to item of type '");
                print_type_inline(item_type);
                printf("'\n");
                exit(1);
            }
        }
    }
}

void check_return(Process_State* state, Location* location) {
    for (int i = state->generic.current_returns.count - 1; i >= 0; i--) {
        Ast_Type* return_type = state->generic.current_returns.elements[i];

        if (state->stack.count == 0) {
            print_error_stub(location);
            printf("Ran out of values for return\n");
            exit(1);
        }

        Ast_Type given_type = stack_type_pop(&state->stack);
        if (!is_type(return_type, &given_type, state)) {
            print_error_stub(location);
            printf("Type '");
            print_type_inline(&given_type);
            printf("' is not returnable of type '");
            print_type_inline(return_type);
            printf("'\n");
            exit(1);
        }
    }

    if (state->stack.count > 0) {
        print_error_stub(location);
        printf("Extra values at the end of return\n");
        exit(1);
    }
}

void process_statement(Ast_Statement* statement, Process_State* state) {
    if (has_directive(&statement->directives, Directive_If)) {
        Ast_Directive_If* if_node = &get_directive(&statement->directives, Directive_If)->data.if_;
        if_node->result = evaluate_if_directive(if_node, state);
        if (!if_node->result) {
            return;
        }
    }

    switch (statement->kind) {
        case Statement_Expression: {
            Ast_Statement_Expression* statement_expression = &statement->data.expression;
            process_expression(statement_expression->expression, state);

            if (!statement_expression->skip_stack_check && state->stack.count > 0) {
                print_error_stub(&statement->statement_end_location);
                printf("Extra values at the end of statement\n");
                exit(1);
            }
            break;
        }
        case Statement_Declare: {
            Ast_Statement_Declare* declare = &statement->data.declare;
            for (size_t i = 0; i < declare->declarations.count; i++) {
                Ast_Declaration* declaration = &declare->declarations.elements[i];
                process_type(&declaration->type, state);
            }

            if (declare->expression != NULL) {
                if (declare->expression->kind == Expression_Multiple) {
                    size_t declare_index = 0;
                    for (size_t i = 0; i < declare->expression->data.multiple.expressions.count; i++) {
                        Ast_Declaration* declaration = &declare->declarations.elements[i];
                        size_t stack_start = state->stack.count;
                        state->wanted_type = &declaration->type;
                        process_expression(declare->expression->data.multiple.expressions.elements[i], state);
                        declare_index += state->stack.count - stack_start;
                    }
                } else {
                    state->wanted_type = &declare->declarations.elements[declare->declarations.count - 1].type;
                    process_expression(declare->expression, state);
                }

                for (int i = declare->declarations.count - 1; i >= 0; i--) {
                    Ast_Declaration* declaration = &declare->declarations.elements[i];

                    if (state->stack.count == 0) {
                        print_error_stub(&declaration->location);
                        printf("Ran out of values for declaration assignment\n");
                        exit(1);
                    }

                    Ast_Type popped = stack_type_pop(&state->stack);
                    if (!is_type(&declaration->type, &popped, state)) {
                        print_error_stub(&declaration->location);
                        printf("Type '");
                        print_type_inline(&popped);
                        printf("' is not assignable to variable of type '");
                        print_type_inline(&declaration->type);
                        printf("'\n");
                        exit(1);
                    }
                    array_ast_declaration_append(&state->generic.current_declares, *declaration);
                }
            } else {
                for (int i = declare->declarations.count - 1; i >= 0; i--) {
                    Ast_Declaration* declaration = &declare->declarations.elements[i];
                    process_type(&declaration->type, state);
                    array_ast_declaration_append(&state->generic.current_declares, *declaration);
                }
            }

            if (state->stack.count > 0) {
                print_error_stub(&statement->statement_end_location);
                printf("Extra values at the end of declare statement\n");
                exit(1);
            }
            break;
        }
        case Statement_Assign: {
            process_assign(&statement->data.assign, state);

            if (state->stack.count > 0) {
                print_error_stub(&statement->statement_end_location);
                printf("Extra values at the end of assign statement\n");
                exit(1);
            }
            break;
        }
        case Statement_Return: {
            Ast_Statement_Return* return_ = &statement->data.return_;
            if (return_->expression != NULL) {
                if (return_->expression->kind == Expression_Multiple) {
                    size_t declare_index = 0;
                    for (size_t i = 0; i < return_->expression->data.multiple.expressions.count; i++) {
                        size_t stack_start = state->stack.count;
                        state->wanted_type = state->generic.current_returns.elements[declare_index];
                        process_expression(return_->expression->data.multiple.expressions.elements[i], state);
                        declare_index += state->stack.count - stack_start;
                    }
                } else {
                    state->wanted_type = state->generic.current_returns.elements[state->generic.current_returns.count - 1];
                    process_expression(return_->expression, state);
                }
            }

            check_return(state, &return_->location);

            break;
        }
        case Statement_While: {
            Ast_Statement_While* node = &statement->data.while_;

            process_expression(node->condition, state);

            if (state->stack.count == 0) {
                print_error_stub(&node->location);
                printf("Ran out of values for if\n");
                exit(1);
            }

            Ast_Type given = stack_type_pop(&state->stack);
            Ast_Type bool_type = create_internal_type(Type_Bool);
            if (!is_type(&bool_type, &given, state)) {
                print_error_stub(&node->location);
                printf("Type '");
                print_type_inline(&given);
                printf("' is not a boolean\n");
                exit(1);
            }

            process_expression(node->inside, state);
            break;
        }
        case Statement_Break: {
            break;
        }
        default:
            assert(false);
    }
}

bool is_register_sized(Ast_Type* type, Process_State* state) {
    Ast_Type register_type = (Ast_Type) { .kind = Type_RegisterSize, .data = {} };
    return is_type(&register_type, type, state);
}

bool is_like_number_literal(Ast_Expression* expression) {
    if (expression->kind == Expression_Number) return true;
    if (expression->kind == Expression_SizeOf) return true;
    return false;
}

Ast_Type* get_local_variable_type(Process_State* state, char* name) {
    for (int i = state->generic.current_declares.count - 1; i >= 0; i--) {
        Ast_Declaration* declaration = &state->generic.current_declares.elements[i];
        if (strcmp(declaration->name, name) == 0) {
            return &declaration->type;
        }
    }

    return NULL;
}

bool can_operate_together(Ast_Type* first, Ast_Type* second, Process_State* state) {
    if (is_type(first, second, state)) {
        return true;
    }

    if (first->kind == Type_Internal && first->data.internal == Type_Ptr && second->kind == Type_Internal && second->data.internal == Type_UInt) {
        return true;
    }

    return false;
}

void process_build_type(Ast_Expression_Build* build, Ast_Type* type, Process_State* state) {
    switch (type->kind) {
        case Type_Basic: {
            Resolved resolved = resolve(&state->generic, type->data.basic.identifier);
            switch (resolved.kind) {
                case Resolved_Item: {
                    Ast_Item* item = resolved.data.item;
                    assert(item->kind == Item_Type);
                    process_build_type(build, &item->data.type.type, state);
                    break;
                }
                case Unresolved:
                    assert(false);
            }
            break;
        }
        case Type_Struct: {
            Ast_Type_Struct* struct_ = &type->data.struct_;
            if (build->arguments.count == struct_->items.count) {
                for (int i = build->arguments.count - 1; i >= 0; i--) {
                    Ast_Type* wanted_type = &struct_->items.elements[i]->type;
                    state->wanted_type = wanted_type;

                    process_expression(build->arguments.elements[i], state);

                    Ast_Type popped = stack_type_pop(&state->stack);
                    if (!is_type(wanted_type, &popped, state)) {
                        print_error_stub(&build->location);
                        printf("Building of struct expected '");
                        print_type_inline(&struct_->items.elements[i]->type);
                        printf("', given '");
                        print_type_inline(&popped);
                        printf("'\n");
                        exit(1);
                    }
                }

                for (size_t i = 0; i < struct_->items.count; i++) {
                }
            } else {
                print_error_stub(&build->location);
                printf("Building of struct doesn't provide all item values\n");
                exit(1);
            }
            break;
        }
        case Type_Array: {
            Ast_Type_Array* array = &type->data.array;
            assert(array->size_type->kind == Type_Number);
            size_t array_size = array->size_type->data.number.value;

            if (build->arguments.count == array_size) {
                for (int i = build->arguments.count - 1; i >= 0; i--) {
                    Ast_Type* wanted_type = array->element_type;
                    state->wanted_type = wanted_type;

                    process_expression(build->arguments.elements[i], state);

                    Ast_Type popped = stack_type_pop(&state->stack);
                    if (!is_type(wanted_type, &popped, state)) {
                        print_error_stub(&build->location);
                        printf("Building of array expected '");
                        print_type_inline(array->element_type);
                        printf("', given '");
                        print_type_inline(&popped);
                        printf("'\n");
                        exit(1);
                    }
                }
            } else {
                print_error_stub(&build->location);
                printf("Building of array doesn't provide all index values\n");
                exit(1);
            }
            break;
        }
        case Type_RunMacro: {
            process_build_type(build, type->data.run_macro.result.data.type, state);
            break;
        }
        default:
            assert(false);
    }
}

void process_expression(Ast_Expression* expression, Process_State* state) {
    switch (expression->kind) {
        case Expression_Block: {
            Ast_Expression_Block* block = &expression->data.block;
            array_size_append(&state->scoped_declares, state->generic.current_declares.count);
            for (size_t i = 0; i < block->statements.count; i++) {
                process_statement(block->statements.elements[i], state);
            }
            state->generic.current_declares.count = state->scoped_declares.elements[state->scoped_declares.count - 1];
            state->scoped_declares.count--;
            break;
        }
        case Expression_Multiple: {
            Ast_Expression_Multiple* multiple = &expression->data.multiple;
            for (size_t i = 0; i < multiple->expressions.count; i++) {
                process_expression(multiple->expressions.elements[i], state);
            }
            break;
        }
        case Expression_Invoke: {
            Ast_Expression_Invoke* invoke = &expression->data.invoke;

            if (invoke->kind == Invoke_Standard) {
                Ast_Expression* procedure = invoke->data.procedure.procedure;
                bool handled = false;

                if (procedure->kind == Expression_Retrieve) {
                    bool is_internal = false;
                    if (procedure->data.retrieve.kind == Retrieve_Assign_Identifier) {
                        char* name = procedure->data.retrieve.data.identifier.name;

                        if (strcmp(name, "@syscall6") == 0 || strcmp(name, "@syscall5") == 0 || strcmp(name, "@syscall4") == 0 || strcmp(name, "@syscall3") == 0 || strcmp(name, "@syscall2") == 0 || strcmp(name, "@syscall1") == 0 || strcmp(name, "@syscall0") == 0) {
                            is_internal = true;
                        }
                    }

                    if (is_internal) {
                        char* name = procedure->data.retrieve.data.identifier.name;

                        for (size_t i = 0; i < invoke->arguments.count; i++) {
                            state->wanted_type = usize_type();
                            process_expression(invoke->arguments.elements[i], state);
                        }

                        if (strncmp(name, "@syscall", 8) == 0) {
                            size_t count = strtoul(name + 8, NULL, 0) + 1;

                            if (state->stack.count < count) {
                                print_error_stub(&invoke->location);
                                printf("Ran out of values for %s, needed %zu\n", name, count);
                                exit(1);
                            }

                            for (size_t i = 0; i < count; i++) {
                                Ast_Type type = stack_type_pop(&state->stack);
                                if (!is_register_sized(&type, state)) {
                                    print_error_stub(&invoke->location);
                                    printf("Type '");
                                    print_type_inline(&type);
                                    printf("' cannot be passed to a syscall\n");
                                    exit(1);
                                }
                            }

                            stack_type_push(&state->stack, (Ast_Type) { .kind = Type_RegisterSize, .data = {} });
                        }

                        handled = true;
                    }
                }

                if (!handled) {
                    process_expression(procedure, state);

                    Ast_Type type = stack_type_pop(&state->stack);
                    if (type.kind != Type_Pointer || type.data.pointer.child->kind != Type_Procedure) {
                        print_error_stub(&invoke->location);
                        printf("Attempting to invoke a non procedure\n");
                        exit(1);
                    }

                    Ast_Type_Procedure* procedure_type = &type.data.pointer.child->data.procedure;
                    invoke->data.procedure.computed_procedure_type = *type.data.pointer.child;
                    size_t arg_index = 0;
                    for (size_t i = 0; i < invoke->arguments.count; i++) {
                        size_t stack_start = state->stack.count;
                        Ast_Type* argument_type = procedure_type->arguments.elements[arg_index];
                        state->wanted_type =argument_type;
                        process_expression(invoke->arguments.elements[i], state);
                        arg_index += state->stack.count - stack_start;
                    }

                    for (int i = procedure_type->arguments.count - 1; i >= 0; i--) {
                        if (state->stack.count == 0) {
                            print_error_stub(&invoke->location);
                            printf("Ran out of values for invocation\n");
                            exit(1);
                        }

                        Ast_Type given = stack_type_pop(&state->stack);
                        if (!is_type(procedure_type->arguments.elements[i], &given, state)) {
                            print_error_stub(&invoke->location);
                            printf("Type '");
                            print_type_inline(&given);
                            printf("' is not assignable to argument of type '");
                            print_type_inline(procedure_type->arguments.elements[i]);
                            printf("'\n");
                            exit(1);
                        }
                    }

                    for (size_t i = 0; i < procedure_type->returns.count; i++) {
                        stack_type_push(&state->stack, *procedure_type->returns.elements[i]);
                    }
                }
            } else if (invoke->kind == Invoke_Operator) {
                Operator operator = invoke->data.operator_.operator_;

                if (operator == Operator_Add ||
                        operator == Operator_Subtract ||
                        operator == Operator_Multiply ||
                        operator == Operator_Divide ||
                        operator == Operator_Modulus ||
                        operator == Operator_Equal ||
                        operator == Operator_NotEqual ||
                        operator == Operator_Greater ||
                        operator == Operator_GreaterEqual ||
                        operator == Operator_Less ||
                        operator == Operator_LessEqual) {
                    bool reversed = is_like_number_literal(invoke->arguments.elements[0]);

                    process_expression(invoke->arguments.elements[reversed ? 1 : 0], state);

                    Ast_Type* wanted = malloc(sizeof(*wanted));
                    *wanted = state->stack.elements[state->stack.count - 1];
                    state->wanted_type = wanted;

                    process_expression(invoke->arguments.elements[reversed ? 0 : 1], state);
                } else if (operator == Operator_Not) {
                    process_expression(invoke->arguments.elements[0], state);
                } else if (operator == Operator_And ||
                        operator == Operator_Or) {
                    process_expression(invoke->arguments.elements[0], state);
                    process_expression(invoke->arguments.elements[1], state);
                } else {
                    assert(false);
                }

                if (operator == Operator_Add ||
                        operator == Operator_Subtract ||
                        operator == Operator_Multiply ||
                        operator == Operator_Divide ||
                        operator == Operator_Modulus) {
                    Ast_Type second = stack_type_pop(&state->stack);
                    Ast_Type first = stack_type_pop(&state->stack);

                    if (!can_operate_together(&first, &second, state)) {
                        print_error_stub(&invoke->location);
                        printf("Type '");
                        print_type_inline(&second);
                        printf("' cannot be operated on with type '");
                        print_type_inline(&first);
                        printf("'\n");
                        exit(1);
                    }

                    invoke->data.operator_.computed_operand_type = first;
                    stack_type_push(&state->stack, first);
                } else if (operator == Operator_Equal ||
                        operator == Operator_NotEqual ||
                        operator == Operator_Greater ||
                        operator == Operator_GreaterEqual ||
                        operator == Operator_Less ||
                        operator == Operator_LessEqual) {
                    Ast_Type first = stack_type_pop(&state->stack);
                    Ast_Type second = stack_type_pop(&state->stack);

                    if (!is_type(&first, &second, state)) {
                        print_error_stub(&invoke->location);
                        printf("Type '");
                        print_type_inline(&second);
                        printf("' cannot be operated on with type '");
                        print_type_inline(&first);
                        printf("'\n");
                        exit(1);
                    }

                    invoke->data.operator_.computed_operand_type = first;

                    stack_type_push(&state->stack, create_internal_type(Type_Bool));
                } else if (operator == Operator_Not) {
                    Ast_Type input = stack_type_pop(&state->stack);

                    Ast_Type bool_type = create_internal_type(Type_Bool);
                    if (!is_type(&bool_type, &input, state)) {
                        print_error_stub(&invoke->location);
                        printf("Type '");
                        print_type_inline(&input);
                        printf("' is not a boolean\n");
                        exit(1);
                    }

                    stack_type_push(&state->stack, create_internal_type(Type_Bool));
                } else if (operator == Operator_And ||
                        operator == Operator_Or) {
                    Ast_Type first = stack_type_pop(&state->stack);
                    Ast_Type second = stack_type_pop(&state->stack);

                    if (!is_internal_type(Type_Bool, &first) || !is_internal_type(Type_Bool, &second)) {
                        print_error_stub(&invoke->location);
                        printf("Type '");
                        print_type_inline(&first);
                        printf("' and '");
                        print_type_inline(&second);
                        printf("'cannot be operated on here\n");
                        exit(1);
                    }

                    invoke->data.operator_.computed_operand_type = first;

                    stack_type_push(&state->stack, create_internal_type(Type_Bool));
                } else {
                    assert(false);
                }
            }
            break;
        }
        case Expression_RunMacro: {
            Ast_RunMacro* run_macro = &expression->data.run_macro;
            process_run_macro(run_macro, Macro_Expression, state);
            break;
        }
        case Expression_Retrieve: {
            Ast_Expression_Retrieve* retrieve = &expression->data.retrieve;
            bool found = false;

            if (!found && retrieve->kind == Retrieve_Assign_Identifier) {
                if (strcmp(retrieve->data.identifier.name, "@file") == 0) {
                    stack_type_push(&state->stack, create_pointer_type(create_array_type(create_internal_type(Type_Byte))));
                    found = true;
                } else if (strcmp(retrieve->data.identifier.name, "@line") == 0) {
                    stack_type_push(&state->stack, create_internal_type(Type_UInt));
                    found = true;
                }
            }

            if (!found && retrieve->kind == Retrieve_Assign_Array) {
                bool in_reference = consume_in_reference(&state->generic);
                process_expression(retrieve->data.array.expression_outer, state);
                Ast_Type array_type = stack_type_pop(&state->stack);

                Ast_Type* array_ast_type_raw;
                if (array_type.kind == Type_Pointer) {
                    array_ast_type_raw = array_type.data.pointer.child;
                } else {
                    array_ast_type_raw = &array_type;
                }

                retrieve->data.array.computed_array_type = array_type;

                state->wanted_type = usize_type();
                process_expression(retrieve->data.array.expression_inner, state);
                Ast_Type array_index_type = stack_type_pop(&state->stack);
                if (!is_internal_type(Type_UInt, &array_index_type)) {
                    print_error_stub(&retrieve->location);
                    printf("Type '");
                    print_type_inline(&array_index_type);
                    printf("' cannot be used to access array\n");
                    exit(1);
                }

                Ast_Type resulting_type = *array_ast_type_raw->data.array.element_type;
                if (in_reference) {
                    resulting_type = create_pointer_type(resulting_type);
                }
                stack_type_push(&state->stack, resulting_type);
                found = true;
            }

            if (!found && retrieve->kind == Retrieve_Assign_Identifier) {
                Ast_Type* variable_type = get_local_variable_type(state, retrieve->data.identifier.name);

                if (variable_type != NULL) {
                    found = true;
                    if (consume_in_reference(&state->generic)) {
                        stack_type_push(&state->stack, create_pointer_type(*variable_type));
                    } else {
                        stack_type_push(&state->stack, *variable_type);
                    }
                }
            }

            if (!found && retrieve->kind == Retrieve_Assign_Identifier) {
                Ast_Type type = { .directives = array_ast_directive_new(1) };
                for (size_t i = 0; i < state->generic.current_arguments.count; i++) {
                    Ast_Declaration* declaration = &state->generic.current_arguments.elements[state->generic.current_arguments.count - i - 1];
                    if (strcmp(declaration->name, retrieve->data.identifier.name) == 0) {
                        type = declaration->type;
                        found = true;
                        break;
                    }
                }

                if (found) {
                    if (consume_in_reference(&state->generic)) {
                        type = create_pointer_type(type);
                    }

                    stack_type_push(&state->stack, type);
                }
            }

            if (!found && retrieve->kind == Retrieve_Assign_Parent) {
                bool in_reference = consume_in_reference(&state->generic);
                process_expression(retrieve->data.parent.expression, state);
                Ast_Type parent_type = stack_type_pop(&state->stack);

                Ast_Type* parent_type_raw;
                if (parent_type.kind == Type_Pointer) {
                    parent_type_raw = parent_type.data.pointer.child;
                } else {
                    parent_type_raw = &parent_type;
                    retrieve->data.parent.needs_reference = true;
                }

                retrieve->data.parent.computed_parent_type = *parent_type_raw;

                Ast_Type item_type;
                Ast_Type result_type = get_parent_item_type(parent_type_raw, retrieve->data.parent.name, &state->generic);
                if (result_type.kind != Type_None) {
                    item_type = result_type;
                    found = true;
                }

                if (in_reference) {
                    item_type = create_pointer_type(item_type);
                }

                stack_type_push(&state->stack, item_type);
            }

            if (!found && retrieve->kind == Retrieve_Assign_Identifier && state->wanted_type != NULL) {
                Ast_Type evaluated_wanted_type = evaluate_type_complete(state->wanted_type, &state->generic);
                if (evaluated_wanted_type.kind == Type_Enum) {
                    char* enum_variant = retrieve->data.identifier.name;

                    Ast_Type_Enum* enum_type = &evaluated_wanted_type.data.enum_;
                    for (size_t i = 0; i < enum_type->items.count; i++) {
                        if (strcmp(enum_variant, enum_type->items.elements[i]) == 0) {
                            Ast_Type* evaluated_wanted_type_allocated = malloc(sizeof(*evaluated_wanted_type_allocated));
                            *evaluated_wanted_type_allocated = evaluated_wanted_type;
                            retrieve->computed_result_type = evaluated_wanted_type_allocated;
                            stack_type_push(&state->stack, *state->wanted_type);
                            found = true;
                        }
                    }
                }
            }

            if (!found && retrieve->kind == Retrieve_Assign_Identifier) {
                Resolved resolved = resolve(&state->generic, retrieve->data.identifier);
                switch (resolved.kind) {
                    case Resolved_Item: {
                        Ast_Item* item = resolved.data.item;
                        process_item_reference(item, state);

                        found = true;
                        switch (item->kind) {
                            case Item_Procedure: {
                                Ast_Item_Procedure* procedure = &item->data.procedure;

                                Ast_Type type = { .directives = array_ast_directive_new(1) };
                                Ast_Type_Procedure procedure_type;
                                procedure_type.arguments = array_ast_type_new(4);
                                procedure_type.returns = array_ast_type_new(4);

                                for (size_t i = 0; i < procedure->arguments.count; i++) {
                                    array_ast_type_append(&procedure_type.arguments, &procedure->arguments.elements[i].type);
                                }

                                for (size_t i = 0; i < procedure->returns.count; i++) {
                                    array_ast_type_append(&procedure_type.returns, procedure->returns.elements[i]);
                                }

                                type.kind = Type_Procedure;
                                type.data.procedure = procedure_type;
                                stack_type_push(&state->stack, create_pointer_type(type));
                                break;
                            }
                            case Item_Global: {
                                Ast_Item_Global* global = &item->data.global;

                                if (consume_in_reference(&state->generic)) {
                                    stack_type_push(&state->stack, create_pointer_type(global->type));
                                } else {
                                    stack_type_push(&state->stack, global->type);
                                }
                                break;
                            }
                            case Item_Constant: {
                                Ast_Type* wanted_type = state->wanted_type;
                                if (wanted_type == NULL || !is_number_type(wanted_type)) {
                                    wanted_type = usize_type();
                                }

                                retrieve->computed_result_type = wanted_type;

                                stack_type_push(&state->stack, *wanted_type);
                                break;
                            }
                            default:
                                assert(false);
                        }
                        break;
                    }
                    case Unresolved:
                        break;
                    default:
                        assert(false);
                }
            }

            if (!found) {
                print_error_stub(&retrieve->location);
                printf("Retrieve not found\n");
                exit(1);
            }

            break;
        }
        case Expression_If: {
            Ast_Expression_If* node = &expression->data.if_;
            process_expression(node->condition, state);

            if (state->stack.count == 0) {
                print_error_stub(&node->location);
                printf("Ran out of values for if\n");
                exit(1);
            }

            Ast_Type given = stack_type_pop(&state->stack);
            Ast_Type bool_type = create_internal_type(Type_Bool);
            if (!is_type(&bool_type, &given, state)) {
                print_error_stub(&node->location);
                printf("Type '");
                print_type_inline(&given);
                printf("' is not a boolean\n");
                exit(1);
            }

            size_t stack_pointer_start = state->stack.count;
            process_expression(node->if_expression, state);
            size_t stack_pointer_middle = state->stack.count;

            if (node->else_expression == NULL) {
                if (stack_pointer_middle - stack_pointer_start > 0) {
                    print_error_stub(&node->location);
                    printf("If expression resulting in value(s) must have an else expression\n");
                    exit(1);
                }
            } else {
                process_expression(node->else_expression, state);
                size_t stack_pointer_end = state->stack.count;

                bool matches = true;
                if (stack_pointer_end - stack_pointer_middle != stack_pointer_middle - stack_pointer_start) matches = false;

                for (size_t i = 0; i < stack_pointer_middle - stack_pointer_start; i++) {
                    if (!is_type(&state->stack.elements[stack_pointer_start + i], &state->stack.elements[stack_pointer_middle + i], state)) matches = false;
                }
                
                if (!matches) {
                    print_error_stub(&node->location);
                    printf("If expression resulting in value(s) must result in identical values no matter the case\n");
                    exit(1);
                }

                state->stack.count = stack_pointer_middle;
            }
            break;
        }
        case Expression_Number: {
            Ast_Expression_Number* number = &expression->data.number;
            Ast_Type* wanted = state->wanted_type;

            if (wanted != NULL && is_number_type(wanted)) {
                stack_type_push(&state->stack, *wanted);
                number->type = wanted;
            } else {
                Ast_Type* usize = malloc(sizeof(*usize));
                *usize = create_internal_type(Type_UInt);
                stack_type_push(&state->stack, *usize);
                number->type = usize;
            }
            break;
        }
        case Expression_Null: {
            Ast_Expression_Null* null = &expression->data.null;
            Ast_Type* wanted = state->wanted_type;

            if (wanted != NULL && is_pointer_type(wanted)) {
                stack_type_push(&state->stack, *wanted);
                null->type = wanted;
            } else {
                Ast_Type* usize = malloc(sizeof(*usize));
                *usize = create_internal_type(Type_UInt);
                stack_type_push(&state->stack, *usize);
                null->type = usize;
            }
            break;
        }
        case Expression_Boolean: {
            stack_type_push(&state->stack, create_internal_type(Type_Bool));
            break;
        }
        case Expression_String: {
            stack_type_push(&state->stack, create_pointer_type(create_array_type(create_internal_type(Type_Byte))));
            break;
        }
        case Expression_Char: {
            stack_type_push(&state->stack, create_internal_type(Type_Byte));
            break;
        }
        case Expression_Reference: {
            Ast_Expression_Reference* reference = &expression->data.reference;
            state->generic.in_reference = true;

            process_expression(reference->inner, state);
            break;
        }
        case Expression_Cast: {
            Ast_Expression_Cast* cast = &expression->data.cast;

            process_expression(cast->expression, state);
            Ast_Type input = stack_type_pop(&state->stack);

            bool is_valid = false;
            if (cast->type.kind == Type_Internal && input.kind == Type_Internal) {
                Ast_Type_Internal output_internal = cast->type.data.internal;
                Ast_Type_Internal input_internal = input.data.internal;

                if ((input_internal == Type_UInt || input_internal == Type_UInt64 || input_internal == Type_UInt32 || input_internal == Type_UInt16 || input_internal == Type_UInt8) &&
                        (output_internal == Type_UInt || output_internal == Type_UInt64 || output_internal == Type_UInt32 || output_internal == Type_UInt16 || output_internal == Type_UInt8)) {
                    is_valid = true;
                } else if (input_internal == Type_Float64 && output_internal == Type_UInt64) {
                    is_valid = true;
                }
            }

            if (input.kind == Type_Internal && input.data.internal == Type_Ptr && cast->type.kind == Type_Pointer) {
                is_valid = true;
            }

            if (cast->type.kind == Type_Internal && cast->type.data.internal == Type_Ptr && input.kind == Type_Pointer) {
                is_valid = true;
            }

            if (cast->type.kind == Type_Internal && cast->type.data.internal == Type_Byte && input.kind == Type_Internal && input.data.internal == Type_UInt8) {
                is_valid = true;
            }

            if (cast->type.kind == Type_Internal && cast->type.data.internal == Type_UInt8 && input.kind == Type_Internal && input.data.internal == Type_Byte) {
                is_valid = true;
            }

            if (!is_valid) {
                print_error_stub(&cast->location);
                printf("Type '");
                print_type_inline(&input);
                printf("' cannot be cast to type '");
                print_type_inline(&cast->type);
                printf("'\n");
                exit(1);
            }

            cast->computed_input_type = input;

            stack_type_append(&state->stack, cast->type);
            break;
        }
        case Expression_Init: {
            Ast_Expression_Init* init = &expression->data.init;
            Ast_Type* type = &init->type;
            process_type(type, state);
            stack_type_append(&state->stack, *type);
            break;
        }
        case Expression_Build: {
            Ast_Expression_Build* build = &expression->data.build;

            Ast_Type* type = &build->type;
            process_type(type, state);
            process_build_type(build, type, state);

            stack_type_append(&state->stack, *type);
            break;
        }
        case Expression_SizeOf: {
            Ast_Expression_SizeOf* size_of = &expression->data.size_of;
            Ast_Type* wanted_type = state->wanted_type;
            if (wanted_type == NULL || !is_number_type(wanted_type)) {
                wanted_type = usize_type();
            }

            process_type(&size_of->type, state);

            size_of->computed_result_type = *wanted_type;
            stack_type_append(&state->stack, *wanted_type);
            break;
        }
        case Expression_LengthOf: {
            Ast_Expression_LengthOf* length_of = &expression->data.length_of;
            Ast_Type* wanted_type = state->wanted_type;
            if (wanted_type == NULL || !is_number_type(wanted_type)) {
                wanted_type = usize_type();
            }

            process_type(&length_of->type, state);

            length_of->computed_result_type = *wanted_type;
            stack_type_append(&state->stack, *wanted_type);
            break;
        }
        default:
            assert(false);
    }
}

bool has_directive(Array_Ast_Directive* directives, Directive_Kind kind) {
    for (size_t i = 0; i < directives->count; i++) {
        if (directives->elements[i].kind == kind) {
            return true;
        }
    }
    return false;
}

Ast_Directive* get_directive(Array_Ast_Directive* directives, Directive_Kind kind) {
    for (size_t i = 0; i < directives->count; i++) {
        if (directives->elements[i].kind == kind) {
            return &directives->elements[i];
        }
    }
    return false;
}

void process_item_reference(Ast_Item* item, Process_State* state) {
    switch (item->kind) {
        case Item_Procedure: {
            Ast_Item_Procedure* procedure = &item->data.procedure;
            for (size_t i = 0; i < procedure->arguments.count; i++) {
                process_type(&procedure->arguments.elements[i].type, state);
            }

            for (size_t i = 0; i < procedure->returns.count; i++) {
                process_type(procedure->returns.elements[i], state);
            }
            break;
        }
        case Item_Type: {
            process_type(&item->data.type.type, state);
            break;
        }
        case Item_Macro:
        case Item_Global:
        case Item_Constant:
            break;
    }
}

bool has_implicit_return(Ast_Expression* expression) {
    if (expression->kind == Expression_Block) {
        return expression->data.block.statements.count == 0 || expression->data.block.statements.elements[expression->data.block.statements.count - 1]->kind != Statement_Return;
    }
    return true;
}

void process_item(Ast_Item* item, Process_State* state) {
    if (has_directive(&item->directives, Directive_If)) {
        Ast_Directive_If* if_node = &get_directive(&item->directives, Directive_If)->data.if_;
        if_node->result = evaluate_if_directive(if_node, state);
        if (!if_node->result) {
            return;
        }
    }

    switch (item->kind) {
        case Item_Procedure: {
            Ast_Item_Procedure* procedure = &item->data.procedure;
            state->generic.current_procedure = procedure;
            state->generic.current_declares = array_ast_declaration_new(4);
            state->generic.current_arguments = procedure->arguments;
            state->generic.current_returns = procedure->returns;

            for (size_t i = 0; i < procedure->arguments.count; i++) {
                process_type(&procedure->arguments.elements[i].type, state);
            }

            process_expression(procedure->body, state);

            procedure->has_implicit_return = has_implicit_return(procedure->body);
            if (procedure->has_implicit_return) {
                check_return(state, &procedure->end_location);
            }
            break;
        }
        case Item_Type: {
            process_type(&item->data.type.type, state);
            break;
        }
        case Item_Macro: {
            break;
        }
        case Item_Global: {
            break;
        }
        case Item_Constant:
            break;
        default:
            assert(false);
    }
}

bool is_enum_type(Ast_Type* type, Generic_State* generic_state) {
    if (type->kind == Type_Basic) {
        Resolved resolved = resolve(generic_state, type->data.basic.identifier);
        if (resolved.kind == Resolved_Item && resolved.data.item->kind == Item_Type) {
            return is_enum_type(&resolved.data.item->data.type.type, generic_state);
        }
    } else if (type->kind == Type_Enum) {
        return true;
    }
    return false;
}

void process(Program* program) {
    Process_State state = (Process_State) {
        .generic = (Generic_State) {
            .program = program,
            // should items just be able to store their file?
            .current_file = NULL,
            .current_arguments = {},
            .current_declares = {},
            .current_returns = {},
            .in_reference = false,
        },
        .stack = stack_type_new(8),
        .wanted_type = NULL,
        .scoped_declares = array_size_new(8),
    };

    for (size_t j = 0; j < program->count; j++) {
        Ast_File* file_node = &program->elements[j];
        state.generic.current_file = file_node;

        for (size_t i = 0; i < file_node->items.count; i++) {
            Ast_Item* item = &file_node->items.elements[i];
            process_item(item, &state);
        }
    }
}
