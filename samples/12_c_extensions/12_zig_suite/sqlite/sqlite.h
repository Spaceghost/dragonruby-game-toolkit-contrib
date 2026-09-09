#ifndef DRBZ_SQLITE_H
#define DRBZ_SQLITE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct sqlite3;
struct sqlite3_stmt;
typedef struct { struct sqlite3 *handle; char message[256]; } drbz_database;
typedef struct { struct sqlite3_stmt *handle; drbz_database *owner; int state; } drbz_statement;
typedef struct { const unsigned char *data; size_t length; int is_null; } drbz_text;
/* Initialize exactly once before first use. Owners and their addresses must
 * outlive statements. Serialize access to each database and statement.
 * SQLite allocates internally; this wrapper does not claim a zero-allocation
 * database. message retains the last error, truncated and NUL terminated. */
void drbz_sql_init(drbz_database *);
int drbz_sql_open(drbz_database *, const char *path);
/* Outstanding statements cause SQLITE_BUSY and leave the connection open. */
int drbz_sql_close(drbz_database *);
int drbz_sql_exec(drbz_database *, const char *sql);
void drbz_sql_statement_init(drbz_statement *);
/* Like the original query sample, prepare compiles the first SQL statement.
 * exec runs all statements. Neither interface is a SQL safety validator.
 * persistent requests SQLITE_PREPARE_PERSISTENT; reset enables actual reuse. */
int drbz_sql_prepare(drbz_database *, drbz_statement *, const char *sql, int persistent);
int drbz_sql_bind_int(drbz_statement *, int index, int64_t value);
/* SQLITE_ROW or SQLITE_DONE, otherwise an error that must be checked.
 * Explicit reset is required after DONE or a failed step. */
int drbz_sql_step(drbz_statement *);
/* Borrowed column 0, valid only at ROW and until next step/reset/finalize.
 * SQL NULL is distinguished from empty text. Embedded NULs retain length. */
int drbz_sql_first_text(drbz_statement *, drbz_text *);
int drbz_sql_reset(drbz_statement *, int clear_bindings);
/* Finalize always releases its statement, even when returning an error. */
int drbz_sql_finalize(drbz_statement *);
#ifdef __cplusplus
}
#endif
#endif
