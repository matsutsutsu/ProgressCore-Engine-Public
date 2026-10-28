#pragma once
// ===========================================================================
// 概要: ジャスト回避が成立した後の「ラッシュ」の設定値と状態を持つ Component。
//       状態を書くのは PlayerRushSystem。
//
// 責務:
//   - 世界のスローの深さと、プレイヤーの時間倍率を持つ
//   - フェーズ（回避離脱・待機・攻撃・自由）と、その残り時間を持つ
//   - 回避離脱・間合い詰めの設定と、ラッシュ中に保つ無敵を持つ
//
// 備考: 画面演出は PlayerRushEffectComponent、カメラは PlayerRushCameraComponent。
// ===========================================================================
#include <cstdint>
#include <SimpleMath.h>
#include "Engine/ECS/Common/CCL_Types.h"

// ラッシュのフェーズ。成立した直後から順に進む
// スローはフェーズによらず一律で、フェーズは操作と演出だけを切り替える
enum class RushPhase : uint8_t
{
    Locked = 0, // 回避離脱。回避の向きへ減速しながら離れる。操作不能
    Ready,      // 待機。攻撃の入力だけ受け付ける。攻撃で Attack、時間切れで Free
    Attack,     // 間合いを詰めて斬る。終わったら Free
    Free,       // 自由。スローの中で動ける。時間切れでラッシュが終わる
};

struct PlayerRushComponent
{
    // --- 世界のスロー（ラッシュ中は一律）---

    float worldScale  = 0.1f; // 世界の時間倍率（1 で等速）
    float playerScale = 1.0f; // プレイヤーの時間倍率（1 で実時間）
    float slowRecover = 0.4f; // 終わった後、等速へ戻るまで（秒）

    // --- フェーズの長さ（秒）---

    float lockedDuration = 0.25f; // 回避離脱。離脱の移動にかける時間
    float readyDuration  = 0.40f; // 待機。この間に攻撃しなければ Free へ
    float attackDuration = 0.35f; // 攻撃（詰め＋斬り）
    float freeDuration   = 0.80f; // 自由

    // --- 回避離脱 ---

    float leapSpeed = 120.0f; // 離脱の初速（m/秒）。0 まで直線で落とす

    // --- 間合い詰め ---

    float closeInSpeed        = 90.0f; // 詰める速さ（m/秒）
    float closeInStopDistance = 3.0f;  // ロックオン中の敵にこの距離まで近づいたら斬る（m）

    // --- 無敵 ---

    float invincibleRefresh = 0.1f; // ラッシュ中に保つ無敵の残り（秒）

    // --- 実行時の状態 ---

    bool      isActive    = false;             // ラッシュ中か
    RushPhase phase       = RushPhase::Locked; // 今のフェーズ
    float     timer       = 0.0f;              // 今のフェーズの残り（秒）。回避離脱では使わない
    bool      attackFired = false;             // Attack で斬りを出したか

    DirectX::SimpleMath::Vector3 evadeDir      = {0, 0, 0};                 // 離脱の向き（ダッシュの向き・正規化済み）
    DirectX::SimpleMath::Vector3 closeInDir    = {0, 0, 0};                 // 詰める向き（正規化済み）
    CCL::ECS::EntityID           closeInTarget = CCL::ECS::InvalidEntityID; // 詰め先の敵。ロックオンが無ければ無効
};
