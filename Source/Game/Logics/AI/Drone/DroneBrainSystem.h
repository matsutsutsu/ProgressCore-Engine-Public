/// @file  DroneBrainSystem.h
/// @brief ドローン個体の判断層。DroneComponent.currentState を決めるだけのシステム。
///
/// 判断と移動の分離は維持している:
///   判断層（このファイル）        … 「今このドローンは何をすべきか」を決める
///   実行層（DroneMovementSystem） … currentState を見て物理的に動かす
/// ここは分岐と代入のみで、補間や速度計算（Lerp/Slerp等）には一切関与しない。
///
/// 汎用のステートマシン機構（ツリー＋ノード登録）を使わない理由:
///   状態が3つで固定なので木構造の可読性が効かず、振る舞いの編集も結局は
///   C++ の再ビルドになるためデータ駆動の利点も出ない。加えてノード評価ごとに
///   world->GetComponent<> のランダムアクセスが入り、1000機×毎フレームでは
///   ECS の連続配置が判断層でだけ崩れる。
///   ボス1体のような「複雑で調整したい判断」とはコスト構造が逆になる、が線引き。
///
/// 実行順序: L02_PreUpdate。DroneCommandSystem / DroneFormationSystem（L02_Update）
///   より前に置くことで、陣形側が「今フレームの currentState」を見て形を作れる。
///   ここを動かすと予備動作→突撃の切り替わりが1フレームずれる。
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Time/TimeState.h"
#include <SimpleMath.h>

class DroneBrainSystem : public CCL::ECS::IfSystem<
                             DroneBrainSystem,
                             CCL::ECS::Read<TransformComponent>,
                             CCL::ECS::Write<DroneComponent>,
                             CCL::ECS::Read<TimeStateComponent>>
{
  public:
    DroneBrainSystem()
        : IfSystem("DroneBrainSystem")
    {
    }
    virtual ~DroneBrainSystem() override = default;

    virtual void Update(float dt) override;

  private:
    /// @brief 陣形スロットへの初回接近がまだ必要か。
    /// @note  毎フレーム動く targetPosition（円陣回転）との距離だけで判定すると、
    ///        Idle の追従で閾値以内に収まり MoveToTarget が1フレームだけ点滅する。
    ///        初回到着を hasReachedSlot にラッチし、接近フェーズと維持フェーズを分離する。
    bool NeedsApproach(const TransformComponent &trans, DroneComponent &drone) const;

    /// @brief FireCharge へ「入った瞬間」だけ走る初期化（タメ時間と突撃方向のロック）。
    /// @note  ここで方向を固定するので、DroneMovementSystem の「タメ→リリース」が
    ///        コミットした直進として成立する（ホーミングしない）。
    void BeginCharge(DroneComponent &drone,
                     const DirectX::SimpleMath::Vector3 &playerPos,
                     bool hasPlayer) const;

    /// @brief この距離以内に入ったら陣形スロットへ初回到着したとみなす。
    static constexpr float kArriveThreshold = 2.5f;

    /// @brief 突撃前のタメ時間。ProcessFireCharge が stateTimer>0 を予備動作として扱う。
    static constexpr float kChargeWindup = 0.4f;
};
