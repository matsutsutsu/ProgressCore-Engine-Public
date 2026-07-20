/// @file OverdriveHudSystem.cpp
/// 役割・設計方針は OverdriveHudSystem.h を参照。
#include "OverdriveHudSystem.h"

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

using namespace CCL::ECS;

namespace
{
// 旧 OnGui の色（IM_COL32 8bit）を 0..1 に変換したもの。
const DirectX::XMFLOAT4 kColActive = {1.0f, 0.275f, 0.863f, 1.0f}; // 発動中：マゼンタ
const DirectX::XMFLOAT4 kColFull   = {1.0f, 0.843f, 0.235f, 1.0f}; // MAX：ゴールド
const DirectX::XMFLOAT4 kColCharge = {0.275f, 0.667f, 1.0f, 1.0f}; // 蓄積中：ブルー
} // namespace

void OverdriveHudSystem::Initialize()
{
    // UI階層のルート。配下(ゲージ/ラベル)をこの Canvas の子にしてヒエラルキーで束ねる。
    const EntityID canvas = CCL::GameUI::SpawnCanvas(*_world, "OverdriveCanvas");

    // --- ゲージ本体（左下・アンカー方式） ---
    auto gaugeArch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent,
        UIProgressBarComponent>();
    auto gauge     = EntityFactory::SpawnNamed(*_world, "OverdriveGauge", gaugeArch);

    UITransformComponent gt;
    gt.anchor = {0.0f, 1.0f};   // 画面左下
    gt.pivot  = {0.0f, 0.0f};   // 自身の左上をアンカーに合わせる
    gt.offset = {48.0f, -72.0f}; // 左から48px / 下端から72px上
    gt.size   = {340.0f, 24.0f};

    UIProgressBarComponent gb;
    gb.fillColor = kColCharge;
    gb.sortOrder = 100;

    gauge.Set(CCL::GameUI::MakeUINode(canvas)).Set(gt).Set(gb);
    m_gaugeID = gauge.ID();

    // --- ラベル（ゲージの上） ---
    auto labelArch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent,
        UITextComponent>();
    auto label     = EntityFactory::SpawnNamed(*_world, "OverdriveLabel", labelArch);

    UITransformComponent lt;
    lt.anchor = {0.0f, 1.0f};
    lt.pivot  = {0.0f, 0.0f};
    lt.offset = {48.0f, -96.0f}; // ゲージの上
    lt.size   = {340.0f, 20.0f};

    UITextComponent lx;
    lx.text      = "OVERDRIVE";
    lx.pixelSize = 18.0f;
    lx.sortOrder = 102;

    label.Set(CCL::GameUI::MakeUINode(canvas)).Set(lt).Set(lx);
    m_labelID = label.ID();
}

void OverdriveHudSystem::Update(float /*dt*/)
{
    // 生成直後はコンポーネントが未反映のことがあるので null 許容。
    auto *bar = (m_gaugeID != InvalidEntityID) ? _world->GetComponent<UIProgressBarComponent>(m_gaugeID) : nullptr;
    auto *txt = (m_labelID != InvalidEntityID) ? _world->GetComponent<UITextComponent>(m_labelID) : nullptr;
    if (!bar && !txt) return;

    bool found = false;
    ForEach([&](const OverdriveComponent &od) {
        if (found) return; // プレイヤーは1体想定。最初の1つだけHUDへ。
        found = true;

        const float ratio = (od.maxGauge > 0.0f) ? (od.currentGauge / od.maxGauge) : 0.0f;

        if (bar)
        {
            bar->visible   = true;
            bar->value     = ratio;
            bar->fillColor = od.isActive ? kColActive : (od.IsFull() ? kColFull : kColCharge);
        }
        if (txt)
        {
            txt->visible = true;
            txt->text    = od.isActive ? "OVERDRIVE!!"
                         : (od.IsFull() ? "OVERDRIVE READY  [Q]" : "OVERDRIVE");
        }
    });

    // Overdrive を持つエンティティがまだ無ければHUDを隠す。
    if (!found)
    {
        if (bar) bar->visible = false;
        if (txt) txt->visible = false;
    }
}

REGISTER_LOGIC_SYSTEM(OverdriveHudSystem, Priority::LogicStage::L02_PostUpdate);
