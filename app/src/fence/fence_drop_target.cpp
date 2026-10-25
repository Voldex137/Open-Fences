#include "fence/fence_drop_target.h"
#include "fence/fence_window.h"
#include "core/com_ptr.h"
#include <shlobj.h>
#include <shobjidl.h>
#include <string>

// ── OLE Drop Target ──
// DragAcceptFiles only surfaces CF_HDROP (filesystem paths). System icons
// (此电脑, 回收站) are shell namespace objects carried as CFSTR_SHELLIDLIST
// PIDLs. RegisterDragDrop + IDropTarget receives both, and
// SHCreateShellItemArrayFromDataObject resolves them uniformly into IShellItem
// objects from which we extract display names and parse paths.

static bool HasShellDropFormat(IDataObject* pDataObj) {
    static const UINT s_cfShellIdList = (UINT)RegisterClipboardFormatW(CFSTR_SHELLIDLIST);
    FORMATETC fmt = {};
    fmt.dwAspect = DVASPECT_CONTENT;
    fmt.lindex = -1;
    fmt.tymed = TYMED_HGLOBAL;
    fmt.cfFormat = CF_HDROP;
    if (pDataObj->QueryGetData(&fmt) == S_OK) return true;
    if (s_cfShellIdList) {
        fmt.cfFormat = s_cfShellIdList;
        if (pDataObj->QueryGetData(&fmt) == S_OK) return true;
    }
    return false;
}

namespace {

class FenceDropTarget : public IDropTarget {
    LONG m_ref = 1;
    HWND m_hwnd = nullptr;
    bool m_over = false;
public:
    explicit FenceDropTarget(HWND hwnd) : m_hwnd(hwnd) {}

    STDMETHOD(QueryInterface)(REFIID riid, void** ppv) {
        if (riid == IID_IUnknown || riid == IID_IDropTarget) {
            *ppv = static_cast<IDropTarget*>(this);
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

    STDMETHOD(DragEnter)(IDataObject* pDataObj, DWORD, POINTL, DWORD* pdwEffect) {
        m_over = HasShellDropFormat(pDataObj);
        *pdwEffect = m_over ? DROPEFFECT_MOVE : DROPEFFECT_NONE;
        if (m_over) {
            auto* self = (FenceWindow*)GetWindowLongPtr(m_hwnd, GWLP_USERDATA);
            if (self) self->SetDragOver(true);
        }
        return S_OK;
    }
    STDMETHOD(DragOver)(DWORD, POINTL, DWORD* pdwEffect) {
        *pdwEffect = m_over ? DROPEFFECT_MOVE : DROPEFFECT_NONE;
        return S_OK;
    }
    STDMETHOD(DragLeave)() {
        m_over = false;
        auto* self = (FenceWindow*)GetWindowLongPtr(m_hwnd, GWLP_USERDATA);
        if (self) self->SetDragOver(false);
        return S_OK;
    }
    STDMETHOD(Drop)(IDataObject* pDataObj, DWORD, POINTL pt, DWORD* pdwEffect) {
        m_over = false;
        auto* self = (FenceWindow*)GetWindowLongPtr(m_hwnd, GWLP_USERDATA);
        if (!self) { *pdwEffect = DROPEFFECT_NONE; return S_OK; }
        self->SetDragOver(false);

        ComPtr<IShellItemArray> items;
        if (FAILED(SHCreateShellItemArrayFromDataObject(pDataObj, IID_PPV_ARGS(&items))) || !items) {
            *pdwEffect = DROPEFFECT_NONE;
            return S_OK;
        }

        POINT dropPt = { pt.x, pt.y };
        ScreenToClient(m_hwnd, &dropPt);
        float dx = (float)dropPt.x, dy = (float)dropPt.y;

        DWORD count = 0;
        items->GetCount(&count);
        for (DWORD i = 0; i < count; i++) {
            ComPtr<IShellItem> item;
            if (FAILED(items->GetItemAt(i, &item)) || !item) continue;

            LPWSTR pwszPath = nullptr;
            if (FAILED(item->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &pwszPath)) || !pwszPath)
                continue;
            std::wstring parsePath(pwszPath);
            CoTaskMemFree(pwszPath);

            LPWSTR pwszName = nullptr;
            std::wstring displayName;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &pwszName)) && pwszName) {
                displayName = pwszName;
                CoTaskMemFree(pwszName);
            }
            if (displayName.empty()) displayName = parsePath;

            self->AddDroppedItem(displayName, parsePath, dx, dy);
        }

        *pdwEffect = DROPEFFECT_MOVE;
        return S_OK;
    }
};

}  // namespace

IDropTarget* CreateFenceDropTarget(HWND hwnd) { return new FenceDropTarget(hwnd); }
