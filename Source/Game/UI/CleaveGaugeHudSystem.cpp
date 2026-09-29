#include "CleaveGaugeHudSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/Common/CCL_Archetype.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Serialization/Factory/EntityFactory.h"

#include "Engine/Transform/Components/TransformComponent.h" // UI の親子関係に使う
#include "Engine/UI/ECS/Components/UITransformComponent.h"
#include "Engine/UI/ECS/Components/UIProgressBarComponent.h"
#include "Engine/UI/ECS/Components/UITextComponent.h"
#include "Game/UI/MenuUiFactory.h" // SpawnCanvas / MakeUINode

#include <cstdio>

using namespace CCL::ECS;

namespace
{
    const DirectX::XMFLOAT4 kColReady  = {1.0f, 0.843f, 0.235f, 1.0f}; // ストックがあるとき（金）
    const DirectX::XMFLOAT4 kColCharge = {0.275f, 0.667f, 1.0f, 1.0f}; // ストックが 0 でためているとき（青）
} // namespace

void CleaveGaugeHudSystem::Initialize()
{
    // ゲージとラベルはこの Canvas の子にする
    const EntityID canvas = CCL::GameUI::SpawnCanvas(*m_world, "CleaveCanvas");

    // --- ゲージ（画面左下。次のストックまでのたまり具合） ---
    auto gaugeArch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent,
                                               UIProgressBarComponent>();
    auto gauge     = EntityFactory::SpawnNamed(*m_world, "CleaveGauge", gaugeArch);

    UITransformComponent gt;
    gt.anchor = {0.0f, 1.0f};    // 画面の左下
    gt.pivot  = {0.0f, 0.0f};    // 自分の左上をアンカーに合わせる
    gt.offset = {48.0f, -72.0f}; // 左から 48px、下端から 72px 上
    gt.size   = {340.0f, 24.0f};

    UIProgressBarComponent gb;
    gb.fillColor = kColCharge;
    gb.sortOrder = 100;
    gb.gradient  = 0.45f;

    gauge.Set(CCL::GameUI::MakeUINode(canvas)).Set(gt).Set(gb);
    m_gaugeID = gauge.ID();

    // --- ラベル（ゲージの上。ストック数） ---
    auto labelArch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent,
                                               UITextComponent>();
    auto label     = EntityFactory::SpawnNamed(*m_world, "CleaveLabel", labelArch);

    UITransformComponent lt;
    lt.anchor = {0.0f, 1.0f};
    lt.pivot  = {0.0f, 0.0f};
    lt.offset = {48.0f, -96.0f}; // ゲージの上
    lt.size   = {340.0f, 20.0f};

    UITextComponent lx;
    lx.text      = "CLEAVE";
    lx.pixelSize = 18.0f;
    lx.sortOrder = 110; // ゲージは sortOrder から +2 までの層を使うので、それより手前に置く

    label.Set(CCL::GameUI::MakeUINode(canvas)).Set(lt).Set(lx);
    m_labelID = label.ID();
}

void CleaveGaugeHudSystem::Update(float /*dt*/)
{
    // 生成した直後はコンポーネントがまだ反映されていないことがあるので、null でも続ける
    auto *bar = (m_gaugeID != InvalidEntityID) ? m_world->GetComponent<UIProgressBarComponent>(m_gaugeID) : nullptr;
    auto *txt = (m_labelID != InvalidEntityID) ? m_world->GetComponent<UITextComponent>(m_labelID) : nullptr;
    if (!bar && !txt) return;

    bool found = false;
    ForEach([&](const CleaveComponent &c) {
        if (found) return; // プレイヤーは1体の前提。最初の1つだけを表示する
        found = true;

        const float ratio = (c.maxGauge > 0.0f) ? (c.gauge / c.maxGauge) : 0.0f;

        if (bar)
        {
            bar->visible   = true;
            bar->value     = ratio;
            bar->fillColor = (c.stock > 0) ? kColReady : kColCharge;
        }
        if (txt)
        {
            txt->visible = true;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "CLEAVE  x%d", c.stock);
            txt->text = buf;
        }
    });

    // CleaveComponent を持つエンティティが無ければ隠す
    if (!found)
    {
        if (bar) bar->visible = false;
        if (txt) txt->visible = false;
    }
}

REGISTER_LOGIC_SYSTEM(CleaveGaugeHudSystem, Priority::LogicStage::L06_HudSync);
