#include "fence/fence_window.h"
#include "core/com_ptr.h"
#include "core/config.h"
#include "core/storage.h"

#include <shlobj.h>
#include <shobjidl.h>
#include <algorithm>
#include <string>
#include <vector>

// Dragging icons out of a fence: once the cursor leaves the window during an icon
// drag, the drag becomes a normal shell drag (OLE) carrying the real files, so they
// can be dropped on the desktop, into an Explorer window, or onto another fence.

FenceWindow* FenceWindow::s_externalDragSource = nullptr;

namespace {

class DropSource : public IDropSource {
    LONG m_ref = 1;
public:
    STDMETHOD(QueryInterface)(REFIID riid, void** ppv) {
        if (riid == IID_IUnknown || riid == IID_IDropSource) {
            *ppv = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)()  { return InterlockedIncrement(&m_ref); }
    STDMETHOD_(ULONG, Release)() {
        LONG c = InterlockedDecrement(&m_ref);
        if (c == 0) delete this;
        return c;
    }
    STDMETHOD(QueryContinueDrag)(BOOL escape, DWORD keyState) {
        if (escape) return DRAGDROP_S_CANCEL;
        if (!(keyState & MK_LBUTTON)) return DRAGDROP_S_DROP;
        return S_OK;
    }
    STDMETHOD(GiveFeedback)(DWORD) { return DRAGDROP_S_USEDEFAULTCURSORS; }
};

// An IDataObject for files that may live in different folders.
ComPtr<IDataObject> MakeDataObject(const std::vector<std::wstring>& paths) {
    std::vector<PIDLIST_ABSOLUTE> pidls;
    for (const auto& p : paths) {
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (SUCCEEDED(SHParseDisplayName(p.c_str(), nullptr, &pidl, 0, nullptr)) && pidl) pidls.push_back(pidl);
    }
    ComPtr<IDataObject> data;
    if (!pidls.empty()) {
        ComPtr<IShellItemArray> items;
        if (SUCCEEDED(SHCreateShellItemArrayFromIDLists((UINT)pidls.size(),
                                                        (PCIDLIST_ABSOLUTE_ARRAY)pidls.data(), &items)) && items)
            items->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(&data));
    }
    for (auto* p : pidls) CoTaskMemFree(p);
    return data;
}

}  // namespace

bool FenceWindow::CursorLeftWindow(int margin) const {
    POINT c;
    RECT r;
    if (!GetCursorPos(&c) || !GetWindowRect(m_hwnd, &r)) return false;
    InflateRect(&r, margin, margin);
    return !PtInRect(&r, c);
}

void FenceWindow::BeginExternalDrag() {
    // The files being dragged: the whole dragged selection.
    std::vector<std::wstring> paths;
    for (const auto& d : m_dragGroup)
        if (d.idx >= 0 && d.idx < (int)m_icons.size()) paths.push_back(m_icons[d.idx].path);
    if (paths.empty()) return;

    // Put the icons back where they were: the manual in-window drag is over.
    for (const auto& d : m_dragGroup)
        if (d.idx >= 0 && d.idx < (int)m_icons.size()) { m_icons[d.idx].x = d.x; m_icons[d.idx].y = d.y; }
    SettleDragGroup();
    ReleaseCapture();

    ComPtr<IDataObject> data = MakeDataObject(paths);
    if (!data) return;

    // Files that live in the storage folder were put there by this app, so dragging
    // them out MOVES them (the desktop is where they came from). A reference to a
    // file somewhere else must never be moved by dropping it elsewhere.
    bool allStored = std::all_of(paths.begin(), paths.end(),
                                 [](const std::wstring& p) { return Storage::IsStored(p); });
    DWORD allowed = (allStored || IsMapped()) ? (DROPEFFECT_MOVE | (IsMapped() ? (DROPEFFECT_COPY | DROPEFFECT_LINK) : 0))
                                              : (DROPEFFECT_COPY | DROPEFFECT_LINK);
    DWORD effect = DROPEFFECT_NONE;

    s_externalDragSource = this;
    ComPtr<IDropSource> source;
    source.Attach(new DropSource());
    DoDragDrop(data.Get(), source.Get(), allowed, &effect);
    s_externalDragSource = nullptr;

    // Moved away: the icons go from this fence (the files are elsewhere now, or were
    // handed to another fence, which stores its own reference).
    if (effect == DROPEFFECT_MOVE) {
        for (const auto& path : paths) {
            for (int i = 0; i < (int)m_icons.size(); i++) {
                if (m_icons[i].path == path) { RemoveIconAt(i, false); break; }
            }
        }
    }
    Redraw();
}
