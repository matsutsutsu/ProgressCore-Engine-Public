# Shader 仕様書 / 使い方ガイド

このドキュメントは、**個々の HLSL を全部読まなくても、シェーダーの追加・利用・理解ができる**ことを目的とした単一の参照資料です。

> 関連ドキュメント: レンダリング経路の全体像は `Render_Architecture_Map.md`、スロット共有の詳細契約は `ShaderBindingContract.md`（存在すれば）を参照。本書は「シェーダー本体の仕様と追加手順」に特化。

**凡例**: ✅=現行の実装で使用中 / ⚠️=マニフェスト未登録（現在は未使用）

---

## 1. 3行サマリ

- マテリアルシェーダーの追加は **HLSL を書いて `shaders.json` に1行足すだけ**（C++・再ビルド不要）。
- 見た目は **HLSL の中身**、ブレンド/カリング/深度は **`shaders.json` の PSO 指定** が決める。`MaterialShader` という C++ クラスは中身を知らない汎用配送屋。
- マテリアルのパラメータは **cbuffer の変数名 = `MaterialData` のキー** で自動連携（リフレクション）。C++ 側に構造体定義は要らない。

---

## 2. ファイル配置と命名規約（最重要）

シェーダー名を `N` とすると、パスは**すべて機械的に決まる**。

| 種類 | パス |
|------|------|
| HLSL ソース (VS) | `Shader/Material/<N>/<N>VS.hlsl` |
| HLSL ソース (PS) | `Shader/Material/<N>/<N>PS.hlsl` |
| ビルド出力 (VS) | `Assets/Shader/<N>VS.cso` |
| ビルド出力 (PS) | `Assets/Shader/<N>PS.cso` |

`ShaderManifest` がこの規約からパスを導出して登録するため、**この命名を外すと即 `ErrorShader`（マゼンタ）になる**。VS プロジェクトのシェーダーファイルは、出力 `.cso` 名も `<N>VS.cso`/`<N>PS.cso` になるよう設定すること。

---

## 3. 登録の仕組み（shaders.json）

`Assets/Shader/shaders.json` が唯一の登録リスト。`ShaderManifest::LoadAndRegister`（`FileSystem` 経由で読む）が起動時に一括登録する。`Graphics.cpp` は触らない。

```json
{
  "materialShaders": [
    { "name": "PBR" },
    { "name": "Glow",     "blend": "additive" },
    { "name": "Grass",    "cull":  "none" },
    { "name": "GhostPBR", "blend": "transparency", "depth": "testOnly" }
  ],
  "customShaders": [
    { "name": "Outline", "class": "OutlineShader" }
  ]
}
```

### PSO フィールド（任意・省略時はパス既定）

| フィールド | 値 | 意味 |
|-----------|----|----|
| `blend` | `opaque` / `transparency`(`alpha`) / `additive`(`add`) / `subtract` / `multiply` | 合成方法 |
| `cull`  | `back` / `none` | カリング（`none`=両面描画） |
| `depth` | `testWrite`(`default`) / `testOnly`(`readOnly`) / `writeOnly` / `none` / `testLessEqual` | 深度テスト/書き込み |

- **ベース自動選択**: `blend` が透明系（transparency/additive/subtract/multiply）なら土台が自動で「深度書込なし・両面」になる。`cull`/`depth` を明示すればその軸だけ上書き。
- `blend`/`cull`/`depth` を**1つも書かない**とパス既定 PSO をそのまま使う（＝既存挙動、リスクゼロ）。

### customShaders（C++ クラスが必要な特殊シェーダー）

GS/CS や独自バインドが要るものは `RegisterShader<T>` が必要。`ShaderManifest.cpp` の `CustomRegistrars` にクラス名→型の対応が1行ある。**新しいカスタム C++ シェーダーを足すときだけ**そこに追記する（文字列から型は生成できないため不可避）。

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

### Shader Resource / Texture (t)

| slot | 名前 | 用途 |
|------|------|------|
| t0 | `SLOT_SRV_MAT_TEX0` | Albedo / Diffuse / Emissive(Glow) |
| t1 | `SLOT_SRV_MAT_TEX1` | Normal |
| t2 | `SLOT_SRV_MAT_TEX2` | MetalRough / Occlusion |
| t3 | `SLOT_SRV_MAT_TEX3` | Emissive 等の追加マップ（※パーティクルbufと共有） |
| t9 | Fog Noise | Per-frame |
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
    float4 customParams : CUSTOM_PARAMS; // 個体ごとの汎用値(x=ディゾルブ進行度など)
};
```

### 主な共有関数

| 関数 | 置き場所 | 用途 |
|------|---------|------|
| `CalculateWorldTransform(...)` | `Transform.hlsli` | スキン/インスタンシング両対応のワールド変換（3オーバーロード：位置のみ/+法線/+タンジェント） |
| `GetInstanceCustomParams(id)` | `Instancing.hlsli` | インスタンスの customParams 取得 |
| `ApplyHeightFog(...)` | `Common.hlsli` | 高さ/距離/ノイズフォグ |
| `CalculateIBL / CalculateAllLightsPBR` | `PBRLighting.hlsli` | PBR ライティング |
| `CalculateShadowWeight(...)` | `ShadowMap.hlsli` | カスケードシャドウ |

`cameraPosition` / `viewProjection` / `gFog*` などは `CbScene`/`CbFog`（Per-frame）に入っており、`Graphics::Update` が毎フレーム埋めるので**マテリアル側で追加バインド不要**。

---

## 6. マテリアル cbuffer のルール

- 変数名がそのまま `MaterialData` のキー: `SetScalar("intensity"_hash, 3.0f)` ⇔ HLSL の `float intensity;`。
- 全体サイズは **16 バイトの倍数**にする（`float2 _pad;` 等で調整）。個々の変数はリフレクションがオフセットで拾うので、順序を変えても名前が合えば動く。
- `float4` は色、`float` はスカラーとしてエディタ（Advanced Parameters）が自動でスライダー/カラーピッカーを出す。

---

## 7. シェーダーカタログ

**lit** = ライティング計算あり / **unlit** = 発光・単純出力（陰影なし）

### PBR ✅ （lit・不透明）
標準的な物理ベース表面。`Assets/Shader/PBRPS.cso`。
- **CB(b0)**: `baseColor`(f4), `emissiveColor`(f3), `metalness`(f), `roughness`(f), `occlusionStrength`(f), `useAlbedoMap`/`useNormalMap`/`useMetalRoughMap`/`useOcclusionMap`(f)
- **Tex**: AlbedoMap t0 / NormalMap t1 / MetalRoughMap t2 / EmissiveMap t3
- **PSO**: 既定（不透明）
- **用途**: キャラ・小物・地形など普通の質感。

### GhostPBR ✅ （lit・半透明）
PBR の陰影を保ったまま透明度だけ調整。
- **CB(b0)**: PBR と同一 + `ghostAlpha`(f)
- **PSO**: `blend: transparency`, `depth: testOnly`
- **用途**: 半透明キャラ・幽霊。`ghostAlpha` は 0.3〜0.5 目安（1.0 だと不透明と同じ）。

### Glow ✅ （unlit・加算）
面全体が均一に光る自己発光。
- **CB(b0)**: `glowColor`(f4), `intensity`(f), `useEmissiveMap`(f), `_pad`(f2)
- **Tex**: EmissiveMap t0
- **PSO**: `blend: additive`
- **用途**: 魔法弾・エネルギー体・発光紋様など「それ自体が光源」。加算合成は `SrcBlend=SRC_ALPHA` なので出力アルファが強さ倍率になる。

### Hologram ✅ （unlit・加算）
視線と法線の角度で**輪郭だけ**光るフレネル発光。
- **CB(b0)**: `rimColor`(f4), `rimPower`(f), `intensity`(f), `_pad`(f2)
- **Tex**: なし
- **PSO**: `blend: additive`
- **用途**: シールド・ホログラム・幽霊の縁。複雑形状に直貼りすると全体が光るので、**一回り大きい殻メッシュや単純形状に適用**するのが定石。`rimPower` 3〜8 で鋭い輪郭。

### Grass ✅ （unlit・不透明・両面・アルファクリップ）
アルファで形を切り抜く両面描画。
- **CB(b0)**: `baseColor`(f4), `alphaClipThreshold`(f), `_pad`(f3)
- **Tex**: DiffuseMap t0（**アルファ抜きテクスチャが実質必須**）
- **PSO**: `cull: none`
- **用途**: 草・葉の板ポリ（十字配置カード等）。`alphaClipThreshold` は 0.5 目安（1.0 だと全消え）。閉じた立体や不透明テクスチャに使っても Basic と同じ見た目にしかならない（切り抜くものが無い/裏面が見えないため）。

### Basic ✅ （unlit・不透明）
テクスチャ×色をそのまま出す最小シェーダー。ライティング計算を一切行わない。
- **CB**: `materialColor`(f4) ※このシェーダーのみ `SLOT_CB_MESH` に置いている（他は `SLOT_CB_MATERIAL`）
- **Tex**: DiffuseMap t0
- **PSO**: マニフェスト側に指定なし（既定の不透明設定）
- **用途**: UI 的な板、デバッグ表示、ライティング不要のオブジェクト。最も軽い。

### Lambert ✅ （lit・不透明）
名前に反して**拡散反射のみではない**。`CalculateAllLights` で平行光源・点光源・スポットライトを
一括計算するため、鏡面反射も含まれる。法線マップは扱わない。
- **CB**: `materialColor`(f4)
- **Tex**: DiffuseMap t0
- **処理**: α<0.01 で `discard` → 影の重み → 全ライト計算（diffuse+specular）→ 半球環境光 → 高さフォグ
- **PSO**: マニフェスト側に指定なし（既定の不透明設定）
- **用途**: 法線マップ不要な一般オブジェクト。Phong から法線マップと各種エフェクトを外した位置づけ。

### Phong ✅ （lit・不透明・法線マップ対応）
マテリアル系で**最も機能が多い**シェーダー。名前は Phong だが、実態は法線マップ＋各種エフェクト込み。
- **CB**: `materialColor`(f4)
- **Tex**: DiffuseMap t0 / NormalMap t1 / DissolveNoiseMap（グローバルノイズ枠）
- **頂点経由**: `customParams.x`=ディゾルブ進行度, `customParams.z`=被弾フラッシュ強度
- **処理**: ディゾルブクリップ → 法線マップ適用 → 影 → 全ライト計算 → 半球環境光
  → リムライト → 高さフォグ → ヒットフラッシュ → ディゾルブ発光エッジ
- **PSO**: マニフェスト側に指定なし（既定の不透明設定）
- **用途**: キャラクター・敵など、被弾演出と消滅演出が要るオブジェクトの標準シェーダー。

### Toon ✅ （lit・不透明・セルルック）
ランプテクスチャで階調を制御するセルシェーディング。
- **CB**: `materialColor`(f4), `gRimColor`(f4), `gRimPower`(f), `gRimIntensity`(f), `gToonPadding`(f2)
- **Tex**: DiffuseMap t0 / NormalMap t1 / ToonRampMap t2 / DissolveNoiseMap（グローバルノイズ枠）
- **頂点経由**: `customParams.x`=ディゾルブ進行度
- **処理**: ディゾルブクリップ → 法線マップ → `CalcToonDirectionalLight`（ランプ参照）
  → 半球環境光 → Toon 専用リムライト → 影色 → 高さフォグ → ディゾルブ発光エッジ
- **注意**: トゥーン化されるのは**平行光源のみ**。点光源・スポットライトは計算に入らない。
- **用途**: セルルックのキャラクター。共通リムライトは呼ばず、専用リムで置き換えている。

### Dissolve ⚠️ （未登録・現在は未使用）
**`Assets/Shader/shaders.json` に登録されておらず、`Source`/`Assets` からの参照も無い。**
PS のみ存在し VS が無いのは、マニフェスト経由で読み込まれていないため。
ディゾルブ表現は現在 `MaterialEffect.hlsli` の `ApplyDissolveClip` / `ApplyDissolveEdge` として
Phong・Toon に組み込む方式に移行しており、本ファイルはその前段階の実装。
- **旧実装のCB**: `materialColor`(f4), `dissolveThreshold`(f), `edgeWidth`(f), `padding`(f2), `edgeColor`(f4)
  ※現行方式が `customParams.x` を使うのに対し、こちらは**専用CBで進行度を渡す**設計だった
- **扱い**: 使うならマニフェスト登録と VS の用意が必要。使わないなら削除してよい。

### Sprite ✅ （2D・マニフェスト外）
2D スプライト用。`Source/Engine/Graphics/Renderer/Sprite.cpp` が直接扱い、
`shaders.json` には登録されない（マテリアル差し替え経路の対象外）。
`Sprite.hlsli` を共有し、頂点レイアウトも 3D マテリアルとは別系統。

### カスタム（非マテリアル・RegisterShader<T>）
| 名前 | クラス | 役割 |
|------|--------|------|
| Outline | `OutlineShader` | 輪郭線パス |
| ShadowMap | `ShadowMapShader` | シャドウ深度書き込み |
| Particle | `ParticleShader` | GPU パーティクル（GS 使用） |

---

## 8. 新しいシェーダーを追加する手順

1. `Shader/Material/<N>/<N>VS.hlsl` と `<N>PS.hlsl` を書く（§9 のテンプレ推奨）。
2. VS プロジェクトに追加し、出力 `.cso` 名を `<N>VS.cso`/`<N>PS.cso` に設定してビルド。
3. `shaders.json` の `materialShaders` に `{ "name": "<N>" }` を追加（必要なら `blend`/`cull`/`depth`）。
4. 起動 → エディタのシェーダーコンボに自動で出る。メッシュのマテリアルで選択し、Advanced Parameters で値を設定。

C++ とエンジン再ビルドは不要（②③の拡張が入るまで `.cso` ビルドだけは VS 側に残る）。

---

## 9. 自己記述ヘッダーテンプレート（推奨）

各 PS の冒頭にこの形式でメタ情報を書いておくと、この仕様書を更新しなくてもファイル単体で仕様が読める。

```hlsl
// <Name>PS.hlsl
// Category : lit / unlit
// PSO      : blend=... cull=... depth=...   (shaders.json と一致させる)
// Params   : glowColor(f4) 発光色 / intensity(f) 強さ
// Textures : EmissiveMap(t0)
// Use      : 一言用途
```

---

## 10. 既知の落とし穴

- **「Fill All Missing With Defaults」は一律 1.0 を入れる。** `alphaClipThreshold`(→0.5)、`ghostAlpha`(→0.4)、`useXxxMap`（テクスチャ未設定なら 0）など、変数によっては押した直後に手で直さないと真っ黒/全消えになる。
- **`useXxxMap` フラグの罠**: 1.0 なのに対応テクスチャ未バインドだと黒くなる。将来的にはエンジン側でテクスチャ有無から自動設定するのが望ましい（本書 §改善提案）。
- **加算合成の強さはアルファ依存**: `BlendState::Additive` は `SrcBlend=SRC_ALPHA`。PS の出力アルファが 0 だと何も足されない。
- **命名を規約から外すと即マゼンタ**: `<N>VS.cso`/`<N>PS.cso` が見つからないと `ErrorShader`。起動ログに cso 欠落の警告が出るので確認する。

---

## 11. 改善提案（今後の TODO 候補）

1. **`useXxxMap` の自動化**: `MaterialShader::Update` でテクスチャ束縛の有無から `useXxxMap` を自動設定し、GUI から除外する（「真っ黒事故」の恒久対策）。
2. **`shaders.json` に `defaults` ブロック**: `"defaults": { "ghostAlpha": 0.4 }` を材質生成時に流し込み、新規マテリアルを最初から使える状態にする。
3. **VS の共通化**: 同一な材質 VS を `Transform.hlsli` のマクロに集約し、各 `<N>VS.hlsl` を数行化してドリフトを防ぐ。
4. **Dissolve の登録経路の明文化**（VS 欠如問題）。
5. **PBR emissive の `useEmissiveMap` ガード追加**（他マップと一貫させる）。
6. （既出）② VS リフレクション対応 → 頂点アニメ（風揺れ等）を HLSL だけで。③ デバッグ時ランタイムコンパイル → `.cso` ビルド工程の廃止。
