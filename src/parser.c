#include <stdio.h>

#include "parser.h"
#include "tokenizer.h"

Token_Kind peek(Tokens* tokens, size_t index) {
    Token* current = &tokens->elements[index];
    return current->kind;
}

Token_Kind consume(Tokens* tokens, size_t* index) {
    Token* current = &tokens->elements[*index];
    (*index)++;
    return current->kind;
}

void consume_check(Tokens* tokens, size_t* index, Token_Kind wanted_kind) {
    Token* current = &tokens->elements[*index];
    if (current->kind != wanted_kind) {
        Token temp = (Token) {
            wanted_kind
        };

        printf("Error: Expected ");
        print_token(&temp, false);
        printf(", got ");
        print_token(current, false);
        printf("\n");
        exit(1);
    }
    (*index)++;
}

char* consume_string(Tokens* tokens, size_t* index) {
    Token* current = &tokens->elements[*index];
    if (current->kind != Token_String) {
        printf("Error: Expected String, got ");
        print_token(current, false);
        printf("\n");
        exit(1);
    }
    (*index)++;
    return current->data;
}

char* consume_boolean(Tokens* tokens, size_t* index) {
    Token* current = &tokens->elements[*index];
    if (current->kind != Token_Boolean) {
        printf("Error: Expected Boolean, got ");
        print_token(current, false);
        printf("\n");
        exit(1);
    }
    (*index)++;
    return current->data;
}

char* consume_number(Tokens* tokens, size_t* index) {
    Token* current = &tokens->elements[*index];
    if (current->kind != Token_Number) {
        printf("Error: Expected Number, got ");
        print_token(current, false);
        printf("\n");
        exit(1);
    }
    (*index)++;
    return current->data;
}

char* consume_keyword(Tokens* tokens, size_t* index) {
    Token* current = &tokens->elements[*index];
    if (current->kind != Token_Keyword) {
        printf("Error: Expected Keyword, got ");
        print_token(current, false);
        printf("\n");
        exit(1);
    }
    (*index)++;
    return current->data;
}

char* consume_identifier(Tokens* tokens, size_t* index) {
    Token* current = &tokens->elements[*index];
    if (current->kind != Token_Identifier) {
        printf("Error: Expected Identifier, got ");
        print_token(current, false);
        printf("\n");
        exit(1);
    }
    (*index)++;
    return current->data;
}

Type parse_type(Tokens* tokens, size_t* index_in) {
    size_t index = *index_in;
    Type result;

    if (peek(tokens, index) == Token_Asterisk) {
        Pointer_Type pointer;

        consume(tokens, &index);

        Type child = parse_type(tokens, &index);
        Type* child_allocated = malloc(sizeof(Type));
        *child_allocated = child;
        pointer.child = child_allocated;

        result.kind = Type_Pointer;
        result.data.pointer = pointer;
    } else if (peek(tokens, index) == Token_LeftBracket) {
        consume(tokens, &index);
        
        BArray_Type array = {};

        if (peek(tokens, index) != Token_RightBracket) {
            array.has_size = true;
            char* size = consume_number(tokens, &index);
            array.size = atoi(size);
        }

        consume_check(tokens, &index, Token_RightBracket);

        Type child = parse_type(tokens, &index);
        Type* child_allocated = malloc(sizeof(Type));
        *child_allocated = child;
        array.element_type = child_allocated;

        result.kind = Type_Array;
        result.data.array = array;
    } else {
        char* name = consume_identifier(tokens, &index);
        if (strcmp(name, "usize") == 0 || strcmp(name, "u64") == 0 || strcmp(name, "u32") == 0 || strcmp(name, "u16") == 0 || strcmp(name, "u8") == 0) {
            Internal_Type internal;

            if (strcmp(name, "usize") == 0) {
                internal = Type_USize;
            } else if (strcmp(name, "u64") == 0) {
                internal = Type_U64;
            } else if (strcmp(name, "u32") == 0) {
                internal = Type_U32;
            } else if (strcmp(name, "u16") == 0) {
                internal = Type_U16;
            } else if (strcmp(name, "u8") == 0) {
                internal = Type_U8;
            }

            result.kind = Type_Internal;
            result.data.internal = internal;
        } else {
            Basic_Type basic;
            basic.kind = Type_Single;

            basic.data.single = name;

            if (peek(tokens, index) == Token_DoubleColon) {
                Array_String names = array_string_new(2);
                array_string_append(&names, basic.data.single);
                while (peek(tokens, index) == Token_DoubleColon) {
                    consume(tokens, &index);
                    array_string_append(&names, consume_identifier(tokens, &index));
                }

                basic.kind = Type_Multi;
                basic.data.multi = names;
            }

            result.kind = Type_Basic;
            result.data.basic = basic;
        }
    }

    *index_in = index;
    return result;
}

Statement_Node parse_statement(Tokens* tokens, size_t* index_in) {
    size_t index = *index_in;
    Statement_Node result;

    Token_Kind token = peek(tokens, index);
    if (token == Token_Keyword && strcmp(tokens->elements[index].data, "var") == 0) {
        Statement_Declare_Node node = {};

        consume(tokens, &index);

        Array_Declaration declarations = array_declaration_new(8);
        while (peek(tokens, index) != Token_Equals && peek(tokens, index) != Token_Semicolon) {
            if (peek(tokens, index) == Token_Comma) {
                consume(tokens, &index);
                continue;
            }
            Declaration declaration;
            declaration.location = tokens->elements[index].location;
            declaration.name = consume_identifier(tokens, &index);
            consume_check(tokens, &index, Token_Colon);
            declaration.type = parse_type(tokens, &index);
            array_declaration_append(&declarations, declaration);
        }
        node.declarations = declarations;

        Token_Kind next = consume(tokens, &index);
        if (next == Token_Equals) {
            Expression_Node expression = parse_expression(tokens, &index);
            Expression_Node* expression_allocated = malloc(sizeof(Expression_Node));
            *expression_allocated = expression;
            node.expression = expression_allocated;

            consume_check(tokens, &index, Token_Semicolon);
        } else if (next == Token_Semicolon) {
        } else {
            printf("Error: Unexpected token ");
            print_token(&tokens->elements[index - 1], false);
            printf("\n");
            exit(1);
        }

        result.kind = Statement_Declare;
        result.data.declare = node;
    } else if (token == Token_Keyword && strcmp(tokens->elements[index].data, "return") == 0) {
        Statement_Return_Node node;
        node.location = tokens->elements[index].location;

        consume(tokens, &index);

        Expression_Node expression = parse_expression(tokens, &index);
        Expression_Node* expression_allocated = malloc(sizeof(Expression_Node));
        *expression_allocated = expression;
        node.expression = expression_allocated;

        consume_check(tokens, &index, Token_Semicolon);

        result.kind = Statement_Return;
        result.data.return_ = node;
    } else {
        Statement_Expression_Node node;

        Expression_Node expression = parse_expression(tokens, &index);
        Expression_Node* expression_allocated = malloc(sizeof(Expression_Node));
        *expression_allocated = expression;
        node.expression = expression_allocated;

        Token_Kind token = consume(tokens, &index);
        switch (token) {
            case Token_Equals: {
                Statement_Assign_Node assign = {};
                assign.parts = array_statement_assign_part_new(2);

                if (expression.kind == Expression_Retrieve) {
                    Statement_Assign_Part assign_part;
                    assign_part.location = expression.data.retrieve.location;
                    assign_part.kind = expression.data.retrieve.kind;
                    assign_part.data = expression.data.retrieve.data;
                    array_statement_assign_part_append(&assign.parts, assign_part);
                } else if (expression.kind == Expression_Multi) {
                    Multi_Expression_Node* multi = &expression.data.multi;
                    for (size_t i = 0; i < multi->expressions.count; i++) {
                        Expression_Node* expression = multi->expressions.elements[i];
                        Statement_Assign_Part assign_part;
                        assign_part.kind = expression->data.retrieve.kind;
                        assign_part.data = expression->data.retrieve.data;
                        array_statement_assign_part_append(&assign.parts, assign_part);
                    }
                }

                Expression_Node expression = parse_expression(tokens, &index);
                Expression_Node* expression_allocated = malloc(sizeof(Expression_Node));
                *expression_allocated = expression;
                assign.expression = expression_allocated;

                result.kind = Statement_Assign;
                result.data.assign = assign;

                consume_check(tokens, &index, Token_Semicolon);
                break;
            }
            case Token_Semicolon: {
                result.kind = Statement_Expression;
                result.data.expression = node;
                break;
            }
            default: {
                printf("Error: Unexpected token ");
                print_token(&tokens->elements[index - 1], false);
                printf("\n");
                exit(1);
                break;
            }
        }
    }

    *index_in = index;
    return result;
}

Expression_Node parse_expression(Tokens* tokens, size_t* index_in) {
    size_t index = *index_in;
    Expression_Node result;

    switch (peek(tokens, index)) {
        case Token_LeftCurlyBrace: {
            Block_Node node;
            consume(tokens, &index);

            Array_Statement_Node statements = array_statement_node_new(32);

            while (peek(tokens, index) != Token_RightCurlyBrace) {
                Statement_Node statement = parse_statement(tokens, &index);
                Statement_Node* statement_allocated = malloc(sizeof(Statement_Node));
                *statement_allocated = statement;
                array_statement_node_append(&statements, statement_allocated);
            }
            node.statements = statements;
            consume(tokens, &index);

            result.kind = Expression_Block;
            result.data.block = node;
            break;
        }
        case Token_Number: {
            Number_Node node;

            int value = atoi(consume_number(tokens, &index));
            node.value = value;

            result.kind = Expression_Number;
            result.data.number = node;
            break;
        }
        case Token_String: {
            String_Node node;

            node.value = consume_string(tokens, &index);

            result.kind = Expression_String;
            result.data.string = node;
            break;
        }
        case Token_Boolean: {
            Boolean_Node node;

            node.value = strcmp(consume_boolean(tokens, &index), "true") == 0;

            result.kind = Expression_Boolean;
            result.data.boolean = node;
            break;
        }
        case Token_Identifier: {
            Retrieve_Node node = {};
            node.location = tokens->elements[index].location;
            char* name = consume_identifier(tokens, &index);
            node.kind = Complex_Single;
            node.data.single.name = name;

            if (peek(tokens, index) == Token_DoubleColon) {
                node.kind = Complex_Multi;
                Array_String names = array_string_new(2);
                array_string_append(&names, name);
                while (peek(tokens, index) == Token_DoubleColon) {
                    consume(tokens, &index);

                    array_string_append(&names, consume_identifier(tokens, &index));
                }
                node.data.multi = names;
            }

            result.kind = Expression_Retrieve;
            result.data.retrieve = node;
            break;
        }
        case Token_Keyword: {
            char* name = consume_keyword(tokens, &index);

            if (strcmp(name, "if") == 0) {
                If_Node node = {};

                Expression_Node condition = parse_expression(tokens, &index);
                Expression_Node* condition_allocated = malloc(sizeof(Expression_Node));
                *condition_allocated = condition;
                node.condition = condition_allocated;

                Expression_Node inside = parse_expression(tokens, &index);
                Expression_Node* inside_allocated = malloc(sizeof(Expression_Node));
                *inside_allocated = inside;
                node.inside = inside_allocated;

                If_Node* current = &node;
                while (tokens->elements[index].kind == Token_Keyword && strcmp(tokens->elements[index].data, "else") == 0) {
                    If_Node next_node = {};
                    consume(tokens, &index);

                    Token next = tokens->elements[index];
                    if (next.kind == Token_Keyword && strcmp(next.data, "if") == 0) {
                        consume(tokens, &index);

                        Expression_Node condition = parse_expression(tokens, &index);
                        Expression_Node* condition_allocated = malloc(sizeof(Expression_Node));
                        *condition_allocated = condition;
                        next_node.condition = condition_allocated;
                    }

                    Expression_Node inside = parse_expression(tokens, &index);
                    Expression_Node* inside_allocated = malloc(sizeof(Expression_Node));
                    *inside_allocated = inside;
                    next_node.inside = inside_allocated;

                    If_Node* next_node_allocated = malloc(sizeof(If_Node));
                    *next_node_allocated = next_node;
                    current->next = next_node_allocated;
                    current = next_node_allocated;
                }

                result.kind = Expression_If;
                result.data.if_ = node;
            } else if (strcmp(name, "while") == 0) {
                While_Node node;

                Expression_Node condition = parse_expression(tokens, &index);
                Expression_Node* condition_allocated = malloc(sizeof(Expression_Node));
                *condition_allocated = condition;
                node.condition = condition_allocated;

                Expression_Node inside = parse_expression(tokens, &index);
                Expression_Node* inside_allocated = malloc(sizeof(Expression_Node));
                *inside_allocated = inside;
                node.inside = inside_allocated;

                result.kind = Expression_While;
                result.data.while_ = node;
            } else if (strcmp(name, "cast") == 0) {
                Cast_Node node;

                consume_check(tokens, &index, Token_LeftParenthesis);

                Type type = parse_type(tokens, &index);
                node.type = type;

                consume_check(tokens, &index, Token_RightParenthesis);
                
                Expression_Node inner = parse_expression(tokens, &index);
                Expression_Node* inner_allocated = malloc(sizeof(Expression_Node));
                *inner_allocated = inner;
                node.expression = inner_allocated;

                result.kind = Expression_Cast;
                result.data.cast = node;
                break;
            } else {
                printf("Error: Unexpected token ");
                print_token(&tokens->elements[index - 1], false);
                printf("\n");
                exit(1);
            }
            break;
        }
        case Token_Ampersand: {
            Reference_Node node;

            consume(tokens, &index);
            
            Expression_Node inner = parse_expression(tokens, &index);
            Expression_Node* inner_allocated = malloc(sizeof(Expression_Node));
            *inner_allocated = inner;
            node.inner = inner_allocated;

            result.kind = Expression_Reference;
            result.data.reference = node;
            break;
        }
        case Token_LeftParenthesis: {
            consume(tokens, &index);

            result = parse_expression(tokens, &index);

            consume_check(tokens, &index, Token_RightParenthesis);
            break;
        }
        default: {
            printf("Error: Unexpected token ");
            print_token(&tokens->elements[index], false);
            printf("\n");
            exit(1);
        }
    }

    bool running = true;
    while (running) {
        running = false;
        if (peek(tokens, index) == Token_Period) {
            running = true;
            Retrieve_Node node;
            node.kind = Complex_Single;

            consume(tokens, &index);

            Expression_Node* previous_result = malloc(sizeof(Expression_Node));
            *previous_result = result;
            node.data.single.expression = previous_result;

            if (peek(tokens, index) == Token_Asterisk) {
                consume(tokens, &index);
                node.data.single.name = "*";
            } else {
                node.data.single.name = consume_identifier(tokens, &index);
            }

            result.kind = Expression_Retrieve;
            result.data.retrieve = node;
            continue;
        }

        if (peek(tokens, index) == Token_LeftBracket) {
            running = true;
            Retrieve_Node node;
            node.kind = Complex_Array;
            node.location = tokens->elements[index].location;

            Expression_Node* previous_result = malloc(sizeof(Expression_Node));
            *previous_result = result;
            node.data.array.expression_outer = previous_result;

            consume(tokens, &index);

            Expression_Node inner = parse_expression(tokens, &index);
            Expression_Node* inner_allocated = malloc(sizeof(Expression_Node));
            *inner_allocated = inner;
            node.data.array.expression_inner = inner_allocated;

            consume_check(tokens, &index, Token_RightBracket);

            result.kind = Expression_Retrieve;
            result.data.retrieve = node;
            continue;
        }

        if (peek(tokens, index) == Token_LeftParenthesis) {
            running = true;
            Invoke_Node node;
            node.location = tokens->elements[index].location;
            node.kind = Invoke_Standard;

            Expression_Node* previous_result = malloc(sizeof(Expression_Node));
            *previous_result = result;
            node.data.procedure = previous_result;
            consume(tokens, &index);

            Array_Expression_Node arguments = array_expression_node_new(32);

            if (peek(tokens, index) != Token_RightParenthesis) {
                Expression_Node expression = parse_expression(tokens, &index);
                Expression_Node* expression_allocated = malloc(sizeof(Expression_Node));
                *expression_allocated = expression;
                if (expression_allocated->kind == Expression_Multi) {
                    arguments = expression_allocated->data.multi.expressions;
                } else {
                    array_expression_node_append(&arguments, expression_allocated);
                }
            }

            node.arguments = arguments;

            consume(tokens, &index);

            result.kind = Expression_Invoke;
            result.data.invoke = node;
            continue;
        }

        if (peek(tokens, index) == Token_Plus ||
                peek(tokens, index) == Token_Minus ||
                peek(tokens, index) == Token_Asterisk ||
                peek(tokens, index) == Token_Slash ||
                peek(tokens, index) == Token_DoubleEquals ||
                peek(tokens, index) == Token_ExclamationEquals ||
                peek(tokens, index) == Token_GreaterThan ||
                peek(tokens, index) == Token_LessThan ||
                peek(tokens, index) == Token_LessThanEqual ||
                peek(tokens, index) == Token_GreaterThanEqual) {
            running = true;
            Invoke_Node node;
            node.kind = Invoke_Operator;
            node.location = tokens->elements[index].location;

            Operator operator;
            switch (consume(tokens, &index)) {
                case Token_Plus:
                    operator = Operator_Add;
                    break;
                case Token_Minus:
                    operator = Operator_Subtract;
                    break;
                case Token_Asterisk:
                    operator = Operator_Multiply;
                    break;
                case Token_Slash:
                    operator = Operator_Divide;
                    break;
                case Token_DoubleEquals:
                    operator = Operator_Equal;
                    break;
                case Token_ExclamationEquals:
                    operator = Operator_NotEqual;
                    break;
                case Token_GreaterThan:
                    operator = Operator_Greater;
                    break;
                case Token_LessThan:
                    operator = Operator_Less;
                    break;
                case Token_GreaterThanEqual:
                    operator = Operator_GreaterEqual;
                    break;
                case Token_LessThanEqual:
                    operator = Operator_LessEqual;
                    break;
            }
            node.data.operator.operator = operator;

            Expression_Node* left_side = malloc(sizeof(Expression_Node));
            *left_side = result;

            Expression_Node* right_side = malloc(sizeof(Expression_Node));
            *right_side = parse_expression(tokens, &index);

            Array_Expression_Node arguments = array_expression_node_new(32);
            array_expression_node_append(&arguments, left_side);
            array_expression_node_append(&arguments, right_side);
            node.arguments = arguments;

            result.kind = Expression_Invoke;
            result.data.invoke = node;
            continue;
        }

        if (peek(tokens, index) == Token_Comma) {
            consume(tokens, &index);
            if (result.kind == Expression_Multi) {
                Expression_Node next = parse_expression(tokens, &index);
                Expression_Node* next_allocated = malloc(sizeof(Expression_Node));
                *next_allocated = next;
                array_expression_node_append(&result.data.multi.expressions, next_allocated);
            } else {
                Multi_Expression_Node node;
                node.expressions = array_expression_node_new(2);

                Expression_Node* previous_result = malloc(sizeof(Expression_Node));
                *previous_result = result;
                array_expression_node_append(&node.expressions, previous_result);

                Expression_Node next = parse_expression(tokens, &index);
                Expression_Node* next_allocated = malloc(sizeof(Expression_Node));
                *next_allocated = next;
                if (next_allocated->kind == Expression_Multi) {
                    for (size_t i = 0; i < next_allocated->data.multi.expressions.count; i++) {
                        array_expression_node_append(&node.expressions, next_allocated->data.multi.expressions.elements[i]);
                    }
                } else {
                    array_expression_node_append(&node.expressions, next_allocated);
                }

                result.kind = Expression_Multi;
                result.data.multi = node;
            }

            running = true;
            continue;
        }
    }

    *index_in = index;
    return result;
}

Definition_Node parse_definition(Tokens* tokens, size_t* index_in) {
    size_t index = *index_in;
    Definition_Node result;

    char* keyword = consume_keyword(tokens, &index);
    if (strcmp(keyword, "proc") == 0) {
        char* name = consume_identifier(tokens, &index);
        result.name = name;

        consume_check(tokens, &index, Token_Colon);

        Procedure_Node node;
        switch (consume(tokens, &index)) {
            case Token_LeftParenthesis:
                Procedure_Literal_Node literal_node;

                Array_Declaration arguments = array_declaration_new(4);
                while (peek(tokens, index) != Token_RightParenthesis) {
                    if (peek(tokens, index) == Token_Comma) {
                        index += 1;
                        continue;
                    }

                    char* name = consume_identifier(tokens, &index);
                    consume_check(tokens, &index, Token_Colon);
                    Type type = parse_type(tokens, &index);
                    array_declaration_append(&arguments, (Declaration) { name, type });
                }
                literal_node.arguments = arguments;
                consume_check(tokens, &index, Token_RightParenthesis);

                literal_node.returns = array_type_new(1);
                if (peek(tokens, index) == Token_Colon) {
                    consume(tokens, &index);

                    bool running = true;
                    while (running) {
                        Type type = parse_type(tokens, &index);
                        Type* type_allocated = malloc(sizeof(Type));
                        *type_allocated = type;

                        array_type_append(&literal_node.returns, type_allocated);
                        running = false;
                        if (peek(tokens, index) == Token_Comma) {
                            running = true;
                            consume(tokens, &index);
                        }
                    }
                }

                Expression_Node body = parse_expression(tokens, &index);
                Expression_Node* body_allocated = malloc(sizeof(Expression_Node));
                *body_allocated = body;
                literal_node.body = body_allocated;

                consume_check(tokens, &index, Token_Semicolon);

                node.kind = Procedure_Literal;
                node.data.literal = literal_node;
                break;
            default:
                printf("Error: Unexpected token ");
                print_token(&tokens->elements[index - 1], false);
                printf("\n");
                exit(1);
                break;
        }

        result.kind = Definition_Procedure;
        result.data.procedure = node;
    } else if (strcmp(keyword, "type") == 0) {
        Type_Node node;

        char* name = consume_identifier(tokens, &index);
        result.name = name;

        consume_check(tokens, &index, Token_Colon);

        switch (peek(tokens, index)) {
            case Token_LeftCurlyBrace: {
                Struct_Node struct_node;
                consume(tokens, &index);

                Array_Declaration items = array_declaration_new(4);
                while (peek(tokens, index) != Token_RightCurlyBrace) {
                    if (peek(tokens, index) == Token_Semicolon) {
                        consume(tokens, &index);
                        continue;
                    }

                    char* name = consume_identifier(tokens, &index);
                    consume_check(tokens, &index, Token_Colon);
                    Type type = parse_type(tokens, &index);

                    array_declaration_append(&items, (Declaration) { name, type });
                }
                struct_node.items = items;
                consume(tokens, &index);

                node.kind = Type_Node_Struct;
                node.data.struct_ = struct_node;
                break;
            }
            default:
                printf("Error: Unexpected token ");
                print_token(&tokens->elements[index], false);
                printf("\n");
                exit(1);
        }

        consume_check(tokens, &index, Token_Semicolon);

        result.kind = Definition_Type;
        result.data.type = node;
    } else if (strcmp(keyword, "mod") == 0) {
        Module_Node node;
        node.definitions = array_definition_node_new(8);
        
        char* name = consume_identifier(tokens, &index);
        consume_check(tokens, &index, Token_Colon);
        consume_check(tokens, &index, Token_LeftCurlyBrace);

        while (peek(tokens, index) != Token_RightCurlyBrace) {
            Definition_Node definition = parse_definition(tokens, &index);
            Definition_Node* definition_allocated = malloc(sizeof(Definition_Node));
            *definition_allocated = definition;
            array_definition_node_append(&node.definitions, definition);
        }

        consume(tokens, &index);
        consume_check(tokens, &index, Token_Semicolon);

        result.kind = Definition_Module;
        result.data.module = node;
    } else {
        printf("Error: Unexpected token ");
        print_token(&tokens->elements[index - 1], false);
        printf("\n");
        exit(1);
    }

    *index_in = index;
    return result;
}

File_Node parse(Tokens* tokens) {
    File_Node result;

    Array_Definition_Node array = array_definition_node_new(32);
    size_t index = 0;
    while (index < tokens->count) {
        Definition_Node node = parse_definition(tokens, &index);
        array_definition_node_append(&array, node);
    }

    result.definitions = array;
    return result;
}
