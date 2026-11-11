#pragma once
#include "Engine/Graphics/Resource/ResourceTypes.h"

// 空（スカイボックス）と IBL（拡散 IEM・鏡面 PMREM）を1セットで差し替える（シングルトン）
// 空の見た目と陰影は同じ空から作らないと食い違うので、3枚のキューブマップを1つにまとめる
// パスが空なら EnvironmentSystem の初期設定の空を使う。キューブマップは cmft などで HDR から作る
struct SkyboxComponent
{
    // --- パス（保存する） ---
    char skyboxTexturePath[256] = {}; // 背景のスカイボックス（TextureCube .dds）
    char irradiancePath[256]    = {}; // IBL 拡散用の IEM（TextureCube .dds）
    char prefilterPath[256]     = {}; // IBL 鏡面用の PMREM（TextureCube .dds。ミップごとに roughness が違う）

    // 空の背景の明るさの倍率（保存する）。IBL（物の陰影）には掛からない
    // 空はアルベドを掛けずにそのまま画面に出るので、物に合わせた露出だと空だけ白く飛ぶ。その分をここで下げる
    float skyIntensity = 1.0f;

    // --- 読み込んだテクスチャ（実行時のみ。保存しない） ---
    // パスを変えたら無効にし、EnvironmentSystem に読み直させる
    TextureHandle skyboxHandle;
    TextureHandle irradianceHandle;
    TextureHandle prefilterHandle;
};
