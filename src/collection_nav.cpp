#include "collection_nav.hpp"
#include "collection_page.hpp"

#include "f_pc/f_pc_profile_lst.h"


HookAction on_get_item_tag_pre(ModContext*, void* args, void* ret, void*) {
    if (!is_collection_menu_enabled() || !args || !ret) return HOOK_CONTINUE;
    int i_tag1 = mods::arg<int>(args, 1);
    int i_tag2 = mods::arg<int>(args, 2);
    bool param_3 = mods::arg<bool>(args, 3);

    if (i_tag2 == 5 && !param_3) {
        *(u64*)ret = 0;
        return HOOK_SKIP_ORIGINAL;
    }

    // Mod-added slots resolve to the pane tag recorded in their SlotSpec - a
    // custom slot owns its cell COMPLETELY, including the vanilla starter
    // cells: when the starter gear is off, auto-fill places a custom sword at
    // (3,0), and its unlocked() gate (not the starter gate below) decides
    // whether the cell - and therefore its mouse hover - exists.
    if (const SlotSpec* slot = slot_at(i_tag1, i_tag2)) {
        *(u64*)ret = slot->iconTag;
        return HOOK_SKIP_ORIGINAL;
    }

    // Wooden-sword (3,0) and ordon-clothes (3,2) slots don't exist without the
    // starter-equip claim; the Ordon Shield (3,1) also goes unless "keep ordon
    // shield" holds it (greyed - still has a pane). Only reached for cells no
    // custom slot has claimed.
    if (i_tag1 == 3) {
        if ((i_tag2 == 0 || i_tag2 == 2) && !(cl_column_claimed(1, 1) || cl_column_claimed(3, 1))) {
            *(u64*)ret = 0;
            return HOOK_SKIP_ORIGINAL;
        }
        if (i_tag2 == 1 && !ordon_shield_slot_present()) {
            *(u64*)ret = 0;
            return HOOK_SKIP_ORIGINAL;
        }
    }

    // Any cell outside the visible 4x3 equip grid is a leftover vanilla hidden
    // pane (the game parks its own unused cells at x0..2 / y3..5). It has a
    // pane and a tag, so the mouse hover scan stops on it and shadows every
    // custom slot drawn further right. Report "no item" for them.
    if (i_tag2 >= 3 || i_tag1 < 3) {
        *(u64*)ret = 0;
        return HOOK_SKIP_ORIGINAL;
    }

    if (i_tag2 == 0) {
        if (i_tag1 == 3) { *(u64*)ret = MULTI_CHAR('ken_n0'); return HOOK_SKIP_ORIGINAL; }
        if (i_tag1 == 5) { *(u64*)ret = MULTI_CHAR('ken_n1'); return HOOK_SKIP_ORIGINAL; }
        if (i_tag1 == 6) { *(u64*)ret = MULTI_CHAR('heart_n'); return HOOK_SKIP_ORIGINAL; }
    } else if (i_tag2 == 1) {
        if (i_tag1 == 3) { *(u64*)ret = MULTI_CHAR('tate_n0'); return HOOK_SKIP_ORIGINAL; }
        if (i_tag1 == 5) { *(u64*)ret = MULTI_CHAR('tate_n1'); return HOOK_SKIP_ORIGINAL; }
        if (i_tag1 == 6) { *(u64*)ret = 0; return HOOK_SKIP_ORIGINAL; }
    } else if (i_tag2 == 2) {
        if (i_tag1 == 4) { *(u64*)ret = MULTI_CHAR('fuku_n0'); return HOOK_SKIP_ORIGINAL; }
        if (i_tag1 == 5) { *(u64*)ret = MULTI_CHAR('fuku_n1'); return HOOK_SKIP_ORIGINAL; }
        if (i_tag1 == 6) { *(u64*)ret = MULTI_CHAR('fuku_n2'); return HOOK_SKIP_ORIGINAL; }
    }
    return HOOK_CONTINUE;
}

J2DPane* get_target_pane(dMenu_Collect2D_c* collect2D, u8 x, u8 y) {
    if (!collect2D || !collect2D->mpScreen) return nullptr;
    if (const SlotSpec* s = slot_at(x, y)) {
        if (s->autoLayout.on && s->icon) return s->icon;
    }
    if (y == 0) {
        if (x == 3) return collect2D->mpScreen->search(MULTI_CHAR('ken_n0'));
        if (x == 4) return slot_icon(4, 0);
        if (x == 5) return collect2D->mpScreen->search(MULTI_CHAR('ken_n1'));
        if (x == 6) return collect2D->mpScreen->search(MULTI_CHAR('heart_n'));
    } else if (y == 1) {
        if (x == 3) return collect2D->mpScreen->search(MULTI_CHAR('tate_n0'));
        if (x == 4) return slot_icon(4, 1);
        if (x == 5) return collect2D->mpScreen->search(MULTI_CHAR('tate_n1'));
    } else if (y == 2) {
        if (x == 3) return slot_icon(3, 2);
        if (x == 4) return collect2D->mpScreen->search(MULTI_CHAR('fuku_n0'));
        if (x == 5) return collect2D->mpScreen->search(MULTI_CHAR('fuku_n1'));
        if (x == 6) return collect2D->mpScreen->search(MULTI_CHAR('fuku_n2'));
    }
    if (x < 7 && y < 6 && collect2D->mpSelPm[x][y]) {
        return collect2D->mpSelPm[x][y]->getPanePtr();
    }
    return nullptr;
}

HookAction on_cursor_pos_set_pre(ModContext*, void* args, void*, void*) {
    if (!is_collection_menu_enabled() || !args) return HOOK_CONTINUE;
    dMenu_Collect2D_c* collect2D = mods::arg<dMenu_Collect2D_c*>(args, 0);
    if (!collect2D || !collect2D->mpScreen || !collect2D->mpDrawCursor) return HOOK_CONTINUE;

    u8 curX = collect2D->mCursorX;
    u8 curY = collect2D->mCursorY;

    // Scale all panes
    for (u8 y = 0; y < 6; y++) {
        for (u8 x = 0; x < 7; x++) {
            J2DPane* pane = get_target_pane(collect2D, x, y);
            if (pane) {
                if ((x != 0 || y != 0) && (x != 6 || y != 0)) {
                    if (y == 5) {
                        if (x == curX && y == curY) {
                            pane->scale(g_drawHIO.mCollectScreen.mSelectSaveOptionScale,
                                       g_drawHIO.mCollectScreen.mSelectSaveOptionScale);
                        } else {
                            pane->scale(g_drawHIO.mCollectScreen.mUnselectSaveOptionScale,
                                       g_drawHIO.mCollectScreen.mUnselectSaveOptionScale);
                        }
                    } else if (x == curX && y == curY) {
                        pane->scale(g_drawHIO.mCollectScreen.mSelectItemScale,
                                   g_drawHIO.mCollectScreen.mSelectItemScale);
                    } else {
                        pane->scale(g_drawHIO.mCollectScreen.mUnselectItemScale,
                                   g_drawHIO.mCollectScreen.mUnselectItemScale);
                    }
                }
            }
        }
    }

    collect2D->mpDrawCursor->setAlphaRate(1.0f);

    J2DPane* curPane = get_target_pane(collect2D, curX, curY);
    if (curPane) {
        Vec pos;
        if (curX == 6 && curY == 0 && !slot_at(6, 0)) {
            J2DPane* heart_n = collect2D->mpScreen->search(MULTI_CHAR('heart_n'));
            pos.x = heart_n ? heart_n->getTranslateX() : curPane->getTranslateX();
            pos.y = heart_n ? heart_n->getTranslateY() : curPane->getTranslateY();
            pos.z = 0.0f;
        } else {
            pos.x = curPane->getTranslateX();
            pos.y = curPane->getTranslateY();
            pos.z = 0.0f;
        }
        collect2D->mpDrawCursor->setPos(pos.x, pos.y, curPane, false);
    }

    if (curY == 5) {
        collect2D->mpDrawCursor->setParam(1.1f, 0.85f, 0.05f, 0.5f, 0.5f);
    } else if (curX == 6 && curY == 0 && !slot_at(6, 0)) {
        collect2D->mpDrawCursor->setParam(0.6f, 0.85f, 0.03f, 0.6f, 0.6f);
    } else {
        collect2D->mpDrawCursor->setParam(1.0f, 1.0f, 0.1f, 0.7f, 0.7f);
    }

    return HOOK_SKIP_ORIGINAL;
}

HookAction on_cursor_move_pre(ModContext*, void* args, void*, void*) {
    if (!is_collection_menu_enabled() || !args) return HOOK_CONTINUE;
    dMenu_Collect2D_c* collect2D = mods::arg<dMenu_Collect2D_c*>(args, 0);
    if (!collect2D || !collect2D->mpScreen || !collect2D->mpStick) return HOOK_CONTINUE;

    u8 curX = collect2D->mCursorX;
    u8 curY = collect2D->mCursorY;

    const SlotSpec* curSlot = slot_at(curX, curY);
    bool inEquipGrid = (curX >= 3 && curX <= 6 && curY <= 2) || (curX == 6 && curY == 0) || (curSlot != nullptr && curSlot->autoLayout.on);
    if (inEquipGrid) {
        collect2D->mpStick->checkTrigger();

        u8 targetX = curX;
        u8 targetY = curY;
        bool moved = false;

        // One trigger read per direction (checkDown/UpTrigger mutate repeat-timer
        // state). dir: 0=left 1=right 2=up 3=down, -1=nothing.
        int dir = -1;
        if (collect2D->mpStick->checkRightTrigger()) {
            dir = 1;
        } else if (collect2D->mpStick->checkLeftTrigger()) {
            dir = 0;
        } else {
            int v = collect2D->mpStick->checkDownTrigger()
                        ? 1
                        : (collect2D->mpStick->checkUpTrigger() ? -1 : 0);
            if (v == 1) dir = 3;
            else if (v == -1) dir = 2;
        }

        // Auto-managed mod slots declare their own neighbours in the SlotSpec.
        SlotCell nav = (dir >= 0) ? slot_nav_target(curX, curY, dir) : SlotCell{};

        if (dir < 0) {
            // nothing
        } else if (slot_cell_set(nav)) {
            targetX = nav.x;
            targetY = nav.y;
            moved = true;
        } else if (dir == 1) {   // right
            for (int tx = curX + 1; tx <= 6; tx++) {
                if (collect2D->field_0x22d[tx][curY] != 0) {
                    targetX = tx;
                    moved = true;
                    break;
                }
            }
        } else if (dir == 0) {   // left
            for (int tx = curX - 1; tx >= 3; tx--) {
                if (collect2D->field_0x22d[tx][curY] != 0) {
                    targetX = tx;
                    moved = true;
                    break;
                }
            }
            if (!moved) {
                targetX = 2;
                targetY = (curY == 0) ? 3 : 4;
                moved = true;
            }
        } else {                 // up (2) / down (3)
            // Pick the slot in the next row that's visually closest, so vertical
            // nav still lines up when a row is shifted (starter-equip slot off).
            const bool goingDown = (dir == 3);
            J2DPane* fromPane = get_target_pane(collect2D, curX, curY);
            f32 fromX = fromPane ? fromPane->getTranslateX() : 0.0f;

            int tyStart = goingDown ? (int)curY + 1 : (int)curY - 1;
            int tyEnd   = goingDown ? 2 : 0;
            int tyStep  = goingDown ? 1 : -1;

            for (int ty = tyStart; goingDown ? (ty <= tyEnd) : (ty >= tyEnd); ty += tyStep) {
                int bestX = -1;
                f32 bestDist = 1.0e9f;
                for (int tx = 3; tx <= 6; tx++) {
                    if (collect2D->field_0x22d[tx][ty] != 0) {
                        J2DPane* p = get_target_pane(collect2D, tx, ty);
                        f32 px = p ? p->getTranslateX() : (f32)tx;
                        f32 d = px - fromX;
                        if (d < 0.0f) d = -d;
                        if (d < bestDist) {
                            bestDist = d;
                            bestX = tx;
                        }
                    }
                }
                if (bestX != -1) {
                    targetX = (u8)bestX;
                    targetY = (u8)ty;
                    moved = true;
                    break;
                }
            }
            if (!moved && goingDown) {
                targetX = 3;
                targetY = 3;
                moved = true;
            }
        }

        if (moved) {
            collect2D->mCursorX = targetX;
            collect2D->mCursorY = targetY;
            Z2GetAudioMgr()->seStart(Z2SE_SY_CURSOR_ITEM, NULL, 0, 0, 1.0f, 1.0f, -1.0f, -1.0f, 0);
            collect2D->cursorPosSet();
            collect2D->setItemNameString(collect2D->mCursorX, collect2D->mCursorY);
            return HOOK_SKIP_ORIGINAL;
        }
    }
    return HOOK_CONTINUE;
}

// The mouse-hover scan (dMenu_Collect2D_c::pointerWait) walks cells in row-major
// order and stops at the FIRST whose pane hit-box (bounds + 8px) contains the
// cursor. The heart container (cell 6,0) has a huge .blo bounds and the mod
// repositions it right over the clothes-row's right side, so it shadowed Hylian
// Shield (5,1), Magic Armor (6,2) and Ordon Hero (6,1). Shrink its hit-box to
// one cell for the duration of the scan, then put it back.
static JGeometry::TBox2<f32> s_heartBoundsSave;
static bool s_heartBoundsSaved = false;

struct SlotBoundsSave {
    J2DPane* pane;
    JGeometry::TBox2<f32> bounds;
};
static SlotBoundsSave s_modSlotBoundsSave[12];
static int s_modSlotBoundsSaveCount = 0;

static J2DPane* pw_heart(void* args) {
    dMenu_Collect2D_c* c = args ? mods::arg<dMenu_Collect2D_c*>(args, 0) : nullptr;
    return (c && c->mpScreen) ? c->mpScreen->search(MULTI_CHAR('heart_n')) : nullptr;
}

HookAction on_pointer_wait_pre(ModContext*, void* args, void*, void*) {
    s_heartBoundsSaved = false;
    s_modSlotBoundsSaveCount = 0;
    if (!is_collection_menu_enabled()) return HOOK_CONTINUE;

    dMenu_Collect2D_c* collect2D = args ? mods::arg<dMenu_Collect2D_c*>(args, 0) : nullptr;
    if (collect2D) {
        static int s_logCounter = 0;
        if (s_logCounter++ < 3) {
            auto log_pm = [&](const char* label, CPaneMgr* pm) {
                if (!pm) {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "[PM_LOG] %s: pm is null", label);
                    g_logSvc->info(g_modCtx, buf);
                    return;
                }
                J2DPane* p = pm->getPanePtr();
                if (!p) {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "[PM_LOG] %s: pane is null", label);
                    g_logSvc->info(g_modCtx, buf);
                    return;
                }
                Mtx mtx;
                Vec v0 = pm->getGlobalVtx(&mtx, 0, false, 0);
                Vec v3 = pm->getGlobalVtx(&mtx, 3, false, 0);
                char buf[256];
                snprintf(buf, sizeof(buf),
                    "[PM_LOG] %s: trans=(%.1f,%.1f) bounds=(%.1f,%.1f)-(%.1f,%.1f) v0=(%.1f,%.1f) v3=(%.1f,%.1f)",
                    label, p->getTranslateX(), p->getTranslateY(),
                    p->mBounds.i.x, p->mBounds.i.y, p->mBounds.f.x, p->mBounds.f.y,
                    v0.x, v0.y, v3.x, v3.y);
                g_logSvc->info(g_modCtx, buf);
            };
            log_pm("Kokiri (4,2)", collect2D->mpSelPm[4][2]);
            log_pm("MagicArmor (6,2)", collect2D->mpSelPm[6][2]);
            log_pm("Reinforced (6,1)", collect2D->mpSelPm[6][1]);
            log_pm("DEMOdd (2,1)", collect2D->mpSelPm[2][1]);
            log_pm("OrdonHero (2,2)", collect2D->mpSelPm[2][2]);
        }
        bool isP2 = collection_page_active();

        for (int i = 0; i < slot_count(); i++) {
            const SlotSpec* s = slot_get(i);
            if (!s || !s->autoLayout.on || !s->icon) continue;

            if (s_modSlotBoundsSaveCount < 12) {
                s_modSlotBoundsSave[s_modSlotBoundsSaveCount++] = { s->icon, s->icon->mBounds };
            }

            if (!isP2 && is_collect_item_unlocked(s->x, s->y)) {
                f32 px = s->icon->getTranslateX();
                f32 py = s->icon->getTranslateY();
                s->icon->mBounds.set(px - 22.5f, py - 22.5f, px + 22.5f, py + 22.5f);
            } else {
                s->icon->mBounds.set(-99999.0f, -99999.0f, -99990.0f, -99990.0f);
            }
        }


    }

    if (J2DPane* heart = pw_heart(args)) {
        s_heartBoundsSave = heart->mBounds;
        s_heartBoundsSaved = true;
        if (!collection_page_active()) {
            heart->mBounds.set(-99999.0f, -99999.0f, -99990.0f, -99990.0f);
        } else {
            heart->mBounds.set(-24.0f, -28.0f, 24.0f, 28.0f);
        }
    }

    return HOOK_CONTINUE;
}

void on_pointer_wait_post(ModContext*, void* args, void*, void*) {
    // Diagnostics: log the landed cell once per CHANGE, with the resolved slot.
    static u8 s_lastX = 0xFF, s_lastY = 0xFF;
    dMenu_Collect2D_c* c2d = args ? mods::arg<dMenu_Collect2D_c*>(args, 0) : nullptr;
    if (c2d && (c2d->mCursorX != s_lastX || c2d->mCursorY != s_lastY)) {
        s_lastX = c2d->mCursorX;
        s_lastY = c2d->mCursorY;
        const SlotSpec* slot = slot_at(c2d->mCursorX, c2d->mCursorY);
        char buf[192];
        if (slot && slot->name.str) {
            std::snprintf(buf, sizeof(buf), "[CollectionLib] hover -> (%d,%d) '%s'",
                          (int)c2d->mCursorX, (int)c2d->mCursorY, slot->name.str);
        } else if (slot) {
            std::snprintf(buf, sizeof(buf), "[CollectionLib] hover -> (%d,%d) msgId %u",
                          (int)c2d->mCursorX, (int)c2d->mCursorY,
                          (unsigned)slot->name.msgID);
        } else {
            std::snprintf(buf, sizeof(buf), "[CollectionLib] hover -> (%d,%d) <no slot spec>",
                          (int)c2d->mCursorX, (int)c2d->mCursorY);
        }
        g_logSvc->info(g_modCtx, buf);
    }
    for (int i = 0; i < s_modSlotBoundsSaveCount; i++) {
        if (s_modSlotBoundsSave[i].pane) {
            s_modSlotBoundsSave[i].pane->mBounds = s_modSlotBoundsSave[i].bounds;
        }
    }
    s_modSlotBoundsSaveCount = 0;

    if (!s_heartBoundsSaved) return;
    if (J2DPane* heart = pw_heart(args)) {
        heart->mBounds = s_heartBoundsSave;
    }
    s_heartBoundsSaved = false;
}


HookAction on_set_item_name_string_pre(ModContext*, void* args, void*, void*) {
    if (!is_collection_menu_enabled() || !args) return HOOK_CONTINUE;
    dMenu_Collect2D_c* collect2D = mods::arg<dMenu_Collect2D_c*>(args, 0);
    u8 x = mods::arg<u8>(args, 1);
    u8 y = mods::arg<u8>(args, 2);
    if (!collect2D || !collect2D->mpScreen) return HOOK_CONTINUE;

    const SlotSpec* slot = slot_at(x, y);
    if ((x >= 3 && x <= 6 && y <= 2) || (slot != nullptr && slot->autoLayout.on)) {
        if (!is_collect_item_unlocked(x, y)) {
            collect2D->setItemNameStringNull();
            return HOOK_SKIP_ORIGINAL;
        }

        if (slot) {
            collect2D->field_0x180 = slot_name_id(slot);
            collect2D->mItemNameString = slot_desc_id(slot);
        } else if (y == 0) {
            if (x == 3) {
                collect2D->field_0x180 = 0x1a4; // Wooden Sword
                collect2D->mItemNameString = 0x2a4;
            } else if (x == 5) {
                collect2D->field_0x180 = dComIfGs_isItemFirstBit(dItemNo_LIGHT_SWORD_e) ? 0x1ae : 0x18e; // Master Sword
                collect2D->mItemNameString = collect2D->field_0x180 + 0x100;
            } else if (x == 6) {
                collect2D->field_0x180 = 0x186; // Heart Container
                collect2D->mItemNameString = 0x286;
            }
        } else if (y == 1) {
            if (x == 3) {
                collect2D->field_0x180 = 0x18f; // Wooden Shield
                collect2D->mItemNameString = 0x28f;
            } else if (x == 5) {
                collect2D->field_0x180 = 0x191; // Hylian Shield
                collect2D->mItemNameString = 0x291;
            }
        } else if (y == 2) {
            if (x == 4) {
                collect2D->field_0x180 = 0x194; // Kokiri Clothes
                collect2D->mItemNameString = 0x294;
            } else if (x == 5) {
                collect2D->field_0x180 = 0x196; // Zora Armor
                collect2D->mItemNameString = 0x296;
            } else if (x == 6) {
                collect2D->field_0x180 = 0x195; // Magic Armor
                collect2D->mItemNameString = 0x295;
            }
        }

        collect2D->field_0x184[x][y] = collect2D->field_0x180;
        collect2D->field_0x1d8[x][y] = collect2D->mItemNameString;
        return HOOK_CONTINUE;
    }
    return HOOK_CONTINUE;
}

HookAction on_get_string_kanji_pre(ModContext*, void* args, void*, void*) {
    if (!is_collection_menu_enabled() || !args) return HOOK_CONTINUE;
    u32 msgID = mods::arg<u32>(args, 1);
    if (const SlotSpec* slot = slot_by_msgid(msgID)) {
        if (msgID == slot_name_id(slot) && slot->name.str) {
            TEXT_SPAN o_str = mods::arg<TEXT_SPAN>(args, 2);
            if (o_str) {
                SAFE_STRCPY(o_str, slot->name.str);
            }
            return HOOK_SKIP_ORIGINAL;
        }
    }
    if (msgID == 0x437) {
        TEXT_SPAN o_str = mods::arg<TEXT_SPAN>(args, 2);
        if (o_str) {
            SAFE_STRCPY(o_str, "Unequip");
        }
        return HOOK_SKIP_ORIGINAL;
    }
    return HOOK_CONTINUE;
}

static HookAction write_slot_description(void* args, void* ret, const char* text) {
    dMsgStringBase_c* msgStr = mods::arg<dMsgStringBase_c*>(args, 0);
    J2DTextBox* boxes[2] = { mods::arg<J2DTextBox*>(args, 2), mods::arg<J2DTextBox*>(args, 3) };
    COutFont_c* outFont = mods::arg<COutFont_c*>(args, 5);

    for (J2DTextBox* tb : boxes) {
        if (!tb) continue;
        if (msgStr) msgStr->resetStringLocal(tb);
        if (outFont) outFont->reset(tb);
        if (tb->getStringPtr()) {
            SAFE_STRCPY(tb->getStringPtr(), text);
        }
    }
    if (ret) *(f32*)ret = 0.0f;
    return HOOK_SKIP_ORIGINAL;
}

HookAction on_get_string_local_pre(ModContext*, void* args, void* ret, void*) {
    if (!is_collection_menu_enabled() || !args) return HOOK_CONTINUE;
    u32 msgID = mods::arg<u32>(args, 1);
    if (const SlotSpec* slot = slot_by_msgid(msgID)) {
        if (msgID == slot_desc_id(slot) && slot->description.str) {
            return write_slot_description(args, ret, slot->description.str);
        }
    }
    return HOOK_CONTINUE;
}


