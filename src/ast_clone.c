#include <assert.h>

#include "ast_clone.h"

Ast_Statement clone_statement(Ast_Statement statement) {
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

                *if_out.expression = clone_expression(*if_in->expression);

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
                declaration_out.type = clone_type(declaration_in->type);
                array_ast_declaration_append(&declare_out.declarations, declaration_out);
            }

            declare_out.expression = malloc(sizeof(Ast_Expression));
            *declare_out.expression = clone_expression(*declare_in->expression);

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
                        *assign_part_out.data.parent.expression = clone_expression(*assign_part_in->data.parent.expression);
                        assign_part_out.data.parent.name = assign_part_in->data.parent.name;
                        break;
                    }
                    case Retrieve_Assign_Array: {
                        assign_part_out.data.array.expression_inner = malloc(sizeof(Ast_Expression));
                        *assign_part_out.data.array.expression_inner = clone_expression(*assign_part_in->data.array.expression_inner);
                        assign_part_out.data.array.expression_outer = malloc(sizeof(Ast_Expression));
                        *assign_part_out.data.array.expression_outer = clone_expression(*assign_part_in->data.array.expression_outer);
                        break;
                    }
                    default:
                        assert(false);
                }

                array_statement_assign_part_append(&assign_out.parts, assign_part_out);
            }

            *assign_out.expression = clone_expression(*assign_in->expression);

            result.data.assign = assign_out;
            break;
        }
        case Statement_Expression: {
            Ast_Statement_Expression* expression_in = &statement.data.expression;
            Ast_Statement_Expression expression_out = { .expression = malloc(sizeof(Ast_Expression)) };

            *expression_out.expression = clone_expression(*expression_in->expression);

            result.data.expression = expression_out;
            break;
        }
        default:
            assert(false);
    }

    return result;
}

Ast_Type clone_type(Ast_Type type) {
    Ast_Type result;
    result.kind = type.kind;

    switch (type.kind) {
        case Type_Basic: {
            Ast_Type_Basic* basic_in = &type.data.basic;
            Ast_Type_Basic basic_out = *basic_in;

            result.data.basic = basic_out;
            break;
        }
        case Type_Pointer: {
            Ast_Type_Pointer* pointer_in = &type.data.pointer;
            Ast_Type_Pointer pointer_out = { .child = malloc(sizeof(Ast_Type)) };

            *pointer_out.child = clone_type(*pointer_in->child);

            result.data.pointer = pointer_out;
            break;
        }
        case Type_Array: {
            Ast_Type_Array* array_in = &type.data.array;
            Ast_Type_Array array_out = { .element_type = malloc(sizeof(Ast_Type)) };

            *array_out.element_type = clone_type(*array_in->element_type);

            if (array_in->has_size) {
                array_out.size_type = malloc(sizeof(Ast_Type));
                *array_out.size_type = clone_type(*array_in->size_type);
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
                declaration->type = clone_type(struct_in->items.elements[i]->type);
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

            *type_of_out.expression = clone_expression(*type_of_in->expression);

            result.data.type_of = type_of_out;
            break;
        }
        default:
            assert(false);
    }

    return result;
}

Ast_Expression clone_expression(Ast_Expression expression) {
    Ast_Expression result;
    result.kind = expression.kind;

    switch (expression.kind) {
        case Expression_Block: {
            Ast_Expression_Block* block_in = &expression.data.block;
            Ast_Expression_Block block_out = { .statements = array_ast_statement_new(block_in->statements.count) };

            for (size_t i = 0; i < block_in->statements.count; i++) {
                Ast_Statement* statement = malloc(sizeof(Ast_Statement));
                *statement = clone_statement(*block_in->statements.elements[i]);
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
                *expression = clone_expression(*invoke_in->arguments.elements[i]);
                array_ast_expression_append(&invoke_out.arguments, expression);
            }

            switch (invoke_in->kind) {
                case Invoke_Standard: {
                    Ast_Expression* expression = malloc(sizeof(Ast_Expression));
                    *expression = clone_expression(*invoke_in->data.procedure);
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
                *syntax_data = clone_syntax_data(*run_macro_in->arguments.elements[i]);

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

                    //for (size_t i = 0; i < bindings.count; i++) {
                    //    if (strcmp(bindings.elements[i], identifier.name) == 0) {
                    //        if (strcmp(bindings.elements[i], "..") == 0) {
                    //            Ast_Expression expression = {};

                    //            Ast_Expression_Multiple multiple = { .expressions = array_ast_expression_new(2) };

                    //            for (size_t j = i; j < values.count; j++) {
                    //                array_ast_expression_append(&multiple.expressions, values.elements[j]->data.expression);
                    //            }

                    //            expression.kind = Expression_Multiple;
                    //            expression.data.multiple = multiple;

                    //            return expression;
                    //        } else {
                    //            return *values.elements[i]->data.expression;
                    //        }
                    //    }
                    //}

                    retrieve_out.data.identifier = identifier;
                    break;
                }
                case Retrieve_Assign_Array: {
                    retrieve_out.data.array.expression_inner = malloc(sizeof(Ast_Expression));
                    *retrieve_out.data.array.expression_inner = clone_expression(*retrieve_in->data.array.expression_inner);
                    retrieve_out.data.array.expression_outer = malloc(sizeof(Ast_Expression));
                    *retrieve_out.data.array.expression_outer = clone_expression(*retrieve_in->data.array.expression_outer);
                    break;
                }
                case Retrieve_Assign_Parent: {
                    retrieve_out.data.parent.expression = malloc(sizeof(Ast_Expression));
                    *retrieve_out.data.parent.expression = clone_expression(*retrieve_in->data.parent.expression);
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
            *reference_out.inner = clone_expression(*reference_in->inner);

            result.data.reference = reference_out;
            break;
        }
        case Expression_If: {
            Ast_Expression_If* if_in = &expression.data.if_;
            Ast_Expression_If if_out = { .condition = malloc(sizeof(Ast_Expression)), .if_expression = malloc(sizeof(Ast_Expression)) };
            *if_out.condition = clone_expression(*if_in->condition);
            *if_out.if_expression = clone_expression(*if_in->if_expression);

            if (if_in->else_expression != NULL) {
                if_out.else_expression = malloc(sizeof(Ast_Expression));
                *if_out.else_expression = clone_expression(*if_in->else_expression);
            }

            result.data.if_ = if_out;
            break;
        }
        case Expression_While: {
            Ast_Expression_While* while_in = &expression.data.while_;
            Ast_Expression_While while_out = { .condition = malloc(sizeof(Ast_Expression)), .inside = malloc(sizeof(Ast_Expression)) };
            *while_out.condition = clone_expression(*while_in->condition);
            *while_out.inside = clone_expression(*while_in->inside);

            result.data.while_ = while_out;
            break;
        }
        case Expression_IsType: {
            Ast_Expression_IsType* is_type_in = &expression.data.is_type;
            Ast_Expression_IsType is_type_out = {};
            is_type_out.given = clone_type(is_type_in->given);
            is_type_out.wanted = clone_type(is_type_in->wanted);

            result.data.is_type = is_type_out;
            break;
        }
        case Expression_SizeOf: {
            Ast_Expression_SizeOf* size_of_in = &expression.data.size_of;
            Ast_Expression_SizeOf size_of_out = {};
            size_of_out.type = clone_type(size_of_in->type);

            result.data.size_of = size_of_out;
            break;
        }
        case Expression_Cast: {
            Ast_Expression_Cast* cast_in = &expression.data.cast;
            Ast_Expression_Cast cast_out = { .expression = malloc(sizeof(Ast_Expression)) };
            *cast_out.expression = clone_expression(*cast_in->expression);
            cast_out.type = clone_type(cast_in->type);

            result.data.cast = cast_out;
            break;
        }
        case Expression_Build: {
            Ast_Expression_Build* build_in = &expression.data.build;
            Ast_Expression_Build build_out = { .arguments = array_ast_expression_new(build_in->arguments.count) };
            build_out.type = clone_type(build_in->type);

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
                *cloned = clone_expression(*multiple_in->expressions.elements[i]);
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

Ast_Macro_SyntaxData clone_syntax_data(Ast_Macro_SyntaxData data) {
    Ast_Macro_SyntaxData result;
    result.kind = data.kind;

    switch (data.kind.kind) {
        case Macro_Expression: {
            result.data.expression = malloc(sizeof(Ast_Expression));
            *result.data.expression = clone_expression(*data.data.expression);
            break;
        }
        case Macro_Multiple: {
            if (data.kind.data.multiple->kind == Macro_Expression) {
                Ast_Macro_SyntaxData inner = *data.data.multiple;
                inner = clone_syntax_data(inner);
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
