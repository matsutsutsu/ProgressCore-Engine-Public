#ifndef REGISTER_DEF_HLSLI
#define REGISTER_DEF_HLSLI

// =================================================================
// シェーダーレジスタ配置マップ
// =================================================================
//
// [ b0 - b7 ] : パス固有 (Material, Mesh, PostProcess) - 描画パスによって中身が変わる
// [ b8 -    ] : システム共通 (Scene, Light, Shadow, Fog...) - 常に固定
//
// [ t0 - t9 ] : パス固有 (DiffuseMap, BloomInput) - 描画パスによって中身が変わる
// [ t10 -   ] : システム共通 (ShadowMap, Instancing) - 常に固定
//
// =================================================================

// -----------------------------------------------------------------
// Constant Buffers (b)
// -----------------------------------------------------------------
// === Geometry Pass (通常描画) ===
#define REG_CB_MATERIAL         b0  // マテリアル設定
#define REG_CB_MESH             b1  // ワールド行列

// === Post Process Pass (画面効果) ===
// マテリアルと被ってもOK（同時に使わないため）
#define REG_CB_BLOOM            b0  // Bloomパラメータ (Threshold, Intensity...)
#define REG_CB_TONE_MAP         b1  // ToneMapパラメータ (Exposure...)


// === System Common (共通) ===
#define REG_CB_SCENE            b8  // ビュー・プロジェクション
#define REG_CB_LIGHT            b9  // ライト情報
#define REG_CB_SHADOW           b10 // 影設定
#define REG_CB_FOG              b11 // フォグ設定
#define REG_CB_SKELETON         b12 // スキニング行列
#define REG_CB_PARTICLE         b13 // パーティクルシステム設定

// -----------------------------------------------------------------
// Shader Resources (t)
// -----------------------------------------------------------------
// === Geometry Pass ===
#define REG_SRV_MATERIAL_TEX0   t0  // DiffuseMap
#define REG_SRV_MATERIAL_TEX1   t1  // NormalMap / ColorRamp
#define REG_SRV_MATERIAL_TEX2   t2  // SpecularMap / Noise
#define REG_SRV_PARTICLE_BUF    t3  // パーティクルバッファ(VS読取)

// === Post Process Pass ===
#define REG_SRV_BLOOM_INPUT     t0  // Bloom入力テクスチャ
#define REG_SRV_BLOOM_HIGHRES   t1  // Upsample用 高解像度テクスチャ

// === Fog ===
#define REG_SRV_FOG             t9  // フォグ

// === System Common ===
#define REG_SRV_SHADOW          t10 // シャドウマップ
#define REG_SRV_INSTANCE        t11 // インスタンシングデータ
#define REG_SRV_BONE            t12 // ボーンテクスチャ
#define REG_SRV_GLOBAL_NOISE    t13 // グローバルノイズ

// -----------------------------------------------------------------
// Unordered Access Views (u)
// -----------------------------------------------------------------
#define REG_UAV_PARTICLE_BUF    u0  // パーティクルバッファ(CS書込)

// -----------------------------------------------------------------
// Samplers (s)
// -----------------------------------------------------------------
#define REG_SMP_DEFAULT         s0  // Point/Clamp or Wrap
#define REG_SMP_LINEAR          s1  // Linear/Wrap
#define REG_SMP_ANISOTROPIC     s2  // Anisotropic
#define REG_SMP_SHADOW          s10 // PCF用比較サンプラー

#endif // REGISTER_DEF_HLSLI