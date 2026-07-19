/// @file BossHealthHudSystem.cpp
/// 役割・設計方針は BossHealthHudSystem.h を参照。
#include "BossHealthHudSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/Common/CCL_Archetype.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Serialization/Factory/EntityFactory.h"

#include "Engine/UI/ECS/Components/UITransformComponent.h"
#include "Engine/UI/ECS/Components/UIProgressBarComponent.h"
#include "Engine/UI/ECS/Components/UITextComponent.h"

#include <algorithm>
#include <cmath>

using namespace CCL::ECS;

namespace
{
// ボスHPバーは残量で色を変えず、威圧感のあるクリムゾン固定。失った分はアンバーで見せる。
const DirectX::XMFLOAT4 kBossFill  = {0.80f, 0.15f, 0.18f, 1.0f}; // 深紅
const DirectX::XMFLOAT4 kBossTrail = {1.00f, 0.82f, 0.35f, 0.95f}; // アンバー（失った分）
} // namespace

void BossHealthHudSystem::Initialize()
{
    // --- ゲージ本体（画面上部中央・アンカー方式） ---
    auto barArch = ArchetypeHelper::Generate<UITransformComponent, UIProgressBarComponent>();
    auto bar     = EntityFactory::SpawnNamed(*_world, "BossHpGauge", barArch);

    UITransformComponent t;
    t.anchor = {0.5f, 0.0f};   // 画面上辺の中央
    t.pivot  = {0.5f, 0.0f};   // 自身の上辺中央をアンカーに合わせる
    t.offset = {0.0f, 44.0f};  // 上端から44px下
    t.size   = {900.0f, 26.0f};

    UIProgressBarComponent b;
    b.sortOrder  = 100;
    b.showTrail  = true;       // 遅延ダメージ（チップ）バーを有効化
    b.fillColor  = kBossFill;
    b.trailColor = kBossTrail;
    b.value      = 1.0f;
    b.trailValue = 1.0f;
    b.visible    = false;      // 出現するまで隠しておく

    bar.Set(t).Set(b);
    m_barID = bar.ID();

    // --- ラベル（ゲージの上・中央寄せは近似オフセット） ---
    auto labelArch = ArchetypeHelper::Generate<UITransformComponent, UITextComponent>();
    auto label     = EntityFactory::SpawnNamed(*_world, "BossHpLabel", labelArch);

    UITransformComponent lt;
    lt.anchor = {0.5f, 0.0f};
    lt.pivot  = {0.5f, 0.0f};
    lt.offset = {-30.0f, 22.0f}; // ゲージの少し上
    lt.size   = {200.0f, 20.0f};

    UITextComponent lx;
    lx.text      = "BOSS";
    lx.pixelSize = 18.0f;
    lx.sortOrder = 103; // フィル(102)より手前
    lx.visible   = false;

    label.Set(lt).Set(lx);
    m_labelID = label.ID();
}

void BossHealthHudSystem::Update(float dt)
{
    auto *bar = (m_barID != InvalidEntityID) ? _world->GetComponent<UIProgressBarComponent>(m_barID) : nullptr;
    auto *txt = (m_labelID != InvalidEntityID) ? _world->GetComponent<UITextComponent>(m_labelID) : nullptr;
    if (!bar) return;

    // 生存中(HP>0)のボスを1体選ぶ。撃破済み(DeadTag/HP0)のボスは対象外にして戦後は隠す。
    const HealthComponent *boss = nullptr;
    ForEach([&](const HealthComponent &hp, const BossActionComponent & /*tag*/) {
        if (boss) return;
        if (hp.currentHealth > 0.0f) boss = &hp;
    });

    if (!boss)
    {
        bar->visible = false;
        if (txt) txt->visible = false;
        _init = false; // 次のボスに備えて初回スナップし直す
        return;
    }

    const float ratio = (boss->maxHealth > 0.0f)
                      ? std::clamp(boss->currentHealth / boss->maxHealth, 0.0f, 1.0f)
                      : 0.0f;

    // 初回は瞬時に合わせる（出現時にバーがスッと減って見えないように）。
    if (!_init)
    {
        bar->trailValue = ratio;
        _prevRatio      = ratio;
        _init           = true;
    }

    bar->visible = true;
    bar->value   = ratio;
    if (txt) txt->visible = true;

    // 被弾（HPが減った）を検出したら、遅延バーの「溜め」をリセットして失った分を見せる。
    if (ratio < _prevRatio - 1e-4f) _trailHold = kTrailHold;

    // 遅延バー：回復/据え置きは即追従、被弾直後は溜めのあと ease で value まで減っていく。
    if (ratio >= bar->trailValue)
    {
        bar->trailValue = ratio;
    }
    else if (_trailHold > 0.0f)
    {
        _trailHold -= dt;
    }
    else
    {
        const float k = 1.0f - std::exp(-kTrailSpeed * dt); // フレームレート非依存の指数減衰
        bar->trailValue += (ratio - bar->trailValue) * k;
    }

    _prevRatio = ratio;
}

REGISTER_LOGIC_SYSTEM(BossHealthHudSystem, Priority::LogicStage::L02_PostUpdate);
