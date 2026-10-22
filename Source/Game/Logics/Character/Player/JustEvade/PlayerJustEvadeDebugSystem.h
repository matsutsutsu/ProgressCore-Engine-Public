#pragma once
// ===========================================================================
// 概要: ジャスト回避を、攻撃に触れなくても狙った方向で成立させるデバッグ用 System。
//
// 責務:
//   - OnGui の方向ボタンで、カメラ基準の前後左右へ本物のダッシュを入力として差し込む
//   - 差し込んだダッシュ（または「常に成立」中の全ダッシュ）の直後に、ジャスト回避を成立させる
//   - 待機（Ready）中に、指定の遅れで攻撃入力を差し込む（詰め→斬りまで手を離して確かめる）
//   - ラッシュを途中で終わらせる（スロー再生で寄りだけ見て、すぐ次を試す用）
//
// 備考: 省くのは「攻撃への接触」だけ。ダッシュの開始は PlayerDashSystem、成立後のラッシュは
//       PlayerRushSystem が普段どおり処理するので、方向の受け渡し・ダッシュの中断・イベントの購読まで
//       本番と同じ道筋で試せる。成立の書き方（受付を閉じる・成立フラグ・イベント）は PlayerJustEvadeSystem と揃える。
//       既定ではすべて無効で、OnGui で有効にしたときだけ入力を差し込む。
//       成立はリザルトのジャスト回避回数にも数えられる（デバッグ中の記録は参考にしない）。
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

class PlayerJustEvadeDebugSystem : public CCL::ECS::IfSystem<PlayerJustEvadeDebugSystem,
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

    // 方向ボタン・常に成立・待機中の自動攻撃と、直近の結果を出す。
    void OnGui() override;

    // 入力を確定させた後（PlayerInputSystem）に上書きする。攻撃入力を書くので PlayerAttackSystem より後にする。
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

    /**
     * @brief 要求された向きのダッシュを入力として差し込む。ダッシュできない状態なら待つか捨てる。
     *
     * @param pin      ダッシュの押下（dashPressed）を書く先。
     * @param movement ダッシュの向き（desiredVelocity）を書く先。PlayerDashSystem はこれを向きに使う。
     * @param dash     ダッシュ中・クールダウン中かを見る。
     * @param rush     ラッシュ中は差し込まない。
     * @param dt       待ち時間を減らす経過時間（秒）。
     */
    void InjectDash(PlayerInputComponent &pin, CharacterMovementComponent &movement, const PlayerDashComponent &dash,
                    const PlayerRushComponent &rush, float dt);

    /**
     * @brief ダッシュ開始直後（受付窓が開いている）なら、ジャスト回避を成立させる。
     *
     * PlayerJustEvadeSystem の成立と同じく、受付を閉じて成立フラグを立て、JustEvadeSuccessEvent を出す。
     *
     * @param id     プレイヤーのエンティティ。
     * @param evade  成立フラグを書く先。
     * @param dash   受付窓を閉じる先。
     * @param player 攻撃してきた敵の代わりに、ロックオン先をイベントへ載せる。
     */
    void TryForceSuccess(CCL::ECS::EntityID id, PlayerJustEvadeComponent &evade, PlayerDashComponent &dash,
                         const PlayerComponent &player);

    // 待機（Ready）に入ってから m_autoAttackDelay 秒後に、攻撃入力を1回だけ差し込む。
    void InjectAutoAttack(PlayerInputComponent &pin, const PlayerRushComponent &rush, float dt);

    /**
     * @brief 要求があれば、ラッシュを終わらせる。
     *
     * 自由（Free）の時間切れにするだけで、終了そのものは PlayerRushSystem の EndRush に任せる
     * （時間倍率の戻しなど、本番の終わり方と揃えるため）。
     *
     * @param rush フェーズと残り時間を書く先。
     */
    void ForceEndRush(PlayerRushComponent &rush);

    // 要求された向きを、カメラ基準の水平な向きにする。カメラが無ければゼロ。
    DirectX::SimpleMath::Vector3 ToWorldDir(EvadeDir dir) const;

    // 向きの表示名（結果の表示用）
    const char *DirLabel(EvadeDir dir) const;

    // --- 設定（OnGui で切り替える。保存しない） ---
    bool  m_alwaysSucceed   = false; // ON の間は、どのダッシュでも開始直後に成立させる（WASD で自由に試す用）
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
