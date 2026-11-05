#pragma once
// ===========================================================================
// 概要: ジャスト回避を、攻撃に触れなくても狙った方向で成立させるデバッグ用 System
//
// 責務:
//   - OnGui の方向ボタンで、カメラ基準の前後左右へ本物のダッシュを入力として差し込む
//   - 差し込んだダッシュ（「常に成立」中は全ダッシュ）の直後に、ジャスト回避を成立させる
//   - 待機中に、指定の遅れで攻撃入力を差し込む
//   - ラッシュを途中で終わらせる
//
// 備考: 省くのは攻撃への接触だけで、ダッシュとラッシュは PlayerDashSystem と PlayerRushSystem が普段どおり処理する
//       成立の書き方（受付を閉じる・成立フラグ・イベント）は PlayerJustEvadeSystem とそろえる
//       既定ではすべて無効。成立はリザルトのジャスト回避回数にも数えられる
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include "Game/Logics/Character/Player/Core/PlayerInputComponent.h"
#include "Game/Logics/Character/Player/Core/PlayerComponent.h"
#include "Game/Logics/Character/Player/JustEvade/PlayerJustEvadeComponent.h"
#include "Game/Logics/Character/Player/Dash/PlayerDashComponent.h"
#include "Game/Logics/Character/Player/Rush/PlayerRushComponent.h"

#include <SimpleMath.h>
#include <string>
#include <vector>

class PlayerJustEvadeDebugSystem : public CCL::ECS::IfSystem<
    PlayerJustEvadeDebugSystem,
    CCL::ECS::Write<PlayerInputComponent>,
    CCL::ECS::Write<CharacterMovementComponent>,
    CCL::ECS::Write<PlayerJustEvadeComponent>,
    CCL::ECS::Write<PlayerDashComponent>,
    CCL::ECS::Read<PlayerComponent>,
    CCL::ECS::Write<PlayerRushComponent>>
{
public:
    PlayerJustEvadeDebugSystem() : IfSystem("PlayerJustEvadeDebugSystem") {}

    void Update(float dt) override;

    void OnGui() override;

    // 入力を確定させた後（PlayerInputSystem）に上書きする。攻撃入力を書くので PlayerAttackSystem より後にする
    std::vector<const char *> GetRunAfter() const override { return {"PlayerInputSystem", "PlayerAttackSystem"}; }

private:
    // 方向ボタンで要求された向き（カメラ基準）
    enum class EvadeDir : uint8_t
    {
        None = 0,
        Forward,
        Back,
        Left,
        Right,
    };

    // 要求された向きのダッシュを、入力として差し込む。ダッシュできない状態なら待つか捨てる
    // 向きは desiredVelocity に書く。PlayerDashSystem はこれをダッシュの向きに使う
    void InjectDash(PlayerInputComponent &pin, CharacterMovementComponent &movement, const PlayerDashComponent &dash,
                    const PlayerRushComponent &rush, float dt);

    // ダッシュ開始直後（受付中）なら、ジャスト回避を成立させる
    // PlayerJustEvadeSystem と同じく受付を閉じ、成立フラグを立てて JustEvadeSuccessEvent を出す
    // 攻撃してきた敵の代わりに、ロックオン先をイベントへ載せる
    void TryForceSuccess(CCL::ECS::EntityID id, PlayerJustEvadeComponent &evade, PlayerDashComponent &dash,
                         const PlayerComponent &player);

    // 待機（Ready）に入ってから m_autoAttackDelay 秒後に、攻撃入力を1回だけ差し込む
    void InjectAutoAttack(PlayerInputComponent &pin, const PlayerRushComponent &rush, float dt);

    // 要求があれば、連撃の時間切れにしてラッシュを終わらせる
    // 終了そのものは PlayerRushSystem の EndRush に任せ、本番と同じ終わり方にする
    void ForceEndRush(PlayerRushComponent &rush);

    // 要求された向きを、カメラ基準の水平な向きにする。カメラがなければゼロ
    DirectX::SimpleMath::Vector3 ToWorldDir(EvadeDir dir) const;

    // 向きの表示名（結果の表示用）
    const char *DirLabel(EvadeDir dir) const;

    // --- 設定（OnGui で切り替える。保存しない） ---
    bool  m_alwaysSucceed   = false; // ON の間は、どのダッシュでも開始直後に成立させる
    bool  m_autoAttackReady = false; // 待機中に自動で攻撃を入れる
    float m_autoAttackDelay = 0.2f;  // 待機に入ってから攻撃を入れるまで（秒）

    // --- 状態 ---
    EvadeDir    m_pendingDir       = EvadeDir::None; // 差し込み待ちの向き
    float       m_pendingTimeout   = 0.0f;           // 差し込み待ちを諦めるまでの残り（秒）
    EvadeDir    m_forcedDir        = EvadeDir::None; // 差し込んだダッシュの向き（成立させる対象）
    int         m_forceFramesLeft  = 0;              // 差し込んだダッシュの成立を待つ残りステップ数
    float       m_readyElapsed     = 0.0f;           // 待機に入ってからの経過（秒）
    bool        m_autoAttackIssued = false;          // この待機で攻撃を入れたか
    bool        m_endRushRequested = false;          // ラッシュを終わらせる要求（OnGui のボタン）
    std::string m_lastResult       = "-";            // 直近の結果（OnGui に出す）
};
