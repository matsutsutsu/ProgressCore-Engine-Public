// AIの状態をキャラクター頭上に3Dテキストで表示するデバッグシステム。
//   1行目: [Combat]                … BossPhaseSystem が決めた大状態
//   2行目: ボス：移動               … BTが選んだ具体的アクション
//   3行目: HP 895/1000 Drone 100   … ステータス
//
// BossPhaseComponent と BehaviorTreeComponent を横断して読むので、どちらかの
// システムに間借りさせず独立させてある。
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Character/Boss/Components/BossPhaseComponent.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h"
#include "Engine/Transform/Components/TransformComponent.h"


class AIDebugOverlaySystem : public CCL::ECS::IfSystem<
    AIDebugOverlaySystem,
    CCL::ECS::Read<BossPhaseComponent>,
    CCL::ECS::Read<BehaviorTreeComponent>,
    CCL::ECS::Read<TransformComponent>
>
{
public:
    AIDebugOverlaySystem() : IfSystem("AIDebugOverlaySystem") {}

    // ImGui 前景リストへ直接描くので、System Manager のヘッダ開閉に関係なく
    // m_enabled が true の間ずっと表示される。
    void Update(float dt) override;

    /// System Manager 内に表示する操作UI。別ウィンドウは作らない。
    void OnGui() override;

private:
    bool  m_enabled    = true;
    //bool  m_enabled    = false; d
    float m_fontSize   = 25.0f; 
    float m_headOffsetY = 15.0f; // キャラ原点からの頭上ワールドYオフセット（本体に被らせないよう調整）
};
