// SSAO の設定を保持するコンポーネント。
// 役割     : 遮蔽の半径・強さ・バイアス・適用最大距離を保持。
// 責務     : 値の保持のみ。PostProcessSystem が読み取り SSAOEffect へ転送する。
// 位置付け : シーンにsingletonとして置く「ポストプロセス設定」の一つ。
// 設計方針 : 既定ONの控えめな設定。SSAOは演出ではなく「絵の基礎品質」なので、
//            常時薄く効かせるのが正しい使い方(強く掛けすぎると汚く見える)。
#pragma once
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"

// SSAOのON/OFFとGPUパラメータを保持する設定コンポーネント。
//
// data が GPU定数バッファ構造体のため、シリアライズはパターンC(カスタム)。
struct SSAOConfigComponent
{
    bool enable = true;

    // radius=0.5m / intensity=0.8 / bias=0.02 / maxDistance=50m
    GpuData::CbSSAOData data{0.5f, 0.8f, 0.02f, 50.0f};
};
