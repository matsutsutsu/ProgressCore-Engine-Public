#pragma once
// ===========================================================================
// KnockbackComponent.h
// --------------------
// 役割:
//   被弾時の「吹き飛ばし」状態を保持する常設コンポーネント。被弾させたいキャラ
//   （プレイヤー／ボス等）に付けておく。
//
// 責務:
//   - config: 吹き飛ばしの適用時間 duration と、操作を受け付けない硬直時間 stunDuration。
//   - runtime: 現在のノックバック速度 velocity と残り時間 timer、硬直の残り stunTimer。
//     DamageResolutionSystem が命中時に velocity/timer/stunTimer を書き込み、
//     JoltCharacterUpdateSystem が水平速度へ加算しつつ両タイマーを減衰させる。
//
// なぜ「滑る時間」と「動かせない時間」を分けるか:
//   吹き飛ばしの距離は duration×velocity で決まるが、手触りとして欲しい硬直は
//   それより長いことが多い。1つのタイマーを共用すると「硬直を伸ばしたら
//   吹き飛び過ぎる」となって、どちらも詰められなくなる。
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

    /// @brief 被弾後、操作を受け付けない時間(秒)。0 で硬直なし。
    /// @note  実際に入力を止めるのは PlayerInputSystem。ボスやドローンはこの値を
    ///        読まないので、既定値が入っていても影響しない。
    float stunDuration = 0.35f;

    // --- runtime（非シリアライズ） ---
    DirectX::SimpleMath::Vector3 velocity = {0.0f, 0.0f, 0.0f}; ///< 現在のノックバック速度
    float                        timer    = 0.0f;               ///< 残り時間(秒)。>0 の間だけ適用。
    float                        stunTimer = 0.0f;              ///< 硬直の残り(秒)。>0 の間は操作不能。

    /// @brief 操作を受け付けない状態か。
    bool IsStunned() const { return stunTimer > 0.0f; }
};
