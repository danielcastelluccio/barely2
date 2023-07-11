#include <stdio.h>

#include "ast_print.h"
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

typedef struct {
    File_Node* file_node;
    Stack_Type stack;
    Array_Declaration current_declares;
    Array_Declaration current_arguments;
    Array_Type current_returns;
    Expression_Node* current_body;
    bool in_reference;
    Type* wanted_type;
} Process_State;

Definition_Node* resolve_definition(File_Node* file_node, Complex_Name* data) {
    for (size_t i = 0; i < file_node->definitions.count; i++) {
        Definition_Node* definition = &file_node->definitions.elements[i];
        // TODO: support multi retrieves & assigns
        if (data->kind == Complex_Single && strcmp(definition->name, data->data.single.name) == 0) {
            return definition;
        }
    }

    return NULL;
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

    return false;
}

void print_type_inline(Type* type) {
    switch (type->kind) {
        case Type_Basic: {
            Basic_Type* basic = &type->data.basic;

            if (basic->kind == Type_Single) {
                printf("%s", basic->data.single);
            } else {
                printf("names: [");
                for (int i = 0; i < basic->data.multi.count; i++) {
                    printf("%s", basic->data.multi.elements[i]);
                    if (i < basic->data.multi.count - 1) {
                        printf(" ");
                    }
                }
                printf("]\n");
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
                printf("%i", array->size);
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
    }
}

void print_error_stub(Location* location) {
    printf("%s:%i:%i: ", location->file, location->row, location->col);
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
                    for (int i = 0; i < declare->expression->data.multi.expressions.count; i++) {
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
            process_expression(assign->expression, state);

            for (int i = assign->parts.count - 1; i >= 0; i--) {
                Statement_Assign_Part* assign_part = &assign->parts.elements[i];

                if (assign_part->kind == Complex_Array) {
                    process_expression(assign_part->data.array.expression_outer, state);
                    Type popped_outer = stack_type_pop(&state->stack);

                    Type* child;
                    if (popped_outer.kind == Type_Pointer) {
                        child = popped_outer.data.pointer.child;
                    } else {
                        child = &popped_outer;
                    }

                    assign_part->data.array.added_type = popped_outer;

                    process_expression(assign_part->data.array.expression_inner, state);
                    Type inner_popped = stack_type_pop(&state->stack);
                    Type u64 = create_basic_single_type("u64");
                    // TODO: probably not what I want
                    if (!is_type(&u64, &inner_popped)) {
                        print_error_stub(&assign_part->location);
                        printf("Type '");
                        print_type_inline(&inner_popped);
                        printf("' cannot be used to access array\n");
                        exit(1);
                    }

                    if (state->stack.count == 0) {
                        print_error_stub(&assign_part->location);
                        printf("Ran out of values for assignment\n");
                        exit(1);
                    }

                    Type popped = stack_type_pop(&state->stack);
                    if (!is_type(child->data.array.element_type, &popped)) {
                        print_error_stub(&assign_part->location);
                        printf("Type '");
                        print_type_inline(&popped);
                        printf("' is not assignable to index of array of type '");
                        print_type_inline(child->data.array.element_type);
                        printf("'\n");
                        exit(1);
                    }
                }

                if (assign_part->kind == Complex_Single) {
                    if (assign_part->data.single.expression == NULL) {
                        char* name = assign_part->data.single.name;

                        Type type;
                        for (size_t j = 0; j < state->current_declares.count; j++) {
                            if (strcmp(state->current_declares.elements[j].name, name) == 0) {
                                type = state->current_declares.elements[j].type;
                                break;
                            }
                        }

                        if (state->stack.count == 0) {
                            print_error_stub(&assign_part->location);
                            printf("Ran out of values for declaration assignment\n");
                            exit(1);
                        }

                        Type popped = stack_type_pop(&state->stack);
                        if (!is_type(&type, &popped)) {
                            print_error_stub(&assign_part->location);
                            printf("Type '");
                            print_type_inline(&popped);
                            printf("' is not assignable to variable of type '");
                            print_type_inline(&type);
                            printf("'\n");
                            exit(1);
                        }
                    } else {
                        process_expression(assign_part->data.single.expression, state);
                        Type popped = stack_type_pop(&state->stack);

                        assign_part->data.single.added_type = popped;

                        Type* child;
                        if (popped.kind == Type_Pointer) {
                            child = popped.data.pointer.child;
                        } else {
                            child = &popped;
                        }

                        Type type;
                        if (strcmp(assign_part->data.single.name, "*") == 0) {
                            type = *child;
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
                                if (strcmp(declaration->name, assign_part->data.single.name) == 0) {
                                    type = declaration->type;
                                }
                            }
                        }

                        Type right_side_popped = stack_type_pop(&state->stack);
                        if (!is_type(&right_side_popped, &type)) {
                            print_error_stub(&assign_part->location);
                            printf("Type '");
                            print_type_inline(&popped);
                            printf("' is not assignable to item of type '");
                            print_type_inline(&type);
                            printf("'\n");
                            exit(1);
                        }
                    }
                }
            }

            break;
        }
        case Statement_Return: {
            Statement_Return_Node* return_ = &statement->data.return_;
            process_expression(return_->expression, state);

            for (size_t i = 0; i < state->current_returns.count; i++) {
                if (state->stack.count == 0) {
                    print_error_stub(&return_->location);
                    printf("Ran out of values for return\n");
                    exit(1);
                }

                Type* return_type = state->current_returns.elements[i];
                Type given = stack_type_pop(&state->stack);
                if (!is_type(return_type, &given)) {
                    print_error_stub(&return_->location);
                    printf("Type '");
                    print_type_inline(&given);
                    printf("' is not assignable to return of type '");
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

void process_expression(Expression_Node* expression, Process_State* state) {
    switch (expression->kind) {
        case Expression_Block: {
            Block_Node* block = &expression->data.block;
            for (size_t i = 0; i < block->statements.count; i++) {
                process_statement(block->statements.elements[i], state);
            }
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
                    char* name = procedure->data.retrieve.data.single.name;

                    if (strcmp(name, "syscall6") == 0) {
                        size_t count = 7;
                        if (state->stack.count < count) {
                            print_error_stub(&invoke->location);
                            printf("Ran out of values for %s, needed %i\n", name, count);
                            exit(1);
                        }

                        for (int i = 0; i < count; i++) {
                            stack_type_pop(&state->stack);
                        }
                        handled = true;
                    } else if (strcmp(name, "syscall5") == 0) {
                        size_t count = 6;
                        if (state->stack.count < count) {
                            print_error_stub(&invoke->location);
                            printf("Ran out of values for %s, needed %i\n", name, count);
                            exit(1);
                        }

                        for (int i = 0; i < count; i++) {
                            stack_type_pop(&state->stack);
                        }
                        handled = true;
                    } else if (strcmp(name, "syscall4") == 0) {
                        size_t count = 5;
                        if (state->stack.count < count) {
                            print_error_stub(&invoke->location);
                            printf("Ran out of values for %s, needed %i\n", name, count);
                            exit(1);
                        }

                        for (int i = 0; i < count; i++) {
                            stack_type_pop(&state->stack);
                        }
                        handled = true;
                    } else if (strcmp(name, "syscall3") == 0) {
                        size_t count = 4;
                        if (state->stack.count < count) {
                            print_error_stub(&invoke->location);
                            printf("Ran out of values for %s, needed %i\n", name, count);
                            exit(1);
                        }

                        for (int i = 0; i < count; i++) {
                            stack_type_pop(&state->stack);
                        }
                        handled = true;
                    } else if (strcmp(name, "syscall2") == 0) {
                        size_t count = 3;
                        if (state->stack.count < count) {
                            print_error_stub(&invoke->location);
                            printf("Ran out of values for %s, needed %i\n", name, count);
                            exit(1);
                        }

                        for (int i = 0; i < count; i++) {
                            stack_type_pop(&state->stack);
                        }
                        handled = true;
                    } else if (strcmp(name, "syscall1") == 0) {
                        size_t count = 2;
                        if (state->stack.count < count) {
                            print_error_stub(&invoke->location);
                            printf("Ran out of values for %s, needed %i\n", name, count);
                            exit(1);
                        }

                        for (int i = 0; i < count; i++) {
                            stack_type_pop(&state->stack);
                        }
                        handled = true;
                    } else if (strcmp(name, "syscall0") == 0) {
                        size_t count = 1;
                        if (state->stack.count < count) {
                            print_error_stub(&invoke->location);
                            printf("Ran out of values for %s, needed %i\n", name, count);
                            exit(1);
                        }

                        for (int i = 0; i < count; i++) {
                            stack_type_pop(&state->stack);
                        }
                        handled = true;
                    }

                    if (handled) {
                        for (size_t i = 0; i < invoke->arguments.count; i++) {
                            process_expression(invoke->arguments.elements[i], state);
                        }
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
                        state->wanted_type = procedure_type->arguments.elements[arg_index];
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

                for (size_t i = 0; i < invoke->arguments.count; i++) {
                    process_expression(invoke->arguments.elements[i], state);
                }

                if (operator == Operator_Add ||
                        operator == Operator_Subtract ||
                        operator == Operator_Multiply ||
                        operator == Operator_Divide) {
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

                    invoke->data.operator.added_type = first;
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

                    invoke->data.operator.added_type = first;

                    stack_type_push(&state->stack, create_basic_single_type("bool"));
                }
            }
            break;
        }
        case Expression_Retrieve: {
            Retrieve_Node* retrieve = &expression->data.retrieve;
            bool found = false;

            if (!found) {
                if (retrieve->kind == Complex_Array) {
                    process_expression(retrieve->data.array.expression_outer, state);
                    Type popped = stack_type_pop(&state->stack);

                    Type* child;
                    if (popped.kind == Type_Pointer) {
                        child = popped.data.pointer.child;
                    } else {
                        child = &popped;
                    }

                    retrieve->data.array.added_type = popped;

                    process_expression(retrieve->data.array.expression_inner, state);
                    Type inner_popped = stack_type_pop(&state->stack);
                    Type u64 = create_basic_single_type("u64");
                    // TODO: probably not what I want
                    if (!is_type(&u64, &inner_popped)) {
                        print_error_stub(&retrieve->location);
                        printf("Type '");
                        print_type_inline(&inner_popped);
                        printf("' cannot be used to access array\n");
                        exit(1);
                    }

                    stack_type_push(&state->stack, *child->data.array.element_type);
                    found = true;
                }
            }

            if (!found) {
                if (retrieve->kind == Complex_Single) {
                    if (retrieve->data.single.expression == NULL) {
                        Type variable_type;
                        for (size_t i = 0; i < state->current_declares.count; i++) {
                            Declaration* declaration = &state->current_declares.elements[i];
                            if (strcmp(declaration->name, retrieve->data.single.name) == 0) {
                                variable_type = declaration->type;
                                found = true;
                                break;
                            }
                        }

                        if (found) {
                            if (state->in_reference) {
                                stack_type_push(&state->stack, create_pointer_type(variable_type));

                                state->in_reference = false;
                            } else {
                                stack_type_push(&state->stack, variable_type);
                            }
                        }
                    } else {
                        process_expression(retrieve->data.single.expression, state);
                        Type popped = stack_type_pop(&state->stack);

                        retrieve->data.single.added_type = popped;

                        Type* child;
                        if (popped.kind == Type_Pointer) {
                            child = popped.data.pointer.child;
                        } else {
                            child = &popped;
                        }

                        Type type;
                        if (strcmp(retrieve->data.single.name, "*") == 0) {
                            type = *child;
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
                                if (strcmp(declaration->name, retrieve->data.single.name) == 0) {
                                    type = declaration->type;
                                }
                            }
                        }

                        stack_type_push(&state->stack, type);
                    }
                }
            }

            if (!found) {
                if (retrieve->kind == Complex_Single) {
                    Type type;
                    for (int i = state->current_arguments.count - 1; i >= 0; i--) {
                        Declaration* declaration = &state->current_arguments.elements[i];
                        if (strcmp(declaration->name, retrieve->data.single.name) == 0) {
                            type = declaration->type;
                            found = true;
                            break;
                        }
                    }

                    if (found) {
                        stack_type_push(&state->stack, type);
                    }
                }
            }

            if (!found) {
                Definition_Node* definition = resolve_definition(state->file_node, retrieve);
                if (definition != NULL) {
                    found = true;
                    switch (definition->kind) {
                        case Definition_Procedure:
                            Procedure_Literal_Node* procedure = &definition->data.procedure.data.literal;

                            // TODO: Probably want a full procedure type
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
                        default:
                            printf("Unhandled definition retrieve!\n");
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
        case Expression_Number: {
            Number_Node* number = &expression->data.number;
            Type* wanted = state->wanted_type;

            bool found = false;
            if (wanted != NULL && wanted->kind == Type_Basic) {
                char* basic_name = wanted->data.basic.data.single;

                if (strcmp(basic_name, "u64") == 0 || strcmp(basic_name, "u32") == 0 || strcmp(basic_name, "u8") == 0) {
                    found = true;
                }
            }

            if (found) {
                stack_type_push(&state->stack, *wanted);
                number->type = wanted;
            } else {
                Type* u64 = malloc(sizeof(Type));
                *u64 = create_basic_single_type("u64");
                stack_type_push(&state->stack, *u64);
                number->type = u64;
            }
            break;
        }
        case Expression_Boolean: {
            stack_type_push(&state->stack, create_basic_single_type("bool"));
            break;
        }
        case Expression_String: {
            stack_type_push(&state->stack, create_pointer_type(create_array_type(create_basic_single_type("u8"))));
            break;
        }
        case Expression_Reference: {
            Reference_Node* reference = &expression->data.reference;
            state->in_reference = true;

            process_expression(reference->inner, state);
            break;
        }
        default:
            printf("Unhandled expression_type!\n");
            exit(1);
    }
}

void process_definition(Definition_Node* definition, Process_State* state) {
    switch (definition->kind) {
        case Definition_Procedure: {
            Procedure_Node* procedure = &definition->data.procedure;
            state->current_declares = array_declaration_new(4);
            state->current_arguments = procedure->data.literal.arguments;
            state->current_returns = procedure->data.literal.returns;
            state->current_body = procedure->data.literal.body;

            process_expression(procedure->data.literal.body, state);
            break;
        }
        case Definition_Type:
            break;
        default:
            printf("Unhandled definition_type!\n");
            exit(1);
    }
}

void process(File_Node* file_node) {
    Process_State state = (Process_State) {
        file_node,
        stack_type_new(8),
    };

    for (size_t i = 0; i < file_node->definitions.count; i++) {
        Definition_Node* definition = &file_node->definitions.elements[i];
        process_definition(definition, &state);
    }
}