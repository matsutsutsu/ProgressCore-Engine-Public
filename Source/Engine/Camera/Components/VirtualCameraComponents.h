#pragma once
#include "Engine/Serialization/Core/EntityLink.h" // EntityLinkを使用するため
#include <DirectXMath.h>
#include <algorithm> // for max
#include <cstdint>

// 1. 仮想カメラの基本情報 (vCam)
struct VirtualCamera
{
    int               priority     = 0;
    float             blendTime    = 1.0f;
    float             fov          = 45.0f;
    float             nearClip     = 0.1f;
    float             farClip      = 1000.0f;
    DirectX::XMFLOAT3 resultPos    = {0, 0, 0};
    DirectX::XMFLOAT3 resultLookAt = {0, 0, 1};
    DirectX::XMFLOAT3 resultUp     = {0, 1, 0};

    // -----------------------------------------------------------------
    // 初期化フラグ
    //   isValid は「CameraBrainSystem が採用してよいか」だけを表す。
    //   位置と注視点は別々のシステム(Follow / LookAt)が書きうるので、
    //   「どちらが既に書けたか」は個別のフラグで持つ。
    //   1bit を両者で共有していた頃は、先に走った側のフラグを見て
    //   もう片方が初回ワープをスキップし、原点から補間を始めていた。
    // -----------------------------------------------------------------
    bool posInitialized  = false; // resultPos が一度でも確定したか
    bool lookInitialized = false; // resultLookAt が一度でも確定したか
    bool isValid         = false; // 位置・注視点が揃って採用可能か

    // 位置・注視点の両方を自前で確定させる Body（TPS/Free/Dolly）用。
    void MarkResolved()
    {
        posInitialized  = true;
        lookInitialized = true;
        isValid         = true;
    }

    // 片側だけを書くシステム（Follow / LookAt）から呼ぶ。両方揃って初めて有効化する。
    void RefreshValid() { isValid = posInitialized && lookInitialized; }
};

// 2. 「移動」戦略: ターゲット追従 (Body)
struct CameraBodyFollow
{
    // 生のIDではなく安全なEntityLinkを使用する
    CCL::Serialization::EntityLink target;
    DirectX::XMFLOAT3              offset  = {0, 0, 0};
    float                          damping = 10.0f;
    bool                           lockY   = false; // true: 高さ(Y)をターゲットに追従させず lockedY に固定する
    float                          lockedY = 0.0f;  // lockY 有効時に維持する高さ(m)
};

// 3. 「回転」戦略: ターゲット注視 (Aim)
struct CameraAimLookAt
{
    // EntityLinkを使用する
    CCL::Serialization::EntityLink target;
    DirectX::XMFLOAT3              offset  = {0, 0, 0};
    float                          damping = 10.0f;
};

// 4. 「揺れ」演出用コンポーネント (Shake)
//    揺れの強さは「トラウマ(衝撃の蓄積量 0..1)」だけで表す。発生源は trauma を積み、
//    振幅・周波数・減衰はすべてここに集約する。
//
//    なぜ「上書き」ではなく「蓄積」か:
//      以前は SetShake(振幅, 時間) で丸ごと上書きしていたため、着地の強い揺れが
//      直後の弱い被弾で消えるという「後から来た方が勝つ」挙動になっていた。
//      蓄積なら同時に起きた衝撃が重なり、1.0 で頭打ちなので連打しても破綻しない。
//
//    なぜ「揺れる時間」を持たないか:
//      duration を発生源が決めると、強さと長さの組み合わせが発生源の数だけ増える。
//      蓄積量と減衰速度から長さが決まる形にすると、強い衝撃ほど自然に長く残る。
//
//    揺れは「毎フレーム rand()」ではなく「経過時間で駆動するノイズ」で作る。
//    前者は fps が上がるほど振動周波数まで上がってしまい、60fps と 144fps で
//    見え方が変わっていた。時間駆動なら fps に依らず同じ波形になる。
//
//    このコンポーネントが「揺れてよいカメラ」の目印も兼ねる。揺らしたくないカメラ
//    (導入演出・デバッグ用のフリーカメラ)には付けなければよく、システム側に
//    対象を選ぶ条件を書かずに済む。
struct CameraShake
{
    // --- チューニング（シリアライズする） ---
    float amplitude     = 3.0f;  // trauma=1.0 のときの最大振幅(ワールド単位)
    float frequency     = 22.0f; // 揺れの周波数(Hz)。大きいほど細かく震える。
    float rotationRatio = 0.35f; // 注視点だけ余分にずらす割合。0=平行移動のみ, 大きいほど画面が振れる。
    float decayRate     = 1.2f;  // trauma が1秒あたりに減る量。1/decayRate が最大の揺れの長さ(秒)。

    // --- 実行時内部状態（シリアライズしない） ---
    float         trauma  = 0.0f; // 衝撃の蓄積量(0..1)。0 で停止。
    float         elapsed = 0.0f; // 揺れ始めからの経過時間(秒)。ノイズの位相に使う。
    std::uint32_t seed    = 0u;   // 揺れごとに変える乱数シード。毎回同じ揺れにしないため。

    // 衝撃を積む。書き込みは CameraShakeSystem からのみ行う。
    void AddTrauma(float amount)
    {
        if (amount <= 0.0f) return;

        // 波形を作り直すのは止まっている時だけ。揺れの最中に種や位相を変えると
        // ノイズが不連続に飛び、揺れではなく「カメラのワープ」に見える。
        if (trauma <= 0.0f)
        {
            elapsed = 0.0f;
            seed    = seed * 1664525u + 1013904223u;
        }

        trauma = std::min(trauma + amount, 1.0f);
    }
};

// ---------------------------------------------------------
// 新しい拡張: 自由移動用ボディコンポーネント
// ---------------------------------------------------------
struct CameraBodyFree
{
    float moveSpeed    = 10.0f;
    float lookSpeed    = 0.2f;
    float currentYaw   = 0.0f;
    float currentPitch = 0.0f;
};
