#include <assert.h>
#include <stdio.h>

#include "file_util.h"
#include "processor.h"

Dynamic_Array_Impl(Ast_Type, Stack_Ast_Type, stack_type_)
Dynamic_Array_Impl(size_t, Array_Size, array_size_)

void stack_type_push(Stack_Ast_Type* stack, Ast_Type type) {
    stack_type_append(stack, type);
}

Ast_Type stack_type_pop(Stack_Ast_Type* stack) {
    Ast_Type result = stack->elements[stack->count - 1];
    stack->count--;
    return result;
}

bool is_number_type(Ast_Type* type) {
    if (type->kind == Type_Internal) {
        Ast_Type_Internal internal = type->data.internal;

        if (internal == Type_USize || internal == Type_U64 || internal == Type_U32 || internal == Type_U16 || internal == Type_U8 || internal == Type_F8) {
            return true;
        }
    }

    return false;
}

bool is_pointer_type(Ast_Type* type) {
    return (type->kind == Type_Internal && type->data.internal == Type_Ptr) || type->kind == Type_Pointer;
}

Resolved resolve(Generic_State* state, Ast_Identifier data) {
    Ast_Identifier initial_search = {};
    if (data.kind == Identifier_Single) {
        initial_search = data;
    } else if (data.kind == Identifier_Multiple) {
        initial_search.kind = Identifier_Single;
        initial_search.data.single = data.data.multiple.elements[0];
    }

    Resolved result = { NULL, Unresolved, {} };
    for (size_t j = 0; j < state->program->count; j++) {
        Ast_File* file_node = &state->program->elements[j];
        bool stop = false;
        for (size_t i = 0; i < file_node->items.count; i++) {
            Ast_Item* item = &file_node->items.elements[i];
            if (initial_search.kind == Identifier_Single && strcmp(item->name, initial_search.data.single) == 0) {
                result = (Resolved) { file_node, Resolved_Item, { .item = item } };
                stop = true;
                break;
            }
        }

        if (stop) {
            break;
        }
    }

    if (data.kind == Identifier_Multiple) {
        if (strcmp(data.data.multiple.elements[0], "") == 0) {
            result = (Resolved) { result.file, Resolved_Enum_Variant, { .enum_ = { .enum_ = NULL, .variant = data.data.multiple.elements[1]} } };
        } else {
            size_t identifier_index = 1;
            if (result.kind == Resolved_Item && result.data.item->kind == Item_Type && result.data.item->data.type.type.kind == Type_Enum) {
                char* wanted_name = data.data.multiple.elements[identifier_index];
                result = (Resolved) { result.file, Resolved_Enum_Variant, { .enum_ = { .enum_ = &result.data.item->data.type.type.data.enum_, .variant = wanted_name} } };
            }
        }
    }

    return result;
}

Ast_Type create_basic_single_type(char* name) {
    Ast_Type type = { .directives = array_ast_directive_new(1) };
    Ast_Type_Basic basic = {};

    basic.identifier.kind = Identifier_Single;
    basic.identifier.data.single = name;

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

    Ast_Type* child_allocated = malloc(sizeof(Ast_Type));
    *child_allocated = child;
    array.element_type = child_allocated;

    type.kind = Type_Array;
    type.data.array = array;

    return type;
}

Ast_Type create_pointer_type(Ast_Type child) {
    Ast_Type type = { .directives = array_ast_directive_new(1) };
    Ast_Type_Pointer pointer;

    Ast_Type* child_allocated = malloc(sizeof(Ast_Type));
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
    return *type;
}

Ast_Type evaluate_type_complete(Ast_Type* type_in, Generic_State* state) {
    Ast_Type type = evaluate_type(type_in);
    switch (type.kind) {
        case Type_Basic: {
            Ast_Type_Basic* basic = &type.data.basic;
            Ast_Item* item = basic->resolved_node;

            if (item == NULL) {
                Ast_Identifier identifier = basic->identifier;
                Resolved resolved = resolve(state, identifier);
                if (resolved.kind == Resolved_Item) {
                    item = resolved.data.item;
                }
            }

            if (item != NULL) {
                assert(item->kind == Item_Type);
                Ast_Item_Type* type_node = &item->data.type;
                Ast_Type type_result = type_node->type;
                return type_result;
            }
            break;
        }
        default:
            return type;
    }
    return type;
}

void process_expression(Ast_Expression* expression, Process_State* state);

bool is_type(Ast_Type* wanted_in, Ast_Type* given_in, Process_State* state) {
    Ast_Type wanted = evaluate_type(wanted_in);
    Ast_Type given = evaluate_type(given_in);

    if (wanted.kind == Type_RegisterSize || given.kind == Type_RegisterSize) {
        Ast_Type to_check;
        if (wanted.kind == Type_RegisterSize) to_check = given;
        if (given.kind == Type_RegisterSize) to_check = wanted;

        if (to_check.kind == Type_Pointer) return true;
        if (to_check.kind == Type_Internal && (to_check.data.internal == Type_USize || to_check.data.internal == Type_Ptr)) return true;

        return false;
    }

    if (wanted.kind != given.kind) {
        return false;
    }

    if (wanted.kind == Type_Pointer) {
        return is_type(wanted.data.pointer.child, given.data.pointer.child, state);
    }

    if (wanted.kind == Type_Basic) {
        if (wanted.data.basic.identifier.kind == Identifier_Single && given.data.basic.identifier.kind == Identifier_Single) {
            return strcmp(wanted.data.basic.identifier.data.single, given.data.basic.identifier.data.single) == 0;
        }
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

            if (basic->identifier.kind == Identifier_Single) {
                printf("%s", basic->identifier.data.single);
            } else {
                for (size_t i = 0; i < basic->identifier.data.multiple.count; i++) {
                    printf("%s", basic->identifier.data.multiple.elements[i]);
                    if (i < basic->identifier.data.multiple.count - 1) {
                        printf("::");
                    }
                }
            }
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
                case Type_USize:
                    printf("usize");
                    break;
                case Type_U64:
                    printf("u64");
                    break;
                case Type_U32:
                    printf("u32");
                    break;
                case Type_U16:
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

bool consume_in_reference(Process_State* state) {
    bool cached = state->in_reference;
    state->in_reference = false;
    return cached;
}

Ast_Type_Basic identifier_to_basic_type(Ast_Identifier identifier) {
    Ast_Type_Basic basic;
    if (identifier.kind == Identifier_Single) {
        basic.identifier.data.single = identifier.data.single;
        basic.identifier.kind = Identifier_Single;
    } else {
        basic.identifier.data.multiple = identifier.data.multiple;
        basic.identifier.kind = Identifier_Multiple;
    }
    return basic;
}

static Ast_Type* _usize_type;

Ast_Type* usize_type() {
    if (_usize_type == NULL) {
        Ast_Type* usize = malloc(sizeof(Ast_Type));
        *usize = create_internal_type(Type_USize);
        _usize_type = usize;
    }
    return _usize_type;
}

Ast_Expression clone_macro_expression(Ast_Expression expression, Array_String bindings, Array_Ast_Macro_SyntaxData values);
Ast_Type clone_macro_type(Ast_Type type, Array_String bindings, Array_Ast_Macro_SyntaxData values);

Ast_Statement clone_macro_statement(Ast_Statement statement, Array_String bindings, Array_Ast_Macro_SyntaxData values) {
    Ast_Statement result = {};
    result.directives = array_ast_directive_new(statement.directives.count);
    result.statement_end_location = statement.statement_end_location;
    for (size_t i = 0; i < statement.directives.count; i++) {
        Ast_Directive directive = statement.directives.elements[i];
        Ast_Directive directive_result = { .kind = directive.kind };
        switch (directive.kind) {
            case Directive_If: {
                Ast_Directive_If* if_in = &directive.data.if_;
                Ast_Directive_If if_out = { .expression = malloc(sizeof(Ast_Expression)) };

                *if_out.expression = clone_macro_expression(*if_in->expression, bindings, values);

                directive_result.data.if_ = if_out;
                break;
            }
            default:
                assert(false);
        }
        array_ast_directive_append(&result.directives, directive_result);
    }
    result.kind = statement.kind;

    switch (statement.kind) {
        case Statement_Declare: {
            Ast_Statement_Declare* declare_in = &statement.data.declare;
            Ast_Statement_Declare declare_out = { .declarations = array_ast_declaration_new(declare_in->declarations.count) };

            for (size_t i = 0; i < declare_in->declarations.count; i++) {
                Ast_Declaration* declaration_in = &declare_in->declarations.elements[i];
                Ast_Declaration declaration_out = { .name = declaration_in->name, .location = declaration_in->location };
                declaration_out.type = clone_macro_type(declaration_in->type, bindings, values);
                array_ast_declaration_append(&declare_out.declarations, declaration_out);
            }

            declare_out.expression = malloc(sizeof(Ast_Expression));
            *declare_out.expression = clone_macro_expression(*declare_in->expression, bindings, values);

            result.data.declare = declare_out;
            break;
        }
        case Statement_Assign: {
            Ast_Statement_Assign* assign_in = &statement.data.assign;
            Ast_Statement_Assign assign_out = { .parts = array_statement_assign_part_new(assign_in->parts.count), .expression = malloc(sizeof(Ast_Expression)) };

            for (size_t i = 0; i < assign_in->parts.count; i++) {
                Statement_Assign_Part* assign_part_in = &assign_in->parts.elements[i];
                Statement_Assign_Part assign_part_out;
                assign_part_out.kind = assign_part_in->kind;
                assign_part_out.location = assign_part_in->location;

                switch (assign_part_in->kind) {
                    case Retrieve_Assign_Identifier: {
                        assign_part_out.data.identifier = assign_part_in->data.identifier;
                        break;
                    }
                    case Retrieve_Assign_Parent: {
                        assign_part_out.data.parent.expression = malloc(sizeof(Ast_Expression));
                        *assign_part_out.data.parent.expression = clone_macro_expression(*assign_part_in->data.parent.expression, bindings, values);
                        assign_part_out.data.parent.name = assign_part_in->data.parent.name;
                        break;
                    }
                    case Retrieve_Assign_Array: {
                        assign_part_out.data.array.expression_inner = malloc(sizeof(Ast_Expression));
                        *assign_part_out.data.array.expression_inner = clone_macro_expression(*assign_part_in->data.array.expression_inner, bindings, values);
                        assign_part_out.data.array.expression_outer = malloc(sizeof(Ast_Expression));
                        *assign_part_out.data.array.expression_outer = clone_macro_expression(*assign_part_in->data.array.expression_outer, bindings, values);
                        break;
                    }
                    default:
                        assert(false);
                }

                array_statement_assign_part_append(&assign_out.parts, assign_part_out);
            }

            *assign_out.expression = clone_macro_expression(*assign_in->expression, bindings, values);

            result.data.assign = assign_out;
            break;
        }
        case Statement_Expression: {
            Ast_Statement_Expression* expression_in = &statement.data.expression;
            Ast_Statement_Expression expression_out = { .expression = malloc(sizeof(Ast_Expression)) };

            *expression_out.expression = clone_macro_expression(*expression_in->expression, bindings, values);

            result.data.expression = expression_out;
            break;
        }
        default:
            assert(false);
    }

    return result;
}

Ast_Type clone_macro_type(Ast_Type type, Array_String bindings, Array_Ast_Macro_SyntaxData values) {
    Ast_Type result;
    result.kind = type.kind;

    switch (type.kind) {
        case Type_Basic: {
            Ast_Type_Basic* basic_in = &type.data.basic;
            Ast_Type_Basic basic_out = *basic_in;

            if (basic_out.identifier.kind == Identifier_Single) {
                for (size_t i = 0; i < bindings.count; i++) {
                    if (strcmp(bindings.elements[i], basic_out.identifier.data.single) == 0) {
                        return *values.elements[i]->data.type;
                    }
                }
            }

            result.data.basic = basic_out;
            break;
        }
        case Type_Pointer: {
            Ast_Type_Pointer* pointer_in = &type.data.pointer;
            Ast_Type_Pointer pointer_out = { .child = malloc(sizeof(Ast_Type)) };

            *pointer_out.child = clone_macro_type(*pointer_in->child, bindings, values);

            result.data.pointer = pointer_out;
            break;
        }
        case Type_Array: {
            Ast_Type_Array* array_in = &type.data.array;
            Ast_Type_Array array_out = { .element_type = malloc(sizeof(Ast_Type)) };

            *array_out.element_type = clone_macro_type(*array_in->element_type, bindings, values);

            if (array_in->has_size) {
                array_out.size_type = malloc(sizeof(Ast_Type));
                *array_out.size_type = clone_macro_type(*array_in->size_type, bindings, values);
                array_out.has_size = true;
            }

            result.data.array = array_out;
            break;
        }
        case Type_Struct: {
            Ast_Type_Struct* struct_in = &type.data.struct_;
            Ast_Type_Struct struct_out = { .items = array_ast_declaration_pointer_new(struct_in->items.count) };

            for (size_t i = 0; i < struct_in->items.count; i++) {
                Ast_Declaration* declaration = malloc(sizeof(Ast_Declaration));
                declaration->name = struct_in->items.elements[i]->name;
                declaration->type = clone_macro_type(struct_in->items.elements[i]->type, bindings, values);
                array_ast_declaration_pointer_append(&struct_out.items, declaration);
            }

            result.data.struct_ = struct_out;
            break;
        }
        case Type_Internal: {
            result.data.internal = type.data.internal;
            break;
        }
        case Type_Number: {
            result.data.number = type.data.number;
            break;
        }
        case Type_TypeOf: {
            Ast_Type_TypeOf* type_of_in = &type.data.type_of;
            Ast_Type_TypeOf type_of_out = { .expression = malloc(sizeof(Ast_Expression)) };

            *type_of_out.expression = clone_macro_expression(*type_of_in->expression, bindings, values);

            result.data.type_of = type_of_out;
            break;
        }
        default:
            printf("%i\n", type.kind);
            assert(false);
    }

    return result;
}

Ast_Macro_SyntaxData clone_macro_syntax_data(Ast_Macro_SyntaxData data, Array_String bindings, Array_Ast_Macro_SyntaxData values);

Ast_Expression clone_macro_expression(Ast_Expression expression, Array_String bindings, Array_Ast_Macro_SyntaxData values) {
    Ast_Expression result;
    result.kind = expression.kind;

    switch (expression.kind) {
        case Expression_Block: {
            Ast_Expression_Block* block_in = &expression.data.block;
            Ast_Expression_Block block_out = { .statements = array_ast_statement_new(block_in->statements.count) };

            for (size_t i = 0; i < block_in->statements.count; i++) {
                Ast_Statement* statement = malloc(sizeof(Ast_Statement));
                *statement = clone_macro_statement(*block_in->statements.elements[i], bindings, values);
                array_ast_statement_append(&block_out.statements, statement);
            }

            result.data.block = block_out;
            break;
        }
        case Expression_Invoke: {
            Ast_Expression_Invoke* invoke_in = &expression.data.invoke;
            Ast_Expression_Invoke invoke_out = { .kind = invoke_in->kind, .arguments = array_ast_expression_new(invoke_in->arguments.count), .location = invoke_in->location };

            for (size_t i = 0; i < invoke_in->arguments.count; i++) {
                Ast_Expression* expression = malloc(sizeof(Ast_Expression));
                *expression = clone_macro_expression(*invoke_in->arguments.elements[i], bindings, values);
                array_ast_expression_append(&invoke_out.arguments, expression);
            }

            switch (invoke_in->kind) {
                case Invoke_Standard: {
                    Ast_Expression* expression = malloc(sizeof(Ast_Expression));
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
        case Expression_RunMacro: {
            Ast_RunMacro* run_macro_in = &expression.data.run_macro;
            Ast_RunMacro run_macro_out = { .identifier = run_macro_in->identifier, .arguments = array_ast_macro_syntax_data_new(run_macro_in->arguments.count), .location = run_macro_in->location };

            for (size_t i = 0; i < run_macro_in->arguments.count; i++) {
                Ast_Macro_SyntaxData* syntax_data = malloc(sizeof(Ast_Macro_SyntaxData));
                *syntax_data = clone_macro_syntax_data(*run_macro_in->arguments.elements[i], bindings, values);

                if (syntax_data->kind.kind == Macro_Multiple_Expanded) {
                    for (size_t j = 0; j < syntax_data->data.multiple_expanded.count; j++) {
                        array_ast_macro_syntax_data_append(&run_macro_out.arguments, syntax_data->data.multiple_expanded.elements[j]);
                    }
                } else {
                    array_ast_macro_syntax_data_append(&run_macro_out.arguments, syntax_data);
                }
            }

            result.data.run_macro = run_macro_out;
            break;
        }
        case Expression_Retrieve: {
            Ast_Expression_Retrieve* retrieve_in = &expression.data.retrieve;
            Ast_Expression_Retrieve retrieve_out = { .location = retrieve_in->location };

            retrieve_out.kind = retrieve_in->kind;

            switch (retrieve_in->kind) {
                case Retrieve_Assign_Identifier: {
                    Ast_Identifier identifier = retrieve_in->data.identifier;

                    if (identifier.kind == Identifier_Single) {
                        for (size_t i = 0; i < bindings.count; i++) {
                            if (strcmp(bindings.elements[i], identifier.data.single) == 0) {
                                if (strcmp(bindings.elements[i], "..") == 0) {
                                    Ast_Expression expression = {};

                                    Ast_Expression_Multiple multiple = { .expressions = array_ast_expression_new(2) };

                                    for (size_t j = i; j < values.count; j++) {
                                        array_ast_expression_append(&multiple.expressions, values.elements[j]->data.expression);
                                    }

                                    expression.kind = Expression_Multiple;
                                    expression.data.multiple = multiple;

                                    return expression;
                                } else {
                                    return *values.elements[i]->data.expression;
                                }
                            }
                        }
                    }

                    retrieve_out.data.identifier = retrieve_in->data.identifier;
                    break;
                }
                case Retrieve_Assign_Array: {
                    retrieve_out.data.array.expression_inner = malloc(sizeof(Ast_Expression));
                    *retrieve_out.data.array.expression_inner = clone_macro_expression(*retrieve_in->data.array.expression_inner, bindings, values);
                    retrieve_out.data.array.expression_outer = malloc(sizeof(Ast_Expression));
                    *retrieve_out.data.array.expression_outer = clone_macro_expression(*retrieve_in->data.array.expression_outer, bindings, values);
                    break;
                }
                case Retrieve_Assign_Parent: {
                    retrieve_out.data.parent.expression = malloc(sizeof(Ast_Expression));
                    *retrieve_out.data.parent.expression = clone_macro_expression(*retrieve_in->data.parent.expression, bindings, values);
                    retrieve_out.data.parent.name = retrieve_in->data.parent.name;
                    break;
                }
                default:
                    assert(false);
            }

            result.data.retrieve = retrieve_out;
            break;
        }
        case Expression_Reference: {
            Ast_Expression_Reference* reference_in = &expression.data.reference;
            Ast_Expression_Reference reference_out = { .inner = malloc(sizeof(Ast_Expression)) };
            *reference_out.inner = clone_macro_expression(*reference_in->inner, bindings, values);

            result.data.reference = reference_out;
            break;
        }
        case Expression_If: {
            Ast_Expression_If* if_in = &expression.data.if_;
            Ast_Expression_If if_out = { .condition = malloc(sizeof(Ast_Expression)), .if_expression = malloc(sizeof(Ast_Expression)) };
            *if_out.condition = clone_macro_expression(*if_in->condition, bindings, values);
            *if_out.if_expression = clone_macro_expression(*if_in->if_expression, bindings, values);

            if (if_in->else_expression != NULL) {
                if_out.else_expression = malloc(sizeof(Ast_Expression));
                *if_out.else_expression = clone_macro_expression(*if_in->else_expression, bindings, values);
            }

            result.data.if_ = if_out;
            break;
        }
        case Expression_While: {
            Ast_Expression_While* while_in = &expression.data.while_;
            Ast_Expression_While while_out = { .condition = malloc(sizeof(Ast_Expression)), .inside = malloc(sizeof(Ast_Expression)) };
            *while_out.condition = clone_macro_expression(*while_in->condition, bindings, values);
            *while_out.inside = clone_macro_expression(*while_in->inside, bindings, values);

            result.data.while_ = while_out;
            break;
        }
        case Expression_IsType: {
            Ast_Expression_IsType* is_type_in = &expression.data.is_type;
            Ast_Expression_IsType is_type_out = {};
            is_type_out.given = clone_macro_type(is_type_in->given, bindings, values);
            is_type_out.wanted = clone_macro_type(is_type_in->wanted, bindings, values);

            result.data.is_type = is_type_out;
            break;
        }
        case Expression_SizeOf: {
            Ast_Expression_SizeOf* size_of_in = &expression.data.size_of;
            Ast_Expression_SizeOf size_of_out = {};
            size_of_out.type = clone_macro_type(size_of_in->type, bindings, values);

            result.data.size_of = size_of_out;
            break;
        }
        case Expression_Cast: {
            Ast_Expression_Cast* cast_in = &expression.data.cast;
            Ast_Expression_Cast cast_out = { .expression = malloc(sizeof(Ast_Expression)) };
            *cast_out.expression = clone_macro_expression(*cast_in->expression, bindings, values);
            cast_out.type = clone_macro_type(cast_in->type, bindings, values);

            result.data.cast = cast_out;
            break;
        }
        case Expression_Build: {
            Ast_Expression_Build* build_in = &expression.data.build;
            Ast_Expression_Build build_out = { .arguments = array_ast_expression_new(build_in->arguments.count) };
            build_out.type = clone_macro_type(build_in->type, bindings, values);

            for (size_t i = 0; i < build_in->arguments.count; i++) {
                array_ast_expression_append(&build_out.arguments, build_in->arguments.elements[i]);
            }

            result.data.build = build_out;
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
        case Expression_Multiple: {
            Ast_Expression_Multiple* multiple_in = &expression.data.multiple;
            Ast_Expression_Multiple multiple_out = { .expressions = array_ast_expression_new(multiple_in->expressions.count) };

            for (size_t i = 0; i < multiple_in->expressions.count; i++) {
                Ast_Expression* cloned = malloc(sizeof(Ast_Expression));
                *cloned = clone_macro_expression(*multiple_in->expressions.elements[i], bindings, values);
                array_ast_expression_append(&multiple_out.expressions, cloned);
            }
            result.data.multiple = multiple_out;
            break;
        }
        default:
            assert(false);
    }

    return result;
}

Ast_Macro_SyntaxData clone_macro_syntax_data(Ast_Macro_SyntaxData data, Array_String bindings, Array_Ast_Macro_SyntaxData values) {
    Ast_Macro_SyntaxData result;
    result.kind = data.kind;

    switch (data.kind.kind) {
        case Macro_Expression: {
            result.data.expression = malloc(sizeof(Ast_Expression));
            *result.data.expression = clone_macro_expression(*data.data.expression, bindings, values);
            break;
        }
        case Macro_Multiple: {
            if (data.kind.data.multiple->kind == Macro_Expression) {
                Ast_Macro_SyntaxData inner = *data.data.multiple;
                inner = clone_macro_syntax_data(inner, bindings, values);
                assert(inner.data.expression->kind == Expression_Multiple);

                Array_Ast_Macro_SyntaxData datas = array_ast_macro_syntax_data_new(2);
                for (size_t i = 0; i < inner.data.expression->data.multiple.expressions.count; i++) {
                    Ast_Macro_SyntaxData* individual = malloc(sizeof(Ast_Macro_SyntaxData));
                    individual->kind.kind = Macro_Expression;
                    individual->data.expression = inner.data.expression->data.multiple.expressions.elements[i];

                    array_ast_macro_syntax_data_append(&datas, individual);
                }

                result.data.multiple_expanded = datas;
                result.kind.kind = Macro_Multiple_Expanded;
            } else {
                assert(false);
            }
            break;
        }
        default:
            assert(false);
    }
    return result;
}

bool macro_syntax_kind_equal(Ast_Macro_SyntaxKind wanted, Ast_Macro_SyntaxKind given) {
    if (wanted.kind == given.kind) {
        if (wanted.kind == Macro_Multiple && given.kind == Macro_Multiple) {
            return macro_syntax_kind_equal(*wanted.data.multiple, *given.data.multiple);
        }
        return true;
    }

    if (wanted.kind == Macro_Multiple) {
        return macro_syntax_kind_equal(*wanted.data.multiple, given);
    }

    return false;
}

void process_type(Ast_Type* type, Process_State* state) {
    switch (type->kind) {
        case Type_TypeOf: {
            Ast_Type_TypeOf* type_of = &type->data.type_of;
            Ast_Expression* expression = type_of->expression;
            size_t stack_initial = state->stack.count;
            process_expression(expression, state);

            Ast_Type* type = malloc(sizeof(Ast_Type));
            *type = stack_type_pop(&state->stack);
            type_of->computed_result_type = type;

            assert(state->stack.count == stack_initial);
            break;
        }
        case Type_RunMacro: {
            Ast_RunMacro* run_macro = &type->data.run_macro;

            Resolved resolved = resolve(&state->generic, run_macro->identifier);
            assert(resolved.kind == Resolved_Item && resolved.data.item->kind == Item_Macro);
            Ast_Item_Macro* macro = &resolved.data.item->data.macro;

            assert(macro->return_.kind == Macro_Type);

            size_t current_macro_argument_index = 0;
            for (size_t i = 0; i < run_macro->arguments.count; i++) {
                Ast_Macro_SyntaxKind current_macro_argument = macro->arguments.elements[current_macro_argument_index];
                Ast_Macro_SyntaxKind given_argument = run_macro->arguments.elements[i]->kind;
                if (!macro_syntax_kind_equal(current_macro_argument, given_argument)) {
                    print_error_stub(&run_macro->location);
                    printf("Macro invocation with wrong type!\n");
                    exit(1);
                }

                if (current_macro_argument.kind != Macro_Multiple) {
                    current_macro_argument_index++;
                }
            }

            Ast_Macro_Variant variant;
            bool matched = false;
            for (size_t i = 0; i < macro->variants.count; i++) {
                bool matches = true;

                size_t argument_index = 0;
                for (size_t j = 0; j < macro->variants.elements[i].bindings.count; j++) {
                    if (strcmp(macro->variants.elements[i].bindings.elements[j], "..") == 0) {
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

            Ast_Type cloned = clone_macro_type(*variant.data.data.type, variant.bindings, run_macro->arguments);

            run_macro->result.kind.kind = Macro_Type;
            run_macro->result.data.type = malloc(sizeof(Ast_Expression));
            *run_macro->result.data.type = cloned;

            process_type(run_macro->result.data.type, state);
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
        result = parent_type;
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
        Ast_Type* wanted_type = malloc(sizeof(Ast_Type));
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

        if (assign_part->kind == Retrieve_Assign_Identifier && assign_part->data.identifier.kind == Identifier_Single) {
            char* name = assign_part->data.identifier.data.single;

            Ast_Type* type = malloc(sizeof(Ast_Type));
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
            Ast_Type parent_type = stack_type_pop(&state->stack);

            assign_part->data.parent.computed_parent_type = parent_type;

            Ast_Type* parent_type_raw;
            if (parent_type.kind == Type_Pointer) {
                parent_type_raw = parent_type.data.pointer.child;
            } else {
                parent_type_raw = &parent_type;
            }

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

        if (assign_part->kind == Retrieve_Assign_Identifier && assign_part->data.identifier.kind == Identifier_Single) {
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
            break;
        }
        case Statement_Declare: {
            Ast_Statement_Declare* declare = &statement->data.declare;
            if (declare->expression != NULL) {
                if (declare->expression->kind == Expression_Multiple) {
                    size_t declare_index = 0;
                    for (size_t i = 0; i < declare->expression->data.multiple.expressions.count; i++) {
                        size_t stack_start = state->stack.count;
                        state->wanted_type = &declare->declarations.elements[declare_index].type;
                        process_expression(declare->expression->data.multiple.expressions.elements[i], state);
                        declare_index += state->stack.count - stack_start;
                    }
                } else {
                    state->wanted_type = &declare->declarations.elements[declare->declarations.count - 1].type;
                    process_expression(declare->expression, state);
                }

                for (int i = declare->declarations.count - 1; i >= 0; i--) {
                    Ast_Declaration* declaration = &declare->declarations.elements[i];
                    process_type(&declaration->type, state);

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
                    array_ast_declaration_append(&state->current_declares, *declaration);
                }
            } else {
                for (int i = declare->declarations.count - 1; i >= 0; i--) {
                    Ast_Declaration* declaration = &declare->declarations.elements[i];
                    process_type(&declaration->type, state);
                    array_ast_declaration_append(&state->current_declares, *declaration);
                }
            }
            break;
        }
        case Statement_Assign: {
            process_assign(&statement->data.assign, state);
            break;
        }
        case Statement_Return: {
            Ast_Statement_Return* return_ = &statement->data.return_;
            if (return_->expression->kind == Expression_Multiple) {
                size_t declare_index = 0;
                for (size_t i = 0; i < return_->expression->data.multiple.expressions.count; i++) {
                    size_t stack_start = state->stack.count;
                    state->wanted_type = state->current_returns.elements[declare_index];
                    process_expression(return_->expression->data.multiple.expressions.elements[i], state);
                    declare_index += state->stack.count - stack_start;
                }
            } else {
                state->wanted_type = state->current_returns.elements[state->current_returns.count - 1];
                process_expression(return_->expression, state);
            }

            for (int i = state->current_returns.count - 1; i >= 0; i--) {
                Ast_Type* return_type = state->current_returns.elements[i];

                if (state->stack.count == 0) {
                    print_error_stub(&return_->location);
                    printf("Ran out of values for return\n");
                    exit(1);
                }

                Ast_Type given_type = stack_type_pop(&state->stack);
                if (!is_type(return_type, &given_type, state)) {
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
    for (int i = state->current_declares.count - 1; i >= 0; i--) {
        Ast_Declaration* declaration = &state->current_declares.elements[i];
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

    if (first->kind == Type_Internal && first->data.internal == Type_Ptr && second->kind == Type_Internal && second->data.internal == Type_USize) {
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
                case Resolved_Enum_Variant: {
                    assert(false);
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
            array_size_append(&state->scoped_declares, state->current_declares.count);
            for (size_t i = 0; i < block->statements.count; i++) {
                process_statement(block->statements.elements[i], state);
            }
            state->current_declares.count = state->scoped_declares.elements[state->scoped_declares.count - 1];
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
                Ast_Expression* procedure = invoke->data.procedure;
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

                    Ast_Type* wanted_allocated = malloc(sizeof(Ast_Type));
                    *wanted_allocated = state->stack.elements[state->stack.count - 1];
                    state->wanted_type = wanted_allocated;

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

            Resolved resolved = resolve(&state->generic, run_macro->identifier);
            assert(resolved.kind == Resolved_Item && resolved.data.item->kind == Item_Macro);
            Ast_Item_Macro* macro = &resolved.data.item->data.macro;

            assert(macro->return_.kind == Macro_Expression);

            size_t current_macro_argument_index = 0;
            for (size_t i = 0; i < run_macro->arguments.count; i++) {
                Ast_Macro_SyntaxKind current_macro_argument = macro->arguments.elements[current_macro_argument_index];
                Ast_Macro_SyntaxKind given_argument = run_macro->arguments.elements[i]->kind;
                if (!macro_syntax_kind_equal(current_macro_argument, given_argument)) {
                    print_error_stub(&run_macro->location);
                    printf("Macro invocation with wrong type!\n");
                    exit(1);
                }

                if (current_macro_argument.kind != Macro_Multiple) {
                    current_macro_argument_index++;
                }
            }

            Ast_Macro_Variant variant;
            bool matched = false;
            for (size_t i = 0; i < macro->variants.count; i++) {
                bool matches = true;

                size_t argument_index = 0;
                for (size_t j = 0; j < macro->variants.elements[i].bindings.count; j++) {
                    if (strcmp(macro->variants.elements[i].bindings.elements[j], "..") == 0) {
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

            Ast_Expression cloned = clone_macro_expression(*variant.data.data.expression, variant.bindings, run_macro->arguments);

            run_macro->result.kind.kind = Macro_Expression;
            run_macro->result.data.expression = malloc(sizeof(Ast_Expression));
            *run_macro->result.data.expression = cloned;

            process_expression(run_macro->result.data.expression, state);
            break;
        }
        case Expression_Retrieve: {
            Ast_Expression_Retrieve* retrieve = &expression->data.retrieve;
            bool found = false;

            if (!found && retrieve->kind == Retrieve_Assign_Array) {
                bool in_reference = consume_in_reference(state);
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
                if (!is_internal_type(Type_USize, &array_index_type)) {
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

            if (!found && retrieve->kind == Retrieve_Assign_Identifier && retrieve->data.identifier.kind == Identifier_Single) {
                Ast_Type* variable_type = get_local_variable_type(state, retrieve->data.identifier.data.single);

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
                Ast_Type type = { .directives = array_ast_directive_new(1) };
                for (size_t i = 0; i < state->current_arguments.count; i++) {
                    Ast_Declaration* declaration = &state->current_arguments.elements[state->current_arguments.count - i - 1];
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
                Ast_Type parent_type = stack_type_pop(&state->stack);

                retrieve->data.parent.computed_parent_type = parent_type;

                Ast_Type* parent_type_raw;
                if (parent_type.kind == Type_Pointer) {
                    parent_type_raw = parent_type.data.pointer.child;
                } else {
                    parent_type_raw = &parent_type;
                }

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

            if (!found && retrieve->kind == Retrieve_Assign_Identifier) {
                if (retrieve->data.identifier.kind == Identifier_Multiple && strcmp(retrieve->data.identifier.data.multiple.elements[0], "") == 0) {
                    assert(state->wanted_type->kind == Type_Enum);

                    char* enum_variant = retrieve->data.identifier.data.multiple.elements[1];

                    Ast_Type_Enum* enum_type = &state->wanted_type->data.enum_;
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
                        Ast_Item* item = resolved.data.item;

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

                                if (consume_in_reference(state)) {
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
                        Ast_Type type;
                        type.kind = Type_Basic;
                        Ast_Type_Basic basic = identifier_to_basic_type(retrieve->data.identifier);

                        // Kinda a hack, probably should make a more robust way to handling enumerations
                        if (basic.identifier.data.multiple.count > 2) {
                            basic.identifier.data.multiple.count--;
                        } else {
                            basic.identifier.kind = Identifier_Single;
                            basic.identifier.data.single = basic.identifier.data.multiple.elements[0];
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

            process_expression(node->if_expression, state);

            if (node->else_expression != NULL) {
                process_expression(node->else_expression, state);
            }
            break;
        }
        case Expression_While: {
            Ast_Expression_While* node = &expression->data.while_;

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
        case Expression_Number: {
            Ast_Expression_Number* number = &expression->data.number;
            Ast_Type* wanted = state->wanted_type;

            if (wanted != NULL && is_number_type(wanted)) {
                stack_type_push(&state->stack, *wanted);
                number->type = wanted;
            } else {
                Ast_Type* usize = malloc(sizeof(Ast_Type));
                *usize = create_internal_type(Type_USize);
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
                Ast_Type* usize = malloc(sizeof(Ast_Type));
                *usize = create_internal_type(Type_USize);
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
            stack_type_push(&state->stack, create_pointer_type(create_array_type(create_internal_type(Type_U8))));
            break;
        }
        case Expression_Reference: {
            Ast_Expression_Reference* reference = &expression->data.reference;
            state->in_reference = true;

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

                if ((input_internal == Type_USize || input_internal == Type_U64 || input_internal == Type_U32 || input_internal == Type_U16 || input_internal == Type_U8) &&
                        (output_internal == Type_USize || output_internal == Type_U64 || output_internal == Type_U32 || output_internal == Type_U16 || output_internal == Type_U8)) {
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
            state->current_procedure = item;
            Ast_Item_Procedure* procedure = &item->data.procedure;
            state->current_declares = array_ast_declaration_new(4);
            state->current_arguments = procedure->arguments;
            state->current_returns = procedure->returns;
            state->current_body = procedure->body;

            for (size_t i = 0; i < procedure->arguments.count; i++) {
                process_type(&procedure->arguments.elements[i].type, state);
            }

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
        .scoped_declares = array_size_new(8),
        .current_arguments = {},
        .current_returns = {},
        .current_body = NULL,
        .in_reference = false,
        .wanted_type = NULL,
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
