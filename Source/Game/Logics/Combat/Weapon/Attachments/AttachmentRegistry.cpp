#include "AttachmentRegistry.h"
#include "AttachmentComponents.h"
#include "ECS/Core/CCL_World.h"
#include "Game/Logics/Combat/Weapon/Attachments/AttachmentLogics.h"
#include "Game/Logics/Combat/Weapon/Attachments/AttachmentMasterData.h" 
#include <random>

using namespace CCL::ECS;
using namespace CCL::ECS::Core;

// 簡易乱数
static float RandomRange(float min, float max) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dis(min, max);
    return dis(gen);
}

/* ==============================================================================
 *  宣言的マクロ & トークン連結
 * `##` を使ってコンポーネント名(TravelXXComponent等)を自動生成し、記述を極限まで減らす。
 * ============================================================================== */

// ① Spawnフェーズ登録用のマクロ
#define REG_SPAWN(EFFECT_ID, ...) \
    { EffectID::EFFECT_ID, [](int lv, SpawnContext &c) { \
        const auto& p = AttachmentMasterData::GetParam(EffectID::EFFECT_ID); \
        __VA_ARGS__; \
    }}

// ② ベースとなる汎用登録マクロ
#define REGISTER_MOD(PHASE, EFFECT_ID, COMP_TYPE, ...) \
    { BulletPhase::PHASE, EffectID::EFFECT_ID, \
        [](Archetype &a) { a.Add<COMP_TYPE>(); }, \
        [](World *w, EntityID id, int lv, const BulletBlueprint *bp) { \
            const auto& p = AttachmentMasterData::GetParam(EffectID::EFFECT_ID); \
            COMP_TYPE c; \
            __VA_ARGS__; \
            w->AddComponent<COMP_TYPE>(id, c); \
        } \
    }

// ③ Travel / OnHit 専用の短縮マクロ (コンポーネント名を自動合成)
#define REG_TRAVEL(EFFECT, ...) REGISTER_MOD(Travel, EFFECT, Travel##EFFECT##Component, __VA_ARGS__)
#define REG_ONHIT(EFFECT,  ...) REGISTER_MOD(OnHit,  EFFECT, OnHit##EFFECT##Component,  __VA_ARGS__)


// ===========================================================================
// ① Spawnフェーズ（発射前のパラメータ計算）
// ===========================================================================
const std::vector<SpawnBinding> &AttachmentRegistry::GetSpawnBindings()
{
    static const std::vector<SpawnBinding> bindings = {
        //        [モッド名]    [適用する計算式]
        REG_SPAWN(Homing,     c.speed       *= (p.spawnValue1 + p.spawnScale1 * lv); ),
        REG_SPAWN(Accelerate, c.speed       *= (p.spawnValue1 + p.spawnScale1 * lv); ),
        REG_SPAWN(Slow,       c.speed       *= (p.spawnValue1 + p.spawnScale1 * lv); ),
        REG_SPAWN(Pierce,     c.pierceCount += static_cast<int>(p.spawnValue1 + p.spawnScale1 * lv); ),
        
        REG_SPAWN(Split, 
            c.bulletCount += static_cast<int>(p.spawnValue1 + p.spawnScale1 * lv);
            c.spread      += (p.spawnValue2 + p.spawnScale2 * lv);
        ),
        REG_SPAWN(Explosion, 
            c.speed *= (p.spawnValue1 + p.spawnScale1 * lv);
            ExplosionData expData;
            expData.radius     = p.baseValue1 + p.levelScale1 * lv;
            expData.damageMult = p.baseValue2 + p.levelScale2 * lv;
            BulletAttachmentLogic::SpawnExplosion(c.world, c.startPos, expData, 0.0f, CCL::ECS::InvalidEntityID);
        ),
        REG_SPAWN(Chaos, 
            float randomDirAngle = RandomRange(-p.spawnValue1, p.spawnValue1); 
            DirectX::XMVECTOR dirVec = DirectX::XMLoadFloat3(&c.direction);
            DirectX::XMMATRIX rot    = DirectX::XMMatrixRotationY(DirectX::XMConvertToRadians(randomDirAngle));
            DirectX::XMStoreFloat3(&c.direction, DirectX::XMVector3TransformNormal(dirVec, rot));
            c.spread += RandomRange(p.spawnValue2, p.spawnScale2);
            c.speed  *= RandomRange(p.baseValue1, p.levelScale1);
        )
    };
    return bindings;
}

// ===========================================================================
// ② Travel / OnHit フェーズ（Archetype動的合成・データ注入用）
// ===========================================================================
const std::vector<AttachmentBinding> &AttachmentRegistry::GetBindings()
{
    static const std::vector<AttachmentBinding> bindings = {
        // -------------------------------------------------------------------
        // 【 Travel 】 飛行中の振る舞い
        // -------------------------------------------------------------------
        //         [モッド名]   [いじる変数]            [適用する計算式]
        REG_TRAVEL(Homing,     c.data.turnSpeed      = p.baseValue2 + p.levelScale2 * lv; ),
        REG_TRAVEL(Accelerate, c.data.rate           = p.baseValue1 + p.levelScale1 * lv; ),
        REG_TRAVEL(Slow,       c.data.brakePower     = p.baseValue1 + p.levelScale1 * lv; ),
        
        REG_TRAVEL(Explosion,  c.data.radius         = p.baseValue1 + p.levelScale1 * lv;
                               c.data.damageMult     = p.baseValue2 + p.levelScale2 * lv;
                               c.intervalTimer       = p.interval; ),
                               
        REG_TRAVEL(Split,      c.data.count          = static_cast<int>(p.baseValue1 + p.levelScale1 * lv);
                               c.data.angleSpread    = p.baseValue2 + p.levelScale2 * lv;
                               c.intervalTimer       = p.interval; ),
                               
        REG_TRAVEL(Chaos,      c.data.speedMultRange = p.baseValue2 + p.levelScale2 * lv;
                               c.changeTimer         = p.interval; ),

        // -------------------------------------------------------------------
        // 【 OnHit 】 命中時の振る舞い
        // -------------------------------------------------------------------
        //         [モッド名]   [いじる変数]            [適用する計算式]
        REG_ONHIT (Explosion,  c.data.radius         = p.baseValue1 + p.levelScale1 * lv;
                               c.data.damageMult     = p.baseValue2 + p.levelScale2 * lv; ),
                               
        REG_ONHIT (Split,      c.data.count          = static_cast<int>(p.baseValue1 + p.levelScale1 * lv);
                               c.data.angleSpread    = p.baseValue2 + p.levelScale2 * lv; ),
                               
        REG_ONHIT (Chaos,      c.data.speedMultRange = p.baseValue2 + p.levelScale2 * lv; ),
        
        REG_ONHIT (Pierce,     c.data.addCount       = lv; ), // ※Pierceはレベルだけで完結
        
        REG_ONHIT (Knockback,  c.data.powerMult      = p.baseValue1 + p.levelScale1 * lv; ),
        
        //REG_ONHIT (Reflection, c.remainingCount      = static_cast<int>(p.baseValue1 + p.levelScale1 * lv); )
    };
    return bindings;
}