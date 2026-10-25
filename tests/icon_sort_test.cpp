// Ordering of icons in a folder-backed fence (Explorer-like).
#include "icon_sort.h"
#include "test_util.h"

#include <algorithm>
#include <string>
#include <vector>

using test::Check;

static IconEntry Entry(const wchar_t* name, bool isDir, ULONGLONG size, const wchar_t* ext,
                       unsigned long long fileTime = 0) {
    IconEntry e;
    e.name = name;
    e.isDir = isDir;
    e.size = size;
    e.ext = ext;
    e.lastWrite.dwLowDateTime = (DWORD)(fileTime & 0xFFFFFFFF);
    e.lastWrite.dwHighDateTime = (DWORD)(fileTime >> 32);
    return e;
}

static std::wstring Order(std::vector<IconEntry> v, int column, bool asc) {
    std::sort(v.begin(), v.end(), [&](const IconEntry& a, const IconEntry& b) {
        return CompareIcons(a, b, column, asc) < 0;
    });
    std::wstring s;
    for (const auto& e : v) s += e.name + L" ";
    return s;
}

int main() {
    std::vector<IconEntry> files = {
        Entry(L"b.txt", false, 300, L".txt", 20),
        Entry(L"a.png", false, 100, L".png", 30),
        Entry(L"c.txt", false, 200, L".txt", 10),
    };

    Check(Order(files, 0, true) == L"a.png b.txt c.txt ", "by name ascending");
    Check(Order(files, 0, false) == L"c.txt b.txt a.png ", "by name descending");
    Check(Order(files, 2, true) == L"a.png c.txt b.txt ", "by size ascending");
    Check(Order(files, 2, false) == L"b.txt c.txt a.png ", "by size descending");
    Check(Order(files, 1, true) == L"c.txt b.txt a.png ", "by date modified ascending");
    Check(Order(files, 1, false) == L"a.png b.txt c.txt ", "by date modified descending");
    Check(Order(files, 3, true) == L"a.png b.txt c.txt ", "by type, then name");
    Check(Order(files, 3, false) == L"b.txt c.txt a.png ", "by type descending, ties still by name ascending");

    std::vector<IconEntry> mixed = {
        Entry(L"zeta.txt", false, 1, L".txt"),
        Entry(L"Docs", true, 0, L""),
        Entry(L"alpha.txt", false, 1, L".txt"),
        Entry(L"Apps", true, 0, L""),
    };
    Check(Order(mixed, 0, true) == L"Apps Docs alpha.txt zeta.txt ", "folders first, ascending");
    Check(Order(mixed, 0, false) == L"Docs Apps zeta.txt alpha.txt ", "folders stay first when descending");

    std::vector<IconEntry> numbered = {
        Entry(L"file10.txt", false, 1, L".txt"),
        Entry(L"file2.txt", false, 1, L".txt"),
        Entry(L"file1.txt", false, 1, L".txt"),
    };
    Check(Order(numbered, 0, true) == L"file1.txt file2.txt file10.txt ", "natural number order (file2 before file10)");

    Check(CompareIcons(files[0], files[0], 0, true) == 0, "an icon equals itself");
    return test::Finish();
}
