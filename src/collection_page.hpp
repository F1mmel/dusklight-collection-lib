#pragma once

#include "collection_lib/collection_common.hpp"

// Second collection page: press R to slide from the item grid to a page that
// shows only the Heart-Piece container and the Mirror of Twilight (the "fused
// shadow" model), press L to slide back. Moving both off the main page frees up
// its right side.
//
// The animation is a horizontal two-layer slide driven by `s_anim` (0 = main
// page, 1 = second page). Layer A = the grid + connectors + Link doll (slides
// left). Layer B = heart + mirror + model background (slides in from the right).

void collection_page_reset();                         // clear state (menu open/close)
void collection_page_update();                        // advance the animation, once per frame
void collection_page_handle_input(dMenu_Collect2D_c*);// R / L page toggle (from wait_proc pre)
bool collection_page_active();                        // true while not fully on the main page
bool collection_page_p2_focused();                    // true if on P2 and focusing Heart/Mirror
f32  collection_page_grid_dx();                       // X offset to add to the grid's baseX
void collection_page_apply(dMenu_Collect2D_c*);       // reposition heart / mirror / doll

