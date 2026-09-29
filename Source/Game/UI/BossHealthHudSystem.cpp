#include "BossHealthHudSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/Common/CCL_Archetype.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Core/Math/Interpolation.h" // Damp: フレームレートに依らない追従の係数
#include "Engine/Serialization/Factory/EntityFactory.h"

#include "Engine/Transform/Components/TransformComponent.h" // UI の親子関係に使う
#include "Engine/UI/ECS/Components/UITransformComponent.h"
#include "Engine/UI/ECS/Components/UIProgressBarComponent.h"
#include "Engine/UI/ECS/Components/UITextComponent.h"
#include "Game/UI/MenuUiFactory.h" // SpawnCanvas / MakeUINode

#include <algorithm>
#include <cmath>

using namespace CCL::ECS;

namespace
{
    // ボスの HP バーは残りの量で色を変えず、深紅のままにする。失った分は琥珀色で見せる
    const DirectX::XMFLOAT4 kBossFill  = {0.80f, 0.15f, 0.18f, 1.0f};
    const DirectX::XMFLOAT4 kBossTrail = {1.00f, 0.82f, 0.35f, 0.95f};
} // namespace

void BossHealthHudSystem::Initialize()
{
    // ゲージとラベルはこの Canvas の子にする
    const EntityID canvas = CCL::GameUI::SpawnCanvas(*m_world, "BossHpCanvas");

    // --- ゲージ（画面上部の中央） ---
    auto barArch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent,
                                             UIProgressBarComponent>();
    auto bar     = EntityFactory::SpawnNamed(*m_world, "BossHpGauge", barArch);

    UITransformComponent t;
    t.anchor = {0.5f, 0.0f};  // 画面の上辺の中央
    t.pivot  = {0.5f, 0.0f};  // 自分の上辺の中央をアンカーに合わせる
    t.offset = {0.0f, 44.0f}; // 上端から 44px 下
    t.size   = {900.0f, 26.0f};

    UIProgressBarComponent b;
    b.sortOrder  = 100;
    b.showTrail  = true; // 失った分を遅れて減らすバーを出す
    b.fillColor  = kBossFill;
    b.trailColor = kBossTrail;
    b.gradient   = 0.45f; // 下端を暗くして、一色の塗りに見えないようにする
    b.value      = 1.0f;
    b.trailValue = 1.0f;
    b.visible    = false; // ボスが出るまで隠す

    bar.Set(CCL::GameUI::MakeUINode(canvas)).Set(t).Set(b);
    m_barID = bar.ID();

    // --- ラベル（ゲージの上。中央に見えるよう offset で寄せている） ---
    auto labelArch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent,
                                               UITextComponent>();
    auto label     = EntityFactory::SpawnNamed(*m_world, "BossHpLabel", labelArch);

    UITransformComponent lt;
    lt.anchor = {0.5f, 0.0f};
    lt.pivot  = {0.5f, 0.0f};
    lt.offset = {-30.0f, 22.0f}; // ゲージの少し上
    lt.size   = {200.0f, 20.0f};

    UITextComponent lx;
    lx.text      = "BOSS";
    lx.pixelSize = 18.0f;
    lx.sortOrder = 110; // ゲージは sortOrder から +2 までの層を使うので、それより手前に置く
    lx.visible   = false;

    label.Set(CCL::GameUI::MakeUINode(canvas)).Set(lt).Set(lx);
    m_labelID = label.ID();
}

void BossHealthHudSystem::Update(float dt)
{
    auto *bar = (m_barID != InvalidEntityID) ? m_world->GetComponent<UIProgressBarComponent>(m_barID) : nullptr;
    auto *txt = (m_labelID != InvalidEntityID) ? m_world->GetComponent<UITextComponent>(m_labelID) : nullptr;
    if (!bar) return;

    // HP が 0 より大きいボスを1体選ぶ。倒したボスは対象にせず、戦闘の後はゲージを隠す
    const HealthComponent *boss = nullptr;
    ForEach([&](const HealthComponent &hp, const BossActionComponent & /*tag*/) {
        if (boss) return;
        if (hp.currentHealth > 0.0f) boss = &hp;
    });

    if (!boss)
    {
        bar->visible = false;
        if (txt) txt->visible = false;
        m_init  = false; // 次のボスが出たら値を合わせ直す
        m_shake = 0.0f;
        return;
    }

    const float ratio = (boss->maxHealth > 0.0f)
                            ? std::clamp(boss->currentHealth / boss->maxHealth, 0.0f, 1.0f)
                            : 0.0f;

    // 最初は遅れて減るバーもすぐに合わせる（出た瞬間にバーが減っていくように見えないように）
    if (!m_init)
    {
        bar->trailValue = ratio;
        m_prevRatio     = ratio;
        m_init          = true;
    }

    bar->visible = true;
    bar->value   = ratio;
    if (txt) txt->visible = true;

    // HP が減ったら、遅れて減るバーを少し止めて失った分を見せ、ゲージを揺らす
    if (ratio < m_prevRatio - 1e-4f)
    {
        m_trailHold = kTrailHold;
        m_shake     = 1.0f;
        m_shakeTime = 0.0f; // 毎回同じ揺れ方から始める
    }

    // 揺れを指数関数的に減らす。ウィジェットは揺れの量を持つだけで、減らすのはこの System
    m_shakeTime += dt;
    m_shake -= m_shake * CCL::Math::Damp(kShakeDecay, dt);
    if (m_shake < 0.02f) m_shake = 0.0f; // わずかな揺れが 1px 残り続けないように 0 にする
    bar->shake        = m_shake;
    bar->shakeElapsed = m_shakeTime;

    // 遅れて減るバー: 回復・変化なしならすぐ合わせ、被弾したら少し止めてから value へ近づける
    if (ratio >= bar->trailValue)
    {
        bar->trailValue = ratio;
    }
    else if (m_trailHold > 0.0f)
    {
        m_trailHold -= dt;
    }
    else
    {
        const float k = CCL::Math::Damp(kTrailSpeed, dt);
        bar->trailValue += (ratio - bar->trailValue) * k;
    }

    m_prevRatio = ratio;
}

REGISTER_LOGIC_SYSTEM(BossHealthHudSystem, Priority::LogicStage::L06_HudSync);
