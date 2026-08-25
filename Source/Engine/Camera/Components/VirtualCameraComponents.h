#pragma once
#include "Engine/Serialization/Core/EntityLink.h" // EntityLinkを使用するため
#include <DirectXMath.h>
#include <algorithm> // for max
#include <cstdint>

// 1. 仮想カメラの基本情報 (vCam)
struct VirtualCamera {
    int priority = 0;
    float blendTime = 1.0f;
    float fov = 45.0f;
    float nearClip = 0.1f;
    float farClip = 1000.0f;
    DirectX::XMFLOAT3 resultPos = { 0, 0, 0 };
    DirectX::XMFLOAT3 resultLookAt = { 0, 0, 1 };
    DirectX::XMFLOAT3 resultUp = { 0, 1, 0 };

    // -----------------------------------------------------------------
    // 初期化フラグ
    //   isValid は「CameraBrainSystem が採用してよいか」だけを表す。
    //   位置と注視点は別々のシステム(Follow / LookAt)が書きうるので、
    //   「どちらが既に書けたか」は個別のフラグで持つ。
    //   1bit を両者で共有していた頃は、先に走った側のフラグを見て
    //   もう片方が初回ワープをスキップし、原点から補間を始めていた。
    // -----------------------------------------------------------------
    bool posInitialized = false;  ///< resultPos が一度でも確定したか
    bool lookInitialized = false; ///< resultLookAt が一度でも確定したか
    bool isValid = false;         ///< 位置・注視点が揃って採用可能か

    /// @brief 位置・注視点の両方を自前で確定させる Body（TPS/Free/Dolly）用。
    void MarkResolved()
    {
        posInitialized = true;
        lookInitialized = true;
        isValid = true;
    }

    /// @brief 片側だけを書くシステム（Follow / LookAt）から呼ぶ。両方揃って初めて有効化する。
    void RefreshValid() { isValid = posInitialized && lookInitialized; }
};

// 2. 「移動」戦略: ターゲット追従 (Body)
struct CameraBodyFollow {
    // 生のIDではなく安全なEntityLinkを使用する
    CCL::Serialization::EntityLink target;
    DirectX::XMFLOAT3  offset = { 0, 0, 0 };
    float              damping = 10.0f;
    bool               lockY = false;   ///< true: 高さ(Y)をターゲットに追従させず lockedY に固定する
    float              lockedY = 0.0f;  ///< lockY 有効時に維持する高さ(m)
};

// 3. 「回転」戦略: ターゲット注視 (Aim)
struct CameraAimLookAt {
    // EntityLinkを使用する
    CCL::Serialization::EntityLink target;
    DirectX::XMFLOAT3  offset = { 0, 0, 0 };
    float              damping = 10.0f;
};

// 4. 「揺れ」演出用コンポーネント (Shake)
//    揺れは「毎フレーム rand()」ではなく「経過時間で駆動するノイズ」で作る。
//    前者は fps が上がるほど振動周波数まで上がってしまい、60fps と 144fps で
//    見え方が変わっていた。時間駆動なら fps に依らず同じ波形になる。
struct CameraShake {
    float amplitude   = 0.0f; ///< 揺れの最大振幅(m)
    float duration    = 0.0f; ///< 残り時間(秒)。0 で停止。
    float maxDuration = 1.0f; ///< 揺れ全体の長さ(秒)。減衰カーブの分母。
    float frequency   = 18.0f; ///< 揺れの周波数(Hz)。大きいほど細かく震える。
    float rotationRatio = 0.35f; ///< 注視点だけ余分にずらす割合。0=平行移動のみ, 大きいほど画面が振れる。

    // --- 実行時内部状態（シリアライズ不要） ---
    float        elapsed = 0.0f; ///< 揺れ開始からの経過時間(秒)。ノイズの位相に使う。
    std::uint32_t seed   = 0u;   ///< 揺れごとに変える乱数シード。毎回同じ揺れにしないため。

    void SetShake(float strength, float time)
    {
        amplitude   = strength;
        maxDuration = (time > 0.0f) ? time : 1.0f; // 0除算でtraumaがNaNになるのを防ぐ
        duration    = time;
        elapsed     = 0.0f;
        // 呼ばれるたびに別の波形になるよう、シードを進めておく。
        seed        = seed * 1664525u + 1013904223u;
    }
};

// ---------------------------------------------------------
// 新しい拡張: 自由移動用ボディコンポーネント
// ---------------------------------------------------------
struct CameraBodyFree {
    float moveSpeed = 10.0f;
    float lookSpeed = 0.2f;
    float currentYaw = 0.0f;
    float currentPitch = 0.0f;
};
