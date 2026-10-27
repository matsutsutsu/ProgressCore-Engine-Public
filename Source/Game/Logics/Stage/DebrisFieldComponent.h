#pragma once
// ===========================================================================
// 概要: ステージの外を回るがれきの帯。帯全体を Entity 1つで持つ。
//
// 責務:
//   - 帯の形（半径・高さ）と動き（公転・自転・上下の揺れ）の設定を持つ
//   - 岩の種類（プレハブから読んだモデル）と、岩1個ごとの乱数を実行時に持つ
//
// 備考: 岩は Entity にしない。1000個以上を Entity にすると、生成のたびに World の確定が走り、
//       Transform・カリング・影の System も毎フレームその数だけ回るため。
//       岩は DebrisFieldSystem が RenderQueue に直接積む。
//       時間の倍率は、この Entity の TimeStateComponent 1つで帯全体に掛ける。
// ===========================================================================
#include "Engine/Graphics/Components/Core/ModelComponent.h"

#include <DirectXMath.h>
#include <cstdint>
#include <string>
#include <vector>

// 岩の種類。プレハブ1つから作る
struct DebrisKind
{
    ModelComponent    model;            // プレハブの Model と同じモデル（解放もこれが行う）
    DirectX::XMFLOAT3 scale{1, 1, 1};   // プレハブの Transform の scale
};

// 岩1個。範囲を掛ける前の 0〜1 の値で持つ。
// インスペクタで範囲を変えても、作り直さずにそのまま反映できるようにするため
struct DebrisRock
{
    uint16_t          kind    = 0;    // DebrisFieldComponent::kinds の添字
    float             angle0  = 0.0f; // 公転の初期角度 [rad]
    float             radiusT = 0.0f;
    float             heightT = 0.0f;
    float             scaleT  = 0.0f;
    float             speedT  = 0.0f; // 公転の速さのばらつき（-1〜1）
    float             spinT   = 0.0f; // 自転の速さのばらつき（-1〜1。負なら逆回り）
    float             phase   = 0.0f; // 自転と上下の揺れの位相 [rad]
    DirectX::XMFLOAT3 spinAxis{0, 1, 0}; // 自転の軸（単位ベクトル）
};

struct DebrisFieldComponent
{
    // --- 生成（変えると岩を作り直す） ---
    std::string prefabFolder = "Assets/Prefabs/Stage/Props"; // このフォルダのプレハブを岩の種類にする
    int         count        = 1200;
    int         seed         = 1;

    // --- 帯の形（この Entity の位置が中心） ---
    float radiusMin = 230.0f; // [m]
    float radiusMax = 320.0f;
    float heightMin = -20.0f;
    float heightMax = 60.0f;

    // --- 動き ---
    float orbitSpeed   = 1.5f;  // 公転の速さ [deg/s]
    float speedJitter  = 0.3f;  // 公転の速さのばらつき（0.3 なら ±30%）
    float spinSpeedMax = 15.0f; // 自転の最高速 [deg/s]
    float bobAmplitude = 1.5f;  // 上下の揺れ幅 [m]
    float bobFrequency = 0.15f; // 上下の揺れ [Hz]

    // --- 見た目 ---
    float scaleMin = 0.3f; // プレハブの scale に掛ける倍率
    float scaleMax = 1.2f;

    // --- 実行時（保存しない） ---
    float                   elapsed = 0.0f; // 時間の倍率を掛けた経過時間 [s]
    std::vector<DebrisKind> kinds;
    std::vector<DebrisRock> rocks;
    std::string             builtFolder;     // kinds を読んだフォルダ
    int                     builtCount = -1; // rocks を作ったときの count
    int                     builtSeed  = 0;  // rocks を作ったときの seed
};
