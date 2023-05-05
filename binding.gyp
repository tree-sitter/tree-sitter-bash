{
  "targets": [
    {
      "target_name": "tree_sitter_bash_binding",
      "include_dirs": [
        "<!(node -e \"require('nan')\")",
        "src"
      ],
      "sources": [
        "src/parser.c",
        "bindings/node/binding.cc",
        "src/scanner.cc",
      ],
      "cflags_c": [
        "-std=c99",
      ],
      "cflags_cc": ["-std=c++17"],
      "xcode_settings": {
        "OTHER_CPLUSPLUSFLAGS": ["-std=c++17", "-stdlib=libc++"],
      },
      "msvs_settings": {
        "VCCLCompilerTool": {
          "AdditionalOptions": [
            "/std:c++17",
          ],
          "RuntimeLibrary": 0,
        },
      },

    }
  ],
  'variables': {
    'openssl_fips': '',
  },
}
