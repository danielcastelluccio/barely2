#include "ast_walk.h"

void walk_type(Ast_Type* type, Ast_Walk_State* state) {
    if (state->type_func != NULL) {
        state->type_func(type, state->internal_state);
    }

    switch (type->kind) {
        case Type_Pointer: {
            Ast_Type_Pointer* pointer = &type->data.pointer;

            walk_type(pointer->child, state);
            break;
        }
        case Type_Array: {
            Ast_Type_Array* array = &type->data.array;

            walk_type(array->element_type, state);

            if (array->has_size) {
                walk_type(array->size_type, state);
            }
            break;
        }
        case Type_Struct: {
            Ast_Type_Struct* struct_ = &type->data.struct_;

            for (size_t i = 0; i < struct_->items.count; i++) {
                walk_type(&struct_->items.elements[i]->type, state);
            }
            break;
        }
        case Type_TypeOf: {
            Ast_Type_TypeOf* type_of = &type->data.type_of;

            walk_expression(type_of->expression, state);
            break;
        }
        default:
            break;
    }
}

void walk_expression(Ast_Expression* expression, Ast_Walk_State* state) {
    if (state->expression_func != NULL) {
        state->expression_func(expression, state->internal_state);
    }

    switch (expression->kind) {
        case Expression_Block: {
            for (size_t i = 0; i < expression->data.block.statements.count; i++) {
                walk_statement(expression->data.block.statements.elements[i], state);
            }
            break;
        }
        case Expression_Invoke: {
            Ast_Expression_Invoke* invoke = &expression->data.invoke;
            for (size_t i = 0; i < invoke->arguments.count; i++) {
                walk_expression(invoke->arguments.elements[i], state);
            }

            switch (invoke->kind) {
                case Invoke_Standard: {
                    walk_expression(invoke->data.procedure, state);
                    break;
                }
                default:
                    break;
            }
            break;
        }
        case Expression_Retrieve: {
            Ast_Expression_Retrieve* retrieve = &expression->data.retrieve;

            switch (retrieve->kind) {
                case Retrieve_Assign_Identifier: {
                    break;
                }
                case Retrieve_Assign_Array: {
                    walk_expression(retrieve->data.array.expression_inner, state);
                    walk_expression(retrieve->data.array.expression_outer, state);
                    break;
                }
                case Retrieve_Assign_Parent: {
                    walk_expression(retrieve->data.parent.expression, state);
                    break;
                }
                default:
                    break;
            }
            break;
        }
        case Expression_Reference: {
            Ast_Expression_Reference* reference = &expression->data.reference;
            walk_expression(reference->inner, state);
            break;
        }
        case Expression_Multiple: {
            for (size_t i = 0; i < expression->data.multiple.expressions.count; i++) {
                walk_expression(expression->data.multiple.expressions.elements[i], state);
            }
            break;
        }
        case Expression_If: {
            Ast_Expression_If* if_ = &expression->data.if_;
            walk_expression(if_->condition, state);
            walk_expression(if_->if_expression, state);

            if (if_->else_expression != NULL) {
                walk_expression(if_->else_expression, state);
            }
            break;
        }
        case Expression_While: {
            walk_expression(expression->data.while_.condition, state);
            walk_expression(expression->data.while_.inside, state);
            break;
        }
        case Expression_RunMacro: {
            Ast_RunMacro* run_macro = &expression->data.run_macro;
            for (size_t i = 0; i < run_macro->arguments.count; i++) {
                walk_macro_syntax_data(run_macro->arguments.elements[i], state);
            }

            if (run_macro->result.data.expression != NULL) {
                walk_expression(run_macro->result.data.expression, state);
            }
            break;
        }
        case Expression_IsType: {
            Ast_Expression_IsType* is_type = &expression->data.is_type;
            walk_type(&is_type->given, state);
            walk_type(&is_type->wanted, state);
            break;
        }
        case Expression_SizeOf: {
            Ast_Expression_SizeOf* size_of = &expression->data.size_of;
            walk_type(&size_of->type, state);
            break;
        }
        case Expression_Cast: {
            Ast_Expression_Cast* cast = &expression->data.cast;
            walk_expression(cast->expression, state);
            walk_type(&cast->type, state);
            break;
        }
        case Expression_Build: {
            Ast_Expression_Build* build = &expression->data.build;
            walk_type(&build->type, state);

            for (size_t i = 0; i < build->arguments.count; i++) {
                walk_expression(build->arguments.elements[i], state);
            }
            break;
        }
        default:
            break;
    }
}

void walk_statement(Ast_Statement* statement, Ast_Walk_State* state) {
    if (state->statement_func != NULL) {
        state->statement_func(statement, state->internal_state);
    }

    for (size_t i = 0; i < statement->directives.count; i++) {
        Ast_Directive directive = statement->directives.elements[i];
        switch (directive.kind) {
            case Directive_If: {
                Ast_Directive_If* if_ = &directive.data.if_;

                walk_expression(if_->expression, state);
                break;
            }
        }
    }

    switch (statement->kind) {
        case Statement_Expression: {
            walk_expression(statement->data.expression.expression, state);
            break;
        }
        case Statement_Declare: {
            Ast_Statement_Declare* declare = &statement->data.declare;

            for (size_t i = 0; i < declare->declarations.count; i++) {
                Ast_Declaration* declaration = &declare->declarations.elements[i];
                walk_type(&declaration->type, state);
            }

            if (declare->expression != NULL) {
                walk_expression(declare->expression, state);
            }
            break;
        }
        case Statement_Assign: {
            Ast_Statement_Assign* assign = &statement->data.assign;

            for (size_t i = 0; i < assign->parts.count; i++) {
                Statement_Assign_Part* assign_part_in = &assign->parts.elements[i];

                switch (assign_part_in->kind) {
                    case Retrieve_Assign_Identifier: {
                        break;
                    }
                    case Retrieve_Assign_Parent: {
                        walk_expression(assign_part_in->data.parent.expression, state);
                        break;
                    }
                    case Retrieve_Assign_Array: {
                        walk_expression(assign_part_in->data.array.expression_inner, state);
                        walk_expression(assign_part_in->data.array.expression_outer, state);
                        break;
                    }
                    default:
                        break;
                }
            }

            walk_expression(assign->expression, state);
            break;
        }
        default:
            break;
    }
}

void walk_macro_syntax_data(Ast_Macro_SyntaxData* data, Ast_Walk_State* state) {
    switch (data->kind.kind) {
        case Macro_Expression: {
            walk_expression(data->data.expression, state);
            break;
        }
        case Macro_Multiple: {
            if (data->kind.data.multiple->kind == Macro_Expression) {
                walk_macro_syntax_data(data->data.multiple, state);
            }
            break;
        }
        default:
            break;
    }
}
