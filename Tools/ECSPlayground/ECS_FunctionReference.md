# CCL ECS 関数リファレンス

デバッガで潜っている最中に「この関数は何をするんだ？」で手が止まらないための辞書。

**使い方**: 止まった関数名を `Ctrl+F` で検索してください。
**方針**: エンジン本体のソースは一切変更していません。説明は全部このファイルに集約しています。

---

## 目次

1. [Chunk — データの実体](#1-chunk--データの実体)
2. [ChunkManager — 全Chunkの統括](#2-chunkmanager--全chunkの統括)
3. [World — 唯一の窓口](#3-world--唯一の窓口)
4. [Archetype / TypeData / EntityHandle — 型と ID](#4-archetype--typedata--entityhandle--型と-id)
5. [PendingOps — 予約の中身](#5-pendingops--予約の中身)
6. [System 層](#6-system-層)
7. [よく出る C++ の書き方](#7-よく出る-c-の書き方)

---

# 1. Chunk — データの実体

`Source/Engine/ECS/Core/CCL_Chunk.{h,cpp}`

**役割**: 同じ型構成のエンティティを SoA で1つの連続メモリに詰める「箱」。

**内部の持ち物**

| 変数 | 意味 |
|---|---|
| `_archetype` | この箱が入れる型の一覧（何のコンポーネントか） |
| `_memoryBlock` | 実データ本体。`vector<uint8_t>` の巨大な1本の帯 |
| `_componentOffsets` | 各コンポーネント配列が `_memoryBlock` の何バイト目から始まるか |
| `_ownerEntities` | 各席に座っている EntityID。空席は `InvalidEntityID` |
| `_entityCount` | 現在の入居者数 |
| `_capacity` | 定員（`Create` 時に決まり、以後変わらない） |
| `_entityStride` | 1体あたりの合計バイト数（全コンポーネントのサイズ合計） |
| `_destroyedEntities` | 「後で消す」と印を付けた席番号のリスト |

### Create(archetype, initialEntityCapacity)
**一言で**: 箱を作ってメモリを一括確保する。

型ごとのサイズを合計して `_entityStride` を出し、各コンポーネント配列の開始位置を
16バイト境界に揃えながら `_componentOffsets` に記録。最後に総バイト数で
`_memoryBlock.resize()` を**1回だけ**呼ぶ。

**★最重要**: 配列の幅は「今いる数」ではなく**定員**で決まる。
定員682体なら、8体しか居なくても Position 配列は 682体分の幅を持つ。
以降エンティティを足してもメモリ確保は二度と起きない（席を埋めるだけ）。

### CreateNull(capacity)
**一言で**: 型を1つも持たない空っぽの箱を作る。実データ領域は確保しない。

### Finalize()
**一言で**: 箱を畳む。中の全員のデストラクタを呼んでからメモリを解放。

`_componentOffsets` が空なら即 return（二重解放の防止）。
生きている要素だけに型ごとの `destructor` を呼ぶ。

### AddEntity(entity) → size_t
**一言で**: 席を1つ確保して、その**席番号**を返す。

満員なら `assert` して `InvalidIndex` を返す。やることは名簿に ID を書いて
`_entityCount++` するだけで、**メモリ確保は一切発生しない**。

### InsertEntity(entity) → void*
**一言で**: `AddEntity` で席を取り、その席を全部ゼロで初期化してから先頭ポインタを返す。

`AddEntity` との違いは「ゼロクリアするかどうか」だけ。

### RemoveEntity(entityIndex)
**一言で**: その席の全コンポーネントのデストラクタを呼び、名簿を無効化する。

メモリの穴を詰めるのはここではなく、後の `CompactHoles`。

### MarkDestroyEntity(entityIndex)
**一言で**: 「この席は後で消す」と印を付けるだけ。デストラクタは呼ばない。

名簿を `InvalidEntityID` にして `_destroyedEntities` に席番号を積む。
**破棄が2段階なのは、ループ中に席がずれると危険だから。**

### CompactHoles(onMoveCallback)
**一言で**: 印の付いた穴を詰めて、生きている要素を前に寄せる。

`read`（走査位置）と `write`（詰め先）の2本指で進む古典的なアルゴリズム。
空席なら `read` だけ進め、生きていて穴があれば後ろから前へデータを移動する。
移動には型の `mover`（ムーブ）を優先し、無ければ `memmove`。
移動元は「抜け殻」になるので `destructor` を呼んで始末する。

移動が起きるたび `onMoveCallback` で「この Entity がここへ動いた」と外へ通知する
（住所録の更新に必要）。最後に `_entityCount` を生存数へ更新。

### GetComponentPtr(compIdx, entityIndex) → void*  ※private
**一言で**: SoA 内の正確なアドレスを計算する、この設計の心臓部。

```cpp
_memoryBlock.data() + _componentOffsets[compIdx] + (entityIndex * typeSize)
       箱の先頭         その型の配列の開始位置        何番目か
```

### GetComponentPtrByType(TypeID, entityIndex) → void*
**一言で**: 型 ID から配列番号を引いて `GetComponentPtr` を呼ぶ。持っていなければ `nullptr`。

### GetComponentArray\<T\>() → T*
**一言で**: 型 T の配列の**先頭ポインタ**を返す。実験1で見ているのはこれ。

`ForEach` がループの外で一度だけ呼び、あとは `array[i]` で回すのが高速化の鉄則。

### GetComponentIndex(TypeID) → size_t
**一言で**: この箱の中で、その型が何番目の配列かを返す。無ければ `InvalidIndex`。
`_archetype` を先頭から線形探索する。

### IsEntityDestroyed(index) → bool
**一言で**: その席が破棄予約されているか。
`_destroyedEntities` を `std::find` で**線形探索**する（既知の性能上の弱点）。

### その他の小さな関数

| 関数 | 返すもの |
|---|---|
| `GetEntityCount()` | 現在の入居者数 |
| `GetCapacity()` | 定員 |
| `GetChunkSize()` | `_entityStride`（1体あたりのバイト数）※名前が紛らわしい |
| `GetArchetype()` / `GetTypeDatas()` | 型構成 |
| `GetEntityByIndex(i)` | i 番目の席の EntityID |
| `GetEntityIDs()` | 名簿配列の先頭ポインタ |
| `HasComponent<T>()` / `HasComponent(TypeID)` | その型を持つか |
| `HasDestroyedEntities()` | 破棄予約が1つでもあるか |
| `GetArchetypeHash()` | デバッグ用の簡易ハッシュ |

---

# 2. ChunkManager — 全Chunkの統括

`Source/Engine/ECS/Core/CCL_ChunkManager.{h,cpp}`

**役割**: 全 Chunk を持ち、エンティティを適切な Chunk へ振り分け・引っ越しさせる。

**内部の持ち物**

| 変数 | 意味 |
|---|---|
| `_chunks` | 全 Chunk のリスト。**一度作ると回収されない**（空でも残る） |
| `_entityLocationMap` | index → (chunkIndex, entityIndex) の住所録。O(1) 逆引き用 |
| `_onEntityMove` | Entity が動いたときに外へ知らせるコールバック |
| `_structureMutex` | 構造変更の排他用。読みは shared、書きは unique |
| `CHUNK_SIZE_BYTES` | 16384（16KB）固定。L1/L2 キャッシュに収まるサイズ |

### GetPureIndex(id) → size_t  ※ファイル内ヘルパー
**一言で**: EntityID から世代を捨てて、下位32bitの index だけ取り出す。
`id & 0xFFFFFFFF` するだけ。住所録の添字に使う。

### SearchEntityIn(entity, outEntityIndex) → size_t
**一言で**: 「その Entity は今どの Chunk の何番目にいるか」を O(1) で答える。

```cpp
size_t index = GetPureIndex(entity);          // 世代を捨てて index に
if (index >= _entityLocationMap.size()) return InvalidIndex;  // 範囲外
const auto& loc = _entityLocationMap[index];  // 配列アクセス一発
if (loc.chunkIndex == InvalidIndex) return InvalidIndex;      // 墓石（破棄済み）
if (outEntityIndex) *outEntityIndex = loc.entityIndex;        // 席番号を書き戻す
return loc.chunkIndex;                                        // Chunk 番号を返す
```

**返り値**: Chunk のインデックス。見つからなければ `InvalidIndex`。
**第2引数**: ポインタで渡すと Chunk 内の席番号が書き込まれる。不要なら `nullptr` で可。

**ポイント**: 全 Chunk を探し回るのではなく、**index を配列の添字にして一発で引く**。
これが O(1) の正体。世代を捨てるのは、世代が変わっても「配列のどこか」は同じだから。

**注意**: 世代チェックはしない。古い世代の ID を渡すと、再利用後の別 Entity の
住所が返ってくる。世代チェックは `World::IsEntityValid` の担当（役割分担）。

### CreateChunk(archetype)
**一言で**: その型構成の Chunk がまだ無ければ1つ作る。既にあれば何もしない。

### AddEntity(entity, archetype)
**一言で**: 空きのある Chunk を探して（無ければ作って）エンティティを入れ、住所録に登録。
`unique_lock` で排他する即時実行版。

### DestroyEntity(entity)
**一言で**: その Entity に削除マークを付け、住所録から消す。即時実行版。

### GetOrCreateAvailableChunk(archetype) → Chunk&
**一言で**: その型構成で**空きのある** Chunk を返す。無ければ新規作成して返す。

`std::find_if` で「型が一致 かつ 満員でない」Chunk を探す。
定員を超えたときに2個目の Chunk が生まれるのはここ。

### InsertNewEntity(dstChunk, entityID, addOps)
**一言で**: Spawn 経路の実体化処理。席を取り、初期値を書き込む。

1. `AddEntity` で席を確保し、住所録を更新
2. Chunk が持つ全ての型について、初期値を書く
3. `addOps`（AddComponent の予約）を**逆順**に探す
   → 同じ型を複数回 Set したとき、**最後の値が勝つ**仕様のため
4. 見つかれば `factory` → `mover` → `constructor` → `memcpy` の優先順で構築
5. 見つからなければ `defaultConstructor` かゼロクリア

### MoveEntityBetweenChunks(srcChunk, srcIndex, dstChunk, entity, addOps, removeTypeIDs) → bool
**一言で**: アーキタイプ遷移の本体。Chunk 間の「引っ越し」。

1. 引っ越し先に席を確保、住所録を更新
2. 両者の Archetype から TypeID→添字の逆引きマップを作る
3. 引っ越し先の各列を埋める
   - **パターンA**: 元の Chunk にもある型 → `mover` で移送
   - **パターンB**: 新規追加の型 → `addOps` の初期値で構築
   - **パターンC**: どちらでもない → ゼロクリア
4. 元 Chunk の全コンポーネントに `destructor` を呼ぶ
5. `_onEntityMove` で通知
6. 元 Chunk の席に削除マーク

**実験4で見ているのがこれ。** Position の値が引っ越し後も保持されるのはパターンA。

### ApplyPendingOperations(pendingOps) → vector\<EntityID\>
**一言で**: 溜まった予約を全部実行する総司令塔。**戻り値は実際に破棄された ID のリスト**。

呼び出し元は `World::ScrutinyAndApply` の1箇所だけ。

```
Phase 0 : unique_lock で排他（この間 System は動けない）
Phase 0': 予約が空なら即 return
Phase 1 : GroupOpsByEntity()      予約を Entity ごとに束ねる
Phase 2 : for each entity → ProcessEntityOps()
Phase 3 : CompactAllChunks()      全 Chunk の穴を詰める
```

### GroupOpsByEntity(pendingOps) → unordered_map\<EntityID, vector\<PendingOp*\>\>
**一言で**: バラバラの予約を Entity ごとに束ね直す。

```
入力（予約された順に一列）      出力（Entity ごと）
  Entity10 AddTransform          Entity10 → [AddTransform, AddModel]
  Entity20 RemovePhysics    →    Entity20 → [RemovePhysics]
  Entity10 AddModel              Entity30 → [Spawn]
  Entity30 Spawn
```

**なぜ束ねるのか（本質）**: 束ねないと、コンポーネントを足すたびに Chunk の引っ越しが
発生してしまう。先にまとめておけば「この Entity の最終形」を一度で計算でき、
引っ越しは1回で済む。

**`auto` について**: 実際の型は `std::unordered_map<EntityID, std::vector<PendingOp*>>`。
長いので `auto` にしているだけで意味は変わらない。
デバッガで型を確かめたいときは変数にマウスを乗せるか「自動変数」ウィンドウを見る。

### ProcessEntityOps(entityID, ops, alreadyDestroyed, destroyedEntities)
**一言で**: 1体分の予約をまとめて処理する。最も重要なオーケストレータ。

```
ステップ1: HasDestroyOp?  → あれば ApplyDestroyOp して即 return（破棄が最優先）
ステップ2: 今どこにいるか調べる + 既に死んでいないか二重チェック
ステップ3: DetermineNextArchetype()  次の型構成のベースを決める
ステップ4: CalculateArchetypeDiff()  Add/Remove を反映して最終形にする
ステップ5: 3分岐で確定
           - Spawn なら          → InsertNewEntity
           - 型構成が変わったら  → MoveEntityBetweenChunks（引っ越し）
           - 型構成が同じなら    → UpdateExistingComponents（値の上書きだけ）
ステップ6: ApplyPatchOps()  部分書き換えを適用
```

### HasDestroyOp(ops) → bool
**一言で**: 予約リストに Destroy が混ざっているか調べるだけ。

### ApplyDestroyOp(entityID, alreadyDestroyed, destroyedEntities)
**一言で**: 破棄の確定処理。

二重削除を `alreadyDestroyed`（set）で防ぎ、全コンポーネントの `destructor` を呼び、
Chunk に削除マークを付け、住所録から抹消し、結果リストへ積む。

**`unordered_set` とは**: 「重複を許さず、中身の有無を高速に判定できる入れ物」。

| | 順番 | 重複 | 存在確認 |
|---|---|---|---|
| `std::vector` | あり | OK | 先頭から線形探索 O(n) |
| `std::unordered_set` | なし | なし | ハッシュで O(1) |

`map` との違いは「キーと値のペア」か「キーだけ」かの一点。
ここでは「もう消したか？」を何度も問い合わせるので set が適任。

### DetermineNextArchetype(ops, srcChunk, outArchetype, outIsNewSpawn) → bool
**一言で**: 次の型構成の**出発点**を決める。

- 予約に Spawn があれば → `op.spawnArchetype` を採用、`isNewSpawn = true`
- 無ければ → 今いる Chunk の Archetype を引き継ぐ
- どちらも無い（存在しないし Spawn でもない）→ `false` を返して何もしない

### CalculateArchetypeDiff(ops, nextArch, outAddOps, outRemoveIDs)
**一言で**: Add/Remove を `nextArch` に反映して**最終形**にする。

- `AddComponent` → `outAddOps` に必ず積む（初期値として使うため）。
  まだ型が無ければ `TypeData` を組み立てて `nextArch.Add()`
- `RemoveComponent` → `outRemoveIDs` に積み、`nextArch.Sub()`

### UpdateExistingComponents(srcChunk, srcIndex, ops)
**一言で**: 型構成が変わらないときの、値の上書きだけの処理（引っ越し無し）。

- `factory` がある型 → 「破棄してから新規構築」（ムーブ専用型のため）
- `data` がある型 → `assigner`（代入演算子）で上書き。無ければ `memcpy`

### ApplyPatchOps(entityID, ops)
**一言で**: `PatchComponent` の予約を、**移動が全部終わった後の最終アドレス**に適用する。

最後に実行される理由は、引っ越しでアドレスが変わるから。
先に patch すると古い場所を書いてしまう。

### CompactAllChunks()
**一言で**: 全 Chunk に `CompactHoles` を呼ぶ。穴が詰まって席がずれるので、
コールバックで住所録と外部キャッシュの両方を更新する。

**最後にまとめてやる理由**: 1体消すたびに詰め直すと無駄が多いから。

### 住所録まわり

| 関数 | 内容 |
|---|---|
| `UpdateEntityLocation(entity, chunkIdx, entityIdx)` | 住所録に書き込む。足りなければ `resize` |
| `RemoveEntityLocation(entity)` | erase せず `InvalidIndex` で上書き（墓石を残す） |
| `GetChunkIndex(chunk*)` | Chunk ポインタ → 配列添字。**線形探索**（既知の弱点） |
| `SearchEqualChunk(archetype)` | 型構成が完全一致する Chunk を探す。線形探索 |

### CalculateChunkCapacity(archetype) → size_t
**一言で**: 定員を計算する。`16384 / 1体のバイト数`。

- 全部タグ型（サイズ0）なら → 1024
- 1体が16KBを超えるなら → 最低1体は確保

**実験1の 682 はここで出た値**（16384 / 24 = 682）。

---

# 3. World — 唯一の窓口

`Source/Engine/ECS/Core/CCL_World.{h,cpp}`

**内部の持ち物**

| 変数 | 意味 |
|---|---|
| `_chunkManager` | Chunk 群の管理者 |
| `_pendingOps` | 予約キュー |
| `_frameAllocator` | フレーム単位で使い捨てる高速メモリ |
| `_eventBus` | イベント配信 |
| `_entityGenerations` | index ごとの現在の世代 |
| `_freeIndices` | 再利用待ちの index |
| `_nextUnusedIndex` | まだ使っていない index の先頭（**初期値 1**。0 は予約） |
| `_isStructureDirty` | 今フレームで構造が変わったか |

### ScrutinyAndApply()
**一言で**: **フレームの境界線そのもの。** 予約を実体化する。

```cpp
deadList = _chunkManager.ApplyPendingOperations(_pendingOps);  // 1. 予約を全部実行
for (id : deadList) RecycleEntityID(id);                       // 2. ID を世代++して再利用へ
_pendingOps.Clear();                                           // 3. 予約リストを空に
_frameAllocator.Reset();                                       // 4. 一時メモリを一括解放
_isStructureDirty = true;                                      // 5. 構造が変わった印
```

**この関数の前後で世界が変わる**:
- 前: `GetComponent` は `nullptr`、Chunk 数 0
- 後: 実体が載っていてアドレスが返る

**なぜ即座にやらないのか**: System が `ForEach` を回している最中にメモリが動くと、
ループが握っているポインタが壊れるから。**これがこの ECS の最重要ルール。**

### RequestSpawnEntity(archetype) → EntityID
**一言で**: エンティティを予約し、**ID だけ即座に返す**。実体はまだ無い。

1. `_freeIndices` に空きがあれば再利用、無ければ `_nextUnusedIndex++`
2. `EntityHandle::Combine(index, generation)` で64bit ID を合成
3. `PendingOpKind::Spawn` を積む（`spawnArchetype` は**値でコピー**保持）

**値コピーの理由**: 昔は FrameAllocator にポインタを置いていたが、
`Reset()` のタイミング次第で破棄済みメモリを指す不具合があったため。

### Spawn(archetype) → EntityRef
**一言で**: `RequestSpawnEntity` を呼んで `EntityRef` に包んで返す（推奨API）。
`.Set(...).Patch<T>(...)` のメソッドチェーンが書けるようになる。

### AddComponent\<T\>(entity, args...)
**一言で**: コンポーネント追加を**予約**する。

FrameAllocator から一時メモリを借りてその場で T を構築し、そのポインタを予約に積む。
ヒープ確保より遥かに速く、`Reset()` で一括解放されるので `delete` 不要。

### AddComponent\<T\>(entity)（引数なし版）
**一言で**: デフォルト構築版。`factory`（構築関数）を登録する経路を通る。

### RequestRemoveComponent\<T\>(entity) / (entity, TypeID)
**一言で**: コンポーネント削除を予約する。

### PatchComponent\<T\>(entity, lambda)
**一言で**: 「既にある値の一部だけ書き換える」予約。

型安全なラムダを `void*` を受けるラムダに包んで積む。
生成直後でまだ `GetComponent` できないタイミングでも使える。

### Destroy(id)
**一言で**: `DestroyTag` を付けるだけ。**実際の破棄は別の System がやる。**

**★Playground での落とし穴**: 実破棄を担当する `HierarchyCleanupSystem` は
`Engine/Transform` 側にあり ECS コアに含まれない。
Playground では効かないので `RequestDestroyEntity` を使うこと。

### RequestDestroyEntity(entity)
**一言で**: 親子関係を無視した強制破棄の予約。フレームワーク内部用。

### RecycleEntityID(id)
**一言で**: 破棄された index の**世代を +1** して再利用リストへ戻す。

`UINT32_MAX` に達したら 1 に戻す（0 は無効値として避ける）。
**実験3の「index は同じ、世代だけ違う」はこれ。**

### IsEntityValid(entity) → bool
**一言で**: その ID がまだ生きているか。**世代チェックの本体。**

```cpp
index >= _entityGenerations.size()  → false（範囲外）
_entityGenerations[index] != gen    → false（世代が違う＝再利用された古いID）
それ以外                            → true
```

### GetComponent\<T\>(entity) → T*
**一言で**: 世代チェック → `GetComponentOfEntity` でアドレスを引く。無ければ `nullptr`。

### GetComponentOfEntity(entity, TypeID) → void*
**一言で**: 型 ID 指定の低レベル取得。エディタやシリアライザ用。

### HasComponent\<T\>(entity) / (entity, TypeID) → bool
**一言で**: `GetComponent` が `nullptr` でないかを見るだけ。

### GetHandle\<T\>(entity) → ComponentHandle\<T\>
**一言で**: 構造変更に強い安全ハンドルを返す。

生ポインタはフレームを跨ぐとダングリングするので、長く持つならこちら。
アクセスのたびに住所を引き直す。

### GetEntityArchetype(entity) → const Archetype&
**一言で**: その Entity の現在の型構成。存在しなければ空の Archetype。

### View\<Cs...\>() → vector\<EntityID\>
**一言で**: 指定の型を全部持つ Entity の ID 一覧を返す。

全 Chunk を走査するので**単発・エディタ向け**。
高頻度ループでは System の自動 Chunk マッチを使うほうが速い。

### GetSingletonComponent\<T\>() → T*
**一言で**: `View<T>()` の先頭1件を返す。世界に1つしかない前提のデータ用。

### Resource 系

| 関数 | 内容 |
|---|---|
| `AddResource<T>(args...)` | 特定 Entity に属さない「世界に1つ」のデータを登録 |
| `GetResource<T>()` | 型 → ID → 配列添字の O(1) アクセス。**未登録だと assert** |
| `HasResource<T>()` | 登録済みか。`GetResource` の前に必ず確認する規約 |

### その他

| 関数 | 内容 |
|---|---|
| `Clear()` | 全 Entity 削除、ID 管理をリセット。Resource は残す |
| `GetChunkManager()` | ChunkManager への参照 |
| `GetPendingOps()` | 予約キューへの参照 |
| `GetEventBus()` | イベントバスへの参照 |
| `IsStructureDirty()` / `ResetStructureDirty()` | 構造変更フラグの読み書き |

---

# 4. Archetype / TypeData / EntityHandle — 型と ID

### EntityHandle（CCL_Types.h）
EntityID は 64bit で、**上位32bit = 世代、下位32bit = index**。

| 関数 | 内容 |
|---|---|
| `GetIndex(id)` | `id & 0xFFFFFFFF` で下位32bit |
| `GetGeneration(id)` | `id >> 32` で上位32bit |
| `Combine(index, gen)` | `(gen << 32) \| index` で合成 |

**なぜ世代が要るのか**: index を再利用したとき、古い ID が新しい別の Entity を
指してしまう事故（ダングリングID）を防ぐため。

### Archetype（CCL_Archetype.h）
**役割**: 「持っている型の集合」。実データは持たない。
中身は `vector<TypeData>` を **TypeID 昇順ソート**で保持する（これが不変条件）。

| 関数 | 内容 |
|---|---|
| `Add<T>()` / `Add(TypeData)` | 二分探索して挿入（ソート維持） |
| `Sub<T>()` / `Sub(TypeID)` | 二分探索して削除 |
| `GetComponentIndex(TypeID)` | O(log N) で位置を返す |
| `operator==` | サイズと全要素の一致 |

### ArchetypeHelper

| 関数 | 内容 |
|---|---|
| `Generate<Ts...>()` | 型パックから Archetype を作る。`ArchetypeHelper::Generate<Position, Velocity>()` |
| `LhasR(chunk, filter)` | filter が chunk に**含まれる**か（System の適合判定） |
| `LExcludesR(l, exclude)` | exclude を**1つも含まない**か（除外フィルタ） |

### TypeInfo\<T\> / TypeData

`TypeInfo<T>::ID()` が型ごとに一意な `TypeID` を自動採番する。
`TypeData` はそれを「実行時に持ち回れる形」にしたレコードで、
`destructor / constructor / assigner / mover / defaultConstructor` といった
**関数ポインタの束**を持つ。

**なぜ関数ポインタが要るのか**: Chunk は `void*` の生メモリを扱うので、
「この番地を正しい型として破棄・移動する」手段を別途持つ必要がある（型消去）。

---

# 5. PendingOps — 予約の中身

`Source/Engine/ECS/Core/CCL_PendingOps.h`

`PendingOpKind` = `Spawn / Destroy / AddComponent / RemoveComponent / PatchComponent / None`

`PendingOp` が運ぶもの:

| フィールド | 意味 |
|---|---|
| `kind` | 何の予約か |
| `entity` | 対象の EntityID |
| `type` / `typeSize` | 対象の型 ID とサイズ |
| `data` | 初期値。**FrameAllocator 上**に確保される |
| `factory` | 構築関数（ムーブ専用型で使う） |
| `patcher` | 部分書き換えのラムダ |
| `destructor/constructor/assigner/mover` | 型消去された操作の関数ポインタ |
| `spawnArchetype` | Spawn 用の型構成（**値で保持**） |

`PendingOps::Clear()` は AddComponent の `op.data`（ムーブ後の抜け殻）に
`destructor` を呼ぶ。メモリ自体は `FrameAllocator::Reset()` で一括回収されるので
`delete` は不要。

---

# 6. System 層

### ECSContext（CCL_ECSContext.h）
`World` と `SystemManager` を束ねた1つの ECS 環境。

| 関数 | 中身 |
|---|---|
| `UpdateLogic(dt)` | ①`ScrutinyAndApply` ②dirtyなら`ResolveTargetChunks` ③`SystemManager::UpdateLogic` |
| `UpdateRender(dt)` | `SystemManager::UpdateRender` |
| `UpdatePaused(dt)` | ロジックを止めて構造変更と描画だけ |

**この3ステップがフレームの全て。** 実験5で追っているのがこれ。

### SystemAccess（CCL_SystemAccess.h）
- `Read<T>` → `ForEach` で `const T&` で受け取る。`IsWrite = false`
- `Write<T>` → `T&` で受け取る。`IsWrite = true`

飾りではなく、**TaskScheduler が並列可否を判断する材料**になる。

### SystemBase（CCL_SystemBase.h）
全 System の基底（非テンプレートで軽量）。

| メンバ/関数 | 内容 |
|---|---|
| `_targetChunks` | 今フレーム処理する Chunk のリスト（SystemManager が配る） |
| `_filter` | 必要な型構成 |
| `_excludeFilter` | 除外する型構成 |
| `Initialize()` | 登録時に1度だけ呼ばれる |
| `Update(dt)` | 純粋仮想。派生が実装する本体 |
| `UpdateImpl(dt)` | 計測付きラッパー。スケジューラはこちらを呼ぶ |
| `GetReadTypes()` / `GetWriteTypes()` | 依存解析用。IfSystem が自動実装 |

### IfSystem（CCL_IfSystem.h）
実装の中心。`IfSystem<自分自身, Write<A>, Read<B>>` の形で継承する。

| 関数 | 内容 |
|---|---|
| `ForEach(func)` | 担当 Chunk を順に走査。**配列ポインタをループ外で一括取得**してから回す |
| `ForEachParallel(func)` | 同上を JobSystem で並列化 |
| `ForEachWithID(func)` | EntityID も一緒に受け取れる版 |
| `SetExclude<Ts...>()` | 指定型を持つ Entity を対象外にする |
| `ListenEvent<E>(cb)` | イベント購読。**デストラクタで自動解除される** |

コンストラクタで `AccessTypes` から `_filter` を自動生成するので、
フィルタを手書きする必要がない。

### SystemManager（CCL_SystemManager.{h,cpp}）

| 関数 | 内容 |
|---|---|
| `RegisterSystem<T>(stage, args...)` | 生成 → World/優先度/mutex/JobSystem を注入 → `Initialize()` → 優先度ソート |
| `ResolveTargetChunks(cm)` | 各 System の `_targetChunks` に該当 Chunk を配る |
| `UpdateLogic(dt)` | グラフが古ければ `BuildGraph` → `TaskScheduler::Execute` |
| `UpdateRender(dt)` | 描画系 System を実行 |
| `GetSystem<T>()` | 登録済み System を探す |

**`ResolveTargetChunks` の適合条件**: `LhasR(chunkArch, filter) && LExcludesR(chunkArch, exclude)`。
Chunk ポインタ配列が前回と完全一致ならスキップ（構造不変なら 0ms）。

**実験5で c が動かないのはここ**。Velocity を持たないので担当 Chunk から外れる。

### TaskScheduler（CCL_TaskScheduler.h）
System の Read/Write を解析して「同時に走らせてよい集合＝バッチ」を作る。

```
_executionBatches:
  外側 = 実行順（Batch0 → Batch1 → ...）  ← 直列
  内側 = 同じバッチ内の System            ← 並列OK
```

判定ルール: ①ステージが違えば必ず直列 ②同ステージのみ R/W 競合を判定
③型が競合しても Chunk が完全に独立なら並列化

### JobSystem（CCL_JobSystem.{h,cpp}）
コア数分のワーカースレッド + 単一ジョブキュー。

| 関数 | 内容 |
|---|---|
| `Execute(job, counter)` | カウンタを増やしてキューへ投入 |
| `WaitForCounter(counter)` | 0 になるまで待つ。**待つ間は自分も仕事を手伝う**（デッドロック回避） |

### FrameAllocator（CCL_FrameAllocator.h）
フレーム単位でリセットする超高速アリーナ。

| 関数 | 内容 |
|---|---|
| `Alloc(size, align)` | 位置をずらすだけの確保 |
| `New<T>(args...)` | placement new。非トリビアル型はデストラクタを登録 |
| `Reset()` | 登録デストラクタを実行し、**ページは解放せず**書き込み位置を先頭に戻す |

### EventBus（CCL_EventBus.h）

| 関数 | 内容 |
|---|---|
| `Subscribe<E>(cb)` | 購読してチケットIDを受け取る |
| `Unsubscribe<E>(id)` | 解除 |
| `Publish<E>(data)` | 発行。**即時・非ロック・非再入** |

**注意**: シングルスレッド前提。並列バッチ内から `Publish` すると競合する。

---

# 7. よく出る C++ の書き方

デバッグ中に「これ何？」となりやすい構文のメモ。

### `auto`
型を書くのが長いときの省略。**意味は変わらない**。
デバッガでは変数にマウスを乗せるか「自動変数」ウィンドウで実際の型が見える。

### 構造化束縛 `for (auto& [key, value] : map)`
map から「キー」と「値」を同時に取り出す書き方。

```cpp
for (auto& [entityID, ops] : opsByEntity)   // entityID と ops に分解される
```

### コンテナの使い分け

| 型 | 順番 | 重複 | 検索 | 用途 |
|---|---|---|---|---|
| `vector` | あり | OK | O(n) | 順に舐めるだけならこれ |
| `unordered_set` | なし | なし | O(1) | 「含まれるか？」を何度も聞く |
| `unordered_map` | なし | キー重複なし | O(1) | キーから値を引く |

### `std::find_if(begin, end, ラムダ)`
条件に合う最初の要素を探す。見つからなければ `end()` が返る。

### `unique_lock` と `shared_lock`
- `unique_lock` = 書き込み用。**自分1人だけ**が入れる
- `shared_lock` = 読み取り用。**複数同時に**入れる

`ApplyPendingOperations` が `unique_lock` なのは、構造を書き換える間
System に読ませてはいけないから。

### `void*`
「型が分からない番地」。Chunk は生メモリを扱うのでこれを多用し、
`TypeData` の関数ポインタ経由で正しい型として操作する（型消去）。

### `reinterpret_cast<T*>(ptr)`
「この番地を T として見る」という強制変換。SoA のアドレス計算結果を
実際の型に戻すときに使う。

### `placement new` — `new (ptr) T(args)`
**メモリ確保をせず**、既にある番地に T を構築する。
Chunk は事前にメモリを確保済みなので、構築だけしたいときに使う。
