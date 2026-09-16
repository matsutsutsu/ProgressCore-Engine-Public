// 役割・設計方針は CleaveGaugeHudSystem.h を参照。
#include "CleaveGaugeHudSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/Common/CCL_Archetype.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Serialization/Factory/EntityFactory.h"

#include "Engine/Transform/Components/TransformComponent.h" // UI階層（親子）
#include "Engine/UI/ECS/Components/UITransformComponent.h"
#include "Engine/UI/ECS/Components/UIProgressBarComponent.h"
#include "Engine/UI/ECS/Components/UITextComponent.h"
#include "Game/UI/MenuUiFactory.h" // SpawnCanvas / MakeUINode

#include <cstdio>

using namespace CCL::ECS;

namespace
{
    const DirectX::XMFLOAT4 kColReady  = {1.0f, 0.843f, 0.235f, 1.0f}; // ストックあり：ゴールド
    const DirectX::XMFLOAT4 kColCharge = {0.275f, 0.667f, 1.0f, 1.0f}; // ストック0で蓄積中：ブルー
} // namespace

void CleaveGaugeHudSystem::Initialize()
{
    // UI階層のルート。配下(ゲージ/ラベル)をこの Canvas の子にしてヒエラルキーで束ねる。
    const EntityID canvas = CCL::GameUI::SpawnCanvas(*m_world, "CleaveCanvas");

    // --- ゲージ本体（左下・アンカー方式。次のストックまでの蓄積を表示） ---
    auto gaugeArch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent,
                                               UIProgressBarComponent>();
    auto gauge     = EntityFactory::SpawnNamed(*m_world, "CleaveGauge", gaugeArch);

    UITransformComponent gt;
    gt.anchor = {0.0f, 1.0f};    // 画面左下
    gt.pivot  = {0.0f, 0.0f};    // 自身の左上をアンカーに合わせる
    gt.offset = {48.0f, -72.0f}; // 左から48px / 下端から72px上
    gt.size   = {340.0f, 24.0f};

    UIProgressBarComponent gb;
    gb.fillColor = kColCharge;
    gb.sortOrder = 100;
    gb.gradient  = 0.45f;

    gauge.Set(CCL::GameUI::MakeUINode(canvas)).Set(gt).Set(gb);
    m_gaugeID = gauge.ID();

    // --- ラベル（ゲージの上。ストック数を表示） ---
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
    lx.sortOrder = 110; // ゲージは sortOrder+4 まで使うので、被らない位置へ

    label.Set(CCL::GameUI::MakeUINode(canvas)).Set(lt).Set(lx);
    m_labelID = label.ID();
}

void CleaveGaugeHudSystem::Update(float /*dt*/)
{
    // 生成直後はコンポーネントが未反映のことがあるので null 許容。
    auto *bar = (m_gaugeID != InvalidEntityID) ? m_world->GetComponent<UIProgressBarComponent>(m_gaugeID) : nullptr;
    auto *txt = (m_labelID != InvalidEntityID) ? m_world->GetComponent<UITextComponent>(m_labelID) : nullptr;
    if (!bar && !txt) return;

    bool found = false;
    ForEach([&](const CleaveComponent &c) {
        if (found) return; // プレイヤーは1体想定。最初の1つだけHUDへ。
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

    // Cleave を持つエンティティがまだ無ければHUDを隠す。
    if (!found)
    {
        if (bar) bar->visible = false;
        if (txt) txt->visible = false;
    }
}

REGISTER_LOGIC_SYSTEM(CleaveGaugeHudSystem, Priority::LogicStage::L06_HudSync);
