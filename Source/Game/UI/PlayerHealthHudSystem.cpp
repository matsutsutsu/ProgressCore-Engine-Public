#include "PlayerHealthHudSystem.h"

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
#include <cstdio>

using namespace CCL::ECS;

namespace
{
    // HP の割合で色を変える（50% より上は緑、25% より上は黄、それ以下は赤）
    DirectX::XMFLOAT4 HpColor(float ratio)
    {
        if (ratio > 0.5f) return {0.30f, 0.82f, 0.35f, 1.0f};
        if (ratio > 0.25f) return {0.95f, 0.78f, 0.15f, 1.0f};
        return {0.90f, 0.25f, 0.20f, 1.0f};
    }
} // namespace

void PlayerHealthHudSystem::Initialize()
{
    // ゲージとラベルはこの Canvas の子にする
    const EntityID canvas = CCL::GameUI::SpawnCanvas(*m_world, "PlayerHpCanvas");

    // --- ゲージ（画面左上） ---
    auto barArch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent,
                                             UIProgressBarComponent>();
    auto bar     = EntityFactory::SpawnNamed(*m_world, "PlayerHpGauge", barArch);

    UITransformComponent t;
    t.anchor = {0.0f, 0.0f}; // 画面の左上
    t.pivot  = {0.0f, 0.0f};
    t.offset = {48.0f, 48.0f};
    t.size   = {420.0f, 28.0f};

    UIProgressBarComponent b;
    b.sortOrder  = 100;
    b.showTrail  = true; // 失った分を遅れて減らすバーを出す
    b.fillColor  = HpColor(1.0f);
    b.gradient   = 0.45f; // 下端を暗くして、一色の塗りに見えないようにする
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
    lx.sortOrder = 110; // ゲージは sortOrder から +2 までの層を使うので、それより手前に置く

    label.Set(CCL::GameUI::MakeUINode(canvas)).Set(lt).Set(lx);
    m_labelID = label.ID();
}

void PlayerHealthHudSystem::Update(float dt)
{
    // 生成した直後はコンポーネントがまだ反映されていないことがあるので、null なら何もしない
    auto *bar = (m_barID != InvalidEntityID) ? m_world->GetComponent<UIProgressBarComponent>(m_barID) : nullptr;
    auto *txt = (m_labelID != InvalidEntityID) ? m_world->GetComponent<UITextComponent>(m_labelID) : nullptr;
    if (!bar) return;

    bool found = false;
    ForEach([&](const HealthComponent &hp, const PlayerComponent & /*tag*/) {
        if (found) return; // プレイヤーは1体の前提。最初の1つだけを表示する
        found = true;

        const float ratio = (hp.maxHealth > 0.0f)
                                ? std::clamp(hp.currentHealth / hp.maxHealth, 0.0f, 1.0f)
                                : 0.0f;

        // 最初は遅れて減るバーもすぐに合わせる（起動した瞬間にバーが減っていくように見えないように）
        if (!m_init)
        {
            bar->trailValue = ratio;
            m_prevRatio     = ratio;
            m_init          = true;
        }

        bar->visible   = true;
        bar->value     = ratio;
        bar->fillColor = HpColor(ratio);

        // HP が減ったら、遅れて減るバーを少し止めて失った量を見せ、ゲージを揺らして当たったことを伝える
        if (ratio < m_prevRatio - 1e-4f)
        {
            m_trailHold = kTrailHold;
            m_shake     = 1.0f;
            m_shakeTime = 0.0f; // 毎回同じ揺れ方から始める
        }

        // 揺れを減らすのはこの System。ウィジェットは揺れの量を持つだけ
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

        if (txt)
        {
            txt->visible = true;
            char      buf[32];
            const int cur = static_cast<int>(std::max(0.0f, hp.currentHealth) + 0.5f);
            const int max = static_cast<int>(hp.maxHealth + 0.5f);
            std::snprintf(buf, sizeof(buf), "HP  %d / %d", cur, max);
            txt->text = buf;
        }

        m_prevRatio = ratio;
    });

    // プレイヤーが居ないシーン（タイトルなど）では隠す
    if (!found)
    {
        bar->visible = false;
        m_shake      = 0.0f;
        if (txt) txt->visible = false;
    }
}

REGISTER_LOGIC_SYSTEM(PlayerHealthHudSystem, Priority::LogicStage::L06_HudSync);
