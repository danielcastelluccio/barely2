#include <assert.h>
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
    } else if (strcmp(buffer, "macro") == 0) {
        return true;
    } else if (strcmp(buffer, "type") == 0) {
        return true;
    } else if (strcmp(buffer, "struct") == 0) {
        return true;
    } else if (strcmp(buffer, "union") == 0) {
        return true;
    } else if (strcmp(buffer, "enum") == 0) {
        return true;
    } else if (strcmp(buffer, "mod") == 0) {
        return true;
    } else if (strcmp(buffer, "global") == 0) {
        return true;
    } else if (strcmp(buffer, "const") == 0) {
        return true;
    } else if (strcmp(buffer, "var") == 0) {
        return true;
    } else if (strcmp(buffer, "if") == 0) {
        return true;
    } else if (strcmp(buffer, "else") == 0) {
        return true;
    } else if (strcmp(buffer, "while") == 0) {
        return true;
    } else if (strcmp(buffer, "return") == 0) {
        return true;
    }

    return false;
}

void print_token(Token* token, bool newline) {
    switch (token->kind) {
        case Token_LeftParenthesis:
            printf("LeftParenthesis");
            break;
        case Token_RightParenthesis:
            printf("RightParenthesis");
            break;
        case Token_LeftBracket:
            printf("LeftBracket");
            break;
        case Token_RightBracket:
            printf("RightBracket");
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
        case Token_DoublePeriod:
            printf("DoublePeriod");
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
        case Token_QuestionMark:
            printf("QuestionMark");
            break;
        case Token_DoubleQuestionMark:
            printf("DoubleQuestionMark");
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
        case Token_Null:
            printf("Null");
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

#define LOCATION(file, row, col) (Location) { file, row, col }

void check_append_string_token(Tokens* tokens, String_Buffer* buffer, char* file, size_t* row, size_t* col) {
    char* buffer_contents = buffer->elements;
    if (strlen(buffer_contents) == 0) {
        return;
    }

    Token_Kind kind;
    if (is_keyword(buffer_contents)) {
        kind = Token_Keyword;
    } else if (buffer_contents[0] >= '0' && buffer_contents[0] <= '9') {
        kind = Token_Number;
    } else if (strcmp(buffer_contents, "true") == 0 || strcmp(buffer_contents, "false") == 0) {
        kind = Token_Boolean;
    } else if (strcmp(buffer_contents, "null") == 0) {
        kind = Token_Null;
    } else {
        kind = Token_Identifier;
    }

    tokens_append(tokens, (Token) { kind, copy_string(buffer_contents), LOCATION(file, *row, *col) });
    *col += buffer->count;
    stringbuffer_clear(buffer);
}


#define INITIAL_CAPACITY 512

Tokens tokenize(char* file, char* contents) {
    Tokens tokens = {
        (Token*) malloc(sizeof(Token) * INITIAL_CAPACITY),
        0,
        INITIAL_CAPACITY
    };

    String_Buffer buffer = stringbuffer_new(32);

    size_t row = 1;
    size_t col = 1;

    size_t cached_i = 0;

    bool in_string = false;
    bool in_comment = false;
    size_t length = strlen(contents);
    size_t i = 0;
    while (i < length) {
        char character = contents[i];

        if (in_comment) {
            if (character == '\n') {
                in_comment = false;
                col = 1;
                row++;
            }
            i++;
        } else if (in_string) {
            switch (character) {
                case '"':
                    tokens_append(&tokens, (Token) { Token_String, copy_string(buffer.elements), LOCATION(file, row, col) });
                    stringbuffer_clear(&buffer);
                    in_string = false;
                    i++;
                    col += (i - cached_i);
                    break;
                default:
                    if (character == '\\') {
                        switch (contents[i + 1]) {
                            case 'n':
                                stringbuffer_append(&buffer, '\n');
                                break;
                            case '"':
                                stringbuffer_append(&buffer, '"');
                                break;
                            default:
                                assert(false);
                        }
                        i += 2;
                    } else {
                        stringbuffer_append(&buffer, character);
                        i++;
                    }
            }
        } else {
            switch (character) {
                case '(':
                    check_append_string_token(&tokens, &buffer, file, &row, &col);
                    tokens_append(&tokens, (Token) { Token_LeftParenthesis, 0, LOCATION(file, row, col) });
                    col++;
                    i++;
                    break;
                case ')':
                    check_append_string_token(&tokens, &buffer, file, &row, &col);
                    tokens_append(&tokens, (Token) { Token_RightParenthesis, 0, LOCATION(file, row, col) });
                    col++;
                    i++;
                    break;
                case ':':
                    check_append_string_token(&tokens, &buffer, file, &row, &col);
                    if (contents[i + 1] == ':') {
                        tokens_append(&tokens, (Token) { Token_DoubleColon, 0, LOCATION(file, row, col) });
                        col += 2;
                        i += 2;
                    } else {
                        tokens_append(&tokens, (Token) { Token_Colon, 0, LOCATION(file, row, col) });
                        col++;
                        i++;
                    }
                    break;
                case ';':
                    check_append_string_token(&tokens, &buffer, file, &row, &col);
                    tokens_append(&tokens, (Token) { Token_Semicolon, 0, LOCATION(file, row, col) });
                    col++;
                    i++;
                    break;
                case ',':
                    check_append_string_token(&tokens, &buffer, file, &row, &col);
                    tokens_append(&tokens, (Token) { Token_Comma, 0, LOCATION(file, row, col) });
                    col++;
                    i++;
                    break;
                case '.':
                    check_append_string_token(&tokens, &buffer, file, &row, &col);
                    if (contents[i + 1] == '.') {
                        tokens_append(&tokens, (Token) { Token_DoublePeriod, 0, LOCATION(file, row, col) });
                        col += 2;
                        i += 2;
                    } else {
                        tokens_append(&tokens, (Token) { Token_Period, 0, LOCATION(file, row, col) });
                        col++;
                        i++;
                    }
                    break;
                case '=':
                    check_append_string_token(&tokens, &buffer, file, &row, &col);
                    if (contents[i + 1] == '=') {
                        tokens_append(&tokens, (Token) { Token_DoubleEquals, 0, LOCATION(file, row, col) });
                        col += 2;
                        i += 2;
                    } else {
                        tokens_append(&tokens, (Token) { Token_Equals, 0, LOCATION(file, row, col) });
                        col++;
                        i++;
                    }
                    break;
                case '>':
                    check_append_string_token(&tokens, &buffer, file, &row, &col);
                    if (contents[i + 1] == '=') {
                        tokens_append(&tokens, (Token) { Token_GreaterThanEqual, 0, LOCATION(file, row, col) });
                        col += 2;
                        i += 2;
                    } else {
                        tokens_append(&tokens, (Token) { Token_GreaterThan, 0, LOCATION(file, row, col) });
                        col++;
                        i++;
                    }
                    break;
                case '<':
                    check_append_string_token(&tokens, &buffer, file, &row, &col);
                    if (contents[i + 1] == '=') {
                        tokens_append(&tokens, (Token) { Token_LessThanEqual, 0, LOCATION(file, row, col) });
                        i += 2;
                    } else {
                        tokens_append(&tokens, (Token) { Token_LessThan, 0, LOCATION(file, row, col) });
                        i++;
                    }
                    break;
                case '!':
                    check_append_string_token(&tokens, &buffer, file, &row, &col);
                    if (contents[i + 1] == '=') {
                        tokens_append(&tokens, (Token) { Token_ExclamationEquals, 0, LOCATION(file, row, col) });
                        col += 2;
                        i += 2;
                    } else {
                        tokens_append(&tokens, (Token) { Token_Exclamation, 0, LOCATION(file, row, col) });
                        col++;
                        i++;
                    }
                    break;
                case '+':
                    check_append_string_token(&tokens, &buffer, file, &row, &col);
                    tokens_append(&tokens, (Token) { Token_Plus, 0, LOCATION(file, row, col) });
                    col++;
                    i++;
                    break;
                case '-':
                    check_append_string_token(&tokens, &buffer, file, &row, &col);
                    tokens_append(&tokens, (Token) { Token_Minus, 0, LOCATION(file, row, col) });
                    col++;
                    i++;
                    break;
                case '*':
                    check_append_string_token(&tokens, &buffer, file, &row, &col);
                    tokens_append(&tokens, (Token) { Token_Asterisk, 0, LOCATION(file, row, col) });
                    col++;
                    i++;
                    break;
                case '/':
                    check_append_string_token(&tokens, &buffer, file, &row, &col);
                    if (contents[i + 1] == '/') {
                        in_comment = true;
                        col += 2;
                        i += 2;
                    } else {
                        tokens_append(&tokens, (Token) { Token_Slash, 0, LOCATION(file, row, col) });
                        col++;
                        i++;
                    }
                    break;
                case '%':
                    check_append_string_token(&tokens, &buffer, file, &row, &col);
                    tokens_append(&tokens, (Token) { Token_Percent, 0, LOCATION(file, row, col) });
                    col++;
                    i++;
                    break;
                case '&':
                    check_append_string_token(&tokens, &buffer, file, &row, &col);
                    if (contents[i + 1] == '&') {
                        tokens_append(&tokens, (Token) { Token_DoubleAmpersand, 0, LOCATION(file, row, col) });
                        col += 2;
                        i += 2;
                    } else {
                        tokens_append(&tokens, (Token) { Token_Ampersand, 0, LOCATION(file, row, col) });
                        col++;
                        i++;
                    }
                    break;
                case '|':
                    check_append_string_token(&tokens, &buffer, file, &row, &col);
                    if (contents[i + 1] == '|') {
                        tokens_append(&tokens, (Token) { Token_DoubleBar, 0, LOCATION(file, row, col) });
                        col += 2;
                        i += 2;
                    } else {
                        assert(false);
                    }
                    break;
                case '{':
                    check_append_string_token(&tokens, &buffer, file, &row, &col);
                    tokens_append(&tokens, (Token) { Token_LeftCurlyBrace, 0, LOCATION(file, row, col) });
                    col++;
                    i++;
                    break;
                case '}':
                    check_append_string_token(&tokens, &buffer, file, &row, &col);
                    tokens_append(&tokens, (Token) { Token_RightCurlyBrace, 0, LOCATION(file, row, col) });
                    col++;
                    i++;
                    break;
                case '[':
                    check_append_string_token(&tokens, &buffer, file, &row, &col);
                    tokens_append(&tokens, (Token) { Token_LeftBracket, 0, LOCATION(file, row, col) });
                    col++;
                    i++;
                    break;
                case ']':
                    check_append_string_token(&tokens, &buffer, file, &row, &col);
                    tokens_append(&tokens, (Token) { Token_RightBracket, 0, LOCATION(file, row, col) });
                    col++;
                    i++;
                    break;
                case '"':
                    check_append_string_token(&tokens, &buffer, file, &row, &col);
                    in_string = true;
                    cached_i = i;
                    i++;
                    break;
                case '?':
                    check_append_string_token(&tokens, &buffer, file, &row, &col);
                    if (contents[i + 1] == '?') {
                        tokens_append(&tokens, (Token) { Token_DoubleQuestionMark, 0, LOCATION(file, row, col) });
                        i += 2;
                    } else {
                        tokens_append(&tokens, (Token) { Token_QuestionMark, 0, LOCATION(file, row, col) });
                        i++;
                    }
                    break;
                case ' ':
                    check_append_string_token(&tokens, &buffer, file, &row, &col);
                    //tokens_append(&tokens, (Token) { Space });
                    col++;
                    i++;
                    break;
                case '\n':
                    check_append_string_token(&tokens, &buffer, file, &row, &col);
                    //tokens_append(&tokens, (Token) { NewLine });
                    col = 1;
                    row++;
                    i++;
                    break;
                default:
                    stringbuffer_append(&buffer, character);
                    i++;

                    if (character >= '0' && character <= '9' && contents[i] == '.') {
                        bool is_number_buffer = true;
                        for (size_t j = 0; j < buffer.count; j++) {
                            if (buffer.elements[j] < '0' || buffer.elements[j] > '9') {
                                is_number_buffer = false;
                            }
                        }
                        if (is_number_buffer) {
                            stringbuffer_append(&buffer, contents[i]);
                            i++;
                        }
                    }

                    break;
            }
        }
    }

    stringbuffer_free(&buffer);
    return tokens;
}
