#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

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
            wanted_kind,
            NULL,
            (Location) {
                NULL,
                0,
                0,
            },
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
            Type* size_type = malloc(sizeof(Type));
            *size_type = parse_type(tokens, &index);
            array.size_type = size_type;
        }

        consume_check(tokens, &index, Token_RightBracket);

        Type child = parse_type(tokens, &index);
        Type* child_allocated = malloc(sizeof(Type));
        *child_allocated = child;
        array.element_type = child_allocated;

        result.kind = Type_Array;
        result.data.array = array;
    } else if (peek(tokens, index) == Token_Keyword) {
        char* keyword = consume_keyword(tokens, &index);

        if (strcmp(keyword, "proc") == 0) {
            index += 1;

            Procedure_Type procedure = {
                .arguments = array_type_new(2),
                .returns = array_type_new(1),
            };

            while (peek(tokens, index) != Token_RightParenthesis) {
                if (peek(tokens, index) == Token_Comma) {
                    index += 1;
                    continue;
                }

                Type* type = malloc(sizeof(Type));
                *type = parse_type(tokens, &index);
                array_type_append(&procedure.arguments, type);
            }

            consume(tokens, &index);

            if (peek(tokens, index) == Token_Colon) {
                index += 1;

                bool looking_at_returns = true;
                while (looking_at_returns) {
                    Type* type = malloc(sizeof(Type));
                    *type = parse_type(tokens, &index);
                    array_type_append(&procedure.returns, type);

                    looking_at_returns = peek(tokens, index) == Token_Comma;
                }
            }

            result.kind = Type_Procedure;
            result.data.procedure = procedure;
        } else if (strcmp(keyword, "struct") == 0 || strcmp(keyword, "union") == 0) {
            bool is_struct = strcmp(keyword, "struct") == 0;
            consume(tokens, &index);

            Array_Declaration_Pointer items = array_declaration_pointer_new(4);
            while (peek(tokens, index) != Token_RightCurlyBrace) {
                if (peek(tokens, index) == Token_Comma) {
                    consume(tokens, &index);
                    continue;
                }

                Location location = tokens->elements[index].location;
                char* name = consume_identifier(tokens, &index);
                consume_check(tokens, &index, Token_Colon);
                Type type = parse_type(tokens, &index);

                Declaration* declaration = malloc(sizeof(Declaration));
                declaration->name = name;
                declaration->type = type;
                declaration->location = location;
                array_declaration_pointer_append(&items, declaration);
            }
            consume(tokens, &index);

            if (is_struct) {
                Struct_Type struct_type;
                struct_type.items = items;
                result.kind = Type_Struct;
                result.data.struct_ = struct_type;
            } else {
                Union_Type union_type;
                union_type.items = items;
                result.kind = Type_Union;
                result.data.union_ = union_type;
            }
        } else if (strcmp(keyword, "enum") == 0) {
            Enum_Type enum_type;
            consume(tokens, &index);

            Array_String items = array_string_new(4);
            while (peek(tokens, index) != Token_RightCurlyBrace) {
                if (peek(tokens, index) == Token_Comma) {
                    consume(tokens, &index);
                    continue;
                }

                char* name = consume_identifier(tokens, &index);
                array_string_append(&items, name);
            }
            enum_type.items = items;
            consume(tokens, &index);

            result.kind = Type_Enum;
            result.data.enum_ = enum_type;
        } else {
            assert(false);
        }
    } else if (peek(tokens, index) == Token_Number) {
        Number_Type number_type;
        char* value_string = consume_number(tokens, &index);
        number_type.value = atoll(value_string);

        result.kind = Type_Number;
        result.data.number = number_type;
    } else {
        char* name = consume_identifier(tokens, &index);
        Internal_Type internal;
        bool found = false;

        if (!found) {
            if (strcmp(name, "@typeof") == 0) {
                TypeOf_Type type_of;

                consume_check(tokens, &index, Token_LeftParenthesis);

                Expression_Node* expression = malloc(sizeof(Expression_Node));
                *expression = parse_expression(tokens, &index);
                type_of.expression = expression;

                consume_check(tokens, &index, Token_RightParenthesis);

                result.kind = Type_TypeOf;
                result.data.type_of = type_of;
                found = true;
            }
        }

        if (!found) {
            if (strcmp(name, "usize") == 0) {
                internal = Type_USize;
                found = true;
            } else if (strcmp(name, "u8") == 0) {
                internal = Type_U8;
                found = true;
            } else if (strcmp(name, "u4") == 0) {
                internal = Type_U4;
                found = true;
            } else if (strcmp(name, "u2") == 0) {
                internal = Type_U2;
                found = true;
            } else if (strcmp(name, "u1") == 0) {
                internal = Type_U1;
                found = true;
            } else if (strcmp(name, "f8") == 0) {
                internal = Type_F8;
                found = true;
            }

            if (found) {
                result.kind = Type_Internal;
                result.data.internal = internal;
            }
        }

        if (!found) {
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

Array_Directive parse_directives(Tokens* tokens, size_t* index_in) {
    Array_Directive directives = array_directive_new(1);
    size_t index = *index_in;

    while (peek(tokens, index) == Token_Identifier && tokens->elements[index].data[0] == '#') {
        Directive_Node directive;
        char* directive_string = consume_identifier(tokens, &index);

        if (strcmp(directive_string, "#if") == 0) {
            Directive_If_Node if_node;
            consume_check(tokens, &index, Token_LeftParenthesis);

            Expression_Node* expression = malloc(sizeof(Expression_Node));
            *expression = parse_expression(tokens, &index);
            if_node.expression = expression;

            consume_check(tokens, &index, Token_RightParenthesis);

            directive.kind = Directive_If;
            directive.data.if_ = if_node;
        }

        array_directive_append(&directives, directive);
    }

    *index_in = index;
    return directives;
}

Statement_Node parse_statement(Tokens* tokens, size_t* index_in) {
    size_t index = *index_in;
    Statement_Node result = {};

    result.directives = parse_directives(tokens, &index);

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
                Statement_Node* statement = malloc(sizeof(Statement_Node));
                *statement = parse_statement(tokens, &index);
                array_statement_node_append(&statements, statement);
            }
            node.statements = statements;
            consume(tokens, &index);

            result.kind = Expression_Block;
            result.data.block = node;
            break;
        }
        case Token_Number: {
            Number_Node node;

            char* string_value = consume_number(tokens, &index);

            if (string_contains(string_value, '.')) {
                double value = strtod(string_value, NULL);
                node.kind = Number_Decimal;
                node.value.decimal = value;
            } else {
                size_t value = strtoul(string_value, NULL, 0);
                node.kind = Number_Integer;
                node.value.integer = value;
            }

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
            Location location = tokens->elements[index].location;
            char* name = consume_identifier(tokens, &index);
            if (strcmp(name, "@sizeof") == 0) {
                SizeOf_Node node;

                consume_check(tokens, &index, Token_LeftParenthesis);

                Type type = parse_type(tokens, &index);
                node.type = type;

                consume_check(tokens, &index, Token_RightParenthesis);

                result.kind = Expression_SizeOf;
                result.data.size_of = node;
            } else if (strcmp(name, "@lengthof") == 0) {
                LengthOf_Node node;

                consume_check(tokens, &index, Token_LeftParenthesis);

                Type type = parse_type(tokens, &index);
                node.type = type;

                consume_check(tokens, &index, Token_RightParenthesis);

                result.kind = Expression_LengthOf;
                result.data.length_of = node;
            } else if (strcmp(name, "@cast") == 0) {
                Cast_Node node;
                node.location = location;

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
                Retrieve_Node node = {};
                node.location = location;
                node.kind = Retrieve_Assign_Identifier;
                Identifier identifier = {};
                identifier.kind = Identifier_Single;
                identifier.data.single = name;

                if (peek(tokens, index) == Token_DoubleColon) {
                    identifier.kind = Identifier_Multi;
                    Array_String names = array_string_new(2);
                    array_string_append(&names, name);
                    while (peek(tokens, index) == Token_DoubleColon) {
                        consume(tokens, &index);

                        array_string_append(&names, consume_identifier(tokens, &index));
                    }
                    identifier.data.multi = names;
                }

                node.kind = Retrieve_Assign_Identifier;
                node.data.identifier = identifier;
                result.kind = Expression_Retrieve;
                result.data.retrieve = node;
            }
            break;
        }
        case Token_DoubleColon: {
            Retrieve_Node node = {};
            node.location = tokens->elements[index].location;
            node.kind = Retrieve_Assign_Identifier;
            Identifier identifier = {};

            identifier.kind = Identifier_Multi;
            Array_String names = array_string_new(2);
            array_string_append(&names, "");
            while (peek(tokens, index) == Token_DoubleColon) {
                consume(tokens, &index);

                array_string_append(&names, consume_identifier(tokens, &index));
            }
            identifier.data.multi = names;

            node.kind = Retrieve_Assign_Identifier;
            node.data.identifier = identifier;
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
            node.kind = Retrieve_Assign_Parent;

            consume(tokens, &index);

            Expression_Node* previous_result = malloc(sizeof(Expression_Node));
            *previous_result = result;
            node.data.parent.expression = previous_result;

            if (peek(tokens, index) == Token_Asterisk) {
                consume(tokens, &index);
                node.data.parent.name = "*";
            } else {
                node.data.parent.name = consume_identifier(tokens, &index);
            }

            result.kind = Expression_Retrieve;
            result.data.retrieve = node;
            continue;
        }

        if (peek(tokens, index) == Token_LeftBracket) {
            running = true;
            Retrieve_Node node;
            node.kind = Retrieve_Assign_Array;
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
                peek(tokens, index) == Token_Percent ||
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
                case Token_Percent:
                    operator = Operator_Modulus;
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
                default:
                    assert(false);
                    break;
            }
            node.data.operator_.operator_ = operator;

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

static size_t module_id;

Item_Node parse_item(Tokens* tokens, size_t* index_in) {
    size_t index = *index_in;
    Item_Node result;

    result.directives = parse_directives(tokens, &index);

    char* keyword = consume_keyword(tokens, &index);
    if (strcmp(keyword, "proc") == 0) {
        char* name = consume_identifier(tokens, &index);
        result.name = name;

        consume_check(tokens, &index, Token_Colon);

        Procedure_Node node;
        switch (consume(tokens, &index)) {
            case Token_LeftParenthesis: {
                Procedure_Literal_Node literal_node;

                Array_Declaration arguments = array_declaration_new(4);
                while (peek(tokens, index) != Token_RightParenthesis) {
                    if (peek(tokens, index) == Token_Comma) {
                        index += 1;
                        continue;
                    }

                    Location location = tokens->elements[index].location;
                    char* name = consume_identifier(tokens, &index);
                    consume_check(tokens, &index, Token_Colon);
                    Type type = parse_type(tokens, &index);
                    array_declaration_append(&arguments, (Declaration) { name, type, location });
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
            }
            default:
                printf("Error: Unexpected token ");
                print_token(&tokens->elements[index - 1], false);
                printf("\n");
                exit(1);
                break;
        }

        result.kind = Item_Procedure;
        result.data.procedure = node;
    } else if (strcmp(keyword, "type") == 0) {
        Type_Node node;

        char* name = consume_identifier(tokens, &index);
        result.name = name;

        consume_check(tokens, &index, Token_Colon);

        node.type = parse_type(tokens, &index);

        consume_check(tokens, &index, Token_Semicolon);

        result.kind = Item_Type;
        result.data.type = node;
    } else if (strcmp(keyword, "mod") == 0) {
        Module_Node node;
        node.items = array_item_node_new(8);
        node.id = module_id;
        module_id++;
        
        result.name = consume_identifier(tokens, &index);
        consume_check(tokens, &index, Token_Colon);
        consume_check(tokens, &index, Token_LeftCurlyBrace);

        while (peek(tokens, index) != Token_RightCurlyBrace) {
            Item_Node item = parse_item(tokens, &index);
            Item_Node* item_allocated = malloc(sizeof(Item_Node));
            *item_allocated = item;
            array_item_node_append(&node.items, item);
        }

        consume(tokens, &index);
        consume_check(tokens, &index, Token_Semicolon);

        result.kind = Item_Module;
        result.data.module = node;
    } else if (strcmp(keyword, "global") == 0) {
        Global_Node node;

        result.name = consume_identifier(tokens, &index);
        consume_check(tokens, &index, Token_Colon);

        node.type = parse_type(tokens, &index);

        consume_check(tokens, &index, Token_Semicolon);

        result.kind = Item_Global;
        result.data.global = node;
    } else if (strcmp(keyword, "use") == 0) {
        Use_Node node = {};

        char* path = consume_string(tokens, &index);
        if (string_contains(path, ':')) {
            int index = string_index(path, ':');
            node.package = string_substring(path, 0, index);
            node.path = string_substring(path, index + 1, strlen(path));
        } else {
            node.path = path;
        }

        result.name = "";

        consume_check(tokens, &index, Token_Semicolon);

        result.kind = Item_Use;
        result.data.use = node;
    } else {
        printf("Error: Unexpected token ");
        print_token(&tokens->elements[index - 1], false);
        printf("\n");
        exit(1);
    }

    *index_in = index;
    return result;
}

static size_t file_id;

File_Node parse(char* path, Tokens* tokens) {
    File_Node result;
    result.path = path;
    result.id = file_id++;

    Array_Item_Node array = array_item_node_new(32);
    size_t index = 0;
    while (index < tokens->count) {
        Item_Node node = parse_item(tokens, &index);
        array_item_node_append(&array, node);
    }

    result.items = array;
    return result;
}
