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

DWORD WINAPI FenceWindow::ChangeThreadProc(LPVOID param) {
    auto* self = (FenceWindow*)param;
    HANDLE handles[2] = { self->m_stopEvent, self->m_changeHandle };
    for (;;) {
        DWORD result = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if (result == WAIT_OBJECT_0) break;
        if (result == WAIT_OBJECT_0 + 1) {
            PostMessage(self->m_hwnd, WM_FENCE_REFRESH, 0, 0);
            FindNextChangeNotification(self->m_changeHandle);
        }
    }
    return 0;
}

void FenceWindow::StartSourceWatch() {
    if (m_sourceFolder.empty() || m_changeHandle != INVALID_HANDLE_VALUE) return;
    m_changeHandle = FindFirstChangeNotificationW(
        m_sourceFolder.c_str(), FALSE,
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
        FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE |
        FILE_NOTIFY_CHANGE_ATTRIBUTES);
    if (m_changeHandle == INVALID_HANDLE_VALUE) return;
    m_stopEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!m_stopEvent) {
        FindCloseChangeNotification(m_changeHandle);
        m_changeHandle = INVALID_HANDLE_VALUE;
        return;
    }
    m_changeThread = CreateThread(nullptr, 0, ChangeThreadProc, this, 0, nullptr);
    if (!m_changeThread) {
        CloseHandle(m_stopEvent); m_stopEvent = nullptr;
        FindCloseChangeNotification(m_changeHandle); m_changeHandle = INVALID_HANDLE_VALUE;
    }
}

void FenceWindow::StopSourceWatch() {
    if (m_changeHandle == INVALID_HANDLE_VALUE) return;
    SetEvent(m_stopEvent);
    WaitForSingleObject(m_changeThread, 2000);
    CloseHandle(m_changeThread); m_changeThread = nullptr;
    CloseHandle(m_stopEvent); m_stopEvent = nullptr;
    FindCloseChangeNotification(m_changeHandle); m_changeHandle = INVALID_HANDLE_VALUE;
}

void FenceWindow::MapToFolder(const std::wstring& folderPath) {
    StopSourceWatch();
    ClearSearch();
    m_sourceFolder = folderPath;
    if (folderPath.empty()) { Config::MarkDirty(); return; }
    EnumerateSourceFolder(false);
    StartSourceWatch();
    Config::MarkDirty();
}

void FenceWindow::UnmapFolder() {
    StopSourceWatch();
    ClearSearch();
    m_sourceFolder.clear();
    Config::MarkDirty();
}

void FenceWindow::RefreshFromSource() {
    if (m_sourceFolder.empty()) return;
    if (m_dragIcon >= 0 || m_moving || m_resizeMask) return;
    EnumerateSourceFolder(true);
    Redraw();
    Config::MarkDirty();
}

void FenceWindow::EnumerateSourceFolder(bool keepPositions) {
    if (m_sourceFolder.empty()) return;

    std::unordered_map<std::wstring, std::pair<float, float>> oldPos;
    if (keepPositions) {
        for (const auto& icon : m_icons) {
            std::wstring key = icon.path;
            for (auto& ch : key) ch = (wchar_t)::towlower(ch);
            oldPos[key] = { icon.x, icon.y };
        }
    }

    std::vector<IconEntry> newIcons;
    std::wstring searchPath = m_sourceFolder + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (fd.cFileName[0] == L'.' && (fd.cFileName[1] == L'\0' ||
                (fd.cFileName[1] == L'.' && fd.cFileName[2] == L'\0')))
                continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) continue;

            IconEntry e;
            e.name = fd.cFileName;
            e.path = m_sourceFolder + L"\\" + fd.cFileName;
            e.size = ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            e.lastWrite = fd.ftLastWriteTime;
            e.isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            e.ext = PathFindExtensionW(fd.cFileName);

            if (keepPositions) {
                std::wstring key = e.path;
                for (auto& ch : key) ch = (wchar_t)::towlower(ch);
                auto it = oldPos.find(key);
                if (it != oldPos.end()) {
                    e.x = it->second.first;
                    e.y = it->second.second;
                }
            }
            newIcons.push_back(std::move(e));
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }

    m_icons = std::move(newIcons);
    ApplySort();
    if (SearchActive()) RebuildFilter();
    else              RelayoutIcons();
}
