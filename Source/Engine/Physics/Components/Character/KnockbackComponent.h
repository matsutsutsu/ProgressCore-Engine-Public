#pragma once
// ===========================================================================
// KnockbackComponent.h
// --------------------
// 役割:
//   被弾時の「吹き飛ばし」状態を保持する常設コンポーネント。被弾させたいキャラ
//   （プレイヤー／ボス等）に付けておく。
//
// 責務:
//   - config: 吹き飛ばしの適用時間 duration。
//   - runtime: 現在のノックバック速度 velocity と残り時間 timer。
//     DamageResolutionSystem が命中時に velocity/timer を書き込み、
//     JoltCharacterUpdateSystem が水平速度へ加算しつつ timer を減衰させる。
//
// 設計方針:
//   - CharacterVirtual の速度へ加算する形で動かすため、押し出し・壁は物理が処理する
//     （直接 SetPosition しないので壁抜けしない）。
//   - AddComponent はしない「常設」方式。多数同時被弾でもアーキタイプ移動が発生しない。
//   - velocity/timer はランタイム値なので Fields() に載せない＝保存対象外。
// ===========================================================================
#include <SimpleMath.h>

/// @brief 被弾ノックバックの状態。被弾側キャラに常設で付ける。
struct KnockbackComponent
{
    // --- config（シリアライズ／インスペクタ） ---
    float duration = 0.18f; ///< ノックバックの適用時間(秒)。短いほどキビキビ。

    // --- runtime（非シリアライズ） ---
    DirectX::SimpleMath::Vector3 velocity = {0.0f, 0.0f, 0.0f}; ///< 現在のノックバック速度
    float                        timer    = 0.0f;               ///< 残り時間(秒)。>0 の間だけ適用。
};
