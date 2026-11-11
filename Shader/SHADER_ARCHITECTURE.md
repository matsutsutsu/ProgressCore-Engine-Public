# Shader 仕様書 / 使い方ガイド

このドキュメントは、**個々の HLSL を全部読まなくても、シェーダーの追加・利用・理解ができる**ことを目的とした単一の参照資料です。

> 関連ドキュメント: レンダリング経路の全体像は `Render_Architecture_Map.md`、スロット共有の詳細契約は `ShaderBindingContract.md`（存在すれば）を参照。本書は「シェーダー本体の仕様と追加手順」に特化。

---

## 1. 3行サマリ

- マテリアルシェーダーは **HLSL を書いて、`Graphics::Initialize` で `RegisterMaterialShader("<名前>")` を1行足す**と使える。
- 見た目は **HLSL の中身**が決める。ブレンド・深度・カリングは**描くパス**が決め、不透明か半透明かは**マテリアルの `alphaMode`** で決まる（§3）。
- マテリアルのパラメータは **cbuffer の変数名 = `MaterialData` のキー** で自動連携（リフレクション）。C++ 側に構造体定義は要らない。

---

## 2. ファイル配置と命名規約（最重要）

シェーダー名を `N` とすると、パスは**すべて機械的に決まる**（`ShaderRegistry::RegisterMaterialShader` が名前から作る）。

| 種類 | パス |
|------|------|
| HLSL ソース (VS) | `Shader/Material/<N>/<N>VS.hlsl` |
| HLSL ソース (PS) | `Shader/Material/<N>/<N>PS.hlsl` |
| ビルド出力 (VS) | `Assets/Shader/<N>VS.cso` |
| ビルド出力 (PS) | `Assets/Shader/<N>PS.cso` |

VS プロジェクトのシェーダーファイルは、出力 `.cso` 名も `<N>VS.cso`/`<N>PS.cso` になるよう設定すること。

---

## 3. 登録の仕組みと、不透明・半透明の振り分け

### 登録（`Graphics::Initialize`）

```cpp
shaders.RegisterShader<ErrorShader>("Error"_hash);       // 最初に登録する（並べ替えの番号 0）

shaders.RegisterMaterialShader("PBR");                    // マテリアルのシェーダー。
shaders.RegisterMaterialShader("Phong");                  // エディタのシェーダー選択にこの順で出る
shaders.RegisterMaterialShader("Lambert");
shaders.RegisterMaterialShader("Basic");

shaders.RegisterShader<ShadowMapShader>("ShadowMap"_hash); // 専用の C++ クラスを持つシェーダー
shaders.RegisterShader<ParticleShader>("Particle"_hash);
shaders.RegisterShader<NormalPrepassShader>("NormalPrepass"_hash);
```

マテリアルやシーンは、シェーダーを**名前のハッシュの数値**で保存している。登録する名前を変えたり消したりすると、それを使っていたメッシュは未登録のハッシュを要求し、`ErrorShader`（マゼンタ）で描かれる。

### 不透明・半透明の振り分け

メッシュごとに、マテリアルの `alphaMode`（glTF の `alphaMode` をインポート時に入れたもの）で決まる。シェーダーは関係しない。

| alphaMode | 描くパス | PSO |
|-----------|---------|-----|
| `Opaque` | NormalPrepass → OpaquePass | 深度を書く・裏面カリング |
| `Mask` | OpaquePass だけ（NormalPrepass は飛ばす）。PBR の PS が `alpha < alphaCutoff` を `discard` | 同上 |
| `Blend` | TransparentPass（カメラから遠い順に1つずつ） | ブレンドあり・深度はテストだけ・両面 |

判定は `OpaquePass::DrawBatch` と `NormalPrepass::Execute` のメッシュのループの中にある。PS は `alpha` を出力するだけでよい（PBR は `AlbedoMap.a × baseColor.a`）。

- `alphaCutoff` は `Shader::UpdateMaterialProperties` が自動で送る（Mask のときだけマテリアルの値、それ以外は 0 = 何も捨てない）。
- Mask の制限: 影（ShadowPass）は切り抜かずに四角い形で落ちる。SSAO 用の法線も書かれない。
- エディタでは、マテリアルスロットの「アルファ」欄で不透明 / くり抜き / 半透明としきい値を変えられる。

---

## 4. スロットマップ（ShaderResources.h より）

更新頻度の層で整理されている。マテリアルシェーダーが使ってよいのは基本 **Per-material** の枠。

### Constant Buffer (b)

| slot | 名前 | 層 | 誰が書く |
|------|------|----|---------|
| b0 | `SLOT_CB_MATERIAL` | Per-material | `Shader::Update` |
| b1 | `SLOT_CB_MESH` | Per-material | 〃 |
| b6/b7 | Particle Static/Dynamic | Per-emitter | Particle系 |
| b8 | `SLOT_CB_SCENE` | Per-frame | **`Graphics::Update` のみ** |
| b9 | `SLOT_CB_LIGHT` | Per-frame | LightSystem |
| b10 | `SLOT_CB_SHADOW` | Per-frame | Shadow |
| b11 | `SLOT_CB_FOG` | Per-frame | Fog |
| b12 | `SLOT_CB_SKELETON` | Per-object | ModelRenderer |

> b0/b1 は Per-pass のポスプロ（Bloom/ToneMap）と物理スロットを共有。時間的に排他なので衝突しない。
> b0 は UI の 2D パスのゲージ（`SLOT_CB_UIGAUGE`、`UIGaugePS`）も使う。`UIRenderer` がドローごとにバインドする。

### Shader Resource / Texture (t)

| slot | 名前 | 用途 |
|------|------|------|
| t0 | `SLOT_SRV_MAT_TEX0` | Albedo / Diffuse |
| t1 | `SLOT_SRV_MAT_TEX1` | Normal |
| t2 | `SLOT_SRV_MAT_TEX2` | MetalRough / Occlusion |
| t3 | `SLOT_SRV_MAT_TEX3` | Emissive 等の追加マップ（※パーティクルbufと共有） |
| t10 | Shadow (Array) | Per-frame |
| t11/t12 | Instance / Bone | Per-object |
| t14-17 | IBL / Skybox | Per-frame |

### Sampler (s)

| slot | 名前 |
|------|------|
| s0 | `SLOT_SMP_DEFAULT` |
| s1 | `SLOT_SMP_LINEAR`（材質標準） |
| s2 | `SLOT_SMP_ANISOTROPIC` |
| s3 | `SLOT_SMP_IBL` |
| s10 | `SLOT_SMP_SHADOW`（比較サンプラ） |

HLSL では `register(REG_CB(SLOT_...))` のようにマクロ経由で書く（数値直書き禁止）。

---

## 5. 共有インクルードと VS_OUT

`#include "Shader/Common/Common.hlsli"` を入れると、`Scene.hlsli`（cbuffer 群）/ `Lighting.hlsli` / `ShadowMap.hlsli` が芋づるで入る。

### VS_OUT（頂点→ピクセルの共通構造体）

```hlsl
struct VS_OUT {
    float4 vertex     : SV_POSITION; // クリップ空間座標
    float2 texcoord   : TEXCOORD;
    float3 normal     : NORMAL;      // ワールド法線
    float4 position   : POSITION;    // ワールド座標
    float4 tangent    : TANGENT;
    float4 shadowPos  : SHADOW_POS;
    float4 customParams : CUSTOM_PARAMS; // 個体ごとの汎用値(Phong は z=被弾フラッシュ強度)
};
```

### 主な共有関数

| 関数 | 置き場所 | 用途 |
|------|---------|------|
| `CalculateWorldTransform(...)` | `Transform.hlsli` | スキン/インスタンシング両対応のワールド変換（3オーバーロード：位置のみ/+法線/+タンジェント） |
| `GetInstanceCustomParams(id)` | `Instancing.hlsli` | インスタンスの customParams 取得 |
| `ApplyHeightFog(...)` | `Common.hlsli` | 高さ/距離フォグ |
| `CalculateIBL / CalculateAllLightsPBR` | `PBRLighting.hlsli` | PBR ライティング |
| `CalculateShadowWeight(...)` | `ShadowMap.hlsli` | カスケードシャドウ |
| `ApplyHitFlash / ApplyRimLightParams / ScrollUV / Pulse / SampleGlobalNoise` | `MaterialEffect.hlsli` | 被弾フラッシュ・リム・時間・ノイズの小物 |

`cameraPosition` / `viewProjection` / `gFog*` などは `CbScene`/`CbFog`（Per-frame）に入っており、`Graphics::Update` が毎フレーム埋めるので**マテリアル側で追加バインド不要**。

---

## 6. マテリアル cbuffer のルール

- 変数名がそのまま `MaterialData` のキー: `SetScalar("intensity"_hash, 3.0f)` ⇔ HLSL の `float intensity;`。
- 名前は全シェーダーでそろえる: 色は `baseColor`、テクスチャは `AlbedoMap` / `NormalMap` / `MetalRoughMap` / `EmissiveMap`。
- **`use` + テクスチャ名の変数（`useAlbedoMap` など）は自動で決まる**: `Shader::UpdateMaterialProperties` がテクスチャの有無を見て 1 / 0 を書く。マテリアルに持たせる必要はなく、エディタにも出ない。
- **色は `float4` で宣言する**（`float3` にしない）。C++ は色を `float4`（16 バイト）で送り、変数の方が小さいと書き込まれない。
- 全体サイズは **16 バイトの倍数**にする（`float2 _pad;` 等で調整）。個々の変数はリフレクションがオフセットで拾うので、順序を変えても名前が合えば動く。
- `float4` は色、`float` はスカラーとして、エディタの「詳細パラメータ」が自動でカラーピッカー/スライダーを出す。対象はマテリアルの定数バッファ（b0〜b7）だけ。マテリアルに値が無い変数は 0（GPU に送られる値）で表示される。

---

## 7. シェーダーカタログ

**lit** = ライティング計算あり / **unlit** = 発光・単純出力（陰影なし）

### PBR （lit）
標準的な物理ベース表面。モデル読み込み時の既定。
- **CB(b0)**: `baseColor`(f4), `emissiveColor`(f4, rgb だけ使う), `metalness`(f), `roughness`(f), `occlusionStrength`(f), `useAlbedoMap`/`useNormalMap`/`useMetalRoughMap`/`useEmissiveMap`(f, 自動), `useOcclusionMap`(f)
- **Tex**: AlbedoMap t0 / NormalMap t1 / MetalRoughMap t2 / EmissiveMap t3
- **出力アルファ**: `AlbedoMap.a × baseColor.a`（`alphaMode = Blend` のときに効く）
- **用途**: キャラ・小物・地形など普通の質感。

### Basic （unlit）
テクスチャ×色をそのまま出す最小シェーダー。ライティング計算を一切行わない。
- **CB**: `baseColor`(f4) ※このシェーダーのみ `SLOT_CB_MESH` に置いている（他は `SLOT_CB_MATERIAL`）
- **Tex**: AlbedoMap t0
- **用途**: UI 的な板、デバッグ表示、ライティング不要のオブジェクト。最も軽い。

### Lambert （lit）
名前に反して**拡散反射のみではない**。`CalculateAllLights` で平行光源・点光源・スポットライトを
一括計算するため、鏡面反射も含まれる。法線マップは扱わない。
- **CB**: `baseColor`(f4)
- **Tex**: AlbedoMap t0
- **処理**: α<0.01 で `discard` → 影の重み → 全ライト計算（diffuse+specular）→ 半球環境光 → 高さフォグ
- **用途**: 法線マップ不要な一般オブジェクト。Phong から法線マップと各種エフェクトを外した位置づけ。

### Phong （lit・法線マップ対応）
マテリアル系で**最も機能が多い**シェーダー。名前は Phong だが、実態は法線マップ＋被弾フラッシュ込み。
- **CB**: `baseColor`(f4)
- **Tex**: AlbedoMap t0 / NormalMap t1
- **頂点経由**: `customParams.z`=被弾フラッシュ強度
- **処理**: 法線マップ適用 → 影 → 全ライト計算 → 半球環境光 → 高さフォグ → ヒットフラッシュ
- **用途**: キャラクター・敵など、被弾演出が要るオブジェクトの標準シェーダー。

### Sprite （2D・登録外）
2D スプライト用。`Source/Engine/Graphics/Renderer/Sprite.cpp` が直接扱い、
`ShaderRegistry` には登録しない（マテリアル差し替え経路の対象外）。
`Sprite.hlsli` を共有し、頂点レイアウトも 3D マテリアルとは別系統。

### 専用の C++ クラスを持つシェーダー（RegisterShader<T>）
| 名前 | クラス | 役割 |
|------|--------|------|
| ShadowMap | `ShadowMapShader` | シャドウ深度書き込み |
| Particle | `ParticleShader` | GPU パーティクル（GS 使用） |
| NormalPrepass | `NormalPrepassShader` | SSAO 用の法線書き込み |

---

## 8. 新しいマテリアルシェーダーを追加する手順

1. `Shader/Material/<N>/<N>VS.hlsl` と `<N>PS.hlsl` を書く（§9 のテンプレ推奨）。
2. VS プロジェクトに追加し、出力 `.cso` 名を `<N>VS.cso`/`<N>PS.cso` に設定する。
3. `Graphics::Initialize` に `shaders.RegisterMaterialShader("<N>");` を足してビルド。
4. 起動 → エディタの Material コンポーネントの「シェーダー」欄に出る。シェーダーはモデル単位で1つ（先頭のスロットのものでモデル全体を描く）。値はスロットごとの「詳細パラメータ」で設定。

半透明にしたいときは、シェーダーではなくマテリアルの `alphaMode` を `Blend` にする（§3）。

---

## 9. 自己記述ヘッダーテンプレート（推奨）

各 PS の冒頭にこの形式でメタ情報を書いておくと、この仕様書を更新しなくてもファイル単体で仕様が読める。

```hlsl
// <Name>PS.hlsl
// Category : lit / unlit
// Params   : baseColor(f4) 色 / intensity(f) 強さ
// Textures : AlbedoMap(t0)
// Use      : 一言用途
```

---

## 10. 既知の落とし穴

- **Phong / Lambert / Basic は `AlbedoMap` を必ず読む**: テクスチャが無いと null を読んで黒くなる。エディタの「白を設定」で白いテクスチャを入れる。
- **`alphaMode = Blend` で出力アルファが 0 だと見えない**: 半透明は「出力アルファの割合で手前の色を混ぜる」ので、`baseColor.a` が 0 なら何も描かれない。UE から書き出したモデルは、不透明度がテクスチャ側にあって係数が 0 になっていることがある。
- **命名と cso がずれていると止まる**: 登録した名前の `<N>VS.cso`/`<N>PS.cso` が無いと、初めて使うときに読み込みの assert で止まる。

---

## 11. 改善提案（今後の TODO 候補）

1. **Mask の影**: ShadowMap にピクセルシェーダーとマテリアルのテクスチャを足して `discard` すれば、影も切り抜いた形になる。
2. VS リフレクション対応 → 頂点アニメ（風揺れ等）を HLSL だけで。デバッグ時ランタイムコンパイル → `.cso` ビルド工程の廃止。
