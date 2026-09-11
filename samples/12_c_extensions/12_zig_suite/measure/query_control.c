#include "../sqlite/query.h"
#include <sqlite3.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int same_key(const drbz_query_cache *cache, const unsigned char *sql, size_t length) {
    return cache->valid && cache->key && cache->key_len == length && !memcmp(cache->key, sql, length);
}
static int reserve(drbz_query_cache *cache, size_t needed) {
    if (needed <= cache->capacity) return SQLITE_OK;
    size_t next = cache->capacity ? cache->capacity : 256;
    while (next < needed) {
        if (next > SIZE_MAX / 2) { next = needed; break; }
        next *= 2;
    }
    void *p = sqlite3_realloc64(cache->buffer, (sqlite3_uint64)next);
    if (!p) return SQLITE_NOMEM;
    cache->buffer = p; cache->capacity = next;
    return SQLITE_OK;
}
static int append_first(drbz_query_cache *cache, sqlite3_stmt *stmt) {
    uint64_t header = UINT64_MAX;
    const unsigned char *bytes = NULL;
    size_t length = 0;
    if (sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
        bytes = sqlite3_column_text(stmt, 0);
        if (!bytes) return SQLITE_NOMEM;
        int n = sqlite3_column_bytes(stmt, 0);
        if (n < 0) return SQLITE_TOOBIG;
        length = (size_t)n; header = (uint64_t)length;
    }
    if (cache->output_len > SIZE_MAX - sizeof header || cache->output_len + sizeof header > SIZE_MAX - length) return SQLITE_TOOBIG;
    size_t total = cache->output_len + sizeof header + length;
    int rc = reserve(cache, total); if (rc != SQLITE_OK) return rc;
    memcpy(cache->buffer + cache->output_len, &header, sizeof header); cache->output_len += sizeof header;
    if (length) { memcpy(cache->buffer + cache->output_len, bytes, length); cache->output_len += length; }
    ++cache->rows;
    return SQLITE_OK;
}
static drbz_query_result run_stmt(drbz_query_cache *cache, sqlite3_stmt *stmt, int hit, int finalize) {
    drbz_query_result result = {SQLITE_OK, SQLITE_OK, 0, hit};
    for (;;) {
        int rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE) break;
        if (rc != SQLITE_ROW) { result.code = rc; break; }
        result.code = append_first(cache, stmt);
        if (result.code != SQLITE_OK) break;
    }
    result.rows = cache->rows;
    if (finalize) {
        result.reset_code = sqlite3_finalize(stmt);
    } else {
        result.reset_code = sqlite3_reset(stmt);
    }
    if (result.code == SQLITE_OK && result.reset_code != SQLITE_OK) result.code = result.reset_code;
    if (result.code != SQLITE_OK) { cache->output_len = 0; cache->rows = 0; }
    return result;
}
void drbc_query_cache_init(drbz_query_cache *cache, sqlite3 *db) {
    *cache = (drbz_query_cache){.db=db};
}
int drbc_query_cache_clear(drbz_query_cache *cache) {
    int rc = cache->stmt ? sqlite3_finalize(cache->stmt) : SQLITE_OK;
    sqlite3_free(cache->key); sqlite3_free(cache->buffer);
    sqlite3 *db = cache->db; uint64_t prepares = cache->prepares, hits = cache->hits;
    *cache = (drbz_query_cache){.db=db,.prepares=prepares,.hits=hits};
    return rc;
}
static int prepare_cached(drbz_query_cache *cache, const unsigned char *sql, size_t length, int *hit) {
    *hit = 0;
    if (!cache->db || length > INT_MAX || memchr(sql, 0, length)) return length > INT_MAX ? SQLITE_TOOBIG : SQLITE_MISUSE;
    if (cache->stmt && sqlite3_stmt_busy(cache->stmt)) return SQLITE_MISUSE;
    if (same_key(cache, sql, length)) { ++cache->hits; *hit = 1; return SQLITE_OK; }
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v3(cache->db, (const char *)sql, (int)length, SQLITE_PREPARE_PERSISTENT, &stmt, NULL);
    if (rc != SQLITE_OK) return rc;
    unsigned char *key = sqlite3_malloc64(length ? (sqlite3_uint64)length : 1);
    if (!key) { sqlite3_finalize(stmt); return SQLITE_NOMEM; }
    if (length) memcpy(key, sql, length);
    sqlite3_stmt *old_stmt = cache->stmt; unsigned char *old_key = cache->key;
    cache->stmt = stmt; cache->key = key; cache->key_len = length; cache->valid = 1; ++cache->prepares;
    int cleanup = old_stmt ? sqlite3_finalize(old_stmt) : SQLITE_OK;
    sqlite3_free(old_key);
    return cleanup;
}
drbz_query_result drbc_query_pack(drbz_query_cache *cache, const unsigned char *sql, size_t length) {
    cache->output_len = 0; cache->rows = 0;
    int hit = 0, rc = prepare_cached(cache, sql, length, &hit);
    if (rc != SQLITE_OK) return (drbz_query_result){rc,SQLITE_OK,0,hit};
    if (!cache->stmt) return (drbz_query_result){SQLITE_OK,SQLITE_OK,0,hit};
    return run_stmt(cache, cache->stmt, hit, 0);
}
drbz_query_result drbc_query_pack_each(drbz_query_cache *cache, const unsigned char *sql, size_t length) {
    cache->output_len = 0; cache->rows = 0;
    if (!cache->db || length > INT_MAX || memchr(sql,0,length)) return (drbz_query_result){length > INT_MAX ? SQLITE_TOOBIG : SQLITE_MISUSE,SQLITE_OK,0,0};
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(cache->db, (const char *)sql, (int)length, &stmt, NULL);
    if (rc != SQLITE_OK) return (drbz_query_result){rc,SQLITE_OK,0,0};
    ++cache->prepares;
    if (!stmt) return (drbz_query_result){SQLITE_OK,SQLITE_OK,0,0};
    return run_stmt(cache, stmt, 0, 1);
}
