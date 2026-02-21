/**
 * @file table_ui.c
 * @brief Table UI implementation for Mac test apps
 *
 * Uses status_window.h for rendering. All allocation is static.
 */

#include "table_ui.h"
#include "status_window.h"

#include <stdio.h>
#include <string.h>

/* ========================================================================== */
/* Utility Functions                                                           */
/* ========================================================================== */

/**
 * Convert ticks to milliseconds (60 ticks/sec on Mac)
 */
static unsigned long ticks_to_ms(unsigned long ticks)
{
    return (ticks * 1000UL) / 60UL;
}

/**
 * Format a cell value into a string buffer
 *
 * @param cell      Cell to format
 * @param buf       Output buffer
 * @param buflen    Buffer size
 */
static void format_cell(const TableCell *cell, char *buf, size_t buflen)
{
    switch (cell->type) {
    case TABLE_CELL_EMPTY:
        strncpy(buf, "--", buflen - 1);
        buf[buflen - 1] = '\0';
        break;

    case TABLE_CELL_STR:
        strncpy(buf, cell->value.str, buflen - 1);
        buf[buflen - 1] = '\0';
        break;

    case TABLE_CELL_INT:
        snprintf(buf, buflen, "%ld", (long)cell->value.i);
        break;

    case TABLE_CELL_UINT:
        snprintf(buf, buflen, "%lu", (unsigned long)cell->value.u);
        break;

    case TABLE_CELL_KBPS:
        /* Convert bytes/sec to KB/s */
        snprintf(buf, buflen, "%lu", (unsigned long)(cell->value.u / 1024UL));
        break;

    case TABLE_CELL_MS:
        /* Convert ticks to milliseconds */
        snprintf(buf, buflen, "%lu", ticks_to_ms(cell->value.u));
        break;

    default:
        buf[0] = '\0';
        break;
    }
}

/**
 * Align a string within a field width
 *
 * @param src       Source string
 * @param dst       Destination buffer (must be at least width+1 bytes)
 * @param width     Field width
 * @param align     Alignment
 */
static void align_str(const char *src, char *dst, uint8_t width, TableAlign align)
{
    size_t len = strlen(src);
    size_t pad;
    size_t i;

    /* Truncate if too long */
    if (len > width) {
        len = width;
    }

    /* Initialize with spaces */
    for (i = 0; i < width; i++) {
        dst[i] = ' ';
    }
    dst[width] = '\0';

    /* Calculate padding based on alignment */
    switch (align) {
    case TABLE_ALIGN_LEFT:
        pad = 0;
        break;
    case TABLE_ALIGN_RIGHT:
        pad = width - len;
        break;
    case TABLE_ALIGN_CENTER:
        pad = (width - len) / 2;
        break;
    default:
        pad = 0;
        break;
    }

    /* Copy string at padded position */
    memcpy(dst + pad, src, len);
}

/* ========================================================================== */
/* Public API                                                                  */
/* ========================================================================== */

void table_init(TableUI *table, const char *title, uint8_t rows, uint8_t cols)
{
    memset(table, 0, sizeof(TableUI));

    if (title) {
        strncpy(table->title, title, TABLE_MAX_TITLE_LEN - 1);
        table->title[TABLE_MAX_TITLE_LEN - 1] = '\0';
    }

    table->num_rows = (rows > TABLE_MAX_ROWS) ? TABLE_MAX_ROWS : rows;
    table->num_cols = (cols > TABLE_MAX_COLS) ? TABLE_MAX_COLS : cols;
    table->current_row = -1;
}

void table_set_header(TableUI *table, uint8_t col, const char *name,
                      uint8_t width, TableAlign align)
{
    if (col >= table->num_cols) return;

    strncpy(table->columns[col].name, name, TABLE_MAX_CELL_LEN - 1);
    table->columns[col].name[TABLE_MAX_CELL_LEN - 1] = '\0';
    table->columns[col].width = width;
    table->columns[col].align = align;
}

void table_set_cell_int(TableUI *table, uint8_t row, uint8_t col, int32_t value)
{
    if (row >= table->num_rows || col >= table->num_cols) return;

    table->cells[row][col].type = TABLE_CELL_INT;
    table->cells[row][col].value.i = value;
}

void table_set_cell_uint(TableUI *table, uint8_t row, uint8_t col, uint32_t value)
{
    if (row >= table->num_rows || col >= table->num_cols) return;

    table->cells[row][col].type = TABLE_CELL_UINT;
    table->cells[row][col].value.u = value;
}

void table_set_cell_str(TableUI *table, uint8_t row, uint8_t col, const char *value)
{
    if (row >= table->num_rows || col >= table->num_cols) return;

    table->cells[row][col].type = TABLE_CELL_STR;
    strncpy(table->cells[row][col].value.str, value, TABLE_MAX_CELL_LEN - 1);
    table->cells[row][col].value.str[TABLE_MAX_CELL_LEN - 1] = '\0';
}

void table_set_cell_kbps(TableUI *table, uint8_t row, uint8_t col, uint32_t bytes_per_sec)
{
    if (row >= table->num_rows || col >= table->num_cols) return;

    table->cells[row][col].type = TABLE_CELL_KBPS;
    table->cells[row][col].value.u = bytes_per_sec;
}

void table_set_cell_ms(TableUI *table, uint8_t row, uint8_t col, uint32_t ticks)
{
    if (row >= table->num_rows || col >= table->num_cols) return;

    table->cells[row][col].type = TABLE_CELL_MS;
    table->cells[row][col].value.u = ticks;
}

void table_clear_cell(TableUI *table, uint8_t row, uint8_t col)
{
    if (row >= table->num_rows || col >= table->num_cols) return;

    table->cells[row][col].type = TABLE_CELL_EMPTY;
}

void table_set_current_row(TableUI *table, int8_t row)
{
    table->current_row = row;
}

void table_render(TableUI *table)
{
    char line[128];
    char cell_buf[TABLE_MAX_CELL_LEN + 1];
    char aligned[TABLE_MAX_CELL_LEN + 1];
    size_t pos;
    uint8_t col, row;

    status_clear();

    /* Title line */
    if (table->title[0] != '\0') {
        status_line(table->title);
    }

    /* Header line */
    pos = 0;
    line[0] = '\0';

    /* Leave space for row marker */
    line[pos++] = ' ';
    line[pos++] = ' ';

    for (col = 0; col < table->num_cols; col++) {
        TableColumn *c = &table->columns[col];

        align_str(c->name, aligned, c->width, c->align);

        /* Add space between columns */
        if (col > 0 && pos < sizeof(line) - 1) {
            line[pos++] = ' ';
        }

        /* Append aligned header */
        if (pos + c->width < sizeof(line)) {
            memcpy(&line[pos], aligned, c->width);
            pos += c->width;
        }
    }
    line[pos] = '\0';
    status_line(line);

    /* Separator line */
    pos = 0;
    line[pos++] = ' ';
    line[pos++] = ' ';

    for (col = 0; col < table->num_cols; col++) {
        TableColumn *c = &table->columns[col];
        uint8_t i;

        if (col > 0 && pos < sizeof(line) - 1) {
            line[pos++] = ' ';
        }

        for (i = 0; i < c->width && pos < sizeof(line) - 1; i++) {
            line[pos++] = '-';
        }
    }
    line[pos] = '\0';
    status_line(line);

    /* Data rows */
    for (row = 0; row < table->num_rows; row++) {
        pos = 0;

        /* Row marker */
        if (row == table->current_row) {
            line[pos++] = '*';
        } else {
            line[pos++] = ' ';
        }
        line[pos++] = ' ';

        for (col = 0; col < table->num_cols; col++) {
            TableColumn *c = &table->columns[col];
            TableCell *cell = &table->cells[row][col];

            /* Format cell value */
            format_cell(cell, cell_buf, sizeof(cell_buf));

            /* Align within column width */
            align_str(cell_buf, aligned, c->width, c->align);

            /* Add space between columns */
            if (col > 0 && pos < sizeof(line) - 1) {
                line[pos++] = ' ';
            }

            /* Append aligned cell */
            if (pos + c->width < sizeof(line)) {
                memcpy(&line[pos], aligned, c->width);
                pos += c->width;
            }
        }
        line[pos] = '\0';
        status_line(line);
    }
}
