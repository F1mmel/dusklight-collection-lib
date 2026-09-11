#include "collection_page.hpp"

#include "m_Do/m_Do_controller_pad.h"
#include "m_Do/m_Do_graphic.h"
#include "d/d_menu_window.h"     // dMw_LEFT/RIGHT_TRIGGER
#include "d/d_select_cursor.h"   // dSelect_cursor_c
#include "d/d_lib.h"             // STControl (mpStick - the analog cursor input)
#include "Z2AudioLib/Z2SeMgr.h"

// 0 = main page (item grid), 1 = second page (heart + Mirror of Twilight).
// s_anim eases toward it; the grid fades + slides left, heart + mirror slide in
// from the right. The Link doll stays on BOTH pages (never moved).
static int  s_target = 0;
static f32  s_anim = 0.0f;
static int  s_p2sel = 0;   // page-2 cursor: 0 = heart, 1 = mirror

static f32 smoothstep(f32 t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

static f32 page_slide_w() {
    f32 w = mDoGph_gInf_c::getWidthF();
    return (w > 100.0f && w < 4000.0f) ? w : 640.0f;
}

// Every pane that belongs to the item grid (containers - child icons/pics ride
// the parent alpha). Faded out as the grid slides so it doesn't smear over the
// Link doll on the way off-screen.
static void fade_grid(J2DScreen* s, u8 a) {
    static const u64 kGridTags[] = {
        MULTI_CHAR('ken_n0'),  MULTI_CHAR('ken_n1'),
        MULTI_CHAR('tate_n0'), MULTI_CHAR('tate_n1'),
        MULTI_CHAR('fuku_n0'), MULTI_CHAR('fuku_n1'), MULTI_CHAR('fuku_n2'),
        MULTI_CHAR('ken_g_0'),  MULTI_CHAR('ken_g_1'),
        MULTI_CHAR('tate_g_0'), MULTI_CHAR('tate_g_1'),
        MULTI_CHAR('fuku_g_0'), MULTI_CHAR('fuku_g_1'), MULTI_CHAR('fuku_g_2'),
        MULTI_CHAR('tunagi00'), MULTI_CHAR('tunagi01'), MULTI_CHAR('tunagi03'),
        MULTI_CHAR('tunagi04'), MULTI_CHAR('tunagi06'), MULTI_CHAR('tunagi07'),
        MULTI_CHAR('tunagi08'),
        MULTI_CHAR('tuna_k2'), MULTI_CHAR('tuna_t2'), MULTI_CHAR('tuna_f3'),
        MULTI_CHAR('ken_mid'), MULTI_CHAR('tate_mid'), MULTI_CHAR('fuku_ord'),
        MULTI_CHAR('ken_gm'),  MULTI_CHAR('tate_gm'),  MULTI_CHAR('fuku_go'),
        MULTI_CHAR('fuku_her'),
    };
    for (u64 tag : kGridTags) {
        if (J2DPane* p = s->search(tag)) p->setAlpha(a);
    }
    for (int i = 0; i < slot_count(); i++) {
        const SlotSpec* slot = slot_get(i);
        if (slot && slot->autoLayout.on) {
            if (slot->icon) slot->icon->setAlpha(a);
            if (slot->frame) slot->frame->setAlpha(a);
        }
    }
    for (int i = 0; i < s_customConnectorCount; i++) {
        if (s_customConnectors[i]) s_customConnectors[i]->setAlpha(a);
    }
}

void collection_page_reset() {
    s_target = 0;
    s_anim = 0.0f;
    s_p2sel = 0;
}

void collection_page_update() {
    if (!is_collection_menu_enabled()) {
        collection_page_reset();
        return;
    }
    const f32 tgt = static_cast<f32>(s_target);
    s_anim += (tgt - s_anim) * 0.30f;
    if (s_anim < 0.0004f) s_anim = 0.0f;
    if (s_anim > 0.9996f) s_anim = 1.0f;
}

bool collection_page_active() {
    return s_target != 0 || s_anim > 0.0f;
}

bool collection_page_p2_focused() {
    return s_target == 1 && s_p2sel >= 0;
}

void collection_page_handle_input(dMenu_Collect2D_c* collect2D) {
    if (!collect2D) return;

    // --- R / L page toggle ---
    const int prevPage = s_target;
    if (mDoCPd_c::getTrigR(PAD_1)) {
        s_target = 1;
    } else if (mDoCPd_c::getTrigL(PAD_1)) {
        s_target = 0;
    }
    if (s_target != prevPage) {
        if (s_target == 1) {
            s_p2sel = 0;   // arrive focused on the heart
        } else {
            s_p2sel = -1;
        }
        collect2D->setItemNameStringNull();
        Z2GetAudioMgr()->seStart(Z2SE_SY_MENU_CHANGE_WINDOW, NULL, 0, 0, 1.0f, 1.0f, -1.0f, -1.0f, 0);
    }

    // --- Page 2: heart / mirror navigation ---
    // ONLY while a heart/mirror cell is focused (s_p2sel >= 0). Once dropped into
    // the grid (s_p2sel < 0) we must NOT touch mpStick - STControl::check*Trigger
    // latches a repeat-delay, so reading it here would starve the vanilla
    // cursorMove() and make grid nav stutter. on_wait_proc_pre lets wait_proc run
    // in that state (collection_page_grid_nav_active()), and the "walked up out of
    // the collection items into the hidden grid -> back to heart" pop is handled
    // in collection_page_apply.
    if (s_target == 1 && s_anim > 0.5f && s_p2sel >= 0) {
        const int prevSel = s_p2sel;
        bool right = dMw_RIGHT_TRIGGER() != 0;
        bool left  = dMw_LEFT_TRIGGER() != 0;
        bool down  = dMw_DOWN_TRIGGER() != 0;
        if (collect2D->mpStick) {
            collect2D->mpStick->checkTrigger();
            if (collect2D->mpStick->checkRightTrigger()) right = true;
            if (collect2D->mpStick->checkLeftTrigger())  left  = true;
            if (collect2D->mpStick->checkDownTrigger())  down  = true;
        }

        // Page switching is L/R shoulder ONLY - LEFT/DOWN here drops the selection
        // into the grid slots below (never a page flip).
        auto drop_to_grid = [&]() {
            s_p2sel = -1;
            collect2D->mCursorX = 3;
            collect2D->mCursorY = 3;
            collect2D->cursorPosSet();
            collect2D->setItemNameString(3, 3);
            Z2GetAudioMgr()->seStart(Z2SE_SY_CURSOR_ITEM, NULL, 0, 0, 1.0f, 1.0f, -1.0f, -1.0f, 0);
        };

        if (s_p2sel == 0) {            // Heart focused
            if (right)               s_p2sel = 1;
            else if (left || down)  { drop_to_grid(); return; }
        } else {                       // Mirror focused
            if (left)                s_p2sel = 0;
            else if (down)          { drop_to_grid(); return; }
        }

        if (s_p2sel != prevSel) {
            Z2GetAudioMgr()->seStart(Z2SE_SY_CURSOR_ITEM, NULL, 0, 0, 1.0f, 1.0f, -1.0f, -1.0f, 0);
        }
    }
}

f32 collection_page_grid_dx() {
    return -smoothstep(s_anim) * page_slide_w();
}

void collection_page_apply(dMenu_Collect2D_c* collect2D) {
    if (!collect2D || !collect2D->mpScreen) return;
    J2DScreen* screen = collect2D->mpScreen;

    J2DPane* heart   = screen->search(MULTI_CHAR('heart_n'));
    J2DPane* kamen   = screen->search(MULTI_CHAR('kamen_n'));
    J2DPane* modelbg = screen->search(MULTI_CHAR('modelbgn'));

    const f32 p = smoothstep(s_anim);
    const f32 W = page_slide_w();
    const f32 slideIn = (1.0f - p) * W;      // p=0 -> off-screen right; p=1 -> at target
    const bool showP2 = p > 0.001f;

    // Grid fades to nothing over the first 60% of the slide.
    const f32 fadeT = smoothstep(s_anim < 0.6f ? s_anim / 0.6f : 1.0f);
    fade_grid(screen, static_cast<u8>(255.0f * (1.0f - fadeT)));

    // Heart + Mirror of Twilight live only on page 2. Off page 2 they sit off the
    // right edge (and the heart is hidden). Positions tuned on device.
    if (heart)   set_pane_pos(heart,     46.0f + slideIn, -36.0f);
    if (kamen)   set_pane_pos(kamen,   168.0f + slideIn, -36.0f);
    if (modelbg) set_pane_pos(modelbg,  155.0f + slideIn, -58.0f);
    if (heart) { if (showP2) heart->show(); else heart->hide(); }
    // The 3D mirror model tracks kamen_n's centre, so it follows the pane.

    if (showP2 && s_p2sel >= 0) {
        collect2D->setItemNameStringNull();

        // Draw the selection rect on the focused page-2 cell.
        if (collect2D->mpDrawCursor) {
            J2DPane* sel = (s_p2sel == 0) ? heart : kamen;
            if (sel) {
                collect2D->mpDrawCursor->setAlphaRate(1.0f);
                collect2D->mpDrawCursor->setPos(sel->getTranslateX(), sel->getTranslateY(), sel, false);
                collect2D->mpDrawCursor->setParam(1.0f, 1.0f, 0.1f, 0.7f, 0.7f);
            }
        }
    } else if (showP2 && s_p2sel < 0 && collect2D->mCursorY <= 2) {
        // In the grid section, vanilla cursorMove walked the cursor up out of the
        // collection items into the (hidden, slid-off) equipment rows -> pop back
        // up to the heart.
        s_p2sel = 0;
        collect2D->mCursorX = 6;
        collect2D->mCursorY = 0;
        Z2GetAudioMgr()->seStart(Z2SE_SY_CURSOR_ITEM, NULL, 0, 0, 1.0f, 1.0f, -1.0f, -1.0f, 0);
    }


    // The heart cell (6,0) isn't a vanilla-cursor target on the main page.
    if (!slot_at(6, 0)) {
        collect2D->field_0x22d[6][0] = 0;
        if (!showP2 && collect2D->mCursorX == 6 && collect2D->mCursorY == 0) {
            collect2D->mCursorX = 5;
        }
    }

    // The Link doll (linki_n) is deliberately left alone - it stays on both pages.
}

