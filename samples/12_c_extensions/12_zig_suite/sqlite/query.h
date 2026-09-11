#ifndef DRBZ_QUERY_H
#define DRBZ_QUERY_H
#include <stddef.h>
#include <stdint.h>
struct sqlite3;
struct sqlite3_stmt;
typedef struct {
    struct sqlite3 *db;
    struct sqlite3_stmt *stmt;
    unsigned char *key;
    size_t key_len;
    unsigned char *buffer;
    size_t capacity;
    size_t output_len;
    size_t rows;
    uint64_t prepares;
    uint64_t hits;
    int valid;
} drbz_query_cache;
typedef struct { int code, reset_code; size_t rows; int cache_hit; } drbz_query_result;
/* The cache owns its prepared statement, SQL-key copy and reusable packed row
 * buffer through SQLite's allocator. query_pack compiles only the first SQL
 * statement, matching the original sample. Packed rows are native-endian u64
 * lengths followed by bytes; UINT64_MAX denotes SQL NULL. Output is borrowed
 * until the next query or clear. No Ruby callback runs while SQLite is stepped. */
void drbz_query_cache_init(drbz_query_cache *, struct sqlite3 *);
int drbz_query_cache_clear(drbz_query_cache *);
drbz_query_result drbz_query_pack(drbz_query_cache *, const unsigned char *, size_t);
void drbz_query_output(const drbz_query_cache *, const unsigned char **, size_t *, size_t *);
#endif
