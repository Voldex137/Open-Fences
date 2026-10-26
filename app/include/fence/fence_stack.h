#pragma once

#include "fence/fence_window.h"

// Stacks of folded panels on the desktop: panels dropped onto the row of another
// folded panel hang below it and move with it (see stack_chain.h for the data
// model). This module applies the model to the real windows.
namespace FenceStack {

// A panel that can be part of a stack: folded or not, but in title-bar style.
bool Eligible(FenceWindow& f);

// True when `f` has a row above or below it.
bool IsStacked(const FenceWindow& f);

// `f` leaves its stack; the rows below it close up.
void Extract(FenceWindow& f);

// `f` was just dropped: when its centre is over a folded panel's row it is attached
// below that row (together with the rows hanging from `f`). Returns true if it was.
bool DropOnto(FenceWindow& f);

// Puts every stacked row where it belongs, under the top row of its stack. Called
// whenever a panel moves, resizes or folds; safe to call from inside such a change.
void Layout();

}  // namespace FenceStack
