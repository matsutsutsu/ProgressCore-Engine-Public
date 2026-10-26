// ===========================================================================
// 概要: 定数バッファ・テクスチャ・サンプラーのレジスタ番号を1か所で決めるヘッダー。
//
// 責務:
//   - C++ と HLSL の両方から include され、同じスロット番号を使わせる
//   - HLSL 向けに、番号から register(b0) などの名前を作るマクロを用意する
//
// 備考: スロットはバインドする頻度でまとめている。
//         フレームごと    Graphics / EnvironmentSystem がバインド
//         パスごと        各 RenderPass（ポストプロセスなど）がバインド
//         マテリアルごと  Shader::UpdateMaterialProperties がバインド
//         ドローごと      ModelConstantBuffers などがバインド
//       同じ番号を複数の名前で使っている所がある（例: b0 は CbMaterial と CbBloom）。
//       マテリアルの描画とポストプロセスは同時に起きないので、ぶつからない。
//       一覧は Shader/SHADER_ARCHITECTURE.md を参照。
//       HLSL からも読むので #pragma once ではなくインクルードガードを使う。
// ===========================================================================
#ifndef SHADER_RESOURCES_H
#define SHADER_RESOURCES_H

// ==================================================================
// 定数バッファ（b）
// ==================================================================

// --- マテリアルごと（Shader::UpdateMaterialProperties がバインド） ---
#define SLOT_CB_MATERIAL 0 // マテリアルの値（baseColor, roughness, useXXXMap など）
#define SLOT_CB_MESH 1     // マテリアルの値の続き（Basic の CbMesh）
                           //   名前は Mesh だが、メッシュごとの値ではない。b0〜b7 は
                           //   Shader::UpdateMaterialProperties がマテリアルごとに
                           //   0 に戻すので、マテリアル以外の値は置けない
#define SLOT_CB_SKYBOX 2

// --- パスごと・ポストプロセス（各パスがバインド） ---
//     b0〜b2 はマテリアルと同じ番号を使う（同時には使わない）
#define SLOT_CB_BLOOM 0
#define SLOT_CB_TONEMAP 1
#define SLOT_CB_RADIALBLUR 2 // 以下の b2 はどれも1つのエフェクトの中でだけ使う
#define SLOT_CB_VIGNETTE 2
#define SLOT_CB_CHROMATIC 2
#define SLOT_CB_COLORGRADING 2
#define SLOT_CB_DOF 2
#define SLOT_CB_SSAO 2
#define SLOT_CB_SSAO_BLUR 2
#define SLOT_CB_PAUSEBLUR 2

// --- UI の 2D パス（UIRenderer がドローごとにバインド） ---
#define SLOT_CB_UIGAUGE 0 // ゲージ1本分の値（UIShared.h）。2D パスではマテリアルもポストプロセスも動かない

// --- フレームごと（Graphics / EnvironmentSystem がバインド） ---
#define SLOT_CB_SCENE 8   // viewProjection・カメラ・平行光源など。書くのは Graphics::Update だけ
#define SLOT_CB_LIGHT 9   // 点光源・スポットライトの配列
#define SLOT_CB_SHADOW 10 // カスケードの行列・バイアス
#define SLOT_CB_FOG 11    // 距離フォグ・高さフォグ・空の地平線フォグ

// --- ドローごと（ModelConstantBuffers がバインド） ---
#define SLOT_CB_SKELETON 12 // boneTransforms[MAX_BONES]

// --- エミッターごと（GPUParticleUpdateSystem / ParticleRenderer がバインド） ---
// Static  : 設定（Desc）から作る値。変えたときだけ送る（240 バイト）
// Dynamic : 実行中に変わる値。毎フレーム送る（80 バイト）
// 隣り合う番号なので、SetConstantBuffers(STATIC, 2, ...) で1回でバインドできる
#define SLOT_CB_PARTICLE_STATIC 6
#define SLOT_CB_PARTICLE_DYNAMIC 7

// ==================================================================
// シェーダーリソース（t）
// ==================================================================

// --- マテリアルのテクスチャ（Shader::UpdateMaterialProperties がバインド） ---
#define SLOT_SRV_MAT_TEX0 0 // アルベド
#define SLOT_SRV_MAT_TEX1 1 // 法線
#define SLOT_SRV_MAT_TEX2 2 // メタリック・ラフネス / オクルージョン
#define SLOT_SRV_MAT_TEX3 3 // エミッシブなど
//     t3 はパーティクルのバッファと同じ番号。モデルの描画では4枚目のテクスチャ、
//     パーティクルの描画では StructuredBuffer として使う（同時には使わない）
#define SLOT_SRV_PARTICLE_BUF 3
#define SLOT_SRV_SCENE_DEPTH 4  // シーンの深度
#define SLOT_SRV_SCENE_COLOR 5  // SceneColorGrabPass が写したシーンの色
#define SLOT_SRV_SCENE_NORMAL 6 // NormalPrepass の法線

// --- パスごと・ポストプロセス ---
//     t0〜t2 はマテリアルと同じ番号を使う（同時には使わない）
#define SLOT_SRV_BLOOM_IN 0
#define SLOT_SRV_BLOOM_HIGHRES 1

// --- フレームごと（Graphics / EnvironmentSystem） ---
#define SLOT_SRV_SHADOW 10 // Texture2DArray（カスケード）
#define SLOT_SRV_GLOBAL_NOISE 13

// --- ドローごと ---
#define SLOT_SRV_INSTANCE 11 // StructuredBuffer<InstanceData>
#define SLOT_SRV_BONE 12     // 今は使っていない（ボーン行列は CbSkeleton で送る）

// --- IBL（フレームごと。EnvironmentSystem） ---
#define SLOT_SRV_IBL_IRRADIANCE 14
#define SLOT_SRV_IBL_PREFILTER 15
#define SLOT_SRV_IBL_BRDFLUT 16
#define SLOT_SRV_SKYBOX_BG 17 // スカイボックスの描画だけで使う高解像度のテクスチャ

// ==================================================================
// UAV（u）
// ==================================================================
#define SLOT_UAV_PARTICLE_BUF 0

// ==================================================================
// サンプラー（s）
// ==================================================================
#define SLOT_SMP_DEFAULT 0
#define SLOT_SMP_LINEAR 1 // ポストプロセスとマテリアルで使うリニア
#define SLOT_SMP_ANISOTROPIC 2
#define SLOT_SMP_IBL 3
#define SLOT_SMP_SHADOW 10 // SamplerComparisonState
#define SLOT_SMP_BLOOM 1   // s1 は SLOT_SMP_LINEAR と同じ番号

// ==================================================================
// HLSL 用: スロット番号から register の名前を作るマクロ（C++ では使わない）
// ==================================================================
#ifndef __cplusplus
// REG_CB(SLOT_CB_SCENE) → b8 のように、番号の前に文字を付ける。
// ## は引数をマクロ展開する前に連結するので、REG_CB_IMPL(SLOT_CB_SCENE) と直接書くと
// "bSLOT_CB_SCENE" になってしまう。REG_CB でいったん受けると、引数が 8 に展開されてから
// REG_CB_IMPL(8) が呼ばれ、b8 になる
#define REG_CB_IMPL(slot) b##slot
#define REG_SRV_IMPL(slot) t##slot
#define REG_UAV_IMPL(slot) u##slot
#define REG_SMP_IMPL(slot) s##slot

#define REG_CB(slot) REG_CB_IMPL(slot)
#define REG_SRV(slot) REG_SRV_IMPL(slot)
#define REG_UAV(slot) REG_UAV_IMPL(slot)
#define REG_SMP(slot) REG_SMP_IMPL(slot)
#endif

#endif // SHADER_RESOURCES_H
