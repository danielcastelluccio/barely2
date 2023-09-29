#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "fasm_linux_x86_64.h"
#include "util.h"
#include "x86_64_util.h"
#include "../ast_walk.h"

typedef struct {
    Generic_State generic;
    String_Buffer instructions;
    String_Buffer data;
    String_Buffer bss;
    size_t string_index;
    size_t flow_index;
    Array_Size while_index;
} Output_State;

void output_expression_fasm_linux_x86_64(Ast_Expression* expression, Output_State* state);

void output_copy(Output_State* state, char* input_register, bool input_inverted, int input_offset, char* output_register, bool output_inverted, int output_offset, size_t size, char* intermediate_register_8, char* intermediate_register_1) {
    size_t i = size;
    while (i > 0) {
        if (i >= 8) {
            char buffer[128] = {};
            sprintf(buffer, "  mov %s, [%s%s%zu]\n", intermediate_register_8, input_register, input_inverted ? "-" : "+", input_offset + (size - i) * (input_inverted ? -1 : 1));
            stringbuffer_appendstring(&state->instructions, buffer);

            memset(buffer, 0, 128);

            sprintf(buffer, "  mov [%s%s%zu], %s\n", output_register, output_inverted ? "-" : "+", output_offset + (size - i) * (output_inverted ? -1 : 1), intermediate_register_8);
            stringbuffer_appendstring(&state->instructions, buffer);

            i -= 8;
        } else if (i >= 1) {
            char buffer[128] = {};
            sprintf(buffer, "  mov %s, [%s%s%zu]\n", intermediate_register_1, input_register, input_inverted ? "-" : "+", input_offset + (size - i) * (input_inverted ? -1 : 1));
            stringbuffer_appendstring(&state->instructions, buffer);

            memset(buffer, 0, 128);

            sprintf(buffer, "  mov [%s%s%zu], %s\n", output_register, output_inverted ? "-" : "+", output_offset + (size - i) * (output_inverted ? -1 : 1), intermediate_register_1);
            stringbuffer_appendstring(&state->instructions, buffer);

            i -= 1;
        }
    }
}

void output_actual_return_fasm_linux_x86_64(Output_State* state) {
    size_t arguments_size = get_arguments_size(&state->generic);
    size_t returns_size = get_returns_size(&state->generic);
    size_t locals_size = get_locals_size(state->generic.current_procedure, &state->generic);

    // rdx = old rip
    char buffer[128] = {};
    sprintf(buffer, "  mov rcx, [rsp+%zu]\n", returns_size + locals_size);
    stringbuffer_appendstring(&state->instructions, buffer);

    // rdx = old rbp
    memset(buffer, 0, 128);
    sprintf(buffer, "  mov rdx, [rsp+%zu]\n", returns_size + locals_size + 8);
    stringbuffer_appendstring(&state->instructions, buffer);

    output_copy(state, "rsp", false, 0, "rsp", false, 16 + arguments_size + locals_size, returns_size, "rax", "al");

    memset(buffer, 0, 128);
    sprintf(buffer, "  add rsp, %zu\n", 16 + arguments_size + locals_size);
    stringbuffer_appendstring(&state->instructions, buffer);

    stringbuffer_appendstring(&state->instructions, "  mov rbp, rcx\n");
    stringbuffer_appendstring(&state->instructions, "  push rdx\n");
    stringbuffer_appendstring(&state->instructions, "  ret\n");
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
                    for (size_t j = 0; j < state->generic.current_declares.count; j++) {
                        location += get_size(&state->generic.current_declares.elements[j].type, &state->generic);
                    }
                    size_t size = get_size(&declaration.type, &state->generic);

                    output_copy(state, "rsp", false, 0, "rbp", true, location + size, size, "rax", "al");

                    char buffer[128] = {};
                    sprintf(buffer, "  add rsp, %zu\n", size);
                    stringbuffer_appendstring(&state->instructions, buffer);

                    array_ast_declaration_append(&state->generic.current_declares, declaration);
                }
            } else {
                for (int i = declare->declarations.count - 1; i >= 0; i--) {
                    Ast_Declaration declaration = declare->declarations.elements[i];
                    array_ast_declaration_append(&state->generic.current_declares, declaration);
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
                    found = true;

                    Ast_Type array_type = assign_part->data.array.computed_array_type;

                    Ast_Type* array_ast_type_raw;
                    if (array_type.kind == Type_Pointer) {
                        array_ast_type_raw = array_type.data.pointer.child;
                    } else {
                        state->generic.in_reference = true;
                        array_ast_type_raw = &array_type;
                    }

                    output_expression_fasm_linux_x86_64(assign_part->data.array.expression_outer, state);
                    output_expression_fasm_linux_x86_64(assign_part->data.array.expression_inner, state);

                    size_t size = get_size(array_ast_type_raw->data.array.element_type, &state->generic);

                    stringbuffer_appendstring(&state->instructions, "  pop rax\n");
                    stringbuffer_appendstring(&state->instructions, "  pop rcx\n");

                    char buffer[128] = {};
                    sprintf(buffer, "  mov rdx, %zu\n", size);
                    stringbuffer_appendstring(&state->instructions, buffer);

                    stringbuffer_appendstring(&state->instructions, "  mul rdx\n");
                        
                    output_copy(state, "rsp", false, 0, "rax+rcx", false, 0, size, "rbx", "bl");

                    memset(buffer, 0, 128);
                    sprintf(buffer, "  add rsp, %zu\n", size);
                    stringbuffer_appendstring(&state->instructions, buffer);
                }

                if (!found && assign_part->kind == Retrieve_Assign_Parent) {
                    found = true;

                    Ast_Type parent_type = assign_part->data.parent.computed_parent_type;

                    if (assign_part->data.parent.needs_reference) {
                        state->generic.in_reference = true;
                    }

                    output_expression_fasm_linux_x86_64(assign_part->data.parent.expression, state);

                    stringbuffer_appendstring(&state->instructions, "  pop rax\n");

                    Location_Size_Data location_size = get_parent_item_location_size(&parent_type, assign_part->data.parent.name, &state->generic);
                    output_copy(state, "rsp", false, 0, "rax", false, location_size.location, location_size.size, "rbx", "bl");

                    char buffer[128] = {};
                    sprintf(buffer, "  add rsp, %zu\n", location_size.size);
                    stringbuffer_appendstring(&state->instructions, buffer);
                }

                if (!found && assign_part->kind == Retrieve_Assign_Identifier) {
                    char* name = assign_part->data.identifier.name;

                    if (has_local_variable(name, &state->generic)) {
                        found = true;

                        Location_Size_Data location_size = get_local_variable_location_size(name, &state->generic);
                        output_copy(state, "rsp", false, 0, "rbp", true, location_size.location + location_size.size, location_size.size, "rax", "al");

                        char buffer[128] = {};
                        sprintf(buffer, "  add rsp, %zu\n", location_size.size);
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
                                    size_t size = get_size(&global->type, &state->generic);

                                    output_copy(state, "rsp", false, 0, global->name, false, 0, size, "rax", "al");

                                    char buffer[128] = {};
                                    sprintf(buffer, "  add rsp, %zu\n", size);
                                    stringbuffer_appendstring(&state->instructions, buffer);

                                    found = true;
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
            }

            break;
        }
        case Statement_Return: {
            Ast_Statement_Return* return_ = &statement->data.return_;
            output_expression_fasm_linux_x86_64(return_->expression, state);
            output_actual_return_fasm_linux_x86_64(state);
            break;
        }
        case Statement_While: {
            size_t end = state->flow_index;
            state->flow_index++;

            size_t start = state->flow_index;
            state->flow_index++;

            Ast_Statement_While* node = &statement->data.while_;
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

            array_size_append(&state->while_index, end);

            output_expression_fasm_linux_x86_64(node->inside, state);

            state->while_index.count--;

            memset(buffer, 0, 128);
            sprintf(buffer, "  jmp __%zu\n", start);
            stringbuffer_appendstring(&state->instructions, buffer);

            memset(buffer, 0, 128);
            sprintf(buffer, "  __%zu:\n", end);
            stringbuffer_appendstring(&state->instructions, buffer);
            break;
        }
        case Statement_Break: {
            char buffer[128] = {};
            sprintf(buffer, "  jmp __%zu\n", state->while_index.elements[state->while_index.count - 1]);
            stringbuffer_appendstring(&state->instructions, buffer);
            break;
        }
        default:
            assert(false);
    }
}

void output_raw_value(Ast_Type_Internal type, size_t value, Output_State* state) {
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
    } else if (type == Type_UInt8 || type == Type_Byte) {
        stringbuffer_appendstring(&state->instructions, "  sub rsp, 1\n");
        char buffer[128] = {};
        sprintf(buffer, "  mov byte [rsp], %zu\n", value);
        stringbuffer_appendstring(&state->instructions, buffer);
    } else {
        assert(false);
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

void output_expression_fasm_linux_x86_64(Ast_Expression* expression, Output_State* state) {
    switch (expression->kind) {
        case Expression_Block: {
            array_size_append(&state->generic.scoped_declares, state->generic.current_declares.count);
            Ast_Expression_Block* block = &expression->data.block;
            for (size_t i = 0; i < block->statements.count; i++) {
                output_statement_fasm_linux_x86_64(block->statements.elements[i], state);
            }
            state->generic.current_declares.count = state->generic.scoped_declares.elements[state->generic.scoped_declares.count - 1];
            state->generic.scoped_declares.count--;
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
                        } else if (is_internal_type(Type_Float64, &operator_type)) {
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
                        } else if (is_internal_type(Type_Float64, &operator_type)) {
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
                    output_raw_value(Type_UInt, retrieve->location.row, state);
                    found = true;
                }
            }

            if (!found && retrieve->kind == Retrieve_Assign_Array) {
                found = true;

                Ast_Type array_type = retrieve->data.array.computed_array_type;

                bool in_reference = consume_in_reference(&state->generic);

                Ast_Type* array_ast_type_raw;
                if (array_type.kind == Type_Pointer) {
                    array_ast_type_raw = array_type.data.pointer.child;
                } else {
                    state->generic.in_reference = true;
                    array_ast_type_raw = &array_type;
                }

                output_expression_fasm_linux_x86_64(retrieve->data.array.expression_outer, state);
                output_expression_fasm_linux_x86_64(retrieve->data.array.expression_inner, state);

                size_t element_size = get_size(array_ast_type_raw->data.array.element_type, &state->generic);

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

                    output_copy(state, "rax+rcx", false, 0, "rsp", false, 0, element_size, "rbx", "bl");
                }
            }

            if (!found && retrieve->kind == Retrieve_Assign_Parent) {
                found = true;

                bool in_reference = consume_in_reference(&state->generic);
                Ast_Type parent_type = retrieve->data.parent.computed_parent_type;

                if (retrieve->data.parent.needs_reference) {
                    state->generic.in_reference = true;
                }

                output_expression_fasm_linux_x86_64(retrieve->data.parent.expression, state);
                stringbuffer_appendstring(&state->instructions, "  pop rax\n");

                Location_Size_Data location_size = get_parent_item_location_size(&parent_type, retrieve->data.parent.name, &state->generic);
                if (in_reference) {
                    char buffer[128] = {};
                    sprintf(buffer, "  add rax, %zu\n", location_size.location);
                    stringbuffer_appendstring(&state->instructions, buffer);

                    stringbuffer_appendstring(&state->instructions, "  push rax\n");
                } else {
                    char buffer[128] = {};
                    sprintf(buffer, "  sub rsp, %zu\n", location_size.size);
                    stringbuffer_appendstring(&state->instructions, buffer);

                    output_copy(state, "rax", false, location_size.location, "rsp", false, 0, location_size.size, "rbx", "bl");
                }
            }

            if (!found && retrieve->kind == Retrieve_Assign_Identifier) {
                char* name = retrieve->data.identifier.name;
                if (has_local_variable(name, &state->generic)) {
                    found = true;

                    Location_Size_Data location_size = get_local_variable_location_size(name, &state->generic);

                    if (consume_in_reference(&state->generic)) {
                        char buffer[128] = {};
                        sprintf(buffer, "  lea rax, [rbp-%zu]\n", location_size.location + location_size.size);
                        stringbuffer_appendstring(&state->instructions, buffer);
                        stringbuffer_appendstring(&state->instructions, "  push rax\n");
                    } else {
                        char buffer[128] = {};
                        sprintf(buffer, "  sub rsp, %zu\n", location_size.size);
                        stringbuffer_appendstring(&state->instructions, buffer);
                        
                        output_copy(state, "rbp", true, location_size.location + location_size.size, "rsp", false, 0, location_size.size, "rax", "al");
                    }
                }
            }

            if (!found && retrieve->kind == Retrieve_Assign_Identifier) {
                char* name = retrieve->data.identifier.name;
                if (has_argument(name, &state->generic)) {
                    found = true;

                    Location_Size_Data location_size = get_argument_location_size(name, &state->generic);
                    if (consume_in_reference(&state->generic)) {
                        char buffer[128] = {};
                        sprintf(buffer, "  lea rax, [rbp+%zu]\n", location_size.location + 8);
                        stringbuffer_appendstring(&state->instructions, buffer);
                        stringbuffer_appendstring(&state->instructions, "  push rax\n");
                    } else {
                        char buffer[128] = {};
                        sprintf(buffer, "  sub rsp, %zu\n", location_size.size);
                        stringbuffer_appendstring(&state->instructions, buffer);

                        output_copy(state, "rbp", false, location_size.location + 8, "rsp", false, 0, location_size.size, "rax", "al");
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
                                if (consume_in_reference(&state->generic)) {
                                    char buffer[128] = {};
                                    sprintf(buffer + strlen(buffer), "  lea rax, [%s.", global->name);
                                    sprintf(buffer + strlen(buffer), "%zu]\n", resolved.file->id);

                                    stringbuffer_appendstring(&state->instructions, buffer);
                                    stringbuffer_appendstring(&state->instructions, "  push rax\n");
                                } else {
                                    size_t size = get_size(&global->type, &state->generic);

                                    char buffer[128] = {};
                                    sprintf(buffer, "  sub rsp, %zu\n", size);
                                    stringbuffer_appendstring(&state->instructions, buffer);
                                        
                                    output_copy(state, global->name, false, 0, "rsp", false, 0, size, "rax", "al");
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
        case Expression_Number: {
            Ast_Expression_Number* number = &expression->data.number;

            assert(number->type != NULL);
            Ast_Type_Internal type = number->type->data.internal;
            if (type == Type_Float64) {
                union { double d; size_t s; } value;
                switch (number->kind) {
                    case Number_Integer:
                        value.d = (double) number->value.integer;
                        break;
                    case Number_Decimal:
                        value.d = number->value.decimal;
                        break;
                    default:
                        assert(false);
                }

                output_raw_value(Type_UInt64, value.s, state);
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
                output_raw_value(type, value, state);
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
            output_raw_value(Type_Byte, char_->value, state);
            break;
        }
        case Expression_Reference: {
            Ast_Expression_Reference* reference = &expression->data.reference;
            state->generic.in_reference = true;

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
                } else if (input_internal == Type_Float64 && output_internal == Type_UInt64) {
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
            output_zeroes(get_size(&init->type, &state->generic), state);
            break;
        }
        case Expression_Build: {
            Ast_Expression_Build* build = &expression->data.build;
            output_build_type(build, &build->type, state);
            break;
        }
        case Expression_SizeOf: {
            Ast_Expression_SizeOf* size_of = &expression->data.size_of;
            output_raw_value(size_of->computed_result_type.data.internal, get_size(&size_of->type, &state->generic), state);
            break;
        }
        case Expression_LengthOf: {
            Ast_Expression_LengthOf* length_of = &expression->data.length_of;
            output_raw_value(length_of->computed_result_type.data.internal, get_length(&length_of->type), state);
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
            // TODO: use some sort of annotation to specify entry procedure
            Ast_Item_Procedure* procedure = &item->data.procedure;

            state->generic.current_declares = array_ast_declaration_new(4);
            state->generic.current_arguments = procedure->arguments;
            state->generic.current_returns = procedure->returns;
            state->generic.current_procedure = procedure;

            char buffer[128] = {};
            sprintf(buffer, "%s:\n", procedure->name);
            stringbuffer_appendstring(&state->instructions, buffer);

            stringbuffer_appendstring(&state->instructions, "  push rbp\n");
            stringbuffer_appendstring(&state->instructions, "  mov rbp, rsp\n");

            size_t locals_size = get_locals_size(procedure, &state->generic);
            memset(buffer, 0, 128);
            sprintf(buffer, "  sub rsp, %zu\n", locals_size);
            stringbuffer_appendstring(&state->instructions, buffer);

            output_expression_fasm_linux_x86_64(procedure->body, state);

            if (procedure->has_implicit_return) {
                output_actual_return_fasm_linux_x86_64(state);
            }
            break;
        }
        case Item_Global: {
            Ast_Item_Global* global = &item->data.global;
            size_t size = get_size(&global->type, &state->generic);

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

void output_fasm_linux_x86_64(Program* program, char* output_file) {
    Output_State state = (Output_State) {
        .generic = (Generic_State) {
            .program = program,
            .current_file = NULL,
            .current_declares = {},
            .scoped_declares = array_size_new(8),
            .current_arguments = {},
            .in_reference = false,
        },
        .instructions = stringbuffer_new(16384),
        .data = stringbuffer_new(16384),
        .bss = stringbuffer_new(16384),
        .string_index = 0,
        .flow_index = 0,
        .while_index = array_size_new(4),
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
