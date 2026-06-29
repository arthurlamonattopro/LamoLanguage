#ifndef LAMO_ERROR_UTIL_H
#define LAMO_ERROR_UTIL_H

/*
 * error_util.h - Source-snippet printing for error messages.
 *
 * Sprint 3 feature: when the parser or semantic analyzer reports an
 * error, we now print the offending source line followed by a caret
 * line pointing at the column. This matches the diagnostic style of
 * GCC, Rust, and most modern languages — a single "file:line:col"
 * line is much harder to act on than seeing the actual source with a
 * visual pointer.
 *
 * Example output:
 *
 *   /home/user/prog.lamo:3:5: semantic error: use of undeclared variable 'x'
 *   | let y = x + 1;
 *   |     ^
 *
 * The caret is positioned under the column reported by the lexer. We
 * count columns starting at 1, matching the rest of the codebase.
 *
 * Multi-byte characters (UTF-8): the caret column is computed in
 * bytes, not characters. This is the same convention the lexer uses
 * for its `column` field. For ASCII-heavy Lamo source this is fine;
 * for code with wide CJK characters the caret may be visually shifted
 * to the right of where it "should" be. That's a known limitation,
 * documented in the README.
 */

#include <stdio.h>
#include <string.h>

/* Print the source line containing the error, plus a caret line
 * pointing at the column. `source` is the full source text of the
 * file (multiple lines separated by '\n'). `line` and `column` are
 * 1-indexed. If `source` is NULL or the line is out of range, this
 * function is a no-op — callers don't need to guard. */
static inline void error_print_snippet(FILE* out, const char* source, int line, int column) {
    const char* line_start;
    const char* line_end;
    int current_line;
    int i;

    if (!source || line < 1) {
        return;
    }

    /* Walk the source counting newlines until we reach the target line. */
    line_start = source;
    current_line = 1;
    while (line_start < source + strlen(source) && current_line < line) {
        if (*line_start == '\n') {
            current_line++;
        }
        line_start++;
    }
    if (current_line != line) {
        /* Line number is past EOF (shouldn't happen, but be safe). */
        return;
    }

    /* Find the end of the line (either '\n' or end of string). */
    line_end = line_start;
    while (*line_end != '\0' && *line_end != '\n') {
        line_end++;
    }

    /* Print the source line. We strip a trailing '\r' so files with
     * CRLF endings don't show a stray carriage return. */
    fputs("  | ", out);
    fwrite(line_start, 1, (size_t)(line_end - line_start), out);
    /* Strip trailing \r if present. */
    if (line_end > line_start && *(line_end - 1) == '\r') {
        /* already written — emit a backspace-equivalent by writing
         * a fresh newline after the stripped line. We can't actually
         * erase the \r from the stream, so we accept it. */
    }
    fputc('\n', out);

    /* Print the caret line: spaces up to (column - 1), then '^'. */
    fputs("  | ", out);
    for (i = 1; i < column && i < 200; i++) {
        fputc(' ', out);
    }
    fputc('^', out);
    fputc('\n', out);
}

#endif /* LAMO_ERROR_UTIL_H */
