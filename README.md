# OVER CLEAVE — 自作 C++ ECS ゲームエンジン & アクションゲーム

> DirectX 11 上に自作の **データ指向 ECS**（CCL ECS）を核として構築した、
> ハイスピードアクションゲームとその実行基盤。描画・アニメーション・物理・AI・UI・
> エディタまでを一貫した ECS アーキテクチャで実装したプロジェクトです。

| 項目 | 内容 |
|------|------|
| 言語 / プラットフォーム | C++20 / DirectX 11 |
| 中核 | 自作 **Archetype ECS**（アーキタイプ型 + SoA チャンク + 遅延構造変更） |
| 開発体制 | プログラム：個人 ／ エフェクト：アート担当 1 名 |

> GitHub の Contributors に表示されるもう 1 人（YUYUYU-6626）はアート担当で、エフェクトの実装をしてくれています。

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
- ポストプロセス（Bloom / トーンマップ / 色収差  等）
- GPU パーティクル（Compute Shader）・トレイル

**アニメーション**
- スケルタルアニメ + ステートマシン + アニメグラフ
- ルートモーション、ブレンド、速度カーブ（タイムワープ）
- シーケンサーでイベント（SE / VFX / ヒットボックス）を配置

</td><td valign="top" width="50%">


**ゲームプレイ**
- ジャスト回避 → ラッシュ（避けた攻撃の主へ詰めて連打し、とどめでスロー）
- ロックオン / ダッシュ / 派生コンボ、ボス戦（ビヘイビアツリーで行動選択）、陣形を組むドローン敵
- チュートリアル専用シーン

**エディタ（ImGui）**
- Hierarchy / Inspector / アセットブラウザ
- アニメシーケンサー / ビヘイビアツリー
- レンダーデバッグ / ECS デバッグ

</td></tr>
</table>

---

## 技術的なこだわり・工夫した点

- **データ指向設計（DOD）**: コンポーネントを SoA チャンクに連続配置し、キャッシュミスを抑制。構造変更は遅延キューに積んでフレーム境界でまとめて反映し、走査中の破壊を防止。
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
| モデル読み込み | tinygltf |
| シリアライズ | cereal（バイナリ）/ nlohmann::json |
| エディタ UI | Dear ImGui / ImGuizmo / imgui-node-editor / ImPlot |
| テキスト | stb_truetype |
| プロファイラ | Tracy |

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

- ソースの文字コードは UTF-8（コンパイルは `/utf-8` 指定）。
- `AnimLab` / `DemoSwarm` / `PlayerTest` / `Particle` は開発・調整用のシーン。ゲーム本編は `Title` → `Tutorial` / `Game`。
