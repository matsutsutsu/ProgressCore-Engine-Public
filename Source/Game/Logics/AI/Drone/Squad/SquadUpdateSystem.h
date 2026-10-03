#pragma once
// ===========================================================================
// 概要: 小隊（5機編隊）のアンカーを動かす System。小隊単位の判断はここに集める。
//
// 責務:
//   - 各小隊の生存数と、各席の前フレームの位置・速度を集める
//   - フェーズ（Regroup / Approach / Strike）を遷移させる
//   - アンカーの位置・速度・向きを更新し、床より下へ沈ませない
//
// 備考:
//   ドローン系 System の分担は次のとおり。
//     DroneCommandSystem   … ボスの命令を各機へ配る
//     SquadUpdateSystem    … 小隊のアンカーをどこへ動かすか決める（この System）
//     DroneFormationSystem … アンカーを起点に各機の持ち場を出す
//     DroneBrainSystem     … 個体が今どうすべきか決める
//     DroneMovementSystem  … 実際に動かす
//
//   独立した System にしているのは、小隊が最大32個なのに対しドローンは1000機あるため。
//   機体ごとのループで行うと同じ計算を5回繰り返し、同じ小隊へ並列に書き込むことになる。
//
//   単スレッドで回し、SquadState への書き込みはここだけに限る。
// ===========================================================================
#include <vector>

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/ECS/Common/CCL_TypeInfo.h"
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"
#include "Game/Logics/AI/Drone/Squad/SquadState.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"

class SquadUpdateSystem : public CCL::ECS::IfSystem<
                              SquadUpdateSystem,
                              CCL::ECS::Read<DroneComponent>,
                              CCL::ECS::Read<TransformComponent>>
{
    using Base = CCL::ECS::IfSystem<SquadUpdateSystem,
                                    CCL::ECS::Read<DroneComponent>,
                                    CCL::ECS::Read<TransformComponent>>;

public:
    SquadUpdateSystem()
        : IfSystem("SquadUpdateSystem")
    {
    }
    ~SquadUpdateSystem() override = default;

    void Initialize() override;

    // DroneCommandSystem が配った陣形指示を読んでから小隊の行動を決める。BehaviorTreeSystem も直接
    // 名指しする（間接的に後ろになるだけでは起動時に「順序不定」と報告される）。
    // DroneManagerSystem は初回配備で squadId / slotInSquad を書くので、その後に走る。
    std::vector<const char *> GetRunAfter() const override
    {
        return {"BehaviorTreeSystem", "DroneCommandSystem", "DroneManagerSystem"};
    }

    // Update がボスの BossCommandComponent を View で引く。テンプレート引数には
    // 足せない（ForEach の絞り込み条件を兼ねているため）。
    std::vector<CCL::ECS::TypeID> GetReadTypes() const override
    {
        auto types = Base::GetReadTypes();
        types.push_back(CCL::ECS::TypeInfo<BossCommandComponent>::ID());
        return types;
    }

    void Update(float dt) override;

private:
    // SquadRegistry が未登録なら作る。World を作り直しても復帰できるよう Update からも
    // 毎フレーム呼ぶ。データを持つ側が自分で用意し、シーン側に依存させない。
    void EnsureRegistry();

    // 小隊1個ぶんのフェーズ遷移とアンカーの移動。
    // rank は生きている小隊の中での順番（0〜activeCount-1）。並ぶ角度と段をこれで決める。
    void UpdateSquad(SquadState                         &squad,
                     uint8_t                             rank,
                     uint8_t                             activeCount,
                     bool                                ordered,
                     bool                                attacking,
                     const DirectX::SimpleMath::Vector3 &bossPos,
                     const DirectX::SimpleMath::Vector3 &playerPos,
                     float                               chaseSpeed,
                     const SquadTuning                  &tuning,
                     float                               dt);

    // アンカーの向きをプレイヤーへ曲げる（旋回上限つき）。無制限に追尾させると、避けても
    // 曲がって当たる攻撃になる。上限があれば、至近距離で鋭く切り返されたときに曲がりきれず外れる。
    void SteerTowardPlayer(SquadState                         &squad,
                           const DirectX::SimpleMath::Vector3 &playerPos,
                           const SquadTuning                  &tuning,
                           float                               dt) const;

    // アンカーが床へ沈まないように高度を止める。全フェーズの移動計算の最後に必ず通す。
    // 位置だけでなく速度と向きの下向き成分も抜くこと（追従機は「1つ前の機体の速度」を
    // 先渡しで受け取るので、アンカーが下を向いたままだと後続機が床へ押し込まれ続ける）。
    void ClampAltitude(SquadState &squad, const SquadTuning &tuning) const;

    // 待機時にボス頭上のどの高さへ置くか [m]。地上の間合いを塞がない高さにする。
    static constexpr float kRegroupHeight = 22.0f;

    // 待機時のボス中心からの水平距離 [m]。小隊ごとに角度をずらして並ぶ。
    static constexpr float kRegroupRadius = 18.0f;

    // 展開位置を左右に揺らす速さ [rad/s] と振れ幅 [rad]。止まって見えないようにする。
    // 一方向へ回し続けると、扇の中心（ボスの方向）からずれていき、小隊が画面の外へ出ていく。
    static constexpr float kApproachDrift = 0.35f;
    static constexpr float kApproachSway  = 0.2f;

    // アンカーを目標へ寄せるバネの強さと減衰、最高速度 [m/s]（追跡より遅くして緩急を作る）。
    static constexpr float kAnchorGain = 1.8f;

    static constexpr float kAnchorDamping = 3.0f;

    static constexpr float kCruiseSpeed = 26.0f;

    // 待機・展開位置をゆっくり回すのに使う経過時間 [秒]。
    float m_elapsed = 0.0f;
};
