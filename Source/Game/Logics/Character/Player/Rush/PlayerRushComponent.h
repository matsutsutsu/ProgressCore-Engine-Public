#pragma once
// ===========================================================================
// 概要: ジャスト回避が成立した後の「ラッシュ」の設定値と状態を持つ Component。
//       状態を書くのは PlayerRushSystem。
//
// 責務:
//   - 世界のスローの深さと、プレイヤーの時間倍率を持つ
//   - フェーズ（回避離脱・待機・攻撃・連撃）と、その残り時間を持つ
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
    Ready,      // 待機。攻撃の入力だけ受け付ける。攻撃で Attack、時間切れでラッシュが終わる
    Attack,     // 間合いを詰めて、通常のダッシュ攻撃で斬る。終わったら Flurry
    Flurry,     // 連撃。攻撃を押すたびにラッシュの次の段を出す。途切れるか上限でラッシュが終わる
};

struct PlayerRushComponent
{
    // --- 世界のスロー（ラッシュ中は一律）---

    float worldScale  = 0.1f; // 世界の時間倍率（1 で等速）
    float playerScale = 1.0f; // プレイヤーの時間倍率（1 で実時間）
    float slowRecover = 0.4f; // 終わった後、等速へ戻るまで（秒）

    // --- フェーズの長さ（秒）---

    float lockedDuration = 0.25f; // 回避離脱。離脱の移動にかける時間
    float readyDuration  = 0.40f; // 待機。この間に攻撃しなければラッシュが終わる
    float attackDuration = 0.35f; // 攻撃（詰め＋斬り）

    // --- 連撃（秒）---

    // 段は、アニメグラフの RushCombo（Attack_1 を速く流す RushSlash_A / B を交互に）。
    // ダッシュ攻撃の後、RushAttack のトリガーで1段ずつ進む
    int   rushStepCount     = 8;    // ラッシュの段の数の上限
    float rushInputBuffer   = 0.5f; // 押した攻撃を、次の段の受付区間が来るまで覚えておく時間
    float flurryKeepTime    = 1.0f; // 攻撃を押してから、次を押さずに連撃が続く時間。次の段の受付区間が来るより長くする
    float flurryFinishTime  = 1.0f; // 最後の段を出してから、ラッシュを終えるまでの時間
    float flurryMaxDuration = 6.0f; // 連撃の上限。押し続けても、ここで終わる

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
    float     timer       = 0.0f;              // 今のフェーズの残り（秒）。回避離脱では使わない。連撃では次の攻撃までの猶予
    bool      attackFired = false;             // Attack で斬りを出したか
    float     flurryTime  = 0.0f;              // 連撃に入ってからの時間（秒）。flurryMaxDuration で終える
    int       rushStep    = 0;                 // 出したラッシュの段の数（詰めの斬りは数えない。最初の連撃で 1）

    DirectX::SimpleMath::Vector3 evadeDir      = {0, 0, 0};                 // 離脱の向き（ダッシュの向き・正規化済み）
    DirectX::SimpleMath::Vector3 closeInDir    = {0, 0, 0};                 // 詰める向き（正規化済み）
    CCL::ECS::EntityID           closeInTarget = CCL::ECS::InvalidEntityID; // 詰め先の敵。ロックオンが無ければ無効
};
