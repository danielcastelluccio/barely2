#include <assert.h>
#include <stdio.h>

#include "ast_print.h"
#include "file_util.h"
#include "string_util.h"
#include "processor.h"

Dynamic_Array_Impl(Type, Stack_Type, stack_type_)

void stack_type_push(Stack_Type* stack, Type type) {
    stack_type_append(stack, type);
}

Type stack_type_pop(Stack_Type* stack) {
    Type result = stack->elements[stack->count - 1];
    stack->count--;
    return result;
}

Dynamic_Array_Impl(size_t, Array_Size, array_size_)

typedef struct {
    Generic_State generic;
    Stack_Type stack;
    Array_Declaration current_declares;
    Array_Size scoped_declares;
    Array_Declaration current_arguments;
    Array_Type current_returns;
    Expression_Node* current_body;
    bool in_reference;
    Type* wanted_type;
} Process_State;

Item_Identifier retrieve_assign_to_item_identifier(Retrieve_Assign_Node* retrieve_assign) {
    return retrieve_assign->data.identifier;
}

bool is_number_type(Type* type) {
    if (type->kind == Type_Internal) {
        Internal_Type internal = type->data.internal;

        if (internal == Type_USize || internal == Type_U8 || internal == Type_U4 || internal == Type_U2 || internal == Type_U1) {
            return true;
        }
    }

    return false;
}

bool uses(File_Node* checked, File_Node* tested, Generic_State* state) {
    char* checked_path = checked->path;
    size_t checked_path_len = strlen(checked_path);
    size_t last_slash;
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

Resolved_Item resolve_item(Generic_State* state, Item_Identifier data) {
    for (size_t j = 0; j < state->program->count; j++) {
        File_Node* file_node = &state->program->elements[j];
        if (uses(state->current_file, file_node, state) || state->current_file == file_node) {
            for (size_t i = 0; i < file_node->items.count; i++) {
                Item_Node* item = &file_node->items.elements[i];
                // TODO: support multi retrieves & assigns
                if (data.kind == Identifier_Single && strcmp(item->name, data.data.single) == 0) {
                    return (Resolved_Item) { file_node, item };
                }
            }
        }
    }

    return (Resolved_Item) { NULL, NULL };
}

Type create_basic_single_type(char* name) {
    Type type;
    Basic_Type basic;

    basic.kind = Type_Single;
    basic.data.single = name;

    type.kind = Type_Basic;
    type.data.basic = basic;

    return type;
}

Type create_internal_type(Internal_Type internal_type) {
    Type type;

    type.kind = Type_Internal;
    type.data.internal = internal_type;

    return type;
}

Type create_array_type(Type child) {
    Type type;
    BArray_Type array = {};

    Type* child_allocated = malloc(sizeof(Type));
    *child_allocated = child;
    array.element_type = child_allocated;

    type.kind = Type_Array;
    type.data.array = array;

    return type;
}

Type create_pointer_type(Type child) {
    Type type;
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
        if (to_check->kind == Type_Internal && to_check->data.internal == Type_USize) return true;

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
            if (wanted->data.array.size != given->data.array.size) return false;
        }
        return is_type(wanted->data.array.element_type, given->data.array.element_type);
    }

    if (wanted->kind == Type_Internal) {
        return wanted->data.internal == given->data.internal;
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
                printf("%zu", array->size);
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
                case Type_U8:
                    printf("u8");
                    break;
                case Type_U4:
                    printf("u4");
                    break;
                case Type_U2:
                    printf("u2");
                    break;
                case Type_U1:
                    printf("u1");
                    break;
            }
            break;
        }
        case Type_RegisterSize: {
            printf("size");
            break;
        }
        default:
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

Item_Identifier basic_type_to_item_identifier(Basic_Type type) {
    Item_Identifier item_identifier;
    if (type.kind == Type_Single) {
        item_identifier.data.single = type.data.single;
        item_identifier.kind = Identifier_Single;
    } else {
        item_identifier.data.multi = type.data.multi;
        item_identifier.kind = Identifier_Multi;
    }
    return item_identifier;
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

void process_statement(Statement_Node* statement, Process_State* state) {
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
                    Declaration declaration = declare->declarations.elements[i];

                    if (state->stack.count == 0) {
                        print_error_stub(&declaration.location);
                        printf("Ran out of values for declaration assignment\n");
                        exit(1);
                    }

                    Type popped = stack_type_pop(&state->stack);
                    if (!is_type(&declaration.type, &popped)) {
                        print_error_stub(&declaration.location);
                        printf("Type '");
                        print_type_inline(&popped);
                        printf("' is not assignable to variable of type '");
                        print_type_inline(&declaration.type);
                        printf("'\n");
                        exit(1);
                    }
                    array_declaration_append(&state->current_declares, declaration);
                }
            } else {
                for (int i = declare->declarations.count - 1; i >= 0; i--) {
                    Declaration declaration = declare->declarations.elements[i];
                    array_declaration_append(&state->current_declares, declaration);
                }
            }
            break;
        }
        case Statement_Assign: {
            Statement_Assign_Node* assign = &statement->data.assign;

            Array_Type wanted_types = array_type_new(1);
            for (size_t i = 0; i < assign->parts.count; i++) {
                Statement_Assign_Part* assign_part = &assign->parts.elements[i];

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

                    array_type_append(&wanted_types, array_type_raw->data.array.element_type);
                }

                if (assign_part->kind == Retrieve_Assign_Identifier) {
                    assert(assign_part->data.identifier.kind == Identifier_Single);
                    char* name = assign_part->data.identifier.data.single;

                    bool found = false;
                    Type* type = malloc(sizeof(Type));
                    for (size_t j = 0; j < state->current_declares.count; j++) {
                        if (strcmp(state->current_declares.elements[j].name, name) == 0) {
                            *type = state->current_declares.elements[j].type;
                            found = true;
                            break;
                        }
                    }

                    if (!found) {
                        Item_Node* item = resolve_item(&state->generic, retrieve_assign_to_item_identifier(assign_part)).item;
                        if (item != NULL) {
                            if (item->kind == Item_Global) {
                                *type = item->data.global.type;
                                found = true;
                            }
                        }
                    }

                    if (!found) {
                        print_error_stub(&assign_part->location);
                        printf("Assign not found\n");
                        exit(1);
                    }

                    array_type_append(&wanted_types, type);
                }

                if (assign_part->kind == Retrieve_Assign_Struct) {
                    process_expression(assign_part->data.struct_.expression, state);
                    Type struct_type = stack_type_pop(&state->stack);

                    assign_part->data.struct_.computed_struct_type = struct_type;

                    Type* struct_type_raw;
                    if (struct_type.kind == Type_Pointer) {
                        struct_type_raw = struct_type.data.pointer.child;
                    } else {
                        struct_type_raw = &struct_type;
                    }

                    Type* type = malloc(sizeof(Type));
                    if (strcmp(assign_part->data.struct_.name, "*") == 0) {
                        *type = *struct_type_raw;
                    } else {
                        Item_Identifier item_identifier = basic_type_to_item_identifier(struct_type_raw->data.basic);

                        Item_Node* item = resolve_item(&state->generic, item_identifier).item;
                        Struct_Node* struct_ = &item->data.type.data.struct_;
                        for (size_t i = 0; i < struct_->items.count; i++) {
                            Declaration* declaration = &struct_->items.elements[i];
                            if (strcmp(declaration->name, assign_part->data.struct_.name) == 0) {
                                *type = declaration->type;
                            }
                        }
                    }

                    array_type_append(&wanted_types, type);
                }
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

                if (assign_part->kind == Retrieve_Assign_Identifier) {
                    assert(assign_part->data.identifier.kind == Identifier_Single);
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

                if (assign_part->kind == Retrieve_Assign_Struct) {
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
                    // TODO: location
                    print_error_stub(&return_->location);
                    printf("Ran out of values for return\n");
                    exit(1);
                }

                Type given_type = stack_type_pop(&state->stack);
                if (!is_type(return_type, &given_type)) {
                    // TODO: location
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
            printf("Unhandled statement_type!\n");
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

                        if (strcmp(name, "@syscall6") == 0) {
                            size_t count = 7;
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
                        } else if (strcmp(name, "@syscall5") == 0) {
                            size_t count = 6;
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
                        } else if (strcmp(name, "@syscall4") == 0) {
                            size_t count = 5;
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
                        } else if (strcmp(name, "@syscall3") == 0) {
                            size_t count = 4;
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
                        } else if (strcmp(name, "@syscall2") == 0) {
                            size_t count = 3;
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
                        } else if (strcmp(name, "@syscall1") == 0) {
                            size_t count = 2;
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
                        } else if (strcmp(name, "@syscall0") == 0) {
                            size_t count = 1;
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
                    if (type.kind != Type_Procedure) {
                        print_error_stub(&invoke->location);
                        printf("Attempting to invoke a non procedure\n");
                        exit(1);
                    }

                    Procedure_Type* procedure_type = &type.data.procedure;
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
                Operator operator = invoke->data.operator.operator;

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
                }

                if (operator == Operator_Add ||
                        operator == Operator_Subtract ||
                        operator == Operator_Multiply ||
                        operator == Operator_Divide ||
                        operator == Operator_Modulus) {
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

                    invoke->data.operator.computed_operand_type = first;
                    stack_type_push(&state->stack, first);
                }

                if (operator == Operator_Equal ||
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

                    invoke->data.operator.computed_operand_type = first;

                    stack_type_push(&state->stack, create_basic_single_type("bool"));
                }
            }
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

            if (!found && retrieve->kind == Retrieve_Assign_Identifier) {
                assert(retrieve->data.identifier.kind == Identifier_Single);
                Type variable_type;
                for (int i = state->current_declares.count - 1; i >= 0; i--) {
                    Declaration* declaration = &state->current_declares.elements[i];
                    if (strcmp(declaration->name, retrieve->data.identifier.data.single) == 0) {
                        variable_type = declaration->type;
                        found = true;
                        break;
                    }
                }

                if (found) {
                    if (consume_in_reference(state)) {
                        stack_type_push(&state->stack, create_pointer_type(variable_type));
                    } else {
                        stack_type_push(&state->stack, variable_type);
                    }
                }
            }

            if (!found && retrieve->kind == Retrieve_Assign_Struct) {
                bool in_reference = consume_in_reference(state);
                process_expression(retrieve->data.struct_.expression, state);
                Type struct_type = stack_type_pop(&state->stack);

                retrieve->data.struct_.computed_struct_type = struct_type;

                Type* struct_type_raw;
                if (struct_type.kind == Type_Pointer) {
                    struct_type_raw = struct_type.data.pointer.child;
                } else {
                    struct_type_raw = &struct_type;
                }

                Type item_type;
                if (strcmp(retrieve->data.struct_.name, "*") == 0) {
                    item_type = *struct_type_raw;
                } else {
                    Item_Identifier item_identifier = basic_type_to_item_identifier(struct_type_raw->data.basic);

                    Item_Node* item = resolve_item(&state->generic, item_identifier).item;
                    Struct_Node* struct_ = &item->data.type.data.struct_;
                    for (size_t i = 0; i < struct_->items.count; i++) {
                        Declaration* declaration = &struct_->items.elements[i];
                        if (strcmp(declaration->name, retrieve->data.struct_.name) == 0) {
                            item_type = declaration->type;
                            found = true;
                        }
                    }
                }

                if (in_reference) {
                    item_type = create_pointer_type(item_type);
                }

                stack_type_push(&state->stack, item_type);
            }

            if (!found && retrieve->kind == Retrieve_Assign_Identifier) {
                assert(retrieve->data.identifier.kind == Identifier_Single);
                Type type;
                for (size_t i =  0; i < state->current_arguments.count; i--) {
                    Declaration* declaration = &state->current_arguments.elements[state->current_arguments.count - i - 1];
                    if (strcmp(declaration->name, retrieve->data.identifier.data.single) == 0) {
                        type = declaration->type;
                        found = true;
                        break;
                    }
                }

                if (found) {
                    stack_type_push(&state->stack, type);
                }
            }

            if (!found && retrieve->kind == Retrieve_Assign_Identifier) {
                Item_Node* item = resolve_item(&state->generic, retrieve_assign_to_item_identifier(retrieve)).item;
                if (item != NULL) {
                    found = true;
                    switch (item->kind) {
                        case Item_Procedure:
                            Procedure_Literal_Node* procedure = &item->data.procedure.data.literal;

                            Type type;
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
                            stack_type_push(&state->stack, type);
                            break;
                        case Item_Global:
                            Global_Node* global = &item->data.global;
                            
                            if (consume_in_reference(state)) {
                                stack_type_push(&state->stack, create_pointer_type(global->type));
                            } else {
                                stack_type_push(&state->stack, global->type);
                            }
                            break;
                        default:
                            printf("Unhandled item retrieve!\n");
                            exit(1);
                    }
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
            while (node != NULL) {
                if (node->condition != NULL) {
                    process_expression(node->condition, state);

                    if (state->stack.count == 0) {
                        print_error_stub(&node->location);
                        printf("Ran out of values for if\n");
                        exit(1);
                    }

                    Type given = stack_type_pop(&state->stack);
                    Type bool_type = create_basic_single_type("bool");
                    if (!is_type(&bool_type, &given)) {
                        print_error_stub(&node->location);
                        printf("Type '");
                        print_type_inline(&given);
                        printf("' is not a boolean\n");
                        exit(1);
                    }
                }

                process_expression(node->inside, state);

                node = node->next;
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
            Type bool_type = create_basic_single_type("bool");
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
                Type* u8 = malloc(sizeof(Type));
                *u8 = create_internal_type(Type_USize);
                stack_type_push(&state->stack, *u8);
                number->type = u8;
            }
            break;
        }
        case Expression_Boolean: {
            stack_type_push(&state->stack, create_basic_single_type("bool"));
            break;
        }
        case Expression_String: {
            stack_type_push(&state->stack, create_pointer_type(create_array_type(create_internal_type(Type_U1))));
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

                if ((input_internal == Type_USize || input_internal == Type_U8 || input_internal == Type_U4 || input_internal == Type_U2 || input_internal == Type_U1) && 
                        (output_internal == Type_USize || output_internal == Type_U8 || output_internal == Type_U4 || output_internal == Type_U2 || output_internal == Type_U1)) {
                    is_valid = true;
                }
            }

            if (!is_valid) {
                print_error_stub(&cast->location);
                printf("Type '");
                print_type_inline(&input);
                printf("' cast to type '");
                print_type_inline(&cast->type);
                printf("'\n");
                exit(1);
            }

            cast->computed_input_type = input;

            stack_type_append(&state->stack, cast->type);
            break;
        }
        case Expression_SizeOf: {
            SizeOf_Node* size_of = &expression->data.size_of;
            Type* wanted_type = state->wanted_type;
            if (wanted_type == NULL || !is_number_type(wanted_type)) {
                wanted_type = usize_type();
            }

            size_of->computed_result_type = *wanted_type;
            stack_type_append(&state->stack, *wanted_type);
            break;
        }
        default:
            printf("Unhandled expression_type!\n");
            exit(1);
    }
}

void process_item(Item_Node* item, Process_State* state) {
    switch (item->kind) {
        case Item_Procedure: {
            Procedure_Node* procedure = &item->data.procedure;
            state->current_declares = array_declaration_new(4);
            state->current_arguments = procedure->data.literal.arguments;
            state->current_returns = procedure->data.literal.returns;
            state->current_body = procedure->data.literal.body;

            process_expression(procedure->data.literal.body, state);
            break;
        }
        case Item_Type:
            break;
        case Item_Global:
            break;
        case Item_Use:
            break;
        default:
            printf("Unhandled item_type!\n");
            exit(1);
    }
}

void process(Program* program, Array_String* package_names, Array_String* package_paths) {
    Process_State state = (Process_State) {
        .generic = (Generic_State) {
            .program = program,
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
