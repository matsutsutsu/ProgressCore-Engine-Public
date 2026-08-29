/// @file PlayerHealthHudSystem.cpp
/// 役割・設計方針は PlayerHealthHudSystem.h を参照。
#include "PlayerHealthHudSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/Common/CCL_Archetype.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Core/Math/Interpolation.h" // Damp(): フレームレート非依存の追従係数
#include "Engine/Serialization/Factory/EntityFactory.h"

#include "Engine/Transform/Components/TransformComponent.h" // UI階層（親子）
#include "Engine/UI/ECS/Components/UITransformComponent.h"
#include "Engine/UI/ECS/Components/UIProgressBarComponent.h"
#include "Engine/UI/ECS/Components/UITextComponent.h"
#include "Game/UI/MenuUiFactory.h" // SpawnCanvas / MakeUINode

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace CCL::ECS;

namespace
{
// HP比で色を変える（緑→黄→赤）。エディタのHP表と同系色。
DirectX::XMFLOAT4 HpColor(float ratio)
{
    if (ratio > 0.5f)  return {0.30f, 0.82f, 0.35f, 1.0f}; // 緑
    if (ratio > 0.25f) return {0.95f, 0.78f, 0.15f, 1.0f}; // 黄
    return {0.90f, 0.25f, 0.20f, 1.0f};                    // 赤
}
} // namespace

void PlayerHealthHudSystem::Initialize()
{
    // UI階層のルート。配下(ゲージ/ラベル)をこの Canvas の子にしてヒエラルキーで束ねる。
    const EntityID canvas = CCL::GameUI::SpawnCanvas(*m_world, "PlayerHpCanvas");

    // --- ゲージ本体（左上・アンカー方式） ---
    auto barArch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent,
        UIProgressBarComponent>();
    auto bar     = EntityFactory::SpawnNamed(*m_world, "PlayerHpGauge", barArch);

    UITransformComponent t;
    t.anchor = {0.0f, 0.0f};   // 画面左上
    t.pivot  = {0.0f, 0.0f};
    t.offset = {48.0f, 48.0f};
    t.size   = {420.0f, 28.0f};

    UIProgressBarComponent b;
    b.sortOrder  = 100;
    b.showTrail  = true;       // 遅延ダメージ（チップ）バーを有効化
    b.fillColor  = HpColor(1.0f);
    b.value      = 1.0f;
    b.trailValue = 1.0f;

    bar.Set(CCL::GameUI::MakeUINode(canvas)).Set(t).Set(b);
    m_barID = bar.ID();

    // --- ラベル（ゲージの上） ---
    auto labelArch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent,
        UITextComponent>();
    auto label     = EntityFactory::SpawnNamed(*m_world, "PlayerHpLabel", labelArch);

    UITransformComponent lt;
    lt.anchor = {0.0f, 0.0f};
    lt.pivot  = {0.0f, 0.0f};
    lt.offset = {48.0f, 24.0f};
    lt.size   = {420.0f, 20.0f};

    UITextComponent lx;
    lx.text      = "HP";
    lx.pixelSize = 18.0f;
    lx.sortOrder = 103; // フィル(102)より手前

    label.Set(CCL::GameUI::MakeUINode(canvas)).Set(lt).Set(lx);
    m_labelID = label.ID();
}

void PlayerHealthHudSystem::Update(float dt)
{
    // 生成直後はコンポーネントが未反映のことがあるので null 許容。
    auto *bar = (m_barID != InvalidEntityID) ? m_world->GetComponent<UIProgressBarComponent>(m_barID) : nullptr;
    auto *txt = (m_labelID != InvalidEntityID) ? m_world->GetComponent<UITextComponent>(m_labelID) : nullptr;
    if (!bar) return;

    bool found = false;
    ForEach([&](const HealthComponent &hp, const PlayerComponent & /*tag*/) {
        if (found) return; // プレイヤーは1体想定。最初の1つだけHUDへ。
        found = true;

        const float ratio = (hp.maxHealth > 0.0f)
                          ? std::clamp(hp.currentHealth / hp.maxHealth, 0.0f, 1.0f)
                          : 0.0f;

        // 初回は瞬時に合わせる（起動時にバーがスッと減って見えないように）。
        if (!m_init)
        {
            bar->trailValue = ratio;
            m_prevRatio      = ratio;
            m_init           = true;
        }

        // 本体フィルは現在HPへ即追従＋残量で色分け。
        bar->visible   = true;
        bar->value     = ratio;
        bar->fillColor = HpColor(ratio);

        // 被弾（HPが減った）を検出したら、遅延バーの「溜め」をリセットして失った分を見せる。
        if (ratio < m_prevRatio - 1e-4f) m_trailHold = kTrailHold;

        // 遅延バー：回復/据え置きは即追従、被弾直後は溜めのあと ease で value まで減っていく。
        if (ratio >= bar->trailValue)
        {
            bar->trailValue = ratio; // 増加・回復は差分を見せず即キャッチアップ
        }
        else if (m_trailHold > 0.0f)
        {
            m_trailHold -= dt;        // 溜め中は失った分を保持して見せる
        }
        else
        {
            const float k = CCL::Math::Damp(kTrailSpeed, dt);
            bar->trailValue += (ratio - bar->trailValue) * k;   // スッと value まで追いつく
        }

        if (txt)
        {
            txt->visible = true;
            char buf[32];
            const int cur = static_cast<int>(std::max(0.0f, hp.currentHealth) + 0.5f);
            const int max = static_cast<int>(hp.maxHealth + 0.5f);
            std::snprintf(buf, sizeof(buf), "HP  %d / %d", cur, max);
            txt->text = buf;
        }

        m_prevRatio = ratio;
    });

    // プレイヤーが居ないシーン（タイトル等）ではHUDを隠す。
    if (!found)
    {
        bar->visible = false;
        if (txt) txt->visible = false;
    }
}

REGISTER_LOGIC_SYSTEM(PlayerHealthHudSystem, Priority::LogicStage::L06_HudSync);
