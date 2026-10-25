#include "fence_window.h"
#include "fence_metrics.h"
#include "desktop_host.h"
#include "localization.h"
#include "settings_panel.h"
#include "context_menu.h"
#include "menu_icons.h"
#include "config.h"
#include "explorer_sort.h"
#include "shell_context_menu.h"
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <commctrl.h>
#include <algorithm>
#include <cmath>

LRESULT FenceWindow::OnChar(UINT msg, WPARAM wp, LPARAM lp) {
    if (m_renaming && wp >= 32) {
        m_renameBuf += (wchar_t)wp;
        Redraw();
    } else if (m_renameIcon >= 0 && wp >= 32) {
        if (m_iconRenamePristine) {
            m_iconRenameBuf.clear();
            m_iconRenameCaret = 0;
            m_iconRenamePristine = false;
        }
        m_iconRenameBuf.insert(m_iconRenameCaret, 1, (wchar_t)wp);
        m_iconRenameCaret++;
        Redraw();
    } else if (m_searchFocused) {
        if (wp == 0x08) {   // backspace
            if (!m_searchText.empty()) {
                m_searchText.pop_back();
                RebuildFilter();
            }
        } else if (wp >= 32) {
            m_searchText += (wchar_t)wp;
            RebuildFilter();
        }
    } else if (IsMapped() && wp >= 32) {
        FocusSearch();
        m_searchText += (wchar_t)wp;
        RebuildFilter();
    }
    return 0;
}

LRESULT FenceWindow::OnKeyDown(UINT msg, WPARAM wp, LPARAM lp) {
    if (m_renaming) {
        if (wp == VK_RETURN)  { ExitRename(true); }
        if (wp == VK_ESCAPE)  { ExitRename(false); }
        if (wp == VK_BACK && !m_renameBuf.empty()) {
            m_renameBuf.pop_back();
            Redraw();
        }
    } else if (m_renameIcon >= 0) {
        auto& buf = m_iconRenameBuf;
        auto& caret = m_iconRenameCaret;
        // Explorer's rename-box editing: the initial select-all consumes
        // the first typed/backspaced input; Left/Right/Home/End only
        // collapse it to an edge of the selection.
        if (wp == VK_RETURN) {
            ExitIconRename(true);
        } else if (wp == VK_ESCAPE) {
            ExitIconRename(false);
        } else if (wp == VK_BACK) {
            if (m_iconRenamePristine) {
                buf.clear(); caret = 0; m_iconRenamePristine = false;
            } else if (caret > 0) { buf.erase(caret - 1, 1); caret--; }
            Redraw();
        } else if (wp == VK_DELETE) {
            if (m_iconRenamePristine) {
                buf.clear(); caret = 0; m_iconRenamePristine = false;
            } else if (caret < buf.size()) buf.erase(caret, 1);
            Redraw();
        } else if (wp == VK_LEFT) {
            if (m_iconRenamePristine) { m_iconRenamePristine = false; caret = 0; }
            else if (caret > 0) caret--;
            Redraw();
        } else if (wp == VK_RIGHT) {
            if (m_iconRenamePristine) { m_iconRenamePristine = false; caret = buf.size(); }
            else if (caret < buf.size()) caret++;
            Redraw();
        } else if (wp == VK_HOME) {
            m_iconRenamePristine = false; caret = 0;
            Redraw();
        } else if (wp == VK_END) {
            m_iconRenamePristine = false; caret = buf.size();
            Redraw();
        }
    } else if (m_searchFocused) {
        if (wp == VK_ESCAPE) { ClearSearch(); }
    } else if (IsMapped() && wp == 'F' &&
               (GetKeyState(VK_CONTROL) & 0x8000)) {
        FocusSearch();
    }
    return 0;
}

LRESULT FenceWindow::OnKillFocus(UINT msg, WPARAM wp, LPARAM lp) {
    if (this) {
        if (m_renaming)
            ExitRename(false);
        else if (m_renameIcon >= 0)
            ExitIconRename(true);   // Explorer commits on focus loss
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
    } else if (wp == 5) {
        KillTimer(m_hwnd, 5);
        RefreshFromSource();
    }
    return 0;
}
