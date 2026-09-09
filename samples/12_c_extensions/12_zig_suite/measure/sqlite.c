#include "bench.h"
#include "sqlite.h"
#include <sqlite3.h>

static drbz_database database;
static uint32_t parameter_seed;
#ifdef DRBZ_PROFILE
static sqlite3_mem_methods underlying;
static meter_stats statistics;
static uint64_t live;
static int active, fail_next;
static void update_peak(void) {
    if (active && live > statistics.peak_live) statistics.peak_live = live;
}
static void *sql_malloc(int n) {
    void *p = underlying.xMalloc(n);
    if (p) live += (uint64_t)underlying.xSize(p);
    if (active) { ++statistics.malloc_calls; statistics.requested_bytes += (unsigned)n; statistics.failed_calls += p == NULL; update_peak(); }
    return p;
}
static void sql_free(void *p) {
    if (p) {
        uint64_t n = (uint64_t)underlying.xSize(p); assert(live >= n); live -= n;
        if (active) ++statistics.free_calls;
    }
    underlying.xFree(p);
}
static void *sql_realloc(void *p, int n) {
    uint64_t old = p ? (uint64_t)underlying.xSize(p) : 0;
    void *next;
    if (active && fail_next) { fail_next = 0; next = NULL; }
    else next = underlying.xRealloc(p, n);
    if (next) { assert(live >= old); live -= old; live += (uint64_t)underlying.xSize(next); }
    if (active) { ++statistics.realloc_calls; statistics.requested_bytes += (unsigned)n; statistics.failed_calls += next == NULL; update_peak(); }
    return next;
}
void meter_begin(void) {
    assert(!active); memset(&statistics, 0, sizeof statistics);
    statistics.live_before = statistics.peak_live = live; statistics.live_supported = 1; active = 1;
}
meter_stats meter_end(void) { assert(active); active = 0; statistics.live_after = live; return statistics; }
void meter_inject(void) { void *p = sqlite3_malloc(17); assert(p); sqlite3_free(p); }
void meter_selftest(void) {
    meter_begin();
    unsigned char *p = sqlite3_malloc(17); assert(p); memset(p, 3, 17);
    p = sqlite3_realloc(p, 64); assert(p && p[16] == 3);
    fail_next = 1; void *failed = sqlite3_realloc(p, 128); assert(!failed && p[16] == 3);
    sqlite3_free(p);
    meter_stats s = meter_end();
    assert(s.malloc_calls == 1 && s.realloc_calls == 2 && s.free_calls == 1 && s.failed_calls == 1);
    assert(s.live_before == s.live_after && s.peak_live > s.live_before);
}
static void install_meter(void) {
    assert(sqlite3_shutdown() == SQLITE_OK);
    assert(sqlite3_config(SQLITE_CONFIG_GETMALLOC, &underlying) == SQLITE_OK);
    sqlite3_mem_methods wrapped = underlying;
    wrapped.xMalloc = sql_malloc; wrapped.xRealloc = sql_realloc; wrapped.xFree = sql_free;
    assert(sqlite3_config(SQLITE_CONFIG_MALLOC, &wrapped) == SQLITE_OK);
}
#endif
static void reset(const bench_case *c, uint32_t seed) { (void)c; parameter_seed = seed; }
static uint64_t batch(const bench_case *c, unsigned variant, size_t n) {
    (void)c;
    const char *sql = "SELECT CAST(?1 AS TEXT)";
    int reuse = variant >= 2, zig = variant % 2;
    sqlite3_stmt *cs = NULL;
    drbz_statement zs; drbz_sql_statement_init(&zs);
    uint64_t checksum = 0;
    if (reuse) {
        if (zig) assert(drbz_sql_prepare(&database, &zs, sql, 1) == SQLITE_OK);
        else assert(sqlite3_prepare_v3(database.handle, sql, -1, SQLITE_PREPARE_PERSISTENT, &cs, NULL) == SQLITE_OK);
    }
    for (size_t i = 0; i < n; ++i) {
        sqlite3_int64 value = (sqlite3_int64)((i + parameter_seed) & 1023);
        if (zig) {
            if (!reuse) assert(drbz_sql_prepare(&database, &zs, sql, 0) == SQLITE_OK);
            assert(drbz_sql_bind_int(&zs, 1, value) == SQLITE_OK);
            assert(drbz_sql_step(&zs) == SQLITE_ROW);
            drbz_text text; assert(drbz_sql_first_text(&zs, &text) == SQLITE_OK && !text.is_null);
            for (size_t j = 0; j < text.length; ++j) checksum += text.data[j];
            assert(drbz_sql_step(&zs) == SQLITE_DONE);
            if (reuse) assert(drbz_sql_reset(&zs, 1) == SQLITE_OK);
            else assert(drbz_sql_finalize(&zs) == SQLITE_OK);
        } else {
            if (!reuse) assert(sqlite3_prepare_v3(database.handle, sql, -1, 0, &cs, NULL) == SQLITE_OK);
            assert(sqlite3_bind_int64(cs, 1, value) == SQLITE_OK);
            assert(sqlite3_step(cs) == SQLITE_ROW);
            const unsigned char *p = sqlite3_column_text(cs, 0); assert(p);
            int length = sqlite3_column_bytes(cs, 0);
            for (int j = 0; j < length; ++j) checksum += p[j];
            assert(sqlite3_step(cs) == SQLITE_DONE);
            if (reuse) { assert(sqlite3_reset(cs) == SQLITE_OK); assert(sqlite3_clear_bindings(cs) == SQLITE_OK); }
            else { assert(sqlite3_finalize(cs) == SQLITE_OK); cs = NULL; }
        }
    }
    if (reuse) {
        if (zig) assert(drbz_sql_finalize(&zs) == SQLITE_OK);
        else assert(sqlite3_finalize(cs) == SQLITE_OK);
    }
    assert(sqlite3_next_stmt(database.handle, NULL) == NULL);
    return checksum;
}
int main(int argc, char **argv) {
#ifdef DRBZ_PROFILE
    install_meter(); meter_begin();
#endif
    drbz_sql_init(&database); assert(drbz_sql_open(&database, ":memory:") == SQLITE_OK);
#ifdef DRBZ_PROFILE
    bench_alloc_record("sqlite-xMalloc", "sqlite/connection", "shared_engine", "open", 1, 0, meter_end());
#endif
    printf("{\"event\":\"sqlite_environment\",\"version\":\"%s\",\"source_id\":\"%s\",\"threadsafe\":%d,\"lookaside_omitted\":%s,\"live_units\":\"underlying-xSize-bytes\"}\n",
        sqlite3_libversion(), sqlite3_sourceid(), sqlite3_threadsafe(), sqlite3_compileoption_used("OMIT_LOOKASIDE") ? "true" : "false");
    const bench_case cases[] = {{"sqlite/prepare-reuse", {"c_each","zig_each","c_reuse","zig_reuse"}, 4, 1, 0, 0, reset, batch, NULL}};
    bench_run(cases, 1, "sqlite-xMalloc", 0, argc, argv);
#ifdef DRBZ_PROFILE
    meter_begin();
#endif
    assert(drbz_sql_close(&database) == SQLITE_OK);
    assert(sqlite3_shutdown() == SQLITE_OK);
#ifdef DRBZ_PROFILE
    meter_stats final = meter_end(); assert(final.live_after == 0);
    bench_alloc_record("sqlite-xMalloc", "sqlite/connection", "shared_engine", "close-and-shutdown", 1, 0, final);
    assert(sqlite3_config(SQLITE_CONFIG_MALLOC, &underlying) == SQLITE_OK);
#endif
    return 0;
}
