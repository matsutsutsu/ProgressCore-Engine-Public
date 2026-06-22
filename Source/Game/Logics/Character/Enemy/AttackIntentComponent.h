/// @file  AttackIntentComponent.h
/// @brief AI が「攻撃したい」という意図を書き込むコンポーネント。
///
/// AttackSystem がこれを読んでダメージイベントを発行する。
/// StateTreeSystem は「いつ・どの強さで攻撃したいか」だけを書き、
/// ヒット判定やノックバックは AttackSystem が行う。
#pragma once

/// @brief 1フレーム分の「攻撃したい」意図。
struct AttackIntentComponent
{
    bool  requested = false;   ///< true なら今フレームに攻撃要求あり
    float damage    = 0.0f;    ///< 要求ダメージ量

    // 攻撃の種類（近接 / 遠距離 / スキル）は M2 以降で拡張
    // int attackTypeId = 0;
};
