/// @file  IFormation.h
/// @brief 陣形計算の基底インターフェース。1陣形=1クラス（switch廃止）。
///
/// ★DOD上の設計原則（就職作品の要点）★
///   仮想関数（vtable）はコンポーネントに持たせない。
///   FormationSystem が「今の陣形タイプ」で registry から IFormation を1回引き、
///   Calculate の中で全ドローンをまとめて処理する。
///     → 仮想呼び出しは「陣形の種類ごとに1回」だけ
///     → 重いループ（数百機）は連続メモリアクセスでキャッシュが効く
///   これは BT の IBTAction を registry.GetAction(id) で引くのと同じパターン。
#pragma once
#include "SimpleMath.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"

namespace CCL::AI::Formation
{
/// @brief 陣形計算に必要な「場の情報」。System が組み立てて渡す。
struct FormationContext
{
    DirectX::SimpleMath::Vector3 bossPos   = {};   // ボスの位置（陣形の中心）
    DirectX::SimpleMath::Vector3 playerPos = {};   // プレイヤーの位置（狙う系で使う）
    float                        time      = 0.0f; // 経過時間（回転アニメ等に使う）
    float                        dt        = 0.0f; // このフレームの経過時間（攻撃の進行度を進めるのに使う）
};

/// @brief 陣形の基底。派生が「1機分の目標座標」を計算する。
///
/// 設計方針：
///   Calculate は「ドローン1機」を受け取り、その targetPosition を書く。
///   ループは System 側が回す（同じ陣形のドローンを連続処理）。
///   → localIndex / totalDrones を使えば「全体の何番目か」で座標が決まる。
struct IFormation
{
    virtual ~IFormation() = default;

    /// @brief 1機分の目標座標・視線を計算して drone に書き込む。
    /// @param drone  対象ドローン（targetPosition 等を書き換える）
    /// @param ctx    場の情報（ボス位置・プレイヤー位置・時間）
    virtual void Calculate(DroneComponent &drone,
        const FormationContext            &ctx) = 0;
};

} // namespace CCL::AI::Formation