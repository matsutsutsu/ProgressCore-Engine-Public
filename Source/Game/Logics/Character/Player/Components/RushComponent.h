#pragma once
// ===========================================================================
// 概要: ジャスト回避成立後の「ラッシュ」（踏込→固定→集中スロー＋間合い詰め）の
//       設定値と状態を持つ Component。
//
// 責務:
//   - ラッシュ3フェーズ（踏込 Lunge / 固定 Plant / 集中 Focus）の設定値と、現在フェーズ・残り時間を持つ
//   - 集中スロー中に攻撃で対象へ寄る「間合い詰め(closeIn)」の設定値と状態を持つ
//
// 備考: 状態を書くのは PlayerRushSystem（駆動）と JustEvadeSystem（開始）。
//       間合い詰めの要求（closeInTargetID/closeInTimer）は PlayerAttackSystem が書く。
//       回避ダッシュ本体と歩走は DashComponent / PlayerDashSystem（別物）。
// ===========================================================================
#include "Engine/ECS/Common/CCL_IDEmitter.h" // 寄せる相手(EntityID)を持つため
#include <SimpleMath.h>

struct RushComponent
{
    // ラッシュのフェーズ。None=非アクティブ（ラッシュしていない）。
    enum class Phase : unsigned char
    {
        None,  // 非アクティブ
        Lunge, // 踏込：操作ロック・ease-out で回避方向へ前進
        Plant, // 固定：操作ロック・全体フリーズで“ピタッ”と止めて見せる
        Focus  // 集中スロー：操作自由・攻撃で対象へ寄れる
    };

    // --- 設定値（BotW/ゼンゼロのラッシュ風。時間はすべて実時間 rawDt で数える） ---
    float lungeSpeed    = 70.0f; // 踏込の最大速度（m/秒）。減少カーブで ease-out にする
    float lungeDuration = 0.28f; // 踏込の長さ（秒）
    float plantDuration = 0.14f; // 固定で止めて見せる長さ（秒）。メリハリの要
    float plantFreeze   = 0.0f;  // 固定中の世界フリーズ倍率（0=完全停止）。別レイヤーの全体ヒットストップで止める
    float focusDuration = 1.2f;  // 集中スローの長さ（秒）。この間は攻撃を重ねられる
    float worldScale    = 0.2f;  // 世界スロー倍率。SustainWorldSlow は強い方が勝つので単一値にする
    float playerScale   = 1.0f;  // プレイヤー自身の時間倍率（1.0=実時間、>1 で自分だけ速い）
    float slowRecover   = 0.4f;  // 終了後、世界スローが等速へ戻るイーズ時間（秒）

    // --- 間合い詰め(closeIn)：集中スロー中に攻撃すると対象へスッと寄る ---
    float closeInSpeed     = 55.0f; // 寄せ速度（m/秒）。世界スロー中もプレイヤーは実時間なので速く寄れる
    float closeInMaxRange  = 15.0f; // この距離(m)以内の相手にだけ寄る
    float closeInStopRange = 2.5f;  // 対象のこの距離(m)手前で止まる（近接の間合い。めり込み防止）
    float closeInDuration  = 0.4f;  // 1回の寄せの最長時間（秒）。間合いに入るか時間切れで寄せ終了

    // --- 実行時の状態 ---
    Phase                        phase    = Phase::None; // 現在のフェーズ
    float                        timer    = 0.0f;        // 現フェーズの残り時間（秒）
    DirectX::SimpleMath::Vector3 lungeDir = {0, 0, 1};   // 踏込方向（開始時に dashDirection をコピー）

    float              closeInTimer    = 0.0f;                      // 寄せの残り時間（秒）。0=寄せ無し
    CCL::ECS::EntityID closeInTargetID = CCL::ECS::InvalidEntityID; // 寄せる相手（ロックオン先か前方最寄り）

    // ラッシュ中か（None 以外）。
    bool IsActive() const { return phase != Phase::None; }

    // このフレーム、ラッシュが移動を上書きしていて PlayerDashSystem が手を出すべきでないか。
    //   踏込/固定は常にロック。集中中は寄せている間(closeInTimer>0)だけロックし、
    //   寄せていない集中フレームは自由移動（PlayerDashSystem に任せる）。
    bool LocksMovement() const
    {
        return phase == Phase::Lunge || phase == Phase::Plant ||
               (phase == Phase::Focus && closeInTimer > 0.0f);
    }
};
