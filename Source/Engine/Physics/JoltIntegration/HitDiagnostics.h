#pragma once
// ===========================================================================
// 概要: 「攻撃判定がまれに出なくなる」不具合を切り分けるための計測ログのマクロ。
//       物理の接触リスナーから ECS の命中判定までを1本の時系列で追えるようにする。
//
// 責務:
//   - HITBOX_DIAGNOSTICS が 1 のとき、構成に依らず出るログマクロ HITDIAG_LOG を提供する
//
// 備考: CCL_LOG_INFO / CCL_LOG_WARN は _DEBUG でしか展開されない(Logger.h)。
//       通常プレイに使う Development 構成は NDEBUG のため、それらは消える。
//       この不具合は実プレイでしか再現しないので、構成に依らず出るマクロを別に持つ。
//       普段は HITBOX_DIAGNOSTICS を 0 にしておく（全て (void)0 になる）。
//
// ログの読み方（すべて先頭が [HitDiag/...]）:
//   Contact … 物理から接触が来たか。Add/Persist/Remove のどれで来たかが分かる。
//   Sync    … Hitbox の判定区間 ON/OFF と、多段ヒット履歴のリセットが起きたか。
//   Reject  … 接触は来たのに命中に至らなかった理由。
//   Hit     … 命中してダメージイベントを生成した。
//
// 切り分けの指針:
//   Contact が全く出ない            → 物理側（ボディ・レイヤー・スリープ）
//   Contact は Persist ばかり        → OnContactPersisted 未実装の取りこぼし
//   Contact は出るが Reject(AlreadyHit) → 多段ヒット履歴が残り続けている
//   Contact は出るが Reject(inactive)   → 判定区間の同期タイミング
// ===========================================================================
#include "Engine/Platform/Core/Logger.h"

// 1 で計測ログ有効。調査が終わったら 0 に戻す。
#define HITBOX_DIAGNOSTICS 0

#if HITBOX_DIAGNOSTICS
// Warning レベルで出すのは、Info が構成によって間引かれるのを避けるため。
#define HITDIAG_LOG(category, ...)                        \
    Logger::Instance().Log((category), LogLevel::Warning, \
                           std::source_location::current(), __VA_ARGS__)
#else
#define HITDIAG_LOG(category, ...) ((void)0)
#endif
