/* SDK-specific Ruby adapter. Raising and mruby allocation happen on the C side
 * after native kernels return. The test host supplies real mruby but not the
 * proprietary DragonRuby API table or renderer. */
#include <dragonruby.h>
#include <mruby/array.h>
#include "native.h"
#include "apps.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef DRBZ_SQLITE_QUERY
#include <sqlite3.h>
#include "query.h"
#endif

static drb_api_t *api;
static drbz_scanner scanner;

static void argument_error(mrb_state *mrb, const char *message) {
    api->mrb_raise(mrb, api->mrb_class_get(mrb, "ArgumentError"), message);
}
#ifdef DRBZ_SQLITE_QUERY
static void runtime_error(mrb_state *mrb, const char *message) {
    api->mrb_raise(mrb, api->mrb_class_get(mrb, "RuntimeError"), message);
}
#endif
static mrb_value square_value(mrb_state *mrb, mrb_value self) {
    (void)self;
    mrb_int input;
    api->mrb_get_args(mrb, "i", &input);
    int output = 0;
    if (input < INT_MIN || input > INT_MAX || drbz_square((int)input, &output) != 0) {
        argument_error(mrb, "square is outside the supported signed 32-bit range");
        return mrb_nil_value();
    }
    return mrb_fixnum_value(output);
}
static mrb_value newlines(mrb_state *mrb, mrb_value self) {
    (void)self;
    char *text;
    mrb_int length;
    api->mrb_get_args(mrb, "s", &text, &length);
    return mrb_fixnum_value((mrb_int)drbz_count_blocked((const unsigned char *)text, (size_t)length));
}
static mrb_value regex_index(mrb_state *mrb, mrb_value self) {
    (void)self;
    char *pattern, *text;
    mrb_int pattern_length, text_length;
    api->mrb_get_args(mrb, "ss", &pattern, &pattern_length, &text, &text_length);
    drbz_regex compiled;
    if (drbz_regex_compile(&compiled, (const unsigned char *)pattern, (size_t)pattern_length) != 0) {
        argument_error(mrb, "invalid or oversized tiny-regex pattern");
        return mrb_nil_value();
    }
    int matched_length = 0;
    int index = drbz_regex_search(&compiled, (const unsigned char *)text, (size_t)text_length, &matched_length, 1, 10000000);
    if (index == -2) {
        argument_error(mrb, "regex work or input-size limit exceeded");
        return mrb_nil_value();
    }
    return mrb_fixnum_value(index);
}
static void view_from_value(mrb_value value, drbz_view *view) {
    *view = (drbz_view){0, 0, NULL, 0};
    if (mrb_fixnum_p(value)) {
        view->kind = 1;
        view->number = (double)mrb_fixnum(value);
    } else if (mrb_float_p(value)) {
        view->kind = 1;
        view->number = (double)mrb_float(value);
    } else if (mrb_array_p(value)) {
        view->kind = 2;
        view->children = RARRAY_PTR(value);
        view->length = (size_t)RARRAY_LEN(value);
    }
}
static void read_value(void *context, const void *source, size_t index, drbz_view *view) {
    (void)context;
    view_from_value(((const mrb_value *)source)[index], view);
}
static size_t read_values(void *context, const void *source, size_t index, drbz_view *views, size_t capacity) {
    (void)context;
    const mrb_value *values = (const mrb_value *)source + index;
    for (size_t i = 0; i < capacity; ++i) view_from_value(values[i], views + i);
    return capacity;
}
static mrb_value sum_values_impl(mrb_state *mrb, int batched) {
    mrb_value *values;
    mrb_int length;
    api->mrb_get_args(mrb, "*", &values, &length);
    double result = 0;
    int status = batched
        ? drbz_sum_tree_batched(values, (size_t)length, read_values, NULL, &result)
        : drbz_sum_tree(values, (size_t)length, read_value, NULL, &result);
    if (status != 0) {
        argument_error(mrb, status == 1 ? "unsupported value in nested sum" : "cyclic or excessively deep array");
        return mrb_nil_value();
    }
    return api->drb_float_value(mrb, result);
}
static mrb_value sum_values(mrb_state *mrb, mrb_value self) { (void)self; return sum_values_impl(mrb, 1); }
#ifdef DRBZ_SUITE_TEST_HOST
static mrb_value sum_values_single(mrb_state *mrb, mrb_value self) { (void)self; return sum_values_impl(mrb, 0); }
typedef struct { const mrb_value *values; size_t length, next; } c_sum_frame;
static int direct_c_sum(const mrb_value *values, size_t length, double *result) {
    c_sum_frame frames[64];
    frames[0] = (c_sum_frame){values, length, 0};
    size_t depth = 1;
    double sum = 0;
    while (depth) {
        c_sum_frame *frame = &frames[depth - 1];
        if (frame->next == frame->length) { --depth; continue; }
        mrb_value value = frame->values[frame->next++];
        if (mrb_fixnum_p(value)) sum += (double)mrb_fixnum(value);
        else if (mrb_float_p(value)) sum += (double)mrb_float(value);
        else if (mrb_array_p(value)) {
            size_t n = (size_t)RARRAY_LEN(value);
            if (!n) continue;
            const mrb_value *children = RARRAY_PTR(value);
            if (depth == 64 || !children) return 2;
            for (size_t i = 0; i < depth; ++i) if (frames[i].values == children) return 2;
            frames[depth++] = (c_sum_frame){children, n, 0};
        } else return 1;
    }
    *result = sum;
    return 0;
}
static mrb_value sum_values_c_direct(mrb_state *mrb, mrb_value self) {
    (void)self;
    mrb_value *values; mrb_int length;
    api->mrb_get_args(mrb, "*", &values, &length);
    double result = 0;
    int status = direct_c_sum(values, (size_t)length, &result);
    if (status != 0) {
        argument_error(mrb, status == 1 ? "unsupported value in nested sum" : "cyclic or excessively deep array");
        return mrb_nil_value();
    }
    return api->drb_float_value(mrb, result);
}
#endif
static mrb_value greeting(mrb_state *mrb, int goodbye) {
    char *name;
    mrb_int length;
    api->mrb_get_args(mrb, "s", &name, &length);
    unsigned char text[512];
    size_t written = 0;
    if (drbz_greeting(goodbye, (const unsigned char *)name, (size_t)length, text, sizeof text, &written) != 0) {
        argument_error(mrb, "greeting exceeds the adapter's 511-byte limit");
        return mrb_nil_value();
    }
    return api->mrb_str_new(mrb, (const char *)text, (mrb_int)written);
}
static mrb_value hello(mrb_state *mrb, mrb_value self) { (void)self; return greeting(mrb, 0); }
static mrb_value goodbye(mrb_state *mrb, mrb_value self) { (void)self; return greeting(mrb, 1); }
static mrb_value reset(mrb_state *mrb, mrb_value self) {
    (void)self;
    api->mrb_get_args(mrb, "");
    drbz_scanner_reset(&scanner);
    return mrb_nil_value();
}
static mrb_value frame(mrb_state *mrb, mrb_value self) {
    (void)self;
    api->mrb_get_args(mrb, "");
    const uint32_t *pixels = drbz_scanner_frame(&scanner);
    api->drb_upload_pixel_array("zig_suite_scanner", 10, 10, pixels);
    return mrb_nil_value();
}

#ifdef DRBZ_SQLITE_QUERY
static sqlite3 *query_db;
static drbz_query_cache query_cache;
static int query_cache_ready;

static unsigned char *copy_c_string(mrb_state *mrb, const char *text, mrb_int length, const char *what) {
    if (length < 0 || memchr(text, 0, (size_t)length) != NULL) {
        char message[160];
        snprintf(message, sizeof message, "%s contains an embedded NUL", what);
        argument_error(mrb, message);
        return NULL;
    }
    unsigned char *copy = sqlite3_malloc64((sqlite3_uint64)length + 1);
    if (!copy) {
        runtime_error(mrb, "SQLite string allocation failed");
        return NULL;
    }
    if (length) memcpy(copy, text, (size_t)length);
    copy[length] = 0;
    return copy;
}
static void raise_db_error(mrb_state *mrb, const char *prefix, sqlite3 *db) {
    char message[512];
    snprintf(message, sizeof message, "%s: %s", prefix, db ? sqlite3_errmsg(db) : "no database");
    runtime_error(mrb, message);
}
static mrb_value sqlite_open_value(mrb_state *mrb, mrb_value self) {
    (void)self;
    char *path; mrb_int length;
    api->mrb_get_args(mrb, "s", &path, &length);
    unsigned char *copy = copy_c_string(mrb, path, length, "database path");
    if (!copy) return mrb_nil_value();
    sqlite3 *next = NULL;
    int rc = sqlite3_open((const char *)copy, &next);
    sqlite3_free(copy);
    if (rc != SQLITE_OK) {
        char message[512];
        snprintf(message, sizeof message, "SQLite open failed: %s", next ? sqlite3_errmsg(next) : "allocation failure");
        if (next) sqlite3_close(next);
        runtime_error(mrb, message);
        return mrb_nil_value();
    }
    if (query_db) {
        int clear_rc = query_cache_ready ? drbz_query_cache_clear(&query_cache) : SQLITE_OK;
        int close_rc = sqlite3_close(query_db);
        query_db = NULL; query_cache_ready = 0;
        if (clear_rc != SQLITE_OK || close_rc != SQLITE_OK) {
            sqlite3_close(next);
            runtime_error(mrb, "failed to close previous SQLite database");
            return mrb_nil_value();
        }
    }
    query_db = next;
    drbz_query_cache_init(&query_cache, query_db);
    query_cache_ready = 1;
    return mrb_nil_value();
}
static mrb_value sqlite_exec_value(mrb_state *mrb, mrb_value self) {
    (void)self;
    if (!query_db) { runtime_error(mrb, "call sqlite_open before sqlite_exec"); return mrb_nil_value(); }
    char *sql; mrb_int length;
    api->mrb_get_args(mrb, "s", &sql, &length);
    unsigned char *copy = copy_c_string(mrb, sql, length, "SQL");
    if (!copy) return mrb_nil_value();
    int rc = sqlite3_exec(query_db, (const char *)copy, NULL, NULL, NULL);
    sqlite3_free(copy);
    if (rc != SQLITE_OK) { raise_db_error(mrb, "SQLite exec failed", query_db); return mrb_nil_value(); }
    return mrb_nil_value();
}
static mrb_value query_json_value(mrb_state *mrb, mrb_value self) {
    (void)self;
    if (!query_db || !query_cache_ready) { runtime_error(mrb, "call sqlite_open before query_json"); return mrb_nil_value(); }
    char *sql; mrb_int length;
    api->mrb_get_args(mrb, "s", &sql, &length);
    if (length < 0) { argument_error(mrb, "negative SQL length"); return mrb_nil_value(); }
    drbz_query_result result = drbz_query_pack(&query_cache, (const unsigned char *)sql, (size_t)length);
    if (result.code != SQLITE_OK) { raise_db_error(mrb, "SQLite query failed", query_db); return mrb_nil_value(); }
    const unsigned char *packed = NULL; size_t packed_length = 0, rows = 0;
    drbz_query_output(&query_cache, &packed, &packed_length, &rows);
    mrb_value array = api->mrb_ary_new(mrb);
    size_t position = 0;
    for (size_t row = 0; row < rows; ++row) {
        if (packed_length - position < sizeof(uint64_t)) { runtime_error(mrb, "invalid packed query result"); return mrb_nil_value(); }
        uint64_t row_length; memcpy(&row_length, packed + position, sizeof row_length); position += sizeof row_length;
        mrb_value value;
        if (row_length == UINT64_MAX) value = api->mrb_str_new(mrb, "null", 4);
        else {
            if (row_length > (uint64_t)(packed_length - position) || row_length > (uint64_t)MRB_INT_MAX) { runtime_error(mrb, "invalid packed query row length"); return mrb_nil_value(); }
            value = api->mrb_str_new(mrb, (const char *)(packed + position), (mrb_int)row_length);
            position += (size_t)row_length;
        }
        api->mrb_ary_push(mrb, array, value);
    }
    if (position != packed_length) { runtime_error(mrb, "trailing packed query bytes"); return mrb_nil_value(); }
    return array;
}
static mrb_value sqlite_close_value(mrb_state *mrb, mrb_value self) {
    (void)self; api->mrb_get_args(mrb, "");
    if (!query_db) return mrb_nil_value();
    int clear_rc = query_cache_ready ? drbz_query_cache_clear(&query_cache) : SQLITE_OK;
    int close_rc = sqlite3_close(query_db);
    if (close_rc == SQLITE_OK) { query_db = NULL; query_cache_ready = 0; }
    if (clear_rc != SQLITE_OK || close_rc != SQLITE_OK) { runtime_error(mrb, "SQLite close failed"); return mrb_nil_value(); }
    return mrb_nil_value();
}
static mrb_value query_prepares_value(mrb_state *mrb, mrb_value self) { (void)self; api->mrb_get_args(mrb, ""); return mrb_fixnum_value((mrb_int)query_cache.prepares); }
static mrb_value query_hits_value(mrb_state *mrb, mrb_value self) { (void)self; api->mrb_get_args(mrb, ""); return mrb_fixnum_value((mrb_int)query_cache.hits); }
#endif

DRB_FFI_EXPORT void drb_register_c_extensions(mrb_state *mrb, drb_api_t *host) {
    api = host; drbz_scanner_reset(&scanner);
    struct RClass *ffi = api->mrb_module_get(mrb, "FFI");
    struct RClass *zig = api->mrb_define_module_under(mrb, ffi, "Zig");
    api->mrb_define_module_function(mrb, zig, "square", square_value, MRB_ARGS_REQ(1));
    api->mrb_define_module_function(mrb, zig, "count_newlines", newlines, MRB_ARGS_REQ(1));
    api->mrb_define_module_function(mrb, zig, "regex_index", regex_index, MRB_ARGS_REQ(2));
    api->mrb_define_module_function(mrb, zig, "sum", sum_values, MRB_ARGS_ANY());
#ifdef DRBZ_SUITE_TEST_HOST
    api->mrb_define_module_function(mrb, zig, "sum_single_reader", sum_values_single, MRB_ARGS_ANY());
    api->mrb_define_module_function(mrb, zig, "sum_c_direct", sum_values_c_direct, MRB_ARGS_ANY());
#endif
    api->mrb_define_module_function(mrb, zig, "hello", hello, MRB_ARGS_REQ(1));
    api->mrb_define_module_function(mrb, zig, "goodbye", goodbye, MRB_ARGS_REQ(1));
    api->mrb_define_module_function(mrb, zig, "reset_scanner", reset, MRB_ARGS_NONE());
    api->mrb_define_module_function(mrb, zig, "update_scanner_texture", frame, MRB_ARGS_NONE());
#ifdef DRBZ_SQLITE_QUERY
    api->mrb_define_module_function(mrb, zig, "sqlite_open", sqlite_open_value, MRB_ARGS_REQ(1));
    api->mrb_define_module_function(mrb, zig, "sqlite_exec", sqlite_exec_value, MRB_ARGS_REQ(1));
    api->mrb_define_module_function(mrb, zig, "query_json", query_json_value, MRB_ARGS_REQ(1));
    api->mrb_define_module_function(mrb, zig, "sqlite_close", sqlite_close_value, MRB_ARGS_NONE());
    api->mrb_define_module_function(mrb, zig, "query_prepares", query_prepares_value, MRB_ARGS_NONE());
    api->mrb_define_module_function(mrb, zig, "query_hits", query_hits_value, MRB_ARGS_NONE());
#endif
}
