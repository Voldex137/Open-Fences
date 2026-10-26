#pragma once

#include <string>
#include <vector>

// Stacks of folded panels, as plain data: each panel remembers the id of the panel
// directly above it (empty = the top row of a stack, or not stacked at all). A
// stack is therefore a chain top -> ... -> bottom. No window code in here.
struct StackNode {
    std::wstring id;
    std::wstring above;
};
using StackNodes = std::vector<StackNode>;

namespace StackChain {

// Ids of the rows below `id`, top to bottom. Stops at a cycle or a missing link.
std::vector<std::wstring> RowsBelow(const StackNodes& nodes, const std::wstring& id);

// The row directly below `id`, or empty.
std::wstring RowBelow(const StackNodes& nodes, const std::wstring& id);

// Takes `id` out of its stack: the rows that were below it close up (they now hang
// from the row that was above it). `id` itself ends up standing alone.
void Extract(StackNodes& nodes, const std::wstring& id);

// Puts `id` and every row below it directly under `target`; the rows that were
// below `target` follow after them. `id` must be the top row of its own chain
// (call Extract first for a row from the middle of a stack). Returns false, and
// changes nothing, when `target` is `id` itself or one of the rows below it.
bool AttachBelow(StackNodes& nodes, const std::wstring& id, const std::wstring& target);

// Clears links that point at nothing (a panel was deleted, or an edited config).
void Sanitize(StackNodes& nodes);

}  // namespace StackChain
