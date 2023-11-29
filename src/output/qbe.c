#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qbe.h"
#include "util.h"
#include "x86_64_util.h"
#include "../ast_walk.h"

typedef struct {
    Generic_State generic;
    String_Buffer types;
    String_Buffer instructions;
    String_Buffer data;
    String_Buffer bss;
    size_t string_index;
    size_t flow_index;
    Array_Size while_index;
    size_t intermediate_index;
    Array_Size intermediate_stack;
    char* entry;
} Output_State;

void output_expression_qbe(Ast_Expression* expression, Output_State* state);

void output_actual_return_qbe(Output_State* state) {
    size_t size = 0;
    for (size_t i = 0; i < state->generic.current_returns.count; i++) {
        size += get_size(state->generic.current_returns.elements[i], &state->generic);
    }

    if (size > 0) {
        size_t variable_pointer_intermediate = state->intermediate_index;
        char buffer[128] = {};
        sprintf(buffer, "  %%.%zu =l copy %%.r\n", state->intermediate_index);
        stringbuffer_appendstring(&state->instructions, buffer);
        state->intermediate_index++;

        size_t i = size;
        while (i > 0) {
            size_t temporary_pointer = state->intermediate_index;

            char buffer[128] = {};
            sprintf(buffer, "  %%.%zu =l add %%.%zu, %zu\n", temporary_pointer, variable_pointer_intermediate, size - i);
            stringbuffer_appendstring(&state->instructions, buffer);
            state->intermediate_index++;

            if (i >= 8) {
                char buffer[128] = {};
                sprintf(buffer, "  storel %%.%zu, %%.%zu \n", array_size_pop(&state->intermediate_stack), temporary_pointer);
                stringbuffer_appendstring(&state->instructions, buffer);
                i -= 8;
            } else if (i >= 4) {
                char buffer[128] = {};
                sprintf(buffer, "  storew %%.%zu, %%.%zu \n", array_size_pop(&state->intermediate_stack), temporary_pointer);
                stringbuffer_appendstring(&state->instructions, buffer);
                i -= 4;
            } else if (i >= 2) {
                char buffer[128] = {};
                sprintf(buffer, "  storeh %%.%zu, %%.%zu \n", array_size_pop(&state->intermediate_stack), temporary_pointer);
                stringbuffer_appendstring(&state->instructions, buffer);
                i -= 2;
            } else if (i >= 1) {
                char buffer[128] = {};
                sprintf(buffer, "  storeb %%.%zu, %%.%zu \n", array_size_pop(&state->intermediate_stack), temporary_pointer);
                stringbuffer_appendstring(&state->instructions, buffer);
                i -= 1;
            }
        }
        stringbuffer_appendstring(&state->instructions, "  ret %.r\n");

        memset(buffer, 0, 128);
        sprintf(buffer, "  @__%zu\n", state->flow_index);
        stringbuffer_appendstring(&state->instructions, buffer);
        state->flow_index++;
    } else {
        stringbuffer_appendstring(&state->instructions, "  ret\n");

        char buffer[128] = {};
        sprintf(buffer, "  @__%zu\n", state->flow_index);
        stringbuffer_appendstring(&state->instructions, buffer);
        state->flow_index++;
    }
}

void output_statement_qbe(Ast_Statement* statement, Output_State* state) {
    if (has_directive(&statement->directives, Directive_If)) {
        Ast_Directive_If* if_node = &get_directive(&statement->directives, Directive_If)->data.if_;
        if (!if_node->result) {
            return;
        }
    }

    switch (statement->kind) {
        case Statement_Expression: {
            Ast_Statement_Expression* statement_expression = &statement->data.expression;
            output_expression_qbe(statement_expression->expression, state);
            break;
        }
        case Statement_Declare: {
            Ast_Statement_Declare* declare = &statement->data.declare;
            if (declare->expression != NULL) {
                output_expression_qbe(declare->expression, state);

                for (int i = declare->declarations.count - 1; i >= 0; i--) {
                    Ast_Declaration declaration = declare->declarations.elements[i];
                    size_t location = state->generic.current_arguments.count + state->generic.current_declares.count;
                    size_t size = get_size(&declaration.type, &state->generic);

                    size_t variable_pointer_intermediate = state->intermediate_index;
                    char buffer[128] = {};
                    sprintf(buffer, "  %%.%zu =l copy %%.%zu\n", state->intermediate_index, location);
                    stringbuffer_appendstring(&state->instructions, buffer);
                    state->intermediate_index++;

                    Array_Size indexes = array_size_new(8);
                    size_t i = size;
                    while (i > 0) {
                        if (i >= 8) {
                            array_size_append(&indexes, 8);
                            i -= 8;
                        } else if (i >= 4) {
                            array_size_append(&indexes, 4);
                            i -= 4;
                        } else if (i >= 2) {
                            array_size_append(&indexes, 2);
                            i -= 2;
                        } else if (i >= 1) {
                            array_size_append(&indexes, 1);
                            i -= 1;
                        }
                    }

                    size_t total = 0;
                    for (size_t j = 0; j < indexes.count; j++) {
                        size_t temp_size = indexes.elements[indexes.count - j - 1];
                        total += temp_size;

                        size_t temporary_pointer = state->intermediate_index;

                        char buffer[128] = {};
                        sprintf(buffer, "  %%.%zu =l add %%.%zu, %zu\n", temporary_pointer, variable_pointer_intermediate, size - total);
                        stringbuffer_appendstring(&state->instructions, buffer);
                        state->intermediate_index++;

                        if (temp_size == 8) {
                            char buffer[128] = {};
                            sprintf(buffer, "  storel %%.%zu, %%.%zu \n", array_size_pop(&state->intermediate_stack), temporary_pointer);
                            stringbuffer_appendstring(&state->instructions, buffer);
                            state->intermediate_index++;
                        } else if (temp_size == 4) {
                            char buffer[128] = {};
                            sprintf(buffer, "  storew %%.%zu, %%.%zu \n", array_size_pop(&state->intermediate_stack), temporary_pointer);
                            stringbuffer_appendstring(&state->instructions, buffer);
                            state->intermediate_index++;
                        } else if (temp_size == 2) {
                            char buffer[128] = {};
                            sprintf(buffer, "  storeh %%.%zu, %%.%zu \n", array_size_pop(&state->intermediate_stack), temporary_pointer);
                            stringbuffer_appendstring(&state->instructions, buffer);
                            state->intermediate_index++;
                        } else if (temp_size == 1) {
                            char buffer[128] = {};
                            sprintf(buffer, "  storeb %%.%zu, %%.%zu \n", array_size_pop(&state->intermediate_stack), temporary_pointer);
                            stringbuffer_appendstring(&state->instructions, buffer);
                            state->intermediate_index++;
                        }
                    }

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
            output_expression_qbe(assign->expression, state);

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

                    output_expression_qbe(assign_part->data.array.expression_outer, state);
                    output_expression_qbe(assign_part->data.array.expression_inner, state);

                    size_t size = get_size(array_ast_type_raw->data.array.element_type, &state->generic);

                    size_t index_intermediate = array_size_pop(&state->intermediate_stack);
                    size_t variable_pointer_intermediate = array_size_pop(&state->intermediate_stack);

                    size_t index_offset_immediate = state->intermediate_index;
                    char buffer[128] = {};
                    sprintf(buffer, "  %%.%zu =l mul %%.%zu, %zu\n", index_offset_immediate, index_intermediate, size);
                    stringbuffer_appendstring(&state->instructions, buffer);
                    state->intermediate_index++;

                    size_t index_offset_variable_immediate = state->intermediate_index;
                    memset(buffer, 0, 128);
                    sprintf(buffer, "  %%.%zu =l add %%.%zu, %%.%zu\n", index_offset_variable_immediate, variable_pointer_intermediate, index_offset_immediate);
                    stringbuffer_appendstring(&state->instructions, buffer);
                    state->intermediate_index++;

                    size_t i = 0;
                    while (i < size) {
                        if (i + 8 <= size) {
                            i += 8;

                            size_t temporary_pointer = state->intermediate_index;

                            char buffer[128] = {};
                            sprintf(buffer, "  %%.%zu =l add %%.%zu, %zu\n", temporary_pointer, index_offset_variable_immediate, size - i);
                            stringbuffer_appendstring(&state->instructions, buffer);
                            state->intermediate_index++;

                            memset(buffer, 0, 128);
                            sprintf(buffer, "  storel %%.%zu, %%.%zu \n", array_size_pop(&state->intermediate_stack), temporary_pointer);
                            stringbuffer_appendstring(&state->instructions, buffer);
                            state->intermediate_index++;
                        } else if (i + 4 <= size) {
                            i += 4;

                            size_t temporary_pointer = state->intermediate_index;

                            char buffer[128] = {};
                            sprintf(buffer, "  %%.%zu =w add %%.%zu, %zu\n", temporary_pointer, index_offset_variable_immediate, size - i);
                            stringbuffer_appendstring(&state->instructions, buffer);
                            state->intermediate_index++;

                            memset(buffer, 0, 128);
                            sprintf(buffer, "  storew %%.%zu, %%.%zu\n", array_size_pop(&state->intermediate_stack), temporary_pointer);
                            stringbuffer_appendstring(&state->instructions, buffer);
                            state->intermediate_index++;
                        } else if (i + 2 <= size) {
                            i += 2;

                            size_t temporary_pointer = state->intermediate_index;

                            char buffer[128] = {};
                            sprintf(buffer, "  %%.%zu =l add %%.%zu, %zu\n", temporary_pointer, index_offset_variable_immediate, size - i);
                            stringbuffer_appendstring(&state->instructions, buffer);
                            state->intermediate_index++;

                            memset(buffer, 0, 128);
                            sprintf(buffer, "  storeh %%.%zu, %%.%zu\n", array_size_pop(&state->intermediate_stack), temporary_pointer);
                            stringbuffer_appendstring(&state->instructions, buffer);
                            state->intermediate_index++;
                        } else if (i + 1 <= size) {
                            i += 1;

                            size_t temporary_pointer = state->intermediate_index;

                            char buffer[128] = {};
                            sprintf(buffer, "  %%.%zu =l add %%.%zu, %zu\n", temporary_pointer, index_offset_variable_immediate, size - i);
                            stringbuffer_appendstring(&state->instructions, buffer);
                            state->intermediate_index++;

                            memset(buffer, 0, 128);
                            sprintf(buffer, "  storeb %%.%zu, %%.%zu\n", array_size_pop(&state->intermediate_stack), temporary_pointer);
                            stringbuffer_appendstring(&state->instructions, buffer);
                            state->intermediate_index++;
                        }
                    }
                }

                if (!found && assign_part->kind == Retrieve_Assign_Parent) {
                    found = true;

                    Ast_Type parent_type = assign_part->data.parent.computed_parent_type;

                    if (assign_part->data.parent.needs_reference) {
                        state->generic.in_reference = true;
                    }

                    output_expression_qbe(assign_part->data.parent.expression, state);

                    Location_Size_Data location_size = get_parent_item_location_size(&parent_type, assign_part->data.parent.name, &state->generic);

                    size_t variable_pointer_intermediate = array_size_pop(&state->intermediate_stack);

                    size_t i = 0;
                    while (i < location_size.size) {
                        if (i + 8 <= location_size.size) {
                            i += 8;

                            size_t temporary_pointer = state->intermediate_index;

                            char buffer[128] = {};
                            sprintf(buffer, "  %%.%zu =l add %%.%zu, %zu\n", temporary_pointer, variable_pointer_intermediate, location_size.size - i + location_size.location);
                            stringbuffer_appendstring(&state->instructions, buffer);
                            state->intermediate_index++;

                            memset(buffer, 0, 128);
                            sprintf(buffer, "  storel %%.%zu, %%.%zu \n", array_size_pop(&state->intermediate_stack), temporary_pointer);
                            stringbuffer_appendstring(&state->instructions, buffer);
                            state->intermediate_index++;
                        } else if (i + 4 <= location_size.size) {
                            i += 4;

                            size_t temporary_pointer = state->intermediate_index;

                            char buffer[128] = {};
                            sprintf(buffer, "  %%.%zu =w add %%.%zu, %zu\n", temporary_pointer, variable_pointer_intermediate, location_size.size - i + location_size.location);
                            stringbuffer_appendstring(&state->instructions, buffer);
                            state->intermediate_index++;

                            memset(buffer, 0, 128);
                            sprintf(buffer, "  storew %%.%zu, %%.%zu\n", array_size_pop(&state->intermediate_stack), temporary_pointer);
                            stringbuffer_appendstring(&state->instructions, buffer);
                            state->intermediate_index++;
                        } else if (i + 1 <= location_size.size) {
                            i += 1;

                            size_t temporary_pointer = state->intermediate_index;

                            char buffer[128] = {};
                            sprintf(buffer, "  %%.%zu =l add %%.%zu, %zu\n", temporary_pointer, variable_pointer_intermediate, location_size.size - i + location_size.location);
                            stringbuffer_appendstring(&state->instructions, buffer);
                            state->intermediate_index++;

                            memset(buffer, 0, 128);
                            sprintf(buffer, "  storeb %%.%zu, %%.%zu\n", array_size_pop(&state->intermediate_stack), temporary_pointer);
                            stringbuffer_appendstring(&state->instructions, buffer);
                            state->intermediate_index++;
                        }
                    }
                }

                if (!found && assign_part->kind == Retrieve_Assign_Identifier) {
                    char* name = assign_part->data.identifier.name;

                    if (has_local_variable(name, &state->generic)) {
                        found = true;

                        Location_Size_Data location_size = { .location = state->generic.current_arguments.count };

                        bool found = false;
                        for (int i = state->generic.current_declares.count - 1; i >= 0; i--) {
                            Ast_Declaration* declaration = &state->generic.current_declares.elements[i];
                            size_t declaration_size = get_size(&declaration->type, &state->generic);

                            if (found) {
                                location_size.location += 1;
                            }

                            if (!found && strcmp(declaration->name, name) == 0) {
                                location_size.size = declaration_size;
                                found = true;
                            }
                        }

                        size_t variable_pointer_intermediate = state->intermediate_index;
                        char buffer[128] = {};
                        sprintf(buffer, "  %%.%zu =l copy %%.%zu\n", state->intermediate_index, location_size.location);
                        stringbuffer_appendstring(&state->instructions, buffer);
                        state->intermediate_index++;

                        Array_Size indexes = array_size_new(8);
                        size_t i = location_size.size;
                        while (i > 0) {
                            if (i >= 8) {
                                array_size_append(&indexes, 8);
                                i -= 8;
                            } else if (i >= 4) {
                                array_size_append(&indexes, 4);
                                i -= 4;
                            } else if (i >= 2) {
                                array_size_append(&indexes, 2);
                                i -= 2;
                            } else if (i >= 1) {
                                array_size_append(&indexes, 1);
                                i -= 1;
                            }
                        }

                        size_t total = 0;
                        for (size_t j = 0; j < indexes.count; j++) {
                            size_t temp_size = indexes.elements[indexes.count - j - 1];
                            total += temp_size;
                            if (temp_size == 8) {
                                size_t temporary_pointer = state->intermediate_index;

                                char buffer[128] = {};
                                sprintf(buffer, "  %%.%zu =l add %%.%zu, %zu\n", temporary_pointer, variable_pointer_intermediate, location_size.size - total);
                                stringbuffer_appendstring(&state->instructions, buffer);
                                state->intermediate_index++;

                                memset(buffer, 0, 128);
                                sprintf(buffer, "  storel %%.%zu, %%.%zu \n", array_size_pop(&state->intermediate_stack), temporary_pointer);
                                stringbuffer_appendstring(&state->instructions, buffer);
                                state->intermediate_index++;
                            } else if (temp_size == 4) {
                                size_t temporary_pointer = state->intermediate_index;

                                char buffer[128] = {};
                                sprintf(buffer, "  %%.%zu =l add %%.%zu, %zu\n", temporary_pointer, variable_pointer_intermediate, location_size.size - total);
                                stringbuffer_appendstring(&state->instructions, buffer);
                                state->intermediate_index++;

                                memset(buffer, 0, 128);
                                sprintf(buffer, "  storew %%.%zu, %%.%zu\n", array_size_pop(&state->intermediate_stack), temporary_pointer);
                                stringbuffer_appendstring(&state->instructions, buffer);
                                state->intermediate_index++;
                            } else if (temp_size == 2) {
                                size_t temporary_pointer = state->intermediate_index;

                                char buffer[128] = {};
                                sprintf(buffer, "  %%.%zu =l add %%.%zu, %zu\n", temporary_pointer, variable_pointer_intermediate, location_size.size - total);
                                stringbuffer_appendstring(&state->instructions, buffer);
                                state->intermediate_index++;

                                memset(buffer, 0, 128);
                                sprintf(buffer, "  storeh %%.%zu, %%.%zu\n", array_size_pop(&state->intermediate_stack), temporary_pointer);
                                stringbuffer_appendstring(&state->instructions, buffer);
                                state->intermediate_index++;
                            } else if (temp_size == 1) {
                                size_t temporary_pointer = state->intermediate_index;

                                char buffer[128] = {};
                                sprintf(buffer, "  %%.%zu =l add %%.%zu, %zu\n", temporary_pointer, variable_pointer_intermediate, location_size.size - total);
                                stringbuffer_appendstring(&state->instructions, buffer);
                                state->intermediate_index++;

                                memset(buffer, 0, 128);
                                sprintf(buffer, "  storeb %%.%zu, %%.%zu\n", array_size_pop(&state->intermediate_stack), temporary_pointer);
                                stringbuffer_appendstring(&state->instructions, buffer);
                                state->intermediate_index++;
                            }
                        }
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

                                    size_t variable_pointer_intermediate = state->intermediate_index;
                                    char buffer[128] = {};
                                    sprintf(buffer, "  %%.%zu =l copy $%s\n", state->intermediate_index, global->name);
                                    stringbuffer_appendstring(&state->instructions, buffer);
                                    state->intermediate_index++;

                                    Array_Size indexes = array_size_new(8);
                                    size_t i = size;
                                    while (i > 0) {
                                        if (i >= 8) {
                                            array_size_append(&indexes, 8);
                                            i -= 8;
                                        } else if (i >= 4) {
                                            array_size_append(&indexes, 4);
                                            i -= 4;
                                        } else if (i >= 2) {
                                            array_size_append(&indexes, 2);
                                            i -= 2;
                                        } else if (i >= 1) {
                                            array_size_append(&indexes, 1);
                                            i -= 1;
                                        }
                                    }

                                    size_t total = 0;
                                    for (size_t j = 0; j < indexes.count; j++) {
                                        size_t temp_size = indexes.elements[indexes.count - j - 1];
                                        total += temp_size;
                                        if (temp_size == 8) {
                                            size_t temporary_pointer = state->intermediate_index;

                                            char buffer[128] = {};
                                            sprintf(buffer, "  %%.%zu =l add %%.%zu, %zu\n", temporary_pointer, variable_pointer_intermediate, size - total);
                                            stringbuffer_appendstring(&state->instructions, buffer);
                                            state->intermediate_index++;

                                            memset(buffer, 0, 128);
                                            sprintf(buffer, "  storel %%.%zu, %%.%zu \n", array_size_pop(&state->intermediate_stack), temporary_pointer);
                                            stringbuffer_appendstring(&state->instructions, buffer);
                                            state->intermediate_index++;
                                        } else if (temp_size == 4) {
                                            size_t temporary_pointer = state->intermediate_index;

                                            char buffer[128] = {};
                                            sprintf(buffer, "  %%.%zu =l add %%.%zu, %zu\n", temporary_pointer, variable_pointer_intermediate, size - total);
                                            stringbuffer_appendstring(&state->instructions, buffer);
                                            state->intermediate_index++;

                                            memset(buffer, 0, 128);
                                            sprintf(buffer, "  storew %%.%zu, %%.%zu\n", array_size_pop(&state->intermediate_stack), temporary_pointer);
                                            stringbuffer_appendstring(&state->instructions, buffer);
                                            state->intermediate_index++;
                                        } else if (temp_size == 2) {
                                            size_t temporary_pointer = state->intermediate_index;

                                            char buffer[128] = {};
                                            sprintf(buffer, "  %%.%zu =l add %%.%zu, %zu\n", temporary_pointer, variable_pointer_intermediate, size - total);
                                            stringbuffer_appendstring(&state->instructions, buffer);
                                            state->intermediate_index++;

                                            memset(buffer, 0, 128);
                                            sprintf(buffer, "  storeh %%.%zu, %%.%zu\n", array_size_pop(&state->intermediate_stack), temporary_pointer);
                                            stringbuffer_appendstring(&state->instructions, buffer);
                                            state->intermediate_index++;
                                        } else if (temp_size == 1) {
                                            size_t temporary_pointer = state->intermediate_index;

                                            char buffer[128] = {};
                                            sprintf(buffer, "  %%.%zu =l add %%.%zu, %zu\n", temporary_pointer, variable_pointer_intermediate, size - total);
                                            stringbuffer_appendstring(&state->instructions, buffer);
                                            state->intermediate_index++;

                                            memset(buffer, 0, 128);
                                            sprintf(buffer, "  storeb %%.%zu, %%.%zu\n", array_size_pop(&state->intermediate_stack), temporary_pointer);
                                            stringbuffer_appendstring(&state->instructions, buffer);
                                            state->intermediate_index++;
                                        }
                                    }

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
            if (return_->expression != NULL) {
                output_expression_qbe(return_->expression, state);
            }
            output_actual_return_qbe(state);
            break;
        }
        case Statement_While: {
            size_t end = state->flow_index;
            state->flow_index++;

            size_t start = state->flow_index;
            state->flow_index++;

            Ast_Statement_While* node = &statement->data.while_;
            char buffer[128] = {};
            sprintf(buffer, "  @__%zu\n", start);
            stringbuffer_appendstring(&state->instructions, buffer);

            output_expression_qbe(node->condition, state);

            size_t temp = state->flow_index;
            state->flow_index++;

            memset(buffer, 0, 128);
            sprintf(buffer, "  jnz %%.%zu, @__%zu, @__%zu\n", array_size_pop(&state->intermediate_stack), temp, end);
            stringbuffer_appendstring(&state->instructions, buffer);

            memset(buffer, 0, 128);
            sprintf(buffer, "  @__%zu\n", temp);
            stringbuffer_appendstring(&state->instructions, buffer);

            array_size_append(&state->while_index, end);

            output_expression_qbe(node->inside, state);

            state->while_index.count--;

            memset(buffer, 0, 128);
            sprintf(buffer, "  jmp @__%zu\n", start);
            stringbuffer_appendstring(&state->instructions, buffer);

            memset(buffer, 0, 128);
            sprintf(buffer, "  @__%zu\n", end);
            stringbuffer_appendstring(&state->instructions, buffer);
            break;
        }
        case Statement_Break: {
            char buffer[128] = {};
            sprintf(buffer, "  jmp @__%zu\n", state->while_index.elements[state->while_index.count - 1]);
            stringbuffer_appendstring(&state->instructions, buffer);
            break;
        }
        default:
            assert(false);
    }
}

void output_raw_value_qbe(Ast_Type_Internal type, size_t value, Output_State* state) {
    char buffer[128] = {};
    if (type == Type_UInt64 || type == Type_UInt) {
        sprintf(buffer, "  %%.%zu =l copy %zu\n", state->intermediate_index, value);
    } else if (type == Type_Float64) {
        sprintf(buffer, "  %%.%zu =l copy %zu\n", state->intermediate_index, value);
    } else if (type == Type_UInt32) {
        sprintf(buffer, "  %%.%zu =w copy %zu\n", state->intermediate_index, value);
    } else if (type == Type_UInt16) {
        sprintf(buffer, "  %%.%zu =w copy %zu\n", state->intermediate_index, value);
    } else if (type == Type_UInt8 || type == Type_Byte) {
        sprintf(buffer, "  %%.%zu =w copy %zu\n", state->intermediate_index, value);
    } else {
        assert(false);
    }
    stringbuffer_appendstring(&state->instructions, buffer);
    array_size_append(&state->intermediate_stack, state->intermediate_index);
    state->intermediate_index++;
}

void output_string_qbe(char* value, Output_State* state) {
    char buffer[128] = {};
    sprintf(buffer, "  %%.%zu =l copy $__%zu\n", state->intermediate_index,  state->string_index);
    stringbuffer_appendstring(&state->instructions, buffer);
    array_size_append(&state->intermediate_stack, state->intermediate_index);
    state->intermediate_index++;

    memset(buffer, 0, 128);
    sprintf(buffer, "data $__%zu = {", state->string_index);
    stringbuffer_appendstring(&state->data, buffer);

    size_t str_len = strlen(value);
    for (size_t i = 0; i < str_len; i++) {
        char buffer[128] = {};
        sprintf(buffer, "b %i,", (int) value[i]);
        stringbuffer_appendstring(&state->data, buffer);
    }

    memset(buffer, 0, 128);
    sprintf(buffer, "b 0 }\n");
    stringbuffer_appendstring(&state->data, buffer);

    state->string_index++;
}

void output_boolean_qbe(bool value, Output_State* state) {
    char buffer[128] = {};
    sprintf(buffer, "  %%.%zu =w copy %i\n", state->intermediate_index, value);
    stringbuffer_appendstring(&state->instructions, buffer);
    array_size_append(&state->intermediate_stack, state->intermediate_index);
    state->intermediate_index++;
}

void output_zeroes_qbe(size_t count, Output_State* state) {
    size_t i = 0;
    while (i < count) {
        if (i + 8 <= count) {
            char buffer[128] = {};
            sprintf(buffer, "  %%.%zu =l copy 0\n", state->intermediate_index);
            stringbuffer_appendstring(&state->instructions, buffer);
            array_size_append(&state->intermediate_stack, state->intermediate_index);
            state->intermediate_index++;
            i += 8;
        } else {
            char buffer[128] = {};
            sprintf(buffer, "  %%.%zu =w copy 0\n", state->intermediate_index);
            stringbuffer_appendstring(&state->instructions, buffer);
            array_size_append(&state->intermediate_stack, state->intermediate_index);
            state->intermediate_index++;
            i += 1;
        }
    }
}

void output_build_type_qbe(Ast_Expression_Build* build, Ast_Type* type_in, Output_State* state) {
    Ast_Type type = evaluate_type_complete(type_in, &state->generic);
    switch (type.kind) {
        case Type_Struct: {
            Ast_Type_Struct* struct_ = &type.data.struct_;
            if (build->arguments.count == struct_->items.count) {
                for (size_t i = 0; i < struct_->items.count; i++) {
                    output_expression_qbe(build->arguments.elements[i], state);
                }
            }
            break;
        }
        case Type_Array: {
            Ast_Type_Array* array = &type.data.array;
            size_t array_size = array->size_type->data.number.value;

            if (build->arguments.count == array_size) {
                for (size_t i = 0; i < build->arguments.count; i++) {
                    output_expression_qbe(build->arguments.elements[i], state);
                }
            }
            break;
        }
        default:
            assert(false);
    }
}

void output_expression_qbe(Ast_Expression* expression, Output_State* state) {
    switch (expression->kind) {
        case Expression_Block: {
            Ast_Expression_Block* block = &expression->data.block;
            for (size_t i = 0; i < block->statements.count; i++) {
                output_statement_qbe(block->statements.elements[i], state);
            }
            break;
        }
        case Expression_Multiple: {
            Ast_Expression_Multiple* multiple = &expression->data.multiple;
            for (size_t i = 0; i < multiple->expressions.count; i++) {
                output_expression_qbe(multiple->expressions.elements[i], state);
            }
            break;
        }
        case Expression_Invoke: {
            Ast_Expression_Invoke* invoke = &expression->data.invoke;
            for (size_t i = 0; i < invoke->arguments.count; i++) {
                output_expression_qbe(invoke->arguments.elements[i], state);
            }

            if (invoke->kind == Invoke_Standard) {
                Ast_Expression* procedure = invoke->data.procedure.procedure;
                bool handled = false;

                if (procedure->kind == Expression_Retrieve) {
                    char* name = procedure->data.retrieve.data.identifier.name;

                    if (strncmp(name, "@syscall", 8) == 0) {
                        handled = true;

                        size_t arg_count = (size_t) atoi(name + 8);

                        char buffer[128] = {};
                        sprintf(buffer, "  %%.%zu =l call $syscall(", state->intermediate_index);
                        stringbuffer_appendstring(&state->instructions, buffer);
                        size_t syscall_result_intermediate = state->intermediate_index;
                        state->intermediate_index++;

                        for (size_t i = 0; i < arg_count + 1; i++) {
                            char buffer[128] = {};
                            sprintf(buffer, "l %%.%zu", array_size_get(&state->intermediate_stack, state->intermediate_stack.count - arg_count - 1 + i));
                            stringbuffer_appendstring(&state->instructions, buffer);

                            if (i < arg_count) {
                                stringbuffer_appendstring(&state->instructions, ",");
                            }
                        }

                        state->intermediate_stack.count -= arg_count + 1;

                        stringbuffer_appendstring(&state->instructions, ")\n");

                        array_size_append(&state->intermediate_stack, syscall_result_intermediate);
                    }
                }

                if (!handled) {
                    output_expression_qbe(procedure, state);

                    stringbuffer_appendstring(&state->instructions, "  ");

                    Ast_Type_Procedure* procedure_type = &invoke->data.procedure.computed_procedure_type.data.procedure;
                    size_t returns_size = 0;
                    size_t return_intermediate = 0;
                    if (procedure_type->returns.count > 0) {
                        for (size_t i = 0; i < procedure_type->returns.count; i++) {
                            returns_size += get_size(procedure_type->returns.elements[i], &state->generic);
                        }

                        return_intermediate = state->intermediate_index;
                        char buffer[128] = {};
                        sprintf(buffer, "%%.%zu =:.%zu ", return_intermediate, returns_size);
                        stringbuffer_appendstring(&state->instructions, buffer);
                        state->intermediate_index++;
                    }

                    char buffer[128] = {};
                    sprintf(buffer, "call %%.%zu(", array_size_pop(&state->intermediate_stack));
                    stringbuffer_appendstring(&state->instructions, buffer);

                    Array_Size reversed = array_size_new(4);
                    for (size_t i = 0; i < procedure_type->arguments.count; i++) {
                        size_t size = get_size(procedure_type->arguments.elements[i], &state->generic);

                        size_t j = 0;
                        while (j < size) {
                            if (j + 8 <= size) {
                                j += 8;
                                array_size_append(&reversed, array_size_pop(&state->intermediate_stack));
                            } else if (j + 1 <= size) {
                                j += 1;
                                array_size_append(&reversed, array_size_pop(&state->intermediate_stack));
                            }
                        }
                    }

                    for (size_t i = 0; i < procedure_type->arguments.count; i++) {
                        size_t size = get_size(procedure_type->arguments.elements[i], &state->generic);

                        size_t j = 0;
                        while (j < size) {
                            if (i > 0 || j > 0) {
                                stringbuffer_appendstring(&state->instructions, ",");
                            }

                            if (j + 8 <= size) {
                                j += 8;

                                char buffer[128] = {};
                                sprintf(buffer, "l %%.%zu", array_size_pop(&reversed));
                                stringbuffer_appendstring(&state->instructions, buffer);
                                state->intermediate_index++;
                            } else if (j + 1 <= size) {
                                j += 1;

                                char buffer[128] = {};
                                sprintf(buffer, "w %%.%zu", array_size_pop(&reversed));
                                stringbuffer_appendstring(&state->instructions, buffer);
                                state->intermediate_index++;
                            }
                        }
                    }

                    stringbuffer_appendstring(&state->instructions, ")\n");

                    if (procedure_type->returns.count > 0) {
                        size_t pointer_intermediate = state->intermediate_index;
                        memset(buffer, 0, 128);
                        sprintf(buffer, "  %%.%zu =l copy %%.%zu\n", state->intermediate_index, return_intermediate);
                        stringbuffer_appendstring(&state->instructions, buffer);
                        state->intermediate_index++;

                        size_t size = returns_size;
                        size_t i = size;
                        while (i > 0) {
                            if (i >= 8) {
                                i -= 8;
                                size_t temporary_pointer = state->intermediate_index;

                                char buffer[128] = {};
                                sprintf(buffer, "  %%.%zu =l add %%.%zu, %zu\n", temporary_pointer, pointer_intermediate, i);
                                stringbuffer_appendstring(&state->instructions, buffer);
                                state->intermediate_index++;

                                memset(buffer, 0, 128);
                                sprintf(buffer, "  %%.%zu =l loadl %%.%zu \n", state->intermediate_index, temporary_pointer);
                                stringbuffer_appendstring(&state->instructions, buffer);
                                array_size_append(&state->intermediate_stack, state->intermediate_index);
                                state->intermediate_index++;
                            } else if (i >= 1) {
                                i -= 1;
                                size_t temporary_pointer = state->intermediate_index;

                                char buffer[128] = {};
                                sprintf(buffer, "  %%.%zu =l add %%.%zu, %zu\n", temporary_pointer, pointer_intermediate, i);
                                stringbuffer_appendstring(&state->instructions, buffer);
                                state->intermediate_index++;

                                memset(buffer, 0, 128);
                                sprintf(buffer, "  %%.%zu =w loadub %%.%zu \n", state->intermediate_index, temporary_pointer);
                                stringbuffer_appendstring(&state->instructions, buffer);
                                array_size_append(&state->intermediate_stack, state->intermediate_index);
                                state->intermediate_index++;
                            }
                        }
                    }
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
                            size_t result_intermediate = state->intermediate_index;
                            char buffer[128] = {};
                            switch (invoke->data.operator_.operator_) {
                                case Operator_Add: {
                                    sprintf(buffer, "  %%.%zu =l add %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                case Operator_Subtract: {
                                    sprintf(buffer, "  %%.%zu =l sub %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                case Operator_Multiply: {
                                    sprintf(buffer, "  %%.%zu =l mul %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                case Operator_Divide: {
                                    sprintf(buffer, "  %%.%zu =l udiv %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                case Operator_Modulus: {
                                    sprintf(buffer, "  %%.%zu =l urem %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                default:
                                    assert(false);
                            }
                            stringbuffer_appendstring(&state->instructions, buffer);
                            array_size_append(&state->intermediate_stack, result_intermediate);
                            state->intermediate_index++;
                        } else if (is_internal_type(Type_UInt32, &operator_type)) {
                            size_t result_intermediate = state->intermediate_index;
                            char buffer[128] = {};
                            switch (invoke->data.operator_.operator_) {
                                case Operator_Add: {
                                    sprintf(buffer, "  %%.%zu =w add %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                case Operator_Subtract: {
                                    sprintf(buffer, "  %%.%zu =w sub %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                case Operator_Multiply: {
                                    sprintf(buffer, "  %%.%zu =w mul %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                case Operator_Divide: {
                                    sprintf(buffer, "  %%.%zu =w udiv %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                case Operator_Modulus: {
                                    sprintf(buffer, "  %%.%zu =w urem %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                default:
                                    assert(false);
                            }
                            stringbuffer_appendstring(&state->instructions, buffer);
                            array_size_append(&state->intermediate_stack, result_intermediate);
                            state->intermediate_index++;
                        } else if (is_internal_type(Type_UInt16, &operator_type)) {
                            size_t result_intermediate = state->intermediate_index;
                            char buffer[128] = {};
                            switch (invoke->data.operator_.operator_) {
                                case Operator_Add: {
                                    sprintf(buffer, "  %%.%zu =w add %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                case Operator_Subtract: {
                                    sprintf(buffer, "  %%.%zu =w sub %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                case Operator_Multiply: {
                                    sprintf(buffer, "  %%.%zu =w mul %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                case Operator_Divide: {
                                    sprintf(buffer, "  %%.%zu =w udiv %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                case Operator_Modulus: {
                                    sprintf(buffer, "  %%.%zu =w urem %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                default:
                                    assert(false);
                            }
                            stringbuffer_appendstring(&state->instructions, buffer);
                            array_size_append(&state->intermediate_stack, result_intermediate);
                            state->intermediate_index++;
                        } else if (is_internal_type(Type_UInt8, &operator_type)) {
                            size_t result_intermediate = state->intermediate_index;
                            char buffer[128] = {};
                            switch (invoke->data.operator_.operator_) {
                                case Operator_Add: {
                                    sprintf(buffer, "  %%.%zu =w add %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                case Operator_Subtract: {
                                    sprintf(buffer, "  %%.%zu =w sub %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                case Operator_Multiply: {
                                    sprintf(buffer, "  %%.%zu =w mul %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                case Operator_Divide: {
                                    sprintf(buffer, "  %%.%zu =w udiv %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                case Operator_Modulus: {
                                    sprintf(buffer, "  %%.%zu =w urem %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                default:
                                    assert(false);
                            }
                            stringbuffer_appendstring(&state->instructions, buffer);
                            array_size_append(&state->intermediate_stack, result_intermediate);
                            state->intermediate_index++;
                        } else if (is_internal_type(Type_Float64, &operator_type)) {
                            size_t input_intermediate1 = state->intermediate_index;
                            char buffer[128] = {};
                            sprintf(buffer, "  %%.%zu =d cast %%.%zu\n", input_intermediate1, array_size_pop(&state->intermediate_stack));
                            stringbuffer_appendstring(&state->instructions, buffer);
                            state->intermediate_index++;

                            size_t input_intermediate2 = state->intermediate_index;
                            memset(buffer, 0, 128);
                            sprintf(buffer, "  %%.%zu =d cast %%.%zu\n", input_intermediate2, array_size_pop(&state->intermediate_stack));
                            stringbuffer_appendstring(&state->instructions, buffer);
                            state->intermediate_index++;

                            size_t result_intermediate = state->intermediate_index;
                            memset(buffer, 0, 128);
                            switch (invoke->data.operator_.operator_) {
                                case Operator_Add: {
                                    sprintf(buffer, "  %%.%zu =w add %%.%zu, %%.%zu\n", result_intermediate, input_intermediate2, input_intermediate1);
                                    break;
                                }
                                case Operator_Subtract: {
                                    sprintf(buffer, "  %%.%zu =w sub %%.%zu, %%.%zu\n", result_intermediate, input_intermediate2, input_intermediate1);
                                    break;
                                }
                                case Operator_Multiply: {
                                    sprintf(buffer, "  %%.%zu =w mul %%.%zu, %%.%zu\n", result_intermediate, input_intermediate2, input_intermediate1);
                                    break;
                                }
                                case Operator_Divide: {
                                    sprintf(buffer, "  %%.%zu =w udiv %%.%zu, %%.%zu\n", result_intermediate, input_intermediate2, input_intermediate1);
                                    break;
                                }
                                case Operator_Modulus: {
                                    sprintf(buffer, "  %%.%zu =w urem %%.%zu, %%.%zu\n", result_intermediate, input_intermediate2, input_intermediate1);
                                    break;
                                }
                                default:
                                    assert(false);
                            }
                            stringbuffer_appendstring(&state->instructions, buffer);
                            array_size_append(&state->intermediate_stack, result_intermediate);
                            state->intermediate_index++;
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
                            size_t result_intermediate = state->intermediate_index;
                            char buffer[128] = {};
                            switch (invoke->data.operator_.operator_) {
                                case Operator_Equal: {
                                    sprintf(buffer, "  %%.%zu =w ceql %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                case Operator_NotEqual: {
                                    sprintf(buffer, "  %%.%zu =w cnel %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                case Operator_Less: {
                                    sprintf(buffer, "  %%.%zu =w cultl %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                case Operator_LessEqual: {
                                    sprintf(buffer, "  %%.%zu =w culel %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                case Operator_Greater: {
                                    sprintf(buffer, "  %%.%zu =w cugtl %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                case Operator_GreaterEqual: {
                                    sprintf(buffer, "  %%.%zu =w cugel %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                default:
                                    assert(false);
                            }
                            stringbuffer_appendstring(&state->instructions, buffer);
                            array_size_append(&state->intermediate_stack, result_intermediate);
                            state->intermediate_index++;
                        } else if (is_internal_type(Type_UInt32, &operator_type)) {
                            size_t result_intermediate = state->intermediate_index;
                            char buffer[128] = {};
                            switch (invoke->data.operator_.operator_) {
                                case Operator_Equal: {
                                    sprintf(buffer, "  %%.%zu =w ceqw %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                case Operator_NotEqual: {
                                    sprintf(buffer, "  %%.%zu =w cnew %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                case Operator_Less: {
                                    sprintf(buffer, "  %%.%zu =w cultw %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                case Operator_LessEqual: {
                                    sprintf(buffer, "  %%.%zu =w culew %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                case Operator_Greater: {
                                    sprintf(buffer, "  %%.%zu =w cugtw %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                case Operator_GreaterEqual: {
                                    sprintf(buffer, "  %%.%zu =w cugew %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                default:
                                    assert(false);
                            }
                            stringbuffer_appendstring(&state->instructions, buffer);
                            array_size_append(&state->intermediate_stack, result_intermediate);
                            state->intermediate_index++;
                        } else if (is_internal_type(Type_UInt16, &operator_type)) {
                            size_t result_intermediate = state->intermediate_index;
                            char buffer[128] = {};
                            switch (invoke->data.operator_.operator_) {
                                case Operator_Equal: {
                                    sprintf(buffer, "  %%.%zu =w ceqw %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                case Operator_NotEqual: {
                                    sprintf(buffer, "  %%.%zu =w cnew %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                case Operator_Less: {
                                    sprintf(buffer, "  %%.%zu =w cultw %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                case Operator_LessEqual: {
                                    sprintf(buffer, "  %%.%zu =w culew %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                case Operator_Greater: {
                                    sprintf(buffer, "  %%.%zu =w cugtw %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                case Operator_GreaterEqual: {
                                    sprintf(buffer, "  %%.%zu =w cugew %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                default:
                                    assert(false);
                            }
                            stringbuffer_appendstring(&state->instructions, buffer);
                            array_size_append(&state->intermediate_stack, result_intermediate);
                            state->intermediate_index++;
                        } else if (is_internal_type(Type_UInt8, &operator_type) || is_internal_type(Type_Byte, &operator_type)) {
                            size_t result_intermediate = state->intermediate_index;
                            char buffer[128] = {};
                            switch (invoke->data.operator_.operator_) {
                                case Operator_Equal: {
                                    sprintf(buffer, "  %%.%zu =w ceqw %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                case Operator_NotEqual: {
                                    sprintf(buffer, "  %%.%zu =w cnew %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                case Operator_Less: {
                                    sprintf(buffer, "  %%.%zu =w cultw %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                case Operator_LessEqual: {
                                    sprintf(buffer, "  %%.%zu =w culew %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                case Operator_Greater: {
                                    sprintf(buffer, "  %%.%zu =w cugtw %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                case Operator_GreaterEqual: {
                                    sprintf(buffer, "  %%.%zu =w cugew %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                default:
                                    assert(false);
                            }
                            stringbuffer_appendstring(&state->instructions, buffer);
                            array_size_append(&state->intermediate_stack, result_intermediate);
                            state->intermediate_index++;
                        } else if (is_internal_type(Type_Float64, &operator_type)) {
                            size_t input_intermediate1 = state->intermediate_index;
                            char buffer[128] = {};
                            sprintf(buffer, "  %%.%zu =d cast %%.%zu\n", input_intermediate1, array_size_pop(&state->intermediate_stack));
                            stringbuffer_appendstring(&state->instructions, buffer);
                            state->intermediate_index++;

                            size_t input_intermediate2 = state->intermediate_index;
                            memset(buffer, 0, 128);
                            sprintf(buffer, "  %%.%zu =d cast %%.%zu\n", input_intermediate2, array_size_pop(&state->intermediate_stack));
                            stringbuffer_appendstring(&state->instructions, buffer);
                            state->intermediate_index++;

                            size_t result_intermediate = state->intermediate_index;
                            memset(buffer, 0, 128);
                            switch (invoke->data.operator_.operator_) {
                                case Operator_Equal: {
                                    sprintf(buffer, "  %%.%zu =w ceqd %%.%zu, %%.%zu\n", result_intermediate, input_intermediate2, input_intermediate1);
                                    break;
                                }
                                case Operator_NotEqual: {
                                    sprintf(buffer, "  %%.%zu =w cned %%.%zu, %%.%zu\n", result_intermediate, input_intermediate2, input_intermediate1);
                                    break;
                                }
                                case Operator_Less: {
                                    sprintf(buffer, "  %%.%zu =w cltd %%.%zu, %%.%zu\n", result_intermediate, input_intermediate2, input_intermediate1);
                                    break;
                                }
                                case Operator_LessEqual: {
                                    sprintf(buffer, "  %%.%zu =w cled %%.%zu, %%.%zu\n", result_intermediate, input_intermediate2, input_intermediate1);
                                    break;
                                }
                                case Operator_Greater: {
                                    sprintf(buffer, "  %%.%zu =w cgtd %%.%zu, %%.%zu\n", result_intermediate, input_intermediate2, input_intermediate1);
                                    break;
                                }
                                case Operator_GreaterEqual: {
                                    sprintf(buffer, "  %%.%zu =w cged %%.%zu, %%.%zu\n", result_intermediate, input_intermediate2, input_intermediate1);
                                    break;
                                }
                                default:
                                    assert(false);
                            }
                            stringbuffer_appendstring(&state->instructions, buffer);
                            array_size_append(&state->intermediate_stack, result_intermediate);
                            state->intermediate_index++;
                        } else if (is_enum_type(&operator_type, &state->generic)) {
                            size_t result_intermediate = state->intermediate_index;
                            char buffer[128] = {};
                            switch (invoke->data.operator_.operator_) {
                                case Operator_Equal: {
                                    sprintf(buffer, "  %%.%zu =w ceql %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                case Operator_NotEqual: {
                                    sprintf(buffer, "  %%.%zu =w cnel %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                default:
                                    assert(false);
                            }
                            stringbuffer_appendstring(&state->instructions, buffer);
                            array_size_append(&state->intermediate_stack, result_intermediate);
                            state->intermediate_index++;
                        } else if (is_internal_type(Type_Ptr, &operator_type) || operator_type.kind == Type_Pointer) {
                            size_t result_intermediate = state->intermediate_index;
                            char buffer[128] = {};
                            switch (invoke->data.operator_.operator_) {
                                case Operator_Equal: {
                                    sprintf(buffer, "  %%.%zu =w ceql %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                case Operator_NotEqual: {
                                    sprintf(buffer, "  %%.%zu =w cnel %%.%zu, %%.%zu\n", result_intermediate, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                                    break;
                                }
                                default:
                                    assert(false);
                            }
                            stringbuffer_appendstring(&state->instructions, buffer);
                            array_size_append(&state->intermediate_stack, result_intermediate);
                            state->intermediate_index++;
                        } else {
                            assert(false);
                        }
                        
                        break;
                    }
                    case Operator_And: {
                        char buffer[128] = {};
                        sprintf(buffer, "  %%.%zu =w and %%.%zu, %%.%zu\n", state->intermediate_index, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                        stringbuffer_appendstring(&state->instructions, buffer);
                        array_size_append(&state->intermediate_stack, state->intermediate_index);
                        state->intermediate_index++;
                        break;
                    }
                    case Operator_Or: {
                        char buffer[128] = {};
                        sprintf(buffer, "  %%.%zu =w or %%.%zu, %%.%zu\n", state->intermediate_index, array_size_pop(&state->intermediate_stack), array_size_pop(&state->intermediate_stack));
                        stringbuffer_appendstring(&state->instructions, buffer);
                        array_size_append(&state->intermediate_stack, state->intermediate_index);
                        state->intermediate_index++;
                        break;
                    }
                    case Operator_Not: {
                        char buffer[128] = {};
                        sprintf(buffer, "  %%.%zu =w ceqw 0, %%.%zu\n", state->intermediate_index, array_size_pop(&state->intermediate_stack));
                        stringbuffer_appendstring(&state->instructions, buffer);
                        array_size_append(&state->intermediate_stack, state->intermediate_index);
                        state->intermediate_index++;
                        break;
                    }
                }
            }
            break;
        }
        case Expression_RunMacro: {
            Ast_RunMacro* macro = &expression->data.run_macro;
            output_expression_qbe(macro->result.data.expression, state);
            break;
        }
        case Expression_Retrieve: {
            Ast_Expression_Retrieve* retrieve = &expression->data.retrieve;
            bool found = false;

            if (!found && retrieve->kind == Retrieve_Assign_Identifier) {
                if (strcmp(retrieve->data.identifier.name, "@file") == 0) {
                    output_string_qbe(retrieve->location.file, state);
                    found = true;
                } else if (strcmp(retrieve->data.identifier.name, "@line") == 0) {
                    output_raw_value_qbe(Type_UInt, retrieve->location.row, state);
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

                output_expression_qbe(retrieve->data.array.expression_outer, state);
                output_expression_qbe(retrieve->data.array.expression_inner, state);

                size_t element_size = get_size(array_ast_type_raw->data.array.element_type, &state->generic);

                size_t index_intermediate = array_size_pop(&state->intermediate_stack);
                size_t variable_pointer_intermediate = array_size_pop(&state->intermediate_stack);

                size_t index_offset_immediate = state->intermediate_index;
                char buffer[128] = {};
                sprintf(buffer, "  %%.%zu =l mul %%.%zu, %zu\n", index_offset_immediate, index_intermediate, element_size);
                stringbuffer_appendstring(&state->instructions, buffer);
                state->intermediate_index++;

                size_t index_offset_variable_immediate = state->intermediate_index;
                memset(buffer, 0, 128);
                sprintf(buffer, "  %%.%zu =l add %%.%zu, %%.%zu\n", index_offset_variable_immediate, variable_pointer_intermediate, index_offset_immediate);
                stringbuffer_appendstring(&state->instructions, buffer);
                state->intermediate_index++;

                if (in_reference) {
                    array_size_append(&state->intermediate_stack, index_offset_variable_immediate);
                } else {
                    size_t i = 0;
                    while (i < element_size) {
                        if (i + 8 <= element_size) {
                            size_t temporary_pointer = state->intermediate_index;

                            char buffer[128] = {};
                            sprintf(buffer, "  %%.%zu =l add %%.%zu, %zu\n", temporary_pointer, index_offset_variable_immediate, i);
                            stringbuffer_appendstring(&state->instructions, buffer);
                            state->intermediate_index++;

                            memset(buffer, 0, 128);
                            sprintf(buffer, "  %%.%zu =l loadl %%.%zu\n", state->intermediate_index, temporary_pointer);
                            stringbuffer_appendstring(&state->instructions, buffer);
                            array_size_append(&state->intermediate_stack, state->intermediate_index);
                            state->intermediate_index++;

                            i += 8;
                        } else if (i + 4 <= element_size) {
                            size_t temporary_pointer = state->intermediate_index;

                            char buffer[128] = {};
                            sprintf(buffer, "  %%.%zu =w add %%.%zu, %zu\n", temporary_pointer, index_offset_variable_immediate, i);
                            stringbuffer_appendstring(&state->instructions, buffer);
                            state->intermediate_index++;

                            memset(buffer, 0, 128);
                            sprintf(buffer, "  %%.%zu =w loadw %%.%zu\n", state->intermediate_index, temporary_pointer);
                            stringbuffer_appendstring(&state->instructions, buffer);
                            array_size_append(&state->intermediate_stack, state->intermediate_index);
                            state->intermediate_index++;

                            i += 4;
                        } else if (i + 1 <= element_size) {
                            size_t temporary_pointer = state->intermediate_index;

                            char buffer[128] = {};
                            sprintf(buffer, "  %%.%zu =l add %%.%zu, %zu\n", temporary_pointer, index_offset_variable_immediate, i);
                            stringbuffer_appendstring(&state->instructions, buffer);
                            state->intermediate_index++;

                            memset(buffer, 0, 128);
                            sprintf(buffer, "  %%.%zu =w loadub %%.%zu\n", state->intermediate_index, temporary_pointer);
                            stringbuffer_appendstring(&state->instructions, buffer);
                            array_size_append(&state->intermediate_stack, state->intermediate_index);
                            state->intermediate_index++;

                            i += 1;
                        }
                    }
                }
            }

            if (!found && retrieve->kind == Retrieve_Assign_Parent) {
                found = true;

                bool in_reference = consume_in_reference(&state->generic);
                Ast_Type parent_type = retrieve->data.parent.computed_parent_type;

                if (retrieve->data.parent.needs_reference) {
                    state->generic.in_reference = true;
                }

                output_expression_qbe(retrieve->data.parent.expression, state);

                size_t variable_pointer_intermediate = array_size_pop(&state->intermediate_stack);

                Location_Size_Data location_size = get_parent_item_location_size(&parent_type, retrieve->data.parent.name, &state->generic);
                if (in_reference) {
                    size_t result_intermediate = state->intermediate_index;
                    char buffer[128] = {};
                    sprintf(buffer, "  %%.%zu =l add %%.%zu, %zu\n", result_intermediate, variable_pointer_intermediate, location_size.location);
                    stringbuffer_appendstring(&state->instructions, buffer);
                    state->intermediate_index++;

                    array_size_append(&state->intermediate_stack, result_intermediate);
                } else {
                    size_t i = location_size.size;
                    while (i > 0) {
                        if (i >= 8) {
                            size_t temporary_pointer = state->intermediate_index;

                            char buffer[128] = {};
                            sprintf(buffer, "  %%.%zu =l add %%.%zu, %zu\n", temporary_pointer, variable_pointer_intermediate, location_size.size - i + location_size.location);
                            stringbuffer_appendstring(&state->instructions, buffer);
                            state->intermediate_index++;

                            memset(buffer, 0, 128);
                            sprintf(buffer, "  %%.%zu =l loadl %%.%zu \n", state->intermediate_index, temporary_pointer);
                            stringbuffer_appendstring(&state->instructions, buffer);
                            array_size_append(&state->intermediate_stack, state->intermediate_index);
                            state->intermediate_index++;

                            i -= 8;
                        } else if (i >= 1) {
                            size_t temporary_pointer = state->intermediate_index;

                            char buffer[128] = {};
                            sprintf(buffer, "  %%.%zu =l add %%.%zu, %zu\n", temporary_pointer, variable_pointer_intermediate, location_size.size - i + location_size.location);
                            stringbuffer_appendstring(&state->instructions, buffer);
                            state->intermediate_index++;

                            memset(buffer, 0, 128);
                            sprintf(buffer, "  %%.%zu =w loadub %%.%zu \n", state->intermediate_index, temporary_pointer);
                            stringbuffer_appendstring(&state->instructions, buffer);
                            array_size_append(&state->intermediate_stack, state->intermediate_index);
                            state->intermediate_index++;

                            i -= 1;
                        }
                    }
                }
            }

            if (!found && retrieve->kind == Retrieve_Assign_Identifier) {
                char* name = retrieve->data.identifier.name;
                if (has_local_variable(name, &state->generic)) {
                    found = true;

                    Location_Size_Data location_size = { .location = state->generic.current_arguments.count };

                    bool found = false;
                    for (int i = state->generic.current_declares.count - 1; i >= 0; i--) {
                        Ast_Declaration* declaration = &state->generic.current_declares.elements[i];
                        size_t declaration_size = get_size(&declaration->type, &state->generic);

                        if (found) {
                            location_size.location += 1;
                        }

                        if (!found && strcmp(declaration->name, name) == 0) {
                            location_size.size = declaration_size;
                            found = true;
                        }
                    }

                    size_t variable_pointer_intermediate = state->intermediate_index;
                    char buffer[128] = {};
                    sprintf(buffer, "  %%.%zu =l copy %%.%zu\n", variable_pointer_intermediate, location_size.location);
                    stringbuffer_appendstring(&state->instructions, buffer);
                    state->intermediate_index++;

                    if (consume_in_reference(&state->generic)) {
                        array_size_append(&state->intermediate_stack, variable_pointer_intermediate);
                    } else {
                        Array_Size indexes = array_size_new(4);
                        size_t size = location_size.size;
                        size_t i = size;
                        while (i > 0) {
                            if (i >= 8) {
                                array_size_append(&indexes, 8);
                                i -= 8;
                            } else if (i >= 1) {
                                array_size_append(&indexes, 1);
                                i -= 1;
                            }
                        }

                        size_t total = 0;
                        for (size_t i = 0; i < indexes.count; i++) {
                            size_t size_temp = array_size_get(&indexes, i);
                            if (size_temp == 8) {
                                size_t temporary_pointer = state->intermediate_index;

                                char buffer[128] = {};
                                sprintf(buffer, "  %%.%zu =l add %%.%zu, %zu\n", temporary_pointer, variable_pointer_intermediate, total);
                                stringbuffer_appendstring(&state->instructions, buffer);
                                state->intermediate_index++;

                                memset(buffer, 0, 128);
                                sprintf(buffer, "  %%.%zu =l loadl %%.%zu \n", state->intermediate_index, temporary_pointer);
                                stringbuffer_appendstring(&state->instructions, buffer);
                                array_size_append(&state->intermediate_stack, state->intermediate_index);
                                state->intermediate_index++;
                            } else if (size_temp == 1) {
                                size_t temporary_pointer = state->intermediate_index;

                                char buffer[128] = {};
                                sprintf(buffer, "  %%.%zu =l add %%.%zu, %zu\n", temporary_pointer, variable_pointer_intermediate, total);
                                stringbuffer_appendstring(&state->instructions, buffer);
                                state->intermediate_index++;

                                memset(buffer, 0, 128);
                                sprintf(buffer, "  %%.%zu =w loadub %%.%zu \n", state->intermediate_index, temporary_pointer);
                                stringbuffer_appendstring(&state->instructions, buffer);
                                array_size_append(&state->intermediate_stack, state->intermediate_index);
                                state->intermediate_index++;
                            }
                            total += size_temp;
                        }
                    }
                }
            }

            if (!found && retrieve->kind == Retrieve_Assign_Identifier) {
                char* name = retrieve->data.identifier.name;
                if (has_argument(name, &state->generic)) {
                    found = true;

                    Location_Size_Data location_size = { .location = 0 };

                    bool found = false;
                    for (size_t i = 0; i < state->generic.current_arguments.count; i++) {
                        Ast_Declaration* declaration = &state->generic.current_arguments.elements[i];
                        size_t declaration_size = get_size(&declaration->type, &state->generic);

                        if (!found && strcmp(declaration->name, name) == 0) {
                            location_size.size = declaration_size;
                            found = true;
                        }

                        if (!found) {
                            location_size.location += 1;
                        }
                    }

                    if (consume_in_reference(&state->generic)) {
                        char buffer[128] = {};
                        sprintf(buffer, "  %%.%zu =l copy %%.%zu\n", state->intermediate_index, location_size.location);
                        stringbuffer_appendstring(&state->instructions, buffer);
                        array_size_append(&state->intermediate_stack, state->intermediate_index);
                        state->intermediate_index++;
                    } else {
                        size_t variable_pointer_intermediate = state->intermediate_index;
                        char buffer[128] = {};
                        sprintf(buffer, "  %%.%zu =l copy %%.%zu\n", state->intermediate_index, location_size.location);
                        stringbuffer_appendstring(&state->instructions, buffer);
                        state->intermediate_index++;

                        size_t size = location_size.size;
                        size_t i = size;
                        while (i > 0) {
                            if (i >= 8) {
                                size_t temporary_pointer = state->intermediate_index;

                                char buffer[128] = {};
                                sprintf(buffer, "  %%.%zu =l add %%.%zu, %zu\n", temporary_pointer, variable_pointer_intermediate, size - i);
                                stringbuffer_appendstring(&state->instructions, buffer);
                                state->intermediate_index++;

                                memset(buffer, 0, 128);
                                sprintf(buffer, "  %%.%zu =l loadl %%.%zu \n", state->intermediate_index, temporary_pointer);
                                stringbuffer_appendstring(&state->instructions, buffer);
                                array_size_append(&state->intermediate_stack, state->intermediate_index);
                                state->intermediate_index++;

                                i -= 8;
                            } else if (i >= 4) {
                                size_t temporary_pointer = state->intermediate_index;

                                char buffer[128] = {};
                                sprintf(buffer, "  %%.%zu =l add %%.%zu, %zu\n", temporary_pointer, variable_pointer_intermediate, size - i);
                                stringbuffer_appendstring(&state->instructions, buffer);
                                state->intermediate_index++;

                                memset(buffer, 0, 128);
                                sprintf(buffer, "  %%.%zu =w loadw %%.%zu \n", state->intermediate_index, temporary_pointer);
                                stringbuffer_appendstring(&state->instructions, buffer);
                                array_size_append(&state->intermediate_stack, state->intermediate_index);
                                state->intermediate_index++;

                                i -= 4;
                            } else if (i >= 2) {
                                size_t temporary_pointer = state->intermediate_index;

                                char buffer[128] = {};
                                sprintf(buffer, "  %%.%zu =l add %%.%zu, %zu\n", temporary_pointer, variable_pointer_intermediate, size - i);
                                stringbuffer_appendstring(&state->instructions, buffer);
                                state->intermediate_index++;

                                memset(buffer, 0, 128);
                                sprintf(buffer, "  %%.%zu =w loadh %%.%zu \n", state->intermediate_index, temporary_pointer);
                                stringbuffer_appendstring(&state->instructions, buffer);
                                array_size_append(&state->intermediate_stack, state->intermediate_index);
                                state->intermediate_index++;

                                i -= 2;
                            } else if (i >= 1) {
                                size_t temporary_pointer = state->intermediate_index;

                                char buffer[128] = {};
                                sprintf(buffer, "  %%.%zu =l add %%.%zu, %zu\n", temporary_pointer, variable_pointer_intermediate, size - i);
                                stringbuffer_appendstring(&state->instructions, buffer);
                                state->intermediate_index++;

                                memset(buffer, 0, 128);
                                sprintf(buffer, "  %%.%zu =w loadub %%.%zu \n", state->intermediate_index, temporary_pointer);
                                stringbuffer_appendstring(&state->instructions, buffer);
                                array_size_append(&state->intermediate_stack, state->intermediate_index);
                                state->intermediate_index++;

                                i -= 1;
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

                output_raw_value_qbe(Type_UInt64, index, state);

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
                                sprintf(buffer, "  %%.%zu =l copy $%s\n", state->intermediate_index, item->data.procedure.name);
                                stringbuffer_appendstring(&state->instructions, buffer);
                                array_size_append(&state->intermediate_stack, state->intermediate_index);
                                state->intermediate_index++;
                                break;
                            }
                            case Item_Global: {
                                Ast_Item_Global* global = &item->data.global;
                                size_t variable_pointer_intermediate = state->intermediate_index;
                                char buffer[128] = {};
                                sprintf(buffer, "  %%.%zu =l copy $%s\n", variable_pointer_intermediate, global->name);
                                stringbuffer_appendstring(&state->instructions, buffer);
                                state->intermediate_index++;

                                if (consume_in_reference(&state->generic)) {
                                    array_size_append(&state->intermediate_stack, variable_pointer_intermediate);
                                } else {
                                    size_t size = get_size(&global->type, &state->generic);

                                    Array_Size indexes = array_size_new(4);
                                    size_t i = size;
                                    while (i > 0) {
                                        if (i >= 8) {
                                            array_size_append(&indexes, 8);
                                            i -= 8;
                                        } else if (i >= 1) {
                                            array_size_append(&indexes, 1);
                                            i -= 1;
                                        }
                                    }

                                    size_t total = 0;
                                    for (size_t i = 0; i < indexes.count; i++) {
                                        size_t size_temp = array_size_get(&indexes, i);
                                        if (size_temp == 8) {
                                            size_t temporary_pointer = state->intermediate_index;

                                            char buffer[128] = {};
                                            sprintf(buffer, "  %%.%zu =l add %%.%zu, %zu\n", temporary_pointer, variable_pointer_intermediate, total);
                                            stringbuffer_appendstring(&state->instructions, buffer);
                                            state->intermediate_index++;

                                            memset(buffer, 0, 128);
                                            sprintf(buffer, "  %%.%zu =l loadl %%.%zu \n", state->intermediate_index, temporary_pointer);
                                            stringbuffer_appendstring(&state->instructions, buffer);
                                            array_size_append(&state->intermediate_stack, state->intermediate_index);
                                            state->intermediate_index++;
                                        } else if (size_temp == 1) {
                                            size_t temporary_pointer = state->intermediate_index;

                                            char buffer[128] = {};
                                            sprintf(buffer, "  %%.%zu =l add %%.%zu, %zu\n", temporary_pointer, variable_pointer_intermediate, total);
                                            stringbuffer_appendstring(&state->instructions, buffer);
                                            state->intermediate_index++;

                                            memset(buffer, 0, 128);
                                            sprintf(buffer, "  %%.%zu =w loadub %%.%zu \n", state->intermediate_index, temporary_pointer);
                                            stringbuffer_appendstring(&state->instructions, buffer);
                                            array_size_append(&state->intermediate_stack, state->intermediate_index);
                                            state->intermediate_index++;
                                        }
                                        total += size_temp;
                                    }
                                }
                                break;
                            }
                            case Item_Constant: {
                                Ast_Item_Constant* constant = &item->data.constant;
                                Ast_Expression_Number number = constant->expression;
                                number.type = retrieve->computed_result_type;
                                Ast_Expression expression_temp = { .kind = Expression_Number, .data = { .number = number } };
                                output_expression_qbe(&expression_temp, state);
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

            output_expression_qbe(node->condition, state);

            size_t temp = state->flow_index;
            state->flow_index++;

            char buffer[128] = {};
            sprintf(buffer, "  jnz %%.%zu, @__%zu, @__%zu\n", array_size_pop(&state->intermediate_stack), temp, else_);
            stringbuffer_appendstring(&state->instructions, buffer);

            memset(buffer, 0, 128);
            sprintf(buffer, "  @__%zu\n", temp);
            stringbuffer_appendstring(&state->instructions, buffer);

            output_expression_qbe(node->if_expression, state);

            memset(buffer, 0, 128);
            sprintf(buffer, "  jmp @__%zu\n", end);
            stringbuffer_appendstring(&state->instructions, buffer);

            memset(buffer, 0, 128);
            sprintf(buffer, "  @__%zu\n", else_);
            stringbuffer_appendstring(&state->instructions, buffer);

            if (node->else_expression != NULL) {
                output_expression_qbe(node->else_expression, state);
            }

            memset(buffer, 0, 128);
            sprintf(buffer, "  @__%zu\n", end);
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

                output_raw_value_qbe(type, value.s, state);
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
                output_raw_value_qbe(type, value, state);
            }
            break;
        }
        case Expression_Boolean: {
            Ast_Expression_Boolean* boolean = &expression->data.boolean;
            output_boolean_qbe(boolean->value, state);
            break;
        }
        case Expression_Null: {
            output_raw_value_qbe(Type_UInt64, 0, state);
            break;
        }
        case Expression_String: {
            Ast_Expression_String* string = &expression->data.string;
            output_string_qbe(string->value, state);
            break;
        }
        case Expression_Char: {
            Ast_Expression_Char* char_ = &expression->data.char_;
            output_raw_value_qbe(Type_Byte, char_->value, state);
            break;
        }
        case Expression_Reference: {
            Ast_Expression_Reference* reference = &expression->data.reference;
            state->generic.in_reference = true;

            output_expression_qbe(reference->inner, state);
            break;
        }
        case Expression_Cast: {
            Ast_Expression_Cast* cast = &expression->data.cast;

            output_expression_qbe(cast->expression, state);

            Ast_Type input = cast->computed_input_type;
            Ast_Type output = cast->type;

            if (cast->type.kind == Type_Internal && cast->computed_input_type.kind == Type_Internal) {
                Ast_Type_Internal output_internal = output.data.internal;
                Ast_Type_Internal input_internal = input.data.internal;

                if ((input_internal == Type_UInt || input_internal == Type_UInt64 || input_internal == Type_UInt32 || input_internal == Type_UInt16 || input_internal == Type_UInt8) && 
                        (output_internal == Type_UInt || output_internal == Type_UInt64 || output_internal == Type_UInt32 || output_internal == Type_UInt16 || output_internal == Type_UInt8)) {
                    size_t input_size = get_size(&cast->computed_input_type, &state->generic);

                    size_t intermediate = state->intermediate_index;
                    state->intermediate_index++;
                    char buffer[128] = {};
                    if (input_size == 8) {
                        sprintf(buffer, "  %%.%zu =l copy %%.%zu\n", intermediate, array_size_pop(&state->intermediate_stack));
                    } else if (input_size == 4) {
                        sprintf(buffer, "  %%.%zu =l extuw %%.%zu\n", intermediate, array_size_pop(&state->intermediate_stack));
                    } else if (input_size == 2) {
                        sprintf(buffer, "  %%.%zu =l extuh %%.%zu\n", intermediate, array_size_pop(&state->intermediate_stack));
                    } else if (input_size == 1) {
                        sprintf(buffer, "  %%.%zu =l extub %%.%zu\n", intermediate, array_size_pop(&state->intermediate_stack));
                    }
                    stringbuffer_appendstring(&state->instructions, buffer);

                    size_t output_size = get_size(&cast->type, &state->generic);
                    size_t output_intermediate = state->intermediate_index;
                    state->intermediate_index++;

                    memset(buffer, 0, 128);
                    if (output_size == 8) {
                        sprintf(buffer, "  %%.%zu =l copy %%.%zu\n", output_intermediate, intermediate);
                    } else if (output_size == 4) {
                        sprintf(buffer, "  %%.%zu =w copy %%.%zu\n", output_intermediate, intermediate);
                    } else if (output_size == 2) {
                        sprintf(buffer, "  %%.%zu =w copy %%.%zu\n", output_intermediate, intermediate);
                    } else if (output_size == 1) {
                        sprintf(buffer, "  %%.%zu =w copy %%.%zu\n", output_intermediate, intermediate);
                    }
                    stringbuffer_appendstring(&state->instructions, buffer);
                    array_size_append(&state->intermediate_stack, output_intermediate);
                } else if (input_internal == Type_Float64 && output_internal == Type_UInt64) {
                    size_t input_intermediate = state->intermediate_index;
                    char buffer[128] = {};
                    sprintf(buffer, "  %%.%zu =d cast %%.%zu\n", input_intermediate, array_size_pop(&state->intermediate_stack));
                    stringbuffer_appendstring(&state->instructions, buffer);
                    state->intermediate_index++;

                    size_t output_intermediate = state->intermediate_index;
                    memset(buffer, 0, 128);
                    sprintf(buffer, "  %%.%zu =l dtoui %%.%zu\n", output_intermediate, input_intermediate);
                    array_size_append(&state->intermediate_stack, output_intermediate);
                    state->intermediate_index++;
                    stringbuffer_appendstring(&state->instructions, buffer);
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
            output_zeroes_qbe(get_size(&init->type, &state->generic), state);
            break;
        }
        case Expression_Build: {
            Ast_Expression_Build* build = &expression->data.build;
            output_build_type_qbe(build, &build->type, state);
            break;
        }
        case Expression_SizeOf: {
            Ast_Expression_SizeOf* size_of = &expression->data.size_of;
            output_raw_value_qbe(size_of->computed_result_type.data.internal, get_size(&size_of->type, &state->generic), state);
            break;
        }
        case Expression_LengthOf: {
            Ast_Expression_LengthOf* length_of = &expression->data.length_of;
            output_raw_value_qbe(length_of->computed_result_type.data.internal, get_length(&length_of->type), state);
            break;
        }
        default:
            assert(false);
    }
}

typedef struct {
    Output_State* state;
} Locals_Walk_State;

void collect_statement_locals_qbe(Ast_Statement* statement, void* state_in) {
    Locals_Walk_State* state = state_in;
    switch (statement->kind) {
        case Statement_Declare: {
            for (size_t i = 0; i < statement->data.declare.declarations.count; i++) {
                Ast_Type type = statement->data.declare.declarations.elements[i].type;
                char buffer[128] = {};
                sprintf(buffer, "  %%.%zu =l alloc8 %zu\n", state->state->intermediate_index, get_size(&type, &state->state->generic));
                stringbuffer_appendstring(&state->state->instructions, buffer);
                state->state->intermediate_index++;
            }
        }
        default:
            break;
    }
}

void output_item_qbe(Ast_Item* item, Output_State* state) {
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

            if (has_directive(&item->directives, Directive_Entry)) {
                state->entry = procedure->name;
            }

            state->intermediate_index = 0;

            bool has_returns = procedure->returns.count > 0;
            size_t returns_size = 0;

            if (has_returns) {
                for (size_t i = 0; i < procedure->returns.count; i++) {
                    returns_size += get_size(procedure->returns.elements[i], &state->generic);
                }

                char buffer[128] = {};
                sprintf(buffer, "type :.%zu = {", returns_size);
                stringbuffer_appendstring(&state->types, buffer);

                for (size_t i = 0; i < procedure->returns.count; i++) {
                    size_t return_size = get_size(procedure->returns.elements[i], &state->generic);

                    size_t j = 0;
                    while (j < return_size) {
                        if (j != 0 || i != 0) {
                            stringbuffer_appendstring(&state->types, ",");
                        }

                        if (j + 8 <= return_size) {
                            stringbuffer_appendstring(&state->types, "l");
                            j += 8;
                        } else {
                            stringbuffer_appendstring(&state->types, "b");
                            j += 1;
                        }
                    }
                }

                stringbuffer_appendstring(&state->types, "}\n");
            }


            stringbuffer_appendstring(&state->instructions, "export function ");

            if (has_returns) {
                char buffer[128] = {};
                sprintf(buffer, ":.%zu ", returns_size);
                stringbuffer_appendstring(&state->instructions, buffer);
            }
            char buffer[128] = {};
            sprintf(buffer, "$%s(", procedure->name);
            stringbuffer_appendstring(&state->instructions, buffer);

            size_t real_arg_index = 0;
            size_t temp_arg_index = 0;
            for (size_t i = 0; i < procedure->arguments.count; i++) {
                size_t arg_size = get_size(&procedure->arguments.elements[i].type, &state->generic);

                Array_Size indexes = array_size_new(4);

                size_t j = 0;
                while (j < arg_size) {
                    if (j + 8 <= arg_size) {
                        array_size_append(&indexes, 8);
                        real_arg_index++;
                        j += 8;
                    } else {
                        array_size_append(&indexes, 1);
                        real_arg_index++;
                        j += 1;
                    }
                }

                for (size_t j = 0; j < indexes.count; j++) {
                    size_t temp_size = indexes.elements[j];
                    if (j != 0 || i != 0) {
                        stringbuffer_appendstring(&state->instructions, ", ");
                    }

                    if (temp_size == 8) {
                        char buffer[128] = {};
                        sprintf(buffer, "l %%.a%zu", temp_arg_index);
                        stringbuffer_appendstring(&state->instructions, buffer);
                        temp_arg_index++;
                    } else {
                        char buffer[128] = {};
                        sprintf(buffer, "w %%.a%zu", temp_arg_index);
                        stringbuffer_appendstring(&state->instructions, buffer);
                        temp_arg_index++;
                    }
                }
            }

            stringbuffer_appendstring(&state->instructions, ") {\n");

            stringbuffer_appendstring(&state->instructions, "@start\n");

            real_arg_index = 0;
            for (size_t i = 0; i < procedure->arguments.count; i++) {
                size_t arg_size = get_size(&procedure->arguments.elements[i].type, &state->generic);

                size_t arg_local_intermediate = i;
                char buffer[128] = {};
                sprintf(buffer, "  %%.%zu =l alloc8 %zu\n", arg_local_intermediate, arg_size);
                stringbuffer_appendstring(&state->instructions, buffer);

                size_t temp_intermediate_index = 0;
                size_t j = 0;
                while (j < arg_size) {
                    if (j + 8 <= arg_size) {
                        size_t temporary_pointer = temp_intermediate_index;
                        char buffer[128] = {};
                        sprintf(buffer, "  %%.i%zu =l add %%.%zu, %zu\n", temporary_pointer, arg_local_intermediate, j);
                        stringbuffer_appendstring(&state->instructions, buffer);
                        temp_intermediate_index++;

                        memset(buffer, 0, 128);
                        sprintf(buffer, "  storel %%.a%zu, %%.i%zu\n", real_arg_index, temporary_pointer);
                        stringbuffer_appendstring(&state->instructions, buffer);
                        real_arg_index++;

                        j += 8;
                    } else {
                        size_t temporary_pointer = temp_intermediate_index;
                        char buffer[128] = {};
                        sprintf(buffer, "  %%.i%zu =l add %%.%zu, %zu\n", temporary_pointer, arg_local_intermediate, j);
                        stringbuffer_appendstring(&state->instructions, buffer);
                        temp_intermediate_index++;

                        memset(buffer, 0, 128);
                        sprintf(buffer, "  storeb %%.a%zu, %%.i%zu\n", real_arg_index, temporary_pointer);
                        stringbuffer_appendstring(&state->instructions, buffer);
                        real_arg_index++;

                        j += 1;
                    }
                }
            }

            state->intermediate_index += procedure->arguments.count;

            if (has_returns) {
                size_t returns_size = 0;
                for (size_t i = 0; i < procedure->returns.count; i++) {
                    returns_size += get_size(procedure->returns.elements[i], &state->generic);
                }

                char buffer[128] = {};
                sprintf(buffer, "  %%.r =l alloc8 %zu\n", returns_size);
                stringbuffer_appendstring(&state->instructions, buffer);
            }

            Locals_Walk_State locals_state = {
                .state = state,
            };
            Ast_Walk_State walk_state = {
                .expression_func = NULL,
                .statement_func = collect_statement_locals_qbe,
                .internal_state = &locals_state,
            };
            walk_expression(procedure->body, &walk_state);

            output_expression_qbe(procedure->body, state);

            if (procedure->has_implicit_return) {
                output_actual_return_qbe(state);
            }
            stringbuffer_appendstring(&state->instructions, "  ret\n");

            stringbuffer_appendstring(&state->instructions, "}\n");
            break;
        }
        case Item_Global: {
            Ast_Item_Global* global = &item->data.global;
            size_t size = get_size(&global->type, &state->generic);

            char buffer[128] = {};
            sprintf(buffer, "data $%s = { z %zu }\n", global->name, size);
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

void output_qbe(Program* program, char* output_file) {
    Output_State state = (Output_State) {
        .generic = (Generic_State) {
            .program = program,
            .current_file = NULL,
            .current_declares = {},
            .current_arguments = {},
            .in_reference = false,
        },
        .types = stringbuffer_new(16384),
        .instructions = stringbuffer_new(16384),
        .data = stringbuffer_new(16384),
        .bss = stringbuffer_new(16384),
        .string_index = 0,
        .flow_index = 0,
        .intermediate_stack = array_size_new(16),
        .while_index = array_size_new(4),
    };

    for (size_t j = 0; j < program->count; j++) {
        Ast_File* file_node = &program->elements[j];
        state.generic.current_file = file_node;

        for (size_t i = 0; i < file_node->items.count; i++) {
            Ast_Item* item = &file_node->items.elements[i];
            output_item_qbe(item, &state);
        }
    }

    FILE* file = fopen(output_file, "w");

    fprintf(file, "export function $main(l %%.argc, l %%.argv) {\n");
    fprintf(file, "@start\n");
    fprintf(file, "  %%.argc2 =l copy %%.argc\n");
    fprintf(file, "  call $%s(l %%.argc2, l %%.argv)\n", state.entry);
    fprintf(file, "  ret\n");
    fprintf(file, "}\n");


    fwrite(state.types.elements, state.types.count, 1, file);
    fwrite(state.instructions.elements, state.instructions.count, 1, file);
    fwrite(state.data.elements, state.data.count, 1, file);
    fwrite(state.bss.elements, state.bss.count, 1, file);

    fclose(file);
}
