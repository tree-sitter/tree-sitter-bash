package tree_sitter_bash_test

import (
	"testing"

	tree_sitter "github.com/smacker/go-tree-sitter"
	"github.com/tree-sitter/tree-sitter-bash"
)

func TestCanLoadGrammar(t *testing.T) {
	language := tree_sitter.NewLanguage(tree_sitter_bash.Language())
	if language == nil {
		t.Errorf("Error loading Bash grammar")
	}
}
