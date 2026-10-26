// Stacks of folded panels: chaining, closing up a row that leaves, inserting a whole
// stack under a row, and repairing broken links from an edited config.
#include "fence/stack_chain.h"
#include "test_util.h"

using test::Check;

static StackNodes Chain() {   // A <- B <- C, D alone
    return { { L"A", L"" }, { L"B", L"A" }, { L"C", L"B" }, { L"D", L"" } };
}
static std::wstring AboveOf(const StackNodes& n, const wchar_t* id) {
    for (const auto& x : n) if (x.id == id) return x.above;
    return L"?";
}

static void TestRowsBelow() {
    StackNodes n = Chain();
    auto rows = StackChain::RowsBelow(n, L"A");
    Check(rows.size() == 2 && rows[0] == L"B" && rows[1] == L"C", "rows come top to bottom");
    Check(StackChain::RowsBelow(n, L"D").empty(), "a lone panel has no rows below");
    Check(StackChain::RowBelow(n, L"B") == L"C", "direct row below");
}

static void TestExtractMiddle() {
    StackNodes n = Chain();
    StackChain::Extract(n, L"B");
    Check(AboveOf(n, L"B").empty(), "the extracted row stands alone");
    Check(AboveOf(n, L"C") == L"A", "the rows below close up");
}

static void TestExtractTopAndBottom() {
    StackNodes n = Chain();
    StackChain::Extract(n, L"A");
    Check(AboveOf(n, L"B").empty() && AboveOf(n, L"C") == L"B", "removing the top makes the next row the top");
    n = Chain();
    StackChain::Extract(n, L"C");
    Check(AboveOf(n, L"B") == L"A" && AboveOf(n, L"C").empty(), "removing the bottom leaves the rest intact");
}

static void TestAttachSingle() {
    StackNodes n = Chain();
    Check(StackChain::AttachBelow(n, L"D", L"A"), "attach a lone row");
    Check(AboveOf(n, L"D") == L"A", "it sits directly below the target");
    Check(AboveOf(n, L"B") == L"D", "what was below the target now hangs from the new row");
    auto rows = StackChain::RowsBelow(n, L"A");
    Check(rows.size() == 3 && rows[0] == L"D" && rows[1] == L"B" && rows[2] == L"C", "final order A, D, B, C");
}

static void TestAttachWholeStack() {
    StackNodes n = { { L"A", L"" }, { L"B", L"A" }, { L"X", L"" }, { L"Y", L"X" } };
    Check(StackChain::AttachBelow(n, L"X", L"A"), "attach a stack under a row");
    auto rows = StackChain::RowsBelow(n, L"A");
    Check(rows.size() == 3 && rows[0] == L"X" && rows[1] == L"Y" && rows[2] == L"B",
          "the moved stack keeps its order and the old rows follow it");
}

static void TestAttachRefusals() {
    StackNodes n = Chain();
    Check(!StackChain::AttachBelow(n, L"A", L"A"), "not onto itself");
    Check(!StackChain::AttachBelow(n, L"A", L"C"), "not under one of its own rows (would loop)");
    Check(AboveOf(n, L"A").empty() && AboveOf(n, L"B") == L"A", "a refused attach changes nothing");
    Check(!StackChain::AttachBelow(n, L"D", L"missing"), "unknown target");
}

static void TestSanitize() {
    StackNodes n = { { L"A", L"gone" }, { L"B", L"B" }, { L"C", L"D" }, { L"D", L"C" }, { L"E", L"F" }, { L"F", L"E" } };
    StackChain::Sanitize(n);
    Check(AboveOf(n, L"A").empty(), "a link to a missing panel is dropped");
    Check(AboveOf(n, L"B").empty(), "a panel cannot hang from itself");
    bool noLoop = true;
    for (const auto& x : n) {
        auto rows = StackChain::RowsBelow(n, x.id);
        if (rows.size() > n.size()) noLoop = false;
    }
    Check(noLoop && (AboveOf(n, L"C").empty() || AboveOf(n, L"D").empty()), "a cycle is broken");
}

int main() {
    TestRowsBelow();
    TestExtractMiddle();
    TestExtractTopAndBottom();
    TestAttachSingle();
    TestAttachWholeStack();
    TestAttachRefusals();
    TestSanitize();
    return test::Finish();
}
