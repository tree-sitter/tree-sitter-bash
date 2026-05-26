from unittest import TestCase

import tree_sitter, tree_sitter_bash


class TestLanguage(TestCase):
    def test_can_load_grammar(self):
        try:
            Parser(Language(tree_sitter_bash.language()))
        except Exception:
            self.fail("Error loading Bash grammar")
