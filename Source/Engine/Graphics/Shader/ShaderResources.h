// Graphics/Shader/ShaderResources.h
//
// ==================================================================
// 【レジスタスロットの単一ソース】
// ここに1回書くだけで C++ と HLSL の両方に適用される。
//
// スロットは「更新頻度の層」で整理している:
//   Per-frame   毎フレーム1回   Graphics / EnvironmentSystem がバインド
//   Per-pass    パスごと        各 IRenderPass がバインド（ポストプロセス等）
//   Per-material 材質ごと        Shader::Update がバインド
//   Per-object  ドローごと       ModelRenderer がバインド
//
// 同じスロット番号を複数の名前が共有している箇所がある（例: b0 が
// CbMaterial と CbBloom）。これは意図的で、2つの用途は時間的に排他
// （材質描画とポストプロセスは同時に起きない）なため衝突しない。
// 詳細は ShaderBindingContract.md を参照。
// ==================================================================

#ifndef SHADER_RESOURCES_H
#define SHADER_RESOURCES_H

// ==================================================================
// Constant Buffers (CB)
// ==================================================================

// --- Per-material (Shader::Update がバインド) ---
#define SLOT_CB_MATERIAL        0   // 材質パラメータ (baseColor, roughness, useXXXMap...)
#define SLOT_CB_MESH            1   // メッシュ単位パラメータ
#define SLOT_CB_SKYBOX          2

// --- Per-pass / ポストプロセス (各パスがバインド) ---
//     ※ b0/b1 は per-material と物理スロットを共有（時間的に排他）
#define SLOT_CB_BLOOM           0
#define SLOT_CB_TONEMAP         1
#define SLOT_CB_DISTORTION      2   // ディストーション用のスロットを確保
#define SLOT_CB_RADIALBLUR      2   // 放射状ブラー(Distortionとb2を共有=時間的に排他)
#define SLOT_CB_VIGNETTE        2   // ビネット(b2共有=時間的に排他)

// --- Per-frame (Graphics / EnvironmentSystem がバインド) ---
#define SLOT_CB_SCENE           8   // viewProjection, camera, dirLight ... ★Graphics::Update のみ書込
#define SLOT_CB_LIGHT           9   // 点光源/スポットライト配列
#define SLOT_CB_SHADOW          10  // カスケード行列, バイアス
#define SLOT_CB_FOG             11  // 高さフォグ, ノイズ, リム

// --- Per-object (ModelRenderer がバインド) ---
#define SLOT_CB_SKELETON        12  // boneTransforms[256]

// --- Per-emitter (GPUParticleUpdateSystem / ParticleRenderer がバインド) ---
// Static : Desc 由来の設定値。変更時のみ upload (176 bytes)
// Dynamic: ランタイム値。毎フレーム upload (48 bytes)
// ★ 2スロットを連続して取得し、SetConstantBuffers(STATIC, 2, ...) で一括バインド可能
#define SLOT_CB_PARTICLE_STATIC  6   // 旧 13
#define SLOT_CB_PARTICLE_DYNAMIC 7   // 旧 14 (範囲外だった)

// ==================================================================
// Shader Resources (SRV)
// ==================================================================

// --- Per-material テクスチャ (Shader::Update がバインド) ---
#define SLOT_SRV_MAT_TEX0       0   // Albedo
#define SLOT_SRV_MAT_TEX1       1   // Normal
#define SLOT_SRV_MAT_TEX2       2   // MetalRough / Occlusion
#define SLOT_SRV_MAT_TEX3       3   // Emissive 等の追加マップ
//     ↑ t3 は per-object のパーティクルバッファと物理スロットを共有。
//        ジオメトリ描画では4枚目の材質テクスチャ、パーティクル描画では
//        構造化バッファ。両者は同時に発生しないため衝突しない。
#define SLOT_SRV_PARTICLE_BUF   3   // (別名) パーティクル描画時の構造化バッファ
#define SLOT_SRV_SCENE_DEPTH    4

// --- Per-pass / ポストプロセス ---
//     ※ t0/t1 は per-material と物理スロットを共有（時間的に排他）
#define SLOT_SRV_BLOOM_IN       0
#define SLOT_SRV_BLOOM_HIGHRES  1
#define SLOT_SRV_PP_DISTORTION_MAP 2 // 将来的に歪みテクスチャを刺すスロット

// --- Per-frame グローバルリソース (Graphics / EnvironmentSystem) ---
#define SLOT_SRV_FOG_NOISE      9
#define SLOT_SRV_SHADOW         10  // Texture2DArray (カスケード)
#define SLOT_SRV_GLOBAL_NOISE   13

// --- Per-object ---
#define SLOT_SRV_INSTANCE       11  // StructuredBuffer<InstanceData>
#define SLOT_SRV_BONE           12

// --- IBL (Per-frame, EnvironmentSystem) ---
#define SLOT_SRV_IBL_IRRADIANCE 14
#define SLOT_SRV_IBL_PREFILTER  15
#define SLOT_SRV_IBL_BRDFLUT    16
#define SLOT_SRV_SKYBOX_BG      17  // Skybox 描画専用の高解像度テクスチャ

// ==================================================================
// Unordered Access Views (UAV)
// ==================================================================
#define SLOT_UAV_PARTICLE_BUF   0

// ==================================================================
// Samplers (SMP)
// ==================================================================
#define SLOT_SMP_DEFAULT        0
#define SLOT_SMP_LINEAR         1   // ポストプロセス/材質の標準リニアサンプラ
#define SLOT_SMP_ANISOTROPIC    2
#define SLOT_SMP_IBL            3
#define SLOT_SMP_SHADOW         10  // SamplerComparisonState
//     ※ s1 は Bloom 用サンプラと共有
#define SLOT_SMP_BLOOM          1

// ==================================================================
// 【HLSL専用】文字結合ギミック (C++コンパイラは無視する)
// ==================================================================
#ifndef __cplusplus
    // マクロ展開の仕様上、数値を "b" などの文字と結合するには
    // この「2段階クッション」が絶対に必要。
#define REG_CB_IMPL(slot)  b##slot
#define REG_SRV_IMPL(slot) t##slot
#define REG_UAV_IMPL(slot) u##slot
#define REG_SMP_IMPL(slot) s##slot

#define REG_CB(slot)  REG_CB_IMPL(slot)
#define REG_SRV(slot) REG_SRV_IMPL(slot)
#define REG_UAV(slot) REG_UAV_IMPL(slot)
#define REG_SMP(slot) REG_SMP_IMPL(slot)
#endif

#endif // SHADER_RESOURCES_H