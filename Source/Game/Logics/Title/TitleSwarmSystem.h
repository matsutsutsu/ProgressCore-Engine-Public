#pragma once
// ===========================================================================
// 概要: タイトル画面のドローンの群れ。ボス像の頭上に、本編と同じ円陣（HighOrbit）で並べる
//
// 責務:
//   - ボス像（TitleBossStatue）を探し、その頭上にドローンを生成して持ち場に置く
//   - 回り続ける円陣の持ち場と向きを毎フレーム書く
//
// 備考: 円陣の半径・高さ・回転の速さは Formations_Basic.h の HighRing をそのまま使い、本編と輪をそろえる
//       タイトルにはボスの BT も命令もないので、陣形（IFormation）は通さず targetPosition だけを書く
//       主役はボスとプレイヤーの構図なので、集まってくる演出はせず、最初から輪ができた状態で始める
//       TitleScene だけが登録する
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"

#include <SimpleMath.h>
#include <cstdint>

class TitleSwarmSystem : public CCL::ECS::IfSystem<
    TitleSwarmSystem,
    CCL::ECS::Write<DroneComponent>>
{
    using Base = CCL::ECS::IfSystem<
        TitleSwarmSystem,
        CCL::ECS::Write<DroneComponent>>;

public:
    TitleSwarmSystem() : IfSystem("TitleSwarmSystem") {}

    void Update(float dt) override;

    void OnGui() override;

    // 生成した直後に Transform の座標を書く。テンプレート引数に足すと、ForEach の対象まで変わる
    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override;

private:
    // ボス像の位置を返す。輪の中心はここで決まるので、像を動かせば輪も付いてくる
    // まだ生成されていなければ false を返し、次のフレームでもう一度探す
    bool ResolveBossPos(DirectX::SimpleMath::Vector3 &out);

    // 輪の上の持ち場（本編と同じ式）。回り続けるので毎フレーム変わる
    DirectX::SimpleMath::Vector3 SlotPosition(
        const DirectX::SimpleMath::Vector3 &bossPos, uint16_t index) const;

    // ドローンを生成し、その場で持ち場へ置く
    void SpawnSwarm(const DirectX::SimpleMath::Vector3 &bossPos);

    // 番号から 0〜1 の疑似乱数を作る。起動のたびに同じ絵になるよう、乱数生成器は使わない
    static float Hash01(uint32_t seed);

    static constexpr uint16_t kDroneCount = 50; // 群れの機数

    // 追従の個体差の組の大きさと、組の中で1つ後ろの番号ごとにバネを弱める割合
    // 後ろの機ほど遅れて付いてくる。本編の小隊（kSpringLagPerSlot）と同じ式・同じ値
    static constexpr uint16_t kLagGroupSize = 7;
    static constexpr float    kLagPerSeat   = 0.06f;

    float m_elapsed = 0.0f;  // 輪の回転に使う経過時間（秒）
    bool  m_spawned = false; // ドローンを生成したか

    CCL::ECS::EntityID m_bossID = CCL::ECS::InvalidEntityID; // ボス像。名前で探す
};
