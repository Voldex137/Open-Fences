#include "fence/fence_window.h"
#include "core/config.h"
#include "shell/shell_integration.h"
#include <shlwapi.h>

void FenceWindow::EnterRename() {
    m_renaming = true;
    m_renameBuf = m_title;
    m_cursorVisible = true;
    m_cursorTimer = SetTimer(m_hwnd, 1, 530, nullptr);
    // A desktop child shares explorer's foreground; SetFocus can only reach
    // it with our input queue attached to the foreground thread. Attach for
    // the whole rename session (detached in ExitRename).
    HWND fg = GetForegroundWindow();
    DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    DWORD self_ = GetCurrentThreadId();
    if (fgThread && fgThread != self_ && AttachThreadInput(self_, fgThread, TRUE))
        m_focusAttached = fgThread;
    SetFocus(m_hwnd);
    Redraw();
}

void FenceWindow::ExitRename(bool commit) {
    if (!m_renaming) return;
    m_renaming = false;
    if (commit && m_renameBuf != m_title) {
        m_title = m_renameBuf;
        ShellIntegration::Refresh();   // the panel name is in the Explorer menu
        Config::MarkDirty();
    }
    KillTimer(m_hwnd, 1);
    m_cursorTimer = 0;
    if (m_focusAttached) {
        AttachThreadInput(GetCurrentThreadId(), m_focusAttached, FALSE);
        m_focusAttached = 0;
    }
    Redraw();
}

void FenceWindow::EnterIconRename(int idx) {
    if (idx < 0 || idx >= (int)m_icons.size() || m_renaming || m_collapsed) return;
    m_renameIcon = idx;
    // Explorer semantics: the base name comes up selected (typing replaces
    // it) while the extension stays fixed. Folders have no extension.
    const std::wstring& full = m_icons[idx].name;
    m_iconRenameExt = m_icons[idx].isDir ? L"" : PathFindExtensionW(full.c_str());
    m_iconRenameBuf = full.substr(0, full.size() - m_iconRenameExt.size());
    m_iconRenameCaret = m_iconRenameBuf.size();
    m_iconRenamePristine = true;
    m_cursorVisible = true;
    if (!m_cursorTimer) m_cursorTimer = SetTimer(m_hwnd, 1, 530, nullptr);
    // Same focus trick as the title rename: a desktop child can only hold
    // keyboard focus with our input queue attached to the foreground thread.
    HWND fg = GetForegroundWindow();
    DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    DWORD self_ = GetCurrentThreadId();
    if (fgThread && fgThread != self_ && AttachThreadInput(self_, fgThread, TRUE))
        m_focusAttached = fgThread;
    SetFocus(m_hwnd);
    Redraw();
}

void FenceWindow::ExitIconRename(bool commit) {
    if (m_renameIcon < 0) return;
    int idx = m_renameIcon;
    m_renameIcon = -1;
    if (commit && idx < (int)m_icons.size()) {
        std::wstring newName = m_iconRenameBuf + m_iconRenameExt;
        if (newName != m_icons[idx].name) {
            std::wstring oldPath = m_icons[idx].path;
            size_t slash = oldPath.find_last_of(L'\\');
            // Invalid names (empty, reserved chars) fail MoveFileW — then the
            // icon simply keeps its old name, like Explorer's error would.
            if (slash != std::wstring::npos && !m_iconRenameBuf.empty()) {
                std::wstring newPath = oldPath.substr(0, slash + 1) + newName;
                if (MoveFileW(oldPath.c_str(), newPath.c_str())) {
                    m_icons[idx].name = newName;
                    m_icons[idx].path = newPath;
                    m_icons[idx].ext = PathFindExtensionW(newPath.c_str());
                    for (auto& p : m_selectedPaths)
                        if (p == oldPath) p = newPath;
                    // Mapped fences re-enumerate via the change watcher;
                    // updating the entry now keeps the label right until it
                    // fires (the watcher's keep-positions pass sees the new
                    // path).
                    Config::MarkDirty();
                } else {
                }
            }
        }
    }
    if (!m_renaming && !m_searchFocused) {
        if (m_cursorTimer) { KillTimer(m_hwnd, 1); m_cursorTimer = 0; }
        if (m_focusAttached) {
            AttachThreadInput(GetCurrentThreadId(), m_focusAttached, FALSE);
            m_focusAttached = 0;
        }
    }
    Redraw();
}
