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
#ifndef JSON_PARSE_STRINGIFY_INIT_SIZE
#define JSON_PARSE_STRINGIFY_INIT_SIZE 256
#endif
static int is_int_1t9(char ch){

    return ((ch) >= '1' && (ch) <= '9');
}
static int is_int(char ch){

    return ((ch) >= '0' && (ch) <= '9');

}
#define PUT_INTO_STACK(c, ch) do { *(char*)json_context_push(c, sizeof(char)) = (ch); } while(0)
#define ERROR(ret) do { c->top = head; return ret; } while(0)
#define PUTS(c, s, len)     memcpy(json_context_push(c, len), s, len)
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
static int json_parse_value(json_context* c, json_value* v);
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

static int parse_string_raw(json_context* c, char** str, size_t* len) {
    size_t head = c->top;
    unsigned u, u2;
    const char* p;
    assert(*c->json == ('\"'));
    c->json++;
    p = c->json;
    for (;;) {
        char ch = *p++;
        switch (ch) {
            case '\"':
                *len = c->top - head;
                *str = json_context_pop(c, *len);
                c->json = p;
                return JSON_PARSE_OK;
            case '\\':
                switch (*p++) {
                    case '\"': PUT_INTO_STACK(c, '\"'); break;
                    case '\\': PUT_INTO_STACK(c, '\\'); break;
                    case '/':  PUT_INTO_STACK(c, '/' ); break;
                    case 'b':  PUT_INTO_STACK(c, '\b'); break;
                    case 'f':  PUT_INTO_STACK(c, '\f'); break;
                    case 'n':  PUT_INTO_STACK(c, '\n'); break;
                    case 'r':  PUT_INTO_STACK(c, '\r'); break;
                    case 't':  PUT_INTO_STACK(c, '\t'); break;
                    case 'u':
                        if (!(p = parse_hex4(p, &u)))
                            ERROR(JSON_PARSE_INVALID_UNICODE_HEX);
                        if (u >= 0xD800 && u <= 0xDBFF) { /* surrogate pair */
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
                        ERROR(JSON_PARSE_INVALID_STRING_ESCAPE);
                }
                break;
            case '\0':
                ERROR(JSON_PARSE_MISS_QUOTATION_MARK);
            default:
                if ((unsigned char)ch < 0x20)
                    ERROR(JSON_PARSE_INVALID_STRING_CHAR);
                PUT_INTO_STACK(c, ch);
        }
    }
}

static int parse_string(json_context* c, json_value* v) {
    int ret;
    char* s;
    size_t len;
    if ((ret = parse_string_raw(c, &s, &len)) == JSON_PARSE_OK)
        json_set_string(v, s, len);
    return ret;
}


static int parse_array(json_context* c, json_value* v) {
    size_t i, size = 0;
    int ret;
    assert(*c->json == ('[')); 
    c->json++;
    parse_whitespace(c);
    if (*c->json == ']') {
        c->json++;
        v->type = JSON_ARRAY;
        v->u.a.size = 0;
        v->u.a.e = NULL;
        return JSON_PARSE_OK;
    }
    for (;;) {
        json_value e;
        json_init(&e);
        if ((ret = json_parse_value(c, &e)) != JSON_PARSE_OK)
            break;
        memcpy(json_context_push(c, sizeof(json_value)), &e, sizeof(json_value));
        size++;
        parse_whitespace(c);
        if (*c->json == ',') {
            c->json++;
            parse_whitespace(c);
        }
        else if (*c->json == ']') {
            c->json++;
            v->type = JSON_ARRAY;
            v->u.a.size = size;
            size *= sizeof(json_value);
            memcpy(v->u.a.e = (json_value*)malloc(size), json_context_pop(c, size), size);
            return JSON_PARSE_OK;
        }
        else {
            ret = JSON_PARSE_MISS_COMMA_OR_SQUARE_BRACKET;
            break;
        }
    }
    /* Pop and free values on the stack */
    for (i = 0; i < size; i++)
        json_free((json_value*)json_context_pop(c, sizeof(json_value)));
    return ret;
}

static int parse_object(json_context* c, json_value* v) {
    size_t i, size;
    json_member m;
    int ret;
    assert(*c->json == ('{'));
    c->json++;
    parse_whitespace(c);
    if (*c->json == '}') {
        c->json++;
        v->type = JSON_OBJECT;
        v->u.o.m = 0;
        v->u.o.size = 0;
        return JSON_PARSE_OK;
    }
    m.k = NULL;
    size = 0;
    for (;;) {
        char* str;
        json_init(&m.v);
        /* parse key */
        if (*c->json != '"') {
            ret = JSON_PARSE_MISS_KEY;
            break;
        }
        if ((ret = parse_string_raw(c, &str, &m.klen)) != JSON_PARSE_OK)
            break;
        memcpy(m.k = (char*)malloc(m.klen + 1), str, m.klen);
        m.k[m.klen] = '\0';
        /* parse ws colon ws */
        parse_whitespace(c);
        if (*c->json != ':') {
            ret = JSON_PARSE_MISS_COLON;
            break;
        }
        c->json++;
        parse_whitespace(c);
        /* parse value */
        if ((ret = json_parse_value(c, &m.v)) != JSON_PARSE_OK)
            break;
        memcpy(json_context_push(c, sizeof(json_member)), &m, sizeof(json_member));
        size++;
        m.k = NULL; /* ownership is transferred to member on stack */
        /* parse ws [comma | right-curly-brace] ws */
        parse_whitespace(c);
        if (*c->json == ',') {
            c->json++;
            parse_whitespace(c);
        }
        else if (*c->json == '}') {
            size_t s = sizeof(json_member) * size;
            c->json++;
            v->type = JSON_OBJECT;
            v->u.o.size = size;
            memcpy(v->u.o.m = (json_member*)malloc(s), json_context_pop(c, s), s);
            return JSON_PARSE_OK;
        }
        else {
            ret = JSON_PARSE_MISS_COMMA_OR_CURLY_BRACKET;
            break;
        }
    }
    /* Pop and free members on the stack */
    free(m.k);
    for (i = 0; i < size; i++) {
        json_member* m = (json_member*)json_context_pop(c, sizeof(json_member));
        free(m->k);
        json_free(&m->v);
    }
    v->type = JSON_NULL;
    return ret;
}

static int json_parse_value(json_context* c, json_value* v) {
    switch (*c->json) {
        case 't':  return parse_literal(c, v, "true", JSON_TRUE);
        case 'f':  return parse_literal(c, v, "false", JSON_FALSE);
        case 'n':  return parse_literal(c, v, "null", JSON_NULL);
        default:   return parse_number(c, v);
        case '"':  return parse_string(c, v);
        case '[':  return parse_array(c, v);
        case '{':  return parse_object(c, v);
        case '\0': return JSON_PARSE_EXPECT_VALUE;
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
    if ((ret = json_parse_value(&c, v)) == JSON_PARSE_OK) {
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

static void json_stringify_string(json_context* c, const char* s, size_t len) {
    static const char hex_digits[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };
    size_t i, size;
    char* head, *p;
    assert(s != NULL);
    p = head = json_context_push(c, size = len * 6 + 2); /* "\u00xx..." */
    *p++ = '"';
    for (i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)s[i];
        switch (ch) {
            case '\"': *p++ = '\\'; *p++ = '\"'; break;
            case '\\': *p++ = '\\'; *p++ = '\\'; break;
            case '\b': *p++ = '\\'; *p++ = 'b';  break;
            case '\f': *p++ = '\\'; *p++ = 'f';  break;
            case '\n': *p++ = '\\'; *p++ = 'n';  break;
            case '\r': *p++ = '\\'; *p++ = 'r';  break;
            case '\t': *p++ = '\\'; *p++ = 't';  break;
            default:
                if (ch < 0x20) {
                    *p++ = '\\'; *p++ = 'u'; *p++ = '0'; *p++ = '0';
                    *p++ = hex_digits[ch >> 4];
                    *p++ = hex_digits[ch & 15];
                }
                else
                    *p++ = s[i];
        }
    }
    *p++ = '"';
    c->top -= size - (p - head);
}

static void json_stringify_value(json_context* c, const json_value* v) {
    size_t i;
    switch (v->type) {
        case JSON_NULL:   PUTS(c, "null",  4); break;
        case JSON_FALSE:  PUTS(c, "false", 5); break;
        case JSON_TRUE:   PUTS(c, "true",  4); break;
        case JSON_NUMBER: c->top -= 32 - sprintf(json_context_push(c, 32), "%.17g", v->u.n); break;
        case JSON_STRING: json_stringify_string(c, v->u.s.s, v->u.s.len); break;
        case JSON_ARRAY:
            PUT_INTO_STACK(c, '[');
            for (i = 0; i < v->u.a.size; i++) {
                if (i > 0)
                    PUT_INTO_STACK(c, ',');
                json_stringify_value(c, &v->u.a.e[i]);
            }
            PUT_INTO_STACK(c, ']');
            break;
        case JSON_OBJECT:
            PUT_INTO_STACK(c, '{');
            for (i = 0; i < v->u.o.size; i++) {
                if (i > 0)
                    PUT_INTO_STACK(c, ',');
                json_stringify_string(c, v->u.o.m[i].k, v->u.o.m[i].klen);
                PUT_INTO_STACK(c, ':');
                json_stringify_value(c, &v->u.o.m[i].v);
            }
            PUT_INTO_STACK(c, '}');
            break;
        default: assert(0 && "invalid type");
    }
}

char* json_stringify(const json_value* v, size_t* length) {
    json_context c;
    assert(v != NULL);
    c.stack = (char*)malloc(c.size = JSON_PARSE_STRINGIFY_INIT_SIZE);
    c.top = 0;
    json_stringify_value(&c, v);
    if (length)
        *length = c.top;
    PUT_INTO_STACK(&c, '\0');
    return c.stack;
}

void json_free(json_value* v) {
    size_t i;
    assert(v != NULL);
    switch (v->type) {
        case JSON_STRING:
            free(v->u.s.s);
            break;
        case JSON_ARRAY:
            for (i = 0; i < v->u.a.size; i++)
                json_free(&v->u.a.e[i]);
            free(v->u.a.e);
            break;
        case JSON_OBJECT:
            for (i = 0; i < v->u.o.size; i++) {
                free(v->u.o.m[i].k);
                json_free(&v->u.o.m[i].v);
            }
            free(v->u.o.m);
            break;
        default: break;
    }
    v->type = JSON_NULL;
}

json_type json_get_type(const json_value* v) {
    assert(v != NULL);
    return v->type;
}

int json_get_boolean(const json_value* v) {
    assert(v != NULL && (v->type == JSON_TRUE || v->type == JSON_FALSE));
    return v->type == JSON_TRUE;
}

void json_set_boolean(json_value* v, int b) {
    json_free(v);
    v->type = b ? JSON_TRUE : JSON_FALSE;
}

double json_get_number(const json_value* v) {
    assert(v != NULL && v->type == JSON_NUMBER);
    return v->u.n;
}

void json_set_number(json_value* v, double n) {
    json_free(v);
    v->u.n = n;
    v->type = JSON_NUMBER;
}

const char* json_get_string(const json_value* v) {
    assert(v != NULL && v->type == JSON_STRING);
    return v->u.s.s;
}

size_t json_get_string_length(const json_value* v) {
    assert(v != NULL && v->type == JSON_STRING);
    return v->u.s.len;
}

void json_set_string(json_value* v, const char* s, size_t len) {
    assert(v != NULL && (s != NULL || len == 0));
    json_free(v);
    v->u.s.s = (char*)malloc(len + 1);
    memcpy(v->u.s.s, s, len);
    v->u.s.s[len] = '\0';
    v->u.s.len = len;
    v->type = JSON_STRING;
}

size_t json_get_array_size(const json_value* v) {
    assert(v != NULL && v->type == JSON_ARRAY);
    return v->u.a.size;
}

json_value* json_get_array_element(const json_value* v, size_t index) {
    assert(v != NULL && v->type == JSON_ARRAY);
    assert(index < v->u.a.size);
    return &v->u.a.e[index];
}

size_t json_get_object_size(const json_value* v) {
    assert(v != NULL && v->type == JSON_OBJECT);
    return v->u.o.size;
}

const char* json_get_object_key(const json_value* v, size_t index) {
    assert(v != NULL && v->type == JSON_OBJECT);
    assert(index < v->u.o.size);
    return v->u.o.m[index].k;
}

size_t json_get_object_key_length(const json_value* v, size_t index) {
    assert(v != NULL && v->type == JSON_OBJECT);
    assert(index < v->u.o.size);
    return v->u.o.m[index].klen;
}

json_value* json_get_object_value(const json_value* v, size_t index) {
    assert(v != NULL && v->type == JSON_OBJECT);
    assert(index < v->u.o.size);
    return &v->u.o.m[index].v;
}
