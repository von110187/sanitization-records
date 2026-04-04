/*
 * sanitization_records.c
 *
 * Sanitization Records Management System
 * Manages CRUD operations for corporate sanitization service records,
 * storing data in a pipe-delimited flat file (sanitizationRecords.txt).
 *
 * Author  : Von Kok Yew
 * Context : Diploma group project — Sanitization Records module (written independently)
 *
 * Build
 * -----
 * MSVC  : cl sanitization_records.c
 * GCC   : gcc -std=c99 -Wall -o sanitization_records sanitization_records.c
 *
 * _CRT_SECURE_NO_WARNINGS suppresses MSVC deprecation warnings for standard
 * C functions (strncpy, sscanf, fopen). These functions are used safely here:
 * all string writes are bounded, all numeric parsing goes through strtol/strtod,
 * and fgets is used for all interactive input instead of scanf.
 */

#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

 /* =========================================================================
    Constants and macros
    ========================================================================= */

#define MAX_RECORDS  100
#define DATA_FILE    "sanitizationRecords.txt"
#define LINE_BUF     128   /* general-purpose line buffer for fgets */

    /*
     * Field capacities — kept as named constants so buffer sizes stay consistent
     * between struct definitions, input functions, and format strings.
     */
#define SZ_CODE      10    /* "SR"  prefix + digits, e.g. SR001   */
#define SZ_INVNUM    10    /* "INV" prefix + digits, e.g. INV001  */
#define SZ_VENUE     15
#define SZ_COMPANY   20
#define SZ_CONTACT   15    /* Malaysian phone: 012-34567890 = 12 chars */

     /*
      * File format: one record per line, fields separated by '|'.
      *
      * SR_FPRINTF_FMT writes a record to the data file.
      * SR_SSCANF_FMT  parses one line back into an SR struct.
      * Width specifiers in SR_SSCANF_FMT match the SZ_* constants minus one,
      * preventing buffer overflow on malformed file data.
      */
#define SR_FPRINTF_FMT \
    "%s|%d|%d|%d|%d|%d|%d|%d|%d|%s|%s|%s|%s|%s|%.2f|%d|%d|%d\n"

#define SR_SSCANF_FMT \
    "%9[^|]|%d|%d|%d|%d|%d|%d|%d|%d|%14[^|]|%19[^|]|%14[^|]|%14[^|]|%9[^|]|%lf|%d|%d|%d"

      /* =========================================================================
         Data structures
         ========================================================================= */

struct Date {
    int day, month, year;
};

struct Time {
    int startHrs, startMins, endHrs, endMins;
};

struct Company {
    char companyName[SZ_COMPANY];
    char contactName[SZ_CONTACT];
    char contactNumber[SZ_CONTACT];
};

struct DueDate {
    int day, month, year;
};

typedef struct {
    char           number[SZ_INVNUM];
    double         price;
    struct DueDate dueDate;
} Invoice;

typedef struct {
    int            srNum;
    char           srCode[SZ_CODE];
    struct Date    srDate;
    struct Time    srTime;
    int            srPeople;
    char           srVenue[SZ_VENUE];
    struct Company srCompany;
    Invoice        srInvoice;
} SR;

/* =========================================================================
   Forward declarations
   ========================================================================= */

   /* CRUD operations */
void srDisplayRecord(void);
void srAddRecord(void);
void srSearchRecord(void);
void srModifyRecord(void);
void srDeleteRecord(void);
void srReportRecord(void);

/* UI rendering */
void srLogo(void);
void srMenu(void);
void srPrintSectionBanner(const char* title);
void srPrintTableBorder(void);
void srPrintTableHeader(void);
void srPrintTableRow(SR* sr, int num);

/* Input primitives */
void readLine(const char* prompt, char* buf, int size);
int  readInt(const char* prompt, int* out);
int  readDouble(const char* prompt, double* out);
char readYesNo(const char* prompt);

/* Validation predicates */
int isNonEmpty(const char* s);
int isValidDate(int day, int month, int year);
int isValidTime(int hrs, int mins);
int isEndAfterStart(int sHrs, int sMins, int eHrs, int eMins);
int isValidSrCode(const char* s);
int isValidInvNumber(const char* s);
int isValidContactNumber(const char* s);
int isCodeUnique(SR* records, int count, const char* code);

/* Reusable field input blocks (shared by Add and Modify) */
static void inputDate(const char* label, int* day, int* month, int* year);
static void inputTime(const char* label, int* hrs, int* mins,
    int startHrs, int startMins, int checkEnd);
static void inputPeople(int* people);
static void inputSrCode(SR* records, int count, char* code);
static void inputInvNumber(char* invNum);
static void inputContactNumber(char* contNum);
static void inputPrice(double* price);

/* File I/O */
int  srLoadAllRecords(SR* records, int maxRecords);
void srSaveAllRecords(SR* records, int count);

/* =========================================================================
   String utility
   ========================================================================= */

   /*
    * Copy src into dst, always null-terminating.
    * Uses memcpy for clarity — the caller already knows the sizes are safe
    * because we validate all input against the SZ_* constants before storing.
    */
static void strcopy(char* dst, const char* src, int size) {
    int len = (int)strlen(src);
    if (len >= size) len = size - 1;
    memcpy(dst, src, (size_t)len);
    dst[len] = '\0';
}

/* =========================================================================
   Input primitives
   ========================================================================= */

   /*
    * Read one line from stdin, stripping the trailing newline.
    * fgets consumes the newline as part of the read, so no characters
    * are left in the input buffer for the next call.
    */
static void fgetsLine(char* buf, int size) {
    if (fgets(buf, size, stdin)) {
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n')
            buf[len - 1] = '\0';
    }
    else {
        buf[0] = '\0';
    }
}

/*
 * Prompt and read a non-empty string.
 * Re-prompts if the user submits only whitespace.
 */
void readLine(const char* prompt, char* buf, int size) {
    do {
        printf("  %-24s: ", prompt);
        fgetsLine(buf, size);
        if (!isNonEmpty(buf))
            printf("  [!] Cannot be empty. Try again.\n");
    } while (!isNonEmpty(buf));
}

/*
 * Prompt and read one integer via strtol.
 *
 * Reading through fgets first means a non-numeric input like "abc"
 * simply returns 0 instead of leaving junk in stdin and looping forever,
 * which is what scanf would do. The caller is responsible for re-prompting.
 *
 * Returns 1 on success, 0 if the input could not be parsed as an integer.
 */
int readInt(const char* prompt, int* out) {
    char buf[LINE_BUF];
    printf("  %-24s: ", prompt);
    fgetsLine(buf, (int)sizeof(buf));
    if (!isNonEmpty(buf)) { *out = 0; return 0; }
    char* end;
    long val = strtol(buf, &end, 10);
    while (*end && isspace((unsigned char)*end)) end++;
    if (end == buf || *end != '\0') { *out = 0; return 0; }
    *out = (int)val;
    return 1;
}

/*
 * Prompt and read one double via strtod.
 * Same safe-parsing approach as readInt.
 * Returns 1 on success, 0 on parse failure.
 */
int readDouble(const char* prompt, double* out) {
    char buf[LINE_BUF];
    printf("  %-24s: ", prompt);
    fgetsLine(buf, (int)sizeof(buf));
    if (!isNonEmpty(buf)) { *out = 0.0; return 0; }
    char* end;
    double val = strtod(buf, &end);
    while (*end && isspace((unsigned char)*end)) end++;
    if (end == buf || *end != '\0') { *out = 0.0; return 0; }
    *out = val;
    return 1;
}

/*
 * Prompt and read a Y/N confirmation.
 * Loops until the user enters 'Y' or 'N' (case-insensitive).
 * Returns the uppercase result.
 */
char readYesNo(const char* prompt) {
    char buf[LINE_BUF];
    char c;
    do {
        printf("  %s (Y/N) : ", prompt);
        fgetsLine(buf, (int)sizeof(buf));
        c = toupper((unsigned char)buf[0]);
        if (c != 'Y' && c != 'N')
            printf("  [!] Enter Y or N.\n");
    } while (c != 'Y' && c != 'N');
    return c;
}

/* =========================================================================
   Validation predicates
   ========================================================================= */

   /* Returns 1 if s contains at least one non-whitespace character. */
int isNonEmpty(const char* s) {
    if (!s || *s == '\0') return 0;
    while (*s) {
        if (!isspace((unsigned char)*s)) return 1;
        s++;
    }
    return 0;
}

/*
 * Validates calendar date within the range 01-01-2000 to 31-12-2100.
 * Accounts for 30-day months and leap years (Gregorian rule).
 */
int isValidDate(int day, int month, int year) {
    if (year < 2000 || year  > 2100) return 0;
    if (month < 1 || month > 12)   return 0;
    if (day < 1)                    return 0;
    int maxDay = 31;
    if (month == 4 || month == 6 || month == 9 || month == 11) {
        maxDay = 30;
    }
    else if (month == 2) {
        int leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        maxDay = leap ? 29 : 28;
    }
    return day <= maxDay;
}

/* Returns 1 if the time is a valid 24-hour clock value. */
int isValidTime(int hrs, int mins) {
    return hrs >= 0 && hrs <= 23 && mins >= 0 && mins <= 59;
}

/* Returns 1 if end time is strictly later than start time (same day). */
int isEndAfterStart(int sHrs, int sMins, int eHrs, int eMins) {
    return (eHrs * 60 + eMins) > (sHrs * 60 + sMins);
}

/*
 * SR Code must begin with "SR" (uppercase) followed by one or more digits.
 * Examples — valid: SR1, SR001, SR999 | invalid: sr1, SR, SRAA1, SR12A
 */
int isValidSrCode(const char* s) {
    if (!s || s[0] != 'S' || s[1] != 'R') return 0;
    const char* p = s + 2;
    if (*p == '\0') return 0;
    while (*p) {
        if (!isdigit((unsigned char)*p)) return 0;
        p++;
    }
    return 1;
}

/*
 * Invoice number must begin with "INV" (uppercase) followed by one or more digits.
 * Examples — valid: INV1, INV001 | invalid: inv1, INV, INV12A
 */
int isValidInvNumber(const char* s) {
    if (!s || s[0] != 'I' || s[1] != 'N' || s[2] != 'V') return 0;
    const char* p = s + 3;
    if (*p == '\0') return 0;
    while (*p) {
        if (!isdigit((unsigned char)*p)) return 0;
        p++;
    }
    return 1;
}

/*
 * Malaysian phone number format: XXX-XXXXXXX or XXX-XXXXXXXX
 * (3 digits, dash, 7 or 8 digits — no other characters allowed).
 * Examples — valid: 012-3456789, 011-34567890 | invalid: 0123456789, 012-345
 */
int isValidContactNumber(const char* s) {
    if (!s) return 0;
    int pre = 0;
    while (s[pre] && isdigit((unsigned char)s[pre])) pre++;
    if (pre != 3 || s[pre] != '-') return 0;
    const char* post = s + pre + 1;
    int len = 0;
    while (post[len] && isdigit((unsigned char)post[len])) len++;
    if (len != 7 && len != 8) return 0;
    return post[len] == '\0';
}

/* Returns 1 if no existing record shares the given SR Code. */
int isCodeUnique(SR* records, int count, const char* code) {
    for (int i = 0; i < count; i++) {
        if (strcmp(records[i].srCode, code) == 0) return 0;
    }
    return 1;
}

/* =========================================================================
   Reusable field input blocks
   Shared between Add and Modify to avoid duplicating validation logic.
   ========================================================================= */

   /* Prompt for a date, re-prompting until the value passes isValidDate. */
static void inputDate(const char* label, int* day, int* month, int* year) {
    char buf[LINE_BUF], prompt[48];
    int d = 0, m = 0, y = 0;
    snprintf(prompt, sizeof(prompt), "%s (dd-mm-yyyy)", label);
    do {
        printf("  %-24s: ", prompt);
        fgetsLine(buf, (int)sizeof(buf));
        if (sscanf(buf, "%d-%d-%d", &d, &m, &y) != 3) {
            printf("  [!] Enter as dd-mm-yyyy (e.g. 15-06-2024).\n");
            d = m = y = 0;
            continue;
        }
        if (!isValidDate(d, m, y))
            printf("  [!] Invalid date. Check day, month, and year.\n");
    } while (!isValidDate(d, m, y));
    *day = d; *month = m; *year = y;
}

/*
 * Prompt for a time (space-separated "hh mm"), re-prompting on invalid values.
 * When checkEnd is non-zero, also validates that the entered time is strictly
 * after startHrs:startMins, used to enforce end > start on the same record.
 */
static void inputTime(const char* label, int* hrs, int* mins,
    int startHrs, int startMins, int checkEnd) {
    char buf[LINE_BUF];
    int h = -1, m = -1;
    do {
        printf("  %-24s: ", label);
        fgetsLine(buf, (int)sizeof(buf));
        if (sscanf(buf, "%d %d", &h, &m) != 2) {
            printf("  [!] Enter as hh mm (e.g. 09 30).\n");
            h = m = -1; continue;
        }
        if (!isValidTime(h, m)) {
            printf("  [!] Hours: 00-23, Minutes: 00-59.\n");
            h = m = -1;
        }
        else if (checkEnd && !isEndAfterStart(startHrs, startMins, h, m)) {
            printf("  [!] End time must be after start time.\n");
            h = m = -1;
        }
    } while (h < 0 || m < 0);
    *hrs = h; *mins = m;
}

/* Prompt for number of people (must be >= 1). */
static void inputPeople(int* people) {
    int p = 0;
    do {
        if (!readInt("Amount of people", &p) || p <= 0)
            printf("  [!] Must be at least 1.\n");
    } while (p <= 0);
    *people = p;
}

/* Prompt for an SR Code, enforcing format and uniqueness. */
static void inputSrCode(SR* records, int count, char* code) {
    do {
        readLine("SR Code (e.g. SR001)", code, SZ_CODE);
        if (!isValidSrCode(code))
            printf("  [!] Must start with 'SR' followed by digits (e.g. SR1, SR001).\n");
        else if (!isCodeUnique(records, count, code))
            printf("  [!] Code '%s' already exists. Use a different code.\n", code);
    } while (!isValidSrCode(code) || !isCodeUnique(records, count, code));
}

/* Prompt for an invoice number, enforcing INV + digits format. */
static void inputInvNumber(char* invNum) {
    do {
        readLine("Invoice No (e.g. INV001)", invNum, SZ_INVNUM);
        if (!isValidInvNumber(invNum))
            printf("  [!] Must start with 'INV' followed by digits (e.g. INV1, INV001).\n");
    } while (!isValidInvNumber(invNum));
}

/* Prompt for a Malaysian contact number, enforcing XXX-XXXXXXX(X) format. */
static void inputContactNumber(char* contNum) {
    do {
        readLine("Contact number", contNum, SZ_CONTACT);
        if (!isValidContactNumber(contNum))
            printf("  [!] Format: XXX-XXXXXXX or XXX-XXXXXXXX (e.g. 012-3456789).\n");
    } while (!isValidContactNumber(contNum));
}

/* Prompt for a price (must be > 0). */
static void inputPrice(double* price) {
    double p = 0.0;
    do {
        if (!readDouble("Price (RM)", &p) || p <= 0.0)
            printf("  [!] Price must be greater than 0.\n");
    } while (p <= 0.0);
    *price = p;
}

/* =========================================================================
   File I/O
   ========================================================================= */

   /*
    * Load all records from DATA_FILE into the records array.
    * Parses each line with sscanf against SR_SSCANF_FMT.
    * Returns the number of records loaded, or -1 if the file cannot be opened.
    */
int srLoadAllRecords(SR* records, int maxRecords) {
    FILE* fp = fopen(DATA_FILE, "r");
    if (!fp) return -1;

    char line[512];
    int count = 0;
    while (count < maxRecords && fgets(line, (int)sizeof(line), fp)) {
        SR* r = &records[count];
        int fields = sscanf(line, SR_SSCANF_FMT,
            r->srCode,
            &r->srDate.day, &r->srDate.month, &r->srDate.year,
            &r->srTime.startHrs, &r->srTime.startMins,
            &r->srTime.endHrs, &r->srTime.endMins,
            &r->srPeople,
            r->srVenue,
            r->srCompany.companyName,
            r->srCompany.contactName,
            r->srCompany.contactNumber,
            r->srInvoice.number,
            &r->srInvoice.price,
            &r->srInvoice.dueDate.day,
            &r->srInvoice.dueDate.month,
            &r->srInvoice.dueDate.year);
        if (fields == 18) count++;
    }
    fclose(fp);
    return count;
}

/*
 * Overwrite DATA_FILE with the current in-memory records array.
 * Called after every Modify and Delete operation.
 */
void srSaveAllRecords(SR* records, int count) {
    FILE* fp = fopen(DATA_FILE, "w");
    if (!fp) {
        printf("\n  [!] Error: unable to write to %s\n", DATA_FILE);
        return;
    }
    for (int i = 0; i < count; i++) {
        fprintf(fp, SR_FPRINTF_FMT,
            records[i].srCode,
            records[i].srDate.day, records[i].srDate.month, records[i].srDate.year,
            records[i].srTime.startHrs, records[i].srTime.startMins,
            records[i].srTime.endHrs, records[i].srTime.endMins,
            records[i].srPeople,
            records[i].srVenue,
            records[i].srCompany.companyName,
            records[i].srCompany.contactName,
            records[i].srCompany.contactNumber,
            records[i].srInvoice.number,
            records[i].srInvoice.price,
            records[i].srInvoice.dueDate.day,
            records[i].srInvoice.dueDate.month,
            records[i].srInvoice.dueDate.year);
    }
    fclose(fp);
}

/* =========================================================================
   UI helpers
   ========================================================================= */

void srLogo(void) {
    printf("\033[1;34m");
    printf("\t   ___  __   __  _  _  __    _  _ _   _  __   __  ___  __  _  _ ___  __  __   __ \n");
    printf("\t    |  |__| |__) |  | |      |\\/|  \\ /  |__  |_    |  |__| |__|  |  |_  |__) |__|\n");
    printf("\t    |  |  | |  \\ |__| |__    |  |   |    __| |__  _|  |  | |  |  |  |__ |  \\ |  |\n");
    printf("\t __   __  _  _   ___   ___   __  ___    __  _  _     __   __  __  __   __   __   __ \n");
    printf("\t|__  |__| |\\ | |  |  |   /  |__|  |  | |  | |\\ |    |__) |_  |   |  | |__| |  \\ |__ \n");
    printf("\t __| |  | | \\| |  |  |  /__ |  |  |  | |__| | \\|    |  \\ |__ |__ |__| |  \\ |__/  __|\n\n");
    printf("\033[0m");
}

void srMenu(void) {
    printf("+================================+\n");
    printf("|   SANITIZATION RECORD SYSTEM   |\n");
    printf("+================================+\n");
    printf("| 1. Display All Records         |\n");
    printf("| 2. Add Record                  |\n");
    printf("| 3. Search Record by Month/Year |\n");
    printf("| 4. Modify Record               |\n");
    printf("| 5. Delete Record               |\n");
    printf("| 6. Price Report                |\n");
    printf("| 7. Exit                        |\n");
    printf("+================================+\n");
}

/* Prints a titled section divider to visually separate operations. */
void srPrintSectionBanner(const char* title) {
    printf("\n+--------------------------------------------------+\n");
    printf("| %-48s |\n", title);
    printf("+--------------------------------------------------+\n");
}

void srPrintTableBorder(void) {
    printf("+----+----------+------------+------------+----------+------------------+-----------------+----------------------+-----------------+-----------------+---------------+-------------+------------+\n");
}

void srPrintTableHeader(void) {
    srPrintTableBorder();
    printf("| %-2s | %-8s | %-10s | %-10s | %-8s | %-16s | %-15s | %-20s | %-15s | %-15s | %-13s | %-11s | %-10s |\n",
        "No", "Code", "Date", "Start", "End", "People", "Venue",
        "Company", "Contact Person", "Contact No", "Invoice No",
        "Price (RM)", "Due Date");
    srPrintTableBorder();
}

/* Formats and prints one record row, pre-formatting time and date strings. */
void srPrintTableRow(SR* sr, int num) {
    char start[8], end[8], date[12], due[12];
    snprintf(start, sizeof(start), "%02d:%02d", sr->srTime.startHrs, sr->srTime.startMins);
    snprintf(end, sizeof(end), "%02d:%02d", sr->srTime.endHrs, sr->srTime.endMins);
    snprintf(date, sizeof(date), "%02d-%02d-%04d",
        sr->srDate.day, sr->srDate.month, sr->srDate.year);
    snprintf(due, sizeof(due), "%02d-%02d-%04d",
        sr->srInvoice.dueDate.day,
        sr->srInvoice.dueDate.month,
        sr->srInvoice.dueDate.year);

    printf("| %-2d | %-8s | %-10s | %-10s | %-8s | %-16d | %-15s | %-20s | %-15s | %-15s | %-13s | %-11.2f | %-10s |\n",
        num, sr->srCode, date, start, end,
        sr->srPeople, sr->srVenue,
        sr->srCompany.companyName,
        sr->srCompany.contactName,
        sr->srCompany.contactNumber,
        sr->srInvoice.number,
        sr->srInvoice.price, due);
}

/* =========================================================================
   CRUD operations
   ========================================================================= */

void srDisplayRecord(void) {
    SR sr;
    int count = 0, num = 1;

    srPrintSectionBanner("DISPLAY ALL RECORDS");

    FILE* fp = fopen(DATA_FILE, "r");
    if (!fp) {
        printf("\n  No records found.\n\n");
        return;
    }

    char line[512];
    int printed = 0;

    while (fgets(line, (int)sizeof(line), fp)) {
        int fields = sscanf(line, SR_SSCANF_FMT,
            sr.srCode,
            &sr.srDate.day, &sr.srDate.month, &sr.srDate.year,
            &sr.srTime.startHrs, &sr.srTime.startMins,
            &sr.srTime.endHrs, &sr.srTime.endMins,
            &sr.srPeople,
            sr.srVenue,
            sr.srCompany.companyName,
            sr.srCompany.contactName,
            sr.srCompany.contactNumber,
            sr.srInvoice.number,
            &sr.srInvoice.price,
            &sr.srInvoice.dueDate.day,
            &sr.srInvoice.dueDate.month,
            &sr.srInvoice.dueDate.year);
        if (fields != 18) continue;
        if (!printed) { printf("\n"); srPrintTableHeader(); printed = 1; }
        srPrintTableRow(&sr, num++);
        count++;
    }

    if (printed) srPrintTableBorder();
    fclose(fp);
    printf("\n  Total: %d record(s)\n\n", count);
}

void srAddRecord(void) {
    SR records[MAX_RECORDS];
    int count = srLoadAllRecords(records, MAX_RECORDS);
    if (count < 0) count = 0;

    FILE* fp = fopen(DATA_FILE, "a");
    if (!fp) {
        printf("\n  [!] Error: cannot open %s for writing.\n\n", DATA_FILE);
        return;
    }

    char choice = 'Y';
    do {
        srPrintSectionBanner("ADD NEW RECORD");

        if (count >= MAX_RECORDS) {
            printf("\n  [!] Record limit (%d) reached. Cannot add more.\n\n", MAX_RECORDS);
            break;
        }

        SR sr;

        printf("\n  [ Record Details ]\n");
        inputSrCode(records, count, sr.srCode);

        printf("\n  [ Service Date ]\n");
        inputDate("Date", &sr.srDate.day, &sr.srDate.month, &sr.srDate.year);

        printf("\n  [ Service Time ]\n");
        inputTime("Start time (hh mm)", &sr.srTime.startHrs, &sr.srTime.startMins, 0, 0, 0);
        inputTime("End time   (hh mm)", &sr.srTime.endHrs, &sr.srTime.endMins,
            sr.srTime.startHrs, sr.srTime.startMins, 1);

        printf("\n  [ Service Info ]\n");
        inputPeople(&sr.srPeople);
        readLine("Venue", sr.srVenue, SZ_VENUE);

        printf("\n  [ Company Details ]\n");
        readLine("Company name", sr.srCompany.companyName, SZ_COMPANY);
        readLine("Contact person", sr.srCompany.contactName, SZ_CONTACT);
        inputContactNumber(sr.srCompany.contactNumber);

        printf("\n  [ Invoice ]\n");
        inputInvNumber(sr.srInvoice.number);
        inputPrice(&sr.srInvoice.price);

        printf("\n  [ Invoice Due Date ]\n");
        inputDate("Due date", &sr.srInvoice.dueDate.day,
            &sr.srInvoice.dueDate.month, &sr.srInvoice.dueDate.year);

        fprintf(fp, SR_FPRINTF_FMT,
            sr.srCode,
            sr.srDate.day, sr.srDate.month, sr.srDate.year,
            sr.srTime.startHrs, sr.srTime.startMins,
            sr.srTime.endHrs, sr.srTime.endMins,
            sr.srPeople,
            sr.srVenue,
            sr.srCompany.companyName,
            sr.srCompany.contactName,
            sr.srCompany.contactNumber,
            sr.srInvoice.number,
            sr.srInvoice.price,
            sr.srInvoice.dueDate.day,
            sr.srInvoice.dueDate.month,
            sr.srInvoice.dueDate.year);

        records[count++] = sr;
        printf("\n  [+] Record %s added successfully.\n", sr.srCode);

        choice = readYesNo("\n  Add another record?");

    } while (choice == 'Y');

    fclose(fp);
    printf("\n");
}

void srSearchRecord(void) {
    SR sr[MAX_RECORDS];
    int count = srLoadAllRecords(sr, MAX_RECORDS);

    if (count <= 0) {
        srPrintSectionBanner("SEARCH RECORD BY MONTH / YEAR");
        printf("\n  No records found.\n\n");
        return;
    }

    srPrintSectionBanner("SEARCH RECORD BY MONTH / YEAR");

    int month = 0, year = 0, found = 0, num = 1;

    do {
        if (!readInt("Month (1-12)", &month) || month < 1 || month > 12)
            printf("  [!] Month must be between 1 and 12.\n");
    } while (month < 1 || month > 12);

    do {
        if (!readInt("Year (>= 2000)", &year) || year < 2000)
            printf("  [!] Year must be 2000 or later.\n");
    } while (year < 2000);

    printf("\n");

    for (int i = 0; i < count; i++) {
        if (sr[i].srDate.month == month && sr[i].srDate.year == year) {
            if (found == 0) srPrintTableHeader();
            srPrintTableRow(&sr[i], num++);
            found++;
        }
    }

    if (found > 0) srPrintTableBorder();
    printf("\n  %d record(s) found for %02d-%04d\n\n", found, month, year);
}

void srModifyRecord(void) {
    SR sr[MAX_RECORDS];
    int count = srLoadAllRecords(sr, MAX_RECORDS);

    srPrintSectionBanner("MODIFY RECORD");

    if (count <= 0) {
        printf("\n  No records found.\n\n");
        return;
    }

    char srCode[SZ_CODE];
    int  found, modifyCount = 0;
    char more = 'Y';

    do {
        found = 0;
        readLine("SR Code to modify", srCode, SZ_CODE);

        for (int i = 0; i < count; i++) {
            if (strcmp(sr[i].srCode, srCode) != 0) continue;
            found = 1;

            printf("\n  Current record:\n");
            srPrintTableHeader();
            srPrintTableRow(&sr[i], i + 1);
            srPrintTableBorder();
            printf("\n  Enter new details:\n");

            int day, month, year;
            int sHrs, sMins, eHrs, eMins, people;
            int ddDay, ddMonth, ddYear;
            char venue[SZ_VENUE], compName[SZ_COMPANY];
            char contName[SZ_CONTACT], contNum[SZ_CONTACT], invNum[SZ_INVNUM];
            double price;

            printf("\n  [ Service Date ]\n");
            inputDate("Date", &day, &month, &year);

            printf("\n  [ Service Time ]\n");
            inputTime("Start time (hh mm)", &sHrs, &sMins, 0, 0, 0);
            inputTime("End time   (hh mm)", &eHrs, &eMins, sHrs, sMins, 1);

            printf("\n  [ Service Info ]\n");
            inputPeople(&people);
            readLine("Venue", venue, SZ_VENUE);

            printf("\n  [ Company Details ]\n");
            readLine("Company name", compName, SZ_COMPANY);
            readLine("Contact person", contName, SZ_CONTACT);
            inputContactNumber(contNum);

            printf("\n  [ Invoice ]\n");
            inputInvNumber(invNum);
            inputPrice(&price);

            printf("\n  [ Invoice Due Date ]\n");
            inputDate("Due date", &ddDay, &ddMonth, &ddYear);

            if (readYesNo("\n  Confirm modification?") == 'Y') {
                sr[i].srDate.day = day;
                sr[i].srDate.month = month;
                sr[i].srDate.year = year;
                sr[i].srTime.startHrs = sHrs;
                sr[i].srTime.startMins = sMins;
                sr[i].srTime.endHrs = eHrs;
                sr[i].srTime.endMins = eMins;
                sr[i].srPeople = people;
                sr[i].srInvoice.price = price;
                sr[i].srInvoice.dueDate.day = ddDay;
                sr[i].srInvoice.dueDate.month = ddMonth;
                sr[i].srInvoice.dueDate.year = ddYear;
                strcopy(sr[i].srVenue, venue, SZ_VENUE);
                strcopy(sr[i].srCompany.companyName, compName, SZ_COMPANY);
                strcopy(sr[i].srCompany.contactName, contName, SZ_CONTACT);
                strcopy(sr[i].srCompany.contactNumber, contNum, SZ_CONTACT);
                strcopy(sr[i].srInvoice.number, invNum, SZ_INVNUM);
                modifyCount++;
                printf("\n  [~] Record %s modified.\n", sr[i].srCode);
            }
            else {
                printf("\n  [-] Modification cancelled.\n");
            }
            break;
        }

        if (!found)
            printf("\n  [!] No record found with code: %s\n", srCode);

        more = readYesNo("\n  Modify another record?");

    } while (more == 'Y');

    srSaveAllRecords(sr, count);
    printf("\n  %d record(s) modified.\n\n", modifyCount);
}

void srDeleteRecord(void) {
    SR sr[MAX_RECORDS];
    int count = srLoadAllRecords(sr, MAX_RECORDS);

    srPrintSectionBanner("DELETE RECORD");

    if (count <= 0) {
        printf("\n  No records found.\n\n");
        return;
    }

    char srCode[SZ_CODE];
    int  found, deleteCount = 0;
    char more = 'Y';

    do {
        found = 0;
        readLine("SR Code to delete", srCode, SZ_CODE);

        for (int i = 0; i < count; i++) {
            if (strcmp(sr[i].srCode, srCode) != 0) continue;
            found = 1;

            printf("\n  Record to delete:\n");
            srPrintTableHeader();
            srPrintTableRow(&sr[i], i + 1);
            srPrintTableBorder();

            if (readYesNo("\n  Confirm deletion?") == 'Y') {
                /* Shift all subsequent records one position left. */
                for (int j = i; j < count - 1; j++)
                    sr[j] = sr[j + 1];
                count--;
                deleteCount++;
                printf("\n  [-] Record %s deleted.\n", srCode);
            }
            else {
                printf("\n  [-] Deletion cancelled.\n");
            }
            break;
        }

        if (!found)
            printf("\n  [!] No record found with code: %s\n", srCode);

        more = readYesNo("\n  Delete another record?");

    } while (more == 'Y');

    srSaveAllRecords(sr, count);
    printf("\n  %d record(s) deleted.\n\n", deleteCount);
}

/*
 * Scan all records to find the highest and lowest invoice price.
 * Initialises from the first record so the result is always a real value,
 * avoiding the need for a sentinel like 0 or 999999.
 */
void srReportRecord(void) {
    SR sr[MAX_RECORDS];
    int count = srLoadAllRecords(sr, MAX_RECORDS);

    srPrintSectionBanner("PRICE REPORT");

    if (count <= 0) {
        printf("\n  No records available.\n\n");
        return;
    }

    double highest = sr[0].srInvoice.price;
    double lowest = sr[0].srInvoice.price;
    char   highCompany[SZ_COMPANY], lowCompany[SZ_COMPANY];
    strcopy(highCompany, sr[0].srCompany.companyName, SZ_COMPANY);
    strcopy(lowCompany, sr[0].srCompany.companyName, SZ_COMPANY);

    for (int i = 1; i < count; i++) {
        double price = sr[i].srInvoice.price;
        if (price > highest) {
            highest = price;
            strcopy(highCompany, sr[i].srCompany.companyName, SZ_COMPANY);
        }
        if (price < lowest) {
            lowest = price;
            strcopy(lowCompany, sr[i].srCompany.companyName, SZ_COMPANY);
        }
    }

    printf("\n");
    printf("  Total records : %d\n\n", count);
    printf("  Highest price : RM %.2f\n", highest);
    printf("  Company       : %s\n\n", highCompany);
    printf("  Lowest price  : RM %.2f\n", lowest);
    printf("  Company       : %s\n\n", lowCompany);
}

/* =========================================================================
   Entry point
   ========================================================================= */

int main(void) {
    int choice;

    srLogo();

    do {
        printf("\n");
        srMenu();

        if (!readInt("\n  Choice", &choice) || choice < 1 || choice > 7) {
            printf("\n  [!] Invalid choice. Enter 1-7.\n");
            choice = 0;
            continue;
        }

        switch (choice) {
        case 1: srDisplayRecord(); break;
        case 2: srAddRecord();     break;
        case 3: srSearchRecord();  break;
        case 4: srModifyRecord();  break;
        case 5: srDeleteRecord();  break;
        case 6: srReportRecord();  break;
        case 7: printf("\n  Goodbye.\n\n"); break;
        }
    } while (choice != 7);

    return 0;
}