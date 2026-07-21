#pragma once

#include "domain/eham_state.h"

namespace ui {

/** Draw the static EHAM runway status screen from `state`. No live fetch. */
void ehamDisplayDraw(const EhamOperationalState& state);

}  // namespace ui
