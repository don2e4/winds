#include "sqlite3.h"
#include <stdio.h>
#include <string.h>

static int saw_42;

static int check_value(void *unused, int count, char **values, char **names) {
    (void)unused;
    (void)names;
    saw_42 = count == 1 && values[0] && strcmp(values[0], "42") == 0;
    return 0;
}

int main(void) {
    sqlite3 *db = 0;
    char *error = 0;
    if (sqlite3_open(":memory:", &db) != SQLITE_OK) return 1;
    int rc = sqlite3_exec(db,
        "CREATE TABLE t(value); INSERT INTO t VALUES(42); SELECT value FROM t;",
        check_value, 0, &error);
    if (rc != SQLITE_OK) fprintf(stderr, "SQLite query failed (%d): %s\n", rc, error ? error : sqlite3_errmsg(db));
    else if (!saw_42) fprintf(stderr, "SQLite query did not return 42\n");
    sqlite3_free(error);
    sqlite3_close(db);
    return rc != SQLITE_OK || !saw_42;
}
