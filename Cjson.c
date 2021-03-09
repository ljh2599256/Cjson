#include <assert.h>
#include <stdlib.h> /* NULL, strtod() */
#include <stdio.h>
#include <errno.h>  /* errno, ERANGE */
#include <math.h>   /* HUGE_VAL */
#include <string.h>
#include "Cjson.h"

#ifndef STATCK_INIT_SIZE
#define STATCK_INIT_SIZE 256
#endif

static int is_int_1t9(char ch){

    return ((ch) >= '1' && (ch) <= '9');
}
static int is_int(char ch){

    return ((ch) >= '0' && (ch) <= '9');

}
#define PUT_INTO_STACK(c, ch) do { *(char*)json_context_push(c, sizeof(char)) = (ch); } while(0)
#define ERROR(ret) do { c->top = head; return ret; } while(0)

typedef struct {
    const char* json;
    char* stack;        
    size_t size, top;
    short black_page; 
} json_context;

static void parse_whitespace(json_context* c) {
    const char* p = c->json ;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    c->json = p;
}

/* what if strlen(c->json) < strlen(value) ? it will be fine, the if-stmt in for-loop will be pass,
 * which means an error encountered */ 
static int parse_literal(json_context* c, json_value* v, const char* value, json_type type) {
    assert(*c->json == (value[0])); 
    c->json++;
    for (value++; *value; c->json++, value++) {
        if (*c->json != *value)
            return JSON_PARSE_INVALID_VALUE;
    }
    v->type = type;
    return JSON_PARSE_OK;
}

static int parse_number(json_context* c, json_value* v) {
    const char* p = c->json;
    if (*p == '-') p++;
    if (*p == '0') p++;
    else {
        if (!is_int_1t9(*p)) return JSON_PARSE_INVALID_VALUE;
        for (p++; is_int(*p); p++);
    }
    if (*p == '.') {
        p++;
        if (!is_int(*p)) return JSON_PARSE_INVALID_VALUE;
        for (p++; is_int(*p); p++);
    }
    if (*p == 'e' || *p == 'E') {
        p++;
        if (*p == '-' || *p == '+') p++;
        if (!is_int(*p)) return JSON_PARSE_INVALID_VALUE;
        for (p++; is_int(*p); p++);
    }

    errno = 0;
    v->u.n = strtod(c->json, NULL);
    if (errno == ERANGE && (v->u.n == HUGE_VAL || v->u.n == -HUGE_VAL))
        return JSON_PARSE_NUMBER_TOO_BIG;
    c->json = p;
    v->type = JSON_NUMBER;
    return JSON_PARSE_OK;
}


static void* json_context_push(json_context* c, size_t size) {
    assert(size > 0);
    void* ret;
    if (c->size - c->top <= size) {
        if (c->size == 0)
            c->size = STATCK_INIT_SIZE;
        while (c->size - c->top <= size)
            c->size += c->size >> 1;
        c->stack = (char*)realloc(c->stack, c->size);
    }
    ret = c->stack + c->top;
    c->top += size;
    return ret;
}

static void* json_context_pop(json_context* c, size_t size) {
    assert(c->top >= size);
    return c->stack + (c->top -= size);
}

static const char* parse_hex4(const char* p, unsigned* u) {
    size_t i;
    *u = 0;
    for (i=0; i<4; i++) {
        char ch = *p++;
        *u <<= 4;
        if (ch >= '0' && ch <= '9') *u |= ch - '0';
        else if (ch >= 'A' && ch <= 'F') *u |= ch - 'A' + 10;
        else if (ch >= 'a' && ch <= 'f') *u |= ch - 'a' + 10;
        else return NULL;
    }
    return p;
}

static void parse_utf8(json_context* c, unsigned u) {
    if (u <= 0x007F)
        PUT_INTO_STACK(c, u & 0xFF);
    else if (u <= 0x07FF) {
        PUT_INTO_STACK(c, 0xC0 | ((u >> 6) & 0xFF));
        PUT_INTO_STACK(c, 0x80 | ( u       & 0x3F));
    }
    else if (u <= 0xFFFF) {
        PUT_INTO_STACK(c, 0xE0 | ((u >> 12) & 0xFF));
        PUT_INTO_STACK(c, 0x80 | ((u >>  6) & 0x3F));
        PUT_INTO_STACK(c, 0x80 | ( u        & 0x3F));
    }
    else {
        assert(u <= 0x10FFFF);
        PUT_INTO_STACK(c, 0xF0 | ((u >> 18) & 0xFF));
        PUT_INTO_STACK(c, 0x80 | ((u >> 12) & 0x3F));
        PUT_INTO_STACK(c, 0x80 | ((u >>  6) & 0x3F));
        PUT_INTO_STACK(c, 0x80 | ( u        & 0x3F));
    }
}

static int parse_string(json_context* c, json_value* v) {
    assert(*c->json == ('"')); 
    c->json++;
    unsigned u, u2;
    size_t head = c->top, len;
    const char* p = c->json;
    for (;;) {
        char ch = *p++;
        switch (ch) {
            case '\"':
                len = c->top - head;
                json_set_string(v, (const char*)json_context_pop(c, len), len);
                c->json = p;
                return JSON_PARSE_OK;
            case '\\':
                switch(*p++) {
                    case '\"': PUT_INTO_STACK(c, '\"'); break;
                    case 't': PUT_INTO_STACK(c, '\t'); break;
                    case 'r': PUT_INTO_STACK(c, '\r'); break;
                    case 'n': PUT_INTO_STACK(c, '\n'); break;
                    case 'f': PUT_INTO_STACK(c, '\f'); break;
                    case 'b': PUT_INTO_STACK(c, '\b'); break;
                    case '\\': PUT_INTO_STACK(c, '\\'); break;
                    case '/': PUT_INTO_STACK(c, '/'); break;
                    case 'u':
                        if (!(p = parse_hex4(p, &u)))
                            ERROR(JSON_PARSE_INVALID_UNICODE_HEX);
                        if (u >= 0xD800 && u <= 0xDBFF) {
                            if (*p++ != '\\')
                                ERROR(JSON_PARSE_INVALID_UNICODE_SURROGATE);
                            if (*p++ != 'u')
                                ERROR(JSON_PARSE_INVALID_UNICODE_SURROGATE);
                            if (!(p = parse_hex4(p, &u2)))
                                ERROR(JSON_PARSE_INVALID_UNICODE_HEX);
                            if (u2 < 0xDC00 || u2 > 0xDFFF) 
                                ERROR(JSON_PARSE_INVALID_UNICODE_SURROGATE);
                            
                            u = (((u - 0xD800) << 10) | (u2 - 0xDC00)) + 0x10000; 
                        }
                        parse_utf8(c, u);
                        break;
                    default:
                        c->top = head;
                        return JSON_PARSE_INVALID_STRING_ESCAPE;
                }
                break;
            case '\0':
                c->top = head;
                return JSON_PARSE_MISS_QUOTATION_MARK;
            default:
                
                if ((unsigned char)ch < 0x20) {
                    c->top = head;
                    return JSON_PARSE_INVALID_STRING_CHAR;
                }
                PUT_INTO_STACK(c, ch);
        }
    }
}

static int parse_value(json_context* c, json_value* v) {
    switch (*c->json) {
        case 'n': parse_literal(c, v, "null", JSON_NULL); break;
        case 't': parse_literal(c, v, "true", JSON_TRUE); break;
        case 'f': parse_literal(c, v, "false", JSON_FALSE); break;
        case '\0': return JSON_PARSE_EXPECT_VALUE;
        case '"': parse_string(c, v); break;
        default: return parse_number(c, v);
    }
}


int json_parse(json_value *v, const char *json) {
    assert(v != NULL);
    json_context c;
    int ret;

    c.json = json;
    json_init(v);
    c.stack = NULL;
    c.size = c.top = 0;

    parse_whitespace(&c);
    if ((ret = parse_value(&c, v)) == JSON_PARSE_OK) {
        parse_whitespace(&c);
        if (*c.json != '\0') {
            v->type = JSON_NULL;
            ret = JSON_PARSE_ROOT_NOT_SINGULAR;
        }
    }
    assert(c.top == 0);
    free(c.stack);
    return ret;
}

void json_free(json_value* v) {
    assert(v != NULL);
    if (v->type == JSON_STRING)
        free(v->u.s.s);
    v->type = JSON_NULL;    
}

/************************** data access **************************/
json_type json_get_type(const json_value *v) {
    assert(v != NULL);
    return v->type;
}

double json_get_number(const json_value *v) {
    assert(v != NULL && v->type == JSON_NUMBER);
    return v->u.n;
}

void json_set_number(json_value* v, double n) {
     
    json_free(v);
    v->u.n = n;
    v->type = JSON_NUMBER;
}

int json_get_boolean(const json_value *v) {
    assert(v != NULL && (v->type == JSON_TRUE || v->type == JSON_FALSE));
    return v->type == JSON_TRUE;
}

void json_set_boolean(json_value* v, int b) {
    json_free(v);
    v->type = b ? JSON_TRUE : JSON_FALSE;
}

void json_set_string(json_value* v, const char* s, size_t len) {
    assert(v != NULL && (s != NULL || len == 0));  
    json_free(v);
    v->u.s.s = (char*)malloc(len+1);
    memcpy(v->u.s.s, s, len);
    v->u.s.s[len] = '\0';   
    v->u.s.len = len;
    v->type = JSON_STRING;
}

const char* json_get_string(const json_value* v) {
    assert(v != NULL && v->type == JSON_STRING);
    return v->u.s.s;
}

size_t json_get_string_length(const json_value* v) {
    assert(v != NULL && v->type == JSON_STRING);
    return v->u.s.len;
}
