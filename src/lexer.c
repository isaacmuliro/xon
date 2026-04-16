#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include "lexer.h"
#include "xon.h" // Required for token IDs

static char* xon_strdup(const char* src) {
    size_t len = strlen(src) + 1;
    char* out = (char*)malloc(len);
    if (!out) return NULL;
    memcpy(out, src, len);
    return out;
}

static int read_non_ws(FILE* pFile, int* pLine, char** ppzErrMsg) {
    int c;

    if (ppzErrMsg) *ppzErrMsg = NULL;

    while ((c = fgetc(pFile)) != EOF) {
        if (c == '\n') {
            (*pLine)++;
            continue;
        }
        if (isspace((unsigned char)c)) {
            continue;
        }

        if (c == '/') {
            int next = fgetc(pFile);
            if (next == '/') {
                while ((c = fgetc(pFile)) != EOF && c != '\n') {
                    // skip line comment
                }
                if (c == '\n') (*pLine)++;
                continue;
            }
            if (next == '*') {
                int prev = 0;
                while ((c = fgetc(pFile)) != EOF) {
                    if (c == '\n') (*pLine)++;
                    if (prev == '*' && c == '/') break;
                    prev = c;
                }
                if (c == EOF) {
                    if (ppzErrMsg) {
                        *ppzErrMsg = xon_strdup("Unterminated block comment");
                    }
                    return -1;
                }
                continue;
            }
            if (next == EOF) {
                return '/';
            }
            ungetc(next, pFile);
            return '/';
        }

        if (c == '#') {
            while ((c = fgetc(pFile)) != EOF && c != '\n') {
                // skip comment line
            }
            if (c == '\n') (*pLine)++;
            continue;
        }

        return c;
    }
    return EOF;
}

static int append_char(char** buffer, size_t* cap, size_t* len, char c) {
    if (*len + 1 >= *cap) {
        size_t new_cap = (*cap == 0) ? 64 : (*cap * 2);
        char* new_buf = (char*)realloc(*buffer, new_cap);
        if (!new_buf) return 0;
        *buffer = new_buf;
        *cap = new_cap;
    }
    (*buffer)[(*len)++] = c;
    return 1;
}

static int parse_string_token(FILE* pFile, XonTokenData* pData, char** ppzErrMsg, int* pLine) {
    char* buffer = NULL;
    size_t len = 0;
    size_t cap = 0;
    int c;

    while ((c = fgetc(pFile)) != EOF) {
        if (c == '"') {
            if (!append_char(&buffer, &cap, &len, '\0')) {
                free(buffer);
                if (ppzErrMsg) *ppzErrMsg = xon_strdup("Out of memory while parsing string");
                return -1;
            }
            pData->sVal = buffer;
            return STRING;
        }
        if (c == '\n') (*pLine)++;
        if (c == '\\') {
            int esc = fgetc(pFile);
            if (esc == EOF) {
                free(buffer);
                if (ppzErrMsg) *ppzErrMsg = xon_strdup("Unterminated escape sequence");
                return -1;
            }
            if (esc == '\n') (*pLine)++;
            switch (esc) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                default: c = esc; break;
            }
        }
        if (!append_char(&buffer, &cap, &len, (char)c)) {
            free(buffer);
            if (ppzErrMsg) *ppzErrMsg = xon_strdup("Out of memory while parsing string");
            return -1;
        }
    }

    free(buffer);
    if (ppzErrMsg) *ppzErrMsg = xon_strdup("Unterminated string literal");
    return -1;
}

static int parse_number_token(FILE* pFile, int first_char, XonTokenData* pData, char** ppzErrMsg) {
    char buffer[128];
    size_t i = 0;
    int c = first_char;
    int is_hex = 0;

    buffer[i++] = (char)c;

    if (c == '-') {
        int next = fgetc(pFile);
        if (next == EOF || !isdigit((unsigned char)next)) {
            if (next != EOF) ungetc(next, pFile);
            if (ppzErrMsg) *ppzErrMsg = xon_strdup("Invalid number literal");
            return -1;
        }
        if (i < sizeof(buffer) - 1) buffer[i++] = (char)next;
        c = next;
    }

    if (c == '0' || (i >= 2 && buffer[0] == '-' && buffer[1] == '0')) {
        int next = fgetc(pFile);
        if (next == 'x' || next == 'X') {
            if (i < sizeof(buffer) - 1) buffer[i++] = (char)next;
            is_hex = 1;
            int has_digits = 0;
            while ((c = fgetc(pFile)) != EOF && isxdigit((unsigned char)c)) {
                has_digits = 1;
                if (i < sizeof(buffer) - 1) buffer[i++] = (char)c;
            }
            if (c != EOF) ungetc(c, pFile);
            if (!has_digits) {
                if (ppzErrMsg) *ppzErrMsg = xon_strdup("Invalid hexadecimal number");
                return -1;
            }
        } else {
            if (next != EOF) ungetc(next, pFile);
        }
    }

    if (!is_hex) {
        int prev = c;
        while ((c = fgetc(pFile)) != EOF) {
            if (isdigit((unsigned char)c) || c == '.' || c == 'e' || c == 'E') {
                prev = c;
                if (i < sizeof(buffer) - 1) buffer[i++] = (char)c;
                continue;
            }
            if ((c == '+' || c == '-') && (prev == 'e' || prev == 'E')) {
                prev = c;
                if (i < sizeof(buffer) - 1) buffer[i++] = (char)c;
                continue;
            }
            break;
        }
        if (c != EOF) ungetc(c, pFile);
    }

    buffer[i] = '\0';

    if (is_hex) {
        char* parse_start = buffer;
        int negative = 0;
        char* endptr = NULL;
        unsigned long long raw;
        if (buffer[0] == '-') {
            negative = 1;
            parse_start++;
        }
        raw = strtoull(parse_start + 2, &endptr, 16);
        if (*endptr != '\0') {
            if (ppzErrMsg) *ppzErrMsg = xon_strdup("Invalid hexadecimal number");
            return -1;
        }
        pData->nVal = negative ? -(double)raw : (double)raw;
        return NUMBER;
    }

    errno = 0;
    char* endptr = NULL;
    pData->nVal = strtod(buffer, &endptr);
    if (errno != 0 || !endptr || *endptr != '\0') {
        if (ppzErrMsg) *ppzErrMsg = xon_strdup("Invalid number literal");
        return -1;
    }
    return NUMBER;
}

int xon_get_token(FILE *pFile, XonTokenData *pData, char **ppzErrMsg, int *pLine) {
    int c;

    if (ppzErrMsg) *ppzErrMsg = NULL;
    if (pData) {
        pData->sVal = NULL;
        pData->nVal = 0.0;
    }

    c = read_non_ws(pFile, pLine, ppzErrMsg);
    if (c == -1) return -1;
    if (c == EOF) return 0;

    switch (c) {
        case '{': return LBRACE;
        case '}': return RBRACE;
        case '[': return LBRACKET;
        case ']': return RBRACKET;
        case '(': return LPAREN;
        case ')': return RPAREN;
        case ',': return COMMA;
        case ':': return COLON;
        case '.': return DOT;
        case '+': return PLUS;
        case '-': return MINUS;
        case '*': return STAR;
        case '/': return SLASH;
        case '%': return PERCENT;
        case '?': {
            int next = fgetc(pFile);
            if (next == '?') return NULLCOALESCE;
            if (next != EOF) ungetc(next, pFile);
            return QUESTION;
        }
        case '!': {
            int next = fgetc(pFile);
            if (next == '=') return NOTEQ;
            if (next != EOF) ungetc(next, pFile);
            return NOT;
        }
        case '=': {
            int next = fgetc(pFile);
            if (next == '=') return EQEQ;
            if (next == '>') return ARROW;
            if (next != EOF) ungetc(next, pFile);
            return ASSIGN;
        }
        case '<': {
            int next = fgetc(pFile);
            if (next == '=') return LTE;
            if (next != EOF) ungetc(next, pFile);
            return LT;
        }
        case '>': {
            int next = fgetc(pFile);
            if (next == '=') return GTE;
            if (next != EOF) ungetc(next, pFile);
            return GT;
        }
        case '&': {
            int next = fgetc(pFile);
            if (next == '&') return AND;
            if (ppzErrMsg) *ppzErrMsg = xon_strdup("Unexpected '&' operator");
            if (next != EOF) {
                ungetc(next, pFile);
            }
            return -1;
        }
        case '|': {
            int next = fgetc(pFile);
            if (next == '|') return OR;
            if (ppzErrMsg) *ppzErrMsg = xon_strdup("Unexpected '|' operator");
            if (next != EOF) {
                ungetc(next, pFile);
            }
            return -1;
        }
        default:
            break;
    }

    if (c == '"') {
        return parse_string_token(pFile, pData, ppzErrMsg, pLine);
    }

    if (isdigit((unsigned char)c) || c == '-') {
        return parse_number_token(pFile, c, pData, ppzErrMsg);
    }

    if (isalpha((unsigned char)c) || c == '_' || c == '$') {
        char buffer[128];
        size_t i = 0;
        buffer[i++] = (char)c;
        while ((c = fgetc(pFile)) != EOF && (isalnum((unsigned char)c) || c == '_' || c == '$')) {
            if (i < sizeof(buffer) - 1) buffer[i++] = (char)c;
        }
        buffer[i] = '\0';
        if (c != EOF) ungetc(c, pFile);

        if (strcmp(buffer, "let") == 0) return LET;
        if (strcmp(buffer, "const") == 0) return CONST;
        if (strcmp(buffer, "if") == 0) return IF;
        if (strcmp(buffer, "else") == 0) return ELSE;
        if (strcmp(buffer, "true") == 0) return TRUE;
        if (strcmp(buffer, "false") == 0) return FALSE;
        if (strcmp(buffer, "null") == 0) return NULL_VAL;

        pData->sVal = xon_strdup(buffer);
        if (!pData->sVal) {
            if (ppzErrMsg) *ppzErrMsg = xon_strdup("Out of memory while parsing identifier");
            return -1;
        }
        return IDENTIFIER;
    }

    if (ppzErrMsg) {
        char err[64];
        snprintf(err, sizeof(err), "Unexpected character '%c'", (char)c);
        *ppzErrMsg = xon_strdup(err);
    }
    return -1;
}
