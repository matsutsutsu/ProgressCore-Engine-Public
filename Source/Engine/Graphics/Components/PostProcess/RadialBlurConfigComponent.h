/// @file  RadialBlurConfigComponent.h
/// @brief 放射状ブラー(集中線)の設定を保持するコンポーネント。
///        役割     : ブラーのON/OFFとGPUパラメータ(中心・強さ・立ち上がり)を保持。
///        責務     : 値の保持のみ(描画はしない)。PostProcessSystem が読み取り、
///                   RadialBlurEffect へ毎フレーム転送する。
///        位置付け : シーンにsingletonとして置く「ポストプロセス設定」の一つ。
///        設計方針 : data は GPU定数バッファ構造体そのもの。ゲーム側(ダッシュ/被弾演出等)が
///                   strength を一時的に上げ、時間で0へ戻す使い方を想定。
#pragma once
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"

/// @brief 放射状ブラーのON/OFFとGPUパラメータを保持する設定コンポーネント。
/// @note  data が GPU定数バッファ構造体のため、シリアライズはパターンC(カスタム)。
struct RadialBlurConfigComponent
{
    bool enable = false;

    GpuData::CbRadialBlurData data{ { 0.5f, 0.5f }, 0.0f, 2.0f };
};
