#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "parser.h"
#include "tokenizer.h"

void print_token_error_stub(Token* token) {
    printf("%s:%zu:%zu: ", token->location.file, token->location.row, token->location.col);
}

Token_Kind peek(Parser_State* state, size_t index) {
    Token* current = &state->tokens->elements[index];
    return current->kind;
}

Token_Kind consume(Parser_State* state, size_t* index) {
    Token* current = &state->tokens->elements[*index];
    (*index)++;
    return current->kind;
}

void consume_check(Parser_State* state, size_t* index, Token_Kind wanted_kind) {
    Token* current = &state->tokens->elements[*index];
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

        print_token_error_stub(current);
        printf("Expected ");
        print_token(&temp, false);
        printf(", got ");
        print_token(current, false);
        printf("\n");
        exit(1);
    }
    (*index)++;
}

char* consume_string(Parser_State* state, size_t* index) {
    Token* current = &state->tokens->elements[*index];
    if (current->kind != Token_String) {
        printf("Error: Expected String, got ");
        print_token(current, false);
        printf("\n");
        exit(1);
    }
    (*index)++;
    return current->data;
}

char* consume_boolean(Parser_State* state, size_t* index) {
    Token* current = &state->tokens->elements[*index];
    if (current->kind != Token_Boolean) {
        printf("Error: Expected Boolean, got ");
        print_token(current, false);
        printf("\n");
        exit(1);
    }
    (*index)++;
    return current->data;
}

char* consume_number(Parser_State* state, size_t* index) {
    Token* current = &state->tokens->elements[*index];
    if (current->kind != Token_Number) {
        printf("Error: Expected Number, got ");
        print_token(current, false);
        printf("\n");
        exit(1);
    }
    (*index)++;
    return current->data;
}

char* consume_keyword(Parser_State* state, size_t* index) {
    Token* current = &state->tokens->elements[*index];
    if (current->kind != Token_Keyword) {
        print_token_error_stub(current);
        printf("Expected Keyword, got ");
        print_token(current, false);
        printf("\n");
        exit(1);
    }
    (*index)++;
    return current->data;
}

char* consume_identifier(Parser_State* state, size_t* index) {
    Token* current = &state->tokens->elements[*index];
    if (current->kind != Token_Identifier) {
        print_token_error_stub(current);
        printf("Expected Identifier, got ");
        print_token(current, false);
        printf("\n");
        exit(1);
    }
    (*index)++;
    return current->data;
}

Type parse_type(Parser_State* state, size_t* index_in);

void parse_directives(Parser_State* state, size_t* index_in) {
    size_t index = *index_in;

    while (peek(state, index) == Token_Identifier && state->tokens->elements[index].data[0] == '#') {
        Directive_Node directive;
        char* directive_string = consume_identifier(state, &index);

        if (strcmp(directive_string, "#if") == 0) {
            Directive_If_Node if_node;
            consume_check(state, &index, Token_LeftParenthesis);

            Expression_Node* expression = malloc(sizeof(Expression_Node));
            *expression = parse_expression(state, &index);
            if_node.expression = expression;

            consume_check(state, &index, Token_RightParenthesis);

            directive.kind = Directive_If;
            directive.data.if_ = if_node;
        }

        array_directive_append(&state->directives, directive);
    }

    *index_in = index;
}

void filter_add_directive(Parser_State* state, Array_Directive* directives, Directive_Kind kind) {
    size_t i = 0;
    while (i < state->directives.count) {
        if (state->directives.elements[i].kind == kind) {
            array_directive_append(directives, state->directives.elements[i]);
            size_t j = i + 1;
            while (j < state->directives.count) {
                state->directives.elements[j - 1] = state->directives.elements[j];
                j++;
            }
            state->directives.count--;
        } else {
            i++;
        }
    }
}

Type parse_type(Parser_State* state, size_t* index_in) {
    size_t index = *index_in;
    Type result = { .directives = array_directive_new(1) };

    parse_directives(state, &index);

    if (peek(state, index) == Token_Asterisk) {
        Pointer_Type pointer;

        consume(state, &index);

        Type child = parse_type(state, &index);
        Type* child_allocated = malloc(sizeof(Type));
        *child_allocated = child;
        pointer.child = child_allocated;

        result.kind = Type_Pointer;
        result.data.pointer = pointer;
    } else if (peek(state, index) == Token_LeftBracket) {
        consume(state, &index);
        
        BArray_Type array = {};

        if (peek(state, index) != Token_RightBracket) {
            array.has_size = true;
            Type* size_type = malloc(sizeof(Type));
            *size_type = parse_type(state, &index);
            array.size_type = size_type;
        }

        consume_check(state, &index, Token_RightBracket);

        Type child = parse_type(state, &index);
        Type* child_allocated = malloc(sizeof(Type));
        *child_allocated = child;
        array.element_type = child_allocated;

        result.kind = Type_Array;
        result.data.array = array;
    } else if (peek(state, index) == Token_Keyword) {
        char* keyword = consume_keyword(state, &index);

        if (strcmp(keyword, "proc") == 0) {
            index += 1;

            Procedure_Type procedure = {
                .arguments = array_type_new(2),
                .returns = array_type_new(1),
            };

            while (peek(state, index) != Token_RightParenthesis) {
                if (peek(state, index) == Token_Comma) {
                    index += 1;
                    continue;
                }

                Type* type = malloc(sizeof(Type));
                *type = parse_type(state, &index);
                array_type_append(&procedure.arguments, type);
            }

            consume(state, &index);

            if (peek(state, index) == Token_Colon) {
                index += 1;

                bool looking_at_returns = true;
                while (looking_at_returns) {
                    Type* type = malloc(sizeof(Type));
                    *type = parse_type(state, &index);
                    array_type_append(&procedure.returns, type);

                    looking_at_returns = peek(state, index) == Token_Comma;
                }
            }

            result.kind = Type_Procedure;
            result.data.procedure = procedure;
        } else if (strcmp(keyword, "struct") == 0 || strcmp(keyword, "union") == 0) {
            bool is_struct = strcmp(keyword, "struct") == 0;
            consume(state, &index);

            Array_Declaration_Pointer items = array_declaration_pointer_new(4);
            while (peek(state, index) != Token_RightCurlyBrace) {
                if (peek(state, index) == Token_Comma) {
                    consume(state, &index);
                    continue;
                }

                Location location = state->tokens->elements[index].location;
                char* name = consume_identifier(state, &index);
                consume_check(state, &index, Token_Colon);
                Type type = parse_type(state, &index);

                Declaration* declaration = malloc(sizeof(Declaration));
                declaration->name = name;
                declaration->type = type;
                declaration->location = location;
                array_declaration_pointer_append(&items, declaration);
            }
            consume(state, &index);

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
            consume(state, &index);

            Array_String items = array_string_new(4);
            while (peek(state, index) != Token_RightCurlyBrace) {
                if (peek(state, index) == Token_Comma) {
                    consume(state, &index);
                    continue;
                }

                char* name = consume_identifier(state, &index);
                array_string_append(&items, name);
            }
            enum_type.items = items;
            consume(state, &index);

            result.kind = Type_Enum;
            result.data.enum_ = enum_type;
        } else {
            assert(false);
        }
    } else if (peek(state, index) == Token_Number) {
        Number_Type number_type;
        char* value_string = consume_number(state, &index);
        number_type.value = atoll(value_string);

        result.kind = Type_Number;
        result.data.number = number_type;
    } else {
        char* name = consume_identifier(state, &index);
        Internal_Type internal;
        bool found = false;

        if (!found) {
            if (strcmp(name, "@typeof") == 0) {
                TypeOf_Type type_of;

                consume_check(state, &index, Token_LeftParenthesis);

                Expression_Node* expression = malloc(sizeof(Expression_Node));
                *expression = parse_expression(state, &index);
                type_of.expression = expression;

                consume_check(state, &index, Token_RightParenthesis);

                result.kind = Type_TypeOf;
                result.data.type_of = type_of;
                found = true;
            }
        }

        if (!found) {
            if (strcmp(name, "usize") == 0) {
                internal = Type_USize;
                found = true;
            } else if (strcmp(name, "u64") == 0) {
                internal = Type_U64;
                found = true;
            } else if (strcmp(name, "u32") == 0) {
                internal = Type_U32;
                found = true;
            } else if (strcmp(name, "u16") == 0) {
                internal = Type_U86;
                found = true;
            } else if (strcmp(name, "u8") == 0) {
                internal = Type_U8;
                found = true;
            } else if (strcmp(name, "f64") == 0) {
                internal = Type_F8;
                found = true;
            } else if (strcmp(name, "ptr") == 0) {
                internal = Type_Ptr;
                found = true;
            } else if (strcmp(name, "bool") == 0) {
                internal = Type_Bool;
                found = true;
            }

            if (found) {
                result.kind = Type_Internal;
                result.data.internal = internal;
            }
        }

        if (!found) {
            Basic_Type basic = {};
            basic.kind = Type_Single;

            basic.data.single = name;

            if (peek(state, index) == Token_DoubleColon) {
                Array_String names = array_string_new(2);
                array_string_append(&names, basic.data.single);
                while (peek(state, index) == Token_DoubleColon) {
                    consume(state, &index);
                    array_string_append(&names, consume_identifier(state, &index));
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

Expression_Node parse_multi_expression(Parser_State* state, size_t* index_in) {
    size_t index = *index_in;

    Expression_Node result = parse_expression(state, &index);

    if (peek(state, index) == Token_Comma) {
        consume(state, &index);
        if (result.kind == Expression_Multi) {
            Expression_Node* next = malloc(sizeof(Expression_Node));
            *next = parse_expression(state, &index);
            array_expression_node_append(&result.data.multi.expressions, next);
        } else {
            Multi_Expression_Node node;
            node.expressions = array_expression_node_new(2);

            Expression_Node* previous_expression = malloc(sizeof(Expression_Node));
            *previous_expression = result;
            array_expression_node_append(&node.expressions, previous_expression);

            Expression_Node next = parse_expression(state, &index);
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
    }


    *index_in = index;

    return result;
};

Statement_Node parse_statement(Parser_State* state, size_t* index_in) {
    size_t index = *index_in;
    Statement_Node result = { .directives = array_directive_new(1) };

    parse_directives(state, &index);
    filter_add_directive(state, &result.directives, Directive_If);

    Token_Kind token = peek(state, index);
    if (token == Token_Keyword && strcmp(state->tokens->elements[index].data, "var") == 0) {
        Statement_Declare_Node node = {};

        consume(state, &index);

        Array_Declaration declarations = array_declaration_new(8);
        while (peek(state, index) != Token_Equals && peek(state, index) != Token_Semicolon) {
            if (peek(state, index) == Token_Comma) {
                consume(state, &index);
                continue;
            }
            Declaration declaration;
            declaration.location = state->tokens->elements[index].location;
            declaration.name = consume_identifier(state, &index);
            consume_check(state, &index, Token_Colon);
            declaration.type = parse_type(state, &index);
            array_declaration_append(&declarations, declaration);
        }
        node.declarations = declarations;

        Token_Kind next = consume(state, &index);
        if (next == Token_Equals) {
            Expression_Node* expression = malloc(sizeof(Expression_Node));
            *expression = parse_multi_expression(state, &index);
            node.expression = expression;

            consume_check(state, &index, Token_Semicolon);
        } else if (next == Token_Semicolon) {
        } else {
            print_token_error_stub(&state->tokens->elements[index - 1]);
            printf("Unexpected token ");
            print_token(&state->tokens->elements[index - 1], false);
            printf("\n");
            exit(1);
        }

        result.kind = Statement_Declare;
        result.data.declare = node;
    } else if (token == Token_Keyword && strcmp(state->tokens->elements[index].data, "return") == 0) {
        Statement_Return_Node node;
        node.location = state->tokens->elements[index].location;

        consume(state, &index);

        Expression_Node expression = parse_expression(state, &index);
        Expression_Node* expression_allocated = malloc(sizeof(Expression_Node));
        *expression_allocated = expression;
        node.expression = expression_allocated;

        consume_check(state, &index, Token_Semicolon);

        result.kind = Statement_Return;
        result.data.return_ = node;
    } else {
        Statement_Expression_Node node;

        Expression_Node* expression = malloc(sizeof(Expression_Node));
        *expression = parse_multi_expression(state, &index);
        node.expression = expression;

        Token_Kind token = consume(state, &index);
        switch (token) {
            case Token_Equals: {
                Statement_Assign_Node assign = {};
                assign.parts = array_statement_assign_part_new(2);

                if (expression->kind == Expression_Retrieve) {
                    Statement_Assign_Part assign_part;
                    assign_part = expression->data.retrieve;
                    array_statement_assign_part_append(&assign.parts, assign_part);
                } else if (expression->kind == Expression_Multi) {
                    Multi_Expression_Node* multi = &expression->data.multi;
                    for (size_t i = 0; i < multi->expressions.count; i++) {
                        Expression_Node* expression = multi->expressions.elements[i];
                        Statement_Assign_Part assign_part;
                        assign_part = expression->data.retrieve;
                        array_statement_assign_part_append(&assign.parts, assign_part);
                    }
                }

                Expression_Node* expression = malloc(sizeof(Expression_Node));
                *expression = parse_multi_expression(state, &index);
                assign.expression = expression;

                result.kind = Statement_Assign;
                result.data.assign = assign;

                consume_check(state, &index, Token_Semicolon);
                break;
            }
            case Token_Semicolon: {
                result.statement_end_location = state->tokens->elements[index - 1].location;
                result.kind = Statement_Expression;
                result.data.expression = node;
                break;
            }
            default: {
                print_token_error_stub(&state->tokens->elements[index - 1]);
                printf("Unexpected token ");
                print_token(&state->tokens->elements[index - 1], false);
                printf("\n");
                exit(1);
                break;
            }
        }
    }

    *index_in = index;
    return result;
}

int get_precedence(Parser_State* state, size_t index) {
    if (peek(state, index) == Token_Plus ||
            peek(state, index) == Token_Minus ||
            peek(state, index) == Token_Percent) {
        return 2;
    }
    if (peek(state, index) == Token_Asterisk ||
            peek(state, index) == Token_Slash) {
        return 3;
    }
    if (peek(state, index) == Token_DoubleEquals ||
            peek(state, index) == Token_ExclamationEquals ||
            peek(state, index) == Token_GreaterThan ||
            peek(state, index) == Token_LessThan ||
            peek(state, index) == Token_LessThanEqual ||
            peek(state, index) == Token_GreaterThanEqual) {
        return 1;
    }
    return -1;
}

Expression_Node parse_expression_without_operators(Parser_State* state, size_t* index_in) {
    size_t index = *index_in;
    Expression_Node result = { .directives = array_directive_new(1) };

    parse_directives(state, &index);

    switch (peek(state, index)) {
        case Token_LeftCurlyBrace: {
            Block_Node node;
            consume(state, &index);

            Array_Statement_Node statements = array_statement_node_new(32);

            while (peek(state, index) != Token_RightCurlyBrace) {
                Statement_Node* statement = malloc(sizeof(Statement_Node));
                *statement = parse_statement(state, &index);
                array_statement_node_append(&statements, statement);
            }
            node.statements = statements;
            consume(state, &index);

            result.kind = Expression_Block;
            result.data.block = node;
            break;
        }
        case Token_Number: {
            Number_Node node = {};

            char* string_value = consume_number(state, &index);

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

            node.value = consume_string(state, &index);

            result.kind = Expression_String;
            result.data.string = node;
            break;
        }
        case Token_Boolean: {
            Boolean_Node node;

            node.value = strcmp(consume_boolean(state, &index), "true") == 0;

            result.kind = Expression_Boolean;
            result.data.boolean = node;
            break;
        }
        case Token_Identifier: {
            Location location = state->tokens->elements[index].location;
            char* name = consume_identifier(state, &index);
            if (strcmp(name, "@sizeof") == 0) {
                SizeOf_Node node;

                consume_check(state, &index, Token_LeftParenthesis);

                Type type = parse_type(state, &index);
                node.type = type;

                consume_check(state, &index, Token_RightParenthesis);

                result.kind = Expression_SizeOf;
                result.data.size_of = node;
            } else if (strcmp(name, "@lengthof") == 0) {
                LengthOf_Node node;

                consume_check(state, &index, Token_LeftParenthesis);

                Type type = parse_type(state, &index);
                node.type = type;

                consume_check(state, &index, Token_RightParenthesis);

                result.kind = Expression_LengthOf;
                result.data.length_of = node;
            } else if (strcmp(name, "@cast") == 0) {
                Cast_Node node;
                node.location = location;

                consume_check(state, &index, Token_LeftParenthesis);

                Type type = parse_type(state, &index);
                node.type = type;

                consume_check(state, &index, Token_Comma);

                Expression_Node inner = parse_expression(state, &index);
                Expression_Node* inner_allocated = malloc(sizeof(Expression_Node));
                *inner_allocated = inner;
                node.expression = inner_allocated;

                consume_check(state, &index, Token_RightParenthesis);

                result.kind = Expression_Cast;
                result.data.cast = node;
                break;
            } else if (strcmp(name, "@init") == 0) {
                Init_Node node;
                node.location = location;

                consume_check(state, &index, Token_LeftParenthesis);

                Type type = parse_type(state, &index);
                node.type = type;

                consume_check(state, &index, Token_RightParenthesis);

                result.kind = Expression_Init;
                result.data.init = node;
                break;
            } else if (strcmp(name, "@build") == 0) {
                Build_Node node;
                node.location = location;

                consume_check(state, &index, Token_LeftParenthesis);

                Type type = parse_type(state, &index);
                node.type = type;

                Array_Expression_Node arguments = array_expression_node_new(32);

                while (peek(state, index) != Token_RightParenthesis) {
                    if (peek(state, index) == Token_Comma) {
                        index++;
                        continue;
                    }

                    Expression_Node* expression = malloc(sizeof(Expression_Node));
                    *expression = parse_expression(state, &index);

                    array_expression_node_append(&arguments, expression);
                }

                node.arguments = arguments;

                consume_check(state, &index, Token_RightParenthesis);

                result.kind = Expression_Build;
                result.data.build = node;
                break;
            } else {
                Retrieve_Node node = {};
                node.location = location;
                node.kind = Retrieve_Assign_Identifier;
                Identifier identifier = {};
                identifier.kind = Identifier_Single;
                identifier.data.single = name;

                if (peek(state, index) == Token_DoubleColon) {
                    identifier.kind = Identifier_Multi;
                    Array_String names = array_string_new(2);
                    array_string_append(&names, name);
                    while (peek(state, index) == Token_DoubleColon) {
                        consume(state, &index);

                        array_string_append(&names, consume_identifier(state, &index));
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
        case Token_DoublePeriod: {
            consume(state, &index);

            Retrieve_Node node = {};
            node.kind = Retrieve_Assign_Identifier;
            Identifier identifier = {};
            identifier.kind = Identifier_Single;
            identifier.data.single = "..";

            node.kind = Retrieve_Assign_Identifier;
            node.data.identifier = identifier;
            result.kind = Expression_Retrieve;
            result.data.retrieve = node;
            break;
        }
        case Token_DoubleColon: {
            Retrieve_Node node = {};
            node.location = state->tokens->elements[index].location;
            node.kind = Retrieve_Assign_Identifier;
            Identifier identifier = {};

            identifier.kind = Identifier_Multi;
            Array_String names = array_string_new(2);
            array_string_append(&names, "");
            while (peek(state, index) == Token_DoubleColon) {
                consume(state, &index);

                array_string_append(&names, consume_identifier(state, &index));
            }
            identifier.data.multi = names;

            node.kind = Retrieve_Assign_Identifier;
            node.data.identifier = identifier;
            result.kind = Expression_Retrieve;
            result.data.retrieve = node;
            break;
        }
        case Token_Exclamation: {
            Invoke_Node node = { .arguments = array_expression_node_new(1) };
            node.kind = Invoke_Operator;
            node.location = state->tokens->elements[index].location;

            node.data.operator_.operator_ = Operator_Not;

            consume(state, &index);

            Expression_Node* expression = malloc(sizeof(Expression_Node));
            *expression = parse_expression(state, &index);
            array_expression_node_append(&node.arguments, expression);

            result.kind = Expression_Invoke;
            result.data.invoke = node;
            break;
        }
        case Token_Keyword: {
            char* name = consume_keyword(state, &index);

            if (strcmp(name, "if") == 0) {
                If_Node node = {};

                Expression_Node condition = parse_expression(state, &index);
                Expression_Node* condition_allocated = malloc(sizeof(Expression_Node));
                *condition_allocated = condition;
                node.condition = condition_allocated;

                Expression_Node* expression = malloc(sizeof(Expression_Node));
                *expression = parse_expression(state, &index);
                node.if_expression = expression;

                if (state->tokens->elements[index].kind == Token_Keyword && strcmp(state->tokens->elements[index].data, "else") == 0) {
                    consume(state, &index);
                    Expression_Node* expression = malloc(sizeof(Expression_Node));
                    *expression = parse_expression(state, &index);
                    node.else_expression = expression;
                }

                result.kind = Expression_If;
                result.data.if_ = node;
            } else if (strcmp(name, "while") == 0) {
                While_Node node;

                Expression_Node condition = parse_expression(state, &index);
                Expression_Node* condition_allocated = malloc(sizeof(Expression_Node));
                *condition_allocated = condition;
                node.condition = condition_allocated;

                Expression_Node inside = parse_expression(state, &index);
                Expression_Node* inside_allocated = malloc(sizeof(Expression_Node));
                *inside_allocated = inside;
                node.inside = inside_allocated;

                result.kind = Expression_While;
                result.data.while_ = node;
            } else {
                print_token_error_stub(&state->tokens->elements[index - 1]);
                printf("Unexpected token ");
                print_token(&state->tokens->elements[index - 1], false);
                printf("\n");
                exit(1);
            }
            break;
        }
        case Token_Ampersand: {
            Reference_Node node;

            consume(state, &index);
            
            Expression_Node inner = parse_expression(state, &index);
            Expression_Node* inner_allocated = malloc(sizeof(Expression_Node));
            *inner_allocated = inner;
            node.inner = inner_allocated;

            result.kind = Expression_Reference;
            result.data.reference = node;
            break;
        }
        case Token_LeftParenthesis: {
            consume(state, &index);

            result = parse_expression(state, &index);

            consume_check(state, &index, Token_RightParenthesis);
            break;
        }
        default: {
            print_token_error_stub(&state->tokens->elements[index - 1]);
            printf("Unexpected token ");
            print_token(&state->tokens->elements[index], false);
            printf("\n");
            exit(1);
        }
    }

    bool running = true;
    while (running) {
        running = false;
        if (peek(state, index) == Token_Period) {
            running = true;
            Retrieve_Node node;
            node.kind = Retrieve_Assign_Parent;

            consume(state, &index);

            Expression_Node* previous_result = malloc(sizeof(Expression_Node));
            *previous_result = result;
            node.data.parent.expression = previous_result;

            if (peek(state, index) == Token_Asterisk) {
                consume(state, &index);
                node.data.parent.name = "*";
            } else if (peek(state, index) == Token_QuestionMark) {
                consume(state, &index);
                node.data.parent.name = "?";
            } else if (peek(state, index) == Token_DoubleQuestionMark) {
                consume(state, &index);
                node.data.parent.name = "??";
            } else {
                node.data.parent.name = consume_identifier(state, &index);
            }

            result.kind = Expression_Retrieve;
            result.data.retrieve = node;
            continue;
        }

        if (peek(state, index) == Token_LeftBracket) {
            running = true;
            Retrieve_Node node;
            node.kind = Retrieve_Assign_Array;
            node.location = state->tokens->elements[index].location;

            Expression_Node* previous_result = malloc(sizeof(Expression_Node));
            *previous_result = result;
            node.data.array.expression_outer = previous_result;

            consume(state, &index);

            Expression_Node inner = parse_expression(state, &index);
            Expression_Node* inner_allocated = malloc(sizeof(Expression_Node));
            *inner_allocated = inner;
            node.data.array.expression_inner = inner_allocated;

            consume_check(state, &index, Token_RightBracket);

            result.kind = Expression_Retrieve;
            result.data.retrieve = node;
            continue;
        }

        if (peek(state, index) == Token_LeftParenthesis) {
            running = true;
            Invoke_Node node;
            node.location = state->tokens->elements[index].location;
            node.kind = Invoke_Standard;

            Expression_Node* previous_result = malloc(sizeof(Expression_Node));
            *previous_result = result;
            node.data.procedure = previous_result;
            consume(state, &index);

            Array_Expression_Node arguments = array_expression_node_new(32);

            while (peek(state, index) != Token_RightParenthesis) {
                if (peek(state, index) == Token_Comma) {
                    index++;
                    continue;
                }

                Expression_Node* expression = malloc(sizeof(Expression_Node));
                *expression = parse_expression(state, &index);

                array_expression_node_append(&arguments, expression);
            }

            node.arguments = arguments;

            consume(state, &index);

            result.kind = Expression_Invoke;
            result.data.invoke = node;
            continue;
        }

        if (peek(state, index) == Token_Exclamation) {
            running = true;
            Run_Macro_Node node = { .arguments = array_macro_syntax_data_new(2) };
            assert(result.kind == Expression_Retrieve && result.data.retrieve.kind == Retrieve_Assign_Identifier);
            node.identifier = result.data.retrieve.data.identifier;
            node.location = state->tokens->elements[index].location;

            consume(state, &index);
            consume_check(state, &index, Token_LeftParenthesis);

            while (peek(state, index) != Token_RightParenthesis) {
                if (peek(state, index) == Token_Comma) {
                    index++;
                    continue;
                }

                Macro_Syntax_Data* data = malloc(sizeof(Macro_Syntax_Data));;
                char* kind = consume_identifier(state, &index);
                if (strcmp(kind, "$expr") == 0) {
                    Expression_Node* expression = malloc(sizeof(Expression_Node));
                    *expression = parse_expression(state, &index);

                    data->kind = Macro_Expression;
                    data->data.expression = expression;
                } else {
                    assert(false);
                }

                array_macro_syntax_data_append(&node.arguments, data);
            }

            consume(state, &index);

            result.kind = Expression_RunMacro;
            result.data.run_macro = node;
            continue;
        }
    }

    *index_in = index;
    return result;
}

Expression_Node parse_expression(Parser_State* state, size_t* index_in) {
    size_t index = *index_in;
    Expression_Node result = parse_expression_without_operators(state, &index);

    if (get_precedence(state, index) > 0) {
        int previous_precedence = INT_MAX;
        while (get_precedence(state, index) > 0) {
            int current_precedence = get_precedence(state, index);

            Invoke_Node node;
            node.kind = Invoke_Operator;
            node.location = state->tokens->elements[index].location;

            Operator operator;
            switch (consume(state, &index)) {
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

            Expression_Node* parsed = malloc(sizeof(Expression_Node));
            *parsed = parse_expression_without_operators(state, &index);

            if (current_precedence > previous_precedence) {
                Expression_Node* result_inner = malloc(sizeof(Expression_Node));

                Array_Expression_Node arguments = array_expression_node_new(32);
                array_expression_node_append(&arguments, array_expression_node_get(&result.data.invoke.arguments, 1));
                array_expression_node_append(&arguments, parsed);
                node.arguments = arguments;

                result_inner->kind = Expression_Invoke;
                result_inner->data.invoke = node;

                array_expression_node_set(&result.data.invoke.arguments, 1, result_inner);
            } else {
                Expression_Node* result_allocated = malloc(sizeof(Expression_Node));
                *result_allocated = result;

                Array_Expression_Node arguments = array_expression_node_new(32);
                array_expression_node_append(&arguments, result_allocated);
                array_expression_node_append(&arguments, parsed);
                node.arguments = arguments;

                result.kind = Expression_Invoke;
                result.data.invoke = node;
            }

            previous_precedence = current_precedence;
        }
    }

    *index_in = index;
    return result;
}

Macro_Syntax_Kind parse_macro_syntax_kind(Parser_State* state, size_t* index) {
    char* name = consume_identifier(state, index);
    if (strcmp(name, "$expr") == 0) {
        return Macro_Expression;
    } else {
        assert(false);
    }
}

Item_Node parse_item(Parser_State* state, size_t* index_in) {
    size_t index = *index_in;
    Item_Node result = { .directives = array_directive_new(1) };

    parse_directives(state, &index);
    filter_add_directive(state, &result.directives, Directive_If);

    char* keyword = consume_keyword(state, &index);
    if (strcmp(keyword, "proc") == 0) {
        char* name = consume_identifier(state, &index);
        result.name = name;

        Procedure_Node node;
        consume_check(state, &index, Token_LeftParenthesis);

        Array_Declaration arguments = array_declaration_new(4);
        while (peek(state, index) != Token_RightParenthesis) {
            if (peek(state, index) == Token_Comma) {
                index += 1;
                continue;
            }

            Location location = state->tokens->elements[index].location;
            char* name = consume_identifier(state, &index);
            consume_check(state, &index, Token_Colon);
            Type type = parse_type(state, &index);
            array_declaration_append(&arguments, (Declaration) { name, type, location });
        }
        node.arguments = arguments;
        consume_check(state, &index, Token_RightParenthesis);

        node.returns = array_type_new(1);
        if (peek(state, index) == Token_Colon) {
            consume(state, &index);

            bool running = true;
            while (running) {
                Type type = parse_type(state, &index);
                Type* type_allocated = malloc(sizeof(Type));
                *type_allocated = type;

                array_type_append(&node.returns, type_allocated);
                running = false;
                if (peek(state, index) == Token_Comma) {
                    running = true;
                    consume(state, &index);
                }
            }
        }

        Expression_Node body = parse_expression(state, &index);
        Expression_Node* body_allocated = malloc(sizeof(Expression_Node));
        *body_allocated = body;
        node.body = body_allocated;

        result.kind = Item_Procedure;
        result.data.procedure = node;
    } else if (strcmp(keyword, "macro") == 0) {
        char* name = consume_identifier(state, &index);
        result.name = name;

        consume_check(state, &index, Token_Exclamation);

        Macro_Node node = { .arguments = array_macro_syntax_argument_new(2), .variants = array_macro_variant_new(2) };
        consume_check(state, &index, Token_LeftParenthesis);

        while (peek(state, index) != Token_RightParenthesis) {
            if (peek(state, index) == Token_Comma) {
                index += 1;
                continue;
            }

            Macro_Syntax_Argument argument = {};
            argument.kind = parse_macro_syntax_kind(state, &index);

            if (peek(state, index) == Token_DoublePeriod) {
                consume(state, &index);
                argument.repeat = true;
            }

            array_macro_syntax_argument_append(&node.arguments, argument);
        }

        consume(state, &index);
        consume_check(state, &index, Token_Colon);

        node.return_ = parse_macro_syntax_kind(state, &index);

        consume_check(state, &index, Token_LeftCurlyBrace);

        while (peek(state, index) != Token_RightCurlyBrace) {
            if (peek(state, index) == Token_Comma) {
                consume(state, &index);
            }

            Macro_Variant variant = { .bindings = array_string_new(2) };
            consume_check(state, &index, Token_LeftParenthesis);

            while (peek(state, index) != Token_RightParenthesis) {
                if (peek(state, index) == Token_Comma) {
                    consume(state, &index);
                    continue;
                }

                if (peek(state, index) == Token_DoublePeriod) {
                    array_string_append(&variant.bindings, "..");
                    consume(state, &index);
                } else {
                    array_string_append(&variant.bindings, consume_identifier(state, &index));
                }
            }

            consume(state, &index);

            if (node.return_ == Macro_Expression) {
                Expression_Node* node = malloc(sizeof(Expression_Node));
                *node = parse_expression(state, &index);
                variant.data.kind = Macro_Expression;
                variant.data.data.expression = node;

            } else {
                assert(false);
            }

            array_macro_variant_append(&node.variants, variant);
        }

        consume(state, &index);

        result.kind = Item_Macro;
        result.data.macro = node;
    } else if (strcmp(keyword, "type") == 0) {
        Type_Node node;

        char* name = consume_identifier(state, &index);
        result.name = name;

        consume_check(state, &index, Token_Colon);

        node.type = parse_type(state, &index);

        result.kind = Item_Type;
        result.data.type = node;
    } else if (strcmp(keyword, "global") == 0) {
        Global_Node node;

        result.name = consume_identifier(state, &index);
        consume_check(state, &index, Token_Colon);

        node.type = parse_type(state, &index);

        result.kind = Item_Global;
        result.data.global = node;
    } else if (strcmp(keyword, "const") == 0) {
        Constant_Node node;

        result.name = consume_identifier(state, &index);
        consume_check(state, &index, Token_Colon);

        Expression_Node expression = parse_expression(state, &index);
        assert(expression.kind == Expression_Number);

        node.expression = expression.data.number;

        result.kind = Item_Constant;
        result.data.constant = node;
    } else if (strcmp(keyword, "use") == 0) {
        Use_Node node = {};

        char* path = consume_string(state, &index);
        if (string_contains(path, ':')) {
            int index = string_index(path, ':');
            node.package = string_substring(path, 0, index);
            node.path = string_substring(path, index + 1, strlen(path));
        } else {
            node.path = path;
        }

        result.name = "";

        result.kind = Item_Use;
        result.data.use = node;
    } else {
        print_token_error_stub(&state->tokens->elements[index - 1]);
        printf("Unexpected token ");
        print_token(&state->tokens->elements[index - 1], false);
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

    Parser_State state = { .tokens = tokens, .directives = array_directive_new(4) };

    Array_Item_Node array = array_item_node_new(32);
    size_t index = 0;
    while (index < tokens->count) {
        Item_Node node = parse_item(&state, &index);
        array_item_node_append(&array, node);
    }

    result.items = array;
    return result;
}
