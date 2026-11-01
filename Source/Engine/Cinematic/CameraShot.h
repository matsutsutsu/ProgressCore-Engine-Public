#pragma once
// ===========================================================================
// 概要: ムービーの1ショット（カメラをどこからどこへ動かすか）のデータ。
//
// 責務:
//   - ムービーのどこからどこまでを映すか（開始時刻と長さ）を持つ
//   - 構図の基準にする対象（Entity の名前）を持つ
//   - カメラの動かし方と速さのカーブ、前のショットからのつなぎ方を持つ
//   - カメラ位置・注視点・画角の、開始と終了の値を持つ
//   - 被写界深度のオン/オフとぼけ方を持つ
//
// 備考:
//  - 構造体にすることで１ショットをvector<CameraShot>で管理できるようになる
// ===========================================================================
#include <DirectXMath.h>
#include <cstdint>
#include <string>

// カメラの動かし方
enum class CameraMotion : std::uint8_t
{
    Truck, // 開始位置から終了位置へまっすぐ動く
    Orbit, // 注視点の周りを回り込む
};

// 台本に書く名前（CameraMotion の並びと同じ順）
inline constexpr const char *kCameraMotionNames[] = {"Truck", "Orbit"};

// 速さのカーブ（ショット内の進行度に掛ける）
enum class CameraEase : std::uint8_t
{
    Linear,    // 等速
    EaseIn,    // ゆっくり始まり、加速する
    EaseOut,   // 速く始まり、減速して止まる
    EaseInOut, // ゆっくり始まり、ゆっくり止まる
};

// 台本に書く名前（CameraEase の並びと同じ順）
inline constexpr const char *kCameraEaseNames[] = {"Linear", "EaseIn", "EaseOut", "EaseInOut"};

struct CameraShot
{
    float start    = 0.0f; // 開始時刻（秒、ムービーの先頭から）
    float duration = 1.0f; // 長さ（秒）

    std::string subject; // 基準にする Entity の名前（NameComponent）。空ならワールドの原点

    CameraMotion motion  = CameraMotion::Truck; // カメラの動かし方
    CameraEase   ease    = CameraEase::Linear;  // 速さのカーブ
    float        blendIn = 0.0f;                // 前のショットの絵から混ぜる時間（秒）。0 ならカット

    // 位置は subject からのオフセット（m）。subject の向きが基準（+X=右, +Y=上, +Z=前）
    DirectX::XMFLOAT3 lookStart = {0.0f, 0.0f, 0.0f}; // 開始の注視点
    DirectX::XMFLOAT3 lookEnd   = {0.0f, 0.0f, 0.0f}; // 終了の注視点

    // --- Truck のときだけ使う ---
    DirectX::XMFLOAT3 eyeStart = {0.0f, 10.0f, -20.0f}; // 開始のカメラ位置
    DirectX::XMFLOAT3 eyeEnd   = {10.0f, 10.0f, -20.0f}; // 終了のカメラ位置

    // --- Orbit のときだけ使う。中心は注視点、角度は subject の向きが基準（0=前, 90=右） ---
    float angleStart  = 0.0f;  // 開始の水平角（度）
    float angleEnd    = 90.0f; // 終了の水平角（度）
    float radiusStart = 5.0f;  // 開始の注視点からの水平距離（m）
    float radiusEnd   = 5.0f;  // 終了の注視点からの水平距離（m）
    float heightStart = 0.0f;  // 開始の注視点からの高さ（m）
    float heightEnd   = 0.0f;  // 終了の注視点からの高さ（m）

    float fovStart = 45.0f; // 開始の画角（度）
    float fovEnd   = 45.0f; // 終了の画角（度）

    // --- 被写界深度。ピントは注視点（カメラから注視点までの距離）に合わせる ---
    bool  dof        = false; // このショットでぼかすか
    float dofRange   = 2.0f;  // ピントの前後でぼけない幅（m）
    float dofFalloff = 6.0f;  // ぼけない幅の外から、最大のぼけに達するまでの距離（m）
    float dofMaxBlur = 0.01f; // 最大のぼけの半径（画面に対する割合）
};

// ショットが映す被写界深度。カメラが毎フレーム求め、ショットの切り替えで混ぜる
struct CinematicDof
{
    float focusDistance = 10.0f; // ピントの合う距離（m）
    float focusRange    = 2.0f;  // ピントの前後でぼけない幅（m）
    float falloff       = 6.0f;  // 最大のぼけに達するまでの距離（m）
    float maxBlur       = 0.0f;  // 最大のぼけの半径。0 ならぼかさない

    // a から b へ w（0〜1）だけ寄せる。ぼけの強さも寄せるので、オフのショットとの間はじわっと切り替わる
    static CinematicDof Lerp(const CinematicDof &a, const CinematicDof &b, float w)
    {
        CinematicDof out;
        out.focusDistance = a.focusDistance + (b.focusDistance - a.focusDistance) * w;
        out.focusRange    = a.focusRange + (b.focusRange - a.focusRange) * w;
        out.falloff       = a.falloff + (b.falloff - a.falloff) * w;
        out.maxBlur       = a.maxBlur + (b.maxBlur - a.maxBlur) * w;
        return out;
    }
};
