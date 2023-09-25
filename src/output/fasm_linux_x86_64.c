#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "fasm_linux_x86_64.h"
#include "../ast_walk.h"

typedef struct {
    Generic_State generic;
    String_Buffer instructions;
    String_Buffer data;
    String_Buffer bss;
    size_t string_index;
    size_t flow_index;
    Array_Ast_Declaration current_declares;
    Array_Size scoped_declares;
    Ast_Item* current_procedure;
    bool in_reference;
} Output_State;

size_t get_size(Ast_Type* type_in, Output_State* state) {
    Ast_Type type = evaluate_type_complete(type_in, &state->generic);
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
                case Type_Float8:
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

typedef struct {
    size_t total;
    Output_State* output_state;
} Locals_Walk_State;

void collect_statement_locals_size(Ast_Statement* statement, void* state_in) {
    Locals_Walk_State* state = state_in;
    switch (statement->kind) {
        case Statement_Declare: {
            size_t size = 0;
            for (size_t i = 0; i < statement->data.declare.declarations.count; i++) {
                Ast_Type type = statement->data.declare.declarations.elements[i].type;
                size += get_size(&type, state->output_state);
            }
            state->total += size;
        }
        default:
            break;
    }
}

bool consume_in_reference_output(Output_State* state) {
    bool cached = state->in_reference;
    state->in_reference = false;
    return cached;
}

void output_expression_fasm_linux_x86_64(Ast_Expression* expression, Output_State* state);

typedef struct {
    size_t size;
    size_t location;
} Location_Size_Data;

Location_Size_Data get_parent_item_location_size(Ast_Type* parent_type, char* item_name, Output_State* state) {
    Location_Size_Data result = {};

    if (strcmp(item_name, "*") == 0) {
        result.size = get_size(parent_type, state);
    } else {
        switch (parent_type->kind) {
            case Type_Basic: {
                Ast_Identifier identifier = parent_type->data.basic.identifier;
                Resolved resolved = resolve(&state->generic, identifier);

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

bool has_local_variable(char* name, Output_State* state) {
    for (int i = state->current_declares.count - 1; i >= 0; i--) {
        Ast_Declaration* declaration = &state->current_declares.elements[i];
        if (strcmp(declaration->name, name) == 0) {
            return true;
        }
    }

    return false;
}

Location_Size_Data get_local_variable_location_size(char* name, Output_State* state) {
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

void output_statement_fasm_linux_x86_64(Ast_Statement* statement, Output_State* state) {
    if (has_directive(&statement->directives, Directive_If)) {
        Ast_Directive_If* if_node = &get_directive(&statement->directives, Directive_If)->data.if_;
        if (!if_node->result) {
            return;
        }
    }

    switch (statement->kind) {
        case Statement_Expression: {
            Ast_Statement_Expression* statement_expression = &statement->data.expression;
            output_expression_fasm_linux_x86_64(statement_expression->expression, state);
            break;
        }
        case Statement_Declare: {
            Ast_Statement_Declare* declare = &statement->data.declare;
            if (declare->expression != NULL) {
                output_expression_fasm_linux_x86_64(declare->expression, state);

                for (int i = declare->declarations.count - 1; i >= 0; i--) {
                    Ast_Declaration declaration = declare->declarations.elements[i];
                    size_t location = 8;
                    for (size_t j = 0; j < state->current_declares.count; j++) {
                        location += get_size(&state->current_declares.elements[j].type, state);
                    }
                    size_t size = get_size(&declaration.type, state);

                    size_t i = 0;
                    while (i < size) {
                        if (size - i >= 8) {
                            char buffer[128] = {};
                            sprintf(buffer, "  mov rax, [rsp+%zu]\n", i);
                            stringbuffer_appendstring(&state->instructions, buffer);

                            memset(buffer, 0, 128);

                            sprintf(buffer, "  mov [rbp-%zu], rax\n", location + size - i);
                            stringbuffer_appendstring(&state->instructions, buffer);

                            i += 8;
                        } else if (size - i >= 1) {
                            char buffer[128] = {};
                            sprintf(buffer, "  mov al, [rsp+%zu]\n", i);
                            stringbuffer_appendstring(&state->instructions, buffer);

                            memset(buffer, 0, 128);

                            sprintf(buffer, "  mov [rbp-%zu], al\n", location + size - i);
                            stringbuffer_appendstring(&state->instructions, buffer);

                            i += 1;
                        }
                    }

                    char buffer[128] = {};
                    sprintf(buffer, "  add rsp, %zu\n", size);
                    stringbuffer_appendstring(&state->instructions, buffer);

                    array_ast_declaration_append(&state->current_declares, declaration);
                }
            } else {
                for (int i = declare->declarations.count - 1; i >= 0; i--) {
                    Ast_Declaration declaration = declare->declarations.elements[i];
                    array_ast_declaration_append(&state->current_declares, declaration);
                }
            }
            break;
        }
        case Statement_Assign: {
            Ast_Statement_Assign* assign = &statement->data.assign;
            output_expression_fasm_linux_x86_64(assign->expression, state);

            for (int i = assign->parts.count - 1; i >= 0; i--) {
                Statement_Assign_Part* assign_part = &assign->parts.elements[i];
                bool found = false;

                if (!found && assign_part->kind == Retrieve_Assign_Array) {
                    Ast_Type array_type = assign_part->data.array.computed_array_type;

                    Ast_Type* array_ast_type_raw;
                    if (array_type.kind == Type_Pointer) {
                        array_ast_type_raw = array_type.data.pointer.child;
                    } else {
                        state->in_reference = true;
                        array_ast_type_raw = &array_type;
                    }

                    output_expression_fasm_linux_x86_64(assign_part->data.array.expression_outer, state);

                    output_expression_fasm_linux_x86_64(assign_part->data.array.expression_inner, state);

                    size_t size = get_size(array_ast_type_raw->data.array.element_type, state);

                    stringbuffer_appendstring(&state->instructions, "  pop rax\n");
                    stringbuffer_appendstring(&state->instructions, "  pop rcx\n");

                    char buffer[128] = {};
                    sprintf(buffer, "  mov rdx, %zu\n", size);
                    stringbuffer_appendstring(&state->instructions, buffer);

                    stringbuffer_appendstring(&state->instructions, "  mul rdx\n");
                        
                    size_t i = 0;
                    while (i < size) {
                        if (size - i >= 8) {
                            char buffer[128] = {};
                            sprintf(buffer, "  mov rbx, [rsp+%zu]\n", i);
                            stringbuffer_appendstring(&state->instructions, buffer);

                            memset(buffer, 0, 128);

                            sprintf(buffer, "  mov [rax+rcx+%zu], rbx\n", i);
                            stringbuffer_appendstring(&state->instructions, buffer);

                            i += 8;
                        } else if (size - i >= 1) {
                            char buffer[128] = {};
                            sprintf(buffer, "  mov bl, [rsp+%zu]\n", i);
                            stringbuffer_appendstring(&state->instructions, buffer);

                            memset(buffer, 0, 128);

                            sprintf(buffer, "  mov [rax+rcx+%zu], bl\n", i);
                            stringbuffer_appendstring(&state->instructions, buffer);

                            i += 1;
                        }
                    }

                    memset(buffer, 0, 128);
                    sprintf(buffer, "  add rsp, %zu\n", size);
                    stringbuffer_appendstring(&state->instructions, buffer);

                    found = true;
                }

                if (!found && assign_part->kind == Retrieve_Assign_Parent) {
                    Ast_Type parent_type = assign_part->data.parent.computed_parent_type;

                    Ast_Type* parent_type_raw;
                    if (parent_type.kind == Type_Pointer) {
                        parent_type_raw = parent_type.data.pointer.child;
                    } else {
                        state->in_reference = true;
                        parent_type_raw = &parent_type;
                    }

                    output_expression_fasm_linux_x86_64(assign_part->data.parent.expression, state);

                    stringbuffer_appendstring(&state->instructions, "  pop rax\n");

                    Location_Size_Data result = get_parent_item_location_size(parent_type_raw, assign_part->data.parent.name, state);
                    size_t location = result.location;
                    size_t size = result.size;
                        
                    size_t i = 0;
                    while (i < size) {
                        if (size - i >= 8) {
                            char buffer[128] = {};
                            sprintf(buffer, "  mov rbx, [rsp+%zu]\n", i);
                            stringbuffer_appendstring(&state->instructions, buffer);

                            memset(buffer, 0, 128);

                            sprintf(buffer, "  mov [rax+%zu], rbx\n", location + i);
                            stringbuffer_appendstring(&state->instructions, buffer);

                            i += 8;
                        } else if (size - i >= 1) {
                            char buffer[128] = {};
                            sprintf(buffer, "  mov bl, [rsp+%zu]\n", i);
                            stringbuffer_appendstring(&state->instructions, buffer);

                            memset(buffer, 0, 128);

                            sprintf(buffer, "  mov [rax+%zu], bl\n", location + i);
                            stringbuffer_appendstring(&state->instructions, buffer);

                            i += 1;
                        }
                    }

                    char buffer[128] = {};
                    sprintf(buffer, "  add rsp, %zu\n", size);
                    stringbuffer_appendstring(&state->instructions, buffer);

                    found = true;

                }

                if (!found && assign_part->kind == Retrieve_Assign_Identifier) {
                    char* name = assign_part->data.identifier.name;

                    if (has_local_variable(name, state)) {
                        found = true;

                        Location_Size_Data result = get_local_variable_location_size(name, state);
                        size_t location = result.location;
                        size_t size = result.size;

                        size_t i = 0;
                        while (i < size) {
                            if (size - i >= 8) {
                                char buffer[128] = {};
                                sprintf(buffer, "  mov rax, [rsp+%zu]\n", i);
                                stringbuffer_appendstring(&state->instructions, buffer);

                                memset(buffer, 0, 128);

                                sprintf(buffer, "  mov [rbp-%zu], rax\n", location + size - i);
                                stringbuffer_appendstring(&state->instructions, buffer);

                                i += 8;
                            } else if (size - i >= 1) {
                                char buffer[128] = {};
                                sprintf(buffer, "  mov al, [rsp+%zu]\n", i);
                                stringbuffer_appendstring(&state->instructions, buffer);

                                memset(buffer, 0, 128);

                                sprintf(buffer, "  mov [rbp-%zu], al\n", location + size - i);
                                stringbuffer_appendstring(&state->instructions, buffer);

                                i += 1;
                            }
                        }

                        char buffer[128] = {};
                        sprintf(buffer, "  add rsp, %zu\n", size);
                        stringbuffer_appendstring(&state->instructions, buffer);
                    }
                }

                if (!found && assign_part->kind == Retrieve_Assign_Identifier) {
                    Resolved resolved = resolve(&state->generic, assign_part->data.identifier);
                    switch (resolved.kind) {
                        case Resolved_Item: {
                            Ast_Item* item = resolved.data.item;
                            switch (item->kind) {
                                case Item_Global: {
                                    Ast_Item_Global* global = &item->data.global;
                                    size_t size = get_size(&global->type, state);

                                    size_t i = 0;
                                    while (i < size) {
                                        if (size - i >= 8) {
                                            char buffer[128] = {};
                                            sprintf(buffer, "  mov rax, [rsp+%zu]\n", i);
                                            stringbuffer_appendstring(&state->instructions, buffer);

                                            memset(buffer, 0, 128);

                                            sprintf(buffer + strlen(buffer), "  mov [%s+%zu], rax\n", global->name, i);
                                            stringbuffer_appendstring(&state->instructions, buffer);

                                            i += 8;
                                        } else if (size - i >= 1) {
                                            char buffer[128] = {};
                                            sprintf(buffer, "  mov al, [rsp+%zu]\n", i);
                                            stringbuffer_appendstring(&state->instructions, buffer);

                                            memset(buffer, 0, 128);

                                            sprintf(buffer + strlen(buffer), "  mov [%s+%zu], al\n", global->name, i);
                                            stringbuffer_appendstring(&state->instructions, buffer);

                                            i += 1;
                                        }
                                    }

                                    char buffer[128] = {};
                                    sprintf(buffer, "  add rsp, %zu\n", size);
                                    stringbuffer_appendstring(&state->instructions, buffer);

                                    found = true;
                                    break;
                                }
                                default:
                                    printf("Unhandled assign to item!\n");
                                    exit(1);
                            }
                            break;
                        }
                        case Unresolved:
                            break;
                        default:
                            assert(false);
                    }
                }
            }

            break;
        }
        case Statement_Return: {
            Ast_Statement_Return* return_ = &statement->data.return_;
            output_expression_fasm_linux_x86_64(return_->expression, state);

            Array_Ast_Declaration* current_arguments = &state->current_procedure->data.procedure.arguments;
            Array_Ast_Type* current_returns = &state->current_procedure->data.procedure.returns;

            size_t arguments_size = 0;
            for (size_t i = 0; i < current_arguments->count; i++) {
                arguments_size += get_size(&current_arguments->elements[i].type, state);
            }

            size_t returns_size = 0;
            for (size_t i = 0; i < current_returns->count; i++) {
                returns_size += get_size(current_returns->elements[i], state);
            }

            Locals_Walk_State locals_state = {
                .total = 8,
                .output_state = state,
            };
            Ast_Walk_State walk_state = {
                .expression_func = NULL,
                .statement_func = collect_statement_locals_size,
                .internal_state = &locals_state,
            };
            walk_expression(state->current_procedure->data.procedure.body, &walk_state);
            size_t locals_size = locals_state.total;

            // rdx = old rip
            char buffer[128] = {};
            sprintf(buffer, "  mov rcx, [rsp+%zu]\n", returns_size + locals_size);
            stringbuffer_appendstring(&state->instructions, buffer);

            // rdx = old rbp
            memset(buffer, 0, 128);
            sprintf(buffer, "  mov rdx, [rsp+%zu]\n", returns_size + locals_size + 8);
            stringbuffer_appendstring(&state->instructions, buffer);

            size_t i = returns_size;
            while (i > 0) {
                if (i >= 8) {
                    i -= 8;

                    char buffer[128] = {};
                    sprintf(buffer, "  mov rax, [rsp+%zu]\n", returns_size - i - 8);
                    stringbuffer_appendstring(&state->instructions, buffer);

                    memset(buffer, 0, 128);

                    sprintf(buffer, "  mov [rsp+%zu], rax\n", returns_size - i + 8 + arguments_size + locals_size);
                    stringbuffer_appendstring(&state->instructions, buffer);
                } else {
                    i -= 1;

                    char buffer[128] = {};
                    sprintf(buffer, "  mov al, [rsp+%zu]\n", returns_size - i - 1);
                    stringbuffer_appendstring(&state->instructions, buffer);

                    memset(buffer, 0, 128);

                    sprintf(buffer, "  mov [rsp+%zu], al\n", returns_size - i + 15 + arguments_size + locals_size);
                    stringbuffer_appendstring(&state->instructions, buffer);
                }
            }

            memset(buffer, 0, 128);
            sprintf(buffer, "  add rsp, %zu\n", 16 + arguments_size + locals_size);
            stringbuffer_appendstring(&state->instructions, buffer);

            stringbuffer_appendstring(&state->instructions, "  mov rbp, rcx\n");
            stringbuffer_appendstring(&state->instructions, "  push rdx\n");
            stringbuffer_appendstring(&state->instructions, "  ret\n");

            break;
        }
        default:
            printf("Unhandled statement_type!\n");
            exit(1);
    }
}

void output_unsigned_integer(Ast_Type_Internal type, size_t value, Output_State* state) {
    if (type == Type_UInt64 || type == Type_UInt) {
        stringbuffer_appendstring(&state->instructions, "  sub rsp, 8\n");
        char buffer[128] = {};
        sprintf(buffer, "  mov rax, %zu\n", value);
        stringbuffer_appendstring(&state->instructions, buffer);
        stringbuffer_appendstring(&state->instructions, "  mov [rsp], rax\n");
    } else if (type == Type_UInt32) {
        stringbuffer_appendstring(&state->instructions, "  sub rsp, 4\n");
        char buffer[128] = {};
        sprintf(buffer, "  mov dword [rsp], %zu\n", value);
        stringbuffer_appendstring(&state->instructions, buffer);
    } else if (type == Type_UInt16) {
        stringbuffer_appendstring(&state->instructions, "  sub rsp, 2\n");
        char buffer[128] = {};
        sprintf(buffer, "  mov word [rsp], %zu\n", value);
        stringbuffer_appendstring(&state->instructions, buffer);
    } else if (type == Type_UInt8) {
        stringbuffer_appendstring(&state->instructions, "  sub rsp, 1\n");
        char buffer[128] = {};
        sprintf(buffer, "  mov byte [rsp], %zu\n", value);
        stringbuffer_appendstring(&state->instructions, buffer);
    }
}

void output_string(char* value, Output_State* state) {
    char buffer[128] = {};
    sprintf(buffer, "  push _%zu\n", state->string_index);
    stringbuffer_appendstring(&state->instructions, buffer);

    memset(buffer, 0, 128);
    sprintf(buffer, "  _%zu: db ", state->string_index);
    stringbuffer_appendstring(&state->data, buffer);

    size_t str_len = strlen(value);
    for (size_t i = 0; i < str_len; i++) {
        char buffer[128] = {};
        sprintf(buffer, "%i, ", (int) value[i]);
        stringbuffer_appendstring(&state->data, buffer);
    }

    memset(buffer, 0, 128);
    sprintf(buffer, "0\n");
    stringbuffer_appendstring(&state->data, buffer);

    state->string_index++;
}

void output_boolean(bool value, Output_State* state) {
    char buffer[128] = {};
    sprintf(buffer, "  mov rax, %i\n", value);
    stringbuffer_appendstring(&state->instructions, buffer);
    stringbuffer_appendstring(&state->instructions, "  sub rsp, 1\n");
    stringbuffer_appendstring(&state->instructions, "  mov [rsp], al\n");
}

void output_zeroes(size_t count, Output_State* state) {
    char buffer[128] = {};
    sprintf(buffer, "  sub rsp, %zu\n", count);
    stringbuffer_appendstring(&state->instructions, buffer);

    stringbuffer_appendstring(&state->instructions, "  mov rax, 0\n");

    size_t i = 0;
    while (i < count) {
        if (i + 8 < count) {
            char buffer[128] = {};
            sprintf(buffer, "  mov [rsp+%zu], rax\n", i);
            stringbuffer_appendstring(&state->instructions, buffer);
            i += 8;
        } else {
            char buffer[128] = {};
            sprintf(buffer, "  mov [rsp+%zu], al\n", i);
            stringbuffer_appendstring(&state->instructions, buffer);
            i++;
        }
    }
}

void output_build_type(Ast_Expression_Build* build, Ast_Type* type_in, Output_State* state) {
    Ast_Type type = evaluate_type_complete(type_in, &state->generic);
    switch (type.kind) {
        case Type_Struct: {
            Ast_Type_Struct* struct_ = &type.data.struct_;
            if (build->arguments.count == struct_->items.count) {
                for (int i = build->arguments.count - 1; i >= 0; i--) {
                    output_expression_fasm_linux_x86_64(build->arguments.elements[i], state);
                }
            }
            break;
        }
        case Type_Array: {
            Ast_Type_Array* array = &type.data.array;
            size_t array_size = array->size_type->data.number.value;

            if (build->arguments.count == array_size) {
                for (int i = build->arguments.count - 1; i >= 0; i--) {
                    output_expression_fasm_linux_x86_64(build->arguments.elements[i], state);
                }
            }
            break;
        }
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

void output_expression_fasm_linux_x86_64(Ast_Expression* expression, Output_State* state) {
    switch (expression->kind) {
        case Expression_Block: {
            array_size_append(&state->scoped_declares, state->current_declares.count);
            Ast_Expression_Block* block = &expression->data.block;
            for (size_t i = 0; i < block->statements.count; i++) {
                output_statement_fasm_linux_x86_64(block->statements.elements[i], state);
            }
            state->current_declares.count = state->scoped_declares.elements[state->scoped_declares.count - 1];
            state->scoped_declares.count--;
            break;
        }
        case Expression_Multiple: {
            Ast_Expression_Multiple* multiple = &expression->data.multiple;
            for (size_t i = 0; i < multiple->expressions.count; i++) {
                output_expression_fasm_linux_x86_64(multiple->expressions.elements[i], state);
            }
            break;
        }
        case Expression_Invoke: {
            Ast_Expression_Invoke* invoke = &expression->data.invoke;
            for (size_t i = 0; i < invoke->arguments.count; i++) {
                output_expression_fasm_linux_x86_64(invoke->arguments.elements[i], state);
            }

            if (invoke->kind == Invoke_Standard) {
                Ast_Expression* procedure = invoke->data.procedure;
                bool handled = false;

                if (procedure->kind == Expression_Retrieve) {
                    char* name = procedure->data.retrieve.data.identifier.name;

                    if (strncmp(name, "@syscall", 8) == 0) {
                        handled = true;

                        size_t arg_count = (size_t) atoi(name + 8);

                        if (arg_count >= 6) {
                            stringbuffer_appendstring(&state->instructions, "  pop r9\n");
                        }
                        if (arg_count >= 5) {
                            stringbuffer_appendstring(&state->instructions, "  pop r8\n");
                        }
                        if (arg_count >= 4) {
                            stringbuffer_appendstring(&state->instructions, "  pop r10\n");
                        }
                        if (arg_count >= 3) {
                            stringbuffer_appendstring(&state->instructions, "  pop rdx\n");
                        }
                        if (arg_count >= 2) {
                            stringbuffer_appendstring(&state->instructions, "  pop rsi\n");
                        }
                        if (arg_count >= 1) {
                            stringbuffer_appendstring(&state->instructions, "  pop rdi\n");
                        }
                        stringbuffer_appendstring(&state->instructions, "  pop rax\n");
                        stringbuffer_appendstring(&state->instructions, "  syscall\n");
                        stringbuffer_appendstring(&state->instructions, "  push rax\n");
                    }
                }

                if (!handled) {
                    output_expression_fasm_linux_x86_64(procedure, state);

                    stringbuffer_appendstring(&state->instructions, "  pop rax\n");
                    stringbuffer_appendstring(&state->instructions, "  call rax\n");
                }
            } else if (invoke->kind == Invoke_Operator) {
                switch (invoke->data.operator_.operator_) {
                    case Operator_Add:
                    case Operator_Subtract:
                    case Operator_Multiply:
                    case Operator_Divide:
                    case Operator_Modulus: {
                        Ast_Type operator_type = invoke->data.operator_.computed_operand_type;

                        if (is_internal_type(Type_UInt64, &operator_type) || is_internal_type(Type_UInt, &operator_type)|| is_internal_type(Type_Ptr, &operator_type)) {
                            stringbuffer_appendstring(&state->instructions, "  pop rbx\n");
                            stringbuffer_appendstring(&state->instructions, "  pop rax\n");
                            switch (invoke->data.operator_.operator_) {
                                case Operator_Add:
                                    stringbuffer_appendstring(&state->instructions, "  add rax, rbx\n");
                                    break;
                                case Operator_Subtract:
                                    stringbuffer_appendstring(&state->instructions, "  sub rax, rbx\n");
                                    break;
                                case Operator_Multiply:
                                    stringbuffer_appendstring(&state->instructions, "  mul rbx\n");
                                    break;
                                case Operator_Divide:
                                    stringbuffer_appendstring(&state->instructions, "  xor rdx, rdx\n");
                                    stringbuffer_appendstring(&state->instructions, "  div rbx\n");
                                    break;
                                case Operator_Modulus:
                                    stringbuffer_appendstring(&state->instructions, "  xor rdx, rdx\n");
                                    stringbuffer_appendstring(&state->instructions, "  div rbx\n");
                                    stringbuffer_appendstring(&state->instructions, "  mov rax, rdx\n");
                                    break;
                                default:
                                    assert(false);
                            }
                            stringbuffer_appendstring(&state->instructions, "  push rax\n");
                        } else if (is_internal_type(Type_UInt32, &operator_type)) {
                            stringbuffer_appendstring(&state->instructions, "  mov ebx, [rsp]\n");
                            stringbuffer_appendstring(&state->instructions, "  mov eax, [rsp+4]\n");
                            stringbuffer_appendstring(&state->instructions, "  add rsp, 8\n");
                            switch (invoke->data.operator_.operator_) {
                                case Operator_Add:
                                    stringbuffer_appendstring(&state->instructions, "  add eax, ebx\n");
                                    break;
                                case Operator_Subtract:
                                    stringbuffer_appendstring(&state->instructions, "  sub eax, ebx\n");
                                    break;
                                case Operator_Multiply:
                                    stringbuffer_appendstring(&state->instructions, "  mul ebx\n");
                                    break;
                                case Operator_Divide:
                                    stringbuffer_appendstring(&state->instructions, "  xor edx, edx\n");
                                    stringbuffer_appendstring(&state->instructions, "  div ebx\n");
                                    break;
                                case Operator_Modulus:
                                    stringbuffer_appendstring(&state->instructions, "  xor edx, edx\n");
                                    stringbuffer_appendstring(&state->instructions, "  div ebx\n");
                                    stringbuffer_appendstring(&state->instructions, "  mov eax, edx\n");
                                    break;
                                default:
                                    assert(false);
                            }
                            stringbuffer_appendstring(&state->instructions, "  sub rsp, 4\n");
                            stringbuffer_appendstring(&state->instructions, "  mov [rsp], eax\n");
                        } else if (is_internal_type(Type_UInt16, &operator_type)) {
                            stringbuffer_appendstring(&state->instructions, "  mov bx, [rsp]\n");
                            stringbuffer_appendstring(&state->instructions, "  mov ax, [rsp+2]\n");
                            stringbuffer_appendstring(&state->instructions, "  add rsp, 4\n");
                            switch (invoke->data.operator_.operator_) {
                                case Operator_Add:
                                    stringbuffer_appendstring(&state->instructions, "  add ax, bx\n");
                                    break;
                                case Operator_Subtract:
                                    stringbuffer_appendstring(&state->instructions, "  sub ax, bx\n");
                                    break;
                                case Operator_Multiply:
                                    stringbuffer_appendstring(&state->instructions, "  mul bx\n");
                                    break;
                                case Operator_Divide:
                                    stringbuffer_appendstring(&state->instructions, "  xor dx, dx\n");
                                    stringbuffer_appendstring(&state->instructions, "  div bx\n");
                                    break;
                                case Operator_Modulus:
                                    stringbuffer_appendstring(&state->instructions, "  xor dx, dx\n");
                                    stringbuffer_appendstring(&state->instructions, "  div bx\n");
                                    stringbuffer_appendstring(&state->instructions, "  mov ax, dx\n");
                                    break;
                                default:
                                    assert(false);
                            }
                            stringbuffer_appendstring(&state->instructions, "  sub rsp, 2\n");
                            stringbuffer_appendstring(&state->instructions, "  mov [rsp], ax\n");
                        } else if (is_internal_type(Type_UInt8, &operator_type)) {
                            stringbuffer_appendstring(&state->instructions, "  mov bl, [rsp]\n");
                            stringbuffer_appendstring(&state->instructions, "  mov al, [rsp+1]\n");
                            stringbuffer_appendstring(&state->instructions, "  add rsp, 2\n");
                            switch (invoke->data.operator_.operator_) {
                                case Operator_Add:
                                    stringbuffer_appendstring(&state->instructions, "  add al, bl\n");
                                    break;
                                case Operator_Subtract:
                                    stringbuffer_appendstring(&state->instructions, "  sub al, bl\n");
                                    break;
                                case Operator_Multiply:
                                    stringbuffer_appendstring(&state->instructions, "  mul bl\n");
                                    break;
                                case Operator_Divide:
                                    stringbuffer_appendstring(&state->instructions, "  xor dl, dl\n");
                                    stringbuffer_appendstring(&state->instructions, "  div bl\n");
                                    break;
                                case Operator_Modulus:
                                    stringbuffer_appendstring(&state->instructions, "  xor dl, dl\n");
                                    stringbuffer_appendstring(&state->instructions, "  div bl\n");
                                    stringbuffer_appendstring(&state->instructions, "  mov al, dl\n");
                                    break;
                                default:
                                    assert(false);
                            }
                            stringbuffer_appendstring(&state->instructions, "  sub rsp, 1\n");
                            stringbuffer_appendstring(&state->instructions, "  mov [rsp], al\n");
                        } else if (is_internal_type(Type_Float8, &operator_type)) {
                            stringbuffer_appendstring(&state->instructions, "  fld qword [rsp+8]\n");
                            switch (invoke->data.operator_.operator_) {
                                case Operator_Add:
                                    stringbuffer_appendstring(&state->instructions, "  fadd qword [rsp]\n");
                                    break;
                                case Operator_Subtract:
                                    stringbuffer_appendstring(&state->instructions, "  fsub qword [rsp]\n");
                                    break;
                                case Operator_Multiply:
                                    stringbuffer_appendstring(&state->instructions, "  fmul qword [rsp]\n");
                                    break;
                                case Operator_Divide:
                                    stringbuffer_appendstring(&state->instructions, "  fdiv qword [rsp]\n");
                                    break;
                                default:
                                    assert(false);
                            }
                            stringbuffer_appendstring(&state->instructions, "  fstp qword [rsp+8]\n");
                            stringbuffer_appendstring(&state->instructions, "  add rsp, 8\n");
                        } else {
                            assert(false);
                        }
                        
                        break;
                    }
                    case Operator_Equal:
                    case Operator_NotEqual:
                    case Operator_Greater:
                    case Operator_GreaterEqual:
                    case Operator_Less:
                    case Operator_LessEqual: {
                        Ast_Type operator_type = invoke->data.operator_.computed_operand_type;

                        if (is_internal_type(Type_UInt64, &operator_type) || is_internal_type(Type_UInt, &operator_type)) {
                            stringbuffer_appendstring(&state->instructions, "  xor rcx, rcx\n");
                            stringbuffer_appendstring(&state->instructions, "  mov rdx, 1\n");
                            stringbuffer_appendstring(&state->instructions, "  pop rbx\n");
                            stringbuffer_appendstring(&state->instructions, "  pop rax\n");
                            stringbuffer_appendstring(&state->instructions, "  cmp rax, rbx\n");
                            switch (invoke->data.operator_.operator_) {
                                case Operator_Equal:
                                    stringbuffer_appendstring(&state->instructions, "  cmove rcx, rdx\n");
                                    break;
                                case Operator_NotEqual:
                                    stringbuffer_appendstring(&state->instructions, "  cmovne rcx, rdx\n");
                                    break;
                                case Operator_Less:
                                    stringbuffer_appendstring(&state->instructions, "  cmovb rcx, rdx\n");
                                    break;
                                case Operator_LessEqual:
                                    stringbuffer_appendstring(&state->instructions, "  cmovbe rcx, rdx\n");
                                    break;
                                case Operator_Greater:
                                    stringbuffer_appendstring(&state->instructions, "  cmova rcx, rdx\n");
                                    break;
                                case Operator_GreaterEqual:
                                    stringbuffer_appendstring(&state->instructions, "  cmovbe rcx, rdx\n");
                                    break;
                                default:
                                    assert(false);
                            }
                            stringbuffer_appendstring(&state->instructions, "  sub rsp, 1\n");
                            stringbuffer_appendstring(&state->instructions, "  mov [rsp], cl\n");
                        } else if (is_internal_type(Type_UInt32, &operator_type)) {
                            stringbuffer_appendstring(&state->instructions, "  xor rcx, rcx\n");
                            stringbuffer_appendstring(&state->instructions, "  mov rdx, 1\n");
                            stringbuffer_appendstring(&state->instructions, "  mov ebx, [rsp]\n");
                            stringbuffer_appendstring(&state->instructions, "  mov eax, [rsp+4]\n");
                            stringbuffer_appendstring(&state->instructions, "  add rsp, 8\n");
                            stringbuffer_appendstring(&state->instructions, "  cmp eax, ebx\n");
                            switch (invoke->data.operator_.operator_) {
                                case Operator_Equal:
                                    stringbuffer_appendstring(&state->instructions, "  cmove rcx, rdx\n");
                                    break;
                                case Operator_NotEqual:
                                    stringbuffer_appendstring(&state->instructions, "  cmovne rcx, rdx\n");
                                    break;
                                case Operator_Less:
                                    stringbuffer_appendstring(&state->instructions, "  cmovb rcx, rdx\n");
                                    break;
                                case Operator_LessEqual:
                                    stringbuffer_appendstring(&state->instructions, "  cmovbe rcx, rdx\n");
                                    break;
                                case Operator_Greater:
                                    stringbuffer_appendstring(&state->instructions, "  cmova rcx, rdx\n");
                                    break;
                                case Operator_GreaterEqual:
                                    stringbuffer_appendstring(&state->instructions, "  cmovae rcx, rdx\n");
                                    break;
                                default:
                                    assert(false);
                            }
                            stringbuffer_appendstring(&state->instructions, "  sub rsp, 1\n");
                            stringbuffer_appendstring(&state->instructions, "  mov [rsp], cl\n");
                        } else if (is_internal_type(Type_UInt16, &operator_type)) {
                            stringbuffer_appendstring(&state->instructions, "  xor rcx, rcx\n");
                            stringbuffer_appendstring(&state->instructions, "  mov rdx, 1\n");
                            stringbuffer_appendstring(&state->instructions, "  mov bx, [rsp]\n");
                            stringbuffer_appendstring(&state->instructions, "  mov ax, [rsp+2]\n");
                            stringbuffer_appendstring(&state->instructions, "  add rsp, 4\n");
                            stringbuffer_appendstring(&state->instructions, "  cmp ax, bx\n");
                            switch (invoke->data.operator_.operator_) {
                                case Operator_Equal:
                                    stringbuffer_appendstring(&state->instructions, "  cmove rcx, rdx\n");
                                    break;
                                case Operator_NotEqual:
                                    stringbuffer_appendstring(&state->instructions, "  cmovne rcx, rdx\n");
                                    break;
                                case Operator_Less:
                                    stringbuffer_appendstring(&state->instructions, "  cmovb rcx, rdx\n");
                                    break;
                                case Operator_LessEqual:
                                    stringbuffer_appendstring(&state->instructions, "  cmovbe rcx, rdx\n");
                                    break;
                                case Operator_Greater:
                                    stringbuffer_appendstring(&state->instructions, "  cmova rcx, rdx\n");
                                    break;
                                case Operator_GreaterEqual:
                                    stringbuffer_appendstring(&state->instructions, "  cmovae rcx, rdx\n");
                                    break;
                                default:
                                    assert(false);
                            }
                            stringbuffer_appendstring(&state->instructions, "  sub rsp, 1\n");
                            stringbuffer_appendstring(&state->instructions, "  mov [rsp], cl\n");
                        } else if (is_internal_type(Type_UInt8, &operator_type) || is_internal_type(Type_Byte, &operator_type)) {
                            stringbuffer_appendstring(&state->instructions, "  xor rcx, rcx\n");
                            stringbuffer_appendstring(&state->instructions, "  mov rdx, 1\n");
                            stringbuffer_appendstring(&state->instructions, "  mov bl, [rsp]\n");
                            stringbuffer_appendstring(&state->instructions, "  mov al, [rsp+1]\n");
                            stringbuffer_appendstring(&state->instructions, "  add rsp, 2\n");
                            stringbuffer_appendstring(&state->instructions, "  cmp al, bl\n");
                            switch (invoke->data.operator_.operator_) {
                                case Operator_Equal:
                                    stringbuffer_appendstring(&state->instructions, "  cmove rcx, rdx\n");
                                    break;
                                case Operator_NotEqual:
                                    stringbuffer_appendstring(&state->instructions, "  cmovne rcx, rdx\n");
                                    break;
                                case Operator_Less:
                                    stringbuffer_appendstring(&state->instructions, "  cmovb rcx, rdx\n");
                                    break;
                                case Operator_LessEqual:
                                    stringbuffer_appendstring(&state->instructions, "  cmovbe rcx, rdx\n");
                                    break;
                                case Operator_Greater:
                                    stringbuffer_appendstring(&state->instructions, "  cmova rcx, rdx\n");
                                    break;
                                case Operator_GreaterEqual:
                                    stringbuffer_appendstring(&state->instructions, "  cmovae rcx, rdx\n");
                                    break;
                                default:
                                    assert(false);
                            }
                            stringbuffer_appendstring(&state->instructions, "  sub rsp, 1\n");
                            stringbuffer_appendstring(&state->instructions, "  mov [rsp], cl\n");
                        } else if (is_internal_type(Type_Float8, &operator_type)) {
                            stringbuffer_appendstring(&state->instructions, "  xor rcx, rcx\n");
                            stringbuffer_appendstring(&state->instructions, "  mov rdx, 1\n");
                            stringbuffer_appendstring(&state->instructions, "  fld qword [rsp]\n");
                            stringbuffer_appendstring(&state->instructions, "  fld qword [rsp+8]\n");
                            stringbuffer_appendstring(&state->instructions, "  fcomi st1\n");
                            switch (invoke->data.operator_.operator_) {
                                case Operator_Equal:
                                    stringbuffer_appendstring(&state->instructions, "  cmove rcx, rdx\n");
                                    break;
                                case Operator_NotEqual:
                                    stringbuffer_appendstring(&state->instructions, "  cmovne rcx, rdx\n");
                                    break;
                                case Operator_Less:
                                    stringbuffer_appendstring(&state->instructions, "  cmovb rcx, rdx\n");
                                    break;
                                case Operator_LessEqual:
                                    stringbuffer_appendstring(&state->instructions, "  cmovbe rcx, rdx\n");
                                    break;
                                case Operator_Greater:
                                    stringbuffer_appendstring(&state->instructions, "  cmova rcx, rdx\n");
                                    break;
                                case Operator_GreaterEqual:
                                    stringbuffer_appendstring(&state->instructions, "  cmovae rcx, rdx\n");
                                    break;
                                default:
                                    assert(false);
                            }
                            stringbuffer_appendstring(&state->instructions, "  fstp tword [rsp]\n");
                            stringbuffer_appendstring(&state->instructions, "  fstp tword [rsp]\n");
                            stringbuffer_appendstring(&state->instructions, "  add rsp, 16\n");
                            stringbuffer_appendstring(&state->instructions, "  sub rsp, 1\n");
                            stringbuffer_appendstring(&state->instructions, "  mov [rsp], cl\n");
                        } else if (is_enum_type(&operator_type, &state->generic)) {
                            stringbuffer_appendstring(&state->instructions, "  xor rcx, rcx\n");
                            stringbuffer_appendstring(&state->instructions, "  mov rdx, 1\n");
                            stringbuffer_appendstring(&state->instructions, "  pop rbx\n");
                            stringbuffer_appendstring(&state->instructions, "  pop rax\n");
                            stringbuffer_appendstring(&state->instructions, "  cmp rax, rbx\n");
                            switch (invoke->data.operator_.operator_) {
                                case Operator_Equal:
                                    stringbuffer_appendstring(&state->instructions, "  cmove rcx, rdx\n");
                                    break;
                                case Operator_NotEqual:
                                    stringbuffer_appendstring(&state->instructions, "  cmovne rcx, rdx\n");
                                    break;
                                default:
                                    assert(false);
                            }
                            stringbuffer_appendstring(&state->instructions, "  sub rsp, 1\n");
                            stringbuffer_appendstring(&state->instructions, "  mov [rsp], cl\n");
                        } else if (is_internal_type(Type_Ptr, &operator_type) || operator_type.kind == Type_Pointer) {
                            stringbuffer_appendstring(&state->instructions, "  xor rcx, rcx\n");
                            stringbuffer_appendstring(&state->instructions, "  mov rdx, 1\n");
                            stringbuffer_appendstring(&state->instructions, "  pop rbx\n");
                            stringbuffer_appendstring(&state->instructions, "  pop rax\n");
                            stringbuffer_appendstring(&state->instructions, "  cmp rax, rbx\n");
                            switch (invoke->data.operator_.operator_) {
                                case Operator_Equal:
                                    stringbuffer_appendstring(&state->instructions, "  cmove rcx, rdx\n");
                                    break;
                                case Operator_NotEqual:
                                    stringbuffer_appendstring(&state->instructions, "  cmovne rcx, rdx\n");
                                    break;
                                default:
                                    assert(false);
                            }
                            stringbuffer_appendstring(&state->instructions, "  sub rsp, 1\n");
                            stringbuffer_appendstring(&state->instructions, "  mov [rsp], cl\n");
                        } else {
                            assert(false);
                        }
                        
                        break;
                    }
                    case Operator_And: {
                        stringbuffer_appendstring(&state->instructions, "  xor rcx, rcx\n");
                        stringbuffer_appendstring(&state->instructions, "  mov rdx, 1\n");
                        stringbuffer_appendstring(&state->instructions, "  mov bl, [rsp]\n");
                        stringbuffer_appendstring(&state->instructions, "  mov al, [rsp+1]\n");
                        stringbuffer_appendstring(&state->instructions, "  add rsp, 2\n");
                        stringbuffer_appendstring(&state->instructions, "  and al, bl\n");
                        stringbuffer_appendstring(&state->instructions, "  sub rsp, 1\n");
                        stringbuffer_appendstring(&state->instructions, "  mov [rsp], al\n");
                        break;
                    }
                    case Operator_Or: {
                        stringbuffer_appendstring(&state->instructions, "  xor rcx, rcx\n");
                        stringbuffer_appendstring(&state->instructions, "  mov rdx, 1\n");
                        stringbuffer_appendstring(&state->instructions, "  mov bl, [rsp]\n");
                        stringbuffer_appendstring(&state->instructions, "  mov al, [rsp+1]\n");
                        stringbuffer_appendstring(&state->instructions, "  add rsp, 2\n");
                        stringbuffer_appendstring(&state->instructions, "  or al, bl\n");
                        stringbuffer_appendstring(&state->instructions, "  sub rsp, 1\n");
                        stringbuffer_appendstring(&state->instructions, "  mov [rsp], al\n");
                        break;
                    }
                    case Operator_Not: {
                        stringbuffer_appendstring(&state->instructions, "  xor rcx, rcx\n");
                        stringbuffer_appendstring(&state->instructions, "  mov rdx, 1\n");
                        stringbuffer_appendstring(&state->instructions, "  xor rax, rax\n");
                        stringbuffer_appendstring(&state->instructions, "  xor rbx, rbx\n");
                        stringbuffer_appendstring(&state->instructions, "  mov bl, [rsp]\n");
                        stringbuffer_appendstring(&state->instructions, "  add rsp, 1\n");
                        stringbuffer_appendstring(&state->instructions, "  cmp rax, rbx\n");
                        stringbuffer_appendstring(&state->instructions, "  cmove rcx, rdx\n");
                        stringbuffer_appendstring(&state->instructions, "  sub rsp, 1\n");
                        stringbuffer_appendstring(&state->instructions, "  mov [rsp], cl\n");
                        break;
                    }
                }
            }
            break;
        }
        case Expression_RunMacro: {
            Ast_RunMacro* macro = &expression->data.run_macro;
            output_expression_fasm_linux_x86_64(macro->result.data.expression, state);
            break;
        }
        case Expression_Retrieve: {
            Ast_Expression_Retrieve* retrieve = &expression->data.retrieve;
            bool found = false;

            if (!found && retrieve->kind == Retrieve_Assign_Identifier) {
                if (strcmp(retrieve->data.identifier.name, "@file") == 0) {
                    output_string(retrieve->location.file, state);
                    found = true;
                } else if (strcmp(retrieve->data.identifier.name, "@line") == 0) {
                    output_unsigned_integer(Type_UInt, retrieve->location.row, state);
                    found = true;
                }
            }

            if (!found) {
                if (retrieve->kind == Retrieve_Assign_Array) {
                    found = true;
                    Ast_Type array_type = retrieve->data.array.computed_array_type;

                    bool in_reference = consume_in_reference_output(state);

                    Ast_Type* array_ast_type_raw;
                    if (array_type.kind == Type_Pointer) {
                        array_ast_type_raw = array_type.data.pointer.child;
                    } else {
                        state->in_reference = true;
                        array_ast_type_raw = &array_type;
                    }

                    output_expression_fasm_linux_x86_64(retrieve->data.array.expression_outer, state);

                    output_expression_fasm_linux_x86_64(retrieve->data.array.expression_inner, state);

                    size_t element_size = get_size(array_ast_type_raw->data.array.element_type, state);

                    stringbuffer_appendstring(&state->instructions, "  pop rax\n");
                    stringbuffer_appendstring(&state->instructions, "  pop rcx\n");

                    char buffer[128] = {};
                    sprintf(buffer, "  mov rdx, %zu\n", element_size);
                    stringbuffer_appendstring(&state->instructions, buffer);

                    stringbuffer_appendstring(&state->instructions, "  mul rdx\n");

                    if (in_reference) {
                        stringbuffer_appendstring(&state->instructions, "  add rax, rcx\n");
                        stringbuffer_appendstring(&state->instructions, "  push rax\n");
                    } else {
                        memset(buffer, 0, 128);
                        sprintf(buffer, "  sub rsp, %zu\n", element_size);
                        stringbuffer_appendstring(&state->instructions, buffer);

                        size_t i = 0;
                        while (i < element_size) {
                            if (element_size - i >= 8) {
                                char buffer[128] = {};
                                sprintf(buffer, "  mov rbx, [rax+rcx+%zu]\n", i);
                                stringbuffer_appendstring(&state->instructions, buffer);

                                memset(buffer, 0, 128);

                                sprintf(buffer, "  mov [rsp+%zu], rbx\n", i);
                                stringbuffer_appendstring(&state->instructions, buffer);

                                i += 8;
                            } else if (element_size - i >= 1) {
                                char buffer[128] = {};
                                sprintf(buffer, "  mov bl, [rax+rcx+%zu]\n", i);
                                stringbuffer_appendstring(&state->instructions, buffer);

                                memset(buffer, 0, 128);

                                sprintf(buffer, "  mov [rsp+%zu], bl\n", i);
                                stringbuffer_appendstring(&state->instructions, buffer);

                                i += 1;
                            }
                        }
                    }
                }
            }

            if (!found) {
                if (retrieve->kind == Retrieve_Assign_Parent) {
                    bool in_reference = consume_in_reference_output(state);
                    Ast_Type parent_type = retrieve->data.parent.computed_parent_type;

                    Ast_Type* parent_type_raw;
                    if (parent_type.kind == Type_Pointer) {
                        parent_type_raw = parent_type.data.pointer.child;
                    } else {
                        state->in_reference = true;
                        parent_type_raw = &parent_type;
                    }

                    Location_Size_Data result = get_parent_item_location_size(parent_type_raw, retrieve->data.parent.name, state);
                    size_t location = result.location;
                    size_t size = result.size;

                    output_expression_fasm_linux_x86_64(retrieve->data.parent.expression, state);

                    stringbuffer_appendstring(&state->instructions, "  pop rax\n");

                    if (in_reference) {
                        char buffer[128] = {};
                        sprintf(buffer, "  add rax, %zu\n", location);
                        stringbuffer_appendstring(&state->instructions, buffer);

                        stringbuffer_appendstring(&state->instructions, "  push rax\n");
                    } else {
                        char buffer[128] = {};
                        sprintf(buffer, "  sub rsp, %zu\n", size);
                        stringbuffer_appendstring(&state->instructions, buffer);

                        size_t i = 0;
                        while (i < size) {
                            if (size - i >= 8) {
                                char buffer[128] = {};
                                sprintf(buffer, "  mov rbx, [rax+%zu]\n", location + i);
                                stringbuffer_appendstring(&state->instructions, buffer);

                                memset(buffer, 0, 128);

                                sprintf(buffer, "  mov [rsp+%zu], rbx\n", i);
                                stringbuffer_appendstring(&state->instructions, buffer);

                                i += 8;
                            } else if (size - i >= 1) {
                                char buffer[128] = {};
                                sprintf(buffer, "  mov bl, [rax+%zu]\n", location + i);
                                stringbuffer_appendstring(&state->instructions, buffer);

                                memset(buffer, 0, 128);

                                sprintf(buffer, "  mov [rsp+%zu], bl\n", i);
                                stringbuffer_appendstring(&state->instructions, buffer);

                                i += 1;
                            }
                        }
                    }

                    found = true;
                }
            }

            if (!found) {
                if (retrieve->kind == Retrieve_Assign_Identifier) {
                    char* name = retrieve->data.identifier.name;
                    if (has_local_variable(name, state)) {
                        found = true;

                        Location_Size_Data result = get_local_variable_location_size(name, state);
                        size_t location = result.location;
                        size_t size = result.size;

                        if (consume_in_reference_output(state)) {
                            char buffer[128] = {};
                            sprintf(buffer, "  lea rax, [rbp-%zu]\n", location + size);
                            stringbuffer_appendstring(&state->instructions, buffer);
                            stringbuffer_appendstring(&state->instructions, "  push rax\n");
                        } else {
                            char buffer[128] = {};
                            sprintf(buffer, "  sub rsp, %zu\n", size);
                            stringbuffer_appendstring(&state->instructions, buffer);
                                
                            size_t i = 0;
                            while (i < size) {
                                if (size - i >= 8) {
                                    char buffer[128] = {};
                                    sprintf(buffer, "  mov rax, [rbp-%zu]\n", location + size - i);
                                    stringbuffer_appendstring(&state->instructions, buffer);

                                    memset(buffer, 0, 128);

                                    sprintf(buffer, "  mov [rsp+%zu], rax\n", i);
                                    stringbuffer_appendstring(&state->instructions, buffer);

                                    i += 8;
                                } else if (size - i >= 1) {
                                    char buffer[128] = {};
                                    sprintf(buffer, "  mov al, [rbp-%zu]\n", location + size - i);
                                    stringbuffer_appendstring(&state->instructions, buffer);

                                    memset(buffer, 0, 128);

                                    sprintf(buffer, "  mov [rsp+%zu], al\n", i);
                                    stringbuffer_appendstring(&state->instructions, buffer);

                                    i += 1;
                                }
                            }
                        }
                    }
                }
            }

            if (!found) {
                if (retrieve->kind == Retrieve_Assign_Identifier) {
                    Array_Ast_Declaration* current_arguments = &state->current_procedure->data.procedure.arguments;

                    size_t location = 8;
                    size_t size = 0;
                    for (int i = current_arguments->count - 1; i >= 0; i--) {
                        Ast_Declaration* declaration = &current_arguments->elements[i];
                        size_t declaration_size = get_size(&declaration->type, state);
                        if (strcmp(declaration->name, retrieve->data.identifier.name) == 0) {
                            size = declaration_size;
                            found = true;
                            break;
                        }
                        location += declaration_size;
                    }

                    if (found) {
                        if (consume_in_reference_output(state)) {
                            char buffer[128] = {};
                            sprintf(buffer, "  lea rax, [rbp+%zu]\n", location + 8);
                            stringbuffer_appendstring(&state->instructions, buffer);
                            stringbuffer_appendstring(&state->instructions, "  push rax\n");
                        } else {
                            char buffer[128] = {};
                            sprintf(buffer, "  sub rsp, %zu\n", size);
                            stringbuffer_appendstring(&state->instructions, buffer);

                            size_t i = 0;
                            while (i < size) {
                                if (size - i >= 8) {
                                    char buffer[128] = {};
                                    sprintf(buffer, "  mov rax, [rbp+%zu]\n", location + i + 8);
                                    stringbuffer_appendstring(&state->instructions, buffer);

                                    memset(buffer, 0, 128);

                                    sprintf(buffer, "  mov [rsp+%zu], rax\n", i);
                                    stringbuffer_appendstring(&state->instructions, buffer);

                                    i += 8;
                                } else if (size - i >= 1) {
                                    char buffer[128] = {};
                                    sprintf(buffer, "  mov al, [rbp+%zu]\n", location + i + 8);
                                    stringbuffer_appendstring(&state->instructions, buffer);

                                    memset(buffer, 0, 128);

                                    sprintf(buffer, "  mov [rsp+%zu], al\n", i);
                                    stringbuffer_appendstring(&state->instructions, buffer);

                                    i += 1;
                                }
                            }
                        }
                    }
                }
            }

            if (!found && retrieve->computed_result_type != NULL && retrieve->computed_result_type->kind == Type_Enum) {
                size_t index = 0;
                Ast_Type_Enum* enum_ = &retrieve->computed_result_type->data.enum_;
                char* variant = retrieve->data.identifier.name;
                while (strcmp(enum_->items.elements[index], variant) != 0) {
                    index++;
                }

                char buffer[128] = {};
                sprintf(buffer, "  push %zu\n", index);
                stringbuffer_appendstring(&state->instructions, buffer);

                found = true;
            }

            if (!found) {
                Resolved resolved = resolve(&state->generic, retrieve->data.identifier);
                switch (resolved.kind) {
                    case Resolved_Item: {
                        Ast_Item* item = resolved.data.item;
                        found = true;
                        switch (item->kind) {
                            case Item_Procedure: {
                                char buffer[128] = {};
                                sprintf(buffer, "  push %s\n", item->data.procedure.name);
                                stringbuffer_appendstring(&state->instructions, buffer);
                                break;
                            }
                            case Item_Global: {
                                Ast_Item_Global* global = &item->data.global;
                                if (consume_in_reference_output(state)) {
                                    char buffer[128] = {};
                                    sprintf(buffer + strlen(buffer), "  lea rax, [%s.", global->name);
                                    sprintf(buffer + strlen(buffer), "%zu]\n", resolved.file->id);

                                    stringbuffer_appendstring(&state->instructions, buffer);
                                    stringbuffer_appendstring(&state->instructions, "  push rax\n");
                                } else {
                                    size_t size = get_size(&global->type, state);

                                    char buffer[128] = {};
                                    sprintf(buffer, "  sub rsp, %zu\n", size);
                                    stringbuffer_appendstring(&state->instructions, buffer);
                                        
                                    size_t i = 0;
                                    while (i < size) {
                                        if (size - i >= 8) {
                                            char buffer[128] = {};
                                            sprintf(buffer + strlen(buffer), "  mov rax, [%s+%zu]\n", global->name, i);
                                            stringbuffer_appendstring(&state->instructions, buffer);

                                            memset(buffer, 0, 128);

                                            sprintf(buffer, "  mov [rsp+%zu], rax\n", i);
                                            stringbuffer_appendstring(&state->instructions, buffer);

                                            i += 8;
                                        } else if (size - i >= 1) {
                                            char buffer[128] = {};
                                            sprintf(buffer + strlen(buffer), "  mov al, [%s+%zu]\n", global->name, i);
                                            stringbuffer_appendstring(&state->instructions, buffer);

                                            memset(buffer, 0, 128);

                                            sprintf(buffer, "  mov [rsp+%zu], al\n", i);
                                            stringbuffer_appendstring(&state->instructions, buffer);

                                            i += 1;
                                        }
                                    }
                                }
                                break;
                            }
                            case Item_Constant: {
                                Ast_Item_Constant* constant = &item->data.constant;
                                Ast_Expression_Number number = constant->expression;
                                number.type = retrieve->computed_result_type;
                                Ast_Expression expression_temp = { .kind = Expression_Number, .data = { .number = number } };
                                output_expression_fasm_linux_x86_64(&expression_temp, state);
                                break;
                            }
                            default:
                                assert(false);
                        }
                        break;
                    }
                    default:
                        assert(false);
                }

            }

            break;
        }
        case Expression_If: {
            Ast_Expression_If* node = &expression->data.if_;

            size_t end = state->flow_index;
            state->flow_index++;

            size_t else_ = state->flow_index;
            state->flow_index++;

            output_expression_fasm_linux_x86_64(node->condition, state);

            stringbuffer_appendstring(&state->instructions, "  mov rbx, 1\n");
            stringbuffer_appendstring(&state->instructions, "  xor rax, rax\n");
            stringbuffer_appendstring(&state->instructions, "  mov al, [rsp]\n");
            stringbuffer_appendstring(&state->instructions, "  add rsp, 1\n");
            stringbuffer_appendstring(&state->instructions, "  cmp rax, rbx\n");
            char buffer[128] = {};
            sprintf(buffer, "  jne __%zu\n", else_);
            stringbuffer_appendstring(&state->instructions, buffer);

            output_expression_fasm_linux_x86_64(node->if_expression, state);

            memset(buffer, 0, 128);
            sprintf(buffer, "  jmp __%zu\n", end);
            stringbuffer_appendstring(&state->instructions, buffer);

            memset(buffer, 0, 128);
            sprintf(buffer, "  __%zu:\n", else_);
            stringbuffer_appendstring(&state->instructions, buffer);

            if (node->else_expression != NULL) {
                output_expression_fasm_linux_x86_64(node->else_expression, state);
            }

            memset(buffer, 0, 128);
            sprintf(buffer, "  __%zu:\n", end);
            stringbuffer_appendstring(&state->instructions, buffer);
            break;
        }
        case Expression_While: {
            size_t end = state->flow_index;
            state->flow_index++;

            size_t start = state->flow_index;
            state->flow_index++;

            Ast_Expression_While* node = &expression->data.while_;
            char buffer[128] = {};
            sprintf(buffer, "  __%zu:\n", start);
            stringbuffer_appendstring(&state->instructions, buffer);

            output_expression_fasm_linux_x86_64(node->condition, state);

            stringbuffer_appendstring(&state->instructions, "  mov rbx, 1\n");
            stringbuffer_appendstring(&state->instructions, "  xor rax, rax\n");
            stringbuffer_appendstring(&state->instructions, "  mov al, [rsp]\n");
            stringbuffer_appendstring(&state->instructions, "  add rsp, 1\n");
            stringbuffer_appendstring(&state->instructions, "  cmp rax, rbx\n");

            memset(buffer, 0, 128);
            sprintf(buffer, "  jne __%zu\n", end);
            stringbuffer_appendstring(&state->instructions, buffer);

            output_expression_fasm_linux_x86_64(node->inside, state);

            memset(buffer, 0, 128);
            sprintf(buffer, "  jmp __%zu\n", start);
            stringbuffer_appendstring(&state->instructions, buffer);

            memset(buffer, 0, 128);
            sprintf(buffer, "  __%zu:\n", end);
            stringbuffer_appendstring(&state->instructions, buffer);
            break;
        }
        case Expression_Number: {
            Ast_Expression_Number* number = &expression->data.number;

            assert(number->type != NULL);
            Ast_Type_Internal type = number->type->data.internal;
            if (type == Type_Float8) {
                union { double d; size_t s; } value;
                switch (number->kind) {
                    case Number_Integer:
                        value.s = (double) number->value.integer;
                        break;
                    case Number_Decimal:
                        value.d = number->value.decimal;
                        break;
                    default:
                        assert(false);
                }

                stringbuffer_appendstring(&state->instructions, "  sub rsp, 8\n");
                char buffer[128] = {};
                sprintf(buffer, "  mov rax, %zu\n", value.s);
                stringbuffer_appendstring(&state->instructions, buffer);
                stringbuffer_appendstring(&state->instructions, "  mov [rsp], rax\n");
            } else {
                size_t value;
                switch (number->kind) {
                    case Number_Integer:
                        value = number->value.integer;
                        break;
                    case Number_Decimal:
                        value = (size_t) number->value.decimal;
                        break;
                    default:
                        assert(false);
                }
                output_unsigned_integer(type, value, state);
            }
            break;
        }
        case Expression_Boolean: {
            Ast_Expression_Boolean* boolean = &expression->data.boolean;
            output_boolean(boolean->value, state);
            break;
        }
        case Expression_Null: {
            stringbuffer_appendstring(&state->instructions, "  push 0\n");
            break;
        }
        case Expression_String: {
            Ast_Expression_String* string = &expression->data.string;
            output_string(string->value, state);
            break;
        }
        case Expression_Char: {
            Ast_Expression_Char* char_ = &expression->data.char_;
            stringbuffer_appendstring(&state->instructions, "  sub rsp, 1\n");
            char buffer[128] = {};
            sprintf(buffer, "  mov byte [rsp], %i\n", char_->value);
            stringbuffer_appendstring(&state->instructions, buffer);
            break;
        }
        case Expression_Reference: {
            Ast_Expression_Reference* reference = &expression->data.reference;
            state->in_reference = true;

            output_expression_fasm_linux_x86_64(reference->inner, state);
            break;
        }
        case Expression_Cast: {
            Ast_Expression_Cast* cast = &expression->data.cast;

            output_expression_fasm_linux_x86_64(cast->expression, state);

            Ast_Type input = cast->computed_input_type;
            Ast_Type output = cast->type;

            if (cast->type.kind == Type_Internal && cast->computed_input_type.kind == Type_Internal) {
                Ast_Type_Internal output_internal = output.data.internal;
                Ast_Type_Internal input_internal = input.data.internal;

                if ((input_internal == Type_UInt || input_internal == Type_UInt64 || input_internal == Type_UInt32 || input_internal == Type_UInt16 || input_internal == Type_UInt8) && 
                        (output_internal == Type_UInt || output_internal == Type_UInt64 || output_internal == Type_UInt32 || output_internal == Type_UInt16 || output_internal == Type_UInt8)) {
                    size_t input_size = get_size(&cast->computed_input_type, state);
                    if (input_size == 8) {
                        stringbuffer_appendstring(&state->instructions, "  pop rax\n");
                    } else if (input_size == 4) {
                        stringbuffer_appendstring(&state->instructions, "  xor rax, rax\n");
                        stringbuffer_appendstring(&state->instructions, "  mov eax, [rsp]\n");
                        stringbuffer_appendstring(&state->instructions, "  add rsp, 4\n");
                    } else if (input_size == 2) {
                        stringbuffer_appendstring(&state->instructions, "  xor rax, rax\n");
                        stringbuffer_appendstring(&state->instructions, "  mov ax, [rsp]\n");
                        stringbuffer_appendstring(&state->instructions, "  add rsp, 2\n");
                    } else if (input_size == 1) {
                        stringbuffer_appendstring(&state->instructions, "  xor rax, rax\n");
                        stringbuffer_appendstring(&state->instructions, "  mov al, [rsp]\n");
                        stringbuffer_appendstring(&state->instructions, "  add rsp, 1\n");
                    }

                    size_t output_size = get_size(&cast->type, state);
                    if (output_size == 8) {
                        stringbuffer_appendstring(&state->instructions, "  push rax\n");
                    } else if (output_size == 4) {
                        stringbuffer_appendstring(&state->instructions, "  sub rsp, 4\n");
                        stringbuffer_appendstring(&state->instructions, "  mov [rsp], eax\n");
                    } else if (output_size == 2) {
                        stringbuffer_appendstring(&state->instructions, "  sub rsp, 2\n");
                        stringbuffer_appendstring(&state->instructions, "  mov [rsp], ax\n");
                    } else if (output_size == 1) {
                        stringbuffer_appendstring(&state->instructions, "  sub rsp, 1\n");
                        stringbuffer_appendstring(&state->instructions, "  mov [rsp], al\n");
                    }
                } else if (input_internal == Type_Float8 && output_internal == Type_UInt64) {
                    stringbuffer_appendstring(&state->instructions, "  fld qword [rsp]\n");
                    stringbuffer_appendstring(&state->instructions, "  fisttp qword [rsp]\n");
                } else if (input.kind == Type_Internal && input.data.internal == Type_Byte && output.kind == Type_Internal && output.data.internal == Type_UInt8) {
                } else if (input.kind == Type_Internal && input.data.internal == Type_UInt8 && output.kind == Type_Internal && output.data.internal == Type_Byte) {
                } else {
                    assert(false);
                }
            } else if (input.kind == Type_Internal && input.data.internal == Type_Ptr && output.kind == Type_Pointer) {
            } else if (input.kind == Type_Pointer && output.kind == Type_Internal && output.data.internal == Type_Ptr) {
            } else {
                assert(false);
            }

            break;
        }
        case Expression_Init: {
            Ast_Expression_Init* init = &expression->data.init;

            Ast_Type* type = &init->type;
            output_zeroes(get_size(type, state), state);
            break;
        }
        case Expression_Build: {
            Ast_Expression_Build* build = &expression->data.build;

            Ast_Type* type = &build->type;
            output_build_type(build, type, state);
            break;
        }
        case Expression_SizeOf: {
            Ast_Expression_SizeOf* size_of = &expression->data.size_of;

            output_unsigned_integer(size_of->computed_result_type.data.internal, get_size(&size_of->type, state), state);
            break;
        }
        case Expression_LengthOf: {
            Ast_Expression_LengthOf* length_of = &expression->data.length_of;

            output_unsigned_integer(length_of->computed_result_type.data.internal, get_length(&length_of->type), state);
            break;
        }
        default:
            assert(false);
    }
}

void output_item_fasm_linux_x86_64(Ast_Item* item, Output_State* state) {
    if (has_directive(&item->directives, Directive_If)) {
        Ast_Directive_If* if_node = &get_directive(&item->directives, Directive_If)->data.if_;
        if (!if_node->result) {
            return;
        }
    }

    switch (item->kind) {
        case Item_Procedure: {
            Ast_Item_Procedure* procedure = &item->data.procedure;
            // TODO: use some sort of annotation to specify entry procedure

            state->current_declares = array_ast_declaration_new(4);
            state->current_procedure = item;

            char buffer[128] = {};
            sprintf(buffer, "%s:\n", procedure->name);
            stringbuffer_appendstring(&state->instructions, buffer);

            stringbuffer_appendstring(&state->instructions, "  push rbp\n");
            stringbuffer_appendstring(&state->instructions, "  mov rbp, rsp\n");

            Locals_Walk_State locals_state = {
                .total = 8,
                .output_state = state,
            };
            Ast_Walk_State walk_state = {
                .expression_func = NULL,
                .statement_func = collect_statement_locals_size,
                .internal_state = &locals_state,
            };
            walk_expression(procedure->body, &walk_state);
            size_t locals_size = locals_state.total;

            memset(buffer, 0, 128);
            sprintf(buffer, "  sub rsp, %zu\n", locals_size);
            stringbuffer_appendstring(&state->instructions, buffer);

            output_expression_fasm_linux_x86_64(procedure->body, state);

            size_t arguments_size = 0;
            for (size_t i = 0; i < procedure->arguments.count; i++) {
                arguments_size += get_size(&procedure->arguments.elements[i].type, state);
            }

            stringbuffer_appendstring(&state->instructions, "  mov rsp, rbp\n");
            stringbuffer_appendstring(&state->instructions, "  mov rcx, [rsp+0]\n");
            stringbuffer_appendstring(&state->instructions, "  mov rdx, [rsp+8]\n");
            memset(buffer, 0, 128);
            sprintf(buffer, "  add rsp, %zu\n", arguments_size + 16);
            stringbuffer_appendstring(&state->instructions, buffer);
            stringbuffer_appendstring(&state->instructions, "  mov rbp, rcx\n");
            stringbuffer_appendstring(&state->instructions, "  push rdx\n");
            stringbuffer_appendstring(&state->instructions, "  ret\n");
            break;
        }
        case Item_Global: {
            Ast_Item_Global* global = &item->data.global;
            size_t size = get_size(&global->type, state);

            char buffer[128] = {};
            sprintf(buffer, "%s: rb %zu\n", global->name, size);
            stringbuffer_appendstring(&state->bss, buffer);
            break;
        }
        case Item_Constant:
            break;
        case Item_Type:
            break;
        case Item_Macro:
            break;
        default:
            assert(false);
    }
}

void output_fasm_linux_x86_64(Program* program, char* output_file, Array_String* package_names, Array_String* package_paths) {
    Output_State state = (Output_State) {
        .generic = (Generic_State) {
            .program = program,
            .current_file = NULL,
            .package_names = package_names,
            .package_paths = package_paths,
        },
        .instructions = stringbuffer_new(16384),
        .data = stringbuffer_new(16384),
        .bss = stringbuffer_new(16384),
        .string_index = 0,
        .flow_index = 0,
        .current_declares = {},
        .scoped_declares = array_size_new(8),
        .current_procedure = NULL,
        .in_reference = false,
    };

    for (size_t j = 0; j < program->count; j++) {
        Ast_File* file_node = &program->elements[j];
        state.generic.current_file = file_node;

        for (size_t i = 0; i < file_node->items.count; i++) {
            Ast_Item* item = &file_node->items.elements[i];
            output_item_fasm_linux_x86_64(item, &state);
        }
    }

    FILE* file = fopen("temp.fasm", "w");

    fprintf(file, "format ELF64 executable\n");
    fprintf(file, "segment readable executable\n");
    fprintf(file, "  lea rbx, [rsp+8] \n");
    fprintf(file, "  push rbx\n");
    fprintf(file, "  call main\n");
    fprintf(file, "  mov rax, 60\n");
    fprintf(file, "  mov rdi, 0\n");
    fprintf(file, "  syscall\n");

    fwrite(state.instructions.elements, state.instructions.count, 1, file);

    fprintf(file, "segment readable\n");
    fwrite(state.data.elements, state.data.count, 1, file);

    fprintf(file, "segment readable writable\n");
    fwrite(state.bss.elements, state.bss.count, 1, file);

    fclose(file);

    int result = fork();
    if (result == 0) {
        close(1);
        char* args[] = {"fasm", "temp.fasm", output_file, NULL};
        char* env[] = {NULL};

        execve("/bin/fasm", args, env);
        exit(1);
    }

    waitpid(result, NULL, 0);

    remove("temp.fasm");
}
