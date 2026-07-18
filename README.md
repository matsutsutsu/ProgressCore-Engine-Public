# RuneForge — 自作 C++ ECS ゲームエンジン & アクションゲーム

> DirectX 11 上に自作の **データ指向 ECS**（CCL ECS）を核として構築した、
> スタイリッシュアクションゲームとその実行基盤。描画・アニメーション・物理・AI・UI・
> エディタまでを一貫した ECS アーキテクチャで実装した個人開発プロジェクトです。

<!--
  ▼ ここにヒーロー画像 / プレイ動画GIF を貼ると印象が大きく上がります。
  例:
  ![gameplay](docs/images/gameplay.gif)
  ![editor](docs/images/editor.png)
-->

| 項目 | 内容 |
|------|------|
| 言語 / プラットフォーム | C++17 / 20・Win32・DirectX 11 |
| 中核 | 自作 **CCL ECS**（アーキタイプ型 + SoA チャンク + 遅延構造変更） |
| 規模 | ソース 600 ファイル超・エンジン / ゲーム / エディタを分離 |
| 開発体制 | 個人開発 |

---

## 主な機能

<table>
<tr><td valign="top" width="50%">

**コア / ECS**
- アーキタイプ + SoA チャンクによるキャッシュ効率重視の ECS
- 遅延構造変更（フレーム境界で安全に反映）
- 優先度付きシステムスケジューラ / ジョブシステム（並列 ForEach）
- イベントバス・World リソース・時間スケール制御

**描画（D3D11）**
- パス構成のレンダーパイプライン + データ駆動シェーダー
- PBR / トゥーン、シャドウマップ、法線プリパス
- ポストプロセス（Bloom / SSAO / DOF / トーンマップ / カラーグレーディング / 色収差 / ビネット / 放射ブラー 等）
- GPU パーティクル（Compute Shader）・トレイル

**アニメーション**
- スケルタルアニメ + ステートマシン + アニメグラフ
- ルートモーション、ブレンド、速度カーブ（タイムワープ）
- シーケンサーでイベント（SE / VFX / ヒットボックス）を配置

</td><td valign="top" width="50%">

**物理 / AI / オーディオ**
- Jolt Physics（キャラクターコントローラ / 剛体 / 当たり判定レイヤー）
- ビヘイビアツリー・ステートツリー・ドローン編隊 AI・フロー フィールド
- Recast/Detour によるナビゲーション
- FMOD による発音・リスナー・距離減衰

**ゲームプレイ**
- ジャスト回避 → チェイン、OVERDRIVE（ゲージ発動で敵スロー・自機高速）
- ロックオン / ダッシュ / コンボ、ボス戦、多数のドローン敵

**UI（ECS ネイティブ）**
- エンティティ + コンポーネントで組む UI（Inspector で編集 / Prefab 化）
- stb_truetype によるテキスト描画を自作スプライトへ統合
- マウス + ゲームパッド/キーの**フォーカス移動**対応
- データ連動 HUD（ImGui 非依存 = 製品ビルドでも動作）

**エディタ（ImGui）**
- Hierarchy / Inspector / アセットブラウザ
- アニメシーケンサー / ビヘイビアツリー / ステートツリー / カーブ エディタ
- メモリアナライザ / レンダーデバッグ / ECS デバッグ

</td></tr>
</table>

---

## 技術的なこだわり・工夫した点

- **データ指向設計（DOD）**: コンポーネントを SoA チャンクに連続配置し、キャッシュミスを抑制。構造変更は遅延キューに積んでフレーム境界でまとめて反映し、走査中の破壊を防止。
- **ECS ネイティブ UI**: 「UI もエンティティ」に統一し、既存の Hierarchy / Inspector / Prefab / シリアライズ基盤をそのまま流用。描画は 2D パスで描画リストを消費する RenderQueue 方式。ImGui 依存の HUD を撤廃し製品ビルドでも動く構成へ。
- **アニメーションのホットパス最適化**: 骨ごと・毎フレーム走っていた静的処理（ボーン種別の文字列判定・アニメ名検索・0秒基準ポーズ）を「不変値は一度だけ計算してキャッシュ」方針で削減し、`ComputeAnimation` の負荷とスパイクを低減。
- **モデルの二段ロード**: 初回に glTF を解析して独自バイナリ（`.cereal`）へ焼き、次回以降は高速ロード。
- **プロファイリング前提**: Tracy を全域に計測ゾーンとして組み込み、起動負荷や毎フレーム負荷を可視化しながら最適化。
- **データ駆動シェーダー / コンポーネントメタ**: シェーダーやコンポーネントの定義をデータ側に寄せ、追加時のボイラープレートを最小化。

---

## アーキテクチャ

```
FixedUpdate (60Hz) → SceneManager::FixedUpdate → ECSContext::UpdateLogic   … ゲームロジック・物理
Update             → SceneManager::Update       → ECSContext::UpdateRender … 描画準備・アニメ更新
Render             → SceneManager::Render       → Graphics::Render          … レンダーパイプライン実行
```

- `Source/Engine/` — 再利用可能なエンジン基盤（ECS / 描画 / 物理 / アニメ / UI / 音声 …）
- `Source/Game/` — ゲーム固有のロジック（プレイヤー / ボス / ドローン AI / コンバット …）
- `Source/Editor/` — ImGui ベースのエディタ UI（`ENABLE_EDITOR` ビルド時）

---

## 使用技術・外部ライブラリ

| 分野 | 使用 |
|------|------|
| 描画補助 | DirectXTK / DirectXTex |
| 物理 | Jolt Physics |
| ナビゲーション | Recast & Detour |
| オーディオ | FMOD |
| モデル読み込み | tinygltf |
| シリアライズ | cereal（バイナリ）/ nlohmann::json |
| スクリプト | Lua / sol2 |
| エディタ UI | Dear ImGui / ImGuizmo / imgui-node-editor / ImPlot |
| テキスト | stb_truetype |
| プロファイラ | Tracy |

---

## ビルド方法

- **必要環境**: Windows 10/11・Visual Studio 2026（MSVC v143 ツールセット / MSVC 14.38）・Windows SDK・x64
- **手順**:
  1. `ProgressEngine.sln` を Visual Studio で開く
  2. 構成 `Debug`（または `Release` / `Development`）・プラットフォーム `x64` を選択
  3. ビルド → 実行（または `Run.bat`）

> ※ FMOD 等の外部 SDK はライセンス上リポジトリに含めていない場合があります。

---

## ディレクトリ構成

```
Source/
├─ Engine/     … エンジン基盤（ECS / Graphics / Physics / Animation / AI / UI / Audio / Scene …）
├─ Game/       … ゲーム固有ロジック（Character / Combat / AI / Scenes / UI …）
└─ Editor/     … ImGui エディタ（Windows / Inspector / Utils …）
Assets/        … モデル・テクスチャ・フォント・シーン・設定
Shader/        … HLSL シェーダー
External/      … 外部ライブラリ
```

---

<!--
  ▼ 任意で追記すると良い項目:
  - 開発期間・担当範囲（個人開発なら「全て」）
  - 特に見てほしいコードへのリンク（例: ECS コア / レンダーパイプライン / ECS UI）
  - 操作方法（キーコンフィグ）
-->
