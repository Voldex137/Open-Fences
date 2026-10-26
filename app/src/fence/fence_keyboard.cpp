#include "fence/fence_window.h"
#include <cstring>
#include <string>

namespace {

void SetClipboardText(HWND owner, const std::wstring& text) {
    if (text.empty() || !OpenClipboard(owner)) return;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* p = GlobalLock(mem)) {
            memcpy(p, text.c_str(), bytes);
            GlobalUnlock(mem);
            if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
        } else {
            GlobalFree(mem);
        }
    }
    CloseClipboard();
}

std::wstring GetClipboardText(HWND owner) {
    std::wstring text;
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(owner)) return text;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* p = (const wchar_t*)GlobalLock(h)) {
            text = p;
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return text;
}

}  // namespace

// The keys every one-line field understands. Ctrl+A/C/X/V reach the window as key
// presses (WM_CHAR delivers control codes for them, which typing ignores).
bool FenceWindow::EditKey(TextEdit& e, WPARAM vk) {
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    switch (vk) {
    case VK_LEFT:   e.Left(shift, ctrl);  return true;
    case VK_RIGHT:  e.Right(shift, ctrl); return true;
    case VK_HOME:   e.Home(shift);        return true;
    case VK_END:    e.End(shift);         return true;
    case VK_BACK:   e.Backspace(ctrl);    return true;
    case VK_DELETE:
        if (shift && e.HasSelection()) { SetClipboardText(m_hwnd, e.SelectedText()); e.Backspace(); }   // Shift+Del = cut
        else e.Delete(ctrl);
        return true;
    case VK_INSERT:
        if (ctrl && e.HasSelection()) SetClipboardText(m_hwnd, e.SelectedText());   // Ctrl+Ins = copy
        else if (shift) e.Insert(GetClipboardText(m_hwnd));                          // Shift+Ins = paste
        return true;
    case 'A': if (!ctrl) return false; e.SelectAll(); return true;
    case 'C': if (!ctrl) return false; if (e.HasSelection()) SetClipboardText(m_hwnd, e.SelectedText()); return true;
    case 'X':
        if (!ctrl) return false;
        if (e.HasSelection()) { SetClipboardText(m_hwnd, e.SelectedText()); e.Backspace(); }
        return true;
    case 'V': if (!ctrl) return false; e.Insert(GetClipboardText(m_hwnd)); return true;
    }
    return false;
}

LRESULT FenceWindow::OnChar(UINT msg, WPARAM wp, LPARAM lp) {
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (m_renaming && wp >= 32 && !ctrl) {
        m_titleEdit.Insert((wchar_t)wp);
        m_cursorVisible = true;
        Redraw();
    } else if (m_renameIcon >= 0 && wp >= 32 && !ctrl) {
        m_iconEdit.Insert((wchar_t)wp);
        m_cursorVisible = true;
        Redraw();
    } else if (m_searchFocused && wp >= 32 && !ctrl) {
        m_searchEdit.Insert((wchar_t)wp);
        RebuildFilter();
    } else if (IsMapped() && !m_renaming && m_renameIcon < 0 && wp >= 32 && !ctrl) {
        FocusSearch();
        m_searchEdit.Insert((wchar_t)wp);
        RebuildFilter();
    }
    return 0;
}

LRESULT FenceWindow::OnKeyDown(UINT msg, WPARAM wp, LPARAM lp) {
    if (m_renaming) {
        if (wp == VK_RETURN)       ExitRename(true);
        else if (wp == VK_ESCAPE)  ExitRename(false);
        else if (EditKey(m_titleEdit, wp)) { m_cursorVisible = true; Redraw(); }
    } else if (m_renameIcon >= 0) {
        if (wp == VK_RETURN)       ExitIconRename(true);
        else if (wp == VK_ESCAPE)  ExitIconRename(false);
        else if (EditKey(m_iconEdit, wp)) { m_cursorVisible = true; Redraw(); }
    } else if (m_searchFocused) {
        if (wp == VK_ESCAPE) { ClearSearch(); }
        else {
            std::wstring before = m_searchEdit.Text();
            if (EditKey(m_searchEdit, wp)) {
                m_cursorVisible = true;
                if (m_searchEdit.Text() != before) RebuildFilter(); else Redraw();
            }
        }
    } else if (IsMapped() && wp == 'F' &&
               (GetKeyState(VK_CONTROL) & 0x8000)) {
        FocusSearch();
    }
    return 0;
}

LRESULT FenceWindow::OnKillFocus(UINT msg, WPARAM wp, LPARAM lp) {
    if (this) {
        if (m_renaming)
            ExitRename(true);       // like Explorer: losing focus keeps the new name
        else if (m_renameIcon >= 0)
            ExitIconRename(true);
        else if (m_searchFocused) {
            m_searchFocused = false;
            m_cursorVisible = false;
            if (m_cursorTimer) { KillTimer(m_hwnd, 1); m_cursorTimer = 0; }
            if (m_focusAttached) {
                AttachThreadInput(GetCurrentThreadId(), m_focusAttached, FALSE);
                m_focusAttached = 0;
            }
            Redraw();
        }
    }
    return 0;
}

LRESULT FenceWindow::OnTimer(UINT msg, WPARAM wp, LPARAM lp) {
    if (wp == 1 && (m_renaming || m_renameIcon >= 0 ||
                    m_searchFocused)) {
        m_cursorVisible = !m_cursorVisible;
        Redraw();
    } else if (wp == 2) {
        KillTimer(m_hwnd, 2);
        CaptureBackdrop();
    } else if (wp == 4) {
        CollapseOnOutsideClick();
    } else if (wp == 5) {
        KillTimer(m_hwnd, 5);
        RefreshFromSource();
    }
    return 0;
}
