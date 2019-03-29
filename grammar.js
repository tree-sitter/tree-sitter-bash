const SPECIAL_CHARACTERS = [
  "'", '"',
  '<', '>',
  '{', '}',
  '\\[', '\\]',
  '(', ')',
  '`', '$',
  '|', '&', ';',
  '\\',
  '\\s',
  '#',
];

module.exports = grammar({
  name: 'bash',

  inline: $ => [
    $._statement,
    $._terminator,
    $._literal,
    $._primary_expression,
    $._simple_variable_name,
    $._special_variable_name,
  ],

  externals: $ => [
    $.heredoc_start,
    $._simple_heredoc_body,
    $._heredoc_body_beginning,
    $._heredoc_body_middle,
    $._heredoc_body_end,
    $.file_descriptor,
    $._empty_value,
    $._concat,
    $.variable_name, // Variable name followed by an operator like '=' or '+='
    $.regex,
    '}',
    ']',
    '<<',
    '<<-',
    '\n',
  ],

  extras: $ => [
    $.comment,
    /\\?\s/,
  ],

  word: $ => $.word,

  rules: {
    program: $ => optional($._statements),

    _statements: $ => prec(1, seq(
      repeat(seq(
        field('statement', $._statement),
        optional($.heredoc_body),
        $._terminator
      )),
      field('statement', $._statement),
      optional($.heredoc_body),
      optional($._terminator)
    )),

    _terminated_statement: $ => seq(
      $._statement,
      $._terminator
    ),

    // Statements

    _statement: $ => choice(
      $.redirected_statement,
      $.variable_assignment,
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
      $.subshell,
      $.compound_statement,
      $.function_definition
    ),

    redirected_statement: $ => prec(-1, seq(
      field('body', $._statement),
      repeat1(field('redirect', choice(
        $.file_redirect,
        $.heredoc_redirect,
        $.herestring_redirect
      )))
    )),

    for_statement: $ => seq(
      'for',
      field('variable', $._simple_variable_name),
      optional(seq(
        'in',
        field('value', repeat1($._literal))
      )),
      $._terminator,
      field('body', $.do_group)
    ),

    c_style_for_statement: $ => seq(
      'for',
      '((',
      field('initializer', optional($._expression)),
      $._terminator,
      field('condition', optional($._expression)),
      $._terminator,
      field('increment', optional($._expression)),
      '))',
      optional(';'),
      field('body', choice(
        $.do_group,
        $.compound_statement
      ))
    ),

    while_statement: $ => seq(
      'while',
      field('condition', $._terminated_statement),
      field('body', $.do_group)
    ),

    do_group: $ => seq(
      'do',
      optional($._statements),
      'done'
    ),

    if_statement: $ => seq(
      'if',
      field('condition', $._terminated_statement),
      'then',
      optional($._statements),
      repeat(field('conditional_alternative', $.elif_clause)),
      field('alternative', optional($.else_clause)),
      'fi'
    ),

    elif_clause: $ => seq(
      'elif',
      $._terminated_statement,
      'then',
      optional($._statements)
    ),

    else_clause: $ => seq(
      'else',
      optional($._statements)
    ),

    case_statement: $ => seq(
      'case',
      field('value', $._literal),
      optional($._terminator),
      'in',
      $._terminator,
      field('item', optional(seq(
        repeat($.case_item),
        alias($.last_case_item, $.case_item),
      ))),
      'esac'
    ),

    case_item: $ => seq(
      field('matcher', $._literal),
      repeat(seq(
        '|',
        field('matcher', $._literal)
      )),
      ')',
      optional($._statements),
      prec(1, ';;')
    ),

    last_case_item: $ => seq(
      field('matcher', $._literal),
      repeat(seq(
        '|',
        field('matcher', $._literal)
      )),
      ')',
      optional($._statements),
      optional(prec(1, ';;'))
    ),

    function_definition: $ => seq(
      choice(
        seq('function', field('name', $.word), optional(seq('(', ')'))),
        seq(field('name', $.word), '(', ')')
      ),
      field('body', $.compound_statement)
    ),

    compound_statement: $ => seq(
      '{',
      optional($._statements),
      '}'
    ),

    subshell: $ => seq(
      '(',
      $._statements,
      ')'
    ),

    pipeline: $ => prec.left(1, seq(
      field('left', $._statement),
      field('pipe', choice('|', '|&')),
      field('right', $._statement)
    )),

    list: $ => prec.left(-1, seq(
      field('left', $._statement),
      field('operator', choice('&&', '||')),
      field('right', $._statement)
    )),

    // Commands

    negated_command: $ => seq(
      '!',
      field('argument', choice(
        $.command,
        $.test_command,
        $.subshell
      ))
    ),

    test_command: $ => seq(
      choice(
        seq('[', field('argument', $._expression), ']'),
        seq('[[', field('argument', $._expression), ']]'),
        seq('((', field('argument', $._expression), '))')
      )
    ),

    declaration_command: $ => prec.left(seq(
      choice('declare', 'typeset', 'export', 'readonly', 'local'),
      repeat(choice(
        $._literal,
        $._simple_variable_name,
        $.variable_assignment
      ))
    )),

    unset_command: $ => prec.left(seq(
      choice('unset', 'unsetenv'),
      repeat(choice(
        $._literal,
        $._simple_variable_name
      ))
    )),

    command: $ => prec.left(seq(
      repeat(choice(
        field('environment', $.variable_assignment),
        field('redirect', $.file_redirect)
      )),
      field('name', $.command_name),
      repeat(choice(
        field('argument', $._literal),
        seq(
          field('argument', choice('=~', '==')),
          field('argument', choice($._literal, $.regex))
        )
      ))
    )),

    command_name: $ => $._literal,

    variable_assignment: $ => seq(
      field('left', choice(
        $.variable_name,
        $.subscript
      )),
      field('operator', choice(
        '=',
        '+='
      )),
      field('right', choice(
        $._literal,
        $.array,
        $._empty_value
      ))
    ),

    subscript: $ => seq(
      field('array', $.variable_name),
      '[',
      field('index', $._literal),
      optional($._concat),
      ']',
      optional($._concat)
    ),

    file_redirect: $ => prec.left(seq(
      field('file_descriptor', optional($.file_descriptor)),
      choice('<', '>', '>>', '&>', '&>>', '<&', '>&'),
      field('argument', $._literal)
    )),

    heredoc_redirect: $ => seq(
      choice('<<', '<<-'),
      $.heredoc_start
    ),

    heredoc_body: $ => choice(
      $._simple_heredoc_body,
      seq(
        $._heredoc_body_beginning,
        repeat(field('interpolation', choice(
          $.expansion,
          $.simple_expansion,
          $.command_substitution,
          $._heredoc_body_middle
        ))),
        $._heredoc_body_end
      )
    ),

    herestring_redirect: $ => seq(
      '<<<',
      field('argument', $._literal)
    ),

    // Expressions

    _expression: $ => choice(
      $._literal,
      $.unary_expression,
      $.binary_expression,
      $.postfix_expression,
      $.parenthesized_expression
    ),

    binary_expression: $ => prec.left(choice(
      seq(
        field('left', $._expression),
        field('operator', choice(
          '=', '==', '=~', '!=',
          '+', '-', '+=', '-=',
          '<', '>', '<=', '>=',
          '||', '&&',
          $.test_operator
        )),
        field('right', $._expression)
      ),
      seq(
        field('left', $._expression),
        field('operator', choice('==', '=~')),
        field('right', $.regex)
      )
    )),

    unary_expression: $ => prec.right(seq(
      field('operator', choice('!', $.test_operator)),
      field('argument', $._expression)
    )),

    postfix_expression: $ => seq(
      field('argument', $._expression),
      field('operator', choice('++', '--')),
    ),

    parenthesized_expression: $ => seq(
      '(',
      field('value', $._expression),
      ')'
    ),

    // Literals

    _literal: $ => choice(
      $.concatenation,
      $._primary_expression,
      alias(prec(-2, repeat1($._special_character)), $.word)
    ),

    _primary_expression: $ => choice(
      $.word,
      $.string,
      $.raw_string,
      $.expansion,
      $.simple_expansion,
      $.string_expansion,
      $.command_substitution,
      $.process_substitution
    ),

    concatenation: $ => prec(-1, seq(
      choice(
        $._primary_expression,
        $._special_character,
      ),
      repeat1(prec(-1, seq(
        $._concat,
        choice(
          $._primary_expression,
          $._special_character,
        )
      ))),
      optional(seq($._concat, '$'))
    )),

    _special_character: $ => token(prec(-1, choice('{', '}', '[', ']'))),

    string: $ => seq(
      '"',
      repeat(seq(
        choice(
          seq(optional('$'), $._string_content),
          field('interpolation', $.expansion),
          field('interpolation', $.simple_expansion),
          field('interpolation', $.command_substitution)
        ),
        optional($._concat)
      )),
      optional('$'),
      '"'
    ),

    _string_content: $ => token(prec(-1, /([^"`$\\]|\\(.|\n))+/)),

    array: $ => seq(
      '(',
      repeat($._literal),
      ')'
    ),

    raw_string: $ => /'[^']*'/,

    simple_expansion: $ => seq(
      '$',
      choice(
        $._simple_variable_name,
        $._special_variable_name,
        alias('#', $.special_variable_name)
      )
    ),

    string_expansion: $ => seq('$', choice($.string, $.raw_string)),

    expansion: $ => seq(
      '${',
      optional(choice('#', '!')),
      choice(
        seq(
          field('name', $.variable_name),
          '=',
          optional($._literal)
        ),
        seq(
          choice(
            $.subscript,
            field('name', $._simple_variable_name),
            field('name', $._special_variable_name)
          ),
          optional(seq(
            token(prec(1, '/')),
            optional($.regex)
          )),
          repeat(choice(
            $._literal,
            ':', ':?', '=', ':-', '%', '-', '#'
          ))
        ),
      ),
      '}'
    ),

    command_substitution: $ => choice(
      seq('$(', $._statements, ')'),
      seq('$(', $.file_redirect, ')'),
      prec(1, seq('`', $._statements, '`'))
    ),

    process_substitution: $ => seq(
      choice('<(', '>('),
      $._statements,
      ')'
    ),

    comment: $ => token(prec(-10, /#.*/)),

    _simple_variable_name: $ => alias(/\w+/, $.variable_name),

    _special_variable_name: $ => alias(choice('*', '@', '?', '-', '$', '0', '_'), $.special_variable_name),

    word: $ => token(repeat1(choice(
      noneOf(...SPECIAL_CHARACTERS),
      seq('\\', noneOf('\\s'))
    ))),

    test_operator: $ => token(prec(1, seq('-', /[a-zA-Z]+/))),

    _terminator: $ => choice(';', ';;', '\n', '&')
  }
});

function noneOf(...characters) {
  const negatedString = characters.map(c => c == '\\' ? '\\\\' : c).join('')
  return new RegExp('[^' + negatedString + ']')
}
