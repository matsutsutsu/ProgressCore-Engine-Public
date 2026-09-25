#pragma once
// ===========================================================================
// 概要: 空（スカイボックスの背景）と IBL（拡散 IEM・鏡面 PMREM）を1セットで
//       差し替えるためのシングルトンコンポーネント。シーンに1つ置く。
//
// 責務:
//   - 3枚のキューブマップ（.dds）のパスを持つ
//   - 読み込んだテクスチャのハンドルを持つ
//
// 備考: 空の見た目と IBL の陰影は同じ空から作ったものでないと食い違う
//       （空は夕焼けなのに陰影は昼、など）。そのため3つを1コンポーネントにまとめる。
//       キューブマップは外部ツール（cmft など）で equirect HDR から3枚作って指定する。
//       BRDF LUT は空に依存しないので含めない。
//       パスが空なら EnvironmentSystem の既定の空を使う。パスからの読み込みは
//       EnvironmentSystem が、ハンドルが無効なときに行う（FogComponent のノイズと同じ）。
// ===========================================================================
#include "Engine/Graphics/Resource/ResourceTypes.h"

struct SkyboxComponent
{
    // --- パス（保存する） ---
    char skyboxTexturePath[256] = {}; // 背景のスカイボックス（TextureCube .dds）
    char irradiancePath[256]    = {}; // IBL 拡散用の IEM（TextureCube .dds）
    char prefilterPath[256]     = {}; // IBL 鏡面用の PMREM（TextureCube .dds。ミップごとに roughness が違う）

    // --- 読み込んだテクスチャ（実行時のみ。保存しない） ---
    // パスを変えたら無効にし、EnvironmentSystem に読み直させる。
    TextureHandle skyboxHandle;
    TextureHandle irradianceHandle;
    TextureHandle prefilterHandle;
};
