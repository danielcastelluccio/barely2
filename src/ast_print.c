#include <stdio.h>

#include "ast_print.h"

void print_indents(size_t indent) {
    for (size_t i = 0; i < indent; i++) {
        printf("  ");
    }
}

void print_type(Type* type, size_t* indent) {
    switch (type->kind) {
        case Type_Basic: {
            Basic_Type* basic = &type->data.basic;
            print_indents(*indent);
            printf("Basic {\n");
            *indent += 1;

            if (basic->kind == Type_Single) {
                print_indents(*indent);
                printf("name: %s\n", basic->data.single);
            } else {
                print_indents(*indent);
                printf("names: [");
                for (int i = 0; i < basic->data.multi.count; i++) {
                    printf("%s", basic->data.multi.elements[i]);
                    if (i < basic->data.multi.count - 1) {
                        printf(" ");
                    }
                }
                printf("]\n");
            }

            *indent -= 1;
            print_indents(*indent);
            printf("}\n");
            break;
        }
    }
}

void print_expression_node(Expression_Node* expression, size_t* indent);

void print_statement_node(Statement_Node* statement, size_t* indent) {
    switch (statement->kind) {
        case Statement_Expression: {
            print_indents(*indent);
            printf("Statement_Expression {\n");
            *indent += 1;

            print_indents(*indent);
            printf("expression:\n");
            *indent += 1;

            print_expression_node(statement->data.expression.expression, indent);

            *indent -= 2;
            print_indents(*indent);
            printf("}\n");
            break;
        }
        case Statement_Declare: {
            Statement_Declare_Node* declare = &statement->data.declare;

            print_indents(*indent);
            printf("Declare {\n");
            *indent += 1;

            print_indents(*indent);
            printf("names: [\n");
            *indent += 1;
            for (int i = 0; i < declare->declarations.count; i++) {
                Declaration* declaration = &declare->declarations.elements[i];
                print_indents(*indent);
                printf("Declaration {\n");
                *indent += 1;

                print_indents(*indent);
                printf("name: %s\n", declaration->name);

                print_indents(*indent);
                printf("type: \n");
                *indent += 1;
                print_type(&declaration->type, indent);
                *indent -= 1;

                *indent -= 1;
                print_indents(*indent);
                printf("}\n");
            }
            *indent -= 1;
            print_indents(*indent);
            printf("]\n");

            print_indents(*indent);
            printf("expression:\n");
            *indent += 1;

            if (declare->expression != NULL) {
                print_expression_node(declare->expression, indent);
            } else {
                print_indents(*indent);
                printf("NULL\n");
            }

            *indent -= 2;
            print_indents(*indent);
            printf("}\n");
            break;
        }
        case Statement_Assign: {
            Statement_Assign_Node* assign = &statement->data.assign;

            print_indents(*indent);
            printf("Assign {\n");
            *indent += 1;

            print_indents(*indent);
            printf("parts: [\n");
            *indent += 1;

            for (int i = 0; i < assign->parts.count; i++) {
                Statement_Assign_Part* part = &assign->parts.elements[i];
                print_indents(*indent);
                printf("Part {\n");
                *indent += 1;

                switch (part->kind) {
                    case Assign_Single: {
                        print_indents(*indent);
                        printf("name: %s\n", part->data.single.name);

                        if (part->data.single.expression != NULL) {
                            print_indents(*indent);
                            printf("expression: \n");
                            *indent += 1;
                            print_expression_node(part->data.single.expression, indent);
                            *indent -= 1;
                        }
                        break;
                    }
                    case Assign_Multi: {
                        print_indents(*indent);
                        printf("names: [");
                        for (int i = 0; i < part->data.multi.count; i++) {
                            printf("%s", part->data.multi.elements[i]);
                            if (i < part->data.multi.count - 1) {
                                printf(" ");
                            }
                        }
                        printf("]\n");
                        break;
                    }
                }

                *indent -= 1;
                print_indents(*indent);
                printf("}\n");
            }

            *indent -= 1;
            print_indents(*indent);
            printf("]\n");

            print_indents(*indent);
            printf("expression:\n");
            *indent += 1;

            if (assign->expression != NULL) {
                print_expression_node(assign->expression, indent);
            } else {
                print_indents(*indent);
                printf("NULL\n");
            }

            *indent -= 1;
            print_indents(*indent);
            printf("}\n");
            break;
        }
    }
}

void print_expression_node(Expression_Node* expression, size_t* indent) {
    switch (expression->kind) {
        case Expression_Block: {
            Block_Node* block = &expression->data.block;

            print_indents(*indent);
            printf("Block {\n");
            *indent += 1;

            print_indents(*indent);
            printf("statements: [\n");
            *indent += 1;

            for (int i = 0; i < block->statements.count; i++) {
                print_statement_node(block->statements.elements[i], indent);
            }

            *indent -= 1;
            print_indents(*indent);
            printf("]\n");

            *indent -= 1;
            print_indents(*indent);
            printf("}\n");
            break;
        }
        case Expression_Invoke: {
            Invoke_Node* invoke = &expression->data.invoke;

            print_indents(*indent);
            printf("Invoke {\n");
            *indent += 1;

            switch (invoke->kind) {
                case Invoke_Standard:
                    print_indents(*indent);
                    printf("procedure:\n");

                    *indent += 1;
                    print_expression_node(invoke->data.procedure, indent);
                    *indent -= 1;
                    break;
                case Invoke_Operator:
                    print_indents(*indent);
                    printf("operator: ");
                    switch (invoke->data.operator) {
                        case Operator_Add:
                            printf("ADD");
                            break;
                        case Operator_Subtract:
                            printf("SUBTRACT");
                            break;
                        case Operator_Multiply:
                            printf("MULTIPLY");
                            break;
                        case Operator_Divide:
                            printf("DIVIDE");
                            break;
                        case Operator_Equal:
                            printf("EQUAL");
                            break;
                        case Operator_Greater:
                            printf("GREATER");
                            break;
                        case Operator_Less:
                            printf("LESS");
                            break;
                        case Operator_GreaterEqual:
                            printf("GREATER_EQUAL");
                            break;
                        case Operator_LessEqual:
                            printf("LESS_EQUAL");
                            break;
                    }
                    printf("\n");
                    break;
            }

            print_indents(*indent);
            printf("arguments: [\n");
            *indent += 1;

            for (int i = 0; i < invoke->arguments.count; i++) {
                print_expression_node(invoke->arguments.elements[i], indent);
            }

            *indent -= 1;
            print_indents(*indent);
            printf("]\n");

            *indent -= 1;
            print_indents(*indent);
            printf("}\n");
            break;
        }
        case Expression_Number: {
            Number_Node* number = &expression->data.number;

            print_indents(*indent);
            printf("Number {\n");
            *indent += 1;

            print_indents(*indent);
            printf("value: %i\n", number->value);

            *indent -= 1;
            print_indents(*indent);
            printf("}\n");
            break;
        }
        case Expression_String: {
            String_Node* string = &expression->data.string;

            print_indents(*indent);
            printf("String {\n");
            *indent += 1;

            print_indents(*indent);
            printf("value: \"%s\"\n", string->value);

            *indent -= 1;
            print_indents(*indent);
            printf("}\n");
            break;
        }
        case Expression_If: {
            If_Node* if_ = &expression->data.if_;

            print_indents(*indent);
            printf("If {\n");
            *indent += 1;

            if (if_->condition != NULL) {
                print_indents(*indent);
                printf("condition:\n");

                *indent += 1;
                print_expression_node(if_->condition, indent);
                *indent -= 1;
            }

            print_indents(*indent);
            printf("inside:\n");

            *indent += 1;
            print_expression_node(if_->inside, indent);
            *indent -= 1;

            if (if_->next != NULL) {
                print_indents(*indent);
                printf("next:\n");

                *indent += 1;
                Expression_Node if_node;
                if_node.kind = Expression_If;
                if_node.data.if_ = *if_->next;
                print_expression_node(&if_node, indent);
                *indent -= 1;
            }

            *indent -= 1;
            print_indents(*indent);
            printf("}\n");
            break;
        }
        case Expression_While: {
            While_Node* while_ = &expression->data.while_;

            print_indents(*indent);
            printf("While {\n");
            *indent += 1;

            print_indents(*indent);
            printf("condition:\n");

            *indent += 1;
            print_expression_node(while_->condition, indent);
            *indent -= 1;

            print_indents(*indent);
            printf("inside:\n");

            *indent += 1;
            print_expression_node(while_->inside, indent);
            *indent -= 1;

            *indent -= 1;
            print_indents(*indent);
            printf("}\n");
            break;
        }
        case Expression_Retrieve: {
            Retrieve_Node* retrieve = &expression->data.retrieve;

            print_indents(*indent);
            printf("Retrieve {\n");
            *indent += 1;

            switch (retrieve->kind) {
                case Retrieve_Single: {
                    print_indents(*indent);
                    printf("name: %s\n", retrieve->data.single.name);

                    if (retrieve->data.single.expression != NULL) {
                        print_indents(*indent);
                        printf("expression: \n");
                        *indent += 1;
                        print_expression_node(retrieve->data.single.expression, indent);
                        *indent -= 1;
                    }
                    break;
                }
                case Retrieve_Multi: {
                    print_indents(*indent);
                    printf("names: [");
                    for (int i = 0; i < retrieve->data.multi.count; i++) {
                        printf("%s", retrieve->data.multi.elements[i]);
                        if (i < retrieve->data.multi.count - 1) {
                            printf(" ");
                        }
                    }
                    printf("]\n");
                    break;
                }
            }

            *indent -= 1;
            print_indents(*indent);
            printf("}\n");
            break;
        }
        case Expression_Multi: {
            Multi_Expression_Node* multi = &expression->data.multi;

            print_indents(*indent);
            printf("Multi {\n");
            *indent += 1;

            print_indents(*indent);
            printf("expressions: [\n");
            *indent += 1;

            for (int i = 0; i < multi->expressions.count; i++) {
                print_expression_node(multi->expressions.elements[i], indent);
            }

            *indent -= 1;
            print_indents(*indent);
            printf("]\n");

            *indent -= 1;
            print_indents(*indent);
            printf("}\n");
            break;
        }
        default:
            print_indents(*indent);
            printf("Unhandled\n");
    }
}

void print_definition_node(Definition_Node* definition, size_t* indent) {
    switch (definition->kind) {
        case Definition_Procedure: {
            Procedure_Literal_Node* node = &definition->data.procedure.data.literal;
            print_indents(*indent);
            printf("Procedure {\n");

            *indent += 1;
            print_indents(*indent);
            printf("name: %s\n", definition->name);

            print_indents(*indent);
            printf("arguments: [\n");
            *indent += 1;

            for (int i = 0; i < node->arguments.count; i++) {
                Declaration* declaration = &node->arguments.elements[i];
                print_indents(*indent);
                printf("Declaration {\n");
                *indent += 1;

                print_indents(*indent);
                printf("name: %s\n", declaration->name);

                print_indents(*indent);
                printf("type: \n");
                *indent += 1;
                print_type(&declaration->type, indent);
                *indent -= 1;

                *indent -= 1;
                print_indents(*indent);
                printf("}\n");
            }

            *indent -= 1;
            print_indents(*indent);
            printf("]\n");

            print_indents(*indent);
            printf("body:\n");
            *indent += 1;

            print_expression_node(definition->data.procedure.data.literal.body, indent);

            *indent -= 2;

            print_indents(*indent);
            printf("}\n");
            break;
        }
        case Definition_Type: {
            Struct_Node* node = &definition->data.type.data.struct_;
            print_indents(*indent);
            printf("Type {\n");

            *indent += 1;
            print_indents(*indent);
            printf("name: %s\n", definition->name);

            print_indents(*indent);
            printf("items: [\n");
            *indent += 1;

            for (int i = 0; i < node->items.count; i++) {
                Declaration* declaration = &node->items.elements[i];
                print_indents(*indent);
                printf("Declaration {\n");
                *indent += 1;

                print_indents(*indent);
                printf("name: %s\n", declaration->name);

                print_indents(*indent);
                printf("type: \n");
                *indent += 1;
                print_type(&declaration->type, indent);
                *indent -= 1;

                *indent -= 1;
                print_indents(*indent);
                printf("}\n");
            }

            *indent -= 1;
            print_indents(*indent);
            printf("]\n");

            *indent -= 1;

            print_indents(*indent);
            printf("}\n");
            break;
        }
    }
}

void print_file_node(File_Node* file_node) {
    size_t indent = 0;

    printf("[\n");
    for (int i = 0; i < file_node->definitions.count; i++) {
        Definition_Node *definition = &file_node->definitions.elements[i];
        indent += 1;
        print_definition_node(definition, &indent);
        indent -= 1;
    }
    printf("]\n");
}
