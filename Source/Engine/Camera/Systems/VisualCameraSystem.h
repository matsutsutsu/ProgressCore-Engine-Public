#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Camera/Components/VirtualCameraComponents.h"
#include "Engine/Transform/Components/TransformComponent.h"

#include <atomic>
#include <cstdint>
#include <vector>

// ※ Input.h や SimpleMath.h などはここではインクルード不要になります

// ---------------------------------------------------------
// Body: 追従ロジック
// ---------------------------------------------------------
class CameraFollowSystem : public CCL::ECS::IfSystem<CameraFollowSystem,
                                                     CCL::ECS::Write<VirtualCamera>,
                                                     CCL::ECS::Write<CameraBodyFollow>>
{ // target 自動接続のため Write
public:
    CameraFollowSystem() : IfSystem("CameraFollowSystem") {}
    void Update(float dt) override; // 中身は書かない
};

// ---------------------------------------------------------
// Aim: 注視ロジック
// ---------------------------------------------------------
class CameraLookAtSystem : public CCL::ECS::IfSystem<CameraLookAtSystem,
                                                     CCL::ECS::Write<VirtualCamera>,
                                                     CCL::ECS::Write<CameraAimLookAt>>
{ // target 自動接続のため Write
public:
    CameraLookAtSystem() : IfSystem("CameraLookAtSystem") {}
    void Update(float dt) override;

    // 同じ VirtualCamera を書く Follow と競合する。どちらが先でも結果は同じだが、
    // 順序不定の警告を消し、デバッグ時の再現性を確保するため明示しておく。
    std::vector<const char *> GetRunAfter() const override { return {"CameraFollowSystem"}; }
};

// ---------------------------------------------------------
// Shake: カメラ揺れ
// ---------------------------------------------------------
class CameraShakeSystem : public CCL::ECS::IfSystem<CameraShakeSystem,
                                                    CCL::ECS::Write<VirtualCamera>,
                                                    CCL::ECS::Write<CameraShake>>
{
public:
    CameraShakeSystem() : IfSystem("CameraShakeSystem")
    {
    }

    // 揺れ要求(CameraShakeRequestEvent)の購読を開始する。
    //
    // 画面を揺らす経路はこの1本だけ。発生源がカメラを直接触ると、
    // 誰の揺れが誰の揺れを消したのか追えなくなる。
    void Initialize() override;

    void Update(float dt) override;

    // 揺れが「届いていない」のか「弱すぎて見えない」のかを切り分けるための表示。
    //
    // 揺れは trauma の2乗に比例するので、効いていても目視では無に見えることがある。
    // 揺らせるカメラが 0 台なら要求は捨てられている（CameraShake の付け忘れ）。
    void OnGui() override;

    // 【重要】シェイクは「Body が確定させた resultPos / resultLookAt への加算」である。
    // Body より先に走ると加算結果がそのまま上書きされ、揺れが完全に消える。
    // 同一ステージ(R01_Camera)に並んでいるだけでは順序は保証されないため、
    // すべての Body システムより後ろに来ることをここで宣言する。
    std::vector<const char *> GetRunAfter() const override
    {
        return {"CameraActionTPSSystem", "CameraFollowSystem", "CameraLookAtSystem",
                "CameraFreeControlSystem", "CameraIntroOrbitSystem"};
    }

private:
    // 32bit 整数ハッシュ。ノイズの格子点から擬似乱数を作る。
    static std::uint32_t HashUInt(std::uint32_t x);

    // 時間駆動の1次元勾配ノイズ（Perlin, おおむね -1..1）。
    // 同じ (seed, t) なら常に同じ値を返すため、fps に依らず波形が一定になる。
    static float GradientNoise(std::uint32_t seed, float t);

    // このフレームに届いた揺れ要求のうち最大のもの。Update で消費する。
    //
    // イベントはロジック側の任意のスレッドから同期的に飛んでくる。ここで一度
    // 受け止めることで、CameraShake への書き込みを Update の1箇所に閉じる。
    std::atomic<float> m_pendingTrauma{0.0f};

    // --- エディタ表示用のスナップショット（Update が毎フレーム更新する） ---
    int   m_shakableCameraCount = 0;    // CameraShake を持つカメラの数
    float m_lastTrauma          = 0.0f; // 最後に見たカメラの蓄積量
};

// ---------------------------------------------------------
// Free: 自由移動カメラ
// ---------------------------------------------------------
class CameraFreeControlSystem : public CCL::ECS::IfSystem<CameraFreeControlSystem,
                                                          CCL::ECS::Write<VirtualCamera>,
                                                          CCL::ECS::Write<CameraBodyFree>,
                                                          CCL::ECS::Write<TransformComponent>>
{
public:
    CameraFreeControlSystem() : IfSystem("CameraFreeControlSystem") {}
    void Update(float dt) override;

private:
    // ポーズ中/UI操作中/エディタの他ウィンドウ操作中は入力を食わない、の判定。
    // TPS カメラ側と同じ条件に揃えるためのヘルパ。
    bool IsInputBlocked() const;
};
