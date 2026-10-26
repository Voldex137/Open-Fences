#include "fence/stack_chain.h"

#include <algorithm>
#include <set>

namespace StackChain {

namespace {

StackNode* Find(StackNodes& nodes, const std::wstring& id) {
    for (auto& n : nodes) if (n.id == id) return &n;
    return nullptr;
}

}  // namespace

std::wstring RowBelow(const StackNodes& nodes, const std::wstring& id) {
    if (id.empty()) return {};
    for (const auto& n : nodes) if (n.above == id) return n.id;
    return {};
}

std::vector<std::wstring> RowsBelow(const StackNodes& nodes, const std::wstring& id) {
    std::vector<std::wstring> rows;
    std::set<std::wstring> seen{ id };
    std::wstring cur = id;
    for (;;) {
        std::wstring next = RowBelow(nodes, cur);
        if (next.empty() || !seen.insert(next).second) break;
        rows.push_back(next);
        cur = next;
    }
    return rows;
}

void Extract(StackNodes& nodes, const std::wstring& id) {
    StackNode* self = Find(nodes, id);
    if (!self) return;
    std::wstring below = RowBelow(nodes, id);
    if (!below.empty())
        if (StackNode* b = Find(nodes, below)) b->above = self->above;
    self->above.clear();
}

bool AttachBelow(StackNodes& nodes, const std::wstring& id, const std::wstring& target) {
    StackNode* self = Find(nodes, id);
    StackNode* tgt = Find(nodes, target);
    if (!self || !tgt || id == target) return false;
    std::vector<std::wstring> mine = RowsBelow(nodes, id);
    if (std::find(mine.begin(), mine.end(), target) != mine.end()) return false;

    std::wstring tail = mine.empty() ? id : mine.back();
    std::wstring oldBelow = RowBelow(nodes, target);   // what hung under the target so far
    if (!oldBelow.empty() && oldBelow != id)
        if (StackNode* b = Find(nodes, oldBelow)) b->above = tail;
    self->above = target;
    return true;
}

void Sanitize(StackNodes& nodes) {
    for (auto& n : nodes) {
        if (n.above == n.id || (!n.above.empty() && !Find(nodes, n.above))) n.above.clear();
    }
    // Two rows hanging from the same row, or a cycle: keep the first, free the rest.
    std::set<std::wstring> used;
    for (auto& n : nodes) {
        if (n.above.empty()) continue;
        if (!used.insert(n.above).second) n.above.clear();
    }
    for (auto& n : nodes) {
        if (n.above.empty()) continue;
        // Walk upwards; a loop means this row belongs to a cycle with no top row.
        std::set<std::wstring> seen{ n.id };
        std::wstring cur = n.above;
        while (!cur.empty()) {
            if (!seen.insert(cur).second) { n.above.clear(); break; }
            StackNode* up = Find(nodes, cur);
            cur = up ? up->above : std::wstring();
        }
    }
}

}  // namespace StackChain
