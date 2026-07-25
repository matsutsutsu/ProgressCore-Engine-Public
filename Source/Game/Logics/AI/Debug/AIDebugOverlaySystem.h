/// @file  AIDebugOverlaySystem.h
/// @brief AIの状態（StateTree戦術 + BT行動）をキャラクター頭上に3Dテキストで
///        表示する専用デバッグシステム。
///
/// 設計方針:
///   StateTreeComponent と BehaviorTreeComponent の両方を横断して読む必要があるため、
///   どちらか片方のシステムに間借りさせず、専用システムとして独立させる
///   （BehaviorTreeSystemはBT評価に、BossActionSystemは物理移動に責務を限定するため）。
///
/// 表示内容（頭上2行）:
///   1行目: [戦術: Pressure]   … 司令官StateTreeが選んだ戦術
///   2行目: [行動: ボス：移動]  … BTが選んだ具体的アクション
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/StateTree/Core/StateTreeComponent.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h"
#include "Engine/Transform/Components/TransformComponent.h"


class AIDebugOverlaySystem : public CCL::ECS::IfSystem<
    AIDebugOverlaySystem,
    CCL::ECS::Read<StateTreeComponent>,
    CCL::ECS::Read<BehaviorTreeComponent>,
    CCL::ECS::Read<TransformComponent>
>
{
public:
    AIDebugOverlaySystem() : IfSystem("AIDebugOverlaySystem") { hasGui = true; }

    // ★レンダーシステムとして登録し、ImGui::NewFrame の後に走る Update で頭上テキストを
    //   ImGui 前景リストへ直接描く。これにより System Manager のヘッダ開閉に関係なく
    //   （＝フラグ _enabled が true の間）常時表示される。
    void Update(float dt) override;

    // System Manager 内に表示する操作 UI（ON/OFF・フォントサイズ）。別ウィンドウは作らない。
    void OnGui() override;

private:
    bool  _enabled    = true;
    float _fontSize   = 25.0f;
    float _headOffsetY = 15.0f; ///< キャラ原点からの頭上ワールドYオフセット（本体に被らせないよう調整）
};
