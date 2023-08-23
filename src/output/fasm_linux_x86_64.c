#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "fasm_linux_x86_64.h"

typedef struct {
    Generic_State generic;
    String_Buffer instructions;
    String_Buffer data;
    String_Buffer bss;
    size_t string_index;
    size_t flow_index;
    Array_Declaration current_declares;
    Array_Size scoped_declares;
    Array_Declaration current_arguments;
    Array_Type current_returns;
    Expression_Node* current_body;
    Module_Node* current_module;
    bool in_reference;
} Output_State;

size_t get_size(Type* type, Generic_State* state) {
    switch (type->kind) {
        case Type_Basic: {
            Basic_Type* basic = &type->data.basic;
            Identifier identifier = basic_type_to_identifier(*basic);
            Resolved resolved = resolve(state, identifier);
            if (resolved.kind == Resolved_Item) {
                Item_Node* item = resolved.data.item;
                Type_Node* type = &item->data.type;
                switch (type->kind) {
                    case Type_Node_Struct: {
                        size_t size = 0;
                        Struct_Node* struct_ = &type->data.struct_;
                        for (size_t i = 0; i < struct_->items.count; i++) {
                            size += get_size(&struct_->items.elements[i].type, state);
                        }
                        return size;
                    }
                    case Type_Node_Union: {
                        size_t size = 0;
                        Union_Node* union_ = &type->data.union_;
                        for (size_t i = 0; i < union_->items.count; i++) {
                            size_t size_temp = get_size(&union_->items.elements[i].type, state);
                            if (size_temp > size) {
                                size = size_temp;
                            }
                        }
                        return size;
                    }
                    case Type_Node_Enum: {
                        return 8;
                    }
                }
            }
            break;
        }
        case Type_Array: {
            BArray_Type* array = &type->data.array;

            if (array->has_size) {
                size_t size = array->size;
                return size * get_size(array->element_type, state);
            }
            break;
        }
        case Type_Internal: {
            Internal_Type* internal = &type->data.internal;

            switch (*internal) {
                case Type_USize:
                case Type_U8:
                case Type_F8:
                    return 8;
                case Type_U4:
                    return 4;
                case Type_U2:
                    return 2;
                case Type_U1:
                    return 1;
            }
            break;
        }
        case Type_Pointer: {
            return 8;
        }
        case Type_Struct: {
            size_t size = 0;
            Struct_Type* struct_ = &type->data.struct_;
            for (size_t i = 0; i < struct_->items.count; i++) {
                size += get_size(&struct_->items.elements[i]->type, state);
            }
            return size;
        }
        case Type_Union: {
            size_t size = 0;
            Union_Type* union_ = &type->data.union_;
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

    printf("Unhandled type!\n");
    exit(1);
}

size_t collect_expression_locals_size(Expression_Node* expression, Output_State* state);

size_t collect_statement_locals_size(Statement_Node* statement, Output_State* state) {
    switch (statement->kind) {
        case Statement_Expression: {
            return collect_expression_locals_size(statement->data.expression.expression, state);
        }
        case Statement_Declare: {
            size_t size = 0;
            for (size_t i = 0; i < statement->data.declare.declarations.count; i++) {
                size += get_size(&statement->data.declare.declarations.elements[i].type, &state->generic);
            }
            return size;
        } 
        default:
            return 0;
    }
}

size_t collect_expression_locals_size(Expression_Node* expression, Output_State* state) {
    switch (expression->kind) {
        case Expression_Block: {
            size_t size = 0;
            for (size_t i = 0; i < expression->data.block.statements.count; i++) {
                size += collect_statement_locals_size(expression->data.block.statements.elements[i], state);
            }
            return size;
        }
        case Expression_If: {
            return collect_expression_locals_size(expression->data.if_.inside, state);
        }
        case Expression_While: {
            return collect_expression_locals_size(expression->data.while_.inside, state);
        }
        default:
            return 0;
    }
}

bool consume_in_reference_output(Output_State* state) {
    bool cached = state->in_reference;
    state->in_reference = false;
    return cached;
}

void output_expression_fasm_linux_x86_64(Expression_Node* expression, Output_State* state);

typedef struct {
    size_t size;
    size_t location;
} Location_Size_Data;

Location_Size_Data get_parent_item_location_size(Type* parent_type, char* item_name, Generic_State* state) {
    Location_Size_Data result = {};

    switch (parent_type->kind) {
        case Type_Basic: {
            Identifier identifier = basic_type_to_identifier(parent_type->data.basic);
            Resolved resolved = resolve(state, identifier);

            assert(resolved.kind == Resolved_Item);

            Item_Node* item = resolved.data.item;
            switch (item->data.type.kind) {
                case Type_Node_Struct: {
                    Struct_Node* struct_ = &item->data.type.data.struct_;
                    for (size_t i = 0; i < struct_->items.count; i++) {
                        Declaration* declaration = &struct_->items.elements[i];
                        size_t item_size = get_size(&declaration->type, state);
                        if (strcmp(declaration->name, item_name) == 0) {
                            result.size = item_size;
                            break;
                        }
                        result.location += item_size;
                    }
                    break;
                }
                case Type_Node_Union: {
                    Union_Node* union_ = &item->data.type.data.union_;
                    for (size_t i = 0; i < union_->items.count; i++) {
                        Declaration* declaration = &union_->items.elements[i];
                        size_t item_size = get_size(&declaration->type, state);
                        if (strcmp(declaration->name, item_name) == 0) {
                            result.size = item_size;
                            break;
                        }
                    }

                    result.location = 0;
                    break;
                }
                case Type_Node_Enum:
                    assert(false);
            }
            break;
        }
        case Type_Struct: {
            Struct_Type* struct_type = &parent_type->data.struct_;
            for (size_t i = 0; i < struct_type->items.count; i++) {
                Declaration* declaration = struct_type->items.elements[i];
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
            Union_Type* union_type = &parent_type->data.union_;
            for (size_t i = 0; i < union_type->items.count; i++) {
                Declaration* declaration = union_type->items.elements[i];
                size_t item_size = get_size(&declaration->type, state);
                if (strcmp(declaration->name, item_name) == 0) {
                    result.size = item_size;
                    break;
                }
                result.location += item_size;
            }
            break;
        }
        default:
            assert(false);
    }

    return result;
}

void output_statement_fasm_linux_x86_64(Statement_Node* statement, Output_State* state) {
    switch (statement->kind) {
        case Statement_Expression: {
            Statement_Expression_Node* statement_expression = &statement->data.expression;
            output_expression_fasm_linux_x86_64(statement_expression->expression, state);
            break;
        }
        case Statement_Declare: {
            Statement_Declare_Node* declare = &statement->data.declare;
            if (declare->expression != NULL) {
                output_expression_fasm_linux_x86_64(declare->expression, state);

                for (int i = declare->declarations.count - 1; i >= 0; i--) {
                    Declaration declaration = declare->declarations.elements[i];
                    size_t location = 8;
                    for (size_t j = 0; j < state->current_declares.count; j++) {
                        location += get_size(&state->current_declares.elements[j].type, &state->generic);
                    }
                    size_t size = get_size(&declaration.type, &state->generic);

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
            output_expression_fasm_linux_x86_64(assign->expression, state);

            for (int i = assign->parts.count - 1; i >= 0; i--) {
                Statement_Assign_Part* assign_part = &assign->parts.elements[i];
                bool found = false;

                if (!found && assign_part->kind == Retrieve_Assign_Array) {
                    Type array_type = assign_part->data.array.computed_array_type;

                    Type* array_type_raw;
                    if (array_type.kind == Type_Pointer) {
                        array_type_raw = array_type.data.pointer.child;
                    } else {
                        state->in_reference = true;
                        array_type_raw = &array_type;
                    }

                    output_expression_fasm_linux_x86_64(assign_part->data.array.expression_outer, state);

                    output_expression_fasm_linux_x86_64(assign_part->data.array.expression_inner, state);

                    size_t size = get_size(array_type_raw->data.array.element_type, &state->generic);

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
                    Type parent_type = assign_part->data.parent.computed_parent_type;

                    Type* parent_type_raw;
                    if (parent_type.kind == Type_Pointer) {
                        parent_type_raw = parent_type.data.pointer.child;
                    } else {
                        state->in_reference = true;
                        parent_type_raw = &parent_type;
                    }

                    size_t location = 0;
                    size_t size = 0;

                    if (strcmp(assign_part->data.parent.name, "*") == 0) {
                        size = get_size(parent_type_raw, &state->generic);
                    } else {
                        Location_Size_Data result = get_parent_item_location_size(parent_type_raw, assign_part->data.parent.name, &state->generic);
                        location = result.location;
                        size = result.size;
                    }

                    output_expression_fasm_linux_x86_64(assign_part->data.parent.expression, state);

                    stringbuffer_appendstring(&state->instructions, "  pop rax\n");
                        
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
                    char* name = assign_part->data.identifier.data.single;

                    size_t location = 8;
                    size_t size = 0;
                    for (size_t j = 0; j < state->current_declares.count; j++) {
                        size_t declare_size = get_size(&state->current_declares.elements[j].type, &state->generic);
                        if (strcmp(state->current_declares.elements[j].name, name) == 0) {
                            size = declare_size;
                            break;
                        }
                        location += declare_size;
                    }

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

                if (!found && assign_part->kind == Retrieve_Assign_Identifier) {
                    Resolved resolved = resolve(&state->generic, assign_part->data.identifier);
                    switch (resolved.kind) {
                        case Resolved_Item: {
                            Item_Node* item = resolved.data.item;
                            switch (item->kind) {
                                case Item_Global: {
                                    Global_Node* global = &item->data.global;
                                    size_t size = get_size(&global->type, &state->generic);

                                    size_t i = 0;
                                    while (i < size) {
                                        if (size - i >= 8) {
                                            char buffer[128] = {};
                                            sprintf(buffer, "  mov rax, [rsp+%zu]\n", i);
                                            stringbuffer_appendstring(&state->instructions, buffer);

                                            memset(buffer, 0, 128);

                                            sprintf(buffer + strlen(buffer), "  mov [%s.", item->name);
                                            if (resolved.parent_module != NULL) {
                                                sprintf(buffer + strlen(buffer), "%zu.", resolved.parent_module->id);
                                            }
                                            sprintf(buffer + strlen(buffer), "%zu+%zu], rax\n", resolved.file->id, i);
                                            stringbuffer_appendstring(&state->instructions, buffer);

                                            i += 8;
                                        } else if (size - i >= 1) {
                                            char buffer[128] = {};
                                            sprintf(buffer, "  mov al, [rsp+%zu]\n", i);
                                            stringbuffer_appendstring(&state->instructions, buffer);

                                            memset(buffer, 0, 128);

                                            sprintf(buffer + strlen(buffer), "  mov [%s.", item->name);
                                            if (resolved.parent_module != NULL) {
                                                sprintf(buffer + strlen(buffer), "%zu.", resolved.parent_module->id);
                                            }
                                            sprintf(buffer + strlen(buffer), "%zu+%zu], al\n", resolved.file->id, i);
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
            Statement_Return_Node* return_ = &statement->data.return_;
            output_expression_fasm_linux_x86_64(return_->expression, state);

            size_t arguments_size = 0;
            for (size_t i = 0; i < state->current_arguments.count; i++) {
                arguments_size += get_size(&state->current_arguments.elements[i].type, &state->generic);
            }

            size_t returns_size = 0;
            for (size_t i = 0; i < state->current_returns.count; i++) {
                returns_size += get_size(state->current_returns.elements[i], &state->generic);
            }

            size_t locals_size = 8 + collect_expression_locals_size(state->current_body, state);

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

            stringbuffer_appendstring(&state->instructions, "  mov rsp, rbp\n");


            if ((int) returns_size - (int) arguments_size + 16 < 0) {
                char buffer[128];
                sprintf(buffer, "  sub rsp, %zu\n", -(returns_size - arguments_size + 16));
                stringbuffer_appendstring(&state->instructions, buffer);
            } else if ((int) returns_size - (int) arguments_size + 16 > 0) {
                char buffer[128];
                sprintf(buffer, "  add rsp, %zu\n", arguments_size - returns_size + 16);
                stringbuffer_appendstring(&state->instructions, buffer);
            }

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

void output_unsigned_integer(Internal_Type type, size_t value, Output_State* state) {
    if (type == Type_U8 || type == Type_USize) {
        stringbuffer_appendstring(&state->instructions, "  sub rsp, 8\n");
        char buffer[128] = {};
        sprintf(buffer, "  mov rax, %zu\n", value);
        stringbuffer_appendstring(&state->instructions, buffer);
        stringbuffer_appendstring(&state->instructions, "  mov [rsp], rax\n");
    } else if (type == Type_U4) {
        stringbuffer_appendstring(&state->instructions, "  sub rsp, 4\n");
        char buffer[128] = {};
        sprintf(buffer, "  mov dword [rsp], %zu\n", value);
        stringbuffer_appendstring(&state->instructions, buffer);
    } else if (type == Type_U2) {
        stringbuffer_appendstring(&state->instructions, "  sub rsp, 2\n");
        char buffer[128] = {};
        sprintf(buffer, "  mov word [rsp], %zu\n", value);
        stringbuffer_appendstring(&state->instructions, buffer);
    } else if (type == Type_U1) {
        stringbuffer_appendstring(&state->instructions, "  sub rsp, 1\n");
        char buffer[128] = {};
        sprintf(buffer, "  mov byte [rsp], %zu\n", value);
        stringbuffer_appendstring(&state->instructions, buffer);
    }
}

bool is_enum_type(Type* type, Generic_State* generic_state) {
    if (type->kind == Type_Basic) {
        Resolved resolved = resolve(generic_state, basic_type_to_identifier(type->data.basic));
        if (resolved.kind == Resolved_Item && resolved.data.item->kind == Item_Type && resolved.data.item->data.type.kind == Type_Node_Enum) {
            return true;
        }
    } else if (type->kind == Type_Enum) {
        return true;
    }
    return false;
}

void output_expression_fasm_linux_x86_64(Expression_Node* expression, Output_State* state) {
    switch (expression->kind) {
        case Expression_Block: {
            array_size_append(&state->scoped_declares, state->current_declares.count);
            Block_Node* block = &expression->data.block;
            for (size_t i = 0; i < block->statements.count; i++) {
                output_statement_fasm_linux_x86_64(block->statements.elements[i], state);
            }
            state->current_declares.count = state->scoped_declares.elements[state->scoped_declares.count - 1];
            state->scoped_declares.count--;
            break;
        }
        case Expression_Multi: {
            Multi_Expression_Node* multi = &expression->data.multi;
            for (size_t i = 0; i < multi->expressions.count; i++) {
                output_expression_fasm_linux_x86_64(multi->expressions.elements[i], state);
            }
            break;
        }
        case Expression_Invoke: {
            Invoke_Node* invoke = &expression->data.invoke;
            for (size_t i = 0; i < invoke->arguments.count; i++) {
                output_expression_fasm_linux_x86_64(invoke->arguments.elements[i], state);
            }

            if (invoke->kind == Invoke_Standard) {
                Expression_Node* procedure = invoke->data.procedure;
                bool handled = false;

                if (procedure->kind == Expression_Retrieve) {
                    char* name = procedure->data.retrieve.data.identifier.data.single;

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
                        Type operator_type = invoke->data.operator_.computed_operand_type;

                        if (is_internal_type(Type_U8, &operator_type) || is_internal_type(Type_USize, &operator_type)) {
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
                        } else if (is_internal_type(Type_U4, &operator_type)) {
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
                        } else if (is_internal_type(Type_U2, &operator_type)) {
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
                        } else if (is_internal_type(Type_U1, &operator_type)) {
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
                        } else if (is_internal_type(Type_F8, &operator_type)) {
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
                        Type operator_type = invoke->data.operator_.computed_operand_type;

                        if (is_internal_type(Type_U8, &operator_type) || is_internal_type(Type_USize, &operator_type)) {
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
                                    stringbuffer_appendstring(&state->instructions, "  cmovba rcx, rdx\n");
                                    break;
                                default:
                                    assert(false);
                            }
                            stringbuffer_appendstring(&state->instructions, "  sub rsp, 1\n");
                            stringbuffer_appendstring(&state->instructions, "  mov [rsp], cl\n");
                        } else if (is_internal_type(Type_U4, &operator_type)) {
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
                                    stringbuffer_appendstring(&state->instructions, "  cmovba rcx, rdx\n");
                                    break;
                                default:
                                    assert(false);
                            }
                            stringbuffer_appendstring(&state->instructions, "  sub rsp, 1\n");
                            stringbuffer_appendstring(&state->instructions, "  mov [rsp], cl\n");
                        } else if (is_internal_type(Type_U2, &operator_type)) {
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
                                    stringbuffer_appendstring(&state->instructions, "  cmovba rcx, rdx\n");
                                    break;
                                default:
                                    assert(false);
                            }
                            stringbuffer_appendstring(&state->instructions, "  sub rsp, 1\n");
                            stringbuffer_appendstring(&state->instructions, "  mov [rsp], cl\n");
                        } else if (is_internal_type(Type_U1, &operator_type)) {
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
                                    stringbuffer_appendstring(&state->instructions, "  cmovba rcx, rdx\n");
                                    break;
                                default:
                                    assert(false);
                            }
                            stringbuffer_appendstring(&state->instructions, "  sub rsp, 1\n");
                            stringbuffer_appendstring(&state->instructions, "  mov [rsp], cl\n");
                        } else if (is_internal_type(Type_F8, &operator_type)) {
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
                                    stringbuffer_appendstring(&state->instructions, "  cmovba rcx, rdx\n");
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
                        } else {
                            assert(false);
                        }
                        
                        break;
                    }
                }
            }
            break;
        }
        case Expression_Retrieve: {
            Retrieve_Node* retrieve = &expression->data.retrieve;
            bool found = false;

            if (!found) {
                if (retrieve->kind == Retrieve_Assign_Array) {
                    Type array_type = retrieve->data.array.computed_array_type;

                    bool in_reference = consume_in_reference_output(state);

                    Type* array_type_raw;
                    if (array_type.kind == Type_Pointer) {
                        array_type_raw = array_type.data.pointer.child;
                    } else {
                        state->in_reference = true;
                        array_type_raw = &array_type;
                    }

                    output_expression_fasm_linux_x86_64(retrieve->data.array.expression_outer, state);

                    output_expression_fasm_linux_x86_64(retrieve->data.array.expression_inner, state);

                    size_t element_size = get_size(array_type_raw->data.array.element_type, &state->generic);

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
                    Type parent_type = retrieve->data.parent.computed_parent_type;

                    Type* parent_type_raw;
                    if (parent_type.kind == Type_Pointer) {
                        parent_type_raw = parent_type.data.pointer.child;
                    } else {
                        state->in_reference = true;
                        parent_type_raw = &parent_type;
                    }

                    size_t location = 0;
                    size_t size = 0;
                    if (strcmp(retrieve->data.parent.name, "*") == 0) {
                        size = get_size(parent_type_raw, &state->generic);
                    } else {
                        Location_Size_Data result = get_parent_item_location_size(parent_type_raw, retrieve->data.parent.name, &state->generic);
                        location = result.location;
                        size = result.size;
                    }

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
                    size_t location = 8;
                    size_t size = 0;
                    for (int i = state->current_declares.count - 1; i >= 0; i--) {
                        Declaration* declaration = &state->current_declares.elements[i];
                        size_t declaration_size = get_size(&declaration->type, &state->generic);

                        if (found) {
                            location += declaration_size;
                        }

                        if (strcmp(declaration->name, retrieve->data.identifier.data.single) == 0) {
                            size = declaration_size;
                            found = true;
                        }
                    }

                    if (found) {
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
                    size_t location = 8;
                    size_t size = 0;
                    for (int i = state->current_arguments.count - 1; i >= 0; i--) {
                        Declaration* declaration = &state->current_arguments.elements[i];
                        size_t declaration_size = get_size(&declaration->type, &state->generic);
                        if (strcmp(declaration->name, retrieve->data.identifier.data.single) == 0) {
                            size = declaration_size;
                            found = true;
                            break;
                        }
                        location += declaration_size;
                    }

                    if (found) {
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

            //if (!found) {
            //    if (retrieve->kind == Retrieve_Assign_Identifier) {
            //        if (retrieve->data.identifier.kind == Identifier_Multi && strcmp(retrieve->data.identifier.data.multi.elements[0], "") == 0) {
            //            char* enum_variant = retrieve->data.identifier.data.multi.elements[1];

            //            Enum_Type* enum_type = &retrieve->computed_result_type.data.enum_;
            //            for (size_t i = 0; i < enum_type->items.count; i++) {
            //                if (strcmp(enum_variant, enum_type->items.elements[i]) == 0) {
            //                    //retrieve->computed_result_type = *state->wanted_type;
            //                    //stack_type_push(&state->stack, *state->wanted_type);
            //                    //found = true;
            //                }
            //            }
            //        }
            //    }
            //}

            if (!found) {
                Resolved resolved = resolve(&state->generic, retrieve->data.identifier);
                switch (resolved.kind) {
                    case Resolved_Item: {
                        Item_Node* item = resolved.data.item;
                        found = true;
                        switch (item->kind) {
                            case Item_Procedure: {
                                char buffer[128] = {};
                                sprintf(buffer + strlen(buffer), "  push %s.", item->name);
                                if (resolved.parent_module != NULL) {
                                    sprintf(buffer + strlen(buffer), "%zu.", resolved.parent_module->id);
                                }
                                sprintf(buffer + strlen(buffer), "%zu\n", resolved.file->id);
                                stringbuffer_appendstring(&state->instructions, buffer);
                                break;
                            }
                            case Item_Global: {
                                Global_Node* global = &item->data.global;
                                if (consume_in_reference_output(state)) {
                                    char buffer[128] = {};
                                    sprintf(buffer + strlen(buffer), "  lea rax, [%s.", item->name);
                                    if (resolved.parent_module != NULL) {
                                        sprintf(buffer + strlen(buffer), "%zu.", resolved.parent_module->id);
                                    }
                                    sprintf(buffer + strlen(buffer), "%zu]\n", resolved.file->id);

                                    stringbuffer_appendstring(&state->instructions, buffer);
                                    stringbuffer_appendstring(&state->instructions, "  push rax\n");
                                } else {
                                    size_t size = get_size(&global->type, &state->generic);

                                    char buffer[128] = {};
                                    sprintf(buffer, "  sub rsp, %zu\n", size);
                                    stringbuffer_appendstring(&state->instructions, buffer);
                                        
                                    size_t i = 0;
                                    while (i < size) {
                                        if (size - i >= 8) {
                                            char buffer[128] = {};
                                            sprintf(buffer + strlen(buffer), "  mov rax, [%s.", item->name);
                                            if (resolved.parent_module != NULL) {
                                                sprintf(buffer + strlen(buffer), "%zu.", resolved.parent_module->id);
                                            }
                                            sprintf(buffer + strlen(buffer), "%zu+%zu]\n", resolved.file->id, i);
                                            stringbuffer_appendstring(&state->instructions, buffer);

                                            memset(buffer, 0, 128);

                                            sprintf(buffer, "  mov [rsp+%zu], rax\n", i);
                                            stringbuffer_appendstring(&state->instructions, buffer);

                                            i += 8;
                                        } else if (size - i >= 1) {
                                            char buffer[128] = {};
                                            sprintf(buffer + strlen(buffer), "  mov al, [%s.", item->name);
                                            if (resolved.parent_module != NULL) {
                                                sprintf(buffer + strlen(buffer), "%zu.", resolved.parent_module->id);
                                            }
                                            sprintf(buffer + strlen(buffer), "%zu+%zu]\n", resolved.file->id, i);
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
                            default:
                                printf("Unhandled item retrieve!\n");
                                exit(1);
                        }
                        break;
                    }
                    case Resolved_Enum_Variant: {
                        size_t index = 0;
                        Enum_Node* enum_ = resolved.data.enum_.enum_;
                        if (enum_ == NULL) {
                            Enum_Type* enum_2 = &retrieve->computed_result_type.data.enum_;

                            char* variant = resolved.data.enum_.variant;
                            while (strcmp(enum_2->items.elements[index], variant) != 0) {
                                index++;
                            }
                        } else {
                            char* variant = resolved.data.enum_.variant;
                            while (strcmp(enum_->items.elements[index], variant) != 0) {
                                index++;
                            }
                        }

                        char buffer[128] = {};
                        sprintf(buffer, "  push %zu\n", index);
                        stringbuffer_appendstring(&state->instructions, buffer);
                        break;
                    }
                    default:
                        assert(false);
                }

            }

            break;
        }
        case Expression_If: {
            size_t main_end = state->flow_index;
            state->flow_index++;

            size_t individual_start = state->flow_index;
            state->flow_index++;

            If_Node* node = &expression->data.if_;
            while (node != NULL) {
                char buffer[128] = {};
                sprintf(buffer, "  __%zu:\n", individual_start);
                stringbuffer_appendstring(&state->instructions, buffer);

                individual_start = state->flow_index;
                state->flow_index++;
                
                if (node->condition != NULL) {
                    output_expression_fasm_linux_x86_64(node->condition, state);

                    stringbuffer_appendstring(&state->instructions, "  mov rbx, 1\n");
                    stringbuffer_appendstring(&state->instructions, "  xor rax, rax\n");
                    stringbuffer_appendstring(&state->instructions, "  mov al, [rsp]\n");
                    stringbuffer_appendstring(&state->instructions, "  add rsp, 1\n");
                    stringbuffer_appendstring(&state->instructions, "  cmp rax, rbx\n");
                    char buffer[128] = {};
                    sprintf(buffer, "  jne __%zu\n", node->next != NULL ? individual_start : main_end);
                    stringbuffer_appendstring(&state->instructions, buffer);
                }

                output_expression_fasm_linux_x86_64(node->inside, state);

                memset(buffer, 0, 128);
                sprintf(buffer, "  jmp __%zu\n", main_end);
                stringbuffer_appendstring(&state->instructions, buffer);

                node = node->next;
            }

            char buffer[128] = {};
            sprintf(buffer, "  __%zu:\n", main_end);
            stringbuffer_appendstring(&state->instructions, buffer);
            break;
        }
        case Expression_While: {
            size_t end = state->flow_index;
            state->flow_index++;

            size_t start = state->flow_index;
            state->flow_index++;

            While_Node* node = &expression->data.while_;
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
            Number_Node* number = &expression->data.number;
            Internal_Type type = number->type->data.internal;
            if (type == Type_F8) {
                double value;
                switch (number->kind) {
                    case Number_Integer:
                        value = (double) number->value.integer;
                        break;
                    case Number_Decimal:
                        value = number->value.decimal;
                        break;
                }

                stringbuffer_appendstring(&state->instructions, "  sub rsp, 8\n");
                char buffer[128] = {};
                sprintf(buffer, "  mov rax, %zu\n", *(size_t*) &value);
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
                }
                output_unsigned_integer(type, value, state);
            }
            break;
        }
        case Expression_Boolean: {
            Boolean_Node* boolean = &expression->data.boolean;
            char buffer[128] = {};
            sprintf(buffer, "  mov rax, %i\n", boolean->value);
            stringbuffer_appendstring(&state->instructions, buffer);
            stringbuffer_appendstring(&state->instructions, "  sub rsp, 1\n");
            stringbuffer_appendstring(&state->instructions, "  mov [rsp], al\n");
            break;
        }
        case Expression_String: {
            String_Node* string = &expression->data.string;
            char buffer[128] = {};
            sprintf(buffer, "  push _%zu\n", state->string_index);
            stringbuffer_appendstring(&state->instructions, buffer);

            memset(buffer, 0, 128);
            sprintf(buffer, "  _%zu: db \"%s\", 0\n", state->string_index, string->value);
            stringbuffer_appendstring(&state->data, buffer);

            state->string_index++;
            break;
        }
        case Expression_Reference: {
            Reference_Node* reference = &expression->data.reference;
            state->in_reference = true;

            output_expression_fasm_linux_x86_64(reference->inner, state);
            break;
        }
        case Expression_Cast: {
            Cast_Node* cast = &expression->data.cast;

            output_expression_fasm_linux_x86_64(cast->expression, state);

            if (cast->type.kind == Type_Internal && cast->computed_input_type.kind == Type_Internal) {
                Internal_Type output_internal = cast->type.data.internal;
                Internal_Type input_internal = cast->computed_input_type.data.internal;

                if ((input_internal == Type_USize || input_internal == Type_U8 || input_internal == Type_U4 || input_internal == Type_U2 || input_internal == Type_U1) && 
                        (output_internal == Type_USize || output_internal == Type_U8 || output_internal == Type_U4 || output_internal == Type_U2 || output_internal == Type_U1)) {
                    size_t input_size = get_size(&cast->computed_input_type, &state->generic);
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

                    size_t output_size = get_size(&cast->type, &state->generic);
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
                } else if (input_internal == Type_F8 && output_internal == Type_U8) {
                    stringbuffer_appendstring(&state->instructions, "  fld qword [rsp]\n");
                    stringbuffer_appendstring(&state->instructions, "  fisttp qword [rsp]\n");
                } else {
                    assert(false);
                }
            }

            break;
        }
        case Expression_SizeOf: {
            SizeOf_Node* size_of = &expression->data.size_of;

            output_unsigned_integer(size_of->computed_result_type.data.internal, get_size(&size_of->type, &state->generic), state);
            break;
        }
        default:
            printf("Unhandled expression_type!\n");
            exit(1);
    }
}

void output_item_fasm_linux_x86_64(Item_Node* item, Output_State* state) {
    switch (item->kind) {
        case Item_Procedure: {
            Procedure_Node* procedure = &item->data.procedure;
            // TODO: use some sort of annotation to specify entry procedure
            if (strcmp(item->name, "main") == 0) {
                stringbuffer_appendstring(&state->instructions, "main:\n");
            } else {
                char buffer[128] = {};
                sprintf(buffer + strlen(buffer), "%s.", item->name);

                if (state->current_module != NULL) {
                    sprintf(buffer + strlen(buffer), "%zu.", state->current_module->id);
                }

                sprintf(buffer + strlen(buffer), "%zu:\n", state->generic.current_file->id);
                stringbuffer_appendstring(&state->instructions, buffer);
            }

            stringbuffer_appendstring(&state->instructions, "  push rbp\n");
            stringbuffer_appendstring(&state->instructions, "  mov rbp, rsp\n");

            size_t locals_size = 8 + collect_expression_locals_size(procedure->data.literal.body, state);

            char buffer[128] = {};
            sprintf(buffer, "  sub rsp, %zu\n", locals_size);
            stringbuffer_appendstring(&state->instructions, buffer);

            state->current_declares = array_declaration_new(4);
            state->current_arguments = procedure->data.literal.arguments;
            state->current_returns = procedure->data.literal.returns;
            state->current_body = procedure->data.literal.body;

            output_expression_fasm_linux_x86_64(procedure->data.literal.body, state);

            stringbuffer_appendstring(&state->instructions, "  mov rsp, rbp\n");
            stringbuffer_appendstring(&state->instructions, "  pop rbp\n");
            stringbuffer_appendstring(&state->instructions, "  ret\n");
            break;
        }
        case Item_Global: {
            Global_Node* global = &item->data.global;
            size_t size = get_size(&global->type, &state->generic);

            char buffer[128] = {};
            sprintf(buffer + strlen(buffer), "%s.", item->name);

            if (state->current_module != NULL) {
                sprintf(buffer + strlen(buffer), "%zu.", state->current_module->id);
            }

            sprintf(buffer + strlen(buffer), "%zu: rb %zu\n", state->generic.current_file->id, size);
            stringbuffer_appendstring(&state->bss, buffer);
            break;
        }
        case Item_Module: {
            Module_Node* module = &item->data.module;
            for (size_t i = 0; i < module->items.count; i++) {
                state->current_module = module;
                Item_Node* item = &module->items.elements[i];
                output_item_fasm_linux_x86_64(item, state);
            }
            break;
        }
        case Item_Type:
            break;
        case Item_Use:
            break;
        default:
            printf("Unhandled item type!\n");
            exit(1);
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
        .scoped_declares = {},
        .current_arguments = {},
        .current_returns = {},
        .current_body = NULL,
        .current_module = NULL,
        .in_reference = false,
    };

    for (size_t j = 0; j < program->count; j++) {
        File_Node* file_node = &program->elements[j];
        state.generic.current_file = file_node;

        for (size_t i = 0; i < file_node->items.count; i++) {
            Item_Node* item = &file_node->items.elements[i];
            state.current_module = NULL;
            output_item_fasm_linux_x86_64(item, &state);
        }
    }

    FILE* file = fopen(output_file, "w");

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
        char* args[] = {"fasm", "output.asm", NULL};
        char* env[] = {NULL};

        execve("/bin/fasm", args, env);
        exit(1);
    }

    waitpid(result, NULL, 0);
}
