#pragma once

#include <string>

// Single-line text editing state: the text, the insertion point and an optional
// selection. No window or clipboard code in here, so the editing rules (what
// Backspace does with a selection, how Shift+Arrow extends it, ...) are testable;
// the fence window feeds it key presses and handles the clipboard.
class TextEdit {
public:
    TextEdit() = default;
    explicit TextEdit(const std::wstring& text) { Set(text); }

    const std::wstring& Text() const { return m_text; }
    bool Empty() const { return m_text.empty(); }
    size_t Caret() const { return m_caret; }

    // Replaces the whole text; the caret goes to the end and nothing is selected.
    void Set(const std::wstring& text);
    void Clear() { Set(std::wstring()); }

    // Selection as [SelBegin, SelEnd); empty when nothing is selected.
    bool HasSelection() const { return m_anchor != m_caret; }
    size_t SelBegin() const { return m_anchor < m_caret ? m_anchor : m_caret; }
    size_t SelEnd() const { return m_anchor < m_caret ? m_caret : m_anchor; }
    std::wstring SelectedText() const { return m_text.substr(SelBegin(), SelEnd() - SelBegin()); }
    void SelectAll();

    // Typing: replaces the selection, if any. Control characters are ignored.
    void Insert(const std::wstring& s);
    void Insert(wchar_t c) { Insert(std::wstring(1, c)); }

    // Backspace / Delete: remove the selection, else the character before / after the
    // caret (`word`: up to the previous / next word boundary, as with Ctrl held).
    void Backspace(bool word = false);
    void Delete(bool word = false);

    // Caret movement; `extend` (Shift held) grows the selection instead of dropping it.
    // Without `extend`, Left / Right on a selection collapse it to its edge.
    void Left(bool extend, bool word = false);
    void Right(bool extend, bool word = false);
    void Home(bool extend);
    void End(bool extend);

private:
    size_t WordLeft(size_t from) const;
    size_t WordRight(size_t from) const;
    void EraseSelection();
    void MoveTo(size_t pos, bool extend);

    std::wstring m_text;
    size_t m_caret = 0;
    size_t m_anchor = 0;   // the other end of the selection; equal to m_caret when none
};
