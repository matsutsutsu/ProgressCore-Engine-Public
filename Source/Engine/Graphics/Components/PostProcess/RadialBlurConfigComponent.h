// 放射状ブラー(集中線)の設定を保持するコンポーネント。
// 役割     : ブラーのON/OFFとGPUパラメータ(中心・強さ・立ち上がり)を保持。
// 責務     : 値の保持のみ(描画はしない)。PostProcessSystem が読み取り、
//            RadialBlurEffect へ毎フレーム転送する。
// 位置付け : シーンにsingletonとして置く「ポストプロセス設定」の一つ。
// 設計方針 : data は GPU定数バッファ構造体そのもの。演出からの一時的な
//            上乗せは overrides スロットへ書き、data は書き換えない。
#pragma once
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"
#include "Engine/Graphics/Components/PostProcess/PostEffectOverrides.h"

#include <algorithm>

// 放射状ブラーの一時上書きの中身。演出が出したい強度のピーク。
struct RadialBlurOverrideParams
{
    float strength = 0.0f;
};

// 放射状ブラーのON/OFFとGPUパラメータを保持する設定コンポーネント。
//
// data が GPU定数バッファ構造体のため、シリアライズはパターンC(カスタム)。
//
// スロットの仕組みと背景は PostEffectOverrides.h を参照。
struct RadialBlurConfigComponent
{
    bool enable = false;

    GpuData::CbRadialBlurData data{ { 0.5f, 0.5f }, 0.0f, 2.0f };

    PostEffectOverrides<RadialBlurOverrideParams> overrides;

    // 実際に描画へ渡す強度。素の値と全演出スロットの最大値。
    float ResolvedStrength() const
    {
        float out = enable ? data.blurStrength : 0.0f;
        overrides.ForEachActive([&out](const RadialBlurOverrideParams &p, float t) {
            out = (std::max)(out, p.strength * t);
        });
        return out;
    }
};
