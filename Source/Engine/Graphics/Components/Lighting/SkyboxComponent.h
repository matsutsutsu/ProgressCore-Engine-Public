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
//       パスが空なら初期設定（EnvironmentSystem.cpp の定数）の空を使う。パスからの読み込みは
//       EnvironmentSystem が、ハンドルが無効なときに行う（FogComponent のノイズと同じ）。
//       シーンにこのコンポーネントが無ければ、明るさも含めて全部が初期設定になる。
// ===========================================================================
#include "Engine/Graphics/Resource/ResourceTypes.h"

struct SkyboxComponent
{
    // --- パス（保存する） ---
    char skyboxTexturePath[256] = {}; // 背景のスカイボックス（TextureCube .dds）
    char irradiancePath[256]    = {}; // IBL 拡散用の IEM（TextureCube .dds）
    char prefilterPath[256]     = {}; // IBL 鏡面用の PMREM（TextureCube .dds。ミップごとに roughness が違う）

    // 空の背景の明るさの倍率（保存する）。IBL（物の陰影）には掛からない。
    // 空はアルベドを掛けずにそのまま画面に出るので、物に合わせた露出だと空だけ白く飛ぶ。その分をここで下げる
    float skyIntensity = 1.0f;

    // --- 読み込んだテクスチャ（実行時のみ。保存しない） ---
    // パスを変えたら無効にし、EnvironmentSystem に読み直させる。
    TextureHandle skyboxHandle;
    TextureHandle irradianceHandle;
    TextureHandle prefilterHandle;
};
