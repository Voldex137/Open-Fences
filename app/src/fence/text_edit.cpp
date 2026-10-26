#include "fence/text_edit.h"

#include <cwctype>

void TextEdit::Set(const std::wstring& text) {
    m_text = text;
    m_caret = m_anchor = m_text.size();
}

void TextEdit::SelectAll() {
    m_anchor = 0;
    m_caret = m_text.size();
}

void TextEdit::EraseSelection() {
    size_t b = SelBegin(), e = SelEnd();
    m_text.erase(b, e - b);
    m_caret = m_anchor = b;
}

void TextEdit::Insert(const std::wstring& s) {
    std::wstring clean;
    for (wchar_t c : s)
        if (c >= 32 && c != 0x7F) clean.push_back(c);   // no control characters (line breaks, tabs)
    if (clean.empty()) return;
    if (HasSelection()) EraseSelection();
    m_text.insert(m_caret, clean);
    m_caret += clean.size();
    m_anchor = m_caret;
}

size_t TextEdit::WordLeft(size_t from) const {
    size_t i = from;
    while (i > 0 && std::iswspace(m_text[i - 1])) i--;
    while (i > 0 && !std::iswspace(m_text[i - 1])) i--;
    return i;
}

size_t TextEdit::WordRight(size_t from) const {
    size_t i = from;
    while (i < m_text.size() && !std::iswspace(m_text[i])) i++;
    while (i < m_text.size() && std::iswspace(m_text[i])) i++;
    return i;
}

void TextEdit::Backspace(bool word) {
    if (HasSelection()) { EraseSelection(); return; }
    if (m_caret == 0) return;
    size_t to = word ? WordLeft(m_caret) : m_caret - 1;
    m_text.erase(to, m_caret - to);
    m_caret = m_anchor = to;
}

void TextEdit::Delete(bool word) {
    if (HasSelection()) { EraseSelection(); return; }
    if (m_caret >= m_text.size()) return;
    size_t to = word ? WordRight(m_caret) : m_caret + 1;
    m_text.erase(m_caret, to - m_caret);
    m_anchor = m_caret;
}

void TextEdit::MoveTo(size_t pos, bool extend) {
    m_caret = pos;
    if (!extend) m_anchor = pos;
}

void TextEdit::Left(bool extend, bool word) {
    if (!extend && HasSelection()) { MoveTo(SelBegin(), false); return; }
    size_t to = word ? WordLeft(m_caret) : (m_caret > 0 ? m_caret - 1 : 0);
    MoveTo(to, extend);
}

void TextEdit::Right(bool extend, bool word) {
    if (!extend && HasSelection()) { MoveTo(SelEnd(), false); return; }
    size_t to = word ? WordRight(m_caret) : (m_caret < m_text.size() ? m_caret + 1 : m_caret);
    MoveTo(to, extend);
}

void TextEdit::Home(bool extend) { MoveTo(0, extend); }
void TextEdit::End(bool extend) { MoveTo(m_text.size(), extend); }
