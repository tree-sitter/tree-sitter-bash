#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <wctype.h>

#include "tree_sitter/parser.h"

#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define STRING_RESIZE(vec, _cap)                                               \
    void *tmp = realloc((vec).data, ((_cap) + 1) * sizeof((vec).data[0]));     \
    assert(tmp != NULL);                                                       \
    (vec).data = tmp;                                                          \
    memset((vec).data + (vec).len, 0,                                          \
           (((_cap) + 1) - (vec).len) * sizeof((vec).data[0]));                \
    (vec).cap = (_cap);

#define STRING_GROW(vec, _cap)                                                 \
    if ((vec).cap < (_cap)) {                                                  \
        STRING_RESIZE((vec), (_cap));                                          \
    }

#define STRING_PUSH(vec, el)                                                   \
    if ((vec).cap == (vec).len) {                                              \
        STRING_RESIZE((vec), MAX(16, (vec).len * 2));                          \
    }                                                                          \
    (vec).data[(vec).len++] = (el);

#define STRING_FREE(vec)                                                       \
    if ((vec).data != NULL)                                                    \
        free((vec).data);

#define STRING_CLEAR(vec)                                                      \
    {                                                                          \
        (vec).len = 0;                                                         \
        memset((vec).data, 0, (vec).cap * sizeof(char));                       \
    }

enum TokenType {
    HEREDOC_START,
    SIMPLE_HEREDOC_BODY,
    HEREDOC_BODY_BEGINNING,
    HEREDOC_CONTENT,
    HEREDOC_END,
    FILE_DESCRIPTOR,
    EMPTY_VALUE,
    CONCAT,
    VARIABLE_NAME,
    TEST_OPERATOR,
    REGEX,
    REGEX_NO_SLASH,
    REGEX_NO_SPACE,
    EXPANSION_WORD,
    EXTGLOB_PATTERN,
    BARE_DOLLAR,
    BRACE_START,
    IMMEDIATE_DOUBLE_HASH,
    EXTERNAL_EXPANSION_SYM_HASH,
    EXTERNAL_EXPANSION_SYM_BANG,
    EXTERNAL_EXPANSION_SYM_EQUAL,
    CLOSING_BRACE,
    CLOSING_BRACKET,
    HEREDOC_ARROW,
    HEREDOC_ARROW_DASH,
    NEWLINE,
    ERROR_RECOVERY,
};

typedef struct {
    uint32_t cap;
    uint32_t len;
    char *data;
} String;

static String string_new() {
    return (String){.cap = 16, .len = 0, .data = calloc(1, sizeof(char) * 17)};
}

typedef struct {
    bool heredoc_is_raw;
    bool started_heredoc;
    bool heredoc_allows_indent;
    uint8_t last_glob_paren_depth;
    String heredoc_delimiter;
    String current_leading_word;
} Scanner;

static inline void advance(TSLexer *lexer) { lexer->advance(lexer, false); }

static inline void skip(TSLexer *lexer) { lexer->advance(lexer, true); }

static inline bool in_error_recovery(const bool *valid_symbols) {
    return valid_symbols[ERROR_RECOVERY];
}

static inline void reset(Scanner *scanner) {
    scanner->heredoc_is_raw = false;
    scanner->started_heredoc = false;
    scanner->heredoc_allows_indent = false;
    STRING_CLEAR(scanner->heredoc_delimiter);
}

static unsigned serialize(Scanner *scanner, char *buffer) {
    if (scanner->heredoc_delimiter.len + 4 >=
        TREE_SITTER_SERIALIZATION_BUFFER_SIZE) {
        return 0;
    }
    buffer[0] = (char)scanner->heredoc_is_raw;
    buffer[1] = (char)scanner->started_heredoc;
    buffer[2] = (char)scanner->heredoc_allows_indent;
    buffer[3] = (char)scanner->last_glob_paren_depth;
    memcpy(&buffer[4], scanner->heredoc_delimiter.data,
           scanner->heredoc_delimiter.len);
    return scanner->heredoc_delimiter.len + 4;
}

static void deserialize(Scanner *scanner, const char *buffer, unsigned length) {
    if (length == 0) {
        reset(scanner);
    } else {
        scanner->heredoc_is_raw = buffer[0];
        scanner->started_heredoc = buffer[1];
        scanner->heredoc_allows_indent = buffer[2];
        scanner->last_glob_paren_depth = buffer[3];
        scanner->heredoc_delimiter.len = length - 4;
        STRING_GROW(scanner->heredoc_delimiter, scanner->heredoc_delimiter.len);
        memcpy(scanner->heredoc_delimiter.data, &buffer[4],
               scanner->heredoc_delimiter.len);
    }
}

/**
 * Consume a "word" in POSIX parlance, and returns it unquoted.
 *
 * This is an approximate implementation that doesn't deal with any
 * POSIX-mandated substitution, and assumes the default value for
 * IFS.
 */
static bool advance_word(TSLexer *lexer, String *unquoted_word) {
    bool empty = true;

    int32_t quote = 0;
    if (lexer->lookahead == '\'' || lexer->lookahead == '"') {
        quote = lexer->lookahead;
        advance(lexer);
    }

    while (lexer->lookahead &&
           !(quote ? lexer->lookahead == quote : iswspace(lexer->lookahead))) {
        if (lexer->lookahead == '\\') {
            advance(lexer);
            if (!lexer->lookahead) {
                return false;
            }
        }
        empty = false;
        STRING_PUSH(*unquoted_word, lexer->lookahead);
        advance(lexer);
    }

    if (quote && lexer->lookahead == quote) {
        advance(lexer);
    }

    return !empty;
}

static inline bool scan_bare_dollar(TSLexer *lexer) {
    while (iswspace(lexer->lookahead) && lexer->lookahead != '\n' &&
           !lexer->eof(lexer)) {
        skip(lexer);
    }

    if (lexer->lookahead == '$') {
        advance(lexer);
        lexer->result_symbol = BARE_DOLLAR;
        lexer->mark_end(lexer);
        return iswspace(lexer->lookahead) || lexer->eof(lexer);
               lexer->lookahead == '\"';
    }

    return false;
}

static bool scan_heredoc_start(Scanner *scanner, TSLexer *lexer) {
    while (iswspace(lexer->lookahead)) {
        skip(lexer);
    }

    lexer->result_symbol = HEREDOC_START;
    scanner->heredoc_is_raw = lexer->lookahead == '\'' ||
                              lexer->lookahead == '"' ||
                              lexer->lookahead == '\\';
    scanner->started_heredoc = false;
    STRING_CLEAR(scanner->heredoc_delimiter);

    bool found_delimiter = advance_word(lexer, &scanner->heredoc_delimiter);
    if (!found_delimiter)
        STRING_CLEAR(scanner->heredoc_delimiter);
    return found_delimiter;
}

static bool scan_heredoc_end_identifier(Scanner *scanner, TSLexer *lexer) {
    STRING_CLEAR(scanner->current_leading_word);
    // Scan the first 'n' characters on this line, to see if they match the
    // heredoc delimiter
    int32_t size = 0;
    while (lexer->lookahead != '\0' && lexer->lookahead != '\n' &&
           ((int32_t)scanner->heredoc_delimiter.data[size++]) ==
               lexer->lookahead &&
           scanner->current_leading_word.len < scanner->heredoc_delimiter.len) {
        STRING_PUSH(scanner->current_leading_word, lexer->lookahead);
        advance(lexer);
    }
    return strcmp(scanner->current_leading_word.data,
                  scanner->heredoc_delimiter.data) == 0;
}

static bool scan_heredoc_content(Scanner *scanner, TSLexer *lexer,
                                 enum TokenType middle_type,
                                 enum TokenType end_type) {
    bool did_advance = false;

    for (;;) {
        switch (lexer->lookahead) {
            case '\0': {
                if (lexer->eof(lexer) && did_advance) {
                    reset(scanner);
                    lexer->result_symbol = end_type;
                    return true;
                }
                return false;
            }

            case '\\': {
                did_advance = true;
                advance(lexer);
                advance(lexer);
                break;
            }

            case '$': {
                if (scanner->heredoc_is_raw) {
                    did_advance = true;
                    advance(lexer);
                    break;
                }
                if (did_advance) {
                    lexer->mark_end(lexer);
                    lexer->result_symbol = middle_type;
                    scanner->started_heredoc = true;
                    advance(lexer);
                    if (isalpha(lexer->lookahead) || lexer->lookahead == '{') {
                        return true;
                    }
                    break;
                }
                if (middle_type == HEREDOC_BODY_BEGINNING &&
                    lexer->get_column(lexer) == 0) {
                    lexer->result_symbol = middle_type;
                    scanner->started_heredoc = true;
                    return true;
                }
                return false;
            }

            case '\n': {
                if (!did_advance) {
                    skip(lexer);
                } else {
                    advance(lexer);
                }
                did_advance = true;
                if (scanner->heredoc_allows_indent) {
                    while (iswspace(lexer->lookahead)) {
                        advance(lexer);
                    }
                }
                lexer->result_symbol =
                    scanner->started_heredoc ? middle_type : end_type;
                lexer->mark_end(lexer);
                if (scan_heredoc_end_identifier(scanner, lexer)) {
                    return true;
                }
                break;
            }

            default: {
                if (lexer->get_column(lexer) == 0) {
                    // an alternative is to check the starting column of the
                    // heredoc body and track that statefully
                    while (iswspace(lexer->lookahead)) {
                        did_advance ? advance(lexer) : skip(lexer);
                    }
                    if (end_type != SIMPLE_HEREDOC_BODY) {
                        lexer->result_symbol = middle_type;
                        if (scan_heredoc_end_identifier(scanner, lexer)) {
                            return true;
                        }
                    }
                    if (end_type == SIMPLE_HEREDOC_BODY) {
                        lexer->result_symbol = end_type;
                        lexer->mark_end(lexer);
                        if (scan_heredoc_end_identifier(scanner, lexer)) {
                            return true;
                        }
                    }
                }
                did_advance = true;
                advance(lexer);
                break;
            }
        }
    }
}

static bool scan(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols) {
    if (valid_symbols[CONCAT] && !in_error_recovery(valid_symbols)) {
        if (!(lexer->lookahead == 0 || iswspace(lexer->lookahead) ||
              lexer->lookahead == '>' || lexer->lookahead == '<' ||
              lexer->lookahead == ')' || lexer->lookahead == '(' ||
              lexer->lookahead == ';' || lexer->lookahead == '&' ||
              lexer->lookahead == '|' ||
              (lexer->lookahead == '}' && valid_symbols[CLOSING_BRACE]) ||
              (lexer->lookahead == ']' && valid_symbols[CLOSING_BRACKET]))) {
            lexer->result_symbol = CONCAT;
            // So for a`b`, we want to return a concat. We check if the 2nd
            // backtick has whitespace after it, and if it does we return
            // concat.
            if (lexer->lookahead == '`') {
                lexer->mark_end(lexer);
                advance(lexer);
                while (lexer->lookahead != '`' && !lexer->eof(lexer)) {
                    advance(lexer);
                }
                if (lexer->eof(lexer)) {
                    return false;
                }
                if (lexer->lookahead == '`') {
                    advance(lexer);
                }
                return iswspace(lexer->lookahead) || lexer->eof(lexer);
            }
            // strings w/ expansions that contains escaped quotes or backslashes
            // need this to return a concat
            if (lexer->lookahead == '\\') {
                lexer->mark_end(lexer);
                advance(lexer);
                if (lexer->lookahead == '"' || lexer->lookahead == '\'' ||
                    lexer->lookahead == '\\') {
                    return true;
                }
                if (lexer->eof(lexer)) {
                    return false;
                }
            } else {
                return true;
            }
        }
        if (iswspace(lexer->lookahead) && valid_symbols[CLOSING_BRACE] &&
            !valid_symbols[EXPANSION_WORD]) {
            lexer->result_symbol = CONCAT;
            return true;
        }
    }

    if (valid_symbols[IMMEDIATE_DOUBLE_HASH] &&
        !in_error_recovery(valid_symbols)) {
        // advance two # and ensure not } after
        if (lexer->lookahead == '#') {
            lexer->mark_end(lexer);
            advance(lexer);
            if (lexer->lookahead == '#') {
                advance(lexer);
                if (lexer->lookahead != '}') {
                    lexer->result_symbol = IMMEDIATE_DOUBLE_HASH;
                    lexer->mark_end(lexer);
                    return true;
                }
            }
        }
    }

    if (valid_symbols[EXTERNAL_EXPANSION_SYM_HASH] &&
        !in_error_recovery(valid_symbols)) {
        if (lexer->lookahead == '#' || lexer->lookahead == '=' ||
            lexer->lookahead == '!') {
            lexer->result_symbol =
                lexer->lookahead == '#'   ? EXTERNAL_EXPANSION_SYM_HASH
                : lexer->lookahead == '!' ? EXTERNAL_EXPANSION_SYM_BANG
                                          : EXTERNAL_EXPANSION_SYM_EQUAL;
            advance(lexer);
            lexer->mark_end(lexer);
            while (lexer->lookahead == '#' || lexer->lookahead == '=' ||
                   lexer->lookahead == '!') {
                advance(lexer);
            }
            while (iswspace(lexer->lookahead)) {
                skip(lexer);
            }
            if (lexer->lookahead == '}') {
                return true;
            }
            return false;
        }
    }

    if (valid_symbols[EMPTY_VALUE]) {
        if (iswspace(lexer->lookahead) || lexer->eof(lexer) ||
            lexer->lookahead == ';' || lexer->lookahead == '&') {
            lexer->result_symbol = EMPTY_VALUE;
            return true;
        }
    }

    if ((valid_symbols[HEREDOC_BODY_BEGINNING] ||
         valid_symbols[SIMPLE_HEREDOC_BODY]) &&
        scanner->heredoc_delimiter.len > 0 && !scanner->started_heredoc &&
        !in_error_recovery(valid_symbols)) {
        return scan_heredoc_content(scanner, lexer, HEREDOC_BODY_BEGINNING,
                                    SIMPLE_HEREDOC_BODY);
    }

    if (valid_symbols[HEREDOC_END]) {
        if (scan_heredoc_end_identifier(scanner, lexer)) {
            reset(scanner);
            lexer->result_symbol = HEREDOC_END;
            return true;
        }
    }

    if (valid_symbols[HEREDOC_CONTENT] && scanner->heredoc_delimiter.len > 0 &&
        scanner->started_heredoc && !in_error_recovery(valid_symbols)) {
        return scan_heredoc_content(scanner, lexer, HEREDOC_CONTENT,
                                    HEREDOC_END);
    }

    if (valid_symbols[HEREDOC_START] && !in_error_recovery(valid_symbols)) {
        return scan_heredoc_start(scanner, lexer);
    }

    if (valid_symbols[TEST_OPERATOR] && !valid_symbols[EXPANSION_WORD]) {
        while (iswspace(lexer->lookahead) && lexer->lookahead != '\n') {
            skip(lexer);
        }

        if (lexer->lookahead == '\\') {
            if (valid_symbols[EXTGLOB_PATTERN]) {
                goto extglob_pattern;
            }
            if (valid_symbols[REGEX_NO_SPACE]) {
                goto regex;
            }
            skip(lexer);

            if (lexer->eof(lexer)) {
                return false;
            }

            if (lexer->lookahead == '\r') {
                skip(lexer);
                if (lexer->lookahead == '\n') {
                    skip(lexer);
                }
            } else if (lexer->lookahead == '\n') {
                skip(lexer);
            } else {
                return false;
            }

            while (iswspace(lexer->lookahead)) {
                skip(lexer);
            }
        }

        if (lexer->lookahead == '\n' && !valid_symbols[NEWLINE]) {
            skip(lexer);

            while (iswspace(lexer->lookahead)) {
                skip(lexer);
            }
        }

        if (lexer->lookahead == '-') {
            advance(lexer);

            bool advanced_once = false;
            while (isalpha(lexer->lookahead)) {
                advanced_once = true;
                advance(lexer);
            }

            if (iswspace(lexer->lookahead) && advanced_once) {
                lexer->mark_end(lexer);
                advance(lexer);
                if (lexer->lookahead == '}' && valid_symbols[CLOSING_BRACE]) {
                    if (valid_symbols[EXPANSION_WORD]) {
                        lexer->mark_end(lexer);
                        lexer->result_symbol = EXPANSION_WORD;
                        return true;
                    }
                    return false;
                }
                lexer->result_symbol = TEST_OPERATOR;
                return true;
            }
            if (iswspace(lexer->lookahead) && valid_symbols[EXTGLOB_PATTERN]) {
                lexer->result_symbol = EXTGLOB_PATTERN;
                return true;
            }
        }

        if (valid_symbols[BARE_DOLLAR] && !in_error_recovery(valid_symbols) &&
            scan_bare_dollar(lexer)) {
            return true;
        }
    }

    if ((valid_symbols[VARIABLE_NAME] || valid_symbols[FILE_DESCRIPTOR] ||
         valid_symbols[HEREDOC_ARROW]) &&
        !valid_symbols[REGEX_NO_SLASH] && !in_error_recovery(valid_symbols)) {
        for (;;) {
            if ((lexer->lookahead == ' ' || lexer->lookahead == '\t' ||
                 lexer->lookahead == '\r' ||
                 (lexer->lookahead == '\n' && !valid_symbols[NEWLINE])) &&
                !valid_symbols[EXPANSION_WORD]) {
                skip(lexer);
            } else if (lexer->lookahead == '\\') {
                skip(lexer);

                if (lexer->eof(lexer)) {
                    lexer->mark_end(lexer);
                    lexer->result_symbol = VARIABLE_NAME;
                    return true;
                }

                if (lexer->lookahead == '\r') {
                    skip(lexer);
                }
                if (lexer->lookahead == '\n') {
                    skip(lexer);
                } else {
                    if (lexer->lookahead == '\\' &&
                        valid_symbols[EXPANSION_WORD]) {
                        goto expansion_word;
                    }
                    return false;
                }
            } else {
                break;
            }
        }

        // no '*', '@', '?', '-', '$', '0', '_'
        if (!valid_symbols[EXPANSION_WORD] &&
            (lexer->lookahead == '*' || lexer->lookahead == '@' ||
             lexer->lookahead == '?' || lexer->lookahead == '-' ||
             lexer->lookahead == '0' || lexer->lookahead == '_')) {
            lexer->mark_end(lexer);
            advance(lexer);
            if (lexer->lookahead == '=' || lexer->lookahead == '[' ||
                lexer->lookahead == ':' || lexer->lookahead == '-' ||
                lexer->lookahead == '%' || lexer->lookahead == '#' ||
                lexer->lookahead == '/') {
                return false;
            }
            if (valid_symbols[EXTGLOB_PATTERN] && iswspace(lexer->lookahead)) {
                lexer->mark_end(lexer);
                lexer->result_symbol = EXTGLOB_PATTERN;
                return true;
            }
        }

        if (valid_symbols[HEREDOC_ARROW] && lexer->lookahead == '<') {
            advance(lexer);
            if (lexer->lookahead == '<') {
                advance(lexer);
                if (lexer->lookahead == '-') {
                    advance(lexer);
                    scanner->heredoc_allows_indent = true;
                    lexer->result_symbol = HEREDOC_ARROW_DASH;
                } else if (lexer->lookahead == '<' || lexer->lookahead == '=') {
                    return false;
                } else {
                    scanner->heredoc_allows_indent = false;
                    lexer->result_symbol = HEREDOC_ARROW;
                }
                return true;
            }
            return false;
        }

        bool is_number = true;
        if (iswdigit(lexer->lookahead)) {
            advance(lexer);
        } else if (iswalpha(lexer->lookahead) || lexer->lookahead == '_') {
            is_number = false;
            advance(lexer);
        } else {
            if (lexer->lookahead == '{') {
                goto brace_start;
            }
            if (valid_symbols[EXPANSION_WORD]) {
                goto expansion_word;
            }
            if (valid_symbols[EXTGLOB_PATTERN]) {
                goto extglob_pattern;
            }
            return false;
        }

        for (;;) {
            if (iswdigit(lexer->lookahead)) {
                advance(lexer);
            } else if (iswalpha(lexer->lookahead) || lexer->lookahead == '_') {
                is_number = false;
                advance(lexer);
            } else {
                break;
            }
        }

        if (is_number && valid_symbols[FILE_DESCRIPTOR] &&
            (lexer->lookahead == '>' || lexer->lookahead == '<')) {
            lexer->result_symbol = FILE_DESCRIPTOR;
            return true;
        }

        if (valid_symbols[VARIABLE_NAME]) {
            if (lexer->lookahead == '+') {
                lexer->mark_end(lexer);
                advance(lexer);
                if (lexer->lookahead == '=' || lexer->lookahead == ':' ||
                    valid_symbols[CLOSING_BRACE]) {
                    lexer->result_symbol = VARIABLE_NAME;
                    return true;
                }
                return false;
            }
            if (lexer->lookahead == '/') {
                return false;
            }
            if (lexer->lookahead == '=' || lexer->lookahead == '[' ||
                (lexer->lookahead == ':' && !valid_symbols[CLOSING_BRACE]) ||
                lexer->lookahead == '%' ||
                (lexer->lookahead == '#' && !is_number) ||
                lexer->lookahead == '@' ||
                (lexer->lookahead == '-' && valid_symbols[CLOSING_BRACE])) {
                lexer->mark_end(lexer);
                lexer->result_symbol = VARIABLE_NAME;
                return true;
            }

            if (lexer->lookahead == '?') {
                lexer->mark_end(lexer);
                advance(lexer);
                lexer->result_symbol = VARIABLE_NAME;
                return isalpha(lexer->lookahead);
            }
        }

        return false;
    }

    if (valid_symbols[BARE_DOLLAR] && !in_error_recovery(valid_symbols) &&
        scan_bare_dollar(lexer)) {
        return true;
    }

regex:
    if ((valid_symbols[REGEX] || valid_symbols[REGEX_NO_SLASH] ||
         valid_symbols[REGEX_NO_SPACE]) &&
        !in_error_recovery(valid_symbols)) {
        if (valid_symbols[REGEX] || valid_symbols[REGEX_NO_SPACE]) {
            while (iswspace(lexer->lookahead)) {
                skip(lexer);
            }
        }

        if ((lexer->lookahead != '"' && lexer->lookahead != '\'') ||
            (lexer->lookahead == '$' && valid_symbols[REGEX_NO_SLASH])) {
            typedef struct {
                bool done;
                bool advanced_once;
                bool found_non_alnumdollarunderdash;
                uint32_t paren_depth;
                uint32_t bracket_depth;
                uint32_t brace_depth;
            } State;

            if (lexer->lookahead == '$' && valid_symbols[REGEX_NO_SLASH]) {
                lexer->mark_end(lexer);
                advance(lexer);
                if (lexer->lookahead == '(') {
                    return false;
                }
            }

            lexer->mark_end(lexer);

            State state = {false, false, false, 0, 0, 0};
            while (!state.done) {
                switch (lexer->lookahead) {
                    case '\0':
                        return false;
                    case '(':
                        state.paren_depth++;
                        break;
                    case '[':
                        state.bracket_depth++;
                        break;
                    case '{':
                        state.brace_depth++;
                        break;
                    case ')':
                        if (state.paren_depth == 0) {
                            state.done = true;
                        }
                        state.paren_depth--;
                        break;
                    case ']':
                        if (state.bracket_depth == 0) {
                            state.done = true;
                        }
                        state.bracket_depth--;
                        break;
                    case '}':
                        if (state.brace_depth == 0) {
                            state.done = true;
                        }
                        state.brace_depth--;
                        break;
                }

                if (!state.done) {
                    if (valid_symbols[REGEX]) {
                        bool was_space = iswspace(lexer->lookahead);
                        advance(lexer);
                        state.advanced_once = true;
                        if (!was_space || state.paren_depth > 0) {
                            lexer->mark_end(lexer);
                        }
                    } else if (valid_symbols[REGEX_NO_SLASH]) {
                        if (lexer->lookahead == '/') {
                            lexer->mark_end(lexer);
                            lexer->result_symbol = REGEX_NO_SLASH;
                            return state.advanced_once;
                        }
                        if (lexer->lookahead == '\\') {
                            advance(lexer);
                            state.advanced_once = true;
                            if (!lexer->eof(lexer) && lexer->lookahead != '[' &&
                                lexer->lookahead != '/') {
                                advance(lexer);
                                lexer->mark_end(lexer);
                            }
                        } else {
                            bool was_space = iswspace(lexer->lookahead);
                            advance(lexer);
                            state.advanced_once = true;
                            if (!was_space) {
                                lexer->mark_end(lexer);
                            }
                        }
                    } else if (valid_symbols[REGEX_NO_SPACE]) {
                        if (lexer->lookahead == '\\') {
                            state.found_non_alnumdollarunderdash = true;
                            advance(lexer);
                            if (!lexer->eof(lexer)) {
                                advance(lexer);
                            }
                        } else if (lexer->lookahead == '$') {
                            lexer->mark_end(lexer);
                            advance(lexer);
                            // do not parse a command
                            // substitution
                            if (lexer->lookahead == '(') {
                                return false;
                            }
                            // end $ always means regex, e.g.
                            // 99999999$
                            if (iswspace(lexer->lookahead)) {
                                lexer->result_symbol = REGEX_NO_SPACE;
                                lexer->mark_end(lexer);
                                return true;
                            }
                        } else {
                            if (iswspace(lexer->lookahead) &&
                                state.paren_depth == 0) {
                                lexer->mark_end(lexer);
                                lexer->result_symbol = REGEX_NO_SPACE;
                                return state.found_non_alnumdollarunderdash;
                            }
                            if (!iswalnum(lexer->lookahead) &&
                                lexer->lookahead != '$' &&
                                lexer->lookahead != '-' &&
                                lexer->lookahead != '_') {
                                state.found_non_alnumdollarunderdash = true;
                            }
                            advance(lexer);
                        }
                    }
                }
            }

            lexer->result_symbol =
                valid_symbols[REGEX_NO_SLASH]   ? REGEX_NO_SLASH
                : valid_symbols[REGEX_NO_SPACE] ? REGEX_NO_SPACE
                                                : REGEX;
            if (valid_symbols[REGEX] && !state.advanced_once) {
                return false;
            }
            return true;
        }
    }

extglob_pattern:
    if (valid_symbols[EXTGLOB_PATTERN]) {
        // first skip ws, then check for ? * + @ !
        while (iswspace(lexer->lookahead)) {
            skip(lexer);
        }

        if (lexer->lookahead == '?' || lexer->lookahead == '*' ||
            lexer->lookahead == '+' || lexer->lookahead == '@' ||
            lexer->lookahead == '!' || lexer->lookahead == '-' ||
            lexer->lookahead == ')' || lexer->lookahead == '\\' ||
            lexer->lookahead == '.') {
            if (lexer->lookahead == '\\') {
                advance(lexer);
                if ((iswspace(lexer->lookahead) || lexer->lookahead == '"') &&
                    lexer->lookahead != '\r' && lexer->lookahead != '\n') {
                    advance(lexer);
                } else {
                    return false;
                }
            }

            if (lexer->lookahead == ')' &&
                scanner->last_glob_paren_depth == 0) {
                lexer->mark_end(lexer);
                advance(lexer);

                if (iswspace(lexer->lookahead)) {
                    return false;
                }
            }

            lexer->mark_end(lexer);
            advance(lexer);

            // -\w is just a word, find something else special
            if (lexer->lookahead == '-') {
                lexer->mark_end(lexer);
                advance(lexer);
                while (isalnum(lexer->lookahead)) {
                    advance(lexer);
                }

                if (lexer->lookahead == ')' || lexer->lookahead == '\\' ||
                    lexer->lookahead == '.') {
                    return false;
                }
                lexer->mark_end(lexer);
            }

            // case item -) or *)
            if (lexer->lookahead == ')' &&
                scanner->last_glob_paren_depth == 0) {
                lexer->mark_end(lexer);
                advance(lexer);
                if (iswspace(lexer->lookahead)) {
                    lexer->result_symbol = EXTGLOB_PATTERN;
                    return true;
                }
            }

            if (iswspace(lexer->lookahead)) {
                lexer->mark_end(lexer);
                lexer->result_symbol = EXTGLOB_PATTERN;
                scanner->last_glob_paren_depth = 0;
                return true;
            }

            if (lexer->lookahead == '$') {
                lexer->mark_end(lexer);
                advance(lexer);
                if (lexer->lookahead == '{' || lexer->lookahead == '(') {
                    lexer->result_symbol = EXTGLOB_PATTERN;
                    return true;
                }
            }

            if (lexer->lookahead == '|') {
                lexer->mark_end(lexer);
                advance(lexer);
                if (lexer->lookahead == '\\' || lexer->lookahead == '\r' ||
                    lexer->lookahead == '\n') {
                    lexer->result_symbol = EXTGLOB_PATTERN;
                    return true;
                }
            }

            if (!isalnum(lexer->lookahead) && lexer->lookahead != '(' &&
                lexer->lookahead != '"' && lexer->lookahead != '[' &&
                lexer->lookahead != '?' && lexer->lookahead != '/' &&
                lexer->lookahead != '\\' && lexer->lookahead != '_') {
                return false;
            }

            typedef struct {
                bool done;
                uint32_t paren_depth;
                uint32_t bracket_depth;
                uint32_t brace_depth;
            } State;

            State state = {false, scanner->last_glob_paren_depth, 0, 0};
            while (!state.done) {
                switch (lexer->lookahead) {
                    case '\0':
                        return false;
                    case '(':
                        state.paren_depth++;
                        break;
                    case '[':
                        state.bracket_depth++;
                        break;
                    case '{':
                        state.brace_depth++;
                        break;
                    case ')':
                        if (state.paren_depth == 0) {
                            state.done = true;
                        }
                        state.paren_depth--;
                        break;
                    case ']':
                        if (state.bracket_depth == 0) {
                            state.done = true;
                        }
                        state.bracket_depth--;
                        break;
                    case '}':
                        if (state.brace_depth == 0) {
                            state.done = true;
                        }
                        state.brace_depth--;
                        break;
                }

                if (!state.done) {
                    bool was_space = iswspace(lexer->lookahead);
                    if (lexer->lookahead == '$') {
                        lexer->mark_end(lexer);
                        advance(lexer);
                        if (lexer->lookahead == '(' ||
                            lexer->lookahead == '{') {
                            lexer->result_symbol = EXTGLOB_PATTERN;
                            scanner->last_glob_paren_depth = state.paren_depth;
                            return true;
                        }
                    }
                    if (was_space) {
                        lexer->mark_end(lexer);
                        lexer->result_symbol = EXTGLOB_PATTERN;
                        scanner->last_glob_paren_depth = 0;
                        return true;
                    }
                    if (lexer->lookahead == '"') {
                        lexer->mark_end(lexer);
                        lexer->result_symbol = EXTGLOB_PATTERN;
                        scanner->last_glob_paren_depth = 0;
                        return true;
                    }
                    if (lexer->lookahead == '\\') {
                        advance(lexer);
                        if (iswspace(lexer->lookahead) ||
                            lexer->lookahead == '"') {
                            advance(lexer);
                        }
                    } else {
                        advance(lexer);
                    }
                    if (!was_space) {
                        lexer->mark_end(lexer);
                    }
                }
            }

            lexer->result_symbol = EXTGLOB_PATTERN;
            scanner->last_glob_paren_depth = 0;
            return true;
        }
        scanner->last_glob_paren_depth = 0;

        return false;
    }

expansion_word:
    if (valid_symbols[EXPANSION_WORD]) {
        bool advanced_once = false;
        bool advance_once_space = false;
        for (;;) {
            if (lexer->lookahead == '\"') {
                return false;
            }
            if (lexer->lookahead == '$') {
                lexer->mark_end(lexer);
                advance(lexer);
                if (lexer->lookahead == '{' || lexer->lookahead == '(' ||
                    lexer->lookahead == '\'' || iswalnum(lexer->lookahead)) {
                    lexer->result_symbol = EXPANSION_WORD;
                    return advanced_once;
                }
                advanced_once = true;
            }

            if (lexer->lookahead == '}') {
                lexer->mark_end(lexer);
                lexer->result_symbol = EXPANSION_WORD;
                return advanced_once || advance_once_space;
            }

            if (lexer->lookahead == '(' &&
                !(advanced_once || advance_once_space)) {
                lexer->mark_end(lexer);
                advance(lexer);
                while (lexer->lookahead != ')' && !lexer->eof(lexer)) {
                    // if we find a $( or ${ assume this is valid and is a
                    // garbage concatenation of some weird word + an expansion
                    // I wonder where this can fail
                    if (lexer->lookahead == '$') {
                        lexer->mark_end(lexer);
                        advance(lexer);
                        if (lexer->lookahead == '{' ||
                            lexer->lookahead == '(' ||
                            lexer->lookahead == '\'' ||
                            iswalnum(lexer->lookahead)) {
                            lexer->result_symbol = EXPANSION_WORD;
                            return advanced_once;
                        }
                        advanced_once = true;
                    } else {
                        advanced_once =
                            advanced_once || !iswspace(lexer->lookahead);
                        advance_once_space =
                            advance_once_space || iswspace(lexer->lookahead);
                        advance(lexer);
                    }
                }
                lexer->mark_end(lexer);
                if (lexer->lookahead == ')') {
                    advanced_once = true;
                    advance(lexer);
                    lexer->mark_end(lexer);
                    if (lexer->lookahead == '}') {
                        return false;
                    }
                } else {
                    return false;
                }
            }

            if (lexer->lookahead == '\'') {
                return false;
            }

            if (lexer->eof(lexer)) {
                return false;
            }
            advanced_once = advanced_once || !iswspace(lexer->lookahead);
            advance_once_space =
                advance_once_space || iswspace(lexer->lookahead);
            advance(lexer);
        }
    }

brace_start:
    if (valid_symbols[BRACE_START] && !in_error_recovery(valid_symbols)) {
        while (iswspace(lexer->lookahead)) {
            skip(lexer);
        }

        if (lexer->lookahead != '{') {
            return false;
        }

        advance(lexer);
        lexer->mark_end(lexer);

        while (isdigit(lexer->lookahead)) {
            advance(lexer);
        }

        if (lexer->lookahead != '.') {
            return false;
        }
        advance(lexer);

        if (lexer->lookahead != '.') {
            return false;
        }
        advance(lexer);

        while (isdigit(lexer->lookahead)) {
            advance(lexer);
        }

        if (lexer->lookahead != '}') {
            return false;
        }

        lexer->result_symbol = BRACE_START;
        return true;
    }

    return false;
}

void *tree_sitter_bash_external_scanner_create() {
    Scanner *scanner = calloc(1, sizeof(Scanner));
    scanner->heredoc_delimiter = string_new();
    scanner->current_leading_word = string_new();
    return scanner;
}

bool tree_sitter_bash_external_scanner_scan(void *payload, TSLexer *lexer,
                                            const bool *valid_symbols) {
    Scanner *scanner = (Scanner *)payload;
    return scan(scanner, lexer, valid_symbols);
}

unsigned tree_sitter_bash_external_scanner_serialize(void *payload,
                                                     char *state) {
    Scanner *scanner = (Scanner *)payload;
    return serialize(scanner, state);
}

void tree_sitter_bash_external_scanner_deserialize(void *payload,
                                                   const char *state,
                                                   unsigned length) {
    Scanner *scanner = (Scanner *)payload;
    deserialize(scanner, state, length);
}

void tree_sitter_bash_external_scanner_destroy(void *payload) {
    Scanner *scanner = (Scanner *)payload;
    STRING_FREE(scanner->heredoc_delimiter);
    STRING_FREE(scanner->current_leading_word);
    free(scanner);
}
