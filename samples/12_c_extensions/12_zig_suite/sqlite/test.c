#include "sqlite.h"
#include <sqlite3.h>
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static uint64_t now(void) {
    struct timespec t;
    assert(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}
static void fail_query(sqlite3_context *context, int argc, sqlite3_value **argv) {
    (void)argc; (void)argv;
    sqlite3_result_error(context, "deliberate step failure", -1);
}
static void validate(void) {
    drbz_database db;
    drbz_statement statement;
    drbz_sql_init(&db);
    drbz_sql_statement_init(&statement);
    assert(drbz_sql_close(&db) == SQLITE_OK);
    assert(drbz_sql_open(&db, "/dev/null/not-a-database") != SQLITE_OK);
    assert(db.handle == NULL && db.message[0] != 0);
    assert(drbz_sql_open(&db, ":memory:") == SQLITE_OK);
    assert(drbz_sql_open(&db, ":memory:") == SQLITE_MISUSE);
    assert(drbz_sql_exec(&db, "CREATE TABLE t(id PRIMARY KEY); INSERT INTO t VALUES(1)") == SQLITE_OK);
    assert(drbz_sql_exec(&db, "INSERT INTO t VALUES(1)") == SQLITE_CONSTRAINT);
    assert(strstr(db.message, "UNIQUE") != NULL);
    assert(drbz_sql_prepare(&db, &statement, "invalid sql", 0) != SQLITE_OK);
    assert(statement.handle == NULL);
    assert(sqlite3_create_function(db.handle, "fail_query", 0, SQLITE_UTF8, NULL, fail_query, NULL, NULL) == SQLITE_OK);
    assert(drbz_sql_prepare(&db, &statement, "SELECT fail_query()", 0) == SQLITE_OK);
    assert(drbz_sql_step(&statement) == SQLITE_ERROR);
    assert(strstr(db.message, "deliberate step failure") != NULL);
    assert(drbz_sql_finalize(&statement) == SQLITE_ERROR);
    assert(statement.handle == NULL && sqlite3_next_stmt(db.handle, NULL) == NULL);
    assert(drbz_sql_prepare(&db, &statement, "SELECT NULL UNION ALL SELECT '' UNION ALL SELECT CAST(x'610062' AS TEXT)", 1) == SQLITE_OK);
    assert(drbz_sql_close(&db) == SQLITE_BUSY && db.handle != NULL);
    drbz_text text;
    assert(drbz_sql_first_text(&statement, &text) == SQLITE_MISUSE);
    assert(drbz_sql_step(&statement) == SQLITE_ROW);
    assert(drbz_sql_first_text(&statement, &text) == SQLITE_OK && text.is_null && text.length == 0);
    assert(drbz_sql_step(&statement) == SQLITE_ROW);
    assert(drbz_sql_first_text(&statement, &text) == SQLITE_OK && !text.is_null && text.length == 0);
    assert(drbz_sql_step(&statement) == SQLITE_ROW);
    assert(drbz_sql_first_text(&statement, &text) == SQLITE_OK && text.length == 3 && memcmp(text.data, "a\0b", 3) == 0);
    assert(drbz_sql_step(&statement) == SQLITE_DONE);
    assert(drbz_sql_step(&statement) == SQLITE_MISUSE);
    assert(drbz_sql_reset(&statement, 1) == SQLITE_OK);
    assert(drbz_sql_step(&statement) == SQLITE_ROW);
    assert(drbz_sql_finalize(&statement) == SQLITE_OK);
    assert(drbz_sql_finalize(&statement) == SQLITE_OK);
    assert(sqlite3_next_stmt(db.handle, NULL) == NULL);
    assert(drbz_sql_close(&db) == SQLITE_OK && db.handle == NULL);
    puts("SQLITE_PROOF {\"failed_open_closed\":true,\"step_errors_checked\":true,\"busy_close_preserved\":true,\"outstanding_statements\":0}");
}
static uint64_t run(drbz_database *db, int variant, unsigned repetitions) {
    const char *sql = "SELECT CAST(?1 AS TEXT)";
    uint64_t checksum = 0;
    drbz_statement statement;
    sqlite3_stmt *original = NULL;
    drbz_sql_statement_init(&statement);
    if (variant == 2) assert(drbz_sql_prepare(db, &statement, sql, 1) == SQLITE_OK);
    if (variant == 3) assert(sqlite3_prepare_v3(db->handle, sql, -1, SQLITE_PREPARE_PERSISTENT, &original, NULL) == SQLITE_OK);
    for (unsigned i = 0; i < repetitions; ++i) {
        if (variant == 0 || variant == 3) {
            // Match the wrapper's prepare API and flags, including reuse's
            // persistent hint. These are controls, not original sample code.
            if (variant == 0) assert(sqlite3_prepare_v3(db->handle, sql, -1, 0, &original, NULL) == SQLITE_OK);
            assert(sqlite3_bind_int64(original, 1, i) == SQLITE_OK);
            assert(sqlite3_step(original) == SQLITE_ROW);
            assert(sqlite3_column_type(original, 0) != SQLITE_NULL);
            const unsigned char *value = sqlite3_column_text(original, 0);
            assert(value);
            int length = sqlite3_column_bytes(original, 0);
            for (int j = 0; j < length; ++j) checksum += value[j];
            assert(sqlite3_step(original) == SQLITE_DONE);
            if (variant == 0) {
                assert(sqlite3_finalize(original) == SQLITE_OK);
                original = NULL;
            } else {
                // Reset alone retains bindings. Both reuse variants clear
                // them and check both return codes on every iteration.
                assert(sqlite3_reset(original) == SQLITE_OK);
                assert(sqlite3_clear_bindings(original) == SQLITE_OK);
            }
        } else {
            if (variant == 1) assert(drbz_sql_prepare(db, &statement, sql, 0) == SQLITE_OK);
            assert(drbz_sql_bind_int(&statement, 1, i) == SQLITE_OK);
            assert(drbz_sql_step(&statement) == SQLITE_ROW);
            drbz_text text;
            assert(drbz_sql_first_text(&statement, &text) == SQLITE_OK);
            assert(!text.is_null);
            for (size_t j = 0; j < text.length; ++j) checksum += text.data[j];
            assert(drbz_sql_step(&statement) == SQLITE_DONE);
            if (variant == 1) assert(drbz_sql_finalize(&statement) == SQLITE_OK);
            else assert(drbz_sql_reset(&statement, 1) == SQLITE_OK);
        }
    }
    if (variant == 2) assert(drbz_sql_finalize(&statement) == SQLITE_OK);
    if (variant == 3) assert(sqlite3_finalize(original) == SQLITE_OK);
    return checksum;
}
int main(void) {
    validate();
    drbz_database db;
    drbz_sql_init(&db);
    assert(drbz_sql_open(&db, ":memory:") == SQLITE_OK);
    const char *names[] = {"c_control", "zig_prepare_each", "zig_reuse", "c_reuse"};
    const unsigned iterations = 10000;
    uint64_t expected = run(&db, 0, iterations);
    for (int variant = 1; variant < 4; ++variant) assert(run(&db, variant, iterations) == expected);
    printf("SQLITE_ENV {\"version\":\"%s\",\"source_id\":\"%s\",\"scope\":\"in-memory native statement lifecycle, not Ruby or disk I/O\"}\n", sqlite3_libversion(), sqlite3_sourceid());
    for (int trial = 0; trial < 11; ++trial) {
        for (int slot = 0; slot < 4; ++slot) {
            int variant = (trial + slot) % 4;
            uint64_t start = now();
            uint64_t checksum = run(&db, variant, iterations);
            uint64_t elapsed = now() - start;
            assert(checksum == expected);
            printf("BENCH {\"case\":\"sqlite/repeated-query\",\"variant\":\"%s\",\"size\":1,\"iterations\":%u,\"trial\":%d,\"nanoseconds\":%" PRIu64 ",\"checksum\":%" PRIu64 ",\"allocation_count\":null}\n", names[variant], iterations, trial, elapsed, checksum);
        }
    }
    assert(sqlite3_next_stmt(db.handle, NULL) == NULL);
    assert(drbz_sql_close(&db) == SQLITE_OK);
    return 0;
}
