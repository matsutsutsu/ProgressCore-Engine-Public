#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include <DirectXMath.h>

// ===================================================================================
// クラス名: PlayerFeedbackSystem
// 役割: プレイヤーがダメージを受けた時などの「演出（フィードバック）」を専属で管理する。
//       被弾の受信は DamageTakenEvent(EventBus) 一本に集約し、ここでカメラシェイクと
//       「被弾ビネット（画面端の赤フラッシュ）」をまとめて出す（演出の一元管理）。
// ===================================================================================
class PlayerFeedbackSystem : public CCL::ECS::IfSystem<PlayerFeedbackSystem> {
  public:
    PlayerFeedbackSystem();
    virtual ~PlayerFeedbackSystem() = default;

    // ここで EventBus の購読（Subscribe）を設定します
    void Initialize() override;

    // 被弾ビネットの時間経過（フェードアウト）をここで駆動します
    void Update(float dt) override;

  private:
    // 被弾ビネットを毎フレーム駆動する（既存 VignetteConfig を一時的に赤く光らせて戻す）。
    void UpdateDamageVignette(float dt);

    // --- 被弾ビネットのランタイム状態 ---
    float m_flashTimer  = 0.0f;  ///< 残り時間(秒・実時間)。>0 の間フラッシュ。被弾で満タンに。
    bool  m_flashActive = false; ///< 立ち上がり/立ち下がり検出用

    // 立ち上がりで退避する「元の」ビネット値（立ち下がりでここへ完全復元する）。
    struct VigBaseline
    {
        bool              enable    = true;
        float             intensity = 0.4f;
        DirectX::XMFLOAT4 color     = {0.0f, 0.0f, 0.0f, 1.0f};
        float             pulse     = 0.0f;
        float             radius    = 0.55f;
        float             softness  = 0.35f;
    } m_vigBase;

    // --- チューニング用（ここを触れば手触りを調整できる） ---
    static constexpr float kFlashDuration = 0.35f; ///< フラッシュの長さ(秒)
    static constexpr float kFlashPeak     = 0.95f; ///< ピーク時のビネット強度
    static constexpr float kFlashR = 0.80f;        ///< フラッシュ色（血の赤）
    static constexpr float kFlashG = 0.03f;
    static constexpr float kFlashB = 0.03f;
    // フラッシュ中は半径を小さく＝縁の赤を画面の奥まで広げて、四隅だけでなく全体で赤を感じさせる。
    static constexpr float kFlashRadius   = 0.15f; ///< ピーク時の半径（小さいほど赤が内側まで来る）
    static constexpr float kFlashSoftness = 0.60f; ///< ピーク時のふち柔らかさ
};