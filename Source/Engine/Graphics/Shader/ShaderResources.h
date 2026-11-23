// ===========================================================================
// 概要: 定数バッファ・テクスチャ・サンプラーのレジスタ番号を、C++ と HLSL で共有する
//
// 備考: スロットはバインドする頻度（フレーム・パス・マテリアル・ドローごと）でまとめる
//       同じ番号を複数の名前で使う所がある（b0 は CbMaterial と CbBloom）が、同時には使わない
//       一覧は Shader/SHADER_ARCHITECTURE.md。HLSL も読むのでインクルードガードを使う
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

// --- 専用シェーダー（マテリアルを使わない描画。その描画の中でだけ使う） ---
#define SLOT_CB_TRAIL 0    // トレイルの viewProjection
#define SLOT_CB_RT_DEBUG 0 // エディタのレンダーターゲット表示の設定

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

// --- ポストプロセス（各エフェクトがバインド） ---
//     t0・t1 はマテリアルと同じ番号を使う（同時には使わない）
#define SLOT_SRV_POST_INPUT0 0 // 前の段の絵（エフェクトの入力）
#define SLOT_SRV_POST_INPUT1 1 // 2枚目の入力（ブルームの高解像度側・SSAO のぼかした AO）

// --- 専用シェーダー（マテリアルを使わない描画。その描画の中でだけ使う） ---
#define SLOT_SRV_TRAIL_TEX 0       // トレイルのテクスチャ
#define SLOT_SRV_SPRITE 0          // スプライトのテクスチャ
#define SLOT_SRV_RT_DEBUG 0        // エディタで表示するレンダーターゲット（Texture2D）
#define SLOT_SRV_RT_DEBUG_ARRAY 1  // 同じく Texture2DArray のとき

// --- フレームごと（Graphics / EnvironmentSystem） ---
#define SLOT_SRV_SHADOW 10 // Texture2DArray（カスケード）
#define SLOT_SRV_GLOBAL_NOISE 13

// --- ドローごと ---
#define SLOT_SRV_INSTANCE 11 // StructuredBuffer<InstanceData>

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
#define SLOT_SMP_DEFAULT 0 // スプライトとエディタのレンダーターゲット表示
#define SLOT_SMP_LINEAR 1  // マテリアル・ポストプロセス・トレイル。何を置くかは描く側が決める
#define SLOT_SMP_IBL 3
#define SLOT_SMP_SHADOW 10 // SamplerComparisonState

// ==================================================================
// HLSL 用: スロット番号から register の名前を作るマクロ（C++ では使わない）
// ==================================================================
#ifndef __cplusplus
// REG_CB(SLOT_CB_SCENE) → b8 のように、番号の前に文字を付ける
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

// ==================================================================
// C++ 用: 1回の Set*(先頭の番号, 数, ...) でまとめてバインドしている組
// 番号が連続していないと、後ろのものが別のスロットに入る
// ==================================================================
#ifdef __cplusplus
static_assert(SLOT_CB_PARTICLE_DYNAMIC == SLOT_CB_PARTICLE_STATIC + 1, "パーティクルの定数バッファは連番にする");
static_assert(SLOT_SRV_POST_INPUT1 == SLOT_SRV_POST_INPUT0 + 1, "ポストプロセスの入力は連番にする");
static_assert(SLOT_SRV_IBL_PREFILTER == SLOT_SRV_IBL_IRRADIANCE + 1 &&
                  SLOT_SRV_IBL_BRDFLUT == SLOT_SRV_IBL_IRRADIANCE + 2,
              "IBL の3枚は連番にする");
#endif

#endif // SHADER_RESOURCES_H
