#include <stdio.h>

#include "fasm_linux_x86_64.h"
#include "../string_util.h"
#include "../processor.h"

//#include "../ast_print.h"

typedef struct {
    File_Node* file_node;
    String_Buffer instructions;
    String_Buffer data;
    size_t string_index;
    size_t flow_index;
    Array_Declaration current_declares;
    Array_Declaration current_arguments;
    Array_Type current_returns;
    Expression_Node* current_body;
    bool in_reference;
} Output_State;

size_t get_size(Type* type, Output_State* state) {
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
            Definition_Node* definition = resolve_definition(state->file_node, &complex_name);
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
                size += get_size(&statement->data.declare.declarations.elements[i].type, state);
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
    }
    return 0;
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
                        location += get_size(&state->current_declares.elements[j].type, state);
                    }
                    size_t size = get_size(&declaration.type, state);

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

                    size_t size = get_size(child->data.array.element_type, state);

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
                            size = get_size(child, state);
                        } else {
                            Complex_Name complex_name = {};
                            if (child->kind == Type_Single) {
                                complex_name.data.single.name = child->data.basic.data.single;
                                complex_name.kind = Complex_Single;
                            } else {
                                complex_name.data.multi = child->data.basic.data.multi;
                                complex_name.kind = Complex_Multi;
                            }

                            Definition_Node* definition = resolve_definition(state->file_node, &complex_name);
                            Struct_Node* struct_ = &definition->data.type.data.struct_;
                            for (size_t i = 0; i < struct_->items.count; i++) {
                                Declaration* declaration = &struct_->items.elements[i];
                                size_t temp_size = get_size(&declaration->type, state);
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
                                sprintf(buffer, "  mov rbx, [rsp+%i]\n", location + i);
                                stringbuffer_appendstring(&state->instructions, buffer);

                                memset(buffer, 0, 128);

                                sprintf(buffer, "  mov [rax+%i], rbx\n", i);
                                stringbuffer_appendstring(&state->instructions, buffer);

                                i += 8;
                            } else if (size - i >= 1) {
                                char buffer[128] = {};
                                sprintf(buffer, "  mov bl, [rsp+%i]\n", location + i);
                                stringbuffer_appendstring(&state->instructions, buffer);

                                memset(buffer, 0, 128);

                                sprintf(buffer, "  mov [rax+%i], bl\n", i);
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
                            size_t declare_size = get_size(&state->current_declares.elements[j].type, state);
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
            }

            break;
        }
        case Statement_Return: {
            Statement_Return_Node* return_ = &statement->data.return_;
            output_expression_fasm_linux_x86_64(return_->expression, state);

            size_t arguments_size = 0;
            for (size_t i = 0; i < state->current_arguments.count; i++) {
                arguments_size += get_size(&state->current_arguments.elements[i].type, state);
            }

            size_t returns_size = 0;
            for (size_t i = 0; i < state->current_returns.count; i++) {
                returns_size += get_size(state->current_returns.elements[i], state);
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

void output_expression_fasm_linux_x86_64(Expression_Node* expression, Output_State* state) {
    switch (expression->kind) {
        case Expression_Block: {
            Block_Node* block = &expression->data.block;
            for (size_t i = 0; i < block->statements.count; i++) {
                output_statement_fasm_linux_x86_64(block->statements.elements[i], state);
            }
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
                        handled = true;
                    } else if (strcmp(name, "syscall5") == 0) {
                        stringbuffer_appendstring(&state->instructions, "  pop r8\n");
                        stringbuffer_appendstring(&state->instructions, "  pop r10\n");
                        stringbuffer_appendstring(&state->instructions, "  pop rdx\n");
                        stringbuffer_appendstring(&state->instructions, "  pop rsi\n");
                        stringbuffer_appendstring(&state->instructions, "  pop rdi\n");
                        stringbuffer_appendstring(&state->instructions, "  pop rax\n");
                        stringbuffer_appendstring(&state->instructions, "  syscall\n");
                        handled = true;
                    } else if (strcmp(name, "syscall4") == 0) {
                        stringbuffer_appendstring(&state->instructions, "  pop r10\n");
                        stringbuffer_appendstring(&state->instructions, "  pop rdx\n");
                        stringbuffer_appendstring(&state->instructions, "  pop rsi\n");
                        stringbuffer_appendstring(&state->instructions, "  pop rdi\n");
                        stringbuffer_appendstring(&state->instructions, "  pop rax\n");
                        stringbuffer_appendstring(&state->instructions, "  syscall\n");
                        handled = true;
                    } else if (strcmp(name, "syscall3") == 0) {
                        stringbuffer_appendstring(&state->instructions, "  pop rdx\n");
                        stringbuffer_appendstring(&state->instructions, "  pop rsi\n");
                        stringbuffer_appendstring(&state->instructions, "  pop rdi\n");
                        stringbuffer_appendstring(&state->instructions, "  pop rax\n");
                        stringbuffer_appendstring(&state->instructions, "  syscall\n");
                        handled = true;
                    } else if (strcmp(name, "syscall2") == 0) {
                        stringbuffer_appendstring(&state->instructions, "  pop rsi\n");
                        stringbuffer_appendstring(&state->instructions, "  pop rdi\n");
                        stringbuffer_appendstring(&state->instructions, "  pop rax\n");
                        stringbuffer_appendstring(&state->instructions, "  syscall\n");
                        handled = true;
                    } else if (strcmp(name, "syscall1") == 0) {
                        stringbuffer_appendstring(&state->instructions, "  pop rdi\n");
                        stringbuffer_appendstring(&state->instructions, "  pop rax\n");
                        stringbuffer_appendstring(&state->instructions, "  syscall\n");
                        handled = true;
                    } else if (strcmp(name, "syscall0") == 0) {
                        stringbuffer_appendstring(&state->instructions, "  pop rax\n");
                        stringbuffer_appendstring(&state->instructions, "  syscall\n");
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
                    case Operator_Divide: {
                        Type u64 = create_basic_single_type("u64");
                        Type operator_type = invoke->data.operator.added_type;

                        if (is_type(&u64, &operator_type)) {
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
                                    stringbuffer_appendstring(&state->instructions, "  div rbx\n");
                                    break;
                            }
                            stringbuffer_appendstring(&state->instructions, "  push rax\n");
                        }
                        
                        break;
                    }
                    case Operator_Equal:
                    case Operator_NotEqual:
                    case Operator_Greater:
                    case Operator_GreaterEqual:
                    case Operator_Less:
                    case Operator_LessEqual: {
                        Type u64 = create_basic_single_type("u64");
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

                    Type* child;
                    if (from_expression.kind == Type_Pointer) {
                        child = from_expression.data.pointer.child;
                    } else {
                        state->in_reference = true;
                        child = &from_expression;
                    }

                    output_expression_fasm_linux_x86_64(retrieve->data.array.expression_outer, state);

                    output_expression_fasm_linux_x86_64(retrieve->data.array.expression_inner, state);

                    size_t size = get_size(child->data.array.element_type, state);

                    stringbuffer_appendstring(&state->instructions, "  pop rax\n");
                    stringbuffer_appendstring(&state->instructions, "  pop rcx\n");

                    char buffer[128] = {};
                    sprintf(buffer, "  mov rdx, %i\n", size);
                    stringbuffer_appendstring(&state->instructions, buffer);

                    stringbuffer_appendstring(&state->instructions, "  mul rdx\n");

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

            if (!found) {
                if (retrieve->kind == Complex_Single) {
                    if (retrieve->data.single.expression != NULL) {
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
                            size = get_size(child, state);
                        } else {
                            Complex_Name complex_name = {};
                            if (child->kind == Type_Single) {
                                complex_name.data.single.name = child->data.basic.data.single;
                                complex_name.kind = Complex_Single;
                            } else {
                                complex_name.data.multi = child->data.basic.data.multi;
                                complex_name.kind = Complex_Multi;
                            }

                            Definition_Node* definition = resolve_definition(state->file_node, &complex_name);
                            Struct_Node* struct_ = &definition->data.type.data.struct_;
                            for (size_t i = 0; i < struct_->items.count; i++) {
                                Declaration* declaration = &struct_->items.elements[i];
                                size_t temp_size = get_size(&declaration->type, state);
                                if (strcmp(declaration->name, retrieve->data.single.name) == 0) {
                                    size = temp_size;
                                    break;
                                }
                                location += temp_size;
                            }
                        }

                        output_expression_fasm_linux_x86_64(retrieve->data.single.expression, state);

                        stringbuffer_appendstring(&state->instructions, "  pop rax\n");

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

                        found = true;
                    }
                }
            }

            if (!found) {
                if (retrieve->kind == Complex_Single) {
                    size_t location = 8;
                    size_t size = 0;
                    for (size_t i = 0; i < state->current_declares.count; i++) {
                        Declaration* declaration = &state->current_declares.elements[i];
                        size_t declaration_size = get_size(&declaration->type, state);
                        if (strcmp(declaration->name, retrieve->data.single.name) == 0) {
                            size = declaration_size;
                            found = true;
                            break;
                        }
                        location += declaration_size;
                    }

                    if (found) {
                        if (state->in_reference) {
                            char buffer[128] = {};
                            sprintf(buffer, "  lea rax, [rbp-%i]\n", location + size);
                            stringbuffer_appendstring(&state->instructions, buffer);
                            stringbuffer_appendstring(&state->instructions, "  push rax\n");

                            state->in_reference = false;
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
                        size_t declaration_size = get_size(&declaration->type, state);
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
                                sprintf(buffer, "  mov rax, [rbp+%i]\n", location + size - i);
                                stringbuffer_appendstring(&state->instructions, buffer);

                                memset(buffer, 0, 128);

                                sprintf(buffer, "  mov [rsp+%i], rax\n", i);
                                stringbuffer_appendstring(&state->instructions, buffer);

                                i += 8;
                            } else if (size - i >= 1) {
                                char buffer[128] = {};
                                sprintf(buffer, "  mov al, [rbp+%i]\n", location + size - i);
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
                Definition_Node* definition = resolve_definition(state->file_node, retrieve);
                if (definition != NULL) {
                    found = true;
                    switch (definition->kind) {
                        case Definition_Procedure:
                            char buffer[128] = {};
                            sprintf(buffer, "  push %s\n", definition->name);
                            stringbuffer_appendstring(&state->instructions, buffer);
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
        case Expression_Number: {
            Number_Node* number = &expression->data.number;
            char* basic_name = number->type->data.basic.data.single;
            if (strcmp(basic_name, "u64") == 0) {
                char buffer[128] = {};
                sprintf(buffer, "  push %i\n", number->value);
                stringbuffer_appendstring(&state->instructions, buffer);
            } else if (strcmp(basic_name, "u32") == 0) {
                stringbuffer_appendstring(&state->instructions, "  sub rsp, 4\n");
                char buffer[128] = {};
                sprintf(buffer, "  mov dword [rsp], %i\n", number->value);
                stringbuffer_appendstring(&state->instructions, buffer);
            } else if (strcmp(basic_name, "u8") == 0) {
                stringbuffer_appendstring(&state->instructions, "  sub rsp, 4\n");
                char buffer[128] = {};
                sprintf(buffer, "  mov byte [rsp], %i\n", number->value);
                stringbuffer_appendstring(&state->instructions, buffer);
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
        default:
            printf("Unhandled expression_type!\n");
            exit(1);
    }
}

void output_definition_fasm_linux_x86_64(Definition_Node* definition, Output_State* state) {
    switch (definition->kind) {
        case Definition_Procedure: {
            Procedure_Node* procedure = &definition->data.procedure;
            stringbuffer_appendstring(&state->instructions, definition->name);
            stringbuffer_appendstring(&state->instructions, ":\n");
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
        default:
            printf("Unhandled definition_type!\n");
            exit(1);
    }
}

void output_fasm_linux_x86_64(File_Node file_node, char* output_file) {
    //print_file_node(&file_node);

    Output_State state = (Output_State) {
        &file_node,
        stringbuffer_new(16384),
        stringbuffer_new(16384),
        0,
        0
    };

    for (size_t i = 0; i < file_node.definitions.count; i++) {
        Definition_Node* definition = &file_node.definitions.elements[i];
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

    fclose(file);
}
