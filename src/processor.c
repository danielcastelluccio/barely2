#include <assert.h>
#include <stdio.h>

#include "file_util.h"
#include "processor.h"

Dynamic_Array_Impl(Type, Stack_Type, stack_type_)
Dynamic_Array_Impl(size_t, Array_Size, array_size_)

void stack_type_push(Stack_Type* stack, Type type) {
    stack_type_append(stack, type);
}

Type stack_type_pop(Stack_Type* stack) {
    Type result = stack->elements[stack->count - 1];
    stack->count--;
    return result;
}

typedef struct {
    Generic_State generic;
    Stack_Type stack;
    Item_Node* current_procedure;
    Array_Declaration current_declares;
    Array_Size scoped_declares;
    Array_Declaration current_arguments;
    Array_Type current_returns;
    Expression_Node* current_body;
    bool in_reference;
    Type* wanted_type;
} Process_State;

bool is_number_type(Type* type) {
    if (type->kind == Type_Internal) {
        Internal_Type internal = type->data.internal;

        if (internal == Type_USize || internal == Type_U64 || internal == Type_U32 || internal == Type_U86 || internal == Type_U8 || internal == Type_F8) {
            return true;
        }
    }

    return false;
}

bool uses(File_Node* checked, File_Node* tested, Generic_State* state) {
    char* checked_path = checked->path;
    size_t checked_path_len = strlen(checked_path);
    size_t last_slash = 0;
    for (size_t i = 0; i < checked_path_len; i++) {
        if (checked_path[i] == '/') {
            last_slash = i;
        }
    }

    for (size_t i = 0; i < checked->items.count; i++) {
        Item_Node* item = &checked->items.elements[i];
        if (item->kind == Item_Use) {
            char* path = item->data.use.path;
            char* relative_path;

            if (item->data.use.package != NULL) {
                char* package_path = NULL;
                for (size_t j = 0; j < state->package_names->count; j++) {
                    if (strcmp(state->package_names->elements[j], item->data.use.package) == 0) {
                        package_path = state->package_paths->elements[j];
                    }
                }

                if (package_path == NULL) {
                    return false;
                }

                relative_path = concatenate_folder_file_path(package_path, item->data.use.path);
            } else {
                char* current_folder = string_substring(checked_path, 0, last_slash);
                relative_path = concatenate_folder_file_path(current_folder, path);
            }

            char absolute_path[128] = {};
            realpath(relative_path, absolute_path);

            if (strcmp(tested->path, absolute_path) == 0) {
                return true;
            }
        }
    }
    return false;
}

Resolved resolve(Generic_State* state, Identifier data) {
    Identifier initial_search = {};
    if (data.kind == Identifier_Single) {
        initial_search = data;
    } else if (data.kind == Identifier_Multi) {
        initial_search.kind = Identifier_Single;
        initial_search.data.single = data.data.multi.elements[0];
    }

    Resolved result = { NULL, Unresolved, {} };
    for (size_t j = 0; j < state->program->count; j++) {
        File_Node* file_node = &state->program->elements[j];
        bool stop = false;
        if (uses(state->current_file, file_node, state) || state->current_file == file_node) {
            for (size_t i = 0; i < file_node->items.count; i++) {
                Item_Node* item = &file_node->items.elements[i];
                if (initial_search.kind == Identifier_Single && strcmp(item->name, initial_search.data.single) == 0) {
                    result = (Resolved) { file_node, Resolved_Item, { .item = item } };
                    stop = true;
                    break;
                }
            }
        }

        if (stop) {
            break;
        }
    }

    if (data.kind == Identifier_Multi) {
        if (strcmp(data.data.multi.elements[0], "") == 0) {
            result = (Resolved) { result.file, Resolved_Enum_Variant, { .enum_ = { .enum_ = NULL, .variant = data.data.multi.elements[1]} } };
        } else {
            size_t identifier_index = 1;
            if (result.kind == Resolved_Item && result.data.item->kind == Item_Type && result.data.item->data.type.type.kind == Type_Enum) {
                char* wanted_name = data.data.multi.elements[identifier_index];
                result = (Resolved) { result.file, Resolved_Enum_Variant, { .enum_ = { .enum_ = &result.data.item->data.type.type.data.enum_, .variant = wanted_name} } };
            }
        }
    }

    return result;
}

Type create_basic_single_type(char* name) {
    Type type = { .directives = array_directive_new(1) };
    Basic_Type basic = {};

    basic.kind = Type_Single;
    basic.data.single = name;

    type.kind = Type_Basic;
    type.data.basic = basic;

    return type;
}

Type create_internal_type(Internal_Type internal_type) {
    Type type = { .directives = array_directive_new(1) };

    type.kind = Type_Internal;
    type.data.internal = internal_type;

    return type;
}

Type create_array_type(Type child) {
    Type type = { .directives = array_directive_new(1) };
    BArray_Type array = {};

    Type* child_allocated = malloc(sizeof(Type));
    *child_allocated = child;
    array.element_type = child_allocated;

    type.kind = Type_Array;
    type.data.array = array;

    return type;
}

Type create_pointer_type(Type child) {
    Type type = { .directives = array_directive_new(1) };
    Pointer_Type pointer;

    Type* child_allocated = malloc(sizeof(Type));
    *child_allocated = child;
    pointer.child = child_allocated;

    type.kind = Type_Pointer;
    type.data.pointer = pointer;

    return type;
}

bool is_type(Type* wanted, Type* given) {
    if (wanted->kind == Type_RegisterSize || given->kind == Type_RegisterSize) {
        Type* to_check;
        if (wanted->kind == Type_RegisterSize) to_check = given;
        if (given->kind == Type_RegisterSize) to_check = wanted;

        if (to_check->kind == Type_Pointer) return true;
        if (to_check->kind == Type_Internal && (to_check->data.internal == Type_USize || to_check->data.internal == Type_Ptr)) return true;

        return false;
    }

    if (wanted->kind != given->kind) {
        return false;
    }

    if (wanted->kind == Type_Pointer) {
        return is_type(wanted->data.pointer.child, given->data.pointer.child);
    }

    if (wanted->kind == Type_Basic) {
        if (wanted->data.basic.kind == Type_Single && given->data.basic.kind == Type_Single) {
            return strcmp(wanted->data.basic.data.single, given->data.basic.data.single) == 0;
        }
    }

    if (wanted->kind == Type_Array) {
        if (wanted->data.array.has_size) {
            if (!given->data.array.has_size) return false;
            if (!is_type(wanted->data.array.size_type, given->data.array.size_type)) return false;
        }
        return is_type(wanted->data.array.element_type, given->data.array.element_type);
    }

    if (wanted->kind == Type_Internal) {
        return wanted->data.internal == given->data.internal;
    }

    if (wanted->kind == Type_Procedure) {
        Procedure_Type* wanted_proc = &wanted->data.procedure;
        Procedure_Type* given_proc = &given->data.procedure;

        bool is_valid = true;
        if (wanted_proc->arguments.count != given_proc->arguments.count) is_valid = false;
        if (wanted_proc->returns.count != given_proc->returns.count) is_valid = false;

        if (is_valid) {
            for (size_t i = 0; i < wanted_proc->arguments.count; i++) {
                if (!is_type(array_type_get(&wanted_proc->arguments, i), given_proc->arguments.elements[i])) {
                    is_valid = false;
                }
            }
        }

        if (is_valid) {
            for (size_t i = 0; i < wanted_proc->returns.count; i++) {
                if (!is_type(wanted_proc->returns.elements[i], given_proc->returns.elements[i])) {
                    is_valid = false;
                }
            }
        }

        return is_valid;
    }

    if (wanted->kind == Type_Enum) {
        Enum_Type* wanted_enum = &wanted->data.enum_;
        Enum_Type* given_enum = &given->data.enum_;

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

    if (wanted->kind == Type_Number) {
        if (wanted->data.number.value == given->data.number.value) return true;
    }

    return false;
}

bool is_internal_type(Internal_Type wanted, Type* given) {
    return given->kind == Type_Internal && given->data.internal == wanted;
}

void print_type_inline(Type* type) {
    switch (type->kind) {
        case Type_Basic: {
            Basic_Type* basic = &type->data.basic;

            if (basic->kind == Type_Single) {
                printf("%s", basic->data.single);
            } else {
                for (size_t i = 0; i < basic->data.multi.count; i++) {
                    printf("%s", basic->data.multi.elements[i]);
                    if (i < basic->data.multi.count - 1) {
                        printf("::");
                    }
                }
            }
            break;
        }
        case Type_Pointer: {
            Pointer_Type* pointer = &type->data.pointer;
            printf("*");
            print_type_inline(pointer->child);
            break;
        }
        case Type_Array: {
            BArray_Type* array = &type->data.array;
            printf("[");
            if (array->has_size) {
                print_type_inline(array->size_type);
            }
            printf("]");
            print_type_inline(array->element_type);
            break;
        }
        case Type_Procedure: {
            Procedure_Type* procedure = &type->data.procedure;
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
            Internal_Type* internal = &type->data.internal;

            switch (*internal) {
                case Type_USize:
                    printf("usize");
                    break;
                case Type_U64:
                    printf("u64");
                    break;
                case Type_U32:
                    printf("u32");
                    break;
                case Type_U86:
                    printf("u16");
                    break;
                case Type_U8:
                    printf("u8");
                    break;
                case Type_F8:
                    printf("f64");
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
            Array_Declaration_Pointer* items;
            if (type->kind == Type_Struct) {
                Struct_Type* struct_type = &type->data.struct_;
                items = &struct_type->items;
                printf("struct { ");
            } else if (type->kind == Type_Union) {
                Union_Type* union_type = &type->data.union_;
                items = &union_type->items;
                printf("union { ");
            }
            for (size_t i = 0; i < items->count; i++) {
                Declaration* declaration = items->elements[i];
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
            Enum_Type* enum_type = &type->data.enum_;
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
        default:
            printf("%i\n", type->kind);
            assert(false);
    }
}

void print_error_stub(Location* location) {
    printf("%s:%zu:%zu: ", location->file, location->row, location->col);
}

bool consume_in_reference(Process_State* state) {
    bool cached = state->in_reference;
    state->in_reference = false;
    return cached;
}

Identifier basic_type_to_identifier(Basic_Type type) {
    Identifier identifier;
    if (type.kind == Type_Single) {
        identifier.data.single = type.data.single;
        identifier.kind = Identifier_Single;
    } else {
        identifier.data.multi = type.data.multi;
        identifier.kind = Identifier_Multi;
    }
    return identifier;
}

Basic_Type identifier_to_basic_type(Identifier identifier) {
    Basic_Type basic;
    if (identifier.kind == Identifier_Single) {
        basic.data.single = identifier.data.single;
        basic.kind = Type_Single;
    } else {
        basic.data.multi = identifier.data.multi;
        basic.kind = Type_Multi;
    }
    return basic;
}

static Type* _usize_type;

Type* usize_type() {
    if (_usize_type == NULL) {
        Type* usize = malloc(sizeof(Type));
        *usize = create_internal_type(Type_USize);
        _usize_type = usize;
    }
    return _usize_type;
}

void process_expression(Expression_Node* expression, Process_State* state);

Type* get_parent_item_type(Type* parent_type, char* item_name, Generic_State* state) {
    Type* result = NULL;

    if (strcmp(item_name, "*") == 0) {
        result = parent_type;
    } else {
        switch (parent_type->kind) {
            case Type_Basic: {
                Identifier identifier = basic_type_to_identifier(parent_type->data.basic);

                Resolved resolved = resolve(state, identifier);
                switch (resolved.kind) {
                    case Resolved_Item: {
                        Item_Node* item = resolved.data.item;
                        assert(item->kind == Item_Type);
                        return get_parent_item_type(&item->data.type.type, item_name, state);
                    }
                    case Resolved_Enum_Variant: {
                        assert(false);
                    }
                    case Unresolved:
                        assert(false);
                }
                break;
            }
            case Type_Struct:
            case Type_Union: {
                Array_Declaration_Pointer* items;
                if (parent_type->kind == Type_Struct) {
                    Struct_Type* struct_ = &parent_type->data.struct_;
                    items = &struct_->items;
                } else if (parent_type->kind == Type_Union) {
                    Union_Type* union_ = &parent_type->data.union_;
                    items = &union_->items;
                } else {
                    assert(false);
                }

                for (size_t i = 0; i < items->count; i++) {
                    Declaration* declaration = items->elements[i];
                    if (strcmp(declaration->name, item_name) == 0) {
                        result = &declaration->type;
                    }
                }
                break;
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

Evaluation_Value evaluate_if_directive_expression(Expression_Node* expression) {
    switch (expression->kind) {
        case Expression_Retrieve: {
            if (expression->data.retrieve.kind == Retrieve_Assign_Identifier && expression->data.retrieve.data.identifier.kind == Identifier_Single) {
                char* identifier = expression->data.retrieve.data.identifier.data.single;

                if (strcmp(identifier, "@os")) {
                    return (Evaluation_Value) { .kind = Evaluation_Operating_System, .data = { .operating_system = Operating_System_Linux } };
                } else if (strcmp(identifier, "@linux")) {
                    return (Evaluation_Value) { .kind = Evaluation_Operating_System, .data = { .operating_system = Operating_System_Linux } };
                }
            }
            break;
        }
        case Expression_Invoke: {
            if (expression->data.invoke.kind == Invoke_Operator) {
                Operator* operator = &expression->data.invoke.data.operator_.operator_;
                Evaluation_Value left = evaluate_if_directive_expression(expression->data.invoke.arguments.elements[0]);
                Evaluation_Value right = evaluate_if_directive_expression(expression->data.invoke.arguments.elements[1]);

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
        default:
            break;
    }
    assert(false);
}

bool evaluate_if_directive(Directive_If_Node* if_node) {
    Evaluation_Value value = evaluate_if_directive_expression(if_node->expression);
    assert(value.kind == Evaluation_Boolean);
    return value.data.boolean;
}

void process_assign(Statement_Assign_Node* assign, Process_State* state) {
    Array_Type wanted_types = array_type_new(1);
    for (size_t i = 0; i < assign->parts.count; i++) {
        Statement_Assign_Part* assign_part = &assign->parts.elements[i];
        Type* wanted_type = malloc(sizeof(Type));
        bool found = false;

        if (assign_part->kind == Retrieve_Assign_Array) {
            process_expression(assign_part->data.array.expression_outer, state);
            Type array_type = stack_type_pop(&state->stack);

            Type* array_type_raw;
            if (array_type.kind == Type_Pointer) {
                array_type_raw = array_type.data.pointer.child;
            } else {
                array_type_raw = &array_type;
            }

            if (array_type_raw->kind != Type_Array) {
                print_error_stub(&assign_part->location);
                printf("Type '");
                print_type_inline(array_type_raw);
                printf("' is not an array\n");
                exit(1);
            }

            wanted_type = array_type_raw->data.array.element_type;
            found = true;
        }

        if (assign_part->kind == Retrieve_Assign_Identifier && assign_part->data.identifier.kind == Identifier_Single) {
            char* name = assign_part->data.identifier.data.single;

            Type* type = malloc(sizeof(Type));
            for (size_t j = 0; j < state->current_declares.count; j++) {
                if (strcmp(state->current_declares.elements[j].name, name) == 0) {
                    *type = state->current_declares.elements[j].type;
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
            Type parent_type = stack_type_pop(&state->stack);

            assign_part->data.parent.computed_parent_type = parent_type;

            Type* parent_type_raw;
            if (parent_type.kind == Type_Pointer) {
                parent_type_raw = parent_type.data.pointer.child;
            } else {
                parent_type_raw = &parent_type;
            }

            Type* resolved = get_parent_item_type(parent_type_raw, assign_part->data.parent.name, &state->generic);
            if (resolved != NULL) {
                *wanted_type = *resolved;
                found = true;
            }
        }

        if (!found) {
            print_error_stub(&assign_part->location);
            printf("Assign not found\n");
            exit(1);
        }

        array_type_append(&wanted_types, wanted_type);
    }

    if (assign->expression->kind == Expression_Multi) {
        size_t assign_index = 0;
        for (size_t i = 0; i < assign->expression->data.multi.expressions.count; i++) {
            size_t stack_start = state->stack.count;
            state->wanted_type = wanted_types.elements[assign_index];
            process_expression(assign->expression->data.multi.expressions.elements[i], state);
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
            Type array_type = stack_type_pop(&state->stack);

            Type* element_type = wanted_types.elements[assign->parts.count - i - 1];

            assign_part->data.array.computed_array_type = array_type;

            state->wanted_type = usize_type();

            process_expression(assign_part->data.array.expression_inner, state);

            Type array_index_type = stack_type_pop(&state->stack);
            if (!is_internal_type(Type_USize, &array_index_type)) {
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

            Type right_side_given_type = stack_type_pop(&state->stack);
            if (!is_type(element_type, &right_side_given_type)) {
                print_error_stub(&assign_part->location);
                printf("Type '");
                print_type_inline(&right_side_given_type);
                printf("' is not assignable to index of array of type '");
                print_type_inline(element_type);
                printf("'\n");
                exit(1);
            }
        }

        if (assign_part->kind == Retrieve_Assign_Identifier && assign_part->data.identifier.kind == Identifier_Single) {
            Type* variable_type = wanted_types.elements[assign->parts.count - i - 1];

            if (state->stack.count == 0) {
                print_error_stub(&assign_part->location);
                printf("Ran out of values for declaration assignment\n");
                exit(1);
            }

            Type popped = stack_type_pop(&state->stack);
            if (!is_type(variable_type, &popped)) {
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
            Type* item_type = wanted_types.elements[assign->parts.count - i - 1];

            if (state->stack.count == 0) {
                print_error_stub(&assign_part->location);
                printf("Ran out of values for item assignment\n");
                exit(1);
            }

            Type right_side_given_type = stack_type_pop(&state->stack);
            if (!is_type(item_type, &right_side_given_type)) {
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

void process_statement(Statement_Node* statement, Process_State* state) {
    if (has_directive(&statement->directives, Directive_If)) {
        Directive_If_Node* if_node = &get_directive(&statement->directives, Directive_If)->data.if_;
        if_node->result = evaluate_if_directive(if_node);
        if (!if_node->result) {
            return;
        }
    }

    switch (statement->kind) {
        case Statement_Expression: {
            Statement_Expression_Node* statement_expression = &statement->data.expression;
            process_expression(statement_expression->expression, state);
            break;
        }
        case Statement_Declare: {
            Statement_Declare_Node* declare = &statement->data.declare;
            if (declare->expression != NULL) {
                if (declare->expression->kind == Expression_Multi) {
                    size_t declare_index = 0;
                    for (size_t i = 0; i < declare->expression->data.multi.expressions.count; i++) {
                        size_t stack_start = state->stack.count;
                        state->wanted_type = &declare->declarations.elements[declare_index].type;
                        process_expression(declare->expression->data.multi.expressions.elements[i], state);
                        declare_index += state->stack.count - stack_start;
                    }
                } else {
                    state->wanted_type = &declare->declarations.elements[declare->declarations.count - 1].type;
                    process_expression(declare->expression, state);
                }

                for (int i = declare->declarations.count - 1; i >= 0; i--) {
                    Declaration* declaration = &declare->declarations.elements[i];

                    if (state->stack.count == 0) {
                        print_error_stub(&declaration->location);
                        printf("Ran out of values for declaration assignment\n");
                        exit(1);
                    }

                    Type popped = stack_type_pop(&state->stack);
                    if (!is_type(&declaration->type, &popped)) {
                        print_error_stub(&declaration->location);
                        printf("Type '");
                        print_type_inline(&popped);
                        printf("' is not assignable to variable of type '");
                        print_type_inline(&declaration->type);
                        printf("'\n");
                        exit(1);
                    }
                    array_declaration_append(&state->current_declares, *declaration);
                }
            } else {
                for (int i = declare->declarations.count - 1; i >= 0; i--) {
                    Declaration* declaration = &declare->declarations.elements[i];
                    array_declaration_append(&state->current_declares, *declaration);
                }
            }
            break;
        }
        case Statement_Assign: {
            process_assign(&statement->data.assign, state);
            break;
        }
        case Statement_Return: {
            Statement_Return_Node* return_ = &statement->data.return_;
            if (return_->expression->kind == Expression_Multi) {
                size_t declare_index = 0;
                for (size_t i = 0; i < return_->expression->data.multi.expressions.count; i++) {
                    size_t stack_start = state->stack.count;
                    state->wanted_type = state->current_returns.elements[declare_index];
                    process_expression(return_->expression->data.multi.expressions.elements[i], state);
                    declare_index += state->stack.count - stack_start;
                }
            } else {
                state->wanted_type = state->current_returns.elements[state->current_returns.count - 1];
                process_expression(return_->expression, state);
            }

            for (int i = state->current_returns.count - 1; i >= 0; i--) {
                Type* return_type = state->current_returns.elements[i];

                if (state->stack.count == 0) {
                    print_error_stub(&return_->location);
                    printf("Ran out of values for return\n");
                    exit(1);
                }

                Type given_type = stack_type_pop(&state->stack);
                if (!is_type(return_type, &given_type)) {
                    print_error_stub(&return_->location);
                    printf("Type '");
                    print_type_inline(&given_type);
                    printf("' is not returnable of type '");
                    print_type_inline(return_type);
                    printf("'\n");
                    exit(1);
                }
            }
            break;
        }
        default:
            printf("Unhandled statement type %i!\n", statement->kind);
            exit(1);
    }

    if (state->stack.count > 0) {
        print_error_stub(&statement->statement_end_location);
        printf("Extra values at the end of statement\n");
        exit(1);
    }
}

bool is_register_sized(Type* type) {
    Type register_type = (Type) { .kind = Type_RegisterSize, .data = {} };
    return is_type(&register_type, type);
}

bool is_like_number_literal(Expression_Node* expression) {
    if (expression->kind == Expression_Number) return true;
    if (expression->kind == Expression_SizeOf) return true;
    return false;
}

Type* get_local_variable_type(Process_State* state, char* name) {
    for (int i = state->current_declares.count - 1; i >= 0; i--) {
        Declaration* declaration = &state->current_declares.elements[i];
        if (strcmp(declaration->name, name) == 0) {
            return &declaration->type;
        }
    }

    return NULL;
}

void process_type_expression(Type* type, Process_State* state) {
    switch (type->kind) {
        case Type_TypeOf: {
            TypeOf_Type* type_of = &type->data.type_of;
            Expression_Node* expression = type_of->expression;
            size_t stack_initial = state->stack.count;
            process_expression(expression, state);

            Type* type = malloc(sizeof(Type));
            *type = stack_type_pop(&state->stack);
            type_of->computed_result_type = type;

            assert(state->stack.count == stack_initial);
            break;
        }
        default:
            break;
    }
}

bool can_operate_together(Type* first, Type* second) {
    if (is_type(first, second)) {
        return true;
    }

    if (first->kind == Type_Internal && first->data.internal == Type_Ptr && second->kind == Type_Internal && second->data.internal == Type_USize) {
        return true;
    }

    return false;
}

void process_build_type(Build_Node* build, Type* type, Process_State* state) {
    switch (type->kind) {
        case Type_Basic: {
            Resolved resolved = resolve(&state->generic, basic_type_to_identifier(type->data.basic));
            switch (resolved.kind) {
                case Resolved_Item: {
                    Item_Node* item = resolved.data.item;
                    assert(item->kind == Item_Type);
                    process_build_type(build, &item->data.type.type, state);
                    break;
                }
                case Resolved_Enum_Variant: {
                    assert(false);
                }
                case Unresolved:
                    assert(false);
            }
            break;
        }
        case Type_Struct: {
            Struct_Type* struct_ = &type->data.struct_;
            if (build->arguments.count == struct_->items.count) {
                for (int i = build->arguments.count - 1; i >= 0; i--) {
                    Type* wanted_type = &struct_->items.elements[i]->type;
                    state->wanted_type = wanted_type;

                    process_expression(build->arguments.elements[i], state);

                    Type popped = stack_type_pop(&state->stack);
                    if (!is_type(wanted_type, &popped)) {
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
            BArray_Type* array = &type->data.array;
            assert(array->size_type->kind == Type_Number);
            size_t array_size = array->size_type->data.number.value;

            if (build->arguments.count == array_size) {
                for (int i = build->arguments.count - 1; i >= 0; i--) {
                    Type* wanted_type = array->element_type;
                    state->wanted_type = wanted_type;

                    process_expression(build->arguments.elements[i], state);

                    Type popped = stack_type_pop(&state->stack);
                    if (!is_type(wanted_type, &popped)) {
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
        default:
            assert(false);
    }
}

Expression_Node clone_macro_expression(Expression_Node expression, Array_String bindings, Array_Macro_Syntax_Data values);

Statement_Node clone_macro_statement(Statement_Node statement, Array_String bindings, Array_Macro_Syntax_Data values) {
    Statement_Node result = {};
    result.kind = statement.kind;

    switch (statement.kind) {
        case Statement_Declare: {
            Statement_Declare_Node* declare_in = &statement.data.declare;
            Statement_Declare_Node declare_out = { .declarations = array_declaration_new(declare_in->declarations.count) };

            for (size_t i = 0; i < declare_in->declarations.count; i++) {
                array_declaration_append(&declare_out.declarations, declare_in->declarations.elements[i]);
            }

            declare_out.expression = malloc(sizeof(Expression_Node));
            *declare_out.expression = clone_macro_expression(*declare_in->expression, bindings, values);

            result.data.declare = declare_out;
            break;
        }
        default:
            assert(false);
    }

    return result;
}

Type clone_macro_type(Type type, Array_String bindings, Array_Macro_Syntax_Data values) {
    (void) bindings;
    (void) values;

    Type result;
    result.kind = type.kind;

    switch (type.kind) {
        default:
            assert(false);
    }

    return result;
}

Expression_Node clone_macro_expression(Expression_Node expression, Array_String bindings, Array_Macro_Syntax_Data values) {
    Expression_Node result;
    result.kind = expression.kind;

    switch (expression.kind) {
        case Expression_Block: {
            Block_Node* block_in = &expression.data.block;
            Block_Node block_out = { .statements = array_statement_node_new(block_in->statements.count) };

            for (size_t i = 0; i < block_in->statements.count; i++) {
                Statement_Node* statement = malloc(sizeof(Statement_Node));
                *statement = clone_macro_statement(*block_in->statements.elements[i], bindings, values);
                array_statement_node_append(&block_out.statements, statement);
            }

            result.data.block = block_out;
            break;
        }
        case Expression_Invoke: {
            Invoke_Node* invoke_in = &expression.data.invoke;
            Invoke_Node invoke_out = { .kind = invoke_in->kind, .arguments = array_expression_node_new(invoke_in->arguments.count) };

            for (size_t i = 0; i < invoke_in->arguments.count; i++) {
                Expression_Node* expression = malloc(sizeof(Expression_Node));
                *expression = clone_macro_expression(*invoke_in->arguments.elements[i], bindings, values);
                array_expression_node_append(&invoke_out.arguments, expression);
            }

            switch (invoke_in->kind) {
                case Invoke_Standard: {
                    Expression_Node* expression = malloc(sizeof(Expression_Node));
                    *expression = clone_macro_expression(*invoke_in->data.procedure, bindings, values);
                    invoke_out.data.procedure = expression;
                    break;
                }
                default:
                    invoke_out.data.operator_ = invoke_in->data.operator_;
                    break;
            }

            result.data.invoke = invoke_out;
            break;
        }
        case Expression_Retrieve: {
            Retrieve_Node* retrieve_in = &expression.data.retrieve;
            Retrieve_Node retrieve_out = { .location = retrieve_in->location };

            retrieve_out.kind = retrieve_in->kind;

            switch (retrieve_in->kind) {
                case Retrieve_Assign_Identifier: {
                    Identifier identifier = retrieve_in->data.identifier;

                    if (identifier.kind == Identifier_Single) {
                        for (size_t i = 0; i < bindings.count; i++) {
                            if (strcmp(bindings.elements[i], identifier.data.single) == 0) {
                                return *values.elements[i]->data.expression;
                            }
                        }
                    }

                    retrieve_out.data.identifier = retrieve_in->data.identifier;
                    break;
                }
                default:
                    assert(false);
            }

            result.data.retrieve = retrieve_out;
            break;
        }
        case Expression_Number: {
            result.data.number = expression.data.number;
            break;
        }
        case Expression_String: {
            result.data.string = expression.data.string;
            break;
        }
        default:
            printf("%i\n", expression.kind);
            assert(false);
    }

    return result;
}

Macro_Syntax_Data clone_macro_macro_syntax_data(Macro_Syntax_Data data, Array_String bindings, Array_Macro_Syntax_Data values) {
    Macro_Syntax_Data result;
    result.kind = data.kind;

    switch (data.kind) {
        case Macro_Expression: {
            result.data.expression = malloc(sizeof(Expression_Node));
            *result.data.expression = clone_macro_expression(*data.data.expression, bindings, values);
        }
    }
    return data;
}

void process_expression(Expression_Node* expression, Process_State* state) {
    switch (expression->kind) {
        case Expression_Block: {
            Block_Node* block = &expression->data.block;
            array_size_append(&state->scoped_declares, state->current_declares.count);
            for (size_t i = 0; i < block->statements.count; i++) {
                process_statement(block->statements.elements[i], state);
            }
            state->current_declares.count = state->scoped_declares.elements[state->scoped_declares.count - 1];
            state->scoped_declares.count--;
            break;
        }
        case Expression_Multi: {
            Multi_Expression_Node* multi = &expression->data.multi;
            for (size_t i = 0; i < multi->expressions.count; i++) {
                process_expression(multi->expressions.elements[i], state);
            }
            break;
        }
        case Expression_Invoke: {
            Invoke_Node* invoke = &expression->data.invoke;

            if (invoke->kind == Invoke_Standard) {
                Expression_Node* procedure = invoke->data.procedure;
                bool handled = false;

                if (procedure->kind == Expression_Retrieve) {
                    bool is_internal = false;
                    if (procedure->data.retrieve.kind == Retrieve_Assign_Identifier && procedure->data.retrieve.data.identifier.kind == Identifier_Single) {
                        char* name = procedure->data.retrieve.data.identifier.data.single;

                        if (strcmp(name, "@syscall6") == 0 || strcmp(name, "@syscall5") == 0 || strcmp(name, "@syscall4") == 0 || strcmp(name, "@syscall3") == 0 || strcmp(name, "@syscall2") == 0 || strcmp(name, "@syscall1") == 0 || strcmp(name, "@syscall0") == 0) {
                            is_internal = true;
                        }
                    }

                    if (is_internal) {
                        char* name = procedure->data.retrieve.data.identifier.data.single;

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
                                Type type = stack_type_pop(&state->stack);
                                if (!is_register_sized(&type)) {
                                    print_error_stub(&invoke->location);
                                    printf("Type '");
                                    print_type_inline(&type);
                                    printf("' cannot be passed to a syscall\n");
                                    exit(1);
                                }
                            }

                            stack_type_push(&state->stack, (Type) { .kind = Type_RegisterSize, .data = {} });
                        }

                        handled = true;
                    }
                }

                if (!handled) {
                    process_expression(procedure, state);

                    Type type = stack_type_pop(&state->stack);
                    if (type.kind != Type_Pointer || type.data.pointer.child->kind != Type_Procedure) {
                        print_error_stub(&invoke->location);
                        printf("Attempting to invoke a non procedure\n");
                        exit(1);
                    }

                    Procedure_Type* procedure_type = &type.data.pointer.child->data.procedure;
                    size_t arg_index = 0;
                    for (size_t i = 0; i < invoke->arguments.count; i++) {
                        size_t stack_start = state->stack.count;
                        Type* argument_type = procedure_type->arguments.elements[arg_index];
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

                        Type given = stack_type_pop(&state->stack);
                        if (!is_type(procedure_type->arguments.elements[i], &given)) {
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

                    Type* wanted_allocated = malloc(sizeof(Type));
                    *wanted_allocated = state->stack.elements[state->stack.count - 1];
                    state->wanted_type = wanted_allocated;

                    process_expression(invoke->arguments.elements[reversed ? 0 : 1], state);
                } else if (operator == Operator_Not) {
                    process_expression(invoke->arguments.elements[0], state);
                } else {
                    assert(false);
                }

                if (operator == Operator_Add ||
                        operator == Operator_Subtract ||
                        operator == Operator_Multiply ||
                        operator == Operator_Divide ||
                        operator == Operator_Modulus) {
                    Type second = stack_type_pop(&state->stack);
                    Type first = stack_type_pop(&state->stack);

                    if (!can_operate_together(&first, &second)) {
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
                    Type first = stack_type_pop(&state->stack);
                    Type second = stack_type_pop(&state->stack);

                    if (!is_type(&first, &second)) {
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
                    Type input = stack_type_pop(&state->stack);

                    Type bool_type = create_internal_type(Type_Bool);
                    if (!is_type(&bool_type, &input)) {
                        print_error_stub(&invoke->location);
                        printf("Type '");
                        print_type_inline(&input);
                        printf("' is not a boolean\n");
                        exit(1);
                    }

                    stack_type_push(&state->stack, create_internal_type(Type_Bool));
                } else {
                    assert(false);
                }
            }
            break;
        }
        case Expression_RunMacro: {
            Run_Macro_Node* run_macro = &expression->data.run_macro;

            Resolved resolved = resolve(&state->generic, run_macro->identifier);
            assert(resolved.kind == Resolved_Item && resolved.data.item->kind == Item_Macro);
            Macro_Node* macro = &resolved.data.item->data.macro;

            assert(macro->return_ == Macro_Expression);

            for (size_t i = 0; i < macro->arguments.count; i++) {
                if (run_macro->arguments.elements[i]->kind != macro->arguments.elements[i]) {
                    print_error_stub(&run_macro->location);
                    printf("Macro invocation with wrong type!\n");
                    exit(1);
                }
            }

            // TODO: choose which branch to evaluate
            Macro_Variant variant = macro->variants.elements[0];
            Expression_Node cloned = clone_macro_expression(*variant.data.data.expression, variant.bindings, run_macro->arguments);

            run_macro->computed_expression = malloc(sizeof(Expression_Node));
            *run_macro->computed_expression = cloned;

            process_expression(run_macro->computed_expression, state);
            break;
        }
        case Expression_Retrieve: {
            Retrieve_Node* retrieve = &expression->data.retrieve;
            bool found = false;

            if (!found && retrieve->kind == Retrieve_Assign_Array) {
                bool in_reference = consume_in_reference(state);
                process_expression(retrieve->data.array.expression_outer, state);
                Type array_type = stack_type_pop(&state->stack);

                Type* array_type_raw;
                if (array_type.kind == Type_Pointer) {
                    array_type_raw = array_type.data.pointer.child;
                } else {
                    array_type_raw = &array_type;
                }

                retrieve->data.array.computed_array_type = array_type;

                state->wanted_type = usize_type();
                process_expression(retrieve->data.array.expression_inner, state);
                Type array_index_type = stack_type_pop(&state->stack);
                if (!is_internal_type(Type_USize, &array_index_type)) {
                    print_error_stub(&retrieve->location);
                    printf("Type '");
                    print_type_inline(&array_index_type);
                    printf("' cannot be used to access array\n");
                    exit(1);
                }

                Type resulting_type = *array_type_raw->data.array.element_type;
                if (in_reference) {
                    resulting_type = create_pointer_type(resulting_type);
                }
                stack_type_push(&state->stack, resulting_type);
                found = true;
            }

            if (!found && retrieve->kind == Retrieve_Assign_Identifier && retrieve->data.identifier.kind == Identifier_Single) {
                Type* variable_type = get_local_variable_type(state, retrieve->data.identifier.data.single);

                if (variable_type != NULL) {
                    found = true;
                    if (consume_in_reference(state)) {
                        stack_type_push(&state->stack, create_pointer_type(*variable_type));
                    } else {
                        stack_type_push(&state->stack, *variable_type);
                    }
                }
            }

            if (!found && retrieve->kind == Retrieve_Assign_Identifier && retrieve->data.identifier.kind == Identifier_Single) {
                Type type = { .directives = array_directive_new(1) };
                for (size_t i = 0; i < state->current_arguments.count; i++) {
                    Declaration* declaration = &state->current_arguments.elements[state->current_arguments.count - i - 1];
                    if (strcmp(declaration->name, retrieve->data.identifier.data.single) == 0) {
                        type = declaration->type;
                        found = true;
                        break;
                    }
                }

                if (found) {
                    if (consume_in_reference(state)) {
                        type = create_pointer_type(type);
                    }

                    stack_type_push(&state->stack, type);
                }
            }

            if (!found && retrieve->kind == Retrieve_Assign_Parent) {
                bool in_reference = consume_in_reference(state);
                process_expression(retrieve->data.parent.expression, state);
                Type parent_type = stack_type_pop(&state->stack);

                retrieve->data.parent.computed_parent_type = parent_type;

                Type* parent_type_raw;
                if (parent_type.kind == Type_Pointer) {
                    parent_type_raw = parent_type.data.pointer.child;
                } else {
                    parent_type_raw = &parent_type;
                }

                Type item_type;
                Type* result_type = get_parent_item_type(parent_type_raw, retrieve->data.parent.name, &state->generic);
                if (result_type != NULL) {
                    item_type = *result_type;
                    found = true;
                }

                if (in_reference) {
                    item_type = create_pointer_type(item_type);
                }

                stack_type_push(&state->stack, item_type);
            }

            if (!found && retrieve->kind == Retrieve_Assign_Identifier) {
                if (retrieve->data.identifier.kind == Identifier_Multi && strcmp(retrieve->data.identifier.data.multi.elements[0], "") == 0) {
                    assert(state->wanted_type->kind == Type_Enum);

                    char* enum_variant = retrieve->data.identifier.data.multi.elements[1];

                    Enum_Type* enum_type = &state->wanted_type->data.enum_;
                    for (size_t i = 0; i < enum_type->items.count; i++) {
                        if (strcmp(enum_variant, enum_type->items.elements[i]) == 0) {
                            retrieve->computed_result_type = *state->wanted_type;
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
                        Item_Node* item = resolved.data.item;

                        found = true;
                        switch (item->kind) {
                            case Item_Procedure: {
                                Procedure_Node* procedure = &item->data.procedure;

                                Type type = { .directives = array_directive_new(1) };
                                Procedure_Type procedure_type;
                                procedure_type.arguments = array_type_new(4);
                                procedure_type.returns = array_type_new(4);

                                for (size_t i = 0; i < procedure->arguments.count; i++) {
                                    array_type_append(&procedure_type.arguments, &procedure->arguments.elements[i].type);
                                }

                                for (size_t i = 0; i < procedure->returns.count; i++) {
                                    array_type_append(&procedure_type.returns, procedure->returns.elements[i]);
                                }

                                type.kind = Type_Procedure;
                                type.data.procedure = procedure_type;
                                stack_type_push(&state->stack, create_pointer_type(type));
                                break;
                            }
                            case Item_Global: {
                                Global_Node* global = &item->data.global;

                                if (consume_in_reference(state)) {
                                    stack_type_push(&state->stack, create_pointer_type(global->type));
                                } else {
                                    stack_type_push(&state->stack, global->type);
                                }
                                break;
                            }
                            case Item_Constant: {
                                Type* wanted_type = state->wanted_type;
                                if (wanted_type == NULL || !is_number_type(wanted_type)) {
                                    wanted_type = usize_type();
                                }

                                retrieve->computed_result_type = *wanted_type;

                                stack_type_push(&state->stack, *wanted_type);
                                break;
                            }
                            default:
                                assert(false);
                        }
                        break;
                    }
                    case Resolved_Enum_Variant: {
                        found = true;
                        Type type;
                        type.kind = Type_Basic;
                        Basic_Type basic = identifier_to_basic_type(retrieve->data.identifier);

                        // Kinda a hack, probably should make a more robust way to handling enumerations
                        if (basic.data.multi.count > 2) {
                            basic.data.multi.count--;
                        } else {
                            basic.kind = Type_Single;
                            basic.data.single = basic.data.multi.elements[0];
                        }

                        type.data.basic = basic;
                        stack_type_push(&state->stack, type);
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
            If_Node* node = &expression->data.if_;
            process_expression(node->condition, state);

            if (state->stack.count == 0) {
                print_error_stub(&node->location);
                printf("Ran out of values for if\n");
                exit(1);
            }

            Type given = stack_type_pop(&state->stack);
            Type bool_type = create_internal_type(Type_Bool);
            if (!is_type(&bool_type, &given)) {
                print_error_stub(&node->location);
                printf("Type '");
                print_type_inline(&given);
                printf("' is not a boolean\n");
                exit(1);
            }

            process_expression(node->if_expression, state);

            if (node->else_expression != NULL) {
                process_expression(node->else_expression, state);
            }
            break;
        }
        case Expression_While: {
            While_Node* node = &expression->data.while_;
            process_expression(node->condition, state);

            if (state->stack.count == 0) {
                print_error_stub(&node->location);
                printf("Ran out of values for if\n");
                exit(1);
            }

            Type given = stack_type_pop(&state->stack);
            Type bool_type = create_internal_type(Type_Bool);
            if (!is_type(&bool_type, &given)) {
                print_error_stub(&node->location);
                printf("Type '");
                print_type_inline(&given);
                printf("' is not a boolean\n");
                exit(1);
            }

            process_expression(node->inside, state);
            break;
        }
        case Expression_Number: {
            Number_Node* number = &expression->data.number;
            Type* wanted = state->wanted_type;

            if (wanted != NULL && is_number_type(wanted)) {
                stack_type_push(&state->stack, *wanted);
                number->type = wanted;
            } else {
                Type* usize = malloc(sizeof(Type));
                *usize = create_internal_type(Type_USize);
                stack_type_push(&state->stack, *usize);
                number->type = usize;
            }
            break;
        }
        case Expression_Boolean: {
            stack_type_push(&state->stack, create_internal_type(Type_Bool));
            break;
        }
        case Expression_String: {
            stack_type_push(&state->stack, create_pointer_type(create_array_type(create_internal_type(Type_U8))));
            break;
        }
        case Expression_Reference: {
            Reference_Node* reference = &expression->data.reference;
            state->in_reference = true;

            process_expression(reference->inner, state);
            break;
        }
        case Expression_Cast: {
            Cast_Node* cast = &expression->data.cast;

            process_expression(cast->expression, state);
            Type input = stack_type_pop(&state->stack);

            bool is_valid = false;
            if (cast->type.kind == Type_Internal && input.kind == Type_Internal) {
                Internal_Type output_internal = cast->type.data.internal;
                Internal_Type input_internal = input.data.internal;

                if ((input_internal == Type_USize || input_internal == Type_U64 || input_internal == Type_U32 || input_internal == Type_U86 || input_internal == Type_U8) &&
                        (output_internal == Type_USize || output_internal == Type_U64 || output_internal == Type_U32 || output_internal == Type_U86 || output_internal == Type_U8)) {
                    is_valid = true;
                } else if (input_internal == Type_F8 && output_internal == Type_U64) {
                    is_valid = true;
                }
            }

            if (input.kind == Type_Internal && input.data.internal == Type_Ptr && cast->type.kind == Type_Pointer) {
                is_valid = true;
            }

            if (cast->type.kind == Type_Internal && cast->type.data.internal == Type_Ptr && input.kind == Type_Pointer) {
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
            Init_Node* init = &expression->data.init;
            Type* type = &init->type;
            stack_type_append(&state->stack, *type);
            break;
        }
        case Expression_Build: {
            Build_Node* build = &expression->data.build;

            Type* type = &build->type;
            process_build_type(build, type, state);

            stack_type_append(&state->stack, *type);
            break;
        }
        case Expression_SizeOf: {
            SizeOf_Node* size_of = &expression->data.size_of;
            Type* wanted_type = state->wanted_type;
            if (wanted_type == NULL || !is_number_type(wanted_type)) {
                wanted_type = usize_type();
            }

            process_type_expression(&size_of->type, state);

            size_of->computed_result_type = *wanted_type;
            stack_type_append(&state->stack, *wanted_type);
            break;
        }
        case Expression_LengthOf: {
            LengthOf_Node* length_of = &expression->data.length_of;
            Type* wanted_type = state->wanted_type;
            if (wanted_type == NULL || !is_number_type(wanted_type)) {
                wanted_type = usize_type();
            }

            process_type_expression(&length_of->type, state);

            length_of->computed_result_type = *wanted_type;
            stack_type_append(&state->stack, *wanted_type);
            break;
        }
        default:
            assert(false);
    }
}

bool has_directive(Array_Directive* directives, Directive_Kind kind) {
    for (size_t i = 0; i < directives->count; i++) {
        if (directives->elements[i].kind == kind) {
            return true;
        }
    }
    return false;
}

Directive_Node* get_directive(Array_Directive* directives, Directive_Kind kind) {
    for (size_t i = 0; i < directives->count; i++) {
        if (directives->elements[i].kind == kind) {
            return &directives->elements[i];
        }
    }
    return false;
}

void process_item(Item_Node* item, Process_State* state) {
    if (has_directive(&item->directives, Directive_If)) {
        Directive_If_Node* if_node = &get_directive(&item->directives, Directive_If)->data.if_;
        if_node->result = evaluate_if_directive(if_node);
        if (!if_node->result) {
            return;
        }
    }

    switch (item->kind) {
        case Item_Procedure: {
            state->current_procedure = item;
            Procedure_Node* procedure = &item->data.procedure;
            state->current_declares = array_declaration_new(4);
            state->current_arguments = procedure->arguments;
            state->current_returns = procedure->returns;
            state->current_body = procedure->body;

            process_expression(procedure->body, state);
            break;
        }
        case Item_Macro: {
            break;
        }
        case Item_Global: {
            break;
        }
        case Item_Type: {
            break;
        }
        case Item_Constant:
            break;
        case Item_Use:
            break;
        default:
            assert(false);
    }
}

void process(Program* program, Array_String* package_names, Array_String* package_paths) {
    Process_State state = (Process_State) {
        .generic = (Generic_State) {
            .program = program,
            // should items just be able to store their file?
            .current_file = NULL,
            .package_names = package_names,
            .package_paths = package_paths,
        },
        .stack = stack_type_new(8),
        .current_declares = {},
        .scoped_declares = {},
        .current_arguments = {},
        .current_returns = {},
        .current_body = NULL,
        .in_reference = false,
        .wanted_type = NULL,
    };

    for (size_t j = 0; j < program->count; j++) {
        File_Node* file_node = &program->elements[j];
        state.generic.current_file = file_node;

        for (size_t i = 0; i < file_node->items.count; i++) {
            Item_Node* item = &file_node->items.elements[i];
            process_item(item, &state);
        }
    }
}
