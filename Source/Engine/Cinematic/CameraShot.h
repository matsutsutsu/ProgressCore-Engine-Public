#pragma once
// ===========================================================================
// 概要: ムービーの1ショット（カメラをどこからどこへ動かすか）のデータ。
//
// 責務:
//   - ムービーのどこからどこまでを映すか（開始時刻と長さ）を持つ
//   - 構図の基準にする対象（Entity の名前）を持つ
//   - カメラの動かし方と速さのカーブ、前のショットからのつなぎ方を持つ
//   - カメラ位置・注視点・画角の、開始と終了の値を持つ
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
};
