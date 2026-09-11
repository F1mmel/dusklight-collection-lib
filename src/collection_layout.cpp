#include "collection_layout.hpp"
#include "collection_page.hpp"
#include "collection_lib/custom_equip.hpp"
#include "Z2AudioLib/Z2SeMgr.h"


static void addSlot(J2DScreen* screen, const SlotSpec& spec);

// Thin wrappers over addSlot that lock the row: sword / shield / clothes. The
// SlotSpec's leading `at` is written as `{}` and overwritten here from `item`.
static void addSlotRow(J2DScreen* screen, u8 row, u8 item, SlotSpec s) {
    s.at = { row, item };
    addSlot(screen, s);
}
static void addSwordItem(J2DScreen* s, u8 item, SlotSpec spec)  { addSlotRow(s, 1, item, spec); }
static void addShieldItem(J2DScreen* s, u8 item, SlotSpec spec) { addSlotRow(s, 2, item, spec); }
static void addTunicItem(J2DScreen* s, u8 item, SlotSpec spec)  { addSlotRow(s, 3, item, spec); }

// Build the mod-added collection-grid slot described by `s`: an icon container
// pane, its icon picture child, and a frame/highlight picture, each cloned from
// that row's vanilla template panes so it inherits the right size /
// base-position / colours. The three panes are (re-)resolved against the live
// screen first and only (re)built when missing, so this is safe to call on every
// screen rebuild. The spec is also recorded in the slot registry so the rest of
// the menu code can look the slot up by cell or message id.
static void addSlot(J2DScreen* screen, const SlotSpec& spec) {
    if (!screen) return;

    // Resolve the human {row, item} to a real grid cell.
    SlotSpec s = spec;
    SlotCell cell = grid_cell(spec.at.row, spec.at.item);
    s.x = cell.x;
    s.y = cell.y;

    // The intended row (1..3) selects the vanilla template panes: the
    // leftmost-slot position pane (*_n0), the icon texture, and the frame (*_g_0).
    u8 tmplRow = (spec.at.row >= 1 && spec.at.row <= 3) ? static_cast<u8>(spec.at.row - 1) : 2;
    u64 posSrcTag, texSrcTag, frameSrcTag;
    switch (tmplRow) {
    case 0:  posSrcTag = MULTI_CHAR('ken_n0');  texSrcTag = MULTI_CHAR('ken_01');  frameSrcTag = MULTI_CHAR('ken_g_0');  break;
    case 1:  posSrcTag = MULTI_CHAR('tate_n0'); texSrcTag = MULTI_CHAR('tate_00'); frameSrcTag = MULTI_CHAR('tate_g_0'); break;
    default: posSrcTag = MULTI_CHAR('fuku_n0'); texSrcTag = MULTI_CHAR('fuku_00'); frameSrcTag = MULTI_CHAR('fuku_g_0'); break;
    }

    // Re-resolve the slot's panes against the current screen (they're recreated
    // per screen); anything still missing is built below.
    J2DPane*    cont = screen->search(s.iconTag);
    J2DPicture* pic  = static_cast<J2DPicture*>(screen->search(s.iconPicTag));
    J2DPicture* frm  = static_cast<J2DPicture*>(screen->search(s.frameTag));

    if (s.enabled) {
        J2DPane* posSrc   = screen->search(posSrcTag);
        J2DPane* texSrc   = screen->search(texSrcTag);
        J2DPane* frameSrc = screen->search(frameSrcTag);

        // Frame picture (attached under the frames parent).
        if (!frm && frameSrc && frameSrc->getParentPane()) {
            const ResTIMG* frameTex = safe_get_tex_info(frameSrc);
            if (frameTex) {
                frm = JKR_NEW J2DPicture(s.frameTag, frameSrc->mBounds, frameTex, nullptr);
                frm->mKind = 'PIC1';
                frm->setBasePosition((J2DBasePosition)frameSrc->mBasePosition);
                frm->mBounds.set(-23.5f, -23.5f, 23.5f, 23.5f);
                J2DPicture* fp = static_cast<J2DPicture*>(frameSrc);
                frm->setCornerColor(fp->corner(0), fp->corner(1), fp->corner(2), fp->corner(3));
                frm->setBlackWhite(fp->getBlack(), fp->getWhite());
                frameSrc->getParentPane()->appendChild(frm);
            }
        }

        // Icon container pane + its icon picture child.
        if (!cont && posSrc && posSrc->getParentPane()) {
            cont = JKR_NEW J2DPane(posSrc->getParentPane(), true, s.iconTag, posSrc->mBounds);
            cont->setBasePosition((J2DBasePosition)posSrc->mBasePosition);
            cont->mBounds.set(-22.5f, -22.5f, 22.5f, 22.5f);

            const ResTIMG* iconTex = s.texOverride ? s.texOverride : safe_get_tex_info(texSrc);
            if (iconTex) {
                pic = JKR_NEW J2DPicture(
                    s.iconPicTag, texSrc ? texSrc->mBounds : posSrc->mBounds, iconTex, nullptr);
                pic->mKind = 'PIC1';
                if (texSrc) pic->setBasePosition((J2DBasePosition)texSrc->mBasePosition);
                pic->mBounds.set(-22.5f, -22.5f, 22.5f, 22.5f);
                pic->setBlackWhite(JUtility::TColor(0, 0, 0, 0), JUtility::TColor(255, 255, 255, 255));
                pic->setAlpha(255);
                cont->appendChild(pic);
                pic->translate(0.0f, 0.0f);
            }
        }
    }

    // Publish the panes: into the registry (always) and the optional external
    // globals (the vanilla mid-slots read those from their bespoke code).
    s.icon = cont;
    s.iconPic = pic;
    s.frame = frm;
    if (s.outIcon)    *s.outIcon    = cont;
    if (s.outIconPic) *s.outIconPic = pic;
    if (s.outFrame)   *s.outFrame   = frm;

    if (s.enabled) slot_registry_add(s);
}

bool cl_item_exists(u8 row, u8 item) {
    // 1. Column-1 cells exist when a consumer claims them (vanilla-wired slot
    //    - e.g. the wooden sword / ordon shield / ordon clothes). NOTE: this
    //    does NOT short-circuit - a custom slot registered at column 1 must
    //    also count, otherwise auto-fill sees the column as free and stacks
    //    every item onto it.
    if (item == 1 && cl_column_claimed(row, 1)) return true;

    // 2. Vanilla items
    if (row == 1) {
        if (item >= 2 && item <= 3) return true; // Ordon Sword, Master Sword
    } else if (row == 2) {
        if (item >= 2 && item <= 3) return true; // Wooden Shield, Hylian Shield
    } else if (row == 3) {
        if (item >= 2 && item <= 4) return true; // Kokiri, Zora, Magic Armor
    }

    // 2. Custom mod items (purely dynamic based on custom_equip registry)
    for (int i = 0; i < custom_equip_count(); i++) {
        const CustomEquipDef* d = custom_equip_get(i);
        if (d != nullptr) {
            u8 r = (d->kind == CE_SWORD) ? 1 : (d->kind == CE_SHIELD) ? 2 : 3;
            if (r == row && d->item == item) return true;
        }
    }
    return false;
}

static u8 find_closest_item(u8 targetRow, u8 idealItem) {
    u8 bestItem = 1;
    int bestDist = 999;

    // Dynamically calculate the highest item column present in the target row
    u8 maxCol = (targetRow == 3) ? 4 : 3;
    for (int i = 0; i < custom_equip_count(); i++) {
        const CustomEquipDef* d = custom_equip_get(i);
        if (d != nullptr) {
            u8 r = (d->kind == CE_SWORD) ? 1 : (d->kind == CE_SHIELD) ? 2 : 3;
            if (r == targetRow && d->item > maxCol) {
                maxCol = d->item;
            }
        }
    }

    for (u8 it = 1; it <= maxCol; it++) {
        if (cl_item_exists(targetRow, it)) {
            int d = (it > idealItem) ? (it - idealItem) : (idealItem - it);
            if (d < bestDist) {
                bestDist = d;
                bestItem = it;
            }
        }
    }
    return bestItem;
}

// Rebuild the custom-equip registry from the shield/ sword/ tunic/ data tables
// and add one generic grid slot per entry. All behaviour (icon, equip, model
// swap, highlight, nav) is custom_equip.cpp - nothing per-item here.
static void add_custom_equip_slots(J2DScreen* screen) {
    custom_equip_reset_registry();
    collectionlib_run_slot_registration();

    for (int id = 0; id < custom_equip_count(); id++) {
        const CustomEquipDef* d = custom_equip_get(id);
        const u8 row  = d->kind == CE_SWORD ? 1 : d->kind == CE_SHIELD ? 2 : 3;
        const f32 rowY = row == 1 ? s_ken_n0_origY : row == 2 ? s_tate_n0_origY : s_fuku_n0_origY;

        SlotSpec s{};
        s.enabled     = true;
        s.iconTag     = custom_equip_icon_tag(id);
        s.iconPicTag  = custom_equip_pic_tag(id);
        s.frameTag    = custom_equip_frame_tag(id);
        s.texOverride = custom_equip_icon(id);
        s.name        = d->name;
        s.description = d->description;
        s.onEquip     = &custom_equip_on_equip;
        s.unlockFn    = &custom_equip_is_unlocked;
        s.equippedFn  = &custom_equip_is_equipped;
        s.autoLayout.on   = true;
        s.autoLayout.posX = collection_slot_x(static_cast<f32>(d->item - 1));
        s.autoLayout.posY = rowY;
        if (d->item > 1) s.autoLayout.navLeft = { row, static_cast<u8>(d->item - 1) };
        if (row > 1) {
            u8 upItem = find_closest_item(row - 1, d->item);
            s.autoLayout.navUp = { static_cast<u8>(row - 1), upItem };
        }
        if (row < 3) {
            u8 downItem = find_closest_item(row + 1, d->item);
            s.autoLayout.navDown = { static_cast<u8>(row + 1), downItem };
        }

        if (row == 1)      addSwordItem(screen, d->item, s);
        else if (row == 2) addShieldItem(screen, d->item, s);
        else               addTunicItem(screen, d->item, s);
    }
}

void update_screen_bases(J2DScreen* screen, JKRExpHeap* heap) {
    if (!screen) return;

    if (screen != s_cachedScreen) {
        s_cachedScreen = screen;
        s_picTunagiKen2 = static_cast<J2DPicture*>(screen->search(MULTI_CHAR('tuna_k2')));
        s_picTunagiTate2 = static_cast<J2DPicture*>(screen->search(MULTI_CHAR('tuna_t2')));
        s_picTunagiFuku3 = static_cast<J2DPicture*>(screen->search(MULTI_CHAR('tuna_f3')));

        J2DPane* tunagi01 = screen->search(MULTI_CHAR('tunagi01'));
        J2DPane* tunagi03 = screen->search(MULTI_CHAR('tunagi03'));
        J2DPane* tunagi06 = screen->search(MULTI_CHAR('tunagi06'));

        // Clear custom connector pool (screen changed)
        for (int ci = 0; ci < 6; ci++) {
            s_customConnectors[ci] = nullptr;
        }
        s_customConnectorCount = 0;
        s_customConnectorParent[0] = tunagi01 ? tunagi01->getParentPane() : nullptr;
        s_customConnectorParent[1] = tunagi03 ? tunagi03->getParentPane() : nullptr;
        s_customConnectorParent[2] = tunagi06 ? tunagi06->getParentPane() : nullptr;
        s_customConnectorTemplate[0] = tunagi01 ? static_cast<J2DPicture*>(tunagi01) : nullptr;
        s_customConnectorTemplate[1] = tunagi03 ? static_cast<J2DPicture*>(tunagi03) : nullptr;
        s_customConnectorTemplate[2] = tunagi06 ? static_cast<J2DPicture*>(tunagi06) : nullptr;

        JKRHeap* oldHeap = nullptr;
        if (heap != nullptr) {
            oldHeap = mDoExt_setCurrentHeap(heap);
        }

        // The mod's slots. addSwordItem/addShieldItem/addTunicItem fix the row;
        // the second arg is the 1-based item (left-to-right). addSlot maps that to
        // the real grid cell and records the spec in the registry (read via
        // slot_icon/frame/..., the nav loops, the string hooks). Name+description
        // is a game message id OR an inline literal. autoLayout freely places a
        // slot parked on a hidden cell; unlockFn/equippedFn own its show/ring.
        slot_registry_clear();

        addSwordItem(screen, 2, { {}, true,   // Ordon Sword
            MULTI_CHAR('ken_mid'), MULTI_CHAR('ken_im'), MULTI_CHAR('ken_gm'),
            nullptr, 0x18d, 0x28d, nullptr });

        addShieldItem(screen, 2, { {}, true,   // Wooden Shield
            MULTI_CHAR('tate_mid'), MULTI_CHAR('tate_im'), MULTI_CHAR('tate_gm'),
            nullptr, 0x190, 0x290, nullptr });

        // Consumer-claimed vanilla slots (e.g. ordon clothes at row-3 column 1).
        for (int vi = 0; vi < cl_vanilla_slot_count(); ++vi) {
            const CollectionVanillaSlotDef* vd = cl_vanilla_slot_get(vi);
            const u8 vrow = cl_vanilla_slot_row(vi);
            const u8 vitem = cl_vanilla_slot_item(vi);
            const SlotCell vc = grid_cell(vrow, vitem);
            // Cells with an existing vanilla pane (rows 1-2, column 1) are only
            // CLAIMED - the game's own pane handles visibility. Only pane-less
            // cells get panes built here.
            if (vrow == 3 && vitem == 1) {
                SlotSpec vs{};
                vs.enabled = true;
                vs.iconTag = MULTI_CHAR('fuku_ord');
                vs.iconPicTag = MULTI_CHAR('fuku_io');
                vs.frameTag = MULTI_CHAR('fuku_go');
                vs.texOverride = vd->icon;
                if (vd->name != nullptr) vs.name = SlotText(vd->name);
                else vs.name = SlotText(static_cast<u32>(vd->nameMsgId));
                if (vd->description != nullptr) vs.description = SlotText(vd->description);
                else vs.description = SlotText(static_cast<u32>(vd->descMsgId));
                vs.unlockFn = &cl_vanilla_slot_unlocked;
                vs.equippedFn = &cl_vanilla_slot_equipped;
                vs.autoLayout.on = true;
                vs.autoLayout.posX = collection_slot_x(0.0f);
                vs.autoLayout.posY = s_fuku_n0_origY;
                addTunicItem(screen, 1, vs);
            }
        }

        // All custom swords / shields / tunics from the shield/ sword/ tunic/ data
        // tables - one generic slot each, fully handled by custom_equip.cpp.
        add_custom_equip_slots(screen);

        // 4. Sword Connector 2 (Slot 2 to Slot 3)
        if (!s_picTunagiKen2 && tunagi01 && tunagi01->getParentPane()) {
            const ResTIMG* tex = safe_get_tex_info(tunagi01);
            if (tex) {
                s_picTunagiKen2 = JKR_NEW J2DPicture(MULTI_CHAR('tuna_k2'), tunagi01->mBounds, tex, nullptr);
                s_picTunagiKen2->setBasePosition((J2DBasePosition)tunagi01->mBasePosition);
                s_picTunagiKen2->mBounds.set(-6.0f, -18.0f, 6.0f, 18.0f);
                static_cast<CustomPicture*>(s_picTunagiKen2)->copyVisualsFrom(static_cast<J2DPicture*>(tunagi01));
                tunagi01->getParentPane()->appendChild(s_picTunagiKen2);
            }
        }

        // 5. Shield Connector 2 (Slot 2 to Slot 3)
        if (!s_picTunagiTate2 && tunagi03 && tunagi03->getParentPane()) {
            const ResTIMG* tex = safe_get_tex_info(tunagi03);
            if (tex) {
                s_picTunagiTate2 = JKR_NEW J2DPicture(MULTI_CHAR('tuna_t2'), tunagi03->mBounds, tex, nullptr);
                s_picTunagiTate2->setBasePosition((J2DBasePosition)tunagi03->mBasePosition);
                s_picTunagiTate2->mBounds.set(-6.0f, -18.0f, 6.0f, 18.0f);
                static_cast<CustomPicture*>(s_picTunagiTate2)->copyVisualsFrom(static_cast<J2DPicture*>(tunagi03));
                tunagi03->getParentPane()->appendChild(s_picTunagiTate2);
            }
        }

        // 6. Clothes Connector 3 (Slot 3 to Slot 4)
        if (!s_picTunagiFuku3 && tunagi06 && tunagi06->getParentPane()) {
            const ResTIMG* tex = safe_get_tex_info(tunagi06);
            if (tex) {
                s_picTunagiFuku3 = JKR_NEW J2DPicture(MULTI_CHAR('tuna_f3'), tunagi06->mBounds, tex, nullptr);
                s_picTunagiFuku3->setBasePosition((J2DBasePosition)tunagi06->mBasePosition);
                s_picTunagiFuku3->mBounds.set(-6.0f, -18.0f, 6.0f, 18.0f);
                static_cast<CustomPicture*>(s_picTunagiFuku3)->copyVisualsFrom(static_cast<J2DPicture*>(tunagi06));
                tunagi06->getParentPane()->appendChild(s_picTunagiFuku3);
            }
        }

        if (oldHeap != nullptr) {
            mDoExt_setCurrentHeap(oldHeap);
        }
    }
}

// Position / size / show / scale every auto-managed mod slot straight from its
// SlotSpec - no per-slot code. (The three vanilla mid-slots are not autoLayout
// and keep their bespoke handling below.)
static void layout_managed_slots(dMenu_Collect2D_c* collect2D) {
    J2DPane* ref = collect2D->mpScreen ? collect2D->mpScreen->search(MULTI_CHAR('fuku_n0')) : nullptr;
    f32 gridDx = collection_page_grid_dx();

    for (int i = 0; i < slot_count(); i++) {
        const SlotSpec* s = slot_get(i);
        if (!s->autoLayout.on) continue;

        J2DPane*    cont = s->icon;
        J2DPicture* pic  = s->iconPic;
        J2DPicture* frm  = s->frame;
// Slide with the grid AND with the row shift: when the row's first column is
// unclaimed the vanilla slots move one column left - the custom slots follow.
        const f32 rowShift = cl_column_occupied(static_cast<u8>(s->y + 1), 1) ? 0.0f : -(s_col_dx + 5.0f);
        const f32 px = s->autoLayout.posX + gridDx + rowShift;   // slide with the grid
        const f32 py = s->autoLayout.posY;
        const bool vis = is_collect_item_unlocked(s->x, s->y);

        if (cont) {
            cont->mBounds.set(-22.5f, -22.5f, 22.5f, 22.5f);
            set_pane_pos(cont, px, py);
            if (ref) cont->scale(ref->getScaleX(), ref->getScaleY());
            if (vis) cont->show(); else cont->hide();
        }
        if (pic) {
            pic->mBounds.set(-22.5f, -22.5f, 22.5f, 22.5f);
            pic->translate(0.0f, 0.0f);
            if (s->texOverride) pic->changeTexture(s->texOverride, 0);
            if (vis) pic->show(); else pic->hide();
        }
        if (frm) {
            const f32 frameY = (s->at.row == 1) ? s_ken_g0_origY : (s->at.row == 2) ? s_tate_g0_origY : s_fuku_g0_origY;
            frm->mBounds.set(-23.5f, -23.5f, 23.5f, 23.5f);
            set_pane_pos(frm, px - 24.5f, frameY);
            if (vis) frm->show(); else frm->hide();
        }
    }

    // Dynamic connectors: bridge each row's last vanilla item to the custom items
    // that follow it, plus every consecutive pair of custom items. Positioned at
    // the FRAME-pane midpoint, exactly like the vanilla tunagi connectors
    // (slot X - 24.5). Pooled pictures cloned from tunagi01/03/06.
    static const f32 kConnY[3] = { s_ken_g0_origY, s_tate_g0_origY, s_fuku_g0_origY };
    const f32 dxC    = s_col_dx + 5.0f;
    const f32 baseXC = s_ken_n0_origX + gridDx;

    int connIdx = 0;
    for (u8 row = 1; row <= 3; row++) {
        const int tmpl = row - 1;   // 0 sword / 1 shield / 2 clothes
        J2DPane*    parent = s_customConnectorParent[tmpl];
        J2DPicture* srcPic = s_customConnectorTemplate[tmpl];
        if (!parent || !srcPic) continue;

        // Collect this row's custom item columns, then walk them in ASCENDING
        // order - registration order must not matter (the swords register
        // 6,5,4 which used to chain 3->6, 6->5, 5->4 and left the 3->4
        // connector missing).
        u8 cols[8];
        int n = 0;
        for (int i = 0; i < custom_equip_count() && n < 8; i++) {
            const CustomEquipDef* d = custom_equip_get(i);
            if (!d) continue;
            const u8 r = d->kind == CE_SWORD ? 1 : d->kind == CE_SHIELD ? 2 : 3;
            if (r != row) continue;
            int k = n;
            while (k > 0 && cols[k - 1] > d->item) {
                cols[k] = cols[k - 1];
                k--;
            }
            cols[k] = d->item;
            n++;
        }

        // Last vanilla item column in this row (clothes has 4, sword/shield 3):
        // the chain bridges last vanilla -> first custom -> ... -> last custom.
        u8 prevItem = (row == 3) ? 4 : 3;

        // The chain moves with its row's shift, same as the vanilla slots.
        const f32 shiftC = cl_column_occupied(row, 1) ? 0.0f : -dxC;

        for (int k = 0; k < n && connIdx < 6; k++) {
            const u8 item = cols[k];
            const f32 connX = baseXC + shiftC + ((prevItem - 1) + (item - 1)) * 0.5f * dxC - 24.5f;
            const f32 connY = kConnY[tmpl];

            J2DPicture* cp = s_customConnectors[connIdx];
            if (!cp) {
                const ResTIMG* tex = safe_get_tex_info(srcPic);
                if (tex) {
                    cp = JKR_NEW J2DPicture(static_cast<u64>(0x63636E00 + connIdx),   // 'ccn' + idx
                                            srcPic->mBounds, tex, nullptr);
                    cp->setBasePosition((J2DBasePosition)srcPic->mBasePosition);
                    static_cast<CustomPicture*>(cp)->copyVisualsFrom(srcPic);
                    parent->appendChild(cp);
                    s_customConnectors[connIdx] = cp;
                }
            }
            if (cp) {
                static_cast<CustomPicture*>(cp)->copyVisualsFrom(srcPic);
                cp->mBounds.set(-6.0f, -18.0f, 6.0f, 18.0f);
                set_pane_pos(cp, connX, connY);
                cp->show();
            }
            prevItem = item;
            connIdx++;
        }
    }
    s_customConnectorCount = connIdx;
    for (int ci = connIdx; ci < 6; ci++) {
        if (s_customConnectors[ci]) s_customConnectors[ci]->hide();
    }
}

// Make every auto-managed slot selectable + tell the game its name/description
// message ids. Cheap and idempotent - safe to call every frame.
static void configure_managed_slots(dMenu_Collect2D_c* collect2D) {
    for (int i = 0; i < slot_count(); i++) {
        const SlotSpec* s = slot_get(i);
        if (!s->autoLayout.on || s->x >= 7 || s->y >= 6) continue;
        collect2D->field_0x22d[s->x][s->y] = is_collect_item_unlocked(s->x, s->y) ? 1 : 0;
        collect2D->field_0x184[s->x][s->y] = slot_name_id(s);
        collect2D->field_0x1d8[s->x][s->y] = slot_desc_id(s);
    }
}

void apply_collect_shifts(dMenu_Collect2D_c* collect2D) {
    if (!collect2D || !collect2D->mpScreen) return;
    J2DScreen* screen = collect2D->mpScreen;
    update_screen_bases(screen, collect2D->mpHeap);

    J2DPane* ken_n0 = screen->search(MULTI_CHAR('ken_n0'));
    J2DPane* ken_n1 = screen->search(MULTI_CHAR('ken_n1'));
    J2DPane* tate_n0 = screen->search(MULTI_CHAR('tate_n0'));
    J2DPane* tate_n1 = screen->search(MULTI_CHAR('tate_n1'));
    J2DPane* fuku_n0 = screen->search(MULTI_CHAR('fuku_n0'));
    J2DPane* fuku_n1 = screen->search(MULTI_CHAR('fuku_n1'));
    J2DPane* fuku_n2 = screen->search(MULTI_CHAR('fuku_n2'));
    J2DPane* heart_n = screen->search(MULTI_CHAR('heart_n'));
    J2DPane* kamen_n = screen->search(MULTI_CHAR('kamen_n'));
    J2DPane* modelbgn = screen->search(MULTI_CHAR('modelbgn'));

    J2DPane* ken_g0 = screen->search(MULTI_CHAR('ken_g_0'));
    J2DPane* ken_gm = slot_frame(4, 0);   // mod slot frame
    J2DPane* ken_g1 = screen->search(MULTI_CHAR('ken_g_1'));

    J2DPane* tate_g0 = screen->search(MULTI_CHAR('tate_g_0'));
    J2DPane* tate_gm = slot_frame(4, 1);   // mod slot frame
    J2DPane* tate_g1 = screen->search(MULTI_CHAR('tate_g_1'));

    J2DPane* fuku_go = slot_frame(3, 2);   // mod slot frame
    J2DPane* fuku_g0 = screen->search(MULTI_CHAR('fuku_g_0'));
    J2DPane* fuku_g1 = screen->search(MULTI_CHAR('fuku_g_1'));
    J2DPane* fuku_g2 = screen->search(MULTI_CHAR('fuku_g_2'));

    f32 dx = s_col_dx + 5.0f;
    // Second page: the item grid slides left (baseX). The heart is then taken
    // over entirely by collection_page_apply() (it lives on page 2).
    f32 baseX = s_ken_n0_origX + collection_page_grid_dx();
    f32 swordFrameBaseX = baseX - 24.5f;
    f32 shieldFrameBaseX = baseX - 24.5f;
    f32 clothesFrameBaseX = baseX - 24.5f;

    // With the starter-equip slot off, the sword and clothes rows are one column
    // shorter - slide their whole contents (and the heart) one slot left so they
    // line up flush with the shield row instead of leaving an empty slot.
    f32 rowShift = (cl_column_occupied(1, 1) || cl_column_occupied(3, 1)) ? 0.0f : -dx;
    // The Ordon Shield slot is starter gear too: gone (row shifts) unless "keep
    // ordon shield" holds it as a greyed-out, unselectable slot (no shift).
    f32 shieldShift = cl_column_occupied(2, 1) ? 0.0f : -dx;

    // Row 0 (Swords)
    set_pane_pos(ken_n0, baseX, s_ken_n0_origY);
    set_pane_pos(slot_icon(4, 0), baseX + dx + rowShift, s_ken_n0_origY);
    set_pane_pos(ken_n1, baseX + 2.0f * dx + rowShift, s_ken_n0_origY);

    set_pane_pos(ken_g0, swordFrameBaseX, s_ken_g0_origY);
    set_pane_pos(ken_gm, swordFrameBaseX + dx + rowShift, s_ken_g0_origY);
    set_pane_pos(ken_g1, swordFrameBaseX + 2.0f * dx + rowShift, s_ken_g0_origY);

    // Row 1 (Shields)
    set_pane_pos(tate_n0, baseX, s_tate_n0_origY);
    set_pane_pos(slot_icon(4, 1), baseX + dx + shieldShift, s_tate_n0_origY);
    set_pane_pos(tate_n1, baseX + 2.0f * dx + shieldShift, s_tate_n0_origY);

    set_pane_pos(tate_g0, shieldFrameBaseX, s_tate_g0_origY);
    set_pane_pos(tate_gm, shieldFrameBaseX + dx + shieldShift, s_tate_g0_origY);
    set_pane_pos(tate_g1, shieldFrameBaseX + 2.0f * dx + shieldShift, s_tate_g0_origY);

    // Row 2 (Clothes)
    set_pane_pos(slot_icon(3, 2), baseX, s_fuku_n0_origY);
    set_pane_pos(fuku_n0, baseX + dx + rowShift, s_fuku_n0_origY);
    set_pane_pos(fuku_n1, baseX + 2.0f * dx + rowShift, s_fuku_n0_origY);
    set_pane_pos(fuku_n2, baseX + 3.0f * dx + rowShift, s_fuku_n0_origY);

    set_pane_pos(fuku_go, clothesFrameBaseX, s_fuku_g0_origY);
    set_pane_pos(fuku_g0, clothesFrameBaseX + dx + rowShift, s_fuku_g0_origY);
    set_pane_pos(fuku_g1, clothesFrameBaseX + 2.0f * dx + rowShift, s_fuku_g0_origY);
    set_pane_pos(fuku_g2, clothesFrameBaseX + 3.0f * dx + rowShift, s_fuku_g0_origY);


    // Shift mirror 40px, background 20px
    f32 mirrorShiftX = 40.0f;
    // heart_n is positioned by collection_page_apply() (page 2); keep the row's
    // Y here so it's right if the page feature ever gets disabled.
    set_pane_pos(heart_n, baseX + 3.0f * dx + rowShift, s_heart_n_origY);
    set_pane_pos(kamen_n, s_kamen_n_origX + mirrorShiftX, s_kamen_n_origY);
    set_pane_pos(modelbgn, s_modelbgn_origX + mirrorShiftX - mirrorShiftX / 2.0f, s_modelbgn_origY);

    // Ensure bounds and local translations on custom panes and children
    if (slot_icon(4, 0)) slot_icon(4, 0)->mBounds.set(-22.5f, -22.5f, 22.5f, 22.5f);
    if (slot_frame(4, 0)) slot_frame(4, 0)->mBounds.set(-23.5f, -23.5f, 23.5f, 23.5f);
    if (slot_iconPic(4, 0)) {
        slot_iconPic(4, 0)->mBounds.set(-22.5f, -22.5f, 22.5f, 22.5f);
        slot_iconPic(4, 0)->translate(0.0f, 0.0f);
    }

    if (slot_icon(4, 1)) slot_icon(4, 1)->mBounds.set(-22.5f, -22.5f, 22.5f, 22.5f);
    if (slot_frame(4, 1)) slot_frame(4, 1)->mBounds.set(-23.5f, -23.5f, 23.5f, 23.5f);
    if (slot_iconPic(4, 1)) {
        slot_iconPic(4, 1)->mBounds.set(-22.5f, -22.5f, 22.5f, 22.5f);
        slot_iconPic(4, 1)->translate(0.0f, 0.0f);
    }

    if (slot_icon(3, 2)) slot_icon(3, 2)->mBounds.set(-22.5f, -22.5f, 22.5f, 22.5f);
    if (slot_frame(3, 2)) slot_frame(3, 2)->mBounds.set(-23.5f, -23.5f, 23.5f, 23.5f);
    if (slot_iconPic(3, 2)) {
        slot_iconPic(3, 2)->mBounds.set(-22.5f, -22.5f, 22.5f, 22.5f);
        slot_iconPic(3, 2)->translate(0.0f, 0.0f);
        ResTIMG* ordonClothesTex = get_ordon_clothes_texture();
        if (ordonClothesTex) {
            slot_iconPic(3, 2)->changeTexture(ordonClothesTex, 0);
        }
    }


    if (slot_icon(3, 2)) slot_icon(3, 2)->mBounds.set(-22.5f, -22.5f, 22.5f, 22.5f);
    if (slot_frame(3, 2)) slot_frame(3, 2)->mBounds.set(-23.5f, -23.5f, 23.5f, 23.5f);
    if (slot_iconPic(3, 2)) {
        slot_iconPic(3, 2)->mBounds.set(-22.5f, -22.5f, 22.5f, 22.5f);
        slot_iconPic(3, 2)->translate(0.0f, 0.0f);
        ResTIMG* ordonClothesTex = get_ordon_clothes_texture();
        if (ordonClothesTex) {
            slot_iconPic(3, 2)->changeTexture(ordonClothesTex, 0);
        }
    }

    // Connectors (tunagi)
    J2DPane* tunagi00 = screen->search(MULTI_CHAR('tunagi00'));
    J2DPane* tunagi01 = screen->search(MULTI_CHAR('tunagi01'));
    J2DPane* tunagi_k2 = screen->search(MULTI_CHAR('tuna_k2'));

    J2DPane* tunagi04 = screen->search(MULTI_CHAR('tunagi04'));
    J2DPane* tunagi03 = screen->search(MULTI_CHAR('tunagi03'));
    J2DPane* tunagi_t2 = screen->search(MULTI_CHAR('tuna_t2'));

    J2DPane* tunagi07 = screen->search(MULTI_CHAR('tunagi07'));
    J2DPane* tunagi06 = screen->search(MULTI_CHAR('tunagi06'));
    J2DPane* tunagi08 = screen->search(MULTI_CHAR('tunagi08'));
    J2DPane* tunagi_f3 = screen->search(MULTI_CHAR('tuna_f3'));

    // Position Sword connectors (shifted with the row when the starter slot is off)
    set_pane_pos(tunagi00, swordFrameBaseX - 0.5f * dx + rowShift, -44.0f);
    set_pane_pos(tunagi01, swordFrameBaseX + 0.5f * dx + rowShift, -44.0f);
    set_pane_pos(tunagi_k2, swordFrameBaseX + 1.5f * dx + rowShift, -44.0f);

    // Position Shield connectors (shifted when the Ordon Shield slot is dropped)
    set_pane_pos(tunagi04, shieldFrameBaseX - 0.5f * dx + shieldShift, 13.0f);
    set_pane_pos(tunagi03, shieldFrameBaseX + 0.5f * dx + shieldShift, 13.0f);
    set_pane_pos(tunagi_t2, shieldFrameBaseX + 1.5f * dx + shieldShift, 13.0f);

    // Position Clothes connectors
    set_pane_pos(tunagi07, clothesFrameBaseX - 0.5f * dx + rowShift, 70.0f);
    set_pane_pos(tunagi06, clothesFrameBaseX + 0.5f * dx + rowShift, 70.0f);
    if (tunagi08) {
        if (tunagi06) static_cast<CustomPicture*>(tunagi08)->copyVisualsFrom(static_cast<J2DPicture*>(tunagi06));
        tunagi08->mBounds.set(-6.0f, -18.0f, 6.0f, 18.0f);
        set_pane_pos(tunagi08, clothesFrameBaseX + 1.5f * dx + rowShift, 70.0f);
    }
    set_pane_pos(tunagi_f3, clothesFrameBaseX + 2.5f * dx + rowShift, 70.0f);

    if (s_picTunagiKen2 && tunagi01) {
        static_cast<CustomPicture*>(s_picTunagiKen2)->copyVisualsFrom(static_cast<J2DPicture*>(tunagi01));
        s_picTunagiKen2->mBounds.set(-6.0f, -18.0f, 6.0f, 18.0f);
    }
    if (s_picTunagiTate2 && tunagi03) {
        static_cast<CustomPicture*>(s_picTunagiTate2)->copyVisualsFrom(static_cast<J2DPicture*>(tunagi03));
        s_picTunagiTate2->mBounds.set(-6.0f, -18.0f, 6.0f, 18.0f);
    }
    if (s_picTunagiFuku3 && tunagi06) {
        static_cast<CustomPicture*>(s_picTunagiFuku3)->copyVisualsFrom(static_cast<J2DPicture*>(tunagi06));
        s_picTunagiFuku3->mBounds.set(-6.0f, -18.0f, 6.0f, 18.0f);
    }

    // Check item possession/unlocked status
    bool hasWoodSword = cl_column_claimed(1, 1) && cl_vanilla_slot_unlocked(3, 0);
    bool hasOrdonSword = is_collect_item_unlocked(4, 0);
    bool hasMasterSword = is_collect_item_unlocked(5, 0);
    bool hasHeart = (dComIfGs_getMaxLife() > 15);

    bool hasOrdonShield = cl_column_claimed(2, 1) && cl_vanilla_slot_unlocked(3, 1);
    bool hasWoodShield = is_collect_item_unlocked(4, 1);
    bool hasHylianShield = is_collect_item_unlocked(5, 1);

    bool hasKokiriClothes = is_collect_item_unlocked(4, 2);
    bool hasZoraArmor = is_collect_item_unlocked(5, 2);
    bool hasMagicArmor = is_collect_item_unlocked(6, 2);

    // Make all grid positions selectable by the cursor. The wooden-sword (3,0)
    // and ordon-clothes (3,2) slots only exist with the starter-equip option.
    u8 starterSlot = (cl_column_occupied(1, 1) || cl_column_occupied(3, 1)) ? 1 : 0;
    collect2D->field_0x22d[3][0] = starterSlot;
    collect2D->field_0x22d[4][0] = 1;
    collect2D->field_0x22d[5][0] = 1;
    collect2D->field_0x22d[6][0] = 1;

    // Ordon Shield (3,1): selectable only with starter-equip on. Off + "keep
    // ordon shield" leaves it greyed and unselectable; off + no keep removes it.
    collect2D->field_0x22d[3][1] = (cl_column_occupied(1, 1) || cl_column_occupied(3, 1)) ? 1 : 0;
    collect2D->field_0x22d[4][1] = 1;
    collect2D->field_0x22d[5][1] = 1;
    collect2D->field_0x22d[6][1] = 0;   // (auto-managed mod slots re-set their own cell below)

    collect2D->field_0x22d[3][2] = starterSlot;
    collect2D->field_0x22d[4][2] = 1;
    collect2D->field_0x22d[5][2] = 1;
    collect2D->field_0x22d[6][2] = 1;

    // Don't leave the cursor parked on a now-missing / greyed starter-gear slot.
    if (!cl_column_claimed(1, 1) && collect2D->mCursorX == 3 &&
        collect2D->mCursorY <= 2) {
        collect2D->mCursorX = 4;
    }

    // Connectors are ALWAYS drawn - except the leftmost sword / clothes segment,
    // which only leads into the starter-equip slot (3,0) / (3,2).
    if (tunagi00) { if (cl_column_claimed(1, 1)) tunagi00->show(); else tunagi00->hide(); }
    if (tunagi01) tunagi01->show();
    if (tunagi_k2) tunagi_k2->show();

    if (tunagi04) { if (ordon_shield_slot_present()) tunagi04->show(); else tunagi04->hide(); }
    if (tunagi03) tunagi03->show();
    if (tunagi_t2) tunagi_t2->show();

    if (tunagi07) { if (cl_column_claimed(1, 1)) tunagi07->show(); else tunagi07->hide(); }
    if (tunagi06) tunagi06->show();
    if (tunagi08) tunagi08->show();
    if (tunagi_f3) tunagi_f3->show();

    // Frames (rects) are ALWAYS drawn - except the starter-equip slot frames.
    if (ken_g0) { if (cl_column_claimed(1, 1)) ken_g0->show(); else ken_g0->hide(); }
    if (ken_gm) ken_gm->show();
    if (ken_g1) ken_g1->show();

    if (tate_g0) { if (ordon_shield_slot_present()) tate_g0->show(); else tate_g0->hide(); }
    if (tate_gm) tate_gm->show();
    if (tate_g1) tate_g1->show();

    if (fuku_go) { if (cl_column_claimed(1, 1)) fuku_go->show(); else fuku_go->hide(); }
    if (fuku_g0) fuku_g0->show();
    if (fuku_g1) fuku_g1->show();
    if (fuku_g2) fuku_g2->show();

    // Synchronize scaling across custom panes
    if (ken_n0 && slot_icon(4, 0)) slot_icon(4, 0)->scale(ken_n0->getScaleX(), ken_n0->getScaleY());
    if (tate_n0 && slot_icon(4, 1)) slot_icon(4, 1)->scale(tate_n0->getScaleX(), tate_n0->getScaleY());
    if (fuku_n0 && slot_icon(3, 2)) slot_icon(3, 2)->scale(fuku_n0->getScaleX(), fuku_n0->getScaleY());

    // Inner item icons: only shown if the item is unlocked!
    // 1. Swords
    J2DPane* ken_00 = screen->search(MULTI_CHAR('ken_00'));
    J2DPane* ken_01 = screen->search(MULTI_CHAR('ken_01'));
    if (ken_n0) { if (hasWoodSword) ken_n0->show(); else ken_n0->hide(); }
    if (ken_00) { if (hasWoodSword) { ken_00->show(); ken_00->translate(0.0f, 0.0f); } else ken_00->hide(); }
    if (ken_01) ken_01->hide();

    if (slot_icon(4, 0)) { if (hasOrdonSword) slot_icon(4, 0)->show(); else slot_icon(4, 0)->hide(); }
    if (slot_iconPic(4, 0)) { if (hasOrdonSword) { slot_iconPic(4, 0)->show(); slot_iconPic(4, 0)->translate(0.0f, 0.0f); } else slot_iconPic(4, 0)->hide(); }

    if (ken_n1) { if (hasMasterSword) ken_n1->show(); else ken_n1->hide(); }

    // Heart container
    if (heart_n) heart_n->show();

    // 2. Shields
    J2DPane* tate_00 = screen->search(MULTI_CHAR('tate_00'));
    J2DPane* tate_01 = screen->search(MULTI_CHAR('tate_01'));
    if (tate_n0) { if (hasOrdonShield) tate_n0->show(); else tate_n0->hide(); }
    if (tate_01) { if (hasOrdonShield) { tate_01->show(); tate_01->translate(0.0f, 0.0f); } else tate_01->hide(); }
    if (tate_00) tate_00->hide();

    if (slot_icon(4, 1)) { if (hasWoodShield) slot_icon(4, 1)->show(); else slot_icon(4, 1)->hide(); }
    if (slot_iconPic(4, 1)) { if (hasWoodShield) { slot_iconPic(4, 1)->show(); slot_iconPic(4, 1)->translate(0.0f, 0.0f); } else slot_iconPic(4, 1)->hide(); }

    if (tate_n1) { if (hasHylianShield) tate_n1->show(); else tate_n1->hide(); }

    // 3. Clothes
    // The ordon-clothes slot (3,2) exists only with the starter-equip option; its
    // panes aren't even created otherwise, but guard the show() too.
    if (slot_icon(3, 2)) { if (cl_column_claimed(1, 1)) slot_icon(3, 2)->show(); else slot_icon(3, 2)->hide(); }
    if (slot_iconPic(3, 2)) { if (cl_column_claimed(1, 1)) slot_iconPic(3, 2)->show(); else slot_iconPic(3, 2)->hide(); }

    if (fuku_n0) { if (hasKokiriClothes) fuku_n0->show(); else fuku_n0->hide(); }
    J2DPane* p0 = screen->search(MULTI_CHAR('fuku_00'));
    if (p0) { if (hasKokiriClothes) p0->show(); else p0->hide(); }

    if (fuku_n1) { if (hasZoraArmor) fuku_n1->show(); else fuku_n1->hide(); }
    J2DPane* p1 = screen->search(MULTI_CHAR('fuku_01'));
    if (p1) { if (hasZoraArmor) p1->show(); else p1->hide(); }

    if (fuku_n2) { if (hasMagicArmor) fuku_n2->show(); else fuku_n2->hide(); }
    J2DPane* p2 = screen->search(MULTI_CHAR('fuku_02'));
    if (p2) { if (hasMagicArmor) p2->show(); else p2->hide(); }

    // Auto-managed mod slots: position, size, show/hide, scale, selectability,
    // string ids - all straight from their SlotSpec.
    layout_managed_slots(collect2D);
    configure_managed_slots(collect2D);

    // Second page: pull heart + mirror off the grid onto their own page.
    collection_page_apply(collect2D);

    static bool s_hasLoggedCollectState = false;
    if (!s_hasLoggedCollectState) {
        s_hasLoggedCollectState = true;
        log_collect_info("[CollectionLib] State: keepOrdonShield=%d, hasOrdonShield=%d, hasWoodShield=%d, hasHylianShield=%d",
                         cl_keep_ordon_shield_enabled(), hasOrdonShield, hasWoodShield, hasHylianShield);
        log_collect_info("[CollectionLib] Shield Items: OrdonBit(0x2A)=%d, WoodBit(0x2B)=%d, HyliaBit(0x2C)=%d, curShield=0x%02X",
                         dComIfGs_isItemFirstBit(dItemNo_WOOD_SHIELD_e),
                         dComIfGs_isItemFirstBit(dItemNo_SHIELD_e),
                         dComIfGs_isItemFirstBit(dItemNo_HYLIA_SHIELD_e),
                         dComIfGs_getSelectEquipShield());
        log_collect_info("[CollectionLib] Shield Panes: tate_n0=%p(vis=%d, parent=%p), slot_icon(4, 1)=%p(vis=%d, parent=%p), tate_00=%p(vis=%d, parent=%p), tate_01=%p(vis=%d, parent=%p)",
                         tate_n0, tate_n0 ? tate_n0->isVisible() : -1, tate_n0 ? tate_n0->getParentPane() : nullptr,
                         slot_icon(4, 1), slot_icon(4, 1) ? slot_icon(4, 1)->isVisible() : -1, slot_icon(4, 1) ? slot_icon(4, 1)->getParentPane() : nullptr,
                         tate_00, tate_00 ? tate_00->isVisible() : -1, tate_00 ? tate_00->getParentPane() : nullptr,
                         tate_01, tate_01 ? tate_01->isVisible() : -1, tate_01 ? tate_01->getParentPane() : nullptr);
        J2DPicture* picT00 = static_cast<J2DPicture*>(tate_00);
        JUTTexture* tex00 = picT00 ? picT00->getTexture(0) : nullptr;
        const ResTIMG* timg00 = tex00 ? tex00->getTexInfo() : nullptr;
        J2DPicture* picT01 = static_cast<J2DPicture*>(tate_01);
        JUTTexture* tex01 = picT01 ? picT01->getTexture(0) : nullptr;
        const ResTIMG* timg01 = tex01 ? tex01->getTexInfo() : nullptr;
        J2DPane* tate_gm_pane = slot_frame(4, 1);

        log_collect_info("[CollectionLib] tate_00 tex: pic=%p, tex=%p, timg=%p",
                         picT00, tex00, timg00);
        log_collect_info("[CollectionLib] tate_01 tex: pic=%p, tex=%p, timg=%p",
                         picT01, tex01, timg01);
        log_collect_info("[CollectionLib] tate_gm frame: ptr=%p(vis=%d)",
                         tate_gm_pane, tate_gm_pane ? tate_gm_pane->isVisible() : -1);
    }

    update_frame_highlights(collect2D);

    // Consumer-recorded slot moves (collectionlib_move_slot), last so they
    // land on top of the default row layout.
    cl_apply_slot_moves(screen, baseX, dx);
}

void update_frame_highlights(dMenu_Collect2D_c* collect2D) {
    if (!collect2D || !collect2D->mpScreen) return;
    J2DScreen* screen = collect2D->mpScreen;

    u8 currentSword = dComIfGs_getSelectEquipSword();
    u8 currentShield = dComIfGs_getSelectEquipShield();
    u8 currentClothes = dComIfGs_getSelectEquipClothes();

    // A custom sword/shield skin owns the "equipped" ring for its row - suppress
    // every vanilla ring in that row (the mechanical item is unchanged, but the
    // menu shouldn't show two things equipped).
    if (custom_equip_active(CE_SWORD))  currentSword   = 0xFF;
    if (custom_equip_active(CE_SHIELD)) currentShield  = 0xFF;
    if (custom_equip_active(CE_TUNIC))  currentClothes = 0xFF;

    // 1. Swords
    // Slot 1 (Wooden Sword)
    J2DPicture* ken_g0 = static_cast<J2DPicture*>(screen->search(MULTI_CHAR('ken_g_0')));
    if (ken_g0) {
        bool eq = (currentSword == dItemNo_WOOD_STICK_e);
        ken_g0->setBlackWhite(JUtility::TColor(0, 0, 0, 0),
                              eq ? JUtility::TColor(255, 255, 0, 255) : JUtility::TColor(107, 107, 107, 255));
    }
    // Slot 2 (Ordon Sword)
    J2DPicture* picKenMidFrame = slot_frame(4, 0);
    if (picKenMidFrame) {
        bool eq = (currentSword == dItemNo_SWORD_e);
        picKenMidFrame->setBlackWhite(JUtility::TColor(0, 0, 0, 0),
                                      eq ? JUtility::TColor(255, 255, 0, 255) : JUtility::TColor(107, 107, 107, 255));
    }
    // Slot 3 (Master Sword)
    J2DPicture* ken_g1 = static_cast<J2DPicture*>(screen->search(MULTI_CHAR('ken_g_1')));
    if (ken_g1) {
        bool eq = (currentSword == dItemNo_MASTER_SWORD_e || currentSword == dItemNo_LIGHT_SWORD_e);
        ken_g1->setBlackWhite(JUtility::TColor(0, 0, 0, 0),
                              eq ? JUtility::TColor(255, 255, 0, 255) : JUtility::TColor(107, 107, 107, 255));
    }

    // 2. Shields
    // Slot 1 (Ordon Shield)
    J2DPicture* tate_g0 = static_cast<J2DPicture*>(screen->search(MULTI_CHAR('tate_g_0')));
    if (tate_g0) {
        bool eq = (currentShield == dItemNo_WOOD_SHIELD_e);
        tate_g0->setBlackWhite(JUtility::TColor(0, 0, 0, 0),
                               eq ? JUtility::TColor(255, 255, 0, 255) : JUtility::TColor(107, 107, 107, 255));
    }
    // Slot 2 (Wooden Shield)
    J2DPicture* picTateMidFrame = slot_frame(4, 1);
    if (picTateMidFrame) {
        bool eq = (currentShield == dItemNo_SHIELD_e);
        picTateMidFrame->setBlackWhite(JUtility::TColor(0, 0, 0, 0),
                                       eq ? JUtility::TColor(255, 255, 0, 255) : JUtility::TColor(107, 107, 107, 255));
    }
    // Slot 3 (Hylian Shield)
    J2DPicture* tate_g1 = static_cast<J2DPicture*>(screen->search(MULTI_CHAR('tate_g_1')));
    if (tate_g1) {
        bool eq = (currentShield == dItemNo_HYLIA_SHIELD_e);
        tate_g1->setBlackWhite(JUtility::TColor(0, 0, 0, 0),
                               eq ? JUtility::TColor(255, 255, 0, 255) : JUtility::TColor(107, 107, 107, 255));
    }

    // 3. Clothes
    // Slot 1 (Ordon Clothes)
    J2DPicture* picFukuStartFrame = slot_frame(3, 2);
    if (picFukuStartFrame) {
        bool eq = (currentClothes == dItemNo_WEAR_CASUAL_e);
        picFukuStartFrame->setBlackWhite(JUtility::TColor(0, 0, 0, 0),
                                         eq ? JUtility::TColor(255, 255, 0, 255) : JUtility::TColor(107, 107, 107, 255));
    }
    // Slot 2 (Kokiri Tunic)
    J2DPicture* fuku_g0 = static_cast<J2DPicture*>(screen->search(MULTI_CHAR('fuku_g_0')));
    if (fuku_g0) {
        bool eq = (currentClothes == dItemNo_WEAR_KOKIRI_e);
        fuku_g0->setBlackWhite(JUtility::TColor(0, 0, 0, 0),
                               eq ? JUtility::TColor(255, 255, 0, 255) : JUtility::TColor(107, 107, 107, 255));
    }
    // Slot 3 (Zora Armor)
    J2DPicture* fuku_g1 = static_cast<J2DPicture*>(screen->search(MULTI_CHAR('fuku_g_1')));
    if (fuku_g1) {
        bool eq = (currentClothes == dItemNo_WEAR_ZORA_e);
        fuku_g1->setBlackWhite(JUtility::TColor(0, 0, 0, 0),
                               eq ? JUtility::TColor(255, 255, 0, 255) : JUtility::TColor(107, 107, 107, 255));
    }
    // Slot 4 (Magic Armor)
    J2DPicture* fuku_g2 = static_cast<J2DPicture*>(screen->search(MULTI_CHAR('fuku_g_2')));
    if (fuku_g2) {
        bool eq = (currentClothes == dItemNo_ARMOR_e);
        fuku_g2->setBlackWhite(JUtility::TColor(0, 0, 0, 0),
                               eq ? JUtility::TColor(255, 255, 0, 255) : JUtility::TColor(107, 107, 107, 255));
    }

    // Keep all icon pictures in pristine, full brightness
    J2DPicture* io = slot_iconPic(3, 2);
    if (io) {
        io->setBlackWhite(JUtility::TColor(0, 0, 0, 0), JUtility::TColor(255, 255, 255, 255));
        io->setAlpha(255);
    }
    J2DPicture* t00 = static_cast<J2DPicture*>(screen->search(MULTI_CHAR('tate_00')));
    if (t00) {
        t00->setBlackWhite(JUtility::TColor(0, 0, 0, 0), JUtility::TColor(255, 255, 255, 255));
        t00->setAlpha(255);
    }
    J2DPicture* t01 = static_cast<J2DPicture*>(screen->search(MULTI_CHAR('tate_01')));
    if (t01) {
        t01->setBlackWhite(JUtility::TColor(0, 0, 0, 0), JUtility::TColor(255, 255, 255, 255));
        t01->setAlpha(255);
    }
    J2DPicture* k00 = static_cast<J2DPicture*>(screen->search(MULTI_CHAR('ken_00')));
    if (k00) {
        k00->setBlackWhite(JUtility::TColor(0, 0, 0, 0), JUtility::TColor(255, 255, 255, 255));
        k00->setAlpha(255);
    }
    J2DPicture* k01 = static_cast<J2DPicture*>(screen->search(MULTI_CHAR('ken_01')));
    if (k01) {
        k01->setBlackWhite(JUtility::TColor(0, 0, 0, 0), JUtility::TColor(255, 255, 255, 255));
        k01->setAlpha(255);
    }
    J2DPicture* tim = slot_iconPic(4, 1);
    if (tim) {
        tim->setBlackWhite(JUtility::TColor(0, 0, 0, 0), JUtility::TColor(255, 255, 255, 255));
        tim->setAlpha(255);
    }
    J2DPicture* kim = slot_iconPic(4, 0);
    if (kim) {
        kim->setBlackWhite(JUtility::TColor(0, 0, 0, 0), JUtility::TColor(255, 255, 255, 255));
        kim->setAlpha(255);
    }

    // Auto-layout mod slots (Reinforced Shield, Ordon Hero): ring follows
    // is_collect_item_equipped (their equippedFn); icon stays full brightness.
    for (int i = 0; i < slot_count(); i++) {
        const SlotSpec* s = slot_get(i);
        if (!s->autoLayout.on) continue;
        if (s->frame) {
            bool eq = is_collect_item_equipped(s->x, s->y);
            s->frame->setBlackWhite(JUtility::TColor(0, 0, 0, 0),
                eq ? JUtility::TColor(255, 255, 0, 255) : JUtility::TColor(107, 107, 107, 255));
        }
        if (s->iconPic) {
            s->iconPic->setBlackWhite(JUtility::TColor(0, 0, 0, 0), JUtility::TColor(255, 255, 255, 255));
            s->iconPic->setAlpha(255);
        }
    }
}

void on_menu_collect_2d_create_post(ModContext*, void* args, void*, void*) {
    if (!args) return;
    dMenu_Collect2D_c* collect2D = mods::arg<dMenu_Collect2D_c*>(args, 0);
    s_currentCollect2D = collect2D;
    if (!is_collection_menu_enabled()) return;
    collection_page_reset();   // always open on the main page
    apply_collect_shifts(collect2D);
}

HookAction on_menu_collect_2d_delete_pre(ModContext*, void*, void*, void*) {
    s_currentCollect2D = nullptr;
    s_picTunagiKen2 = nullptr;
    s_picTunagiTate2 = nullptr;
    s_picTunagiFuku3 = nullptr;
    for (int ci = 0; ci < 6; ci++) s_customConnectors[ci] = nullptr;
    s_customConnectorCount = 0;
    for (int ri = 0; ri < 3; ri++) {
        s_customConnectorParent[ri] = nullptr;
        s_customConnectorTemplate[ri] = nullptr;
    }
    s_capturedScreen = nullptr;
    s_cachedScreen = nullptr;
    slot_registry_clear();   // drops the mod slots' pane pointers before the screen dies
    collection_page_reset();
    return HOOK_CONTINUE;
}

HookAction on_screen_set_pre(ModContext*, void* args, void*, void*) {
    if (!is_collection_menu_enabled() || !args) return HOOK_CONTINUE;
    dMenu_Collect2D_c* collect2D = mods::arg<dMenu_Collect2D_c*>(args, 0);
    if (collect2D && collect2D->mpScreen) {
        update_screen_bases(collect2D->mpScreen, collect2D->mpHeap);
        apply_collect_shifts(collect2D);
    }
    return HOOK_CONTINUE;
}

void on_screen_set_post(ModContext*, void* args, void*, void*) {
    if (!is_collection_menu_enabled() || !args) return;
    dMenu_Collect2D_c* collect2D = mods::arg<dMenu_Collect2D_c*>(args, 0);
    if (!collect2D) return;

    // Swords name/description string IDs
    collect2D->field_0x184[3][0] = 0x1a4; // Wooden Sword
    collect2D->field_0x1d8[3][0] = 0x2a4;
    collect2D->field_0x184[4][0] = 0x18d; // Ordon Sword
    collect2D->field_0x1d8[4][0] = 0x28d;
    collect2D->field_0x184[5][0] = 0x18e; // Master Sword
    collect2D->field_0x1d8[5][0] = 0x28e;
    collect2D->field_0x184[6][0] = 0x186; // Heart Container
    collect2D->field_0x1d8[6][0] = 0x286;

    // Shields name/description string IDs
    collect2D->field_0x184[3][1] = 0x190; // Ordon Shield
    collect2D->field_0x1d8[3][1] = 0x290;
    collect2D->field_0x184[4][1] = 0x18f; // Wooden Shield
    collect2D->field_0x1d8[4][1] = 0x28f;
    collect2D->field_0x184[5][1] = 0x191; // Hylian Shield
    collect2D->field_0x1d8[5][1] = 0x291;

    // Clothes name/description string IDs
    collect2D->field_0x184[3][2] = 0x193; // Ordon Clothes
    collect2D->field_0x1d8[3][2] = 0x293;
    collect2D->field_0x184[4][2] = 0x194; // Kokiri Clothes
    collect2D->field_0x1d8[4][2] = 0x294;
    collect2D->field_0x184[5][2] = 0x196; // Zora Armor
    collect2D->field_0x1d8[5][2] = 0x296;
    collect2D->field_0x184[6][2] = 0x195; // Magic Armor
    collect2D->field_0x1d8[6][2] = 0x295;

    apply_collect_shifts(collect2D);   // also runs configure_managed_slots() for the mod slots
    update_frame_highlights(collect2D);

    if (collect2D->mpScreen) {
        auto setupSelPm = [&](int x, int y, J2DPane* pane) {
            if (!pane) {
                // Vacant cell: clear the entry. Leaving the previous screen's
                // CPaneMgr in place leaves a DANGLING pane pointer - the hover
                // scan then hit-tests freed memory with stale coordinates and
                // stops there, shadowing every slot behind it.
                collect2D->mpSelPm[x][y] = nullptr;
                return;
            }
            if (!collect2D->mpSelPm[x][y]) {
                JKRHeap* oldHeap = collect2D->mpHeap ? mDoExt_setCurrentHeap(collect2D->mpHeap) : nullptr;
                CPaneMgr* pm = JKR_NEW CPaneMgr();
                if (pm) {
                    pm->mFlags = 0;
                    pm->initiate(pane, (JKRExpHeap*)collect2D->mpHeap);
                    collect2D->mpSelPm[x][y] = pm;
                }
                if (oldHeap) mDoExt_setCurrentHeap(oldHeap);
            } else {
                collect2D->mpSelPm[x][y]->mPane = pane;
                collect2D->mpSelPm[x][y]->reinit();
            }
        };

        // Swords
        setupSelPm(3, 0, cl_column_claimed(1, 1)
                             ? collect2D->mpScreen->search(MULTI_CHAR('ken_n0'))
                             : nullptr);
        setupSelPm(4, 0, slot_icon(4, 0));
        setupSelPm(5, 0, collect2D->mpScreen->search(MULTI_CHAR('ken_n1')));
        setupSelPm(6, 0, collect2D->mpScreen->search(MULTI_CHAR('heart_n')));

        // Shields
        setupSelPm(3, 1, cl_column_claimed(1, 1)
                             ? collect2D->mpScreen->search(MULTI_CHAR('tate_n0'))
                             : nullptr);
        setupSelPm(4, 1, slot_icon(4, 1));
        setupSelPm(5, 1, collect2D->mpScreen->search(MULTI_CHAR('tate_n1')));
        // (6,1) is set up by the auto-managed-slot loop below.

        // Clothes
        setupSelPm(3, 2, slot_icon(3, 2));
        setupSelPm(4, 2, collect2D->mpScreen->search(MULTI_CHAR('fuku_n0')));
        setupSelPm(5, 2, collect2D->mpScreen->search(MULTI_CHAR('fuku_n1')));
        setupSelPm(6, 2, collect2D->mpScreen->search(MULTI_CHAR('fuku_n2')));

        // Auto-managed mod slots
        for (int i = 0; i < slot_count(); i++) {
            const SlotSpec* s = slot_get(i);
            if (s->autoLayout.on && s->icon && s->x < 7 && s->y < 6) {
                setupSelPm(s->x, s->y, s->icon);
            }
        }
    }
}

void on_menu_collect_wide_post(ModContext*, void* args, void*, void*) {
    if (!is_collection_menu_enabled() || !args) return;
    dMenu_Collect2D_c* collect2D = mods::arg<dMenu_Collect2D_c*>(args, 0);
    apply_collect_shifts(collect2D);
    update_frame_highlights(collect2D);
}

void on_menu_collect_2d_move_post(ModContext*, void* args, void*, void*) {
    if (!is_collection_menu_enabled() || !args) return;
    dMenu_Collect2D_c* collect2D = mods::arg<dMenu_Collect2D_c*>(args, 0);
    if (!collect2D) return;
    apply_collect_shifts(collect2D);
    update_frame_highlights(collect2D);
}

void on_mw_execute_post(ModContext*, void*, void*, void*) {
    if (s_needReloadCollect) {
        s_needReloadCollect = false;
        dMw_c* mw = dMeter2Info_getMenuWindowClass();
        if (mw && s_currentCollect2D != nullptr && mw->isPauseWindow()) {
            mw->dMw_collect_delete(true);
            mw->dMw_collect_create();
        }
    }
}
