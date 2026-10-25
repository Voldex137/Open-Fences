// The editable look of a fence round-trips through JSON, an old file without the
// newer fields still loads, and CopyStyle moves exactly the editable fields.
#include "core/appearance_json.h"
#include "test_util.h"

#include <cwchar>

using test::Check;

static void TestRoundTrip() {
    FenceAppearance a;
    a.bg[0] = 0.25f;
    a.fillDir = 1;
    a.autoReflow = false;
    a.collapseStyle = 1;
    a.titleIcon = true;
    wcscpy_s(a.iconPath, L"C:\\Games\\\u0422\u0435\u0441\u0442\\app.exe");
    wcscpy_s(a.fontName, L"Consolas");

    FenceAppearance b;
    AppearanceJson::FromJson(AppearanceJson::ToJson(a), b);
    Check(b.bg[0] == 0.25f, "colour survives");
    Check(b.fillDir == 1 && !b.autoReflow && b.collapseStyle == 1 && b.titleIcon, "flags survive");
    Check(wcscmp(b.iconPath, a.iconPath) == 0, "a non-ASCII icon path survives");
    Check(wcscmp(b.fontName, L"Consolas") == 0, "font survives");
}

static void TestOldFileKeepsDefaults() {
    nlohmann::json old = { { "titleAlign", 2 }, { "displayMode", 1 } };
    FenceAppearance a;
    AppearanceJson::FromJson(old, a);
    Check(a.titleAlign == 2 && a.displayMode == 1, "stored fields load");
    Check(a.autoReflow && a.collapseStyle == 0 && !a.titleIcon && a.iconPath[0] == 0,
          "missing fields keep their defaults (auto-fit on, title bar, no icon)");
}

static void TestCopyStyle() {
    FenceAppearance src, dst;
    src.titleH = 50.0f;
    src.fillDir = 1;
    wcscpy_s(src.iconPath, L"C:\\x.ico");
    dst.cellW = 123.0f;
    AppearanceJson::CopyStyle(dst, src);
    Check(dst.titleH == 50.0f && dst.fillDir == 1 && wcscmp(dst.iconPath, L"C:\\x.ico") == 0,
          "editable fields are copied");
    Check(dst.cellW == 123.0f, "DPI-derived metrics are left alone");
}

int main() {
    TestRoundTrip();
    TestOldFileKeepsDefaults();
    TestCopyStyle();
    return test::Finish();
}
