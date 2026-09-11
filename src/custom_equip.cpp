#include "collection_lib/custom_equip.hpp"

#include "JSystem/J3DGraphAnimator/J3DModel.h"
#include "JSystem/J3DGraphAnimator/J3DModelData.h"
#include "JSystem/J3DGraphAnimator/J3DMaterialAnm.h"
#include "JSystem/J3DGraphBase/J3DMaterial.h"
#include "JSystem/J3DGraphBase/J3DTransform.h"
#include "JSystem/J3DGraphBase/J3DEnum.h"
#include "JSystem/J3DGraphLoader/J3DModelLoader.h"
#include "JSystem/JKernel/JKRArchive.h"
#include "JSystem/JKernel/JKRExpHeap.h"
#include "JSystem/JKernel/JKRHeap.h"
#include "JSystem/JUtility/JUTTexture.h"
#include "d/actor/d_a_alink.h"
#include "d/d_com_inf_game.h"
#include "d/d_kankyo.h"
#include "d/d_stage.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_manager.h"
#include "dolphin/pad.h"
#include "m_Do/m_Do_mtx.h"
#include "mods/svc/resource.h"
#include "mods/svc/save.h"
#include <cctype>
#include <cstring>

extern const ResourceService* cl_get_resource_service();

DEFINE_HOOK(&daAlink_c::draw, CeAlinkDrawHook);
DEFINE_HOOK(&daAlink_c::statusWindowDraw, CeAlinkSwDrawHook);
DEFINE_HOOK(&daAlink_c::modelDraw, CeModelDrawHook);          // world shield/sword draw
DEFINE_HOOK(&daAlink_c::basicModelDraw, CeBasicModelDrawHook); // doll shield/sword draw
DEFINE_HOOK(&daAlink_c::setWaterDropColor, CeSetWaterDropColorHook); // bounds-checked water drop color
DEFINE_HOOK(&dDlst_shadowControl_c::addReal, CeShadowAddRealHook); // redirect real-time shadow to custom models
DEFINE_HOOK(&daAlink_c::shadowDraw, CeAlinkShadowDrawHook);       // swap vanilla equip out of Link's real shadow

namespace {

constexpr int kMaxDefs = 12;

struct Entry {
    CustomEquipDef def;
    // icon
    ResourceBuffer iconBuf = RESOURCE_BUFFER_INIT;
    ResTIMG*       iconTex = nullptr;
    // model
    ResourceBuffer arcBuf  = RESOURCE_BUFFER_INIT;
    JKRArchive*    arc     = nullptr;
    J3DModel*      model       = nullptr; // Body (or sword/shield)
    J3DModel*      sheathModel = nullptr; // Sword sheath
    J3DModel*      hatModel    = nullptr; // Head / Hat
    J3DModel*      faceModel   = nullptr; // Face
    J3DModel*      handModel   = nullptr; // Hands
    bool           tried     = false;  // stop retrying (success, or gave up)
    u8             tryCount  = 0;      // consecutive failed load attempts
};

Entry s_entries[kMaxDefs];
int   s_count = 0;

int s_activeId[3] = { -1, -1, -1 };   // per CustomEquipKind
int s_equipDebounce = 0;
char s_cachedStage[16] = {};
Mtx s_lastBaseMtx[3];
bool s_hasLastBaseMtx[3] = { false, false, false };
bool s_linkModelIsWolf = false;

static J3DModel* s_originalLinkModel = nullptr;
static J3DModel* s_originalHatModel  = nullptr;
static J3DModel* s_originalFaceModel = nullptr;
static J3DModel* s_originalHandModel = nullptr;
static J3DShape* s_origShape_06d0 = nullptr;
static J3DShape* s_origShape_06d4 = nullptr;
static J3DShape* s_origShape_06d8 = nullptr;
static J3DShape* s_origShape_06dc = nullptr;
static J3DShape* s_origShape_06e0 = nullptr;
static J3DShape* s_origShape_06e8 = nullptr;
static J3DShape* s_origShape_06ec = nullptr;
static J3DShape* s_origShape_06f0 = nullptr;

inline s16 deg2s16(f32 d) { return static_cast<s16>(d * (65536.0f / 360.0f)); }

bool is_wolf(daAlink_c* a) {
    return a == nullptr || a->checkWolf() || a->checkWolfShapeReverse() || a->checkMetamorphose();
}

bool is_full_wolf(daAlink_c* a) {
    return a == nullptr || (a->checkWolf() && !a->checkMetamorphose());
}

daAlink_c* player() { return static_cast<daAlink_c*>(dComIfGp_getPlayer(0)); }

static bool is_title_or_menu() {
    daPy_py_c* p = daPy_getLinkPlayerActorClass();
    if (p == nullptr) return true;
    const char* stage = dComIfGp_getStartStageName();
    if (stage != nullptr) {
    if (std::strcmp(stage, "F_SP102") == 0 ||
        std::strcmp(stage, "title") == 0 ||
        std::strcmp(stage, "opening") == 0 ||
        std::strcmp(stage, "name") == 0) {
        return true;
    }
    }
    return false;
}

static bool is_gameplay_ready() {
    if (is_title_or_menu()) return false;

    fpc_ProcID sceneId = dStage_roomControl_c::getProcID();
    if (sceneId == fpcM_ERROR_PROCESS_ID_e || fpcM_IsCreating(sceneId)) {
        return false;
    }

    daAlink_c* a = player();
    if (a == nullptr) return false;
    fpc_ProcID linkId = fopAcM_GetID(a);
    if (linkId == fpcM_ERROR_PROCESS_ID_e || fpcM_IsCreating(linkId)) {
        return false;
    }

    if (a->mpLinkModel == nullptr || a->mpLinkModel->getModelData() == nullptr) return false;
    if (a->mpLinkHatModel == nullptr || a->mpLinkFaceModel == nullptr || a->mpLinkHandModel == nullptr) return false;
    if (a->field_0x1f20 == nullptr || a->field_0x1f24 == nullptr) return false;
    if (a->field_0x2180[0] == nullptr || a->field_0x2180[1] == nullptr) return false;

    if (dComIfGp_isEnableNextStage()) return false;

    return true;
}

static bool str_contains_ci(const char* haystack, const char* needle) {
    if (!haystack || !needle) return false;
    for (; *haystack != '\0'; haystack++) {
        const char* h = haystack;
        const char* n = needle;
        while (*h != '\0' && *n != '\0' &&
               std::tolower(static_cast<unsigned char>(*h)) == std::tolower(static_cast<unsigned char>(*n))) {
            h++;
            n++;
        }
        if (*n == '\0') return true;
    }
    return false;
}

static void* find_bmd_matching(JKRArchive* arc, const char* pattern) {
    if (!arc || !arc->mFiles || !arc->mStringTable) return nullptr;
    u32 num = arc->countFile();
    for (u32 i = 0; i < num; i++) {
        if (arc->mFiles[i].isDirectory()) continue;
        const char* name = arc->mStringTable + arc->mFiles[i].getNameOffset();
        if (name != nullptr && str_contains_ci(name, ".bmd") && str_contains_ci(name, pattern)) {
            return arc->fetchResource(&arc->mFiles[i], nullptr);
        }
    }
    return nullptr;
}

static bool arc_has_file_matching(JKRArchive* arc, const char* ext, const char* pattern) {
    if (!arc || !arc->mFiles || !arc->mStringTable) return false;
    u32 num = arc->countFile();
    for (u32 i = 0; i < num; i++) {
        if (arc->mFiles[i].isDirectory()) continue;
        const char* name = arc->mStringTable + arc->mFiles[i].getNameOffset();
        if (name != nullptr && str_contains_ci(name, ext) && str_contains_ci(name, pattern)) {
            return true;
        }
    }
    return false;
}

static void* find_body_bmd(JKRArchive* arc) {
    if (!arc || !arc->mFiles || !arc->mStringTable) return nullptr;
    u32 num = arc->countFile();
    for (u32 i = 0; i < num; i++) {
        if (arc->mFiles[i].isDirectory()) continue;
        const char* name = arc->mStringTable + arc->mFiles[i].getNameOffset();
        if (name != nullptr && str_contains_ci(name, ".bmd")) {
            if (!str_contains_ci(name, "head") &&
                !str_contains_ci(name, "face") &&
                !str_contains_ci(name, "hand") &&
                !str_contains_ci(name, "kantera") &&
                !str_contains_ci(name, "glow") &&
                !str_contains_ci(name, "boot") &&
                !str_contains_ci(name, "swb")) {
                return arc->fetchResource(&arc->mFiles[i], nullptr);
            }
        }
    }
    return nullptr;
}

// The vanilla J3DModel* a custom item of `kind` replaces.
J3DModel* vanilla_model(daAlink_c* a, CustomEquipKind kind) {
    if (!a) return nullptr;
    switch (kind) {
    case CE_SWORD:  return a->mSwordModel;
    case CE_SHIELD: return a->mShieldModel;
    default:        return nullptr;
    }
}

static void* get_arc_res(JKRArchive* arc, const char* name, u16 fallbackId = 0xFFFF) {
    if (!arc) return nullptr;
    void* res = nullptr;
    if (name && name[0] != '\0') {
        res = arc->getResource(0, name);
        if (!res) res = arc->getResource(0x424D4433, name); // 'BMD3'
        if (!res) res = arc->getResource(name);
    }
    if (!res && fallbackId != 0xFFFF) {
        res = arc->getResource(fallbackId);
        if (!res) res = arc->getIdxResource(fallbackId);
    }
    return res;
}

static J3DModel* load_single_bmd(void* bmd, u32 diffFlags = 0x11000084) {
    if (!bmd) return nullptr;
    J3DModelData* data = J3DModelLoaderDataBase::load(bmd, 0x59020010);
    if (!data || data->getMaterialNum() == 0) return nullptr;

    for (u16 i = 0; i < data->getMaterialNum(); i++) {
        J3DMaterial* mat = data->getMaterialNodePointer(i);
        mat->change();
        if (J3DMaterialAnm* anm = JKR_NEW J3DMaterialAnm()) {
            mat->setMaterialAnm(anm);
        }
    }

    if (data->newSharedDisplayList(J3DMdlFlag_UseSingleDL) == kJ3DError_Success) {
        data->simpleCalcMaterial(const_cast<MtxP>(j3dDefaultMtx));
        data->makeSharedDL();
    }

    return mDoExt_J3DModel__create(data, 0x80000, diffFlags);
}

static void note_load_fail(Entry& e, const char* why) {
    log_collect_info("[CustomEquip] load FAIL (%s): %s (try %u)", why, e.def.modelArc,
                     static_cast<unsigned>(e.tryCount + 1));
    if (e.arc != nullptr) { JKRUnmountArchive(e.arc); e.arc = nullptr; }
    e.model = e.sheathModel = e.hatModel = e.faceModel = e.handModel = nullptr;
    if (++e.tryCount >= 30) {
        e.tried = true;
        log_collect_info("[CustomEquip] giving up on %s", e.def.modelArc);
    }
}

void load_model(Entry& e) {
    if (e.model != nullptr || e.tried) return;

    const ResourceService* res = cl_get_resource_service();
    if (res == nullptr || g_modCtx == nullptr) return;

    if (e.arcBuf.data == nullptr) {
        res->load(g_modCtx, e.def.modelArc, &e.arcBuf);
        if (e.arcBuf.data == nullptr) { note_load_fail(e, "res->load"); return; }
        log_collect_info("[CustomEquip] loaded %s (%u bytes)", e.def.modelArc,
                         static_cast<unsigned>(e.arcBuf.size));
    }

    JKRHeap* persistHeap = JKRHeap::getRootHeap();
    if (persistHeap == nullptr) persistHeap = static_cast<JKRHeap*>(mDoExt_getGameHeap());

    if (e.arc == nullptr) {
        e.arc = JKRArchive::mount(e.arcBuf.data, persistHeap, JKRArchive::MOUNT_DIRECTION_HEAD);
        if (e.arc == nullptr) { note_load_fail(e, "mount"); return; }
    }

    JKRHeap* old = mDoExt_setCurrentHeap(persistHeap);

    if (e.def.kind == CE_TUNIC) {
        void* hatBmd = find_bmd_matching(e.arc, "head");
        if (!hatBmd) hatBmd = get_arc_res(e.arc, "al_head.bmd", 0x0010);
        if (hatBmd) {
            e.hatModel = load_single_bmd(hatBmd, 0x11000084);
        }

        void* faceBmd = find_bmd_matching(e.arc, "face");
        if (!faceBmd) faceBmd = get_arc_res(e.arc, "al_face.bmd", 0x000E);
        if (faceBmd) {
            e.faceModel = load_single_bmd(faceBmd, 0x11020284);
            const bool hasFaceBtp = arc_has_file_matching(e.arc, ".btp", "face");
            const bool hasFaceBtk = arc_has_file_matching(e.arc, ".btk", "face");
            log_collect_info("[CustomEquip] %s: dedicated face anims in archive: btp=%s btk=%s",
                              e.def.modelArc, hasFaceBtp ? "yes" : "no", hasFaceBtk ? "yes" : "no");
        }

        void* handBmd = find_bmd_matching(e.arc, "hand");
        if (!handBmd) handBmd = get_arc_res(e.arc, "al_hands.bmd", 0x000F);
        if (handBmd) {
            e.handModel = load_single_bmd(handBmd, 0x11000084);
        }

        void* bodyBmd = nullptr;
        if (e.def.modelFileId != 0xFFFF) {
            bodyBmd = e.arc->getResource(static_cast<u16>(e.def.modelFileId));
            if (!bodyBmd) bodyBmd = e.arc->getIdxResource(e.def.modelFileId);
        }
        if (!bodyBmd) {
            bodyBmd = find_body_bmd(e.arc);
        }
        if (!bodyBmd) {
            bodyBmd = get_arc_res(e.arc, "al.bmd");
        }
        if (bodyBmd) {
            e.model = load_single_bmd(bodyBmd, 0x11000084);
        }
    } else {
        void* bmd = nullptr;
        if (e.def.modelFileId != 0xFFFF) {
            bmd = e.arc->getResource(static_cast<u16>(e.def.modelFileId));
            if (bmd == nullptr) bmd = e.arc->getIdxResource(e.def.modelFileId);
        }
        if (bmd != nullptr) {
            e.model = load_single_bmd(bmd, 0x11000084);
        }

        if (e.def.kind == CE_SWORD && e.def.sheathFileId != 0xFFFF) {
            void* sBmd = e.arc->getResource(static_cast<u16>(e.def.sheathFileId));
            if (sBmd == nullptr) sBmd = e.arc->getIdxResource(e.def.sheathFileId);
            if (sBmd != nullptr) {
                e.sheathModel = load_single_bmd(sBmd, 0x11000084);
            }
        }
    }

    mDoExt_setCurrentHeap(old);

    if (e.model == nullptr) {
        note_load_fail(e, "build");
        return;
    }

    e.tried = true;
    e.tryCount = 0;
    log_collect_info("[CustomEquip] model ready: %s (head=%p face=%p hands=%p sheath=%p)",
                     e.def.modelArc, e.hatModel, e.faceModel, e.handModel, e.sheathModel);
}

Entry* active_entry(CustomEquipKind kind) {
    int id = s_activeId[kind];
    return (id >= 0 && id < s_count) ? &s_entries[id] : nullptr;
}

// Suppress vanilla sword/shield mesh while custom item is active
HookAction on_alink_model_draw_pre(ModContext*, void* args, void*, void*) {
    daAlink_c* a = args ? mods::arg<daAlink_c*>(args, 0) : nullptr;
    J3DModel*  m = args ? mods::arg<J3DModel*>(args, 1) : nullptr;
    if (!a || !m || s_linkModelIsWolf || is_full_wolf(a)) return HOOK_CONTINUE;

    if (active_entry(CE_SWORD) != nullptr) {
        if (m == a->mSwordModel || m == a->mSheathModel) {
            return HOOK_SKIP_ORIGINAL;
        }
    }
    if (active_entry(CE_SHIELD) != nullptr) {
        if (m == a->mShieldModel) {
            return HOOK_SKIP_ORIGINAL;
        }
    }
    return HOOK_CONTINUE;
}

static void update_custom_model_matrix(Entry* e, CustomEquipKind kind, daAlink_c* a) {
    if (!e || !e->model || !a) return;

    J3DModel* vm = vanilla_model(a, kind);
    MtxP m = (vm != nullptr) ? vm->getBaseTRMtx() : nullptr;

    if (m != nullptr) {
        mDoMtx_copy(m, s_lastBaseMtx[kind]);
        s_hasLastBaseMtx[kind] = true;
    } else if (s_hasLastBaseMtx[kind]) {
        m = s_lastBaseMtx[kind];
    } else if (a->mpLinkModel != nullptr) {
        if (kind == CE_SHIELD) {
            mDoMtx_stack_c::copy(a->mpLinkModel->getAnmMtx(a->field_0x30b6));
            mDoMtx_stack_c::transM(4.2f, -4.4f, -20.0f);
            mDoMtx_stack_c::XYZrotM(deg2s16(91.0f), deg2s16(57.0f), deg2s16(180.0f));
            mDoMtx_copy(mDoMtx_stack_c::get(), s_lastBaseMtx[kind]);
            s_hasLastBaseMtx[kind] = true;
            m = s_lastBaseMtx[kind];
        } else if (kind == CE_SWORD) {
            mDoMtx_stack_c::copy(a->mpLinkModel->getAnmMtx(a->field_0x30b6));
            mDoMtx_stack_c::transM(-18.5f, 0.14f, 12.2f);
            mDoMtx_stack_c::XYZrotM(0, deg2s16(33.1f), 0);
            mDoMtx_copy(mDoMtx_stack_c::get(), s_lastBaseMtx[kind]);
            s_hasLastBaseMtx[kind] = true;
            m = s_lastBaseMtx[kind];
        }
    }
    if (m == nullptr) return;

    mDoMtx_stack_c::copy(m);
    mDoMtx_stack_c::transM(e->def.offX, e->def.offY, e->def.offZ);
    mDoMtx_stack_c::XYZrotM(deg2s16(e->def.rotX), deg2s16(e->def.rotY), deg2s16(e->def.rotZ));
    const f32 s = e->def.scale;
    e->model->setBaseScale(cXyz(s, s, s));
    e->model->setBaseTRMtx(mDoMtx_stack_c::get());
    e->model->calc();

    if (kind == CE_SWORD && e->sheathModel != nullptr) {
        MtxP sm = (a->mSheathModel != nullptr) ? a->mSheathModel->getBaseTRMtx() : nullptr;
        if (sm != nullptr) {
            mDoMtx_stack_c::copy(sm);
        } else if (a->mpLinkModel != nullptr) {
            mDoMtx_stack_c::copy(a->mpLinkModel->getAnmMtx(a->field_0x30b6));
        } else {
            return;
        }
        mDoMtx_stack_c::transM(e->def.offX, e->def.offY, e->def.offZ);
        mDoMtx_stack_c::XYZrotM(deg2s16(e->def.rotX), deg2s16(e->def.rotY), deg2s16(e->def.rotZ));
        e->sheathModel->setBaseScale(cXyz(s, s, s));
        e->sheathModel->setBaseTRMtx(mDoMtx_stack_c::get());
        e->sheathModel->calc();
    }
}

static bool is_warp_visual(daAlink_c* a) {
    if (!a) return false;
    u16 proc = a->mProcID;
    return proc == daAlink_c::PROC_DUNGEON_WARP_READY ||
           proc == daAlink_c::PROC_DUNGEON_WARP ||
           proc == daAlink_c::PROC_DUNGEON_WARP_SCN_START ||
           proc == daAlink_c::PROC_WARP ||
           proc == daAlink_c::PROC_TW_GATE;
}

// ---- Gamepad LED colour override (custom tunics) ---------------------------
// The vanilla gamepad_color logic (dusk gamepad_color.cpp) picks the LED
// colour from the EQUIPPED VANILLA tunic. When a custom tunic with a
// padColor override is worn, recolour every LED write the game makes -
// last-writer-wins per frame keeps the override visible while it runs, and
// vanilla colours return automatically once the tunic is off.
DEFINE_HOOK(&PADSetColor, CePadSetColorHook);

static HookAction on_pad_set_color_pre(ModContext*, void* args, void*, void*) {
    const int id = custom_equip_active_id(CE_TUNIC);
    if (id < 0) return HOOK_CONTINUE;
    const CustomEquipDef* d = custom_equip_get(id);
    if (d == nullptr || d->padColor == 0xFFFFFFFFu) return HOOK_CONTINUE;

    mods::arg_ref<u8>(args, 1) = static_cast<u8>((d->padColor >> 16) & 0xFF);
    mods::arg_ref<u8>(args, 2) = static_cast<u8>((d->padColor >> 8) & 0xFF);
    mods::arg_ref<u8>(args, 3) = static_cast<u8>(d->padColor & 0xFF);
    return HOOK_CONTINUE;
}

// POST (daAlink_c::draw + statusWindowDraw): draw custom models
void on_alink_draw_post(ModContext*, void*, void*, void*) {
    daAlink_c* a = player();
    if (!a || s_linkModelIsWolf || is_full_wolf(a) || is_warp_visual(a)) return;
    // Cutscenes / boss doors hide Link's whole model via checkPlayerNoDraw()
    // (the game passes that flag to every modelDraw of his sub-models). The
    // custom gear is drawn separately here, so honour the same flag or the
    // custom sword/shield keep floating on an invisible Link.
    if (a->checkPlayerNoDraw()) return;

    for (int k = 0; k < 3; k++) {
        CustomEquipKind kind = static_cast<CustomEquipKind>(k);
        if (kind == CE_TUNIC) continue;
        Entry* e = active_entry(kind);
        if (!e || !e->model) continue;

        update_custom_model_matrix(e, kind, a);

        g_env_light.settingTevStruct_colget_player(&a->tevStr);
        g_env_light.setLightTevColorType_MAJI(e->model, &a->tevStr);
        mDoExt_modelUpdateDL(e->model);

        if (kind == CE_SWORD && e->sheathModel != nullptr) {
            g_env_light.settingTevStruct_colget_player(&a->tevStr);
            g_env_light.setLightTevColorType_MAJI(e->sheathModel, &a->tevStr);
            mDoExt_modelUpdateDL(e->sheathModel);
        }
    }
}

// PRE (dDlst_shadowControl_c::addReal): redirect real-time shadow casting to custom
// models. NOTE: on the desktop symbol manifest this hook does NOT reliably fire
// (dDlst_shadowControl_c::addReal is inlined / not exported), so the real work is
// done by the daAlink_c::shadowDraw pre/post pair below - this stays as a
// belt-and-suspenders redirect for platforms where the symbol IS hookable.
HookAction on_add_real_shadow_pre(ModContext*, void* args, void* ret, void*) {
    if (!args) return HOOK_CONTINUE;
    dDlst_shadowControl_c* self = mods::arg<dDlst_shadowControl_c*>(args, 0);
    u32 key = mods::arg<u32>(args, 1);
    J3DModel* model = mods::arg<J3DModel*>(args, 2);
    daAlink_c* a = player();
    if (!a || !model || !self || s_linkModelIsWolf || is_full_wolf(a)) return HOOK_CONTINUE;

    Entry* swordEntry = active_entry(CE_SWORD);
    if (swordEntry != nullptr) {
        if (model == a->mSwordModel) {
            bool r = false;
            if (swordEntry->model != nullptr) {
                update_custom_model_matrix(swordEntry, CE_SWORD, a);
                r = self->addReal(key, swordEntry->model);
            }
            if (ret) *(bool*)ret = r;
            return HOOK_SKIP_ORIGINAL;
        }
        if (model == a->mSheathModel) {
            bool r = false;
            if (swordEntry->sheathModel != nullptr) {
                update_custom_model_matrix(swordEntry, CE_SWORD, a);
                r = self->addReal(key, swordEntry->sheathModel);
            }
            if (ret) *(bool*)ret = r;
            return HOOK_SKIP_ORIGINAL;
        }
    }

    Entry* shieldEntry = active_entry(CE_SHIELD);
    if (shieldEntry != nullptr) {
        if (model == a->mShieldModel) {
            bool r = false;
            if (shieldEntry->model != nullptr) {
                update_custom_model_matrix(shieldEntry, CE_SHIELD, a);
                r = self->addReal(key, shieldEntry->model);
            }
            if (ret) *(bool*)ret = r;
            return HOOK_SKIP_ORIGINAL;
        }
    }

    return HOOK_CONTINUE;
}

// -------------------------------------------------------------------------
// Real-time (projected-silhouette) shadow for custom equip.
//
// daAlink_c::shadowDraw() feeds Link's own model plus mSwordModel / mSheathModel /
// mShieldModel into the real shadow group via dComIfGd_addRealShadow(). We can't
// reliably hook that inner call on desktop (addReal isn't in the manifest), so we
// bracket shadowDraw() itself: PRE nulls the vanilla equip model pointers so the
// engine adds nothing for them, POST restores them and appends OUR custom models
// to the same shadow id (still valid until the drawlist resets next frame - the
// exact trick visible_equipment uses for the bow/lantern).
static J3DModel* s_shadowStash[3] = { nullptr, nullptr, nullptr }; // sword, sheath, shield

// Link's real-shadow group id (field_0x31a4 is what daAlink_c::shadowDraw itself
// uses for the on-foot case; mounted, the sword isn't drawn so we don't care).
static u32 ce_link_shadow_id(daAlink_c* a) {
    return static_cast<u32>(a->field_0x31a4);
}

HookAction on_alink_shadow_draw_pre(ModContext*, void* args, void*, void*) {
    daAlink_c* a = args ? mods::arg<daAlink_c*>(args, 0) : nullptr;
    s_shadowStash[0] = s_shadowStash[1] = s_shadowStash[2] = nullptr;
    if (!a || s_linkModelIsWolf || is_full_wolf(a)) return HOOK_CONTINUE;

    if (active_entry(CE_SWORD) != nullptr) {
        s_shadowStash[0] = a->mSwordModel;  a->mSwordModel  = nullptr;
        s_shadowStash[1] = a->mSheathModel; a->mSheathModel = nullptr;
    }
    if (active_entry(CE_SHIELD) != nullptr) {
        s_shadowStash[2] = a->mShieldModel; a->mShieldModel = nullptr;
    }
    return HOOK_CONTINUE;
}

void on_alink_shadow_draw_post(ModContext*, void* args, void*, void*) {
    daAlink_c* a = args ? mods::arg<daAlink_c*>(args, 0) : nullptr;
    if (!a) return;

    if (s_shadowStash[0] != nullptr) a->mSwordModel  = s_shadowStash[0];
    if (s_shadowStash[1] != nullptr) a->mSheathModel = s_shadowStash[1];
    if (s_shadowStash[2] != nullptr) a->mShieldModel = s_shadowStash[2];
    s_shadowStash[0] = s_shadowStash[1] = s_shadowStash[2] = nullptr;

    if (s_linkModelIsWolf || is_full_wolf(a)) return;

    const u32 sid = ce_link_shadow_id(a);
    if (sid == 0) return;

    Entry* sw = active_entry(CE_SWORD);
    if (sw != nullptr && sw->model != nullptr && a->checkSwordDraw()) {
        update_custom_model_matrix(sw, CE_SWORD, a);
        dComIfGd_addRealShadow(sid, sw->model);
        if (sw->sheathModel != nullptr) dComIfGd_addRealShadow(sid, sw->sheathModel);
    }
    Entry* sh = active_entry(CE_SHIELD);
    if (sh != nullptr && sh->model != nullptr && a->checkShieldDraw()) {
        update_custom_model_matrix(sh, CE_SHIELD, a);
        dComIfGd_addRealShadow(sid, sh->model);
    }
}

// PRE (daAlink_c::setWaterDropColor): prevent out-of-bounds crash on custom tunic models
// (e.g. Magic Armor code accessing hat material 2 when custom hat only has 2 materials 0..1).
HookAction on_set_water_drop_color_pre(ModContext*, void* args, void*, void*) {
    daAlink_c* a = args ? mods::arg<daAlink_c*>(args, 0) : nullptr;
    const J3DGXColorS10* i_color = args ? mods::arg<const J3DGXColorS10*>(args, 1) : nullptr;
    if (!a || !i_color) return HOOK_CONTINUE;

    if (active_entry(CE_TUNIC) != nullptr) {
        J3DModelData* bodyData = (a->field_0x064C != nullptr) ? a->field_0x064C :
                                 (a->mpLinkModel != nullptr ? a->mpLinkModel->getModelData() : nullptr);
        J3DModelData* hatData  = (a->mpLinkHatModel != nullptr) ? a->mpLinkHatModel->getModelData() : nullptr;

        if (bodyData != nullptr) {
            u16 num = bodyData->getMaterialNum();
            const u16 bodyIndices[] = {17, 9, 0, 1, 2, 16, 15, 14};
            for (u16 idx : bodyIndices) {
                if (idx < num) {
                    bodyData->getMaterialNodePointer(idx)->setTevColor(1, i_color);
                }
            }
        }
        if (hatData != nullptr) {
            u16 num = hatData->getMaterialNum();
            if (num > 0) {
                hatData->getMaterialNodePointer(0)->setTevColor(1, i_color);
            }
            if (num > 1) {
                hatData->getMaterialNodePointer(1)->setTevColor(1, i_color);
            }
        }
        return HOOK_SKIP_ORIGINAL;
    }
    return HOOK_CONTINUE;
}

}  // namespace

struct CustomEquipSaveBlob {
    u8 shieldItem = 0;
    u8 swordItem  = 0;
    u8 tunicItem  = 0;
    u8 padding    = 0;
};

static void save_custom_equip_state(CustomEquipKind kind, u8 item) {
    if (!is_title_or_menu()) {
        dSv_player_status_a_c& st = g_dComIfG_gameInfo.info.getPlayer().getPlayerStatusA();
        if (kind == CE_SHIELD) {
            st.setSelectEquip(5, item);
        } else if (kind == CE_SWORD) {
            st.unk31[0] = item;
        } else if (kind == CE_TUNIC) {
            st.unk31[1] = item;
        }
    }

    if (g_saveSvc != nullptr && g_modCtx != nullptr) {
        CustomEquipSaveBlob blob{};
        size_t size = sizeof(blob);
        g_saveSvc->get_blob(g_modCtx, "custom_equip", &blob, &size);
        if (kind == CE_SHIELD)      blob.shieldItem = item;
        else if (kind == CE_SWORD)  blob.swordItem  = item;
        else if (kind == CE_TUNIC)  blob.tunicItem  = item;
        g_saveSvc->set_blob(g_modCtx, "custom_equip", &blob, sizeof(blob));
    }
}

static bool s_restoredFromSave = false;

void custom_equip_restore_from_save() {
    if (!is_gameplay_ready()) return;

    if (s_count == 0) {
        collectionlib_run_slot_registration();
    }

    dSv_player_status_a_c& st = g_dComIfG_gameInfo.info.getPlayer().getPlayerStatusA();

    // 1. Shield
    if (s_activeId[CE_SHIELD] < 0) {
        u8 item = 0;
        if (g_saveSvc != nullptr && g_modCtx != nullptr) {
            CustomEquipSaveBlob blob{};
            size_t size = sizeof(blob);
            if (g_saveSvc->get_blob(g_modCtx, "custom_equip", &blob, &size) == MOD_OK) {
                item = blob.shieldItem;
            }
        }
        if (item == 0) item = st.getSelectEquip(5);
        if (item != 0) {
            for (int i = 0; i < s_count; i++) {
                if (s_entries[i].def.kind == CE_SHIELD && s_entries[i].def.item == item) {
                    s_activeId[CE_SHIELD] = i;
                    // Ensure valid backing shield in dComIfGs so Link can block and guard
                    if (dComIfGs_getSelectEquipShield() == dItemNo_NONE_e) {
                        u8 backing = dItemNo_WOOD_SHIELD_e;
                        if (dComIfGs_isItemFirstBit(dItemNo_HYLIA_SHIELD_e)) backing = dItemNo_HYLIA_SHIELD_e;
                        else if (dComIfGs_isItemFirstBit(dItemNo_SHIELD_e)) backing = dItemNo_SHIELD_e;
                        dMeter2Info_setShield(backing, false);
                    }
                    break;
                }
            }
        }
    }

    // 2. Sword
    if (s_activeId[CE_SWORD] < 0) {
        u8 item = 0;
        if (g_saveSvc != nullptr && g_modCtx != nullptr) {
            CustomEquipSaveBlob blob{};
            size_t size = sizeof(blob);
            if (g_saveSvc->get_blob(g_modCtx, "custom_equip", &blob, &size) == MOD_OK) {
                item = blob.swordItem;
            }
        }
        if (item == 0) item = st.unk31[0];
        if (item != 0) {
            for (int i = 0; i < s_count; i++) {
                if (s_entries[i].def.kind == CE_SWORD && s_entries[i].def.item == item) {
                    s_activeId[CE_SWORD] = i;
                    if (dComIfGs_getSelectEquipSword() == dItemNo_NONE_e) {
                        u8 backing = dItemNo_SWORD_e;
                        if (dComIfGs_isItemFirstBit(dItemNo_LIGHT_SWORD_e)) backing = dItemNo_LIGHT_SWORD_e;
                        else if (dComIfGs_isItemFirstBit(dItemNo_MASTER_SWORD_e)) backing = dItemNo_MASTER_SWORD_e;
                        else if (dComIfGs_isItemFirstBit(dItemNo_SWORD_e)) backing = dItemNo_SWORD_e;
                        else if (dComIfGs_isItemFirstBit(dItemNo_WOOD_STICK_e)) backing = dItemNo_WOOD_STICK_e;
                        dMeter2Info_setSword(backing, false);
                    }
                    break;
                }
            }
        }
    }

    // 3. Tunic
    if (s_activeId[CE_TUNIC] < 0) {
        u8 item = 0;
        if (g_saveSvc != nullptr && g_modCtx != nullptr) {
            CustomEquipSaveBlob blob{};
            size_t size = sizeof(blob);
            if (g_saveSvc->get_blob(g_modCtx, "custom_equip", &blob, &size) == MOD_OK) {
                item = blob.tunicItem;
            }
        }
        if (item == 0) item = st.unk31[1];
        if (item != 0) {
            for (int i = 0; i < s_count; i++) {
                if (s_entries[i].def.kind == CE_TUNIC && s_entries[i].def.item == item) {
                    s_activeId[CE_TUNIC] = i;
                    dMeter2Info_setCloth(s_entries[i].def.baseClothes, false);
                    dComIfGs_setSelectEquipClothes(s_entries[i].def.baseClothes);
                    break;
                }
            }
        }
    }
}

// -------------------------------------------------------------------------
void custom_equip_reset_registry() {
    // Keep loaded icons/models across screen rebuilds - only the defs are re-added.
    s_count = 0;
}

#include "collection_lib/collection_common.hpp"

int custom_equip_register(const CustomEquipDef& def) {
    CustomEquipDef resolved = def;
    if (resolved.item == 0) {
        u8 row = (resolved.kind == CE_SWORD) ? 1 : (resolved.kind == CE_SHIELD) ? 2 : 3;
        const u8 first = (row == 3) ? 5 : 4;
        u8 found = 0;
        for (u8 col = first; col <= 12; ++col) {
            if (!cl_item_exists(row, col)) {
                found = col;
                break;
            }
        }
        if (found == 0) return -1;
        resolved.item = found;
    }

    for (int i = 0; i < s_count; i++) {
        if (s_entries[i].def.kind == resolved.kind && s_entries[i].def.item == resolved.item) {
            if (s_entries[i].def.modelFileId != resolved.modelFileId ||
                s_entries[i].def.sheathFileId != resolved.sheathFileId ||
                (s_entries[i].def.modelArc != nullptr && std::strcmp(s_entries[i].def.modelArc, resolved.modelArc) != 0)) {
                s_entries[i].model = nullptr;
                s_entries[i].sheathModel = nullptr;
                s_entries[i].tried = false;
                s_entries[i].tryCount = 0;
            }
            s_entries[i].def = resolved;   // refresh
            return i;
        }
    }
    if (s_count >= kMaxDefs) return -1;
    int id = s_count++;
    // Preserve any already-loaded icon/model for this slot id.
    ResourceBuffer ib = s_entries[id].iconBuf; ResTIMG* it = s_entries[id].iconTex;
    ResourceBuffer ab = s_entries[id].arcBuf;  JKRArchive* ar = s_entries[id].arc;
    J3DModel* md = s_entries[id].model;
    J3DModel* sm = s_entries[id].sheathModel;
    J3DModel* hm = s_entries[id].hatModel;
    J3DModel* fm = s_entries[id].faceModel;
    J3DModel* hd = s_entries[id].handModel;
    bool tr = s_entries[id].tried;
    if (s_entries[id].def.modelFileId != resolved.modelFileId ||
        s_entries[id].def.sheathFileId != resolved.sheathFileId ||
        s_entries[id].def.kind != resolved.kind ||
        s_entries[id].def.item != resolved.item ||
        (s_entries[id].def.modelArc != nullptr && std::strcmp(s_entries[id].def.modelArc, resolved.modelArc) != 0)) {
        md = nullptr;
        sm = nullptr;
        hm = nullptr;
        fm = nullptr;
        hd = nullptr;
        tr = false;
    }
    s_entries[id] = Entry{};
    s_entries[id].def = resolved;
    s_entries[id].iconBuf = ib; s_entries[id].iconTex = it;
    s_entries[id].arcBuf = ab;  s_entries[id].arc = ar;
    s_entries[id].model = md;
    s_entries[id].sheathModel = sm;
    s_entries[id].hatModel = hm;
    s_entries[id].faceModel = fm;
    s_entries[id].handModel = hd;
    s_entries[id].tried = tr;
    return id;
}

int custom_equip_count() { return s_count; }

const CustomEquipDef* custom_equip_get(int id) {
    return (id >= 0 && id < s_count) ? &s_entries[id].def : nullptr;
}

// setSwordModel() is also the game's "Link draws his sword" routine: it forces
// mEquipItem = 0x103 and un-hides the blade shape. When we only need to rebuild
// the sword model after a skin / backing-sword swap we must NOT change whether
// the sword is actually in Link's hand - otherwise equipping a sword on the
// Collection screen (sheathed on the way in) leaves Link holding it after the
// screen closes, because resetStatusWindow()'s setSelectEquipItem(FALSE) keys
// the blade's visibility off mEquipItem == 0x103.
static void refresh_sword_model(daAlink_c* pl) {
    if (pl == nullptr) return;

    const u16 prevEquip   = pl->mEquipItem;
    const u16 prevPending = pl->field_0x2fde;
    const bool wasDrawn   = (prevEquip == 0x103);

    pl->setSwordModel();
    pl->setItemMatrix(0);

    if (!wasDrawn) {
        pl->offSwordModel();           // hide the blade shape + clear the BGM sword-using flag
        pl->mEquipItem   = prevEquip;  // undo setSwordModel()'s forced "sword in hand"
        pl->field_0x2fde = prevPending;
    }
}

void custom_equip_activate(int id) {
    if (id < 0 || id >= s_count) return;
    CustomEquipKind kind = s_entries[id].def.kind;
    s_activeId[kind] = id;
    // (Re-)equipping always gets a fresh load attempt, even if a previous try on
    // this stage gave up (e.g. it was equipped, un-equipped, the model freed on a
    // transition, then re-equipped).
    if (s_entries[id].model == nullptr) {
        s_entries[id].tried = false;
        s_entries[id].tryCount = 0;
    }
    load_model(s_entries[id]);
    save_custom_equip_state(kind, s_entries[id].def.item);

    daAlink_c* pl = player();
    if (pl && is_gameplay_ready()) {
        if (kind == CE_SWORD) {
            refresh_sword_model(pl);
        } else if (kind == CE_SHIELD) {
            pl->setShieldModel();
            pl->setItemMatrix(0);
        }
    }

    if (kind == CE_SWORD) {
        dMeter2_c* meter = g_meter2_info.getMeterClass();
        dMeter2Draw_c* draw = (meter != nullptr) ? meter->getMeterDrawPtr() : nullptr;
        if (draw != nullptr) {
            draw->changeTextureItemB(dComIfGs_getSelectEquipSword());
        }
    }
}

void custom_equip_clear(CustomEquipKind kind) {
    s_activeId[kind] = -1;
    save_custom_equip_state(kind, 0);

    daAlink_c* pl = player();
    if (pl && is_gameplay_ready()) {
        if (kind == CE_SWORD) {
            refresh_sword_model(pl);
        } else if (kind == CE_SHIELD) {
            pl->setShieldModel();
            pl->setItemMatrix(0);
        }
    }

    if (kind == CE_SWORD) {
        dMeter2_c* meter = g_meter2_info.getMeterClass();
        dMeter2Draw_c* draw = (meter != nullptr) ? meter->getMeterDrawPtr() : nullptr;
        if (draw != nullptr) {
            draw->changeTextureItemB(dComIfGs_getSelectEquipSword());
        }
    }
}

bool custom_equip_active(CustomEquipKind kind) { return s_activeId[kind] >= 0; }

int custom_equip_active_id(CustomEquipKind kind) { return s_activeId[kind]; }

// -------------------------------------------------------------------------
// Slot glue - the layout registers these for every custom-item slot.
static u8 kind_row(CustomEquipKind k) { return k == CE_SWORD ? 1 : k == CE_SHIELD ? 2 : 3; }

static int def_at_cell(u8 x, u8 y) {
    const SlotSpec* s = slot_at(x, y);
    if (s != nullptr) {
        CustomEquipKind kind = (s->at.row == 1) ? CE_SWORD : (s->at.row == 2) ? CE_SHIELD : CE_TUNIC;
        for (int i = 0; i < s_count; i++) {
            if (s_entries[i].def.kind == kind && s_entries[i].def.item == s->at.item) {
                return i;
            }
        }
    }
    for (int i = 0; i < s_count; i++) {
        SlotCell cell = grid_cell(kind_row(s_entries[i].def.kind), s_entries[i].def.item);
        if (cell.x == x && cell.y == y) return i;
    }
    return -1;
}

void update_frame_highlights(dMenu_Collect2D_c* collect2D);

void custom_equip_on_equip(dMenu_Collect2D_c* collect2D) {
    if (!collect2D || collect2D->mIsWolf || s_equipDebounce > 0) return;   // onEquip can fire twice per A-press
    daAlink_c* alink = daAlink_getAlinkActorClass();
    int id = def_at_cell(collect2D->mCursorX, collect2D->mCursorY);
    if (id < 0) return;

    CustomEquipKind kind = s_entries[id].def.kind;
    if (kind == CE_SHIELD && alink && alink->getShieldChangeWaitTimer() != 0) return;
    if (kind == CE_SWORD && alink && alink->getSwordChangeWaitTimer() != 0) return;
    if (kind == CE_TUNIC && alink && alink->getClothesChangeWaitTimer() != 0) return;

    if (s_activeId[kind] == id) {
        // Tunics cannot be unequipped (matches vanilla TP behavior where clothes can never be unequipped)
        if (kind == CE_TUNIC) return;

        // Already equipped -> unequip
        // Unequip is a lib feature (always available; no consumer policy).
        s_equipDebounce = 8;
        custom_equip_clear(kind);
        if (kind == CE_SHIELD) {
            dMeter2Info_setShield(dItemNo_NONE_e, false);
        } else if (kind == CE_SWORD) {
            dMeter2Info_setSword(dItemNo_NONE_e, false);
            refresh_sword_model(player());
        }
        Z2GetAudioMgr()->seStart(Z2SE_SY_ITEM_COMBINE_OFF, NULL, 0, 0, 1.0f, 1.0f, -1.0f, -1.0f, 0);
        dMeter2Info_set2DVibration();
        update_frame_highlights(collect2D);
    } else {
        // Equip
        s_equipDebounce = 8;
        custom_equip_activate(id);
        if (kind == CE_SHIELD) {
            if (dComIfGs_getSelectEquipShield() == dItemNo_NONE_e) {
                u8 backing = dItemNo_WOOD_SHIELD_e;
                if (dComIfGs_isItemFirstBit(dItemNo_HYLIA_SHIELD_e)) backing = dItemNo_HYLIA_SHIELD_e;
                else if (dComIfGs_isItemFirstBit(dItemNo_SHIELD_e)) backing = dItemNo_SHIELD_e;
                dMeter2Info_setShield(backing, false);
            }
        } else if (kind == CE_SWORD) {
            if (dComIfGs_getSelectEquipSword() == dItemNo_NONE_e) {
                u8 backing = dItemNo_SWORD_e;
                if (dComIfGs_isItemFirstBit(dItemNo_LIGHT_SWORD_e)) backing = dItemNo_LIGHT_SWORD_e;
                else if (dComIfGs_isItemFirstBit(dItemNo_MASTER_SWORD_e)) backing = dItemNo_MASTER_SWORD_e;
                else if (dComIfGs_isItemFirstBit(dItemNo_SWORD_e)) backing = dItemNo_SWORD_e;
                else if (dComIfGs_isItemFirstBit(dItemNo_WOOD_STICK_e)) backing = dItemNo_WOOD_STICK_e;
                dMeter2Info_setSword(backing, false);
            }
            refresh_sword_model(player());
        } else if (kind == CE_TUNIC) {
            dMeter2Info_setCloth(s_entries[id].def.baseClothes, false);
            dComIfGs_setSelectEquipClothes(s_entries[id].def.baseClothes);
        }
        Z2GetAudioMgr()->seStart(Z2SE_SY_ITEM_SET_X, NULL, 0, 0, 1.0f, 1.0f, -1.0f, -1.0f, 0);
        dMeter2Info_set2DVibration();
        update_frame_highlights(collect2D);
    }
}

bool custom_equip_is_unlocked(u8 x, u8 y) {
    int id = def_at_cell(x, y);
    if (id < 0) return false;
    const CustomEquipDef& d = s_entries[id].def;
    return d.unlocked ? d.unlocked() : true;
}

bool custom_equip_is_equipped(u8 x, u8 y) {
    int id = def_at_cell(x, y);
    return id >= 0 && s_activeId[s_entries[id].def.kind] == id;
}

ResTIMG* custom_equip_icon(int id) {
    if (id < 0 || id >= s_count) return nullptr;
    Entry& e = s_entries[id];
    if (e.iconTex != nullptr) return e.iconTex;
    if (e.iconBuf.data == nullptr) {
        const ResourceService* res = cl_get_resource_service();
        if (res == nullptr || g_modCtx == nullptr) return nullptr;
        res->load(g_modCtx, e.def.iconBti, &e.iconBuf);
        if (e.iconBuf.data == nullptr) return nullptr;
    }
    JKRHeap* gameHeap = mDoExt_getGameHeap();
    if (gameHeap != nullptr && e.iconBuf.size > 0) {
        void* p = gameHeap->alloc(e.iconBuf.size, 32);
        if (p != nullptr) {
            memcpy(p, e.iconBuf.data, e.iconBuf.size);
            e.iconTex = reinterpret_cast<ResTIMG*>(p);
            e.iconTex->alphaEnabled = 1;
            return e.iconTex;
        }
    }
    e.iconTex = reinterpret_cast<ResTIMG*>(e.iconBuf.data);
    e.iconTex->alphaEnabled = 1;
    return e.iconTex;
}

u64 custom_equip_icon_tag(int id)  { return static_cast<u64>(0x63656900) + id; }  // 'cei' + id
u64 custom_equip_pic_tag(int id)   { return static_cast<u64>(0x63657000) + id; }  // 'cep' + id
u64 custom_equip_frame_tag(int id) { return static_cast<u64>(0x63656700) + id; }  // 'ceg' + id

// -------------------------------------------------------------------------
static void on_custom_equip_save_loaded(ModContext*, uint32_t, void*) {
    s_activeId[0] = s_activeId[1] = s_activeId[2] = -1;
    s_restoredFromSave = false;
    custom_equip_restore_from_save();
    s_restoredFromSave = true;
}

static void on_custom_equip_new_save(ModContext*, uint32_t, void*) {
    s_activeId[0] = s_activeId[1] = s_activeId[2] = -1;
    s_restoredFromSave = true;
    save_custom_equip_state(CE_SHIELD, 0);
    save_custom_equip_state(CE_SWORD, 0);
    save_custom_equip_state(CE_TUNIC, 0);
}

void custom_equip_init_hooks(const HookService* hook_svc, const SaveService* save_svc) {
    if (save_svc != nullptr && g_modCtx != nullptr) {
        save_svc->observe_saves(g_modCtx, on_custom_equip_new_save, on_custom_equip_save_loaded, nullptr, nullptr, nullptr);
    }
    if (!hook_svc) return;
    mods::hook::add_pre<CeModelDrawHook>(hook_svc, on_alink_model_draw_pre);
    mods::hook::add_pre<CeBasicModelDrawHook>(hook_svc, on_alink_model_draw_pre);
    mods::hook::add_post<CeAlinkDrawHook>(hook_svc, on_alink_draw_post);
    mods::hook::add_post<CeAlinkSwDrawHook>(hook_svc, on_alink_draw_post);
    mods::hook::add_pre<CeSetWaterDropColorHook>(hook_svc, on_set_water_drop_color_pre);
    mods::hook::add_pre<CeShadowAddRealHook>(hook_svc, on_add_real_shadow_pre);
    mods::hook::add_pre<CeAlinkShadowDrawHook>(hook_svc, on_alink_shadow_draw_pre);
    mods::hook::add_pre<CePadSetColorHook>(hook_svc, on_pad_set_color_pre);
    mods::hook::add_post<CeAlinkShadowDrawHook>(hook_svc, on_alink_shadow_draw_post);
}

// Runs once per stage change.
static void on_stage_changed() {
    for (int i = 0; i < kMaxDefs; i++) {
        s_entries[i].iconTex = nullptr;
    }
    s_hasLastBaseMtx[0] = s_hasLastBaseMtx[1] = s_hasLastBaseMtx[2] = false;
    s_originalLinkModel = nullptr;
    s_originalHatModel  = nullptr;
    s_originalFaceModel = nullptr;
    s_originalHandModel = nullptr;
}

// Hand-rolled replacement for J3DAnmTexPattern::searchUpdateMaterialID(J3DModelData*)
// / J3DAnmTextureSRTKey::searchUpdateMaterialID(J3DModelData*). Both are opaque
// vanilla engine functions (declared only in this SDK, no source to audit) and both
// crashed (2026-09-05, deep inside J3DAnimation.cpp, first via a hot-reload trace,
// then reproduced reliably by the user just equipping the "Ordon Hero" custom
// tunic in the collection menu / on stage load). That tunic's al_face.bmd is a
// straight retexture of vanilla's own al_face.bmd - same material names, same
// count, same order - so a mismatched/degenerate material table (the earlier
// getMaterialNum()>0 guard) isn't the cause here; the vanilla routine itself
// appears to choke on being re-targeted at all onto a freshly, independently
// loaded J3DModelData instance for the face, something every custom-tunic face
// swap does. Both anm classes expose everything the search needs as plain public
// members (mUpdateMaterialName/mUpdateMaterialID/mUpdateMaterialNum - see
// J3DAnimation.h), and J3DModelData::getMaterialName() returns a JUTNameTab with
// getIndex(name) - so do the same by-name lookup ourselves, fully bounds-checked,
// instead of calling into the crashing routine at all.
// 2026-09-05, second AND third crash (both a short <unknown> frame right under
// ModLoader::tick - no named engine frame at all, i.e. inside OUR code, not
// vanilla's), still reproducing after adding a null check on mUpdateMaterialID
// alone - "Ordon Hero"'s archive ships al_face.bmd but no al_face.btp/al_face.btk
// (checked the arc's own file list), so whatever changeModelDataDirect(1) builds
// for a->mpFaceBtp/mpFaceBtk when the face model has no dedicated anim file of
// its own is suspect wholesale, not just its ID array - mUpdateMaterialNum could
// just as easily be garbage, or mUpdateMaterialName's backing resource pointer
// null, on such a make-do object. Validate the whole object before touching any
// of it: a non-null resource behind mUpdateMaterialName (via the public
// getResNameTable(), so this doesn't itself call into anything that could be the
// thing that's broken) and a sane (non-zero, non-absurd) material count.
template <typename AnmT>
static void safe_search_update_material_id(AnmT* anm, J3DModelData* faceData) {
    if (anm == nullptr || faceData == nullptr) {
        return;
    }
    JUTNameTab* dstNames = faceData->getMaterialName();
    if (dstNames == nullptr) {
        return;
    }
    if (anm->mUpdateMaterialID == nullptr) {
        return;
    }
    if (anm->mUpdateMaterialName.getResNameTable() == nullptr) {
        return;
    }
    const u16 num = anm->getUpdateMaterialNum();
    if (num == 0 || num > 64) {
        return;
    }
    const u16 dstNum = faceData->getMaterialNum();
    for (u16 i = 0; i < num; i++) {
        const char* name = anm->mUpdateMaterialName.getName(i);
        s32 idx = (name != nullptr) ? dstNames->getIndex(name) : -1;
        anm->mUpdateMaterialID[i] =
            (idx >= 0 && idx < dstNum) ? static_cast<u16>(idx) : static_cast<u16>(0xFFFF);
    }
}

// Re-targets Link's face material animators (mpFaceBtp/mpFaceBtk) onto
// whichever face model is CURRENTLY assigned (a->mpLinkFaceModel) - vanilla or
// custom - by material name, so entryTexMtxAnimator()/getMaterialAnm() index
// this model's own material table instead of the one they were originally
// built against. Called exactly once per swap, AFTER changeModelDataDirect(1)
// has (re)built mpFaceBtp/mpFaceBtk for the newly-assigned model - matching the
// single-call-site shape this had before an extra pre-changeModelDataDirect call
// was added for the Wolf->Human transform crash; that second call site turned
// out to be what "Ordon Hero" (a tunic with no dedicated al_face.btp/btk of its
// own) was crashing in, so it's gone again - safe_search_update_material_id's
// own guards are what should carry both cases now. Factored out of what used to
// be 4 near-identical copies so the guards below only need maintaining in one
// place.
static void retarget_face_material_anims(daAlink_c* a) {
    if (a == nullptr || a->mpLinkFaceModel == nullptr) {
        return;
    }
    J3DModelData* faceData = a->mpLinkFaceModel->getModelData();
    if (faceData == nullptr || faceData->getMaterialNum() <= 0) {
        return;
    }
    if (faceData->getMaterialNum() > 3) {
        faceData->getMaterialNodePointer(2)->setMaterialAnm(a->field_0x2180[0]);
        faceData->getMaterialNodePointer(3)->setMaterialAnm(a->field_0x2180[1]);
    }
    if (a->mpFaceBtp != nullptr) {
        safe_search_update_material_id(a->mpFaceBtp, faceData);
    }
    if (a->mpFaceBtk != nullptr) {
        safe_search_update_material_id(a->mpFaceBtk, faceData);
    }
}

static void custom_equip_apply(daAlink_c* a, bool duringRebuild = false);

void custom_equip_update() {
    if (s_equipDebounce > 0) s_equipDebounce--;

    if (is_title_or_menu()) {
        s_activeId[0] = s_activeId[1] = s_activeId[2] = -1;
        s_restoredFromSave = false;
        return;
    }

    // Models/archives live on the game heap, which is torn down on an area load -
    // rebuild them on a stage change (same reason visible_equipment invalidates).
    const char* stage = dComIfGp_getStartStageName();
    if (s_cachedStage[0] == '\0') {
        if (stage != nullptr) {
            std::strncpy(s_cachedStage, stage, sizeof(s_cachedStage) - 1);
            s_cachedStage[sizeof(s_cachedStage) - 1] = '\0';
        }
    } else if (stage != nullptr && std::strncmp(stage, s_cachedStage, sizeof(s_cachedStage) - 1) != 0) {
        std::strncpy(s_cachedStage, stage, sizeof(s_cachedStage) - 1);
        s_cachedStage[sizeof(s_cachedStage) - 1] = '\0';
        on_stage_changed();
    }

    // Wait until stage and Link actor are completely finished loading and initializing!
    if (!is_gameplay_ready()) {
        return;
    }

    custom_equip_apply(player());
}

// Restore the equipped custom gear onto Link: (re)build the models and swap the
// custom tunic onto his body/hat/face/hands. Split out so it can also run the
// instant the new-stage Link finishes creating (custom_equip_on_alink_created) -
// before his first drawn frame - instead of a few frames later from the poll,
// which is what let the Kokiri base model flash through on fade-in.
// duringRebuild: called straight from the changeLink POST hook, which JUST built
// Link's fresh human models - swap even while checkWolf()/checkMetamorphose() is
// still set (the Wolf->Human morph then morphs OUR model in, exactly like the
// working Human->Wolf direction). The per-frame poll passes false -> normal
// "don't touch a wolf" guard.
static void custom_equip_apply(daAlink_c* a, bool duringRebuild) {
    if (!s_restoredFromSave && is_gameplay_ready()) {
        custom_equip_restore_from_save();
        s_restoredFromSave = true;
    }

    for (int k = 0; k < 3; k++) {
        Entry* e = active_entry(static_cast<CustomEquipKind>(k));
        if (e) load_model(*e);
    }

    // Dynamic model swap for custom tunics (replaces Link's body, hat, face, hands)
    Entry* tunicEntry = active_entry(CE_TUNIC);
    if (tunicEntry != nullptr && tunicEntry->model != nullptr) {
        if (dComIfGs_getSelectEquipClothes() != tunicEntry->def.baseClothes) {
            dMeter2Info_setCloth(tunicEntry->def.baseClothes, false);
            dComIfGs_setSelectEquipClothes(tunicEntry->def.baseClothes);
        }
    }
    if (a && (duringRebuild || !is_wolf(a))) {
        if (tunicEntry && tunicEntry->model) {
            if (a->mpLinkModel != tunicEntry->model) {
                // 2026-09-05: three straight fix attempts around face-animator
                // retargeting (getMaterialNum guard, hand-rolled search, single-
                // call-site revert) all left the SAME short "<unknown> under
                // ModLoader::tick" crash equipping "Ordon Hero" unchanged - the
                // real faulting line is still unidentified. Log every step so the
                // NEXT report pins it down by the last line printed, instead of
                // guessing again.
                log_collect_info("[CustomEquip] tunic swap: begin '%s' (model=%p hat=%p face=%p hand=%p)",
                                  tunicEntry->def.name, tunicEntry->model, tunicEntry->hatModel,
                                  tunicEntry->faceModel, tunicEntry->handModel);

                if (s_originalLinkModel == nullptr) {
                    s_originalLinkModel = a->mpLinkModel;
                    s_originalHatModel  = a->mpLinkHatModel;
                    s_originalFaceModel = a->mpLinkFaceModel;
                    s_originalHandModel = a->mpLinkHandModel;
                    s_origShape_06d0 = a->field_0x06d0;
                    s_origShape_06d4 = a->field_0x06d4;
                    s_origShape_06d8 = a->field_0x06d8;
                    s_origShape_06dc = a->field_0x06dc;
                    s_origShape_06e0 = a->field_0x06e0;
                    s_origShape_06e8 = a->field_0x06e8;
                    s_origShape_06ec = a->field_0x06ec;
                    s_origShape_06f0 = a->field_0x06f0;
                }

                // 1. Swap Body
                log_collect_info("[CustomEquip] tunic swap: step 1 (body)");
                a->mpLinkModel = tunicEntry->model;
                a->mpLinkModel->setUserArea((uintptr_t)a);

                // 2. Swap Hat
                log_collect_info("[CustomEquip] tunic swap: step 2 (hat)");
                if (tunicEntry->hatModel != nullptr) {
                    a->mpLinkHatModel = tunicEntry->hatModel;
                    a->mpLinkHatModel->setUserArea((uintptr_t)a);
                }

                // 3. Swap Face
                log_collect_info("[CustomEquip] tunic swap: step 3 (face)");
                if (tunicEntry->faceModel != nullptr) {
                    a->mpLinkFaceModel = tunicEntry->faceModel;
                }

                // 4. Swap Hands
                log_collect_info("[CustomEquip] tunic swap: step 4 (hands)");
                if (tunicEntry->handModel != nullptr) {
                    a->mpLinkHandModel = tunicEntry->handModel;
                }

                // 5. Connect callbacks and animators
                log_collect_info("[CustomEquip] tunic swap: step 5 (changeModelDataDirect) - "
                                  "if this is the LAST line before a crash, it's inside vanilla's "
                                  "changeModelDataDirect itself, not our retargeting code");
                a->changeModelDataDirect(1);
                log_collect_info("[CustomEquip] tunic swap: step 5 done, mpFaceBtp=%p mpFaceBtk=%p",
                                  a->mpFaceBtp, a->mpFaceBtk);

                // 6. Body material shapes
                log_collect_info("[CustomEquip] tunic swap: step 6 (body material shapes)");
                if (a->field_0x064C != nullptr) {
                    if (a->field_0x064C->getMaterialNum() > 16) {
                        a->field_0x064C->getMaterialNodePointer(16)->getShape()->hide();
                    }
                    if (a->field_0x064C->getMaterialNum() > 12) {
                        a->field_0x06d8 = a->field_0x064C->getMaterialNodePointer(11)->getShape();
                        a->field_0x06dc = a->field_0x064C->getMaterialNodePointer(12)->getShape();
                        a->field_0x06e0 = a->field_0x064C->getMaterialNodePointer(6)->getShape();
                        a->field_0x06e8 = a->field_0x064C->getMaterialNodePointer(8)->getShape();
                        a->field_0x06ec = a->field_0x064C->getMaterialNodePointer(4)->getShape();
                        a->field_0x06f0 = a->field_0x064C->getMaterialNodePointer(7)->getShape();
                    }
                    a->field_0x06d0 = a->field_0x06d8;
                    a->field_0x06d4 = a->field_0x06dc;
                }

                // 7. Hand material shapes
                log_collect_info("[CustomEquip] tunic swap: step 7 (hand material shapes)");
                if (a->mpLinkHandModel != nullptr && a->mpLinkHandModel->getModelData() != nullptr) {
                    J3DModelData* handData = a->mpLinkHandModel->getModelData();
                    u16 numMats = handData->getMaterialNum();
                    for (u16 i = 0; i < 11 && i < numMats; i++) {
                        handData->getMaterialNodePointer(i)->getShape()->hide();
                    }
                }

                // 8. Eye LOD fix (mEyeHL1 + eye/highlight texture maxLOD). Deliberately
                // does NOT touch a->mpFaceBtp/a->mpFaceBtk anymore - see the
                // retarget_face_material_anims comment above for why. This part
                // (unchanged since before this session's face-retargeting work) never
                // was the crash: the 2026-09-05 diagnostic that isolated the crash to
                // "step 8" skipped this whole block wholesale, and removing only the
                // retarget_face_material_anims() call (below) already fixed it on its
                // own - this half is safe to keep.
                if (a->mpLinkFaceModel != nullptr && a->mpLinkFaceModel->getModelData() != nullptr) {
                    J3DModelData* faceData = a->mpLinkFaceModel->getModelData();
                    a->mEyeHL1.remove();
                    a->mEyeHL1.entry(faceData, "highlight02");

                    J3DTexture* tex = faceData->getTexture();
                    JUTNameTab* nametable = faceData->getTextureName();
                    if (tex != nullptr && nametable != nullptr) {
                        for (u16 i = 0; i < tex->getNum(); i++) {
                            const char* tex_name = nametable->getName(i);
                            if (tex_name != nullptr &&
                                (strcmp(tex_name, "al_eyeball") == 0 || strcmp(tex_name, "highlight02") == 0 ||
                                 strcmp(tex_name, "eye_kage01") == 0))
                            {
                                ResTIMG* timg = tex->getResTIMG(i);
                                timg->maxLOD = 0;
                            }
                        }
                    }
                }
                log_collect_info("[CustomEquip] tunic swap: complete");
            }
        } else if (s_originalLinkModel != nullptr) {
            if (a->mpLinkModel != s_originalLinkModel) {
                a->mpLinkModel     = s_originalLinkModel;
                a->mpLinkHatModel  = s_originalHatModel;
                a->mpLinkFaceModel = s_originalFaceModel;
                a->mpLinkHandModel = s_originalHandModel;
                a->field_0x06d0    = s_origShape_06d0;
                a->field_0x06d4    = s_origShape_06d4;
                a->field_0x06d8    = s_origShape_06d8;
                a->field_0x06dc    = s_origShape_06dc;
                a->field_0x06e0    = s_origShape_06e0;
                a->field_0x06e8    = s_origShape_06e8;
                a->field_0x06ec    = s_origShape_06ec;
                a->field_0x06f0    = s_origShape_06f0;

                a->mpLinkModel->setUserArea((uintptr_t)a);
                if (a->mpLinkHatModel) a->mpLinkHatModel->setUserArea((uintptr_t)a);
                a->changeModelDataDirect(1);

                a->mEyeHL1.remove();
                retarget_face_material_anims(a);
                if (a->mpLinkFaceModel != nullptr && a->mpLinkFaceModel->getModelData() != nullptr) {
                    a->mEyeHL1.entry(a->mpLinkFaceModel->getModelData(), "highlight02");
                }
            }
            s_originalLinkModel = nullptr;
            s_originalHatModel  = nullptr;
            s_originalFaceModel = nullptr;
            s_originalHandModel = nullptr;
        }
    }
}

// Set from the changeWolf POST (true) / changeLink POST (false) hooks - i.e. the
// moment daAlink_c::mpLinkModel actually becomes the wolf / human model.
void custom_equip_set_link_model_wolf(bool isWolf) {
    s_linkModelIsWolf = isWolf;
}

// Called from the changeLink PRE hook, just before Link's human models are
// rebuilt (create / clothes change / Wolf->Human). The captured base models are
// about to be freed - drop the refs so the POST re-captures the fresh set and the
// unequip-restore path can never write a dangling pointer.
void custom_equip_before_link_rebuild() {
    s_originalLinkModel = nullptr;
    s_originalHatModel  = nullptr;
    s_originalFaceModel = nullptr;
    s_originalHandModel = nullptr;
    s_hasLastBaseMtx[0] = s_hasLastBaseMtx[1] = s_hasLastBaseMtx[2] = false;
}

// Called from the changeLink / create POST hook - the instant Link's human models
// are (re)built, before his first drawn frame. Re-applies the custom tunic/gear
// swap so no plain base model is ever rendered (fade-in, un-transform, clothes
// swap).
void custom_equip_on_alink_created(daAlink_c* a) {
    // NOT gated on is_wolf: changeLink() only ever (re)builds Link's HUMAN form,
    // and running here mid Wolf->Human morph is exactly what makes the custom
    // model morph in instead of popping at the very end.
    if (a == nullptr) return;
    // Everything custom_equip_apply() touches on the actor must already exist.
    if (a->mpLinkModel == nullptr || a->mpLinkHatModel == nullptr ||
        a->mpLinkFaceModel == nullptr || a->mpLinkHandModel == nullptr) return;
    if (a->field_0x2180[0] == nullptr || a->field_0x2180[1] == nullptr) return;
    // Nothing equipped -> nothing to do (and don't want to run restore here).
    if (s_activeId[CE_SWORD] < 0 && s_activeId[CE_SHIELD] < 0 && s_activeId[CE_TUNIC] < 0) return;
    custom_equip_apply(a, /*duringRebuild=*/true);
}

void custom_equip_shutdown() {
    // Unconditionally detach mEyeHL1 from whatever it's currently entered against
    // BEFORE anything below unmounts a custom tunic's archive. It used to only
    // happen inside the `a->mpLinkModel != s_originalLinkModel` branch just below -
    // if that check was ever false at shutdown time (model already restored by
    // some other path, e.g. a stage change that ran first) the whole restore
    // block, remove() included, was skipped, but the archive-unmount loop further
    // down ran regardless. mEyeHL1.m_timg then kept pointing into the texture data
    // of an archive we'd just freed - a dangling pointer that only actually
    // faulted later, in the vanilla engine's own dEyeHL_mng_c::remove() (via
    // ~dEyeHL_c() on the actor's own destruction), reported by the user as a crash
    // on a mod reload / save load while a custom tunic was equipped. remove() is
    // safe to call redundantly (dEyeHL_mng_c::remove is a no-op once m_timg is
    // already null - see d_eye_hl.cpp), so doing it here first is free insurance.
    if (daAlink_c* a0 = player()) {
        a0->mEyeHL1.remove();
    }

    if (s_originalLinkModel != nullptr) {
        daAlink_c* a = player();
        if (a && a->mpLinkModel != s_originalLinkModel) {
            a->mpLinkModel     = s_originalLinkModel;
            a->mpLinkHatModel  = s_originalHatModel;
            a->mpLinkFaceModel = s_originalFaceModel;
            a->mpLinkHandModel = s_originalHandModel;
            a->field_0x06d0    = s_origShape_06d0;
            a->field_0x06d4    = s_origShape_06d4;
            a->field_0x06d8    = s_origShape_06d8;
            a->field_0x06dc    = s_origShape_06dc;
            a->field_0x06e0    = s_origShape_06e0;
            a->field_0x06e8    = s_origShape_06e8;
            a->field_0x06ec    = s_origShape_06ec;
            a->field_0x06f0    = s_origShape_06f0;
            a->mpLinkModel->setUserArea((uintptr_t)a);
            if (a->mpLinkHatModel) a->mpLinkHatModel->setUserArea((uintptr_t)a);
            a->changeModelDataDirect(1);

            a->mEyeHL1.remove();
            retarget_face_material_anims(a);
            if (a->mpLinkFaceModel != nullptr && a->mpLinkFaceModel->getModelData() != nullptr) {
                a->mEyeHL1.entry(a->mpLinkFaceModel->getModelData(), "highlight02");
            }
        }
        s_originalLinkModel = nullptr;
        s_originalHatModel  = nullptr;
        s_originalFaceModel = nullptr;
        s_originalHandModel = nullptr;
    }

    // Models + archives are on the persistent root heap. Only unmount an entry
    // whose model is NOT still assigned to the live Link actor (the restore block
    // above handles the normal case; this guards the narrow window right after a
    // transition where s_original* was nulled but not yet re-captured). A still-
    // referenced archive is left mounted (a bounded root-heap leak) rather than
    // freeing BMD data out from under a drawing model.
    daAlink_c* pl = player();
    const ResourceService* res = cl_get_resource_service();
    for (int i = 0; i < kMaxDefs; i++) {
        Entry& e = s_entries[i];
        bool inUse = pl != nullptr &&
            ((e.model     != nullptr && pl->mpLinkModel     == e.model) ||
             (e.hatModel  != nullptr && pl->mpLinkHatModel  == e.hatModel) ||
             (e.faceModel != nullptr && pl->mpLinkFaceModel == e.faceModel) ||
             (e.handModel != nullptr && pl->mpLinkHandModel == e.handModel));
        if (res != nullptr && g_modCtx != nullptr) {
            res->free(g_modCtx, &e.iconBuf);   // menu-only, never held by a world model
        }
        if (!inUse) {
            if (e.arc) JKRUnmountArchive(e.arc);
            if (res != nullptr && g_modCtx != nullptr) res->free(g_modCtx, &e.arcBuf);
            s_entries[i] = Entry{};
        } else {
            // Keep arc mounted + arcBuf alive (model still points into them);
            // just drop the icon handle.
            e.iconBuf = ResourceBuffer{};
            e.iconTex = nullptr;
        }
    }
    s_count = 0;
    s_activeId[0] = s_activeId[1] = s_activeId[2] = -1;
    s_equipDebounce = 0;
    s_cachedStage[0] = '\0';
    s_hasLastBaseMtx[0] = s_hasLastBaseMtx[1] = s_hasLastBaseMtx[2] = false;
}
