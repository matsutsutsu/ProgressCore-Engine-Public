#pragma once
#include <cstdint>
#include <SimpleMath.h>
#include "Engine/ECS/Common/CCL_Types.h"

// ラッシュのフェーズ。成立した直後から順に進む
// スローはフェーズによらず一定で、フェーズは操作と演出だけを切り替える
enum class RushPhase : uint8_t
{
    Locked = 0, // 回避離脱。減速しながら離れる。操作は受け付けない
    Ready,      // 待機。攻撃の入力だけ受け付ける
    Attack,     // 間合いを詰めて、ダッシュ攻撃で1回斬る
    Flurry,     // 連撃。押すたびに斬り、決まった数の後にとどめ
};

// ジャスト回避の成立後の「ラッシュ」の設定値と状態。状態を書くのは PlayerRushSystem
struct PlayerRushComponent
{
    // --- 世界のスロー ---

    float worldScale    = 0.1f; // 世界の時間倍率（1 で等速）
    float playerScale   = 1.0f; // プレイヤーの時間倍率（1 で実時間）
    float slowRecover   = 0.4f; // 終了後、等速へ戻るまで（秒）
    float finishRecover = 0.4f; // とどめで終えたときの戻り（秒）

    // --- フェーズの長さ ---

    float lockedDuration = 0.25f; // 回避離脱の長さ（秒）
    float readyDuration  = 0.40f; // 待機の長さ（秒）
    float attackDuration = 0.35f; // 斬ってから連撃へ移るまで（秒）

    // --- 連撃 ---

    int   rushStepCount     = 4;     // とどめの前に出す斬りの数
    float rushInputBuffer   = 0.5f;  // 押した攻撃を覚えておく時間（秒）
    float flurryMaxDuration = 10.0f; // 連撃の上限（秒）

    // --- 回避離脱 ---

    float leapSpeed = 120.0f; // 離脱の初速（m/秒）

    // --- 間合い詰め ---

    float closeInSpeed        = 90.0f; // 詰める速さ（m/秒）
    float closeInStopDistance = 3.0f;  // 敵にこの距離まで近づいたら斬る（m）

    // --- 無敵 ---

    float invincibleRefresh = 0.1f; // ラッシュ中に保つ無敵の残り（秒）

    // --- 実行時の状態 ---

    bool      isActive      = false;             // ラッシュ中か
    bool      isRecovering  = false;             // 終了後、スローが戻るのを待っているか
    RushPhase phase         = RushPhase::Locked; // 今のフェーズ
    float     timer         = 0.0f;              // 今のフェーズの残り（秒）。回避離脱では使わない
    bool      attackFired   = false;             // Attack で斬りを出したか
    int       rushStep      = 0;                 // 連撃で入った斬りの数。詰めの斬りは含まない
    bool      finishEntered = false;             // とどめのアニメに入ったか

    uint32_t lastAnimState = 0; // 前のステップのアニメのステート

    DirectX::SimpleMath::Vector3 evadeDir      = {0, 0, 0};                 // 離脱の向き（正規化済み）
    DirectX::SimpleMath::Vector3 closeInDir    = {0, 0, 0};                 // 詰める向き（正規化済み）
    CCL::ECS::EntityID           closeInTarget = CCL::ECS::InvalidEntityID; // 詰め先の敵。ロックオンがなければ無効
};
