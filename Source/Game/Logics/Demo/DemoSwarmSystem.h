#pragma once
// ===========================================================================
// 概要: 発表用デモ。隊長機1機を、大量のドローンが群がって追いかける System（L02_Update）
//
// 責務:
//   - 隊長機を、群れの中心点へ一定の強さで引き寄せて飛ばす（ブレーキなしなので大きくループする）
//   - 残りの全機を、隊長機の周りの目標点へ一定の強さで引き寄せて飛ばす
//   - 更新にかかった時間を測り、デモシーンの表示へ渡す
//
// 備考: 隊長機は通し番号 0 の機体。状態はこの System が持ち、並列ループの前に1回だけ進める
//       各機は確定済みの隊長機の位置を読み、自分の Transform と速度だけを書くので、並列でも競合しない
//       全員が隊長機の1点を目指すと1つの塊になるので、useOrbitTarget で機体ごとの目標点を持たせられる
//       DemoSwarmScene だけが登録する
// ===========================================================================
#include <cstdint>

#include "SimpleMath.h"

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"

// デモ用ドローン1機ぶんのデータ
struct DemoSwarmComponent
{
    uint32_t          index = 0;         // 群れの中での通し番号。0 が隊長機
    DirectX::XMFLOAT3 velocity{0, 0, 0}; // 速度 [m/s]（隊長機は使わない。System が持つ）
};

// 群れの動きの調整値。デモシーンの ImGui から実行中に書き換える
// 長さと強さは最初の塊の一辺に対する割合で持ち、機数を変えても同じ見た目にする
// 初期値は Tools/ECSDemo と同じ動きになる値（一辺 4.9m、隊長機 1.0m/s²・一般機 2.0m/s²）
struct DemoSwarmTuning
{
    float leaderPull   = 1.0f / 4.9f;  // 隊長機 → 中心点の引き寄せ [一辺/s²]
    float leaderOrbit  = 25.46f;       // 隊長機の初速。1 でほぼ円、大きいほど膨らむ。25.46 で ECSDemo と同じ
    float followerPull = 2.0f / 4.9f;  // 一般機 → 目標点の引き寄せ [一辺/s²]
    float followerDrag = 0.0f;         // 一般機の空気抵抗 [1/s]

    bool  useOrbitTarget = false; // true なら、隊長機の周りを回る自分専用の点を目指す
    float orbitRadiusMin = 0.4f; // 目標点が隊長機から離れる距離 [一辺]。機体ごとにこの範囲でばらす
    float orbitRadiusMax = 1.2f;
    float orbitSpeedMin  = 0.3f; // 目標点が隊長機の周りを回る速さ [rad/s]。機体ごとにばらし、向きも半々に分ける
    float orbitSpeedMax  = 0.8f;
};

class DemoSwarmSystem : public CCL::ECS::IfSystem<
    DemoSwarmSystem,
    CCL::ECS::Write<TransformComponent>,
    CCL::ECS::Write<DemoSwarmComponent>>
{
public:
    DemoSwarmSystem() : IfSystem("DemoSwarmSystem") {}

    void Update(float dt) override;

    // 通し番号 index の機体の、最初の位置と速度を返す
    // 全機を立方体に並べて外向きに飛ばし、最初に塊が弾けて見えるようにする
    // 隊長機（index 0）だけは角に置き、中心の周りを回る向きへ飛ばす
    void InitialState(uint32_t index, uint32_t count,
                      DirectX::SimpleMath::Vector3 &outPos,
                      DirectX::SimpleMath::Vector3 &outVel) const;

    // 群れを count 機で最初の状態に戻す。シーンが生成のたびに呼ぶ
    void Reset(uint32_t count);

    uint32_t GetDroneCount() const { return m_droneCount; }

    // 群れの更新（ForEachParallel 1回）にかかった時間 [ms]。表示用になだらかにしてある
    float GetUpdateMs() const { return m_updateMs; }

    // 処理対象の Chunk 数。並列になるかの判定（コア数×2 以上か）を表示するために使う
    size_t GetChunkCount() const { return m_targetChunks.size(); }

    DemoSwarmTuning tuning; // 動きの調整値（デモシーンが ImGui で書き換える）

private:
    uint32_t m_droneCount = 0;
    float    m_elapsed    = 0.0f; // 目標点を回すための時計 [s]

    // 隊長機の状態
    DirectX::SimpleMath::Vector3 m_leaderPos;
    DirectX::SimpleMath::Vector3 m_leaderVel;

    float m_updateMs = 0.0f;
};
