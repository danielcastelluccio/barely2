#include <stdio.h>

#include "fasm_linux_x86_64.h"
#include "../string_util.h"

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
    bool in_reference;
} Output_State;

size_t get_size(Type* type, Generic_State* state) {
    switch (type->kind) {
        case Type_Basic: {
            Basic_Type* basic = &type->data.basic;
            if (basic->kind == Type_Single) {
                char* name = basic->data.single;

                if (strcmp(name, "u64") == 0) {
                    return 8;
                } else if (strcmp(name, "u32") == 0) {
                    return 4;
                } else if (strcmp(name, "u8") == 0) {
                    return 1;
                } else if (strcmp(name, "bool") == 0) {
                    return 1;
                }
            }

            Complex_Name complex_name = {};
            if (basic->kind == Type_Single) {
                complex_name.data.single.name = basic->data.single;
                complex_name.kind = Complex_Single;
            } else {
                complex_name.data.multi = basic->data.multi;
                complex_name.kind = Complex_Multi;
            }
            Definition_Node* definition = resolve_definition(state, &complex_name).definition;
            if (definition != NULL) {
                Type_Node* type = &definition->data.type;
                switch (type->kind) {
                    case Type_Node_Struct: {
                        size_t size = 0;
                        Struct_Node* struct_ = &type->data.struct_;
                        for (size_t i = 0; i < struct_->items.count; i++) {
                            size += get_size(&struct_->items.elements[i].type, state);
                        }
                        return size;
                        break;
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
                case Type_U64:
                    return 8;
                case Type_U32:
                    return 4;
                case Type_U16:
                    return 2;
                case Type_U8:
                    return 1;
            }
            break;
        }
        case Type_Pointer: {
            return 8;
        }
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
    }
    return 0;
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
    }
    return 0;
}

bool consume_in_reference_output(Output_State* state) {
    bool cached = state->in_reference;
    state->in_reference = false;
    return cached;
}

void output_expression_fasm_linux_x86_64(Expression_Node* expression, Output_State* state);

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
                            sprintf(buffer, "  mov rax, [rsp+%i]\n", i);
                            stringbuffer_appendstring(&state->instructions, buffer);

                            memset(buffer, 0, 128);

                            sprintf(buffer, "  mov [rbp-%i], rax\n", location + size - i);
                            stringbuffer_appendstring(&state->instructions, buffer);

                            i += 8;
                        } else if (size - i >= 1) {
                            char buffer[128] = {};
                            sprintf(buffer, "  mov al, [rsp+%i]\n", i);
                            stringbuffer_appendstring(&state->instructions, buffer);

                            memset(buffer, 0, 128);

                            sprintf(buffer, "  mov [rbp-%i], al\n", location + size - i);
                            stringbuffer_appendstring(&state->instructions, buffer);

                            i += 1;
                        }
                    }

                    char buffer[128] = {};
                    sprintf(buffer, "  add rsp, %i\n", size);
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

                if (!found && assign_part->kind == Complex_Array) {
                    Type from_expression = assign_part->data.array.added_type;

                    Type* child;
                    if (from_expression.kind == Type_Pointer) {
                        child = from_expression.data.pointer.child;
                    } else {
                        state->in_reference = true;
                        child = &from_expression;
                    }

                    output_expression_fasm_linux_x86_64(assign_part->data.array.expression_outer, state);

                    output_expression_fasm_linux_x86_64(assign_part->data.array.expression_inner, state);

                    size_t size = get_size(child->data.array.element_type, &state->generic);

                    stringbuffer_appendstring(&state->instructions, "  pop rax\n");
                    stringbuffer_appendstring(&state->instructions, "  pop rcx\n");

                    char buffer[128] = {};
                    sprintf(buffer, "  mov rdx, %i\n", size);
                    stringbuffer_appendstring(&state->instructions, buffer);

                    stringbuffer_appendstring(&state->instructions, "  mul rdx\n");
                        
                    size_t i = 0;
                    while (i < size) {
                        if (size - i >= 8) {
                            char buffer[128] = {};
                            sprintf(buffer, "  mov rbx, [rsp+%i]\n", i);
                            stringbuffer_appendstring(&state->instructions, buffer);

                            memset(buffer, 0, 128);

                            sprintf(buffer, "  mov [rax+rcx+%i], rbx\n", i);
                            stringbuffer_appendstring(&state->instructions, buffer);

                            i += 8;
                        } else if (size - i >= 1) {
                            char buffer[128] = {};
                            sprintf(buffer, "  mov bl, [rsp+%i]\n", i);
                            stringbuffer_appendstring(&state->instructions, buffer);

                            memset(buffer, 0, 128);

                            sprintf(buffer, "  mov [rax+rcx+%i], bl\n", i);
                            stringbuffer_appendstring(&state->instructions, buffer);

                            i += 1;
                        }
                    }

                    memset(buffer, 0, 128);
                    sprintf(buffer, "  add rsp, %i\n", size);
                    stringbuffer_appendstring(&state->instructions, buffer);

                    found = true;
                }

                if (!found && assign_part->kind == Complex_Single) {
                    if (assign_part->data.single.expression != NULL) {
                        Type from_expression = assign_part->data.single.added_type;

                        Type* child;
                        if (from_expression.kind == Type_Pointer) {
                            child = from_expression.data.pointer.child;
                        } else {
                            state->in_reference = true;
                            child = &from_expression;
                        }

                        size_t location = 0;
                        size_t size = 0;
                        if (strcmp(assign_part->data.single.name, "*") == 0) {
                            size = get_size(child, &state->generic);
                        } else {
                            Complex_Name complex_name = {};
                            if (child->data.basic.kind == Type_Single) {
                                complex_name.data.single.name = child->data.basic.data.single;
                                complex_name.kind = Complex_Single;
                            } else {
                                complex_name.data.multi = child->data.basic.data.multi;
                                complex_name.kind = Complex_Multi;
                            }

                            Definition_Node* definition = resolve_definition(&state->generic, &complex_name).definition;
                            Struct_Node* struct_ = &definition->data.type.data.struct_;
                            for (size_t i = 0; i < struct_->items.count; i++) {
                                Declaration* declaration = &struct_->items.elements[i];
                                size_t temp_size = get_size(&declaration->type, &state->generic);
                                if (strcmp(declaration->name, assign_part->data.single.name) == 0) {
                                    size = temp_size;
                                    break;
                                }
                                location += temp_size;
                            }
                        }

                        output_expression_fasm_linux_x86_64(assign_part->data.single.expression, state);

                        stringbuffer_appendstring(&state->instructions, "  pop rax\n");
                            
                        size_t i = 0;
                        while (i < size) {
                            if (size - i >= 8) {
                                char buffer[128] = {};
                                sprintf(buffer, "  mov rbx, [rsp+%i]\n", i);
                                stringbuffer_appendstring(&state->instructions, buffer);

                                memset(buffer, 0, 128);

                                sprintf(buffer, "  mov [rax+%i], rbx\n", location + i);
                                stringbuffer_appendstring(&state->instructions, buffer);

                                i += 8;
                            } else if (size - i >= 1) {
                                char buffer[128] = {};
                                sprintf(buffer, "  mov bl, [rsp+%i]\n", i);
                                stringbuffer_appendstring(&state->instructions, buffer);

                                memset(buffer, 0, 128);

                                sprintf(buffer, "  mov [rax+%i], bl\n", location + i);
                                stringbuffer_appendstring(&state->instructions, buffer);

                                i += 1;
                            }
                        }

                        char buffer[128] = {};
                        sprintf(buffer, "  add rsp, %i\n", size);
                        stringbuffer_appendstring(&state->instructions, buffer);

                        found = true;
                    }
                }

                if (!found && assign_part->kind == Complex_Single) {
                    if (assign_part->data.single.expression == NULL) {
                        char* name = assign_part->data.single.name;

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
                                sprintf(buffer, "  mov rax, [rsp+%i]\n", i);
                                stringbuffer_appendstring(&state->instructions, buffer);

                                memset(buffer, 0, 128);

                                sprintf(buffer, "  mov [rbp-%i], rax\n", location + size - i);
                                stringbuffer_appendstring(&state->instructions, buffer);

                                i += 8;
                            } else if (size - i >= 1) {
                                char buffer[128] = {};
                                sprintf(buffer, "  mov al, [rsp+%i]\n", i);
                                stringbuffer_appendstring(&state->instructions, buffer);

                                memset(buffer, 0, 128);

                                sprintf(buffer, "  mov [rbp-%i], al\n", location + size - i);
                                stringbuffer_appendstring(&state->instructions, buffer);

                                i += 1;
                            }
                        }

                        char buffer[128] = {};
                        sprintf(buffer, "  add rsp, %i\n", size);
                        stringbuffer_appendstring(&state->instructions, buffer);
                    }
                }

                if (!found && assign_part->kind == Complex_Single) {
                    Resolved_Definition resolved_definition = resolve_definition(&state->generic, assign_part);
                    Definition_Node* definition = resolved_definition.definition;

                    if (definition != NULL) {
                        switch (definition->kind) {
                            case Definition_Global: {
                                Global_Node* global = &definition->data.global;
                                size_t size = get_size(&global->type, &state->generic);

                                size_t i = 0;
                                while (i < size) {
                                    if (size - i >= 8) {
                                        char buffer[128] = {};
                                        sprintf(buffer, "  mov rax, [rsp+%i]\n", i);
                                        stringbuffer_appendstring(&state->instructions, buffer);

                                        memset(buffer, 0, 128);

                                        sprintf(buffer, "  mov [%s.%i+%i], rax\n", definition->name, resolved_definition.file->id, i);
                                        stringbuffer_appendstring(&state->instructions, buffer);

                                        i += 8;
                                    } else if (size - i >= 1) {
                                        char buffer[128] = {};
                                        sprintf(buffer, "  mov al, [rsp+%i]\n", i);
                                        stringbuffer_appendstring(&state->instructions, buffer);

                                        memset(buffer, 0, 128);

                                        sprintf(buffer, "  mov [%s.%i+%i], al\n", definition->name, resolved_definition.file->id, i);
                                        stringbuffer_appendstring(&state->instructions, buffer);

                                        i += 1;
                                    }
                                }

                                char buffer[128] = {};
                                sprintf(buffer, "  add rsp, %i\n", size);
                                stringbuffer_appendstring(&state->instructions, buffer);
                                    
                                found = true;
                                break;
                            }
                            default:
                                printf("Unhandled assign to definition!\n");
                                exit(1);
                        }
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
            sprintf(buffer, "  mov rcx, [rsp+%i]\n", returns_size + locals_size);
            stringbuffer_appendstring(&state->instructions, buffer);

            // rdx = old rbp
            memset(buffer, 0, 128);
            sprintf(buffer, "  mov rdx, [rsp+%i]\n", returns_size + locals_size + 8);
            stringbuffer_appendstring(&state->instructions, buffer);

            size_t i = returns_size;
            while (i > 0) {
                if (i >= 8) {
                    i -= 8;

                    char buffer[128] = {};
                    sprintf(buffer, "  mov rax, [rsp+%i]\n", returns_size - i - 8);
                    stringbuffer_appendstring(&state->instructions, buffer);

                    memset(buffer, 0, 128);

                    sprintf(buffer, "  mov [rsp+%i], rax\n", returns_size - i + 8 + arguments_size + locals_size);
                    stringbuffer_appendstring(&state->instructions, buffer);
                } else {
                    i -= 1;

                    char buffer[128] = {};
                    sprintf(buffer, "  mov al, [rsp+%i]\n", returns_size - i - 1);
                    stringbuffer_appendstring(&state->instructions, buffer);

                    memset(buffer, 0, 128);

                    sprintf(buffer, "  mov [rsp+%i], al\n", returns_size - i + 15 + arguments_size + locals_size);
                    stringbuffer_appendstring(&state->instructions, buffer);
                }
            }

            stringbuffer_appendstring(&state->instructions, "  mov rsp, rbp\n");


            if ((int) returns_size - (int) arguments_size + 16 < 0) {
                char buffer[128];
                sprintf(buffer, "  sub rsp, %i\n", -(returns_size - arguments_size + 16));
                stringbuffer_appendstring(&state->instructions, buffer);
            } else if ((int) returns_size - (int) arguments_size + 16 > 0) {
                char buffer[128];
                sprintf(buffer, "  add rsp, %i\n", arguments_size - returns_size + 16);
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
    if (type == Type_U64 || type == Type_USize) {
        stringbuffer_appendstring(&state->instructions, "  sub rsp, 8\n");
        char buffer[128] = {};
        sprintf(buffer, "  mov rax, %zu\n", value);
        stringbuffer_appendstring(&state->instructions, buffer);
        stringbuffer_appendstring(&state->instructions, "  mov [rsp], rax\n");
    } else if (type == Type_U32) {
        stringbuffer_appendstring(&state->instructions, "  sub rsp, 4\n");
        char buffer[128] = {};
        sprintf(buffer, "  mov dword [rsp], %zu\n", value);
        stringbuffer_appendstring(&state->instructions, buffer);
    } else if (type == Type_U8) {
        stringbuffer_appendstring(&state->instructions, "  sub rsp, 1\n");
        char buffer[128] = {};
        sprintf(buffer, "  mov byte [rsp], %zu\n", value);
        stringbuffer_appendstring(&state->instructions, buffer);
    }
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
                    char* name = procedure->data.retrieve.data.single.name;

                    if (strcmp(name, "syscall6") == 0) {
                        stringbuffer_appendstring(&state->instructions, "  pop r9\n");
                        stringbuffer_appendstring(&state->instructions, "  pop r8\n");
                        stringbuffer_appendstring(&state->instructions, "  pop r10\n");
                        stringbuffer_appendstring(&state->instructions, "  pop rdx\n");
                        stringbuffer_appendstring(&state->instructions, "  pop rsi\n");
                        stringbuffer_appendstring(&state->instructions, "  pop rdi\n");
                        stringbuffer_appendstring(&state->instructions, "  pop rax\n");
                        stringbuffer_appendstring(&state->instructions, "  syscall\n");
                        stringbuffer_appendstring(&state->instructions, "  push rax\n");
                        handled = true;
                    } else if (strcmp(name, "syscall5") == 0) {
                        stringbuffer_appendstring(&state->instructions, "  pop r8\n");
                        stringbuffer_appendstring(&state->instructions, "  pop r10\n");
                        stringbuffer_appendstring(&state->instructions, "  pop rdx\n");
                        stringbuffer_appendstring(&state->instructions, "  pop rsi\n");
                        stringbuffer_appendstring(&state->instructions, "  pop rdi\n");
                        stringbuffer_appendstring(&state->instructions, "  pop rax\n");
                        stringbuffer_appendstring(&state->instructions, "  syscall\n");
                        stringbuffer_appendstring(&state->instructions, "  push rax\n");
                        handled = true;
                    } else if (strcmp(name, "syscall4") == 0) {
                        stringbuffer_appendstring(&state->instructions, "  pop r10\n");
                        stringbuffer_appendstring(&state->instructions, "  pop rdx\n");
                        stringbuffer_appendstring(&state->instructions, "  pop rsi\n");
                        stringbuffer_appendstring(&state->instructions, "  pop rdi\n");
                        stringbuffer_appendstring(&state->instructions, "  pop rax\n");
                        stringbuffer_appendstring(&state->instructions, "  syscall\n");
                        stringbuffer_appendstring(&state->instructions, "  push rax\n");
                        handled = true;
                    } else if (strcmp(name, "syscall3") == 0) {
                        stringbuffer_appendstring(&state->instructions, "  pop rdx\n");
                        stringbuffer_appendstring(&state->instructions, "  pop rsi\n");
                        stringbuffer_appendstring(&state->instructions, "  pop rdi\n");
                        stringbuffer_appendstring(&state->instructions, "  pop rax\n");
                        stringbuffer_appendstring(&state->instructions, "  syscall\n");
                        stringbuffer_appendstring(&state->instructions, "  push rax\n");
                        handled = true;
                    } else if (strcmp(name, "syscall2") == 0) {
                        stringbuffer_appendstring(&state->instructions, "  pop rsi\n");
                        stringbuffer_appendstring(&state->instructions, "  pop rdi\n");
                        stringbuffer_appendstring(&state->instructions, "  pop rax\n");
                        stringbuffer_appendstring(&state->instructions, "  syscall\n");
                        stringbuffer_appendstring(&state->instructions, "  push rax\n");
                        handled = true;
                    } else if (strcmp(name, "syscall1") == 0) {
                        stringbuffer_appendstring(&state->instructions, "  pop rdi\n");
                        stringbuffer_appendstring(&state->instructions, "  pop rax\n");
                        stringbuffer_appendstring(&state->instructions, "  syscall\n");
                        stringbuffer_appendstring(&state->instructions, "  push rax\n");
                        handled = true;
                    } else if (strcmp(name, "syscall0") == 0) {
                        stringbuffer_appendstring(&state->instructions, "  pop rax\n");
                        stringbuffer_appendstring(&state->instructions, "  syscall\n");
                        stringbuffer_appendstring(&state->instructions, "  push rax\n");
                        handled = true;
                    }
                }

                if (!handled) {
                    output_expression_fasm_linux_x86_64(procedure, state);

                    stringbuffer_appendstring(&state->instructions, "  pop rax\n");
                    stringbuffer_appendstring(&state->instructions, "  call rax\n");
                }
            } else if (invoke->kind == Invoke_Operator) {
                switch (invoke->data.operator.operator) {
                    case Operator_Add:
                    case Operator_Subtract:
                    case Operator_Multiply:
                    case Operator_Divide:
                    case Operator_Modulus: {
                        Type u8 = create_internal_type(Type_U8);
                        Type u64 = create_internal_type(Type_U64);
                        Type usize = create_internal_type(Type_USize);
                        Type operator_type = invoke->data.operator.added_type;

                        if (is_type(&u64, &operator_type) || is_type(&usize, &operator_type)) {
                            stringbuffer_appendstring(&state->instructions, "  pop rbx\n");
                            stringbuffer_appendstring(&state->instructions, "  pop rax\n");
                            switch (invoke->data.operator.operator) {
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
                            }
                            stringbuffer_appendstring(&state->instructions, "  push rax\n");
                        } else if (is_type(&u8, &operator_type)) {
                            stringbuffer_appendstring(&state->instructions, "  mov bl, [rsp]\n");
                            stringbuffer_appendstring(&state->instructions, "  mov al, [rsp+1]\n");
                            stringbuffer_appendstring(&state->instructions, "  add rsp, 2\n");
                            switch (invoke->data.operator.operator) {
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
                            }
                            stringbuffer_appendstring(&state->instructions, "  sub rsp, 1\n");
                            stringbuffer_appendstring(&state->instructions, "  mov [rsp], al\n");
                        }
                        
                        break;
                    }
                    case Operator_Equal:
                    case Operator_NotEqual:
                    case Operator_Greater:
                    case Operator_GreaterEqual:
                    case Operator_Less:
                    case Operator_LessEqual: {
                        Type u64 = create_internal_type(Type_U64);
                        Type operator_type = invoke->data.operator.added_type;

                        if (is_type(&u64, &operator_type)) {
                            stringbuffer_appendstring(&state->instructions, "  xor rcx, rcx\n");
                            stringbuffer_appendstring(&state->instructions, "  mov rdx, 1\n");
                            stringbuffer_appendstring(&state->instructions, "  pop rbx\n");
                            stringbuffer_appendstring(&state->instructions, "  pop rax\n");
                            stringbuffer_appendstring(&state->instructions, "  cmp rax, rbx\n");
                            switch (invoke->data.operator.operator) {
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
                            }
                            stringbuffer_appendstring(&state->instructions, "  sub rsp, 1\n");
                            stringbuffer_appendstring(&state->instructions, "  mov [rsp], cl\n");
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
                if (retrieve->kind == Complex_Array) {
                    Type from_expression = retrieve->data.array.added_type;

                    bool in_reference = consume_in_reference_output(state);

                    Type* child;
                    if (from_expression.kind == Type_Pointer) {
                        child = from_expression.data.pointer.child;
                    } else {
                        state->in_reference = true;
                        child = &from_expression;
                    }

                    output_expression_fasm_linux_x86_64(retrieve->data.array.expression_outer, state);

                    output_expression_fasm_linux_x86_64(retrieve->data.array.expression_inner, state);

                    size_t size = get_size(child->data.array.element_type, &state->generic);

                    stringbuffer_appendstring(&state->instructions, "  pop rax\n");
                    stringbuffer_appendstring(&state->instructions, "  pop rcx\n");

                    char buffer[128] = {};
                    sprintf(buffer, "  mov rdx, %i\n", size);
                    stringbuffer_appendstring(&state->instructions, buffer);

                    stringbuffer_appendstring(&state->instructions, "  mul rdx\n");

                    if (in_reference) {
                        stringbuffer_appendstring(&state->instructions, "  add rax, rcx\n");
                        stringbuffer_appendstring(&state->instructions, "  push rax\n");
                    } else {
                        memset(buffer, 0, 128);
                        sprintf(buffer, "  sub rsp, %i\n", size);
                        stringbuffer_appendstring(&state->instructions, buffer);

                        size_t i = 0;
                        while (i < size) {
                            if (size - i >= 8) {
                                char buffer[128] = {};
                                sprintf(buffer, "  mov rbx, [rax+rcx+%i]\n", i);
                                stringbuffer_appendstring(&state->instructions, buffer);

                                memset(buffer, 0, 128);

                                sprintf(buffer, "  mov [rsp+%i], rbx\n", i);
                                stringbuffer_appendstring(&state->instructions, buffer);

                                i += 8;
                            } else if (size - i >= 1) {
                                char buffer[128] = {};
                                sprintf(buffer, "  mov bl, [rax+rcx+%i]\n", i);
                                stringbuffer_appendstring(&state->instructions, buffer);

                                memset(buffer, 0, 128);

                                sprintf(buffer, "  mov [rsp+%i], bl\n", i);
                                stringbuffer_appendstring(&state->instructions, buffer);

                                i += 1;
                            }
                        }
                    }
                }
            }

            if (!found) {
                if (retrieve->kind == Complex_Single) {
                    if (retrieve->data.single.expression != NULL) {
                        bool in_reference = consume_in_reference_output(state);
                        Type from_expression = retrieve->data.single.added_type;

                        Type* child;
                        if (from_expression.kind == Type_Pointer) {
                            child = from_expression.data.pointer.child;
                        } else {
                            state->in_reference = true;
                            child = &from_expression;
                        }
                        size_t location = 0;
                        size_t size = 0;
                        if (strcmp(retrieve->data.single.name, "*") == 0) {
                            size = get_size(child, &state->generic);
                        } else {
                            Complex_Name complex_name = {};
                            if (child->data.basic.kind == Type_Single) {
                                complex_name.data.single.name = child->data.basic.data.single;
                                complex_name.kind = Complex_Single;
                            } else {
                                complex_name.data.multi = child->data.basic.data.multi;
                                complex_name.kind = Complex_Multi;
                            }

                            Definition_Node* definition = resolve_definition(&state->generic, &complex_name).definition;
                            Struct_Node* struct_ = &definition->data.type.data.struct_;
                            for (size_t i = 0; i < struct_->items.count; i++) {
                                Declaration* declaration = &struct_->items.elements[i];
                                size_t temp_size = get_size(&declaration->type, &state->generic);
                                if (strcmp(declaration->name, retrieve->data.single.name) == 0) {
                                    size = temp_size;
                                    break;
                                }
                                location += temp_size;
                            }
                        }

                        output_expression_fasm_linux_x86_64(retrieve->data.single.expression, state);

                        stringbuffer_appendstring(&state->instructions, "  pop rax\n");

                        if (in_reference) {
                            char buffer[128] = {};
                            sprintf(buffer, "  add rax, %i\n", location);
                            stringbuffer_appendstring(&state->instructions, buffer);

                            stringbuffer_appendstring(&state->instructions, "  push rax\n");

                        } else {
                            char buffer[128] = {};
                            sprintf(buffer, "  sub rsp, %i\n", size);
                            stringbuffer_appendstring(&state->instructions, buffer);

                            size_t i = 0;
                            while (i < size) {
                                if (size - i >= 8) {
                                    char buffer[128] = {};
                                    sprintf(buffer, "  mov rbx, [rax+%i]\n", location + i);
                                    stringbuffer_appendstring(&state->instructions, buffer);

                                    memset(buffer, 0, 128);

                                    sprintf(buffer, "  mov [rsp+%i], rbx\n", i);
                                    stringbuffer_appendstring(&state->instructions, buffer);

                                    i += 8;
                                } else if (size - i >= 1) {
                                    char buffer[128] = {};
                                    sprintf(buffer, "  mov bl, [rax+%i]\n", location + i);
                                    stringbuffer_appendstring(&state->instructions, buffer);

                                    memset(buffer, 0, 128);

                                    sprintf(buffer, "  mov [rsp+%i], bl\n", i);
                                    stringbuffer_appendstring(&state->instructions, buffer);

                                    i += 1;
                                }
                            }
                        }

                        found = true;
                    }
                }
            }

            if (!found) {
                if (retrieve->kind == Complex_Single) {
                    size_t location = 8;
                    size_t size = 0;
                    for (int i = state->current_declares.count - 1; i >= 0; i--) {
                        Declaration* declaration = &state->current_declares.elements[i];
                        size_t declaration_size = get_size(&declaration->type, &state->generic);

                        if (found) {
                            location += declaration_size;
                        }

                        if (strcmp(declaration->name, retrieve->data.single.name) == 0) {
                            size = declaration_size;
                            found = true;
                        }
                    }

                    if (found) {
                        if (consume_in_reference_output(state)) {
                            char buffer[128] = {};
                            sprintf(buffer, "  lea rax, [rbp-%i]\n", location + size);
                            stringbuffer_appendstring(&state->instructions, buffer);
                            stringbuffer_appendstring(&state->instructions, "  push rax\n");
                        } else {
                            char buffer[128] = {};
                            sprintf(buffer, "  sub rsp, %i\n", size);
                            stringbuffer_appendstring(&state->instructions, buffer);
                                
                            size_t i = 0;
                            while (i < size) {
                                if (size - i >= 8) {
                                    char buffer[128] = {};
                                    sprintf(buffer, "  mov rax, [rbp-%i]\n", location + size - i);
                                    stringbuffer_appendstring(&state->instructions, buffer);

                                    memset(buffer, 0, 128);

                                    sprintf(buffer, "  mov [rsp+%i], rax\n", i);
                                    stringbuffer_appendstring(&state->instructions, buffer);

                                    i += 8;
                                } else if (size - i >= 1) {
                                    char buffer[128] = {};
                                    sprintf(buffer, "  mov al, [rbp-%i]\n", location + size - i);
                                    stringbuffer_appendstring(&state->instructions, buffer);

                                    memset(buffer, 0, 128);

                                    sprintf(buffer, "  mov [rsp+%i], al\n", i);
                                    stringbuffer_appendstring(&state->instructions, buffer);

                                    i += 1;
                                }
                            }
                        }
                    }
                }
            }

            if (!found) {
                if (retrieve->kind == Complex_Single) {
                    size_t location = 8;
                    size_t size = 0;
                    for (int i = state->current_arguments.count - 1; i >= 0; i--) {
                        Declaration* declaration = &state->current_arguments.elements[i];
                        size_t declaration_size = get_size(&declaration->type, &state->generic);
                        if (strcmp(declaration->name, retrieve->data.single.name) == 0) {
                            size = declaration_size;
                            found = true;
                            break;
                        }
                        location += declaration_size;
                    }

                    if (found) {
                        char buffer[128] = {};
                        sprintf(buffer, "  sub rsp, %i\n", size);
                        stringbuffer_appendstring(&state->instructions, buffer);
                            
                        size_t i = 0;
                        while (i < size) {
                            if (size - i >= 8) {
                                char buffer[128] = {};
                                sprintf(buffer, "  mov rax, [rbp+%i]\n", location + i + 8);
                                stringbuffer_appendstring(&state->instructions, buffer);

                                memset(buffer, 0, 128);

                                sprintf(buffer, "  mov [rsp+%i], rax\n", i);
                                stringbuffer_appendstring(&state->instructions, buffer);

                                i += 8;
                            } else if (size - i >= 1) {
                                char buffer[128] = {};
                                sprintf(buffer, "  mov al, [rbp+%i]\n", location + i + 8);
                                stringbuffer_appendstring(&state->instructions, buffer);

                                memset(buffer, 0, 128);

                                sprintf(buffer, "  mov [rsp+%i], al\n", i);
                                stringbuffer_appendstring(&state->instructions, buffer);

                                i += 1;
                            }
                        }
                    }
                }
            }

            if (!found) {
                Resolved_Definition resolved_definition = resolve_definition(&state->generic, retrieve);
                Definition_Node* definition = resolved_definition.definition;
                if (definition != NULL) {
                    found = true;
                    switch (definition->kind) {
                        case Definition_Procedure:
                            char buffer[128] = {};
                            sprintf(buffer, "  push %s.%i\n", definition->name, resolved_definition.file->id);
                            stringbuffer_appendstring(&state->instructions, buffer);
                            break;
                        case Definition_Global:
                            Global_Node* global = &definition->data.global;
                            if (consume_in_reference_output(state)) {
                                char buffer[128] = {};
                                sprintf(buffer, "  lea rax, [%s]\n", definition->name);
                                stringbuffer_appendstring(&state->instructions, buffer);
                                stringbuffer_appendstring(&state->instructions, "  push rax\n");
                            } else {
                                size_t size = get_size(&global->type, &state->generic);

                                char buffer[128] = {};
                                sprintf(buffer, "  sub rsp, %i\n", size);
                                stringbuffer_appendstring(&state->instructions, buffer);
                                    
                                size_t i = 0;
                                while (i < size) {
                                    if (size - i >= 8) {
                                        char buffer[128] = {};
                                        sprintf(buffer, "  mov rax, [%s.%i+%i]\n", definition->name, resolved_definition.file->id, i);
                                        stringbuffer_appendstring(&state->instructions, buffer);

                                        memset(buffer, 0, 128);

                                        sprintf(buffer, "  mov [rsp+%i], rax\n", i);
                                        stringbuffer_appendstring(&state->instructions, buffer);

                                        i += 8;
                                    } else if (size - i >= 1) {
                                        char buffer[128] = {};
                                        sprintf(buffer, "  mov al, [%s.%i+%i]\n", definition->name, resolved_definition.file->id, i);
                                        stringbuffer_appendstring(&state->instructions, buffer);

                                        memset(buffer, 0, 128);

                                        sprintf(buffer, "  mov [rsp+%i], al\n", i);
                                        stringbuffer_appendstring(&state->instructions, buffer);

                                        i += 1;
                                    }
                                }
                            }
                            break;
                        default:
                            printf("Unhandled definition retrieve!\n");
                            exit(1);
                    }
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
                sprintf(buffer, "  __%i:\n", individual_start);
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
                    sprintf(buffer, "  jne __%i\n", node->next != NULL ? individual_start : main_end);
                    stringbuffer_appendstring(&state->instructions, buffer);
                }

                output_expression_fasm_linux_x86_64(node->inside, state);

                memset(buffer, 0, 128);
                sprintf(buffer, "  jmp __%i\n", main_end);
                stringbuffer_appendstring(&state->instructions, buffer);

                node = node->next;
            }

            char buffer[128] = {};
            sprintf(buffer, "  __%i:\n", main_end);
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
            sprintf(buffer, "  __%i:\n", start);
            stringbuffer_appendstring(&state->instructions, buffer);

            output_expression_fasm_linux_x86_64(node->condition, state);

            stringbuffer_appendstring(&state->instructions, "  mov rbx, 1\n");
            stringbuffer_appendstring(&state->instructions, "  xor rax, rax\n");
            stringbuffer_appendstring(&state->instructions, "  mov al, [rsp]\n");
            stringbuffer_appendstring(&state->instructions, "  add rsp, 1\n");
            stringbuffer_appendstring(&state->instructions, "  cmp rax, rbx\n");

            memset(buffer, 0, 128);
            sprintf(buffer, "  jne __%i\n", end);
            stringbuffer_appendstring(&state->instructions, buffer);

            output_expression_fasm_linux_x86_64(node->inside, state);

            memset(buffer, 0, 128);
            sprintf(buffer, "  jmp __%i\n", start);
            stringbuffer_appendstring(&state->instructions, buffer);

            memset(buffer, 0, 128);
            sprintf(buffer, "  __%i:\n", end);
            stringbuffer_appendstring(&state->instructions, buffer);
            break;
        }
        case Expression_Number: {
            Number_Node* number = &expression->data.number;
            Internal_Type type = number->type->data.internal;
            output_unsigned_integer(type, number->value, state);
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
            sprintf(buffer, "  push _%i\n", state->string_index);
            stringbuffer_appendstring(&state->instructions, buffer);

            memset(buffer, 0, 128);
            sprintf(buffer, "  _%i: db \"%s\"\n", state->string_index, string->value);
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

            if (cast->type.kind == Type_Internal && cast->added_type.kind == Type_Internal) {
                Internal_Type output_internal = cast->type.data.internal;
                Internal_Type input_internal = cast->added_type.data.internal;

                if ((input_internal == Type_USize || input_internal == Type_U64 || input_internal == Type_U32 || input_internal == Type_U16 || input_internal == Type_U8) && 
                        (output_internal == Type_USize || output_internal == Type_U64 || output_internal == Type_U32 || output_internal == Type_U16 || output_internal == Type_U8)) {
                    if (input_internal == Type_USize || input_internal == Type_U64) {
                        stringbuffer_appendstring(&state->instructions, "  pop rax\n");
                    } else if (input_internal == Type_U32) {
                        stringbuffer_appendstring(&state->instructions, "  xor rax, rax\n");
                        stringbuffer_appendstring(&state->instructions, "  mov eax, [rsp]\n");
                        stringbuffer_appendstring(&state->instructions, "  add rsp, 4\n");
                    } else if (input_internal == Type_U16) {
                        stringbuffer_appendstring(&state->instructions, "  xor rax, rax\n");
                        stringbuffer_appendstring(&state->instructions, "  mov ax, [rsp]\n");
                        stringbuffer_appendstring(&state->instructions, "  add rsp, 2\n");
                    } else if (input_internal == Type_U8) {
                        stringbuffer_appendstring(&state->instructions, "  xor rax, rax\n");
                        stringbuffer_appendstring(&state->instructions, "  mov al, [rsp]\n");
                        stringbuffer_appendstring(&state->instructions, "  add rsp, 1\n");
                    }

                    if (output_internal == Type_USize || output_internal == Type_U64) {
                        stringbuffer_appendstring(&state->instructions, "  push rax\n");
                    } else if (output_internal == Type_U32) {
                        stringbuffer_appendstring(&state->instructions, "  sub rsp, 4\n");
                        stringbuffer_appendstring(&state->instructions, "  mov [rsp], eax\n");
                    } else if (output_internal == Type_U16) {
                        stringbuffer_appendstring(&state->instructions, "  sub rsp, 2\n");
                        stringbuffer_appendstring(&state->instructions, "  mov [rsp], ax\n");
                    } else if (output_internal == Type_U8) {
                        stringbuffer_appendstring(&state->instructions, "  sub rsp, 1\n");
                        stringbuffer_appendstring(&state->instructions, "  mov [rsp], al\n");
                    }
                }
            }

            break;
        }
        case Expression_SizeOf: {
            SizeOf_Node* size_of = &expression->data.size_of;

            output_unsigned_integer(size_of->added_type.data.internal, get_size(&size_of->type, &state->generic), state);
            break;
        }
        default:
            printf("Unhandled expression_type!\n");
            exit(1);
    }
}

void output_definition_fasm_linux_x86_64(Definition_Node* definition, Output_State* state) {
    switch (definition->kind) {
        case Definition_Procedure: {
            Procedure_Node* procedure = &definition->data.procedure;
            // TODO: use some sort of annotation to specify entry procedure
            if (strcmp(definition->name, "main") == 0) {
                stringbuffer_appendstring(&state->instructions, "main:\n");
            } else {
                char buffer[128] = {};
                sprintf(buffer, "%s.%i:\n", definition->name, state->generic.current_file->id);
                stringbuffer_appendstring(&state->instructions, buffer);
            }

            stringbuffer_appendstring(&state->instructions, "  push rbp\n");
            stringbuffer_appendstring(&state->instructions, "  mov rbp, rsp\n");

            size_t locals_size = 8 + collect_expression_locals_size(procedure->data.literal.body, state);

            char buffer[128] = {};
            sprintf(buffer, "  sub rsp, %i\n", locals_size);
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
        case Definition_Type:
            break;
        case Definition_Global:
            Global_Node* global = &definition->data.global;
            size_t size = get_size(&global->type, &state->generic);

            char buffer[128] = {};
            sprintf(buffer, "  %s.%i: rb %i\n", definition->name, state->generic.current_file->id, size);
            stringbuffer_appendstring(&state->bss, buffer);
            break;
        case Definition_Use:
            break;
        default:
            printf("Unhandled definition_type!\n");
            exit(1);
    }
}

void output_fasm_linux_x86_64(Program program, char* output_file) {
    Output_State state = (Output_State) {
        &program,
        NULL,
        stringbuffer_new(16384),
        stringbuffer_new(16384),
        0,
        0
    };

    for (size_t j = 0; j < program.count; j++) {
        File_Node* file_node = &program.elements[j];
        state.generic.current_file = file_node;

        for (size_t i = 0; i < file_node->definitions.count; i++) {
            Definition_Node* definition = &file_node->definitions.elements[i];
            output_definition_fasm_linux_x86_64(definition, &state);
        }

        FILE* file = fopen(output_file, "w");
        //file = stdout;

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
    }
}
