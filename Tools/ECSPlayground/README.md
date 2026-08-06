# ECSPlayground

CCL ECS **だけ**を切り出して動かすコンソールアプリ。ECS の内部処理をデバッガで
ステップ実行して理解するための実験場です。

実ゲームは起動直後にドローン100体の生成・物理・アニメーション・AI が同時に走るため、
`ApplyPendingOperations` にブレークポイントを置いても無関係な処理で埋まってしまい、
ECS の動きを追うことが事実上できません。ここではエンティティが数体しか存在しない
世界を作るので、**デバッガが止まる行は 100% ECS のコード**になります。

---

## 関数の意味が分からなくなったら

**[ECS_FunctionReference.md](ECS_FunctionReference.md)** に ECS の全関数の説明があります。

デバッガで潜っていて「この関数は何をするんだ？」となったら、
関数名を `Ctrl+F` で検索してください。手を止めずに済みます。

エンジン本体のソースは変更していないので、説明は全部そちらに集約しています。

---

## 使い方（これが基本）

**`ECSPlayground.sln` をダブルクリックして Visual Studio で開き、F5 を押すだけです。**

普通の C++ プロジェクトなので、いつも通りソースの行番号の左をクリックして
ブレークポイントを置き、F5 で実行、F10 / F11 でステップ実行できます。
`OVERCLEAVE`（ゲーム本体）のソリューションとは別物なので、開いても
ゲーム側には何の影響もありません。

起動するとメニューが出ます。

```
 1 : Chunk のメモリレイアウト（SoA）
 2 : 構造変更の遅延（Spawn の実体化タイミング）
 3 : 破棄・穴詰め・世代による ID 再利用
 4 : アーキタイプ遷移（Chunk の引っ越し）
 5 : System と ForEach
 a : 全部まとめて実行
 q : 終了
番号を入力して Enter >
```

**番号を選ぶまで止まっているので、勝手に閉じません。**
`q` を押すまでメニューに戻り続けるので、同じ実験を何度でも試せます。

### 最初の一歩（おすすめの手順）

1. `ECSPlayground.sln` を Visual Studio で開く
2. `ECSPlayground.cpp` を開き、`Experiment2_DeferredApply()` の中の
   **`world.ScrutinyAndApply();`** の行にブレークポイントを置く（`★BP` と書いてあります）
3. F5 で実行 → コンソールに `2` と入力して Enter
4. ブレークポイントで止まるので、**F11** で関数の中へ入っていく
5. そのまま ECS 本体（`CCL_World.cpp` → `CCL_ChunkManager.cpp`）へ潜っていけます

`ECSPlayground.cpp` 内の **`★BP`** と書かれた行が、各実験で最初に止めるべき場所です。

### 日本語が文字化けする場合

`main()` の先頭で `EnableUtf8Console()` を呼び、コンソールの出力コードページを
UTF-8 に切り替えています。このプロジェクトは `/utf-8` でビルドしているため
日本語文字列は UTF-8 で exe に埋め込まれますが、日本語版 Windows のコンソール
既定は CP932(Shift-JIS) なので、これをしないと全て化けます。

それでも化ける場合は、VS の
**［ツール］→［オプション］→［デバッグ］→［全般］** にある
**「出力ウィンドウのテキストをすべてイミディエイト ウィンドウにリダイレクトする」**
がオンになっていないか確認してください。オンだと出力が VS 側のウィンドウへ
流れてしまい、`SetConsoleOutputCP` の効果が及びません。オフにすると
専用のコンソールウィンドウが開き、正しく表示されます。

### リビルド時に LNK1168 が出る場合

`ECSPlayground.exe を開いて書き込むことができません` と言われたら、
**前回の実行がまだ終わっていません**。コンソールウィンドウで `q` を押して
終了させるか、VS の［デバッグ］→［デバッグの停止］(Shift+F5) を実行してから
ビルドし直してください。

### コマンドラインからビルドしたい場合

Visual Studio を開かずにビルドするなら `Build.bat` を使ってください
（出力先は `Bin\ECSPlayground.exe`）。**VS で使う場合は不要です。**

なお `.bat` から直接起動すると、`q` を押した時点でウィンドウが閉じます。
デバッグ目的なら素直に VS から F5 で起動するのが楽です。

---

## 収録している実験

| # | テーマ | 対応する学習ステップ |
|---|--------|---------------------|
| 1 | Chunk のメモリレイアウト（SoA）を実アドレスで見る | ステップ2（最大の山場） |
| 2 | 構造変更の遅延 — Spawn してもすぐには実体化しない | ステップ3 前半 |
| 3 | 破棄・穴詰め・世代カウンタによる ID 再利用 | ステップ3 後半 |
| 4 | アーキタイプ遷移 — コンポーネント追加で Chunk を引っ越す | ステップ2〜3 |
| 5 | System と ForEach、フレームの3ステップ | ステップ4 |
| 6 | **【ベンチ】** AoS と SoA の速度を実測する | 面接用の数字 |
| 7 | **【ベンチ】** ForEach と ForEachParallel の比較 | 面接用の数字 |

### ベンチマーク（6・7）は必ず Release で実行してください

**Debug ビルドで測った数字には意味がありません。** 最適化が無効なので、
メモリ配置の差より遥かに大きいオーバーヘッドに埋もれてしまいます。

VS の構成を **Release** に切り替えてから実行してください。

実測例（16論理コアのマシン）:

```
実験6  AoS vs SoA
  ケース1 太い構成・少数（キャッシュに収まる）   → SoA が 1.16 倍
  ケース2 太い構成・大量（キャッシュを超える）   → SoA が 5.80 倍
  ケース3 細い構成・大量（総量はケース2と同等） → SoA が 1.17 倍

実験7  ForEach vs ForEachParallel（20万体）
  軽い処理（p += v*dt）      → 4.22 倍（効率 26%）
  重い処理（sqrt を24回）    → 9.34 倍（効率 58%）
```

**この3ケースの対比が結論です。** ケース2と3は AoS 側が読む総量を約45MBに
揃えてあるので、倍率の差（5.8倍 vs 1.17倍）は純粋に「1体あたりの無駄の割合」
だけを反映しています。**ECSが効くのは「使わないデータが多いとき」**であって、
常に速いわけではない、というのが正しい理解です。

実験7も同様に、**並列化が効くのは計算が重いSystemだけ**だと分かります。

### 実験1 で必ず引っかかるポイント

Position 配列と Velocity 配列の間隔は **8192 バイト**になりますが、これは
その時点で入っている 8 体分（96バイト）ではありません。
**Chunk は生成時に「容量いっぱい分」を一括確保する**ため、
`容量682体 × sizeof(Position)12 = 8184` を 16 バイト境界に切り上げた値です。

「配列の幅は今いる数ではなく容量で決まる」——ここが SoA を絵に描くときの
一番の勘所です。

---

## ビルド構成のポイント

ECS コアは驚くほど依存が少なく、標準ライブラリ以外に必要なのは実質これだけです。

- `DestroyTag.h`（8行・include ゼロ）
- `SystemPriority.h`（60行・include ゼロ）
- Tracy（`TRACY_ENABLE` 未定義なら全マクロが空になる）

そのため **DirectX も Jolt も ImGui も pch.h も不要**です。
`Build.bat` は次の 4 点だけ守っています。

1. **`ENABLE_EDITOR` を定義しない**
   `CCL_SystemManager.cpp` の ImGui 依存はすべて `#ifdef ENABLE_EDITOR` の内側なので、
   定義しなければ ImGui が完全に消えます。

2. **`TRACY_ENABLE` を定義しない**
   Tracy のマクロがすべて空になります。ただし `tracy::SetThreadName()` だけは
   ガードの外にある実関数なので、`External/Tracy/TracyClient.cpp` を 1 本だけ
   ビルド対象に含めています（TRACY_ENABLE が無いので中身はほぼ空です）。

3. **`Stub\` を `Source\` より先にインクルードパスへ**
   `CCL_World.cpp:3` に `#include "Engine/Graphics/Renderer/ShapeRenderer.h"` が
   残っていますが、**本文では一度も使われていない死んだ include** です。
   本物を読むと DirectX 一式が来てしまうため、`Stub/` に空のヘッダを置いて
   そちらを先に拾わせています。**エンジン本体のソースは無改造です。**

4. **`-vcvars_ver=14.38` でツールセットを固定**
   このマシンの既定ツールセット v145（14.51）は CRT のインポートライブラリが
   インストールされておらず（`lib\x64` に 31 ファイルのみ）、
   `msvcprtd.lib が開けません` でリンクに失敗します。
   実プロジェクトが使う v143（14.38、108 ファイル）を明示的に指定しています。

同じ設定は `ECSPlayground.vcxproj` にも書いてあるので、
VS から F5 で使う場合も `Build.bat` と同じ結果になります。

### ビルド対象ファイル（これで全部）

```
ECSPlayground.cpp
Source/Engine/ECS/Core/CCL_Chunk.cpp
Source/Engine/ECS/Core/CCL_ChunkManager.cpp
Source/Engine/ECS/Core/CCL_World.cpp
Source/Engine/ECS/Jobs/CCL_JobSystem.cpp
Source/Engine/ECS/System/CCL_SystemManager.cpp
External/Tracy/TracyClient.cpp
```

ECS 本体は 5 ファイル・約 1,700 行しかありません。

---

## 注意点

- **`world.Destroy()` はこの Playground では効きません。**
  `Destroy()` は `DestroyTag` を付けるだけで、実際の破棄は
  `HierarchyCleanupSystem`（`Engine/Transform` 側にあり ECS コアには含まれない）が
  行うためです。ここではフレームワーク内部用の `RequestDestroyEntity()` を
  直接呼んでいます。

- **System の登録に `REGISTER_LOGIC_SYSTEM` マクロを使っていません。**
  通常はマクロによる自動登録が規約ですが、登録処理の中身を見せるために
  `RegisterSystem<T>()` を意図的に直接呼んでいます。

- 出力に `printf` を使っています。エンジン本体では `CCL_LOG_*` を使う規約ですが、
  ここは学習用ツールであり、ログ基盤への依存を増やしたくないためです。

- `ProgressEngine.vcxproj` には**追加していません**。あの vcxproj は `pch.h` を
  強制インクルードするため、追加するとエンジン全体を巻き込んでしまいます。
  この Playground は独立した `Build.bat` でのみビルドします。

---

## 実験を追加するには

`ECSPlayground.cpp` に関数を 1 つ足して `main()` から呼ぶだけです。
vcxproj への登録も不要なので、思いついた検証をその場で書けます。

試す価値がある題材:

- `ForEachParallel` に変えて、`TaskScheduler` がどうバッチを組むか見る
- System を 2 本登録し、片方が `Write<Position>`、もう片方が `Read<Position>` の
  ときに直列化されるかを確認する（アーキテクチャ文書 15章「意図的なトレードオフ」を参照）
- Chunk 容量（682体）を超える数を生成し、2 個目の Chunk が作られる瞬間を見る
- `EventBus` の `Publish` / `Subscribe` を試す
