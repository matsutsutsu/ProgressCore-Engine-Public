// 振付師が決定した目標座標へ向かってドローンを物理的に移動・回転させるシステム。
//
// 状態は「基礎ループ3つ」＋「個体が自分で入る2つ」で構成する。
//   Idle / MoveToTarget / FireCharge … 陣形を組む→隊列を直す→突撃する（ボスの指示で動く）
//   Stunned / Returning              … 吹き飛ばされた／飛び切った（個体が自分で入る）
//   LockOn・Hold・HorizontalCharge・Shaking は演出の上乗せなので作らない。
//   状態を増やすほど 1000機ぶんの分岐が増えるため、増やすのは
//   「見た目の動きが根本的に違うもの」だけに絞り、「1ステート=1関数」の形は崩さない。
//
// 補間係数を必ず (std::min)(k * dt, 1.0f) で止める理由:
//   Lerp / Slerp は係数が 1 を超えると補間ではなく外挿になり、目標を通り越す。
//   dt は固定ステップ（FIXED_DT）なので通常は 1 を超えないが、TimeContext の
//   baseGlobalScale は「ゲーム全体の速度調整」用に 1 より大きくできる。
//   そこを上げた瞬間に旋回だけが破綻するのは追いにくいので、係数側で止めておく。
//
// 実行順序: L03_PrePhysics（PushSystem より前）で trans を確定させる。
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Time/TimeState.h"

class DroneMovementSystem : public CCL::ECS::IfSystem<
                                DroneMovementSystem,
                                CCL::ECS::Write<TransformComponent>,
                                CCL::ECS::Write<DroneComponent>,
                                CCL::ECS::Read<TimeStateComponent>>
{
  public:
    DroneMovementSystem()
        : IfSystem("DroneMovementSystem")
    {
    }
    virtual ~DroneMovementSystem() override = default;

    virtual void Update(float dt) override;

  private:
    // =========================================================
    // 各関数は drone.currentVelocity（意思）と rotation を計算する。
    // 最後に trans.position へ積分するのは Update 側の共通処理が担う。
    // =========================================================

    // 滑らかに目標へ移動・回転する (Lerp/Slerp)。陣形の再構築に使う。
    inline void ProcessMoveToTarget(TransformComponent &trans, DroneComponent &drone, float dt);

    // 陣形を維持しつつフワフワ追従する。待機・巡回に使う。
    inline void ProcessIdle(TransformComponent &trans, DroneComponent &drone, float dt);

    // 目標方向へ直線的に高速突撃する。ジャスト回避の対象となる攻撃状態。
    inline void ProcessFireCharge(TransformComponent &trans, DroneComponent &drone, float dt);

    // 隊列を組んだままの突入。targetPosition へバネで追従する。
    //
    // 剛体追従と違って形が歪む。歪むことがこの技の見た目の核。
    inline void ProcessChargeSoftFollow(TransformComponent &trans, DroneComponent &drone, float dt);

    // 予告の震えの「位置オフセット」[m]。位相を引数に取るのは、前フレームぶんの
    // オフセットを引いて差分だけ動かすため（ProcessIdle 参照）。
    static DirectX::SimpleMath::Vector3 Shiver(float phase, float amplitude);

    // 切り払いで吹き飛ばされ、制御を失って漂う。通常の追従・突撃を一切行わない。
    //
    // タイマーの管理は DroneBrainSystem 側。ここは「漂って減衰する」動きだけを持つ。
    inline void ProcessStunned(TransformComponent &trans, DroneComponent &drone, float dt);

    // 突き抜けた後、上方へ抜けながら減速する離脱カーブ。
    //
    // その場で急旋回させると突撃の勢いが嘘になるため、一度抜けさせてから
    // MoveToTarget に隊列復帰を任せる。
    inline void ProcessReturning(TransformComponent &trans, DroneComponent &drone, float dt);

    // --- 陣形へ戻る（MoveToTarget）---
    //   持ち場のズレに掛けるバネの強さと、その速度追従の滑らかさ。
    //   Idle より強く取ってあるのは、隊列を「直す」局面だからで、
    //   同じ値にすると崩れた隊形が整うまでに時間がかかりすぎる。
    static constexpr float kMoveToTargetGain    = 20.0f;
    static constexpr float kMoveToTargetDamping = 15.0f;
    static constexpr float kMoveToTargetSpeedScale = 2.5f;  // moveSpeed に掛ける上限

    // --- 陣形を保つ（Idle）---
    //   こちらは「維持」なのでバネを弱くする。強いと目標に貼り付いてしまい、
    //   ホバーのゆらぎが打ち消されて機械的に見える。
    static constexpr float kIdleGain        = 10.0f;
    static constexpr float kIdleDamping     =  8.0f;
    static constexpr float kIdleSpeedScale  =  1.5f;

    // ホバーのゆらぎ。位相は個体ごとに散らしてある（DroneComponent の hoverTimeOffset）。
    static constexpr float kHoverFrequency = 1.0f;   // ※ kHoverPhasePeriod の巻き戻しが成立するよう整数にする
    static constexpr float kHoverAmplitude = 3.0f;

    // --- 突撃のタメ（予備動作）---
    //   自分の番が来た機が、撃つ方向へ踏み出しながら持ち上がり、細かく震える。
    //   ジャスト回避を「読ませる」時間帯。
    //
    //   タメは「静止 → 加速」の2段になっている:
    //
    //     |<--------- DroneBrainSystem::kChargeWindup (0.4秒) --------->|
    //     |<--- 静止（0.2秒）--->|<--- kWindupLungeTime (0.2秒) ------->| 射出
    //
    //   静止する長さは引き算で決まる（kChargeWindup − kWindupLungeTime）。
    //   中央へ吸い込まれた機がそこで一度ピタリと止まることで、
    //   「集まった」と「撃った」が別々の出来事として読める。止まらないと
    //   集合の勢いのまま流れ出てしまい、どこが発射の瞬間か分からない。
    //   ため時間を変えたいときは kChargeWindup、
    //   出る勢いを変えたいときは下の2つを動かす。
    //
    //   加速にしてある理由:
    //     定速だと「静止 → 一定速 → 射出速度」と速度の段差が2回でき、
    //     1発ぶんの動きが繋がらずカクついて見える。
    //
    //   残り時間で判定するので、kChargeWindup を変えてもここは壊れない。
    //   タメが長ければ静止する時間が延び、短ければ加速が途中までになる。
    static constexpr float kWindupLungeTime  = 0.2f;  // 加速にかける長さ [秒]
    static constexpr float kWindupLungeSpeed = 20.0f; // 加速の終速 [m/s]（前進 ≒2.0m）

    // --- 陣形攻撃の予告（震え）---
    //
    //   縦割り・挟み込みのように「形は完成していて、あとは振り下ろすだけ」の技は、
    //   予備動作の間ほとんど動かない。動かないものは予告にならないので、
    //   撃つ合図の前（持ち場に着いた／まだ撃たない）に震えを出す。
    //
    //   動きで予告する技（弾幕）はこれを使わない。両方入れると予告が二重になり、
    //   どちらを読めばよいのか分からなくなる（IFormation::UsesShiverTelegraph）。
    //
    //   【単位が m/s ではなく m であること】
    //   以前は速度に足していた。速度で揺らすと実際に動く幅は「振幅 ÷ 角速度」
    //   になるので、5.5[m/s] ÷ 100[rad/s] ≒ 5cm しか動かず、画面ではまったく
    //   見えなかった。「何メートル震えるか」を直接指定できるよう位置側で持つ。
    //
    //   振幅は drone.windupTimer（＝構えている秒数）で 0 から立ち上げる。
    //   BT の Wait(予告) が長ければゆっくり大きく、短ければ即ビリッと震える。
    static constexpr float kTelegraphAmplitude = 0.32f; // 溜め切ったときの振れ幅 [m]
    static constexpr float kTelegraphRampTime  = 1.2f;  // この秒数で最大まで溜まる

    //   震えの角速度 [rad/s]。
    //
    //   【3軸に別々の値を入れること】
    //   3軸を同じ値にすると (cos, sin, cos) が同じ角度で動くので、機体は
    //   決まった楕円をなぞって周回するだけになる。震えではなく「小さく回る」
    //   動きに見え、速度を上げても回転が速くなるだけで小刻みにはならない。
    //   互いに整数比にならない値を入れて、軌道を閉じさせないのが要点。
    //
    //   【上げられる上限がある】
    //   60fps で折り返さずに表現できるのは π/dt ≒ 188 [rad/s]（≒30Hz）まで。
    //   これを越えるとサンプリングの折り返しが起き、速く震えるどころか
    //   ゆっくりした揺れに化ける。下の値はすべてその内側に収めてある。
    //
    //   kHoverPhasePeriod(2π) で位相を巻き戻すので、すべて整数のままにすること
    //   （差分計算が折り返しをまたいでも連続するのはこの制約のおかげ）。
    static constexpr float kTelegraphSpeedX = 97.0f;  // ≒15Hz
    static constexpr float kTelegraphSpeedY = 113.0f; // ≒18Hz
    static constexpr float kTelegraphSpeedZ = 89.0f;  // ≒14Hz

    //   さらに角速度の違う波をもう1つ重ねる。
    //   1軸1波だと、どれだけ軸ごとに値を変えても各軸は規則正しい往復のままで、
    //   よく見ると「一定のリズムで揺れている」と分かってしまう。
    //   整数比にならない波を足すと合成の周期が事実上戻ってこなくなり、
    //   同じ振幅でも不規則な振動として読める。
    static constexpr float kTelegraphSpeed2X = 151.0f; // ≒24Hz
    static constexpr float kTelegraphSpeed2Y = 139.0f; // ≒22Hz
    static constexpr float kTelegraphSpeed2Z = 167.0f; // ≒27Hz

    //   第2成分を混ぜる比率。上げるほど荒れた震えになる。
    static constexpr float kTelegraphSecondary = 0.45f;

    // 突撃（リリース）の速度倍率。moveSpeed(既定30) に掛ける。
    //
    //   【上げすぎると擦り抜ける】
    //   当たり判定は毎フレームの距離判定なので、1フレームの移動距離が
    //   当たり半径の2倍を超えると、判定される位置を跨いで通り過ぎる。
    //     当たり半径 2.5m → 1フレーム 5.0m まで
    //     60fps なら 5.0 / (1/60) ≒ 300 m/s が上限の目安
    //   4.0 倍 = 120 m/s は1フレーム 2.0m なので、判定に2〜3フレーム乗る。
    //   ここより上げるなら、当たり半径(FormationCombat::hitRadius)も
    //   一緒に広げるか、線分での判定に変える必要がある。
    static constexpr float kChargeSpeedScale = 5.5f;

    // --- 姿勢の追従速度（1/秒）。局面ごとに変える ---
    //   整列中は機敏に、待機はゆるく、突撃は最も速く、離脱は最も鈍く。
    //   離脱だけ遅いのは「制御を取り戻しつつある」表現。
    static constexpr float kTurnMoveToTarget = 25.0f;
    static constexpr float kTurnIdle         = 15.0f;
    static constexpr float kTurnFireDelay    = 15.0f;
    static constexpr float kTurnWindup       = 20.0f;
    static constexpr float kTurnCharge       = 25.0f;
    static constexpr float kTurnSoftFollow   = 12.0f;
    static constexpr float kTurnReturning    =  8.0f;

    // 吹き飛ばし中の回転速度（rad/秒）。姿勢制御を失っている表現。
    static constexpr float kStunTumbleRate = 4.0f;

    // 吹き飛ばし速度の減衰率（1/秒）。数フレームで初速→ほぼ0にする。
    static constexpr float kKnockbackDecay = 6.0f;

    // 離脱中に上方向へ加える加速度。突撃の軌道から「抜ける」ための持ち上げ。
    static constexpr float kBreakawayLift = 18.0f;

    // 離脱中の減速率（1/秒）。突撃速度を帰投できる速さまで落とす。
    static constexpr float kBreakawayDrag = 2.5f;

    // 同じく速度の追従の滑らかさ。
    static constexpr float kSoftFollowDamping = 10.0f;

    // 隊列追従時の最高速度倍率。頭の突入速度を上回る必要がある。
    static constexpr float kSoftFollowMaxSpeedScale = 2.4f;

    // slotInSquad から追従の鈍さ係数を出す。
    //
    // 後方の機ほどワンテンポ遅れて曲がるので、隊列がムチのようにしなる。
    // 「編隊で飛んでいる」感の大半はこの遅れから出ている。
    // 小隊に属さない機は slotInSquad=0 なので影響を受けない。
    float SlotLag(const DroneComponent &drone) const
    {
        const float lag = 1.0f - static_cast<float>(drone.slotInSquad) * m_slotLagPerSlot;
        return (lag < 0.15f) ? 0.15f : lag;   // 遅すぎて隊列から千切れないよう下限を置く
    }

    // --- SquadTuning から毎フレーム写し取る（ImGui で実行中に変えられる）---
    //   並列ループから resource を引かずに済むよう、Update の先頭で1回だけコピーする。
    float m_followGain     = 10.0f;
    float m_slotLagPerSlot = 0.06f;
};
