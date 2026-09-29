#pragma once
// ===========================================================================
// 概要: ボスの AI の状態を、頭上にテキストで表示するデバッグ用の System（エディタのみ）。
//
// 責務:
//   - ボスの頭上の位置を画面座標に変換し、ImGui の前景描画リストへテキストを描く
//   - 1行目 [Combat]              … BossPhaseSystem が決めた形態
//     2行目 ボス：移動             … BT が最後に実行したアクション
//     3行目 HP 895/1000 Drone 100 … ボスの HP とドローンの残数
//   - OnGui で表示の ON/OFF・文字の大きさ・高さを変えられるようにする
//
// 備考: BossPhaseComponent と BehaviorTreeComponent の両方を読むので、どちらの System にも
//       入れず独立させている。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Character/Boss/Components/BossPhaseComponent.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h"
#include "Engine/Transform/Components/TransformComponent.h"

class AIDebugOverlaySystem : public CCL::ECS::IfSystem<
                                 AIDebugOverlaySystem,
                                 CCL::ECS::Read<BossPhaseComponent>,
                                 CCL::ECS::Read<BehaviorTreeComponent>,
                                 CCL::ECS::Read<TransformComponent>>
{
public:
    AIDebugOverlaySystem() : IfSystem("AIDebugOverlaySystem") {}

    // ImGui 前景リストへ直接描くので、System Manager のヘッダ開閉に関係なく
    // m_enabled が true の間ずっと表示される。
    void Update(float dt) override;

    // System Manager 内に表示する操作 UI。別ウィンドウは作らない。
    void OnGui() override;

private:
    bool  m_enabled     = true;
    float m_fontSize    = 25.0f; // 文字の大きさ（ピクセル）
    float m_headOffsetY = 15.0f; // キャラの原点から頭上までのワールド Y オフセット（m）。体に重ならない高さにする
};
