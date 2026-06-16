#pragma once
// ===========================================================================
// KnockbackComponent.h
// --------------------
// 役割:
//   ノックバック(吹っ飛び)の継続的な減衰運動を表現する状態データ。
//   敵などの被弾対象に【最初から常設】し、isActive フラグでオン/オフを切り替える。
//
// 責務:
//   - 吹っ飛び中の残り速度ベクトル(XZ平面)と減衰係数、稼働フラグの保持のみ。
//
// アーキテクチャ上の位置付け:
//   有効化: DamageResolutionSystem(L06) が命中時に velocity を書き込み isActive=true。
//   消費:   KnockbackSystem(L02) が毎フレーム Transform に反映・減衰し、停止したら
//           isActive=false に戻す(コンポーネントは外さない)。
//
// 設計方針(重要):
//   - 付け外し(AddComponent/RemoveComponent)は【行わない】。構造変更はアーキタイプ間
//     移動を誘発し、弾幕で多数の敵が同時被弾するとChunkコピーが頻発して重くなる。
//     よって敵生成時に常設し、isActive のオン/オフだけで状態を切り替える(構造変更ゼロ)。
//   - 本作は平面ステージのため吹っ飛びは XZ のみ。Y は触らない。
//   - POD。敵はECS駆動(Transform)＋Jolt Kinematic(判定のみ)で、Joltに力を渡さない
//     ため過去の「Joltに力を渡して敵が吹き飛ぶ」事故は構造的に起きない。
// ===========================================================================
#include "SimpleMath.h"

/// @brief 吹っ飛び中の残り速度・減衰・稼働フラグを保持する常設コンポーネント。
struct KnockbackComponent {
    DirectX::SimpleMath::Vector3 velocity = DirectX::SimpleMath::Vector3::Zero; ///< 残り速度(XZ。units/秒)

    /// @brief 1秒あたりの減衰率。velocity *= pow(decayPerSec, dt) で適用。
    ///        小さいほど早く止まる(例: 0.02 で約1秒後に1/50)。
    float decayPerSec = 0.02f;

    /// @brief 吹っ飛び中か。命中時にtrue、停止でfalse。falseの間は処理をスキップする。
    bool isActive = false;
};