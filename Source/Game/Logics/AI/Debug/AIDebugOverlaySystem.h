#pragma once
// ===========================================================================
// 概要: ボスの AI の状態を、頭上にテキストで表示するデバッグ用の System（エディタのみ）。
//
// 責務:
//   - ボスの頭上の位置を画面座標に変換し、ImGui の前景描画リストへテキストを描く
//   - 1行目 [Combat]              … BossPhaseSystem が決めた形態
//     2行目 ボス：移動             … BT が今 Running にしている行動
//     3行目 HP 895/1000 Drone 100 … ボスの HP とドローンの残数
//   - ボスとプレイヤーの中点に、今の距離を表示する（BT の距離条件が比べている値そのもの）。
//     数字は今いる帯（AIDebugSpatialSystem が地面に塗る帯）と同じ色で塗る
//   - OnGui で表示の ON/OFF・文字の大きさ・高さを変えられるようにする
//
// 備考: BossPhaseComponent と BehaviorTreeComponent の両方を読むので、どちらの System にも
//       入れず独立させている。
// ===========================================================================
#include <vector>

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/Debug/AIDebugRanges.h"
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

    // プレイヤーとの距離 1m あたりに足す頭上の高さ（m）。文字の大きさは画面上で一定なのに、
    // 遠いとボスは小さく映り、同じ高さだと文字の枠がボスに被る。離れるほど持ち上げてかわす。
    float m_headOffsetPerMeter = 0.12f;

    // ボスとプレイヤーの距離。AIDebugSpatialSystem の照準線の中ほどに出るので、線と合わせて読める。
    bool  m_showDistance     = true;
    float m_distanceFontSize = 30.0f; // 戦闘中に一目で読めるよう、頭上の文字より少し大きくする
    float m_distanceOffsetY  = 1.0f;  // 中点から持ち上げる高さ（m）。照準線と重ならないようにする

    // 距離の数字の色を決めるための間合いのリング。毎フレーム木から拾い直す（配列の確保は使い回す）。
    std::vector<AIDebugRanges::RangeRing> m_rings;
    std::vector<float>                    m_edgeRings; // CollectRings が一緒に集める。ここでは使わない
};
