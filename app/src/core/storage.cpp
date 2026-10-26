#include "core/storage.h"

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <vector>

namespace Storage {

namespace {

std::wstring g_folder;   // empty = the default

std::wstring KnownFolder(const KNOWNFOLDERID& id) {
    PWSTR raw = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &raw)) && raw) result = raw;
    CoTaskMemFree(raw);
    return result;
}

std::wstring Normalize(std::wstring p) {
    while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
    return p;
}

bool SameText(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(), (int)a.size(), b.c_str(), (int)b.size(), TRUE) == CSTR_EQUAL;
}

std::wstring ParentOf(const std::wstring& path) {
    size_t cut = path.find_last_of(L"\\/");
    return cut == std::wstring::npos ? std::wstring() : path.substr(0, cut);
}

std::wstring NameOf(const std::wstring& path) {
    size_t cut = path.find_last_of(L"\\/");
    return cut == std::wstring::npos ? path : path.substr(cut + 1);
}

// "name", "name (2)", "name (3)" ...: the first that does not exist in `dir`.
std::wstring UniquePath(const std::wstring& dir, const std::wstring& name) {
    std::wstring candidate = dir + L"\\" + name;
    if (GetFileAttributesW(candidate.c_str()) == INVALID_FILE_ATTRIBUTES) return candidate;

    std::wstring stem = name, ext;
    size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) { stem = name.substr(0, dot); ext = name.substr(dot); }
    for (int n = 2; n < 10000; n++) {
        candidate = dir + L"\\" + stem + L" (" + std::to_wstring(n) + L")" + ext;
        if (GetFileAttributesW(candidate.c_str()) == INVALID_FILE_ATTRIBUTES) return candidate;
    }
    return std::wstring();
}

// Moves a file or folder (across volumes too). `to` is the full new path.
bool MovePath(const std::wstring& from, const std::wstring& to) {
    std::wstring src = from; src.push_back(L'\0');   // SHFileOperation wants a double-null list
    std::wstring dst = to;   dst.push_back(L'\0');
    SHFILEOPSTRUCTW op = {};
    op.wFunc = FO_MOVE;
    op.pFrom = src.c_str();
    op.pTo = dst.c_str();
    op.fFlags = FOF_NOCONFIRMATION | FOF_NOCONFIRMMKDIR | FOF_NOERRORUI | FOF_SILENT;
    return SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted;
}

}  // namespace

std::wstring Folder() {
    if (!g_folder.empty()) return g_folder;
    std::wstring docs = KnownFolder(FOLDERID_Documents);
    if (docs.empty()) docs = KnownFolder(FOLDERID_Profile);
    return docs + L"\\Open Fences";
}

void SetFolder(const std::wstring& path) { g_folder = Normalize(path); }

bool IsDefault() { return g_folder.empty(); }

bool IsDesktopItem(const std::wstring& path) {
    // Shell namespace items ("::{...}": This PC, Recycle Bin) are not files.
    if (path.empty() || path.rfind(L"::", 0) == 0) return false;
    std::wstring parent = ParentOf(path);
    if (parent.empty()) return false;
    return SameText(parent, KnownFolder(FOLDERID_Desktop)) ||
           SameText(parent, KnownFolder(FOLDERID_PublicDesktop));
}

bool IsStored(const std::wstring& path) {
    std::wstring folder = Folder();
    if (folder.empty() || path.size() <= folder.size()) return false;
    return SameText(path.substr(0, folder.size()), folder) && path[folder.size()] == L'\\';
}

std::wstring AdoptFromDesktop(const std::wstring& path) {
    if (!IsDesktopItem(path)) return {};
    std::wstring folder = Folder();
    if (SHCreateDirectoryExW(nullptr, folder.c_str(), nullptr) != ERROR_SUCCESS &&
        GetFileAttributesW(folder.c_str()) == INVALID_FILE_ATTRIBUTES)
        return {};
    std::wstring target = UniquePath(folder, NameOf(path));
    if (target.empty() || !MovePath(path, target)) return {};
    return target;
}

std::wstring ReturnToDesktop(const std::wstring& path) {
    if (!IsStored(path)) return {};
    std::wstring desktop = KnownFolder(FOLDERID_Desktop);
    if (desktop.empty() || GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return {};
    std::wstring target = UniquePath(desktop, NameOf(path));
    if (target.empty() || !MovePath(path, target)) return {};
    return target;
}

}  // namespace Storage
