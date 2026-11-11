#pragma once
// 仮想カメラ（VirtualCamera）と、その位置・注視・揺れを決めるコンポーネント
// 各 Body/Aim の System が VirtualCamera の result* を書き、CameraBrainSystem が優先度の一番高いものを使う
#include "Engine/Serialization/Core/EntityLink.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cstdint>

// 仮想カメラの基本情報
struct VirtualCamera
{
    int               priority     = 0;         // 大きいほど優先して採用される
    float             blendTime    = 1.0f;      // このカメラへ切り替わるときのブレンド時間（秒）
    float             fov          = 45.0f;     // 画角（度）
    float             nearClip     = 0.1f;      // ニアクリップ（m）
    float             farClip      = 1000.0f;   // ファークリップ（m）
    DirectX::XMFLOAT3 resultPos    = {0, 0, 0}; // Body が確定したカメラ位置
    DirectX::XMFLOAT3 resultLookAt = {0, 0, 1}; // Aim/Body が確定した注視点
    DirectX::XMFLOAT3 resultUp     = {0, 1, 0}; // 上方向。傾け演出ではここを傾ける

    // 初期化フラグ
    //   isValid は「CameraBrainSystem が採用してよいか」だけを表す
    //   位置と注視点は別々のシステム(Follow / LookAt)が書きうるので、
    //   「どちらが既に書けたか」は個別のフラグで持つ
    //   1つのフラグを共有すると、先に走った側がフラグを立てた時点で
    //   もう片方が初回ワープをスキップし、原点から補間を始めてしまう
    bool posInitialized  = false; // resultPos が一度でも確定したか
    bool lookInitialized = false; // resultLookAt が一度でも確定したか
    bool isValid         = false; // 位置・注視点が揃って採用可能か

    // 位置・注視点の両方を自前で確定させる Body（TPS/Free/ムービー用カメラ など）用
    void MarkResolved()
    {
        posInitialized  = true;
        lookInitialized = true;
        isValid         = true;
    }

    // 片側だけを書くシステム（Follow / LookAt）から呼ぶ。両方揃って初めて有効化する
    void RefreshValid() { isValid = posInitialized && lookInitialized; }
};

// 位置の決め方: ターゲットへ追従する（Body）
struct CameraBodyFollow
{
    CCL::Serialization::EntityLink target;              // 追従対象
    DirectX::XMFLOAT3              offset  = {0, 0, 0}; // ターゲットからのオフセット（m、ワールド軸）
    float                          damping = 10.0f;     // 追従速度（CCL::Math::Damp の speed）
    bool                           lockY   = false;     // true: 高さ(Y)をターゲットに追従させず lockedY に固定する
    float                          lockedY = 0.0f;      // lockY 有効時に維持する高さ（m）
};

// 向きの決め方: ターゲットを注視する（Aim）
struct CameraAimLookAt
{
    CCL::Serialization::EntityLink target;              // 注視対象
    DirectX::XMFLOAT3              offset  = {0, 0, 0}; // 注視点のオフセット（m、ワールド軸）
    float                          damping = 10.0f;     // 追従速度（CCL::Math::Damp の speed）
};

// 画面の揺れ。強さは trauma（衝撃の蓄積 0〜1）だけで表し、振幅・周波数・減衰はここにまとめる
// 上書きでなく蓄積にするのは、着地の強い揺れが直後の弱い被弾で消えないようにするため（1.0 で頭打ち）
// 揺れは経過時間で動くノイズで作る。毎フレーム rand() だと、fps が上がるほど振動も速くなる
// 揺らしてよいカメラの印も兼ねるので、揺らしたくないカメラには付けない
struct CameraShake
{
    // --- チューニング（シリアライズする） ---
    float amplitude     = 3.0f;  // trauma=1.0 のときの最大振幅（m）
    float frequency     = 22.0f; // 揺れの周波数(Hz)。大きいほど細かく震える
    float rotationRatio = 0.35f; // 注視点だけ余分にずらす割合。0=平行移動のみ, 大きいほど画面が振れる
    float decayRate     = 1.2f;  // trauma が1秒あたりに減る量。1/decayRate が最大の揺れの長さ(秒)

    // --- 実行時内部状態（シリアライズしない） ---
    float         trauma  = 0.0f; // 衝撃の蓄積量(0..1)。0 で停止
    float         elapsed = 0.0f; // 揺れ始めからの経過時間(秒)。ノイズの位相に使う
    std::uint32_t seed    = 0u;   // 揺れごとに変える乱数シード。毎回同じ揺れにしないため

    // 衝撃を積む。書き込みは CameraShakeSystem からのみ行う
    void AddTrauma(float amount)
    {
        if (amount <= 0.0f) return;

        // 波形を作り直すのは止まっている時だけ。揺れの最中に種や位相を変えると
        // ノイズが不連続に飛び、揺れではなく「カメラのワープ」に見える
        if (trauma <= 0.0f)
        {
            elapsed = 0.0f;
            seed    = seed * 1664525u + 1013904223u;
        }

        trauma = std::min(trauma + amount, 1.0f);
    }
};

// 自由移動カメラ（デバッグ用）。右クリック中にマウスで視点、WASD/QE で移動する
struct CameraBodyFree
{
    float moveSpeed    = 10.0f; // 移動速度（m/秒）。右クリック中のホイールで変わる
    float lookSpeed    = 0.2f;  // 視点の回転感度（度/ピクセル）
    float currentYaw   = 0.0f;  // 現在の水平角（度）
    float currentPitch = 0.0f;  // 現在の垂直角（度）
};
