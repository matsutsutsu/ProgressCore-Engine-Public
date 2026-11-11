#pragma once
// ===========================================================================
// 概要: 攻撃判定がまれに出ない不具合を切り分ける計測ログ（HITDIAG_LOG）。普段は HITBOX_DIAGNOSTICS を 0 にする
//
// 備考: CCL_LOG_INFO / WARN は Shipping で消えるので、構成によらず出るマクロを別に持つ
//
// ログ（先頭が [HitDiag/...]）と、出たときに疑う所:
//   Contact … 物理から来た接触（Add / Persist / Remove）
//             出なければ物理側（ボディ・レイヤー・スリープ）、Persist ばかりなら OnContactPersisted の取りこぼし
//   Sync    … 判定区間の ON / OFF と、多段ヒット履歴のリセット
//   Reject  … 接触は来たが当たらなかった理由
//             AlreadyHit なら多段ヒット履歴が残っている、inactive なら判定区間の同期がずれている
//   Hit     … 当たってダメージイベントを作った
// ===========================================================================
#include "Engine/Platform/Core/Logger.h"

// 1 で計測ログ有効。調査が終わったら 0 に戻す
#define HITBOX_DIAGNOSTICS 0

#if HITBOX_DIAGNOSTICS
// Warning レベルで出すのは、Info が構成によって間引かれるのを避けるため
#define HITDIAG_LOG(category, ...)                        \
    Logger::Instance().Log((category), LogLevel::Warning, \
                           std::source_location::current(), __VA_ARGS__)
#else
#define HITDIAG_LOG(category, ...) ((void)0)
#endif
