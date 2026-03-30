#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/flexql.h"

/* ── Test counters ───────────────────────────────────────── */
static int passed = 0;
static int failed = 0;

#define EXPECT_OK(rc, label) \
    if ((rc) == FLEXQL_OK) { printf("  [PASS] %s\n", label); passed++; } \
    else { printf("  [FAIL] %s\n", label); failed++; }

#define EXPECT_ERR(rc, label) \
    if ((rc) == FLEXQL_ERROR) { printf("  [PASS] %s\n", label); passed++; } \
    else { printf("  [FAIL] %s (expected ERROR)\n", label); failed++; }

/* ── Shared callback state ───────────────────────────────── */
static int rowCount = 0;
static char lastVal[256] = {0};
static char lastCol[256] = {0};

static int countRows(void *data, int n, char **vals, char **cols) {
    (void)data;
    rowCount++;
    if (n > 0) {
        strncpy(lastCol, cols[0], 255);
        strncpy(lastVal, vals[0] ? vals[0] : "NULL", 255);
    }
    return 0;
}


/* Callback that aborts after first row */
static int abortAfterFirst(void *data, int n, char **vals, char **cols) {
    (void)data; (void)n; (void)vals; (void)cols;
    rowCount++;
    return 1; /* abort */
}

int main() {
    FlexQL *db = NULL;
    char *err  = NULL;
    int rc;

    printf("\n========================================\n");
    printf("  FlexQL Full Requirements Test Suite\n");
    printf("========================================\n\n");

    /* ── 1. CONNECTION ───────────────────────────────────── */
    printf("[1] Connection API\n");
    rc = flexql_open("127.0.0.1", 9003, &db);
    EXPECT_OK(rc, "flexql_open connects successfully");

    /* ── 2. INVALID HANDLE ───────────────────────────────── */
    printf("\n[2] Invalid handle\n");
    rc = flexql_exec(NULL, "SELECT * FROM X", NULL, 0, &err);
    EXPECT_ERR(rc, "flexql_exec with NULL db returns FLEXQL_ERROR");
    if (err) { flexql_free(err); err = NULL; }

    /* ── 3. CREATE TABLE ─────────────────────────────────── */
    printf("\n[3] CREATE TABLE\n");
    rc = flexql_exec(db,
        "CREATE TABLE STUDENT("
        "ID INT PRIMARY KEY NOT NULL,"
        "FIRST_NAME TEXT NOT NULL,"
        "LAST_NAME TEXT NOT NULL,"
        "EMAIL TEXT NOT NULL)",
        NULL, 0, &err);
    EXPECT_OK(rc, "CREATE TABLE STUDENT with 4 columns");
    if (err) { printf("      err: %s\n", err); flexql_free(err); err=NULL; }

    rc = flexql_exec(db,
        "CREATE TABLE COURSE("
        "CID INT PRIMARY KEY NOT NULL,"
        "TITLE TEXT NOT NULL,"
        "STUDENT_ID INT NOT NULL)",
        NULL, 0, &err);
    EXPECT_OK(rc, "CREATE TABLE COURSE (for JOIN test)");
    if (err) { printf("      err: %s\n", err); flexql_free(err); err=NULL; }

    /* duplicate table */
    rc = flexql_exec(db,
        "CREATE TABLE STUDENT(ID INT PRIMARY KEY NOT NULL)",
        NULL, 0, &err);
    EXPECT_ERR(rc, "CREATE TABLE duplicate returns FLEXQL_ERROR");
    if (err) { flexql_free(err); err=NULL; }

    /* ── 4. INSERT ───────────────────────────────────────── */
    printf("\n[4] INSERT\n");
    rc = flexql_exec(db, "INSERT INTO STUDENT VALUES(1,'John','Doe','john@gmail.com')",   NULL, 0, &err);
    EXPECT_OK(rc, "INSERT row 1");
    if (err) { printf("      err: %s\n", err); flexql_free(err); err=NULL; }

    rc = flexql_exec(db, "INSERT INTO STUDENT VALUES(2,'Alice','Smith','alice@gmail.com')", NULL, 0, &err);
    EXPECT_OK(rc, "INSERT row 2");
    if (err) { printf("      err: %s\n", err); flexql_free(err); err=NULL; }

    rc = flexql_exec(db, "INSERT INTO STUDENT VALUES(3,'Bob','Jones','bob@gmail.com')",    NULL, 0, &err);
    EXPECT_OK(rc, "INSERT row 3");
    if (err) { printf("      err: %s\n", err); flexql_free(err); err=NULL; }

    rc = flexql_exec(db, "INSERT INTO STUDENT VALUES(4,'Carol','White','carol@gmail.com')", NULL, 0, &err);
    EXPECT_OK(rc, "INSERT row 4");
    if (err) { printf("      err: %s\n", err); flexql_free(err); err=NULL; }

    /* duplicate PK */
    rc = flexql_exec(db, "INSERT INTO STUDENT VALUES(1,'Dup','Dup','dup@dup.com')", NULL, 0, &err);
    EXPECT_ERR(rc, "INSERT duplicate PK returns FLEXQL_ERROR");
    if (err) { flexql_free(err); err=NULL; }

    /* wrong column count */
    rc = flexql_exec(db, "INSERT INTO STUDENT VALUES(5,'Only','Two')", NULL, 0, &err);
    EXPECT_ERR(rc, "INSERT wrong column count returns FLEXQL_ERROR");
    if (err) { flexql_free(err); err=NULL; }

    /* INSERT into COURSE for JOIN */
    flexql_exec(db, "INSERT INTO COURSE VALUES(101,'Math',1)",    NULL, 0, &err); if(err){flexql_free(err);err=NULL;}
    flexql_exec(db, "INSERT INTO COURSE VALUES(102,'Science',2)", NULL, 0, &err); if(err){flexql_free(err);err=NULL;}
    flexql_exec(db, "INSERT INTO COURSE VALUES(103,'History',1)", NULL, 0, &err); if(err){flexql_free(err);err=NULL;}

    /* ── 5. SELECT * ─────────────────────────────────────── */
    printf("\n[5] SELECT * FROM table\n");
    rowCount = 0;
    rc = flexql_exec(db, "SELECT * FROM STUDENT", countRows, NULL, &err);
    EXPECT_OK(rc, "SELECT * returns FLEXQL_OK");
    if (rowCount == 4) { printf("  [PASS] SELECT * returns 4 rows\n"); passed++; }
    else               { printf("  [FAIL] SELECT * returned %d rows (expected 4)\n", rowCount); failed++; }
    if (err) { flexql_free(err); err=NULL; }

    /* ── 6. SELECT specific columns ──────────────────────── */
    printf("\n[6] SELECT specific columns\n");
    rowCount = 0;
    rc = flexql_exec(db, "SELECT FIRST_NAME, EMAIL FROM STUDENT", countRows, NULL, &err);
    EXPECT_OK(rc, "SELECT FIRST_NAME, EMAIL returns FLEXQL_OK");
    if (rowCount == 4) { printf("  [PASS] SELECT specific cols returns 4 rows\n"); passed++; }
    else               { printf("  [FAIL] SELECT specific cols returned %d rows (expected 4)\n", rowCount); failed++; }
    /* check only 2 columns came back - lastCol should be FIRST_NAME */
    if (strcmp(lastCol, "FIRST_NAME") == 0) { printf("  [PASS] First column is FIRST_NAME\n"); passed++; }
    else { printf("  [FAIL] First column was '%s' (expected FIRST_NAME)\n", lastCol); failed++; }
    if (err) { flexql_free(err); err=NULL; }

    /* ── 7. WHERE clause = ───────────────────────────────── */
    printf("\n[7] WHERE clause (=)\n");
    rowCount = 0;
    rc = flexql_exec(db, "SELECT * FROM STUDENT WHERE ID = 1", countRows, NULL, &err);
    EXPECT_OK(rc, "SELECT WHERE ID=1 returns FLEXQL_OK");
    if (rowCount == 1) { printf("  [PASS] WHERE ID=1 returns 1 row\n"); passed++; }
    else               { printf("  [FAIL] WHERE ID=1 returned %d rows (expected 1)\n", rowCount); failed++; }
    if (err) { flexql_free(err); err=NULL; }

    rowCount = 0;
    rc = flexql_exec(db, "SELECT * FROM STUDENT WHERE FIRST_NAME = John", countRows, NULL, &err);
    EXPECT_OK(rc, "SELECT WHERE FIRST_NAME=John returns FLEXQL_OK");
    if (rowCount == 1) { printf("  [PASS] WHERE FIRST_NAME=John returns 1 row\n"); passed++; }
    else               { printf("  [FAIL] WHERE FIRST_NAME=John returned %d rows (expected 1)\n", rowCount); failed++; }
    if (err) { flexql_free(err); err=NULL; }

    /* ── 8. WHERE with comparison operators ──────────────── */
    printf("\n[8] WHERE comparison operators (<, >, <=, >=)\n");
    rowCount = 0;
    rc = flexql_exec(db, "SELECT * FROM STUDENT WHERE ID > 2", countRows, NULL, &err);
    EXPECT_OK(rc, "WHERE ID > 2 returns FLEXQL_OK");
    if (rowCount == 2) { printf("  [PASS] WHERE ID>2 returns 2 rows\n"); passed++; }
    else               { printf("  [FAIL] WHERE ID>2 returned %d rows (expected 2)\n", rowCount); failed++; }
    if (err) { flexql_free(err); err=NULL; }

    rowCount = 0;
    rc = flexql_exec(db, "SELECT * FROM STUDENT WHERE ID < 3", countRows, NULL, &err);
    EXPECT_OK(rc, "WHERE ID < 3 returns FLEXQL_OK");
    if (rowCount == 2) { printf("  [PASS] WHERE ID<3 returns 2 rows\n"); passed++; }
    else               { printf("  [FAIL] WHERE ID<3 returned %d rows (expected 2)\n", rowCount); failed++; }
    if (err) { flexql_free(err); err=NULL; }

    rowCount = 0;
    rc = flexql_exec(db, "SELECT * FROM STUDENT WHERE ID >= 2", countRows, NULL, &err);
    EXPECT_OK(rc, "WHERE ID >= 2 returns FLEXQL_OK");
    if (rowCount == 3) { printf("  [PASS] WHERE ID>=2 returns 3 rows\n"); passed++; }
    else               { printf("  [FAIL] WHERE ID>=2 returned %d rows (expected 3)\n", rowCount); failed++; }
    if (err) { flexql_free(err); err=NULL; }

    rowCount = 0;
    rc = flexql_exec(db, "SELECT * FROM STUDENT WHERE ID <= 2", countRows, NULL, &err);
    EXPECT_OK(rc, "WHERE ID <= 2 returns FLEXQL_OK");
    if (rowCount == 2) { printf("  [PASS] WHERE ID<=2 returns 2 rows\n"); passed++; }
    else               { printf("  [FAIL] WHERE ID<=2 returned %d rows (expected 2)\n", rowCount); failed++; }
    if (err) { flexql_free(err); err=NULL; }

    /* ── 9. WHERE no results ─────────────────────────────── */
    printf("\n[9] WHERE with no matching rows\n");
    rowCount = 0;
    rc = flexql_exec(db, "SELECT * FROM STUDENT WHERE ID = 999", countRows, NULL, &err);
    EXPECT_OK(rc, "WHERE no match still returns FLEXQL_OK");
    if (rowCount == 0) { printf("  [PASS] WHERE no match returns 0 rows\n"); passed++; }
    else               { printf("  [FAIL] WHERE no match returned %d rows\n", rowCount); failed++; }
    if (err) { flexql_free(err); err=NULL; }

    /* ── 10. INNER JOIN ──────────────────────────────────── */
    printf("\n[10] INNER JOIN\n");
    rowCount = 0;
    rc = flexql_exec(db,
        "SELECT * FROM STUDENT INNER JOIN COURSE ON STUDENT.ID = COURSE.STUDENT_ID",
        countRows, NULL, &err);
    EXPECT_OK(rc, "INNER JOIN returns FLEXQL_OK");
    /* John(1) has Math+History = 2 rows, Alice(2) has Science = 1 row => 3 total */
    if (rowCount == 3) { printf("  [PASS] INNER JOIN returns 3 rows\n"); passed++; }
    else               { printf("  [FAIL] INNER JOIN returned %d rows (expected 3)\n", rowCount); failed++; }
    if (err) { printf("      err: %s\n", err); flexql_free(err); err=NULL; }

    /* JOIN with WHERE */
    printf("\n[11] INNER JOIN with WHERE\n");
    rowCount = 0;
    rc = flexql_exec(db,
        "SELECT * FROM STUDENT INNER JOIN COURSE ON STUDENT.ID = COURSE.STUDENT_ID WHERE ID = 1",
        countRows, NULL, &err);
    EXPECT_OK(rc, "INNER JOIN + WHERE returns FLEXQL_OK");
    if (rowCount == 2) { printf("  [PASS] JOIN+WHERE returns 2 rows for ID=1\n"); passed++; }
    else               { printf("  [FAIL] JOIN+WHERE returned %d rows (expected 2)\n", rowCount); failed++; }
    if (err) { printf("      err: %s\n", err); flexql_free(err); err=NULL; }

    /* ── 12. EXPIRATION TIMESTAMP ────────────────────────── */
    printf("\n[12] Expiration timestamp\n");
    /* Insert with expiry in the past (Unix time 1 = already expired) */
    rc = flexql_exec(db,
        "INSERT INTO STUDENT VALUES(99,'Ghost','User','ghost@test.com',EXP:1)",
        NULL, 0, &err);
    EXPECT_OK(rc, "INSERT with past expiry timestamp accepted");
    if (err) { flexql_free(err); err=NULL; }

    rowCount = 0;
    rc = flexql_exec(db, "SELECT * FROM STUDENT WHERE ID = 99", countRows, NULL, &err);
    EXPECT_OK(rc, "SELECT expired row returns FLEXQL_OK");
    if (rowCount == 0) { printf("  [PASS] Expired row is not returned\n"); passed++; }
    else               { printf("  [FAIL] Expired row WAS returned (%d rows)\n", rowCount); failed++; }
    if (err) { flexql_free(err); err=NULL; }

    /* Insert with far-future expiry - should be returned */
    rc = flexql_exec(db,
        "INSERT INTO STUDENT VALUES(98,'Future','User','future@test.com',EXP:9999999999)",
        NULL, 0, &err);
    if (err) { flexql_free(err); err=NULL; }
    rowCount = 0;
    rc = flexql_exec(db, "SELECT * FROM STUDENT WHERE ID = 98", countRows, NULL, &err);
    if (rowCount == 1) { printf("  [PASS] Non-expired row IS returned\n"); passed++; }
    else               { printf("  [FAIL] Non-expired row not returned (%d rows)\n", rowCount); failed++; }
    if (err) { flexql_free(err); err=NULL; }

    /* ── 13. CALLBACK NULL (no crash) ────────────────────── */
    printf("\n[13] NULL callback\n");
    rc = flexql_exec(db, "SELECT * FROM STUDENT", NULL, 0, &err);
    EXPECT_OK(rc, "SELECT with NULL callback does not crash");
    if (err) { flexql_free(err); err=NULL; }

    /* ── 14. CALLBACK ABORT ──────────────────────────────── */
    printf("\n[14] Callback abort (return 1)\n");
    rowCount = 0;
    rc = flexql_exec(db, "SELECT * FROM STUDENT", abortAfterFirst, NULL, &err);
    EXPECT_OK(rc, "Aborted callback returns FLEXQL_OK");
    if (rowCount == 1) { printf("  [PASS] Callback abort stops after 1 row\n"); passed++; }
    else               { printf("  [FAIL] Callback abort processed %d rows (expected 1)\n", rowCount); failed++; }
    if (err) { flexql_free(err); err=NULL; }

    /* ── 15. SELECT non-existent table ───────────────────── */
    printf("\n[15] Error handling\n");
    rc = flexql_exec(db, "SELECT * FROM DOESNOTEXIST", NULL, 0, &err);
    EXPECT_ERR(rc, "SELECT non-existent table returns FLEXQL_ERROR");
    if (err) { printf("      (error msg: %s)\n", err); flexql_free(err); err=NULL; }

    rc = flexql_exec(db, "INSERT INTO DOESNOTEXIST VALUES(1)", NULL, 0, &err);
    EXPECT_ERR(rc, "INSERT non-existent table returns FLEXQL_ERROR");
    if (err) { flexql_free(err); err=NULL; }

    /* ── 16. flexql_free ─────────────────────────────────── */
    printf("\n[16] flexql_free\n");
    char *toFree = (char*)malloc(64);
    strcpy(toFree, "test string");
    flexql_free(toFree);
    printf("  [PASS] flexql_free does not crash\n"); passed++;

    flexql_free(NULL);
    printf("  [PASS] flexql_free(NULL) does not crash\n"); passed++;

    /* ── 17. DECIMAL type ────────────────────────────────── */
    printf("\n[17] DECIMAL type\n");
    rc = flexql_exec(db,
        "CREATE TABLE PRICES(ID INT PRIMARY KEY NOT NULL, AMOUNT DECIMAL NOT NULL)",
        NULL, 0, &err);
    EXPECT_OK(rc, "CREATE TABLE with DECIMAL column");
    if (err) { flexql_free(err); err=NULL; }

    rc = flexql_exec(db, "INSERT INTO PRICES VALUES(1,99.99)", NULL, 0, &err);
    EXPECT_OK(rc, "INSERT DECIMAL value");
    if (err) { printf("      err: %s\n", err); flexql_free(err); err=NULL; }

    rowCount = 0;
    memset(lastVal, 0, sizeof(lastVal));
    rc = flexql_exec(db, "SELECT * FROM PRICES WHERE ID = 1", countRows, NULL, &err);
    EXPECT_OK(rc, "SELECT DECIMAL value");
    if (rowCount == 1) { printf("  [PASS] DECIMAL row returned\n"); passed++; }
    else               { printf("  [FAIL] DECIMAL row not returned\n"); failed++; }
    if (err) { flexql_free(err); err=NULL; }

    /* ── 18. flexql_close ────────────────────────────────── */
    printf("\n[18] flexql_close\n");
    rc = flexql_close(db);
    EXPECT_OK(rc, "flexql_close returns FLEXQL_OK");

    rc = flexql_close(NULL);
    EXPECT_ERR(rc, "flexql_close(NULL) returns FLEXQL_ERROR");

    /* ── SUMMARY ─────────────────────────────────────────── */
    printf("\n========================================\n");
    printf("  RESULTS: %d passed, %d failed\n", passed, failed);
    printf("========================================\n\n");

    return failed > 0 ? 1 : 0;
}
