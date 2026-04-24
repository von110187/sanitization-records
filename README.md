# Sanitization Records Module

A console-based **Sanitization Records Management System** written in C, developed as part of a diploma group project. This module — handled independently — manages full CRUD operations for corporate sanitization service records, persisting data in a pipe-delimited flat file.

---

## Features

- **Display** all sanitization service records in a formatted table
- **Add** new records with guided, validated input
- **Search** records by month and year
- **Modify** existing records by SR Code
- **Delete** records by SR Code
- **Price Report** — highlights the highest and lowest invoice prices across all records

---

## Project Structure

```
SanitizationRecords/
├── SanitizationRecords/
│   ├── sanitization_records.c      # Main source file (all logic)
│   └── sanitizationRecords.txt     # Flat-file data store (pipe-delimited)
└── SanitizationRecords.slnx        # Visual Studio solution file
```

---

## Data Storage

Records are stored one per line in `sanitizationRecords.txt` using a pipe (`|`) delimiter:

```
SR001|1|1|2021|10|0|11|0|5|Lab301|AA Company|Alex|012-3456789|INV001|500.00|1|7|2021
```

**Field order:** SR Code | Day | Month | Year | Start Hrs | Start Mins | End Hrs | End Mins | People | Venue | Company | Contact Name | Contact Number | Invoice No | Price | Due Day | Due Month | Due Year

---

## Building

**MSVC (Visual Studio):**
```
cl sanitization_records.c
```

**GCC:**
```bash
gcc -std=c99 -Wall -o sanitization_records sanitization_records.c
```

> `_CRT_SECURE_NO_WARNINGS` is defined in the source to suppress MSVC deprecation warnings for standard C functions used safely with bounded writes and `fgets`-based input.

---

## Running

```bash
./sanitization_records
```

The program looks for `sanitizationRecords.txt` in the **current working directory**. If the file does not exist, the system starts with an empty record set and creates the file on the first add.

---

## Input Validation

All user input is validated before being accepted:

| Field | Rule |
|---|---|
| SR Code | Must start with `SR` followed by digits (e.g. `SR001`); must be unique |
| Invoice Number | Must start with `INV` followed by digits (e.g. `INV001`) |
| Contact Number | Malaysian format: `XXX-XXXXXXX` or `XXX-XXXXXXXX` |
| Date | Valid calendar date between 01-01-2000 and 31-12-2100, including leap years |
| Time | Valid 24-hour clock; end time must be strictly after start time |
| People | Must be ≥ 1 |
| Price | Must be > 0 |

---

## Limitations

- Maximum of **100 records** in memory at once (`MAX_RECORDS`)
- Field widths are fixed (see `SZ_*` constants in source) — truncation occurs if input exceeds limits
- No concurrent access control; intended for single-user console use
