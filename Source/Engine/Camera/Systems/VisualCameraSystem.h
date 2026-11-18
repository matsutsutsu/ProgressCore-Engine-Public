#pragma once
// ===========================================================================
// 概要: 汎用の vCam 用 System 群（追従・注視・揺れ・自由移動）
//
// 責務:
//   - CameraFollowSystem: CameraBodyFollow に従って resultPos を書く
//   - CameraLookAtSystem: CameraAimLookAt に従って resultLookAt を書く
//   - CameraShakeSystem: 揺れ要求を受けて、Body が確定した結果に揺れを加算する
//   - CameraFreeControlSystem: デバッグ用の自由移動カメラを操作する
//
// 備考: すべて R01_Camera ステージで動き、CameraBrainSystem(R01b) より前に結果を確定させる
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Camera/Components/VirtualCameraComponents.h"
#include "Engine/Transform/Components/TransformComponent.h"

#include <atomic>
#include <cstdint>
#include <vector>

// Body: ターゲットへ追従する
class CameraFollowSystem : public CCL::ECS::IfSystem<
    CameraFollowSystem,
    CCL::ECS::Write<VirtualCamera>,
    CCL::ECS::Read<CameraBodyFollow>>
{
public:
    CameraFollowSystem() : IfSystem("CameraFollowSystem") {}
    void Update(float dt) override;
};

// Aim: ターゲットを注視する
class CameraLookAtSystem : public CCL::ECS::IfSystem<
    CameraLookAtSystem,
    CCL::ECS::Write<VirtualCamera>,
    CCL::ECS::Read<CameraAimLookAt>>
{
public:
    CameraLookAtSystem() : IfSystem("CameraLookAtSystem") {}
    void Update(float dt) override;

    // 同じ VirtualCamera を書く Follow と競合する。どちらが先でも結果は同じだが、
    // 順序不定の警告を消し、デバッグ時の再現性を確保するため明示しておく
    std::vector<const char *> GetRunAfter() const override { return {"CameraFollowSystem"}; }
};

// Shake: Body が確定した結果に揺れを加算する
class CameraShakeSystem : public CCL::ECS::IfSystem<
    CameraShakeSystem,
    CCL::ECS::Write<VirtualCamera>,
    CCL::ECS::Write<CameraShake>>
{
public:
    CameraShakeSystem() : IfSystem("CameraShakeSystem") {}

    // 揺れの要求（CameraShakeRequestEvent）の購読を始める
    // 画面を揺らすのはこの経路だけ。発生源がカメラを直接触ると、どの揺れが消されたのか追えない
    void Initialize() override;

    void Update(float dt) override;

    // 揺れが届いていないのか、弱すぎて見えないのかを切り分ける表示
    // 揺れは trauma の2乗に比例するので、効いていても見えないことがある。揺らせるカメラが 0 台なら CameraShake の付け忘れ
    void OnGui() override;

    // 揺れは Body が決めた resultPos / resultLookAt に足すので、すべての Body の System より後に走る
    // 先に走ると、足した揺れが Body に上書きされて消える
    // Game 側の Body（CameraActionTPSSystem）は、Engine が名前を知らずに済むよう向こうが GetRunBefore で宣言する
    std::vector<const char *> GetRunAfter() const override
    {
        return {"CameraFollowSystem", "CameraLookAtSystem", "CameraFreeControlSystem"};
    }

private:
    // 32bit 整数ハッシュ。ノイズの格子点から擬似乱数を作る
    static std::uint32_t HashUInt(std::uint32_t x);

    // 時刻で動く1次元の勾配ノイズ（Perlin）。おおむね -1〜1
    // 同じ (seed, t) なら同じ値なので、fps によらず波形が変わらない
    static float GradientNoise(std::uint32_t seed, float t);

    // このフレームに届いた揺れ要求のうち最大のもの。Update で消費する
    //
    // イベントはロジック側の任意のスレッドから同期的に飛んでくる。ここで一度
    // 受け止めることで、CameraShake への書き込みを Update の1箇所に閉じる
    std::atomic<float> m_pendingTrauma{0.0f};

    // --- エディタ表示用のスナップショット（Update が毎フレーム更新する） ---
    int   m_shakableCameraCount = 0;    // CameraShake を持つカメラの数
    float m_lastTrauma          = 0.0f; // 最後に見たカメラの蓄積量
};

// Free: デバッグ用の自由移動カメラ
class CameraFreeControlSystem : public CCL::ECS::IfSystem<
    CameraFreeControlSystem,
    CCL::ECS::Write<VirtualCamera>,
    CCL::ECS::Write<CameraBodyFree>,
    CCL::ECS::Write<TransformComponent>>
{
public:
    CameraFreeControlSystem() : IfSystem("CameraFreeControlSystem") {}
    void Update(float dt) override;

private:
    // ポーズ中・UI の操作中・エディタのほかのウィンドウの操作中なら true（TPS カメラと同じ条件）
    bool IsInputBlocked() const;
};
