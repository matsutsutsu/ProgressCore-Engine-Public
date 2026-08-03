#include "PlayerFeedbackSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Game/Logics/GameEvent/GameEvents.h"

// 必要に応じて、あなたのエンジンのマネージャー類をインクルードしてください

// ★カメラのコンポーネントを使うためにインクルードを追加
#include "Engine/Camera/Components/VirtualCameraComponents.h"

// ★被弾ビネット用：既存のビネット設定(singleton)を一時的に駆動する。
//   回避演出中の二重駆動を避けるため JustEvadeEffectComponent も参照する。
#include "Engine/Graphics/Components/PostProcess/VignetteConfigComponent.h"
#include "Engine/Graphics/Components/Impact/JustEvadeEffectComponent.h"

// #include "Engine/Platform/Input/Input.h"
// #include "Engine/Audio/AudioManager.h"

// システムの実行順序の定義ヘッダー
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

namespace
{
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
} // namespace

PlayerFeedbackSystem::PlayerFeedbackSystem() : IfSystem("PlayerFeedbackSystem") {}

void PlayerFeedbackSystem::Initialize()
{
    if (!m_world) return;

    // =======================================================================
    // 放送局（EventBus）から「ダメージを受けた」というニュースを受信する
    // =======================================================================
    // これを書くだけで、基底クラスが裏でチケットを保管し、
    // システムが破棄されるときに自動的にEventBusから登録を抹消（Unsubscribe）してくれる！
    ListenEvent<DamageTakenEvent>([this](const DamageTakenEvent& e) {
        // もしダメージを受けたのが「プレイヤー」だったら、各種演出を発動！
        if (e.isPlayer) {

            // -----------------------------------------------------------------
            // 演出1：カメラシェイク
            // （ダメージ量 e.damageAmount に応じて揺れの強さを変えるとリッチになります）
            // -----------------------------------------------------------------
            // ワールド内に存在するすべての VirtualCamera を探す
            auto vcamView = m_world->View<VirtualCamera>();
            for (auto camID : vcamView) {

                // ダメージ量(e.damageAmount)に基づいて揺れの強さを計算
                // 例：10ダメージなら強さ1.0。上限を設けて揺れすぎを防ぐ
                float shakeStrength = std::clamp(e.damageAmount * 0.1f, 0.2f, 3.0f);
                float shakeDuration = 0.5f; // 揺れる時間は0.5秒

                // すでに CameraShake コンポーネントを持っているか確認
                if (auto *shake = m_world->GetComponent<CameraShake>(camID)) {
                    // 持っていれば上書きして揺らす
                    shake->SetShake(shakeStrength, shakeDuration);
                }
                else {
                    // 持っていなければ新しく取り付けて揺らす
                    CameraShake newShake;
                    newShake.SetShake(shakeStrength, shakeDuration);
                    m_world->AddComponent<CameraShake>(camID, newShake);
                }
            }

            // -----------------------------------------------------------------
            // 演出2：コントローラーの振動（ゲームパッド用）
            // -----------------------------------------------------------------
            // auto& pad = Input::Instance().GetGamePad();
            // pad.SetVibration(0.5f, 0.5f, 0.2f); // 左モーター, 右モーター, 秒数

            // -----------------------------------------------------------------
            // 演出3：ダメージSEの再生
            // -----------------------------------------------------------------
            // AudioManager::Instance().PlaySE("Player_Damage");

            // -----------------------------------------------------------------
            // 演出4：被弾ビネット（画面端の赤フラッシュ）
            // タイマを立てるだけ。実際の見た目は Update() が VignetteConfig を駆動して作る。
            // 連続被弾でも最新の一撃で満タンにリフレッシュする。
            // -----------------------------------------------------------------
            m_flashTimer = kFlashDuration;
        }
    });
}

void PlayerFeedbackSystem::Update(float dt)
{
    // 被弾ビネットの時間経過（パッと出てスッと引く）を毎フレーム駆動する。
    UpdateDamageVignette(dt);
}

// ---------------------------------------------------------------------------
// 被弾ビネット本体：既存の VignetteConfigComponent(singleton) を一時的に赤く光らせ、
// 演出後は立ち下がりで元の値へ完全復元する（JustEvadeEffectSystem と同じ「壊さない」型）。
// 触るのは intensity / color / pulse のみ。
// ---------------------------------------------------------------------------
void PlayerFeedbackSystem::UpdateDamageVignette(float dt)
{
    auto *vig = m_world->GetSingletonComponent<VignetteConfigComponent>();
    if (!vig) return; // ビネット設定が無いシーンでは何もしない

    // ジャスト回避演出がビネットを駆動中なら譲る（二重駆動を避ける安全弁）。
    // ※通常は「食らう」と「回避成功（無敵）」は同時に起きない。
    if (auto *je = m_world->GetSingletonComponent<JustEvadeEffectComponent>())
    {
        if (je->timer > 0.0f)
        {
            m_flashActive = false; // 復元は JustEvade 側に任せて手を引く
            return;
        }
    }

    const bool active = (m_flashTimer > 0.0f);

    // ── 立ち下がり：退避しておいた元の値へ完全復元する ──
    if (!active)
    {
        if (m_flashActive)
        {
            vig->enable             = m_vigBase.enable;
            vig->data.vigIntensity  = m_vigBase.intensity;
            vig->data.vigColor      = m_vigBase.color;
            vig->data.vigPulseSpeed = m_vigBase.pulse;
            vig->data.vigRadius     = m_vigBase.radius;
            vig->data.vigSoftness   = m_vigBase.softness;
            m_flashActive = false;
        }
        return;
    }

    // ── 立ち上がり：元のビネット値を退避（演出後にここへ戻す） ──
    if (!m_flashActive)
    {
        m_vigBase.enable    = vig->enable;
        m_vigBase.intensity = vig->data.vigIntensity;
        m_vigBase.color     = vig->data.vigColor;
        m_vigBase.pulse     = vig->data.vigPulseSpeed;
        m_vigBase.radius    = vig->data.vigRadius;
        m_vigBase.softness  = vig->data.vigSoftness;
    }

    // ── 減衰（実時間。スロー窓でも体感の長さが一致する） ──
    m_flashTimer -= dt;
    if (m_flashTimer < 0.0f) m_flashTimer = 0.0f;

    // ── エンベロープ：被弾で即最大 → env=e² で素早く減衰（パッと出てスッと引く） ──
    const float e   = m_flashTimer / kFlashDuration; // 1→0
    const float env = e * e;                        // 後半ほど速く引いてキレを出す

    vig->enable            = true;
    vig->data.vigIntensity = Lerp(m_vigBase.intensity, kFlashPeak, env);
    vig->data.vigColor     = DirectX::XMFLOAT4{Lerp(m_vigBase.color.x, kFlashR, env),
                                               Lerp(m_vigBase.color.y, kFlashG, env),
                                               Lerp(m_vigBase.color.z, kFlashB, env), 1.0f};
    // 半径を縮め・ふちを広げて、赤を四隅だけでなく画面の内側まで届かせる。
    vig->data.vigRadius     = Lerp(m_vigBase.radius, kFlashRadius, env);
    vig->data.vigSoftness   = Lerp(m_vigBase.softness, kFlashSoftness, env);
    vig->data.vigPulseSpeed = 0.0f; // フラッシュ中は脈動させない

    m_flashActive = true;
}

// 演出系のシステムなので、物理やロジックが終わった後（PostUpdate等）に実行するのが安全です
REGISTER_LOGIC_SYSTEM(PlayerFeedbackSystem, Priority::LogicStage::L06_Resolution);