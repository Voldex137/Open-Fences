// The editing rules of the single-line fields (title rename, icon rename, search).
#include "fence/text_edit.h"
#include "test_util.h"

using test::Check;

static void TestTyping() {
    TextEdit e;
    e.Insert(L"abc");
    Check(e.Text() == L"abc" && e.Caret() == 3, "typing appends at the caret");
    e.Left(false);
    e.Insert(L'X');
    Check(e.Text() == L"abXc" && e.Caret() == 3, "typing inserts in the middle");
    e.Insert(L"\t\r\n");
    Check(e.Text() == L"abXc", "control characters are ignored");
}

static void TestSelectAllReplace() {
    TextEdit e(L"hello world");
    e.SelectAll();
    Check(e.HasSelection() && e.SelectedText() == L"hello world", "Ctrl+A selects everything");
    e.Insert(L"hi");
    Check(e.Text() == L"hi" && !e.HasSelection(), "typing over a selection replaces it");
}

static void TestBackspaceDelete() {
    TextEdit e(L"abcd");
    e.Backspace();
    Check(e.Text() == L"abc", "backspace removes the previous character");
    e.Home(false);
    e.Backspace();
    Check(e.Text() == L"abc", "backspace at the start does nothing");
    e.Delete();
    Check(e.Text() == L"bc", "delete removes the next character");
    e.SelectAll();
    e.Backspace();
    Check(e.Empty() && e.Caret() == 0, "backspace on a selection removes it");
}

static void TestShiftSelection() {
    TextEdit e(L"abcdef");
    e.Home(false);
    e.Right(true);
    e.Right(true);
    Check(e.SelectedText() == L"ab", "Shift+Right extends the selection");
    e.Left(true);
    Check(e.SelectedText() == L"a", "Shift+Left shrinks it");
    e.End(true);
    Check(e.SelectedText() == L"abcdef", "Shift+End selects to the end");
    e.Left(false);
    Check(!e.HasSelection() && e.Caret() == 0, "Left on a selection collapses to its start");
}

static void TestWords() {
    TextEdit e(L"one two  three");
    e.Backspace(true);
    Check(e.Text() == L"one two  ", "Ctrl+Backspace removes the previous word");
    e.Home(false);
    e.Delete(true);
    Check(e.Text() == L"two  ", "Ctrl+Delete removes the next word and its space");
    e.End(false);
    e.Left(false, true);
    Check(e.Caret() == 0 || e.Text()[e.Caret() - 1] == L' ', "Ctrl+Left lands at a word start");
}

static void TestPaste() {
    TextEdit e(L"ab");
    e.Left(false);
    e.Insert(L"123");
    Check(e.Text() == L"a123b" && e.Caret() == 4, "pasted text goes at the caret");
    e.SelectAll();
    e.Insert(L"line1\r\nline2");
    Check(e.Text() == L"line1line2", "pasted line breaks are dropped");
}

int main() {
    TestTyping();
    TestSelectAllReplace();
    TestBackspaceDelete();
    TestShiftSelection();
    TestWords();
    TestPaste();
    return test::Finish();
}
