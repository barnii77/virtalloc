/*
 * json_parser_virtalloc.c
 *
 * A “safe” JSON parser in C in a single file.
 *
 * This version reads JSON from a file ("test.json"),
 * parses it, then re-serializes the parse tree to a string,
 * and finally compares the re-serialized JSON with the original file content.
 *
 * The parser now distinguishes between integer and floating point numbers.
 * Integers are stored as long long (JSON_INTEGER) and floats as double (JSON_FLOAT).
 *
 * This port replaces all libc memory calls with calls to your custom allocator.
 *
 * Compile with:
 *     gcc -std=c99 -Wall -Wextra -DTEST_JSON_PARSER -o json_parser_virtalloc json_parser_virtalloc.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include "virtalloc.h"

#ifndef ERANGE
#define ERANGE 34
#endif

/* --- Global Allocator ---
   We use a static global allocator (of type vap_t) for all memory operations.
   It is initialized in main() (if not already initialized elsewhere).
*/
static vap_t alloc = NULL;

static void *virtalloc_malloc_wrapper(vap_t allocator, size_t size) {
    static int call_count = 0;
    call_count++;
    void *out = virtalloc_malloc(allocator, size);
    if (!out)
        fprintf(stderr, "f in malloc (call nr. %d)\n", call_count);
    return out;
}

static void *virtalloc_realloc_wrapper(vap_t allocator, void *p, size_t size) {
    static int call_count = 0;
    call_count++;
    if (call_count == 12565) {
        int a = 0;
    }
    void *out = virtalloc_realloc(allocator, p, size);
    if (!out)
        fprintf(stderr, "f in realloc (call nr. %d)\n", call_count);
    return out;
}

static void virtalloc_free_wrapper(vap_t allocator, void *p) {
    virtalloc_free(allocator, p);
}

/* --- Data Structures --- */

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_INTEGER, /* integer value, stored as long long */
    JSON_FLOAT, /* floating-point value, stored as double */
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} JSONType;

typedef struct JSONValue JSONValue;

struct JSONValue {
    JSONType type;

    union {
        int bool_value; /* 0 for false, non-zero for true */
        long long int_value; /* for JSON_INTEGER */
        double float_value; /* for JSON_FLOAT */
        char *string_value;

        struct {
            JSONValue **items;
            size_t count;
            size_t capacity;
        } array;

        struct {
            char **keys;
            JSONValue **values;
            size_t count;
            size_t capacity;
        } object;
    } u;
};

/* --- Forward Declarations --- */

static JSONValue *parse_value(const char **p);

static JSONValue *parse_null(const char **p);

static JSONValue *parse_bool(const char **p);

static JSONValue *parse_number(const char **p);

static JSONValue *parse_string(const char **p);

static JSONValue *parse_array(const char **p);

static JSONValue *parse_object(const char **p);

static void skip_whitespace(const char **p);

static void free_json_value(JSONValue *value);

/* For serialization to a string */
static int json_serialize(JSONValue *value, char *buf, size_t capacity, size_t *pos);

static int buf_append(char *buf, size_t capacity, size_t *pos, const char *fmt, ...);

/* --- Helper Functions --- */

/* Skips whitespace characters */
static void skip_whitespace(const char **p) {
    while (**p && isspace((unsigned char)**p)) {
        (*p)++;
    }
}

/* Append a character to a dynamic string buffer */
static int append_char(char **buffer, size_t *len, size_t *cap, char c) {
    if (*len + 1 >= *cap) {
        size_t new_cap = (*cap) * 2;
        char *new_buf = virtalloc_realloc_wrapper(alloc, *buffer, new_cap);
        if (!new_buf) {
            return 0;
        }
        *buffer = new_buf;
        *cap = new_cap;
    }
    (*buffer)[(*len)++] = c;
    return 1;
}

/* --- Unicode Escape Processing --- */
/* Decodes a 4-digit hex Unicode escape and writes its UTF-8
   representation into the buffer. For brevity, only BMP code points are supported.
*/
static int append_unicode_escape(char **buffer, size_t *len, size_t *cap, const char **p) {
    char hex[5] = {0};
    for (int i = 0; i < 4; i++) {
        if (!isxdigit((unsigned char)(*(*p)))) {
            return 0;
        }
        hex[i] = *(*p);
        (*p)++;
    }
    unsigned int code;
    if (sscanf(hex, "%x", &code) != 1) {
        return 0;
    }
    if (code < 0x80) {
        return append_char(buffer, len, cap, (char) code);
    } else if (code < 0x800) {
        if (!append_char(buffer, len, cap, (char) (0xC0 | (code >> 6)))) return 0;
        if (!append_char(buffer, len, cap, (char) (0x80 | (code & 0x3F)))) return 0;
    } else {
        if (!append_char(buffer, len, cap, (char) (0xE0 | (code >> 12)))) return 0;
        if (!append_char(buffer, len, cap, (char) (0x80 | ((code >> 6) & 0x3F)))) return 0;
        if (!append_char(buffer, len, cap, (char) (0x80 | (code & 0x3F)))) return 0;
    }
    return 1;
}

/* --- Parsing Functions --- */

/* Parse a JSON string value.
   Assumes that the current character is the opening quote.
*/
static JSONValue *parse_string(const char **p) {
    if (**p != '"') {
        return NULL;
    }
    (*p)++; // skip opening quote

    size_t capacity = 32;
    size_t len = 0;
    char *buffer = virtalloc_malloc_wrapper(alloc, capacity);
    if (!buffer) return NULL;

    while (**p) {
        char c = *(*p)++;
        if (c == '"') {
            /* End of string */
            break;
        } else if (c == '\\') {
            /* Escape sequence */
            char esc = *(*p)++;
            switch (esc) {
                case '"': c = '"';
                    break;
                case '\\': c = '\\';
                    break;
                case '/': c = '/';
                    break;
                case 'b': c = '\b';
                    break;
                case 'f': c = '\f';
                    break;
                case 'n': c = '\n';
                    break;
                case 'r': c = '\r';
                    break;
                case 't': c = '\t';
                    break;
                case 'u':
                    if (!append_unicode_escape(&buffer, &len, &capacity, p)) {
                        virtalloc_free_wrapper(alloc, buffer);
                        return NULL;
                    }
                    continue; /* Already appended the Unicode char(s) */
                default:
                    virtalloc_free_wrapper(alloc, buffer);
                    return NULL; /* Invalid escape */
            }
        }
        if (!append_char(&buffer, &len, &capacity, c)) {
            virtalloc_free_wrapper(alloc, buffer);
            return NULL;
        }
    }
    /* Null-terminate the string */
    if (!append_char(&buffer, &len, &capacity, '\0')) {
        virtalloc_free_wrapper(alloc, buffer);
        return NULL;
    }

    JSONValue *value = virtalloc_malloc_wrapper(alloc, sizeof(JSONValue));
    if (!value) {
        virtalloc_free_wrapper(alloc, buffer);
        return NULL;
    }
    value->type = JSON_STRING;
    value->u.string_value = buffer;
    return value;
}

/* Parse a JSON number.
   This function distinguishes between integers and floats.
   If the number text contains a '.' or an exponent ('e' or 'E'),
   it is parsed as a floating-point number (JSON_FLOAT).
   Otherwise, it is parsed as an integer (JSON_INTEGER).
*/
static JSONValue *parse_number(const char **p) {
    const char *start = *p;
    int is_float = 0;
    /* Advance pointer through the number characters.
       JSON numbers can include: a leading '-' (only), digits, a decimal point,
       and an exponent part (with 'e' or 'E' optionally followed by '+' or '-').
    */
    if (**p == '-') {
        (*p)++;
    }
    while (isdigit((unsigned char)**p)) {
        (*p)++;
    }
    if (**p == '.') {
        is_float = 1;
        (*p)++;
        while (isdigit((unsigned char)**p)) {
            (*p)++;
        }
    }
    if (**p == 'e' || **p == 'E') {
        is_float = 1;
        (*p)++;
        if (**p == '+' || **p == '-') {
            (*p)++;
        }
        while (isdigit((unsigned char)**p)) {
            (*p)++;
        }
    }
    size_t num_len = *p - start;
    char *num_str = virtalloc_malloc_wrapper(alloc, num_len + 1);
    if (!num_str) return NULL;
    memcpy(num_str, start, num_len);
    num_str[num_len] = '\0';

    JSONValue *value = virtalloc_malloc_wrapper(alloc, sizeof(JSONValue));
    if (!value) {
        virtalloc_free_wrapper(alloc, num_str);
        return NULL;
    }
    if (is_float) {
        errno = 0;
        double d = strtod(num_str, NULL);
        if (errno == ERANGE) {
            virtalloc_free_wrapper(alloc, num_str);
            virtalloc_free_wrapper(alloc, value);
            return NULL;
        }
        value->type = JSON_FLOAT;
        value->u.float_value = d;
    } else {
        errno = 0;
        long long i = strtoll(num_str, NULL, 10);
        if (errno == ERANGE) {
            virtalloc_free_wrapper(alloc, num_str);
            virtalloc_free_wrapper(alloc, value);
            return NULL;
        }
        value->type = JSON_INTEGER;
        value->u.int_value = i;
    }
    virtalloc_free_wrapper(alloc, num_str);
    return value;
}

/* Parse the literal "null". */
static JSONValue *parse_null(const char **p) {
    if (strncmp(*p, "null", 4) != 0) {
        return NULL;
    }
    *p += 4;
    JSONValue *value = virtalloc_malloc_wrapper(alloc, sizeof(JSONValue));
    if (!value) return NULL;
    value->type = JSON_NULL;
    return value;
}

/* Parse the literals "true" or "false". */
static JSONValue *parse_bool(const char **p) {
    JSONValue *value = virtalloc_malloc_wrapper(alloc, sizeof(JSONValue));
    if (!value) return NULL;
    if (strncmp(*p, "true", 4) == 0) {
        *p += 4;
        value->type = JSON_BOOL;
        value->u.bool_value = 1;
    } else if (strncmp(*p, "false", 5) == 0) {
        *p += 5;
        value->type = JSON_BOOL;
        value->u.bool_value = 0;
    } else {
        virtalloc_free_wrapper(alloc, value);
        return NULL;
    }
    return value;
}

/* Parse a JSON array.
   Assumes that the current character is '['.
*/
static JSONValue *parse_array(const char **p) {
    if (**p != '[') {
        return NULL;
    }
    (*p)++; // Skip '['
    skip_whitespace(p);

    JSONValue *value = virtalloc_malloc_wrapper(alloc, sizeof(JSONValue));
    if (!value) return NULL;
    value->type = JSON_ARRAY;
    value->u.array.count = 0;
    value->u.array.capacity = 4;
    value->u.array.items = virtalloc_malloc_wrapper(alloc, value->u.array.capacity * sizeof(JSONValue *));
    if (!value->u.array.items) {
        virtalloc_free_wrapper(alloc, value);
        return NULL;
    }

    if (**p == ']') {
        (*p)++; // Empty array
        return value;
    }

    while (1) {
        skip_whitespace(p);
        JSONValue *elem = parse_value(p);
        if (!elem) {
            free_json_value(value);
            return NULL;
        }
        /* Resize if necessary */
        if (value->u.array.count >= value->u.array.capacity) {
            size_t new_capacity = value->u.array.capacity * 2;
            JSONValue **new_items = virtalloc_realloc_wrapper(alloc, value->u.array.items,
                                                              new_capacity * sizeof(JSONValue *));
            if (!new_items) {
                free_json_value(elem);
                free_json_value(value);
                return NULL;
            }
            value->u.array.items = new_items;
            value->u.array.capacity = new_capacity;
        }
        value->u.array.items[value->u.array.count++] = elem;
        skip_whitespace(p);
        if (**p == ',') {
            (*p)++;
            continue;
        } else if (**p == ']') {
            (*p)++;
            break;
        } else {
            free_json_value(value);
            return NULL;
        }
    }
    return value;
}

/* Parse a JSON object.
   Assumes that the current character is '{'.
*/
static JSONValue *parse_object(const char **p) {
    if (**p != '{') {
        return NULL;
    }
    (*p)++; // Skip '{'
    skip_whitespace(p);

    JSONValue *value = virtalloc_malloc_wrapper(alloc, sizeof(JSONValue));
    if (!value) return NULL;
    value->type = JSON_OBJECT;
    value->u.object.count = 0;
    value->u.object.capacity = 4;
    value->u.object.keys = virtalloc_malloc_wrapper(alloc, value->u.object.capacity * sizeof(char *));
    value->u.object.values = virtalloc_malloc_wrapper(alloc, value->u.object.capacity * sizeof(JSONValue *));
    if (!value->u.object.keys || !value->u.object.values) {
        virtalloc_free_wrapper(alloc, value->u.object.keys);
        virtalloc_free_wrapper(alloc, value->u.object.values);
        virtalloc_free_wrapper(alloc, value);
        return NULL;
    }

    if (**p == '}') {
        (*p)++; // Empty object
        return value;
    }

    while (1) {
        skip_whitespace(p);
        /* Keys must be strings */
        if (**p != '"') {
            free_json_value(value);
            return NULL;
        }
        JSONValue *key_val = parse_string(p);
        if (!key_val) {
            free_json_value(value);
            return NULL;
        }
        char *key = key_val->u.string_value;
        virtalloc_free_wrapper(alloc, key_val); // Only need the string

        skip_whitespace(p);
        if (**p != ':') {
            virtalloc_free_wrapper(alloc, key);
            free_json_value(value);
            return NULL;
        }
        (*p)++; // Skip ':'
        skip_whitespace(p);
        JSONValue *val = parse_value(p);
        if (!val) {
            virtalloc_free_wrapper(alloc, key);
            free_json_value(value);
            return NULL;
        }
        /* Resize if necessary */
        if (value->u.object.count >= value->u.object.capacity) {
            size_t new_capacity = value->u.object.capacity * 2;
            char **new_keys = virtalloc_realloc_wrapper(alloc, value->u.object.keys, new_capacity * sizeof(char *));
            JSONValue **new_values = virtalloc_realloc_wrapper(alloc, value->u.object.values,
                                                               new_capacity * sizeof(JSONValue *));
            if (!new_keys || !new_values) {
                virtalloc_free_wrapper(alloc, key);
                free_json_value(val);
                virtalloc_free_wrapper(alloc, new_keys);
                virtalloc_free_wrapper(alloc, new_values);
                free_json_value(value);
                return NULL;
            }
            value->u.object.keys = new_keys;
            value->u.object.values = new_values;
            value->u.object.capacity = new_capacity;
        }
        value->u.object.keys[value->u.object.count] = key;
        value->u.object.values[value->u.object.count] = val;
        value->u.object.count++;

        skip_whitespace(p);
        if (**p == ',') {
            (*p)++;
            continue;
        } else if (**p == '}') {
            (*p)++;
            break;
        } else {
            free_json_value(value);
            return NULL;
        }
    }
    return value;
}

/* Parse any JSON value */
static JSONValue *parse_value(const char **p) {
    skip_whitespace(p);
    if (**p == '\0') return NULL;
    if (**p == 'n') {
        return parse_null(p);
    } else if (**p == 't' || **p == 'f') {
        return parse_bool(p);
    } else if (**p == '"') {
        return parse_string(p);
    } else if (**p == '-' || isdigit((unsigned char)**p)) {
        return parse_number(p);
    } else if (**p == '[') {
        return parse_array(p);
    } else if (**p == '{') {
        return parse_object(p);
    }
    return NULL;
}

/* --- Public API --- */

/* Main entry point: parse a JSON string.
   Returns a heap-allocated JSONValue parse tree,
   or NULL if parsing fails.
*/
JSONValue *json_parse(const char *json_str) {
    const char *p = json_str;
    JSONValue *result = parse_value(&p);
    skip_whitespace(&p);
    if (result == NULL || *p != '\0') {
        /* Parsing error or extra data after valid JSON */
        free_json_value(result);
        return NULL;
    }
    return result;
}

/* Recursively free a JSONValue */
static void free_json_value(JSONValue *value) {
    if (!value) return;
    size_t i;
    switch (value->type) {
        case JSON_STRING:
            virtalloc_free_wrapper(alloc, value->u.string_value);
            break;
        case JSON_ARRAY:
            for (i = 0; i < value->u.array.count; i++) {
                free_json_value(value->u.array.items[i]);
            }
            virtalloc_free_wrapper(alloc, value->u.array.items);
            break;
        case JSON_OBJECT:
            for (i = 0; i < value->u.object.count; i++) {
                virtalloc_free_wrapper(alloc, value->u.object.keys[i]);
                free_json_value(value->u.object.values[i]);
            }
            virtalloc_free_wrapper(alloc, value->u.object.keys);
            virtalloc_free_wrapper(alloc, value->u.object.values);
            break;
        default:
            break;
    }
    virtalloc_free_wrapper(alloc, value);
}

/* --- JSON Serialization to a String --- */

/* Helper function to append formatted text to a buffer.
   Returns 0 on success, or -1 if there is not enough space.
*/
static int buf_append(char *buf, size_t capacity, size_t *pos, const char *fmt, ...) {
    if (*pos >= capacity)
        return -1;
    va_list args;
    va_start(args, fmt);
    const size_t remaining = capacity - *pos;
    int n = vsnprintf(buf + *pos, remaining, fmt, args);
    va_end(args);
    if (n < 0 || (size_t) n >= (size_t) remaining) {
        return -1;
    }
    *pos += n;
    return 0;
}

/* Serialize a JSON string value with proper escaping */
static int json_serialize_string(const char *str, char *buf, size_t capacity, size_t *pos) {
    if (buf_append(buf, capacity, pos, "\"") < 0)
        return -1;
    for (const char *p = str; *p; p++) {
        switch (*p) {
            case '\"':
                if (buf_append(buf, capacity, pos, "\\\"") < 0) return -1;
                break;
            case '\\':
                if (buf_append(buf, capacity, pos, "\\\\") < 0) return -1;
                break;
            case '\b':
                if (buf_append(buf, capacity, pos, "\\b") < 0) return -1;
                break;
            case '\f':
                if (buf_append(buf, capacity, pos, "\\f") < 0) return -1;
                break;
            case '\n':
                if (buf_append(buf, capacity, pos, "\\n") < 0) return -1;
                break;
            case '\r':
                if (buf_append(buf, capacity, pos, "\\r") < 0) return -1;
                break;
            case '\t':
                if (buf_append(buf, capacity, pos, "\\t") < 0) return -1;
                break;
            default:
                if ((unsigned char) *p < 0x20) {
                    /* Control characters as Unicode escape */
                    if (buf_append(buf, capacity, pos, "\\u%04x", (unsigned char) *p) < 0)
                        return -1;
                } else {
                    if (buf_append(buf, capacity, pos, "%c", *p) < 0)
                        return -1;
                }
                break;
        }
    }
    if (buf_append(buf, capacity, pos, "\"") < 0)
        return -1;
    return 0;
}

/* Recursively serialize JSONValue into buf.
   The JSON is serialized in a compact form.
   pos is the current write position.
*/
static int json_serialize(JSONValue *value, char *buf, size_t capacity, size_t *pos) {
    int i;
    switch (value->type) {
        case JSON_NULL:
            if (buf_append(buf, capacity, pos, "null") < 0)
                return -1;
            break;
        case JSON_BOOL:
            if (buf_append(buf, capacity, pos, value->u.bool_value ? "true" : "false") < 0)
                return -1;
            break;
        case JSON_INTEGER:
            if (buf_append(buf, capacity, pos, "%lld", value->u.int_value) < 0)
                return -1;
            break;
        case JSON_FLOAT:
            if (buf_append(buf, capacity, pos, "%.12g", value->u.float_value) < 0)
                return -1;
            break;
        case JSON_STRING:
            if (json_serialize_string(value->u.string_value, buf, capacity, pos) < 0)
                return -1;
            break;
        case JSON_ARRAY:
            if (buf_append(buf, capacity, pos, "[") < 0)
                return -1;
            for (i = 0; i < (int) value->u.array.count; i++) {
                if (i > 0) {
                    if (buf_append(buf, capacity, pos, ",") < 0)
                        return -1;
                }
                if (json_serialize(value->u.array.items[i], buf, capacity, pos) < 0)
                    return -1;
            }
            if (buf_append(buf, capacity, pos, "]") < 0)
                return -1;
            break;
        case JSON_OBJECT:
            if (buf_append(buf, capacity, pos, "{") < 0)
                return -1;
            for (i = 0; i < (int) value->u.object.count; i++) {
                if (i > 0) {
                    if (buf_append(buf, capacity, pos, ",") < 0)
                        return -1;
                }
                if (json_serialize_string(value->u.object.keys[i], buf, capacity, pos) < 0)
                    return -1;
                if (buf_append(buf, capacity, pos, ":") < 0)
                    return -1;
                if (json_serialize(value->u.object.values[i], buf, capacity, pos) < 0)
                    return -1;
            }
            if (buf_append(buf, capacity, pos, "}") < 0)
                return -1;
            break;
        default:
            return -1;
    }
    return 0;
}

/* --- Example main() for testing --- */

/* Reads the entire file into a heap-allocated string.
   Returns the string (which should be freed by the caller) or NULL on error.
*/
#ifdef TEST_JSON_PARSER
static char *read_file(const char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    rewind(fp);
    char *buffer = virtalloc_malloc_wrapper(alloc, len + 1);
    if (!buffer) {
        fclose(fp);
        return NULL;
    }
    size_t read_len = fread(buffer, 1, len, fp);
    fclose(fp);
    if (read_len != (size_t) len) {
        virtalloc_free_wrapper(alloc, buffer);
        return NULL;
    }
    buffer[len] = '\0';
    return buffer;
}

int main(void) {
    /* Initialize the allocator (for example, with 512MB and default settings) */
    const int flags = VIRTALLOC_FLAG_VA_DEFAULT_SETTINGS;
    // const int flags =
    //         (VIRTALLOC_FLAG_VA_DEFAULT_SETTINGS | VIRTALLOC_FLAG_VA_HAS_NON_CHECKSUM_SAFETY_CHECKS |
    //          VIRTALLOC_FLAG_VA_ASSUME_THREAD_SAFE_USAGE)
    //         & ~(VIRTALLOC_FLAG_VA_HAS_CHECKSUM);
    alloc = virtalloc_new_allocator(512 * 1024 * 1024, flags);
    if (!alloc) {
        fprintf(stderr, "Failed to initialize custom allocator.\n");
        return 1;
    }

    /* Read the JSON file */
    char *orig_json = read_file("test_giant.json");
    if (!orig_json) {
        fprintf(stderr, "Failed to read test_giant.json\n");
        virtalloc_destroy_allocator(alloc);
        return 1;
    }

    /* Parse the JSON */
    JSONValue *root = json_parse(orig_json);
    if (!root) {
        fprintf(stderr, "Failed to parse JSON.\n");
        virtalloc_free_wrapper(alloc, orig_json);
        virtalloc_destroy_allocator(alloc);
        return 1;
    }

    /* Serialize the JSONValue to a string.
       We allocate a buffer; here we assume a size (for example, twice the original size).
    */
    size_t ser_capacity = strlen(orig_json) * 2 + 1;
    char *ser_json = virtalloc_malloc_wrapper(alloc, ser_capacity);
    if (!ser_json) {
        fprintf(stderr, "Memory allocation error\n");
        free_json_value(root);
        virtalloc_free_wrapper(alloc, orig_json);
        virtalloc_destroy_allocator(alloc);
        return 1;
    }
    size_t pos = 0;
    if (json_serialize(root, ser_json, ser_capacity, &pos) < 0) {
        fprintf(stderr, "Failed to serialize JSON.\n");
        virtalloc_free_wrapper(alloc, ser_json);
        free_json_value(root);
        virtalloc_free_wrapper(alloc, orig_json);
        virtalloc_destroy_allocator(alloc);
        return 1;
    }
    /* Ensure null termination */
    if (pos < ser_capacity)
        ser_json[pos] = '\0';
    else
        ser_json[ser_capacity - 1] = '\0';

    /* Compare the serialized JSON with the original file content */
    if (strcmp(orig_json, ser_json) == 0) {
        printf("Re-serialized JSON matches the original content.\n");
    } else {
        printf("Re-serialized JSON does NOT match the original content.\n");
        printf("Original:\n%s\n", orig_json);
        printf("Re-serialized:\n%s\n", ser_json);
    }

    /* Cleanup */
    free_json_value(root);
    virtalloc_free_wrapper(alloc, ser_json);
    virtalloc_free_wrapper(alloc, orig_json);

    virtalloc_destroy_allocator(alloc);
    return 0;
}
#endif
