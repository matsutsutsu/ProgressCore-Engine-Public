// コンタクトシャドウの設定を保持するコンポーネント。
// 役割     : レイの長さ・厚み・バイアス・強さ・適用最大距離を保持。
// 責務     : 値の保持のみ。PostProcessSystem が読み取り Effect へ転送する。
// 位置付け : シーンにsingletonとして置く「ポストプロセス設定」の一つ。
// 設計方針 : SSAO同様、絵の基礎品質。既定ONで薄く効かせる。
#pragma once
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"

// コンタクトシャドウのON/OFFとGPUパラメータを保持する設定コンポーネント。
//
// data が GPU定数バッファ構造体のため、シリアライズはパターンC(カスタム)。
struct ContactShadowConfigComponent
{
    bool enable = true;

    // rayLength=0.3m / thickness=0.1m / bias=0.02 / intensity=0.7 / maxDistance=30m
    GpuData::CbContactShadowData data{0.3f, 0.1f, 0.02f, 0.7f, 30.0f, {}};
};
