#pragma once
#include "Engine/Graphics/Resource/ResourceTypes.h"

// ===========================================================================
// SkyboxComponent
//   「空(スカイボックス背景) と IBL(拡散IEM / 鏡面PMREM)」を1セットで差し替える
//   ための設定シングルトン。シーンに1つ置く。
//
//   なぜ1セットか:
//     スカイボックスの見た目と IBL の陰影は「同じ空から焼いた」ものでないと
//     破綻する（空は夕焼けなのに陰影は昼、等）。そのため3つのパスを1コンポーネントに
//     まとめ、常に揃った状態で差し替えられるようにする。
//
//   使い方:
//     ・3つのパスに .dds を指定するだけ（空欄なら EnvironmentSystem の既定にフォールバック）。
//     ・作成は外部ツール(cmft 等)で equirect HDR → skybox/iem/pmrem の3キューブマップを焼き、
//       ここに指す運用（B1）。BRDF LUT は空に依存しない定数なのでここには含めない。
//
//   実装メモ:
//     パスからテクスチャへの解決は EnvironmentSystem が毎フレーム遅延ロードする
//     （FogComponent.noiseTexturePath と同じ方式）。handle は非シリアライズのランタイム状態。
// ===========================================================================
struct SkyboxComponent
{
    // --- 差し替え対象のパス（シリアライズ） ---
    char skyboxTexturePath[256] = {}; // 背景スカイボックス (TextureCube .dds)
    char irradiancePath[256]    = {}; // IBL 拡散 IEM       (TextureCube .dds)
    char prefilterPath[256]     = {}; // IBL 鏡面 PMREM     (TextureCube .dds, roughness mips)

    // --- ランタイム状態（非シリアライズ。パスが変わったら Invalid にして再ロードさせる） ---
    TextureHandle skyboxHandle;
    TextureHandle irradianceHandle;
    TextureHandle prefilterHandle;
};
