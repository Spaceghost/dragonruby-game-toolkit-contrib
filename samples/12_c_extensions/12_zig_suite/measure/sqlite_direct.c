/* Independent direct-C control for sqlite/series.zig. Compile separately with
 * the same Zig/Clang target and optimization. Checks, reset policy, parameter
 * stream, row consumption and error precedence match the direct Zig path. */
#include <sqlite3.h>
#include <stdint.h>
#include <stddef.h>

static inline int consume(sqlite3_stmt *stmt, uint64_t *sum) {
    if (sqlite3_column_type(stmt, 0) == SQLITE_NULL) return SQLITE_MISMATCH;
    const unsigned char *bytes = sqlite3_column_text(stmt, 0);
    if (!bytes) return SQLITE_NOMEM;
    int count = sqlite3_column_bytes(stmt, 0);
    for (size_t i = 0; i < (size_t)count; ++i) *sum += bytes[i];
    return SQLITE_OK;
}
static inline int execute(sqlite3_stmt *stmt, size_t count, uint32_t *value, uint64_t *sum, int clear) {
    if (sqlite3_stmt_busy(stmt)) return SQLITE_MISUSE;
    if (sqlite3_bind_parameter_count(stmt) != 1) return SQLITE_RANGE;
    for (size_t i = 0; i < count; ++i) {
        sqlite3_int64 parameter = *value & 1023;
        ++*value;
        int code = sqlite3_bind_int64(stmt, 1, parameter);
        if (code == SQLITE_OK) {
            for (;;) {
                code = sqlite3_step(stmt);
                if (code == SQLITE_DONE) { code = SQLITE_OK; break; }
                if (code != SQLITE_ROW) break;
                code = consume(stmt, sum);
                if (code != SQLITE_OK) break;
            }
        }
        int reset = sqlite3_reset(stmt);
        int cleared = clear ? sqlite3_clear_bindings(stmt) : SQLITE_OK;
        if (code != SQLITE_OK) return code;
        if (reset != SQLITE_OK) return reset;
        if (cleared != SQLITE_OK) return cleared;
    }
    return SQLITE_OK;
}
static inline __attribute__((always_inline)) int series(sqlite3 *db, const char *sql, size_t count, uint32_t value, int reuse, int clear, uint64_t *output) {
    uint64_t sum = 0;
    size_t left = count;
    while (left) {
        sqlite3_stmt *stmt = NULL;
        int code = sqlite3_prepare_v3(db, sql, -1, reuse ? SQLITE_PREPARE_PERSISTENT : 0, &stmt, NULL);
        if (code != SQLITE_OK) { (void)sqlite3_finalize(stmt); return code; }
        if (!stmt) return SQLITE_MISUSE;
        size_t n = reuse ? left : 1;
        code = execute(stmt, n, &value, &sum, clear);
        int finalized = sqlite3_finalize(stmt);
        if (code != SQLITE_OK) return code;
        if (finalized != SQLITE_OK) return finalized;
        left -= n;
    }
    *output = sum;
    return SQLITE_OK;
}
int drbz_c_direct_sql_series(sqlite3 *db, const char *sql, size_t count, uint32_t seed, int reuse, int clear, uint64_t *output) {
    if (!db) return SQLITE_MISUSE;
    if (reuse) return clear ? series(db, sql, count, seed, 1, 1, output) : series(db, sql, count, seed, 1, 0, output);
    return clear ? series(db, sql, count, seed, 0, 1, output) : series(db, sql, count, seed, 0, 0, output);
}
