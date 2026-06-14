#pragma once

/// @brief 壁スライド押し出しを受けるキャラに付けるタグ。
/// @param Tagを点ける理由は敵もプレイヤーも共通してこのシステムの
/// 　　　 処理を通すための目印が必要なため
struct WallSlideTag {};


/// @brief キャラの当たり判定半径（XZ平面の円）。
struct CharacterWallRadiusComponent { float radius = 0.4f; };