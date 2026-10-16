#pragma once
// ===========================================================================
// 概要: 発表用デモ。隊長機1機を、大量のドローンが群がって追いかける System（L02_Update）。
//
// 責務:
//   - 隊長機を、群れの中心点へ一定の強さで引き寄せて飛ばす（ブレーキ無しなので大きくループする）
//   - 残りの全機を、隊長機の周りの目標点へ一定の強さで引き寄せて飛ばす
//   - 更新にかかった時間を測り、デモシーンの表示へ渡す
//
// 備考:
//   隊長機は通し番号 0 の機体。状態はこの System が持ち、並列ループの前に1回だけ進める。
//   各機が読むのは隊長機の位置（ループ前に確定済みの値）だけで、書くのは自分の
//   Transform と速度だけ。そのため Chunk 単位で Job に分けても競合せず、
//   ForEachParallel にそのまま乗る。
//
//   一般機が全員「隊長機の1点」を目指すと、同じ力を受け続けて向きと速さがそろい、
//   時間がたつと1つの塊になって動く。それを避けるため、一般機ごとに「隊長機の周りを
//   回り続ける自分専用の目標点」を持たせる（useOrbitTarget で切り替えられる）。
//
//   DemoSwarmScene だけが登録する（REGISTER_LOGIC_SYSTEM で全体登録はしない）。
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

// 群れの動きの調整値。デモシーンの ImGui から実行中に書き換える。
// 長さと強さは「最初の塊の一辺の長さ」に対する割合で持つ（機数を変えても同じ見た目にするため）。
//
// 初期値は参考にした ECS デモ（Tools/ECSDemo）と同じ動きになる値。あちらは一辺 4.9m の塊で、
// 隊長機を 1.0m/s²・一般機を 2.0m/s² で引き、隊長機の初速は「水平の位置 ×3」。
// それを一辺に対する割合に直したもの。空気抵抗は無く、全員が隊長機そのものを目指す
struct DemoSwarmTuning
{
    float leaderPull   = 1.0f / 4.9f;  // 隊長機 → 中心点の引き寄せ [一辺/s²]
    float leaderOrbit  = 25.46f;       // 隊長機の初速。1 でほぼ円、大きいほど遠くまで膨らむ（作り直したときに効く）
                                       // 25.46 で「水平の位置 ×3」と同じ速さになる
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

    /**
     * @brief 通し番号 index の機体の、最初の位置と速度を返す。
     *
     * 全機を立方体の塊に並べ、中心から外向きに飛び出させる（最初に塊が弾けて見える）。
     * 隊長機（index 0）だけは塊の角に置き、中心点の周りを回る向きへ飛ばす。
     *
     * @param index   群れの中での通し番号（0 〜 count-1）
     * @param count   群れの機数
     * @param outPos  最初の位置
     * @param outVel  最初の速度
     */
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
