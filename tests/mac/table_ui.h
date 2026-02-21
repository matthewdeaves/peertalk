/**
 * @file table_ui.h
 * @brief Lightweight table display component for Mac test apps
 *
 * Provides a reusable table UI with static allocation (no malloc).
 * Builds on status_window.h for rendering.
 *
 * Usage:
 *   TableUI table;
 *   table_init(&table, "Results", 5, 4);
 *   table_set_header(&table, 0, "Size", 6, TABLE_ALIGN_RIGHT);
 *   table_set_header(&table, 1, "Send", 6, TABLE_ALIGN_RIGHT);
 *   table_set_cell_int(&table, 0, 0, 256);
 *   table_set_cell_kbps(&table, 0, 1, 18432);  // Shows "18 KB/s"
 *   table_set_current_row(&table, 0);          // Mark row 0 with "*"
 *   table_render(&table);
 */

#ifndef TABLE_UI_H
#define TABLE_UI_H

#include <stdint.h>

/* Table limits - chosen for memory efficiency on Classic Mac */
#define TABLE_MAX_COLS      8
#define TABLE_MAX_ROWS      16
#define TABLE_MAX_CELL_LEN  12
#define TABLE_MAX_TITLE_LEN 32

/* Column alignment */
typedef enum {
    TABLE_ALIGN_LEFT,
    TABLE_ALIGN_RIGHT,
    TABLE_ALIGN_CENTER
} TableAlign;

/* Cell value type */
typedef enum {
    TABLE_CELL_EMPTY,       /* No value */
    TABLE_CELL_STR,         /* String value */
    TABLE_CELL_INT,         /* Signed integer */
    TABLE_CELL_UINT,        /* Unsigned integer */
    TABLE_CELL_KBPS,        /* KB/s (value is bytes/sec, displayed as KB/s) */
    TABLE_CELL_MS           /* Milliseconds (value is ticks, converted to ms) */
} TableCellType;

/* Column definition */
typedef struct {
    char        name[TABLE_MAX_CELL_LEN];
    uint8_t     width;      /* Display width in characters */
    TableAlign  align;
} TableColumn;

/* Cell value */
typedef struct {
    TableCellType   type;
    union {
        char        str[TABLE_MAX_CELL_LEN];
        int32_t     i;
        uint32_t    u;
    } value;
} TableCell;

/* Table structure - fully static allocation */
typedef struct {
    char        title[TABLE_MAX_TITLE_LEN];
    uint8_t     num_rows;
    uint8_t     num_cols;
    int8_t      current_row;    /* -1 = none, shows "*" marker next to this row */
    TableColumn columns[TABLE_MAX_COLS];
    TableCell   cells[TABLE_MAX_ROWS][TABLE_MAX_COLS];
} TableUI;

/**
 * Initialize a table
 *
 * @param table     Table structure to initialize
 * @param title     Table title (displayed above table, NULL for none)
 * @param rows      Number of data rows (max TABLE_MAX_ROWS)
 * @param cols      Number of columns (max TABLE_MAX_COLS)
 */
void table_init(TableUI *table, const char *title, uint8_t rows, uint8_t cols);

/**
 * Set column header
 *
 * @param table     Table structure
 * @param col       Column index (0-based)
 * @param name      Column header text
 * @param width     Display width in characters
 * @param align     Text alignment
 */
void table_set_header(TableUI *table, uint8_t col, const char *name,
                      uint8_t width, TableAlign align);

/**
 * Set cell to signed integer value
 */
void table_set_cell_int(TableUI *table, uint8_t row, uint8_t col, int32_t value);

/**
 * Set cell to unsigned integer value
 */
void table_set_cell_uint(TableUI *table, uint8_t row, uint8_t col, uint32_t value);

/**
 * Set cell to string value
 */
void table_set_cell_str(TableUI *table, uint8_t row, uint8_t col, const char *value);

/**
 * Set cell to KB/s value (input is bytes/sec)
 * Displays as "18" for 18 KB/s
 */
void table_set_cell_kbps(TableUI *table, uint8_t row, uint8_t col, uint32_t bytes_per_sec);

/**
 * Set cell to milliseconds value (input is ticks, 60 ticks/sec)
 * Displays as "17" for 17ms
 */
void table_set_cell_ms(TableUI *table, uint8_t row, uint8_t col, uint32_t ticks);

/**
 * Clear a cell (show "--")
 */
void table_clear_cell(TableUI *table, uint8_t row, uint8_t col);

/**
 * Set current row marker
 *
 * @param table     Table structure
 * @param row       Row to mark with "*", or -1 for none
 */
void table_set_current_row(TableUI *table, int8_t row);

/**
 * Render table to status window
 *
 * Clears the status window and renders the full table.
 */
void table_render(TableUI *table);

#endif /* TABLE_UI_H */
