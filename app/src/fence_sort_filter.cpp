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

static int CompareIcons(const IconEntry& a, const IconEntry& b,
                        int column, bool asc) {
    if (a.isDir != b.isDir) return a.isDir ? -1 : 1;
    int r = 0;
    switch (column) {
    case 1: // Date modified — newest first descending (Explorer default)
        r = CompareFileTime(&a.lastWrite, &b.lastWrite); break;
    case 2: // Size
        r = a.size < b.size ? -1 : (a.size > b.size ? 1 : 0); break;
    case 3: // Type (extension)
        r = StrCmpLogicalW(a.ext.c_str(), b.ext.c_str()); break;
    default: // Name
        r = StrCmpLogicalW(a.name.c_str(), b.name.c_str()); break;
    }
    if (!asc) r = -r;
    if (r == 0) r = StrCmpLogicalW(a.name.c_str(), b.name.c_str());
    return r;
}

void FenceWindow::ApplySort() {
    if (m_sourceFolder.empty()) return;
    if (!m_sortInitialized) {
        m_sortInitialized = true;
        int col; bool asc;
        if (QueryExplorerSortForFolder(m_sourceFolder, col, asc)) {
            m_sortColumn = col; m_sortAscending = asc;
            Config::MarkDirty();
        }
    }
    std::stable_sort(m_icons.begin(), m_icons.end(),
        [this](const IconEntry& a, const IconEntry& b) {
            return CompareIcons(a, b, m_sortColumn, m_sortAscending) < 0;
        });
}

void FenceWindow::SetSort(int column, bool ascending) {
    m_sortColumn = column;
    m_sortAscending = ascending;
    m_sortInitialized = true;
    if (!m_sourceFolder.empty()) {
        ApplySort();
        if (SearchActive()) RebuildFilter();
        else {
            for (auto& ic : m_icons) ic.x = ic.y = 0;
            RelayoutIcons();
        }
        Config::MarkDirty();
    }
}

void FenceWindow::SetSortPreset(int column, bool ascending) {
    m_sortColumn = column;
    m_sortAscending = ascending;
    m_sortInitialized = true;
}

int FenceWindow::VisibleCount() const {
    return SearchActive() ? (int)m_filterIdx.size() : (int)m_icons.size();
}

void FenceWindow::RebuildFilter() {
    m_filterIdx.clear();
    m_filterBits.assign(m_icons.size(), 0);
    if (SearchActive()) {
        for (int i = 0; i < (int)m_icons.size(); i++) {
            if (StrStrIW(m_icons[i].name.c_str(), m_searchText.c_str())) {
                m_filterIdx.push_back(i);
                m_filterBits[i] = 1;
            }
        }
    }
    RelayoutFiltered();
    Redraw();
}

void FenceWindow::FocusSearch() {
    if (!IsMapped() || m_renaming) return;
    m_searchFocused = true;
    m_cursorVisible = true;
    if (m_cursorTimer) { KillTimer(m_hwnd, 1); m_cursorTimer = 0; }
    m_cursorTimer = SetTimer(m_hwnd, 1, 530, nullptr);
    // Attach thread input so SetFocus can reach this desktop child window
    if (!m_focusAttached) {
        HWND fg = GetForegroundWindow();
        DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
        DWORD self_ = GetCurrentThreadId();
        if (fgThread && fgThread != self_ && AttachThreadInput(self_, fgThread, TRUE))
            m_focusAttached = fgThread;
    }
    SetFocus(m_hwnd);
    Redraw();
}

void FenceWindow::ClearSearch() {
    m_searchText.clear();
    if (m_searchFocused) {
        m_searchFocused = false;
        m_cursorVisible = false;
        if (m_cursorTimer) { KillTimer(m_hwnd, 1); m_cursorTimer = 0; }
        if (m_focusAttached) {
            AttachThreadInput(GetCurrentThreadId(), m_focusAttached, FALSE);
            m_focusAttached = 0;
        }
    }
    m_filterIdx.clear();
    m_filterBits.clear();
    RelayoutIcons();
    Redraw();
}
