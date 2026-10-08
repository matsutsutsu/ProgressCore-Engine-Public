#pragma once
// ===========================================================================
// 概要: 仮想カメラ（vCam）と、その位置・注視・揺れを決めるコンポーネント群。
//
// 責務:
//   - VirtualCamera: 優先度・ブレンド時間・レンズ設定と、Body/Aim が書いた結果を持つ
//   - CameraBodyFollow / CameraAimLookAt: ターゲットへの追従と注視の設定
//   - CameraShake: trauma 方式の画面揺れの設定と状態
//   - CameraBodyFree: デバッグ用の自由移動カメラの設定と状態
//
// 備考: 各 Body/Aim システムが VirtualCamera の result* を書き、CameraBrainSystem が
//       最も優先度の高い vCam を選んでメインカメラへ反映する。
// ===========================================================================
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

    // 初期化フラグ。
    //   isValid は「CameraBrainSystem が採用してよいか」だけを表す。
    //   位置と注視点は別々のシステム(Follow / LookAt)が書きうるので、
    //   「どちらが既に書けたか」は個別のフラグで持つ。
    //   1つのフラグを共有すると、先に走った側がフラグを立てた時点で
    //   もう片方が初回ワープをスキップし、原点から補間を始めてしまう。
    bool posInitialized  = false; // resultPos が一度でも確定したか
    bool lookInitialized = false; // resultLookAt が一度でも確定したか
    bool isValid         = false; // 位置・注視点が揃って採用可能か

    // 位置・注視点の両方を自前で確定させる Body（TPS/Free/Timeline など）用。
    void MarkResolved()
    {
        posInitialized  = true;
        lookInitialized = true;
        isValid         = true;
    }

    // 片側だけを書くシステム（Follow / LookAt）から呼ぶ。両方揃って初めて有効化する。
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

/**
 * @brief 画面揺れ（Shake）。
 *
 * 揺れの強さは trauma（衝撃の蓄積量 0..1）だけで表す。発生源は trauma を積み、
 * 振幅・周波数・減衰はすべてここに集約する。
 *
 * なぜ「上書き」ではなく「蓄積」か:
 *   上書き（SetShake(振幅, 時間) のような形）にすると、着地の強い揺れが直後の
 *   弱い被弾で消える「後から来た方が勝つ」挙動になる。
 *   蓄積なら同時に起きた衝撃が重なり、1.0 で頭打ちなので連打しても破綻しない。
 *
 * なぜ「揺れる時間」を持たないか:
 *   duration を発生源が決めると、強さと長さの組み合わせが発生源の数だけ増える。
 *   蓄積量と減衰速度から長さが決まる形にすると、強い衝撃ほど自然に長く残る。
 *
 * 揺れは「毎フレーム rand()」ではなく「経過時間で駆動するノイズ」で作る。
 * 前者は fps が上がるほど振動周波数まで上がり、60fps と 144fps で見え方が変わる。
 * 時間駆動なら fps に依らず同じ波形になる。
 *
 * @note このコンポーネントが「揺れてよいカメラ」の目印も兼ねる。揺らしたくないカメラ
 *       （導入演出・デバッグ用のフリーカメラ）には付けなければよく、システム側に
 *       対象を選ぶ条件を書かずに済む。
 */
struct CameraShake
{
    // --- チューニング（シリアライズする） ---
    float amplitude     = 3.0f;  // trauma=1.0 のときの最大振幅（m）
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

// 自由移動カメラ（デバッグ用）。右クリック中にマウスで視点、WASD/QE で移動する
struct CameraBodyFree
{
    float moveSpeed    = 10.0f; // 移動速度（m/秒）。右クリック中のホイールで変わる
    float lookSpeed    = 0.2f;  // 視点の回転感度（度/ピクセル）
    float currentYaw   = 0.0f;  // 現在の水平角（度）
    float currentPitch = 0.0f;  // 現在の垂直角（度）
};
