#pragma once
// ===========================================================================
// 概要: タイムライン（ムービー演出）の再生要求と終了通知のイベント定義。
//
// 責務:
//   - ゲーム側から任意のタイミングで再生させる要求を定義する
//   - 再生の終了（スキップ・中止を含む）をゲーム側へ伝える通知を定義する
//   - シグナルトラックの通知を定義する
// ===========================================================================
#include "Engine/ECS/Common/CCL_Types.h"

#include <string>

/**
 * @brief タイムラインの再生要求。ボス撃破でムービーを流す、といった任意タイミング用。
 *
 * シーン開始時のムービーは playOnStart で自動再生されるので、この要求は要らない。
 *
 * 再生する TimelinePlayer は playerID → trigger の順に見て選ぶ。
 * どちらも指定しなければ、全ての TimelinePlayer を再生する。
 */
struct TimelinePlayRequestEvent
{
    // 再生したい TimelinePlayer の Entity。InvalidEntityID なら trigger で選ぶ。
    CCL::ECS::EntityID playerID = CCL::ECS::InvalidEntityID;

    // 再生したい TimelinePlayer の trigger（TimelinePlayerComponent::trigger と比べる）。
    //   ゲーム側は、撃破ムービーなどの TimelinePlayer の Entity を知らない（シーンがプレハブから生成する）。
    //   そのため ID の代わりに名前でも選べるようにしている。
    //   空なら名前では選ばない。
    std::string trigger;
};

// シグナルトラックのクリップが発行する通知。
// 意味はゲーム側の受け手が name で決める。
//   例: name="Telop" → テロップを出す /
//       name="BGM" → BGM を切り替える（CinematicSignalSystem）
struct TimelineSignalEvent
{
    CCL::ECS::EntityID playerID = CCL::ECS::InvalidEntityID; // 発行した TimelinePlayer
    std::string        name;                                 // シグナル名
    std::string        param;                                // 引数1（文字列）
    std::string        param2;                               // 引数2（文字列）
    float              value   = 0.0f;                       // 引数3（数値）
    bool               skipped = false; // スキップで飛ばされた区間から発行されたか（fireOnSkip のクリップだけが来る）
};

// タイムラインの再生が終わった通知（スキップ・中止による終了も含む）。
// カメラの返却とプレイヤー操作の返却は TimelineSystem 側が行うので、受け手は気にしなくてよい。
struct TimelineFinishedEvent
{
    CCL::ECS::EntityID playerID = CCL::ECS::InvalidEntityID;
    bool               skipped  = false; // プレイヤーがスキップしたか
    bool               aborted  = false; // 資産の読み込み失敗・役割の未解決で打ち切ったか
};
