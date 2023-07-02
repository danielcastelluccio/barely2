#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "tokenizer.h"
#include "string_util.h"
#include "dynamic_array.h"

Dynamic_Array_Impl(Token, Tokens, tokens_)

bool is_keyword(char* buffer) {
    if (strcmp(buffer, "proc") == 0) {
        return true;
    } else if (strcmp(buffer, "type") == 0) {
        return true;
    } else if (strcmp(buffer, "var") == 0) {
        return true;
    } else if (strcmp(buffer, "if") == 0) {
        return true;
    } else if (strcmp(buffer, "else") == 0) {
        return true;
    } else if (strcmp(buffer, "while") == 0) {
        return true;
    }

    return false;
}

void check_append_string_token(Tokens* tokens, String_Buffer* buffer) {
    char* buffer_contents = buffer->elements;
    if (strlen(buffer_contents) == 0) {
        return;
    }

    Token_Kind kind;
    if (is_keyword(buffer_contents)) {
        kind = Token_Keyword;
    } else if (buffer_contents[0] >= '0' && buffer_contents[0] <= '9') {
        kind = Token_Number;
    } else {
        kind = Token_Identifier;
    }

    tokens_append(tokens, (Token) { kind, copy_string(buffer_contents) });
    stringbuffer_clear(buffer);
}

void print_token(Token* token, bool newline) {
    switch (token->kind) {
        case Token_LeftParenthesis:
            printf("LeftParenthesis");
            break;
        case Token_RightParenthesis:
            printf("RightParenthesis");
            break;
        case Token_Colon:
            printf("Colon");
            break;
        case Token_DoubleColon:
            printf("DoubleColon");
            break;
        case Token_Semicolon:
            printf("Semicolon");
            break;
        case Token_Comma:
            printf("Comma");
            break;
        case Token_Period:
            printf("Period");
            break;
        case Token_Equals:
            printf("Equals");
            break;
        case Token_DoubleEquals:
            printf("DoubleEquals");
            break;
        case Token_GreaterThan:
            printf("GreaterThan");
            break;
        case Token_LessThan:
            printf("LessThan");
            break;
        case Token_GreaterThanEqual:
            printf("GreaterThanEqual");
            break;
        case Token_LessThanEqual:
            printf("LessThanEqual");
            break;
        case Token_LeftCurlyBrace:
            printf("LeftCurlyBrace");
            break;
        case Token_RightCurlyBrace:
            printf("RightCurlyBrace");
            break;
        case Token_NewLine:
            printf("NewLine");
            break;
        case Token_Space:
            printf("Space");
            break;
        case Token_Identifier:
            printf("Identifier { \"%s\" }", token->data);
            break;
        case Token_Keyword:
            printf("Keyword { \"%s\" }", token->data);
            break;
        case Token_Number:
            printf("Number { \"%s\" }", token->data);
            break;
        case Token_String:
            printf("String { \"%s\" }", token->data);
            break;
        default:
            printf("Unknown");
            break;

    }

    if (newline) {
        printf("\n");
    }
}

#define INITIAL_CAPACITY 512

Tokens tokenize(char* contents) {
    Tokens tokens = {
        (Token*) malloc(sizeof(Token) * INITIAL_CAPACITY),
        0,
        INITIAL_CAPACITY
    };

    String_Buffer buffer = stringbuffer_new(32);

    bool in_string = false;
    size_t length = strlen(contents);
    size_t i = 0;
    while (i < length) {
        char character = contents[i];

        if (in_string) {
            switch (character) {
                case '"':
                    tokens_append(&tokens, (Token) { Token_String, copy_string(buffer.elements) });
                    stringbuffer_clear(&buffer);
                    in_string = false;
                    i++;
                    break;
                default:
                    i++;
                    stringbuffer_append(&buffer, character);
            }
        } else {
            switch (character) {
                case '(':
                    check_append_string_token(&tokens, &buffer);
                    tokens_append(&tokens, (Token) { Token_LeftParenthesis });
                    i++;
                    break;
                case ')':
                    check_append_string_token(&tokens, &buffer);
                    tokens_append(&tokens, (Token) { Token_RightParenthesis });
                    i++;
                    break;
                case ':':
                    check_append_string_token(&tokens, &buffer);
                    if (contents[i + 1] == ':') {
                        tokens_append(&tokens, (Token) { Token_DoubleColon });
                        i += 2;
                    } else {
                        tokens_append(&tokens, (Token) { Token_Colon });
                        i++;
                    }
                    break;
                case ';':
                    check_append_string_token(&tokens, &buffer);
                    tokens_append(&tokens, (Token) { Token_Semicolon });
                    i++;
                    break;
                case ',':
                    check_append_string_token(&tokens, &buffer);
                    tokens_append(&tokens, (Token) { Token_Comma });
                    i++;
                    break;
                case '.':
                    check_append_string_token(&tokens, &buffer);
                    tokens_append(&tokens, (Token) { Token_Period });
                    i++;
                    break;
                case '=':
                    check_append_string_token(&tokens, &buffer);
                    if (contents[i + 1] == '=') {
                        tokens_append(&tokens, (Token) { Token_DoubleEquals });
                        i += 2;
                    } else {
                        tokens_append(&tokens, (Token) { Token_Equals });
                        i++;
                    }
                    break;
                case '>':
                    check_append_string_token(&tokens, &buffer);
                    if (contents[i + 1] == '=') {
                        tokens_append(&tokens, (Token) { Token_GreaterThanEqual });
                        i += 2;
                    } else {
                        tokens_append(&tokens, (Token) { Token_GreaterThan });
                        i++;
                    }
                    break;
                case '<':
                    check_append_string_token(&tokens, &buffer);
                    if (contents[i + 1] == '=') {
                        tokens_append(&tokens, (Token) { Token_LessThanEqual });
                        i += 2;
                    } else {
                        tokens_append(&tokens, (Token) { Token_LessThan });
                        i++;
                    }
                    i++;
                    break;
                case '+':
                    check_append_string_token(&tokens, &buffer);
                    tokens_append(&tokens, (Token) { Token_Plus });
                    i++;
                    break;
                case '-':
                    check_append_string_token(&tokens, &buffer);
                    tokens_append(&tokens, (Token) { Token_Minus });
                    i++;
                    break;
                case '*':
                    check_append_string_token(&tokens, &buffer);
                    tokens_append(&tokens, (Token) { Token_Asterisk });
                    i++;
                    break;
                case '/':
                    check_append_string_token(&tokens, &buffer);
                    tokens_append(&tokens, (Token) { Token_Slash });
                    i++;
                    break;
                case '{':
                    check_append_string_token(&tokens, &buffer);
                    tokens_append(&tokens, (Token) { Token_LeftCurlyBrace });
                    i++;
                    break;
                case '}':
                    check_append_string_token(&tokens, &buffer);
                    tokens_append(&tokens, (Token) { Token_RightCurlyBrace });
                    i++;
                    break;
                case '"':
                    check_append_string_token(&tokens, &buffer);
                    in_string = true;
                    i++;
                    break;
                case ' ':
                    check_append_string_token(&tokens, &buffer);
                    //tokens_append(&tokens, (Token) { Space });
                    i++;
                    break;
                case '\n':
                    check_append_string_token(&tokens, &buffer);
                    //tokens_append(&tokens, (Token) { NewLine });
                    i++;
                    break;
                default:
                    stringbuffer_append(&buffer, character);
                    i++;
                    break;
            }
        }
    }

    stringbuffer_free(&buffer);
    return tokens;
}
