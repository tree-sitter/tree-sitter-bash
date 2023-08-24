/**
 * @file Bash grammar for tree-sitter
 * @author Max Brunsfeld
 * @license MIT
 */

/* eslint-disable arrow-parens */
/* eslint-disable camelcase */
/* eslint-disable-next-line spaced-comment */
/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

const SPECIAL_CHARACTERS = [
  '\'', '"',
  '<', '>',
  '{', '}',
  '\\[', '\\]',
  '(', ')',
  '`', '$',
  '|', '&', ';',
  '\\',
  '\\s',
];

module.exports = grammar({
  name: 'bash',

  conflicts: $ => [
    [$._expression, $.command_name],
    [$.command, $.variable_assignments],
    [$.compound_statement],
    [$.redirected_statement, $.command],
    [$.redirected_statement, $.command_substitution],
  ],

  inline: $ => [
    $._statement,
    $._terminator,
    $._literal,
    $._statements2,
    $._primary_expression,
    $._simple_variable_name,
    $._multiline_variable_name,
    $._special_variable_name,
    $._c_word,
    $._statement_not_subshell,
    $._expansion_syms,
  ],

  externals: $ => [
    $.heredoc_start,
    $.simple_heredoc_body,
    $._heredoc_body_beginning,
    $._heredoc_body_middle,
    $.heredoc_end,
    $.file_descriptor,
    $._empty_value,
    $._concat,
    $.variable_name, // Variable name followed by an operator like '=' or '+='
    $.regex,
    $._regex_no_slash,
    $._regex_no_space,
    $.extglob_pattern,
    $._bare_dollar,
    $._brace_start,
    $._immediate_double_hash,
    $._external_expansion_sym_hash,
    $._external_expansion_sym_bang,
    $._external_expansion_sym_equal,
    '}',
    ']',
    '<<',
    '<<-',
    '\n',
  ],

  extras: $ => [
    $.comment,
    /\s/,
    /\\\r?\n/,
    /\\( |\t|\v|\f)/,
  ],

  supertypes: $ => [
    $._statement,
    $._expression,
    $._primary_expression,
  ],

  word: $ => $.word,

  rules: {
    program: $ => optional($._statements),

    _statements: $ => prec(1, seq(
      repeat(seq(
        $._statement,
        $._terminator,
      )),
      $._statement,
      optional($._terminator),
    )),

    _statements2: $ => repeat1(seq(
      $._statement,
      $._terminator,
    )),

    _terminated_statement: $ => seq(
      $._statement_not_subshell,
      $._terminator,
    ),

    // Statements

    _statement: $ => choice(
      $._statement_not_subshell,
      $.subshell,
    ),

    _statement_not_subshell: $ => choice(
      $.redirected_statement,
      $.variable_assignment,
      $.variable_assignments,
      $.command,
      $.declaration_command,
      $.unset_command,
      $.test_command,
      $.negated_command,
      $.for_statement,
      $.c_style_for_statement,
      $.while_statement,
      $.if_statement,
      $.case_statement,
      $.pipeline,
      $.list,
      $.compound_statement,
      $.function_definition,
    ),

    redirected_statement: $ => prec.dynamic(-1, prec(-1, choice(
      seq(
        field('body', $._statement),
        field('redirect', repeat1(choice(
          $.file_redirect,
          $.heredoc_redirect,
          $.herestring_redirect,
        ))),
      ),
      repeat1($.file_redirect),
    ))),

    for_statement: $ => seq(
      choice('for', 'select'),
      field('variable', $._simple_variable_name),
      optional(seq(
        'in',
        field('value', repeat1($._literal)),
      )),
      $._terminator,
      field('body', $.do_group),
    ),

    c_style_for_statement: $ => seq(
      'for',
      '((',
      choice($._for_body),
      '))',
      optional(';'),
      field('body', choice(
        $.do_group,
        $.compound_statement,
      )),
    ),
    _for_body: $ => seq(
      field('initializer', commaSep($._c_expression)),
      $._c_terminator,
      field('condition', commaSep($._c_expression)),
      $._c_terminator,
      field('update', commaSep($._c_expression)),
    ),

    _c_expression: $ => choice(
      $._c_expression_not_assignment,
      alias($._c_variable_assignment, $.variable_assignment),
    ),
    _c_expression_not_assignment: $ => choice(
      $._c_word,
      $.simple_expansion,
      $.expansion,
      $.number,
      $.string,
      alias($._c_unary_expression, $.unary_expression),
      alias($._c_binary_expression, $.binary_expression),
      alias($._c_postfix_expression, $.postfix_expression),
      alias($._c_parenthesized_expression, $.parenthesized_expression),
      $.command_substitution,
    ),

    _c_variable_assignment: $ => seq(
      alias($._c_word, $.variable_name),
      '=',
      $._c_expression,
    ),
    _c_unary_expression: $ => prec.left(seq(
      field('operator', choice('++', '--')),
      $._c_expression_not_assignment,
    )),
    _c_binary_expression: $ => prec.right(seq(
      $._c_expression_not_assignment,
      field('operator', choice(
        '+=', '-=', '*=', '/=', '%=', '**=',
        '<<=', '>>=', '&=', '^=', '|=',
        '==', '!=', '<=', '>=', '&&', '||',
        '<<', '>>',
        '+', '-', '*', '/', '%', '**',
        '<', '>',
      )),
      $._c_expression_not_assignment,
    )),
    _c_postfix_expression: $ => seq(
      $._c_expression_not_assignment,
      field('operator', choice('++', '--')),
    ),
    _c_parenthesized_expression: $ => seq(
      '(',
      commaSep1($._c_expression),
      ')',
    ),
    _c_word: $ => alias(/[a-zA-Z_][a-zA-Z0-9_]*/, $.word),

    while_statement: $ => seq(
      choice('while', 'until'),
      field('condition', repeat1($._terminated_statement)),
      field('body', $.do_group),
    ),

    do_group: $ => seq(
      'do',
      optional($._statements2),
      'done',
    ),

    if_statement: $ => seq(
      'if',
      field('condition', $._terminated_statement),
      'then',
      optional($._statements2),
      repeat($.elif_clause),
      optional($.else_clause),
      'fi',
    ),

    elif_clause: $ => seq(
      'elif',
      $._terminated_statement,
      'then',
      optional($._statements2),
    ),

    else_clause: $ => seq(
      'else',
      optional($._statements2),
    ),

    case_statement: $ => seq(
      'case',
      field('value', $._literal),
      optional($._terminator),
      'in',
      optional($._terminator),
      optional(seq(
        repeat($.case_item),
        alias($.last_case_item, $.case_item),
      )),
      'esac',
    ),

    case_item: $ => seq(
      choice(
        seq(
          optional('('),
          field('value', choice($._literal, $.extglob_pattern)),
          repeat(seq('|', field('value', choice($._literal, $.extglob_pattern)))),
          ')',
        ),
      ),
      optional($._statements),
      prec(1, choice(
        field('termination', ';;'),
        field('fallthrough', choice(';&', ';;&')),
      )),
    ),

    last_case_item: $ => seq(
      optional('('),
      field('value', choice($._literal, $.extglob_pattern)),
      repeat(seq('|', field('value', choice($._literal, $.extglob_pattern)))),
      ')',
      optional($._statements),
      optional(prec(1, ';;')),
    ),

    function_definition: $ => prec.right(seq(
      choice(
        seq(
          'function',
          field('name', $.word),
          optional(seq('(', ')')),
        ),
        seq(
          field('name', $.word),
          '(', ')',
        ),
      ),
      field(
        'body',
        choice(
          $.compound_statement,
          $.subshell,
          $.test_command),
      ),
      optional($.file_redirect),
    )),

    compound_statement: $ => seq(
      '{',
      optional(choice($._statements2, seq($._statement, $._terminator))),
      token(prec(-1, '}')),
    ),

    subshell: $ => seq(
      '(',
      $._statements,
      ')',
    ),

    pipeline: $ => prec.left(1, seq(
      $._statement,
      choice('|', '|&'),
      $._statement,
    )),

    list: $ => prec.left(-1, seq(
      $._statement,
      choice('&&', '||'),
      $._statement,
    )),

    // Commands

    negated_command: $ => seq(
      '!',
      choice(
        prec(2, $.command),
        prec(1, $.variable_assignment),
        $.test_command,
        $.subshell,
      ),
    ),

    test_command: $ => seq(
      choice(
        seq('[', optional(choice($._expression, $.redirected_statement)), ']'),
        seq('[[', $._expression, ']]'),
        seq('(', '(', optional($._expression), '))'),
      ),
    ),

    declaration_command: $ => prec.left(seq(
      choice('declare', 'typeset', 'export', 'readonly', 'local'),
      repeat(choice(
        $._literal,
        $._simple_variable_name,
        $.variable_assignment,
      )),
    )),

    unset_command: $ => prec.left(seq(
      choice('unset', 'unsetenv'),
      repeat(choice(
        $._literal,
        $._simple_variable_name,
      )),
    )),

    command: $ => prec.left(seq(
      repeat(choice(
        $.variable_assignment,
        $.file_redirect,
      )),
      field('name', $.command_name),
      repeat(field('argument', choice(
        $._literal,
        alias($._bare_dollar, '$'),
        seq(
          choice('=~', '=='),
          choice($._literal, $.regex),
        ),
      ))),
    )),

    command_name: $ => $._literal,

    variable_assignment: $ => seq(
      field('name', choice(
        $.variable_name,
        $.subscript,
      )),
      choice(
        '=',
        '+=',
      ),
      field('value', choice(
        $._literal,
        $.array,
        $._empty_value,
        alias($._comment_word, $.word),
      )),
    ),

    variable_assignments: $ => seq($.variable_assignment, repeat1($.variable_assignment)),

    subscript: $ => seq(
      field('name', $.variable_name),
      '[',
      field('index', choice($._literal, $.binary_expression, $.unary_expression, $.parenthesized_expression)),
      optional($._concat),
      ']',
      optional($._concat),
    ),

    file_redirect: $ => prec.left(seq(
      field('descriptor', optional($.file_descriptor)),
      choice('<', '>', '>>', '&>', '&>>', '<&', '>&', '>|'),
      field('destination', repeat1($._literal)),
    )),

    heredoc_redirect: $ => seq(
      field('descriptor', optional($.file_descriptor)),
      choice('<<', '<<-'),
      $.heredoc_start,
      optional(seq(
        choice(alias($._heredoc_pipeline, $.pipeline), $.file_redirect),
      )),
      '\n',
      choice($._heredoc_body, $._simple_heredoc_body),
    ),

    _heredoc_pipeline: $ => seq(
      choice('|', '|&'),
      $._statement,
    ),

    _heredoc_body: $ => seq(
      $.heredoc_body,
      $.heredoc_end,
    ),

    heredoc_body: $ => seq(
      $._heredoc_body_beginning,
      repeat(choice(
        $.expansion,
        $.simple_expansion,
        $.command_substitution,
        $._heredoc_body_middle,
      )),
    ),

    _simple_heredoc_body: $ => seq(
      $.simple_heredoc_body,
      $.heredoc_end,
    ),

    herestring_redirect: $ => seq(
      field('descriptor', optional($.file_descriptor)),
      '<<<',
      $._literal,
    ),

    // Expressions

    _expression: $ => choice(
      $._literal,
      $.unary_expression,
      $.ternary_expression,
      $.binary_expression,
      $.postfix_expression,
      $.parenthesized_expression,
    ),

    binary_expression: $ => prec.left(2, choice(
      seq(
        field('left', $._expression),
        field('operator', choice(
          '=', '==', '=~', '!=',
          '+', '-', '+=', '-=',
          '*', '/', '*=', '/=',
          '%', '%=', '**',
          '<', '>', '<=', '>=',
          '||', '&&',
          '<<', '>>', '<<=', '>>=',
          '&', '|', '^',
          '&=', '|=', '^=',
          $.test_operator,
        )),
        field('right', $._expression),
      ),
      seq(
        field('left', $._expression),
        field('operator', choice('==', '=~', '!=')),
        field('right', alias($._regex_no_space, $.regex)),
      ),
    )),

    ternary_expression: $ => prec.left(
      seq(
        field('condition', $._expression),
        '?',
        field('consequence', $._expression),
        ':',
        field('alternative', $._expression),
      ),
    ),

    unary_expression: $ => choice(
      prec(1, seq(
        field('operator', tokenLiterals(1, '-', '+', '~', '++', '--')),
        $._expression,
      )),
      prec.right(1, seq(
        field('operator', choice('!', $.test_operator)),
        $._expression,
      )),
    ),

    postfix_expression: $ => seq(
      $._expression,
      field('operator', choice('++', '--')),
    ),

    parenthesized_expression: $ => seq(
      '(',
      $._expression,
      ')',
    ),

    // Literals

    _literal: $ => choice(
      $.concatenation,
      $._primary_expression,
      alias(prec(-2, repeat1($._special_character)), $.word),
    ),

    _primary_expression: $ => choice(
      $.word,
      alias($.test_operator, $.word),
      $.string,
      $.raw_string,
      $.translated_string,
      $.ansi_c_string,
      $.number,
      $.expansion,
      $.simple_expansion,
      $.command_substitution,
      $.process_substitution,
      $.arithmetic_expansion,
      $.brace_expression,
    ),

    arithmetic_expansion: $ => seq(choice('$((', '(('), commaSep1($._arithmetic_expression), '))'),

    brace_expression: $ => seq(
      alias($._brace_start, '{'),
      alias(token.immediate(/\d+/), $.number),
      token.immediate('..'),
      alias(token.immediate(/\d+/), $.number),
      token.immediate('}'),
    ),

    _arithmetic_expression: $ => choice(
      $._arithmetic_literal,
      alias($._arithmetic_unary_expression, $.unary_expression),
      alias($._arithmetic_ternary_expression, $.ternary_expression),
      alias($._arithmetic_binary_expression, $.binary_expression),
      alias($._arithmetic_postfix_expression, $.postfix_expression),
      alias($._arithmetic_parenthesized_expression, $.parenthesized_expression),
    ),

    _arithmetic_literal: $ => prec(1, choice(
      $.number,
      $.subscript,
      $.simple_expansion,
      $.expansion,
      $._simple_variable_name,
      $.variable_name,
    )),

    _arithmetic_binary_expression: $ => prec.left(2, choice(
      seq(
        field('left', $._arithmetic_expression),
        field('operator', choice(
          '=', '==', '=~', '!=',
          '+', '-', '+=', '-=',
          '*', '/', '*=', '/=',
          '%', '%=', '**',
          '<', '>', '<=', '>=',
          '||', '&&',
          '<<', '>>', '<<=', '>>=',
          '&', '|', '^',
          '&=', '|=', '^=',
        )),
        field('right', $._arithmetic_expression),
      ),
    )),

    _arithmetic_ternary_expression: $ => prec.left(
      seq(
        field('condition', $._arithmetic_expression),
        '?',
        field('consequence', $._arithmetic_expression),
        ':',
        field('alternative', $._arithmetic_expression),
      ),
    ),

    _arithmetic_unary_expression: $ => choice(
      prec(3, seq(
        field('operator', tokenLiterals(1, '-', '+', '~', '++', '--')),
        $._arithmetic_expression,
      )),
      prec.right(3, seq(
        field('operator', '!'),
        $._arithmetic_expression,
      )),
    ),

    _arithmetic_postfix_expression: $ => seq(
      $._arithmetic_expression,
      field('operator', choice('++', '--')),
    ),

    _arithmetic_parenthesized_expression: $ => seq(
      '(',
      $._arithmetic_expression,
      ')',
    ),


    concatenation: $ => prec(-1, seq(
      choice(
        $._primary_expression,
        alias($._special_character, $.word),
      ),
      repeat1(seq(
        choice($._concat, alias(/`\s*`/, '``')),
        choice(
          $._primary_expression,
          alias($._special_character, $.word),
          alias($._comment_word, $.word),
        ),
      )),
      optional(seq($._concat, '$')),
    )),

    _special_character: _ => token(prec(-1, choice('{', '}', '[', ']'))),

    string: $ => seq(
      '"',
      repeat(seq(
        choice(
          seq(optional('$'), $._string_content),
          $.expansion,
          $.simple_expansion,
          $.command_substitution,
          $.arithmetic_expansion,
        ),
        optional($._concat),
      )),
      optional('$'),
      '"',
    ),

    _string_content: _ => token(prec(-1, /([^"`$\\]|\\(.|\r?\n))+/)),

    translated_string: $ => seq('$', $.string),

    array: $ => seq(
      '(',
      repeat($._literal),
      ')',
    ),

    raw_string: _ => /'[^']*'/,

    ansi_c_string: _ => /\$'([^']|\\')*'/,

    number: $ => choice(
      /-?(0x)?[0-9]+(#[0-9A-Za-z@_]+)?/,
      // the base can be an expansion
      seq(/-?(0x)?[0-9]+#/, $.expansion),
    ),

    simple_expansion: $ => seq(
      '$',
      choice(
        $._simple_variable_name,
        $._multiline_variable_name,
        $._special_variable_name,
        alias('!', $.special_variable_name),
        alias('#', $.special_variable_name),
      ),
    ),

    string_expansion: $ => seq('$', $.string),

    expansion: $ => seq(
      '${',
      optional($._expansion_body),
      '}',
    ),
    _expansion_body: $ => choice(
      // ${!##} ${!#}
      repeat1(field(
        'operator',
        choice(
          alias($._external_expansion_sym_hash, '#'),
          alias($._external_expansion_sym_bang, '!'),
          alias($._external_expansion_sym_equal, '='),
        ),
      )),
      seq(
        optional(field('operator', token.immediate('!'))),
        choice($.variable_name, $._simple_variable_name, $._special_variable_name, $.subscript),
        choice(
          $._expansion_expression,
          $._expansion_regex,
          $._expansion_regex_replacement,
          $._expansion_regex_removal,
          $._expansion_max_length,
          $._expansion_operator,
        ),
      ),
      seq(
        field('operator', token.immediate('!')),
        choice($._simple_variable_name, $.variable_name),
        optional(field('operator', choice(
          token.immediate('@'),
          token.immediate('*'),
        ))),
      ),
      seq(
        optional(field('operator', immediateLiterals('#', '!', '='))),
        choice(
          $.subscript,
          $._simple_variable_name,
          $._special_variable_name,
          $.command_substitution,
        ),
        repeat(field(
          'operator',
          choice(
            alias($._external_expansion_sym_hash, '#'),
            alias($._external_expansion_sym_bang, '!'),
            alias($._external_expansion_sym_equal, '='),
          ),
        )),
      ),
    ),

    _expansion_syms: _ => repeat1(field('operator',
      immediateLiterals('#', '!', '='),
    )),

    _expansion_expression: $ => prec(1, seq(
      field('operator', immediateLiterals('=', ':=', '-', ':-', '+', ':+', '?', ':?')),
      optional(seq(
        choice(
          alias($._concatenation_in_expansion, $.concatenation),
          // $._simple_variable_name,
          $.command_substitution,
          $.word,
          $.expansion,
          $.simple_expansion,
          $.array,
          $.string,
          $.raw_string,
          $.ansi_c_string,
          alias(/[\s]+[\w]*/, $.word),
        ),
      )),
    )),

    _expansion_regex: $ => seq(
      field('operator', choice('#', alias($._immediate_double_hash, '##'), '%', '%%')),
      choice($.regex, alias(')', $.regex), $.string, $.raw_string, alias(/\s+/, $.regex)),
    ),

    _expansion_regex_replacement: $ => seq(
      field('operator', choice('/', '//', '/#', '/%')),
      alias($._regex_no_slash, $.regex),
      // This can be elided
      optional(seq(
        field('operator', '/'),
        optional(seq(
          $._literal,
          field('operator', optional('/')),
        )),
      )),
    ),

    _expansion_regex_removal: $ => seq(
      field('operator', choice(',', ',,', '^', '^^')),
      optional($.regex),
    ),

    _expansion_max_length: $ => seq(
      field('operator', ':'),
      choice(
        $._simple_variable_name,
        $.number,
        $.arithmetic_expansion,
        $.expansion,
        $.parenthesized_expression,
        '\n',
      ),
      optional(seq(
        field('operator', ':'),
        optional(choice(
          $._simple_variable_name,
          $.number,
          $.arithmetic_expansion,
          '\n',
        )),
      )),
    ),

    _expansion_operator: $ => seq(
      field('operator', token.immediate('@')),
      field('operator', immediateLiterals('U', 'u', 'L', 'Q', 'E', 'P', 'A', 'K', 'a', 'k')),
    ),

    _concatenation_in_expansion: $ => prec(-2, seq(
      choice(
        $.word,
        $.variable_name,
        $.simple_expansion,
        $.expansion,
        $.string,
        $.raw_string,
      ),
      repeat1(seq(
        choice($._concat, alias(/`\s*`/, '``')),
        choice(
          $.word,
          $.variable_name,
          $.simple_expansion,
          $.expansion,
          $.string,
          $.raw_string,
          alias($._comment_word, $.word),
        ),
      )),
    )),

    command_substitution: $ => choice(
      seq('$(', $._statements, ')'),
      seq('$(', $.file_redirect, ')'),
      prec(1, seq('`', $._statements, '`')),
      seq('$`', $._statements, '`'),
    ),

    process_substitution: $ => seq(
      choice('<(', '>('),
      $._statements,
      ')',
    ),

    comment: _ => token(prec(-10, /#.*/)),
    _comment_word: _ => token(prec(-9, seq(
      choice(
        noneOf(...SPECIAL_CHARACTERS),
        seq('\\', noneOf('\\s')),
      ),
      repeat(choice(
        noneOf(...SPECIAL_CHARACTERS),
        seq('\\', noneOf('\\s')),
        '\\ ',
      )),
    ))),

    _simple_variable_name: $ => alias(/\w+/, $.variable_name),
    _multiline_variable_name: $ => alias(
      token(prec(-1, /(\w|\\\r?\n)+/)),
      $.variable_name,
    ),

    _special_variable_name: $ => alias(choice('*', '@', '?', '!', '#', '-', '$', '0', '_'), $.special_variable_name),

    word: _ => token(seq(
      choice(
        noneOf('#', ...SPECIAL_CHARACTERS),
        seq('\\', noneOf('\\s')),
      ),
      repeat(choice(
        noneOf(...SPECIAL_CHARACTERS),
        seq('\\', noneOf('\\s')),
        '\\ ',
      )),
    )),

    test_operator: _ => token(prec(1, seq('-', /[a-zA-Z]+/))),

    _c_terminator: _ => choice(';', '\n', '&'),
    _terminator: _ => choice(';', ';;', '\n', '&'),
  },
});

/**
 * Returns a regular expression that matches any character except the ones
 * provided.
 *
 * @param  {...string} characters
 *
 * @return {RegExp}
 *
 */
function noneOf(...characters) {
  const negatedString = characters.map(c => c == '\\' ? '\\\\' : c).join('');
  return new RegExp('[^' + negatedString + ']');
}

/**
 * Creates a rule to optionally match one or more of the rules separated by a comma
 *
 * @param {RuleOrLiteral} rule
 *
 * @return {ChoiceRule}
 *
 */
function commaSep(rule) {
  return optional(commaSep1(rule));
}

/**
 * Creates a rule to match one or more of the rules separated by a comma
 *
 * @param {RuleOrLiteral} rule
 *
 * @return {SeqRule}
 *
 */
function commaSep1(rule) {
  return seq(rule, repeat(seq(',', rule)));
}

/**
 *
 * Turns a list of rules into a choice of immediate rule
 *
 * @param {(RegExp|String)[]} literals
 *
 * @return {ChoiceRule}
 */
function immediateLiterals(...literals) {
  return choice(...literals.map(l => token.immediate(l)));
}

/**
 *
 * Turns a list of rules into a choice of aliased token rules
 *
 * @param {number} precedence
 *
 * @param {(RegExp|String)[]} literals
 *
 * @return {ChoiceRule}
 */
function tokenLiterals(precedence, ...literals) {
  return choice(...literals.map(l => token(prec(precedence, l))));
}
