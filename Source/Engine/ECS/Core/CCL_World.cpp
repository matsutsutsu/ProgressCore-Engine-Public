#include "CCL_World.h"
#include "../System/CCL_SystemManager.h"
#include "Engine/Graphics/Renderer/ShapeRenderer.h"
#include <algorithm>
#include <cassert>
#include <cstring>

// Tracyのインクルード
#include "tracy/Tracy.hpp"


namespace CCL::ECS {
    using namespace CCL::ECS;

    // ============================================================================
    // World::World  ─ コンストラクタ
    // ----------------------------------------------------------------------------
    //  一言で : 世代テーブルを1024個ぶん用意し、index の払い出しを 1 から始める。
    //
    //  なぜ index 0 を使わないのか
    //    0 を「無効・未設定」の意味で使いたいため。1 から配ることで、
    //    ゼロ初期化されただけの EntityID を「有効なエンティティ」と誤認しない。
    //    実験2で最初の ID が index:1 になるのはこれが理由。
    // ============================================================================
    World::World()
    {
        _entityGenerations.resize(1024, 1); // 初期サイズ確保
        _nextUnusedIndex = 1;               // インデックス0は予約
    }

    World::~World()
    {
        _chunkManager.FinalizeAll();
        _pendingOps.Clear();
    }

    void World::Clear()
    {
        // 1. 保留中の操作を破棄
        _pendingOps.Clear();
        _chunkManager.FinalizeAll();

        // イベントの購読（Subscribe）も全て白紙に戻す
        _eventBus.Clear();

        // 3. キャッシュとID管理情報をリセット
        _entityGenerations.clear();
        _freeIndices.clear();

        // インデックスを初期状態(1)に戻す
        _nextUnusedIndex = 1;

        // ※ _resources (シングルトン) は残す設計にするか、消す設計にするかはお好みで。
        //   通常、Gridなどのリソースは再登録が必要になるため、ここで消しても良いですが、
        //   Initialize() で一度しか登録しない設計なら消さない方が無難です。
        //   今回は「エンティティのリセット」が目的なので resources は触りません。
    }



    // ============================================================================
    // World::RequestAddComponent  ─ コンポーネント追加の「予約」を積む
    // ----------------------------------------------------------------------------
    //  一言で : ここでは何も追加されない。PendingOps に予約を1件積むだけ。
    //           実際に追加されるのは次の ScrutinyAndApply()。
    //
    //  引数の意味（型消去のための関数ポインタ群）
    //    ctor     : 構築関数。あれば factory 経由で「その場で構築」する
    //    dtor     : デストラクタ
    //    copyCtor : コピー構築
    //    assigner : 代入演算子（同じ型構成のまま値だけ上書きするとき使う）
    //    mover    : ムーブ構築（コピー禁止型でも運べるようにするため）
    //    name     : デバッグ表示用の型名
    //
    //  なぜこんなに関数ポインタを持ち回るのか
    //    Chunk は void* の生メモリを扱うので、型情報が消えている。
    //    「この番地を正しい型として破棄・移動・代入する」手段を
    //    データと一緒に運ぶ必要がある。これが型消去(Type Erasure)。
    //
    //  ctor がある場合と無い場合
    //    ctor あり : op.factory に保存し、op.data は使わない（構築は適用時に行う）
    //    ctor なし : FrameAllocator からメモリを借りてゼロ埋めし、op.data に持たせる
    //
    //  先頭の IsEntityValid で弾いているのは、既に死んだ ID への操作を無視するため。
    // ============================================================================
    // コンポーネント追加リクエストの実装部分
    // World.h の AddComponent<T> から呼ばれます
    void World::RequestAddComponent(EntityID entity,
        TypeID                               tid,
        size_t                               size,
        const std::function<void(void *)>   &ctor,
        const Destructor                    &dtor,
        TypeData::ConstructFunc              copyCtor,
        TypeData::AssignFunc                 assigner,
        TypeData::MoveFunc                   mover,
        const char                          *name)
    {
        if (!IsEntityValid(entity)) return;

        PendingOp op;
        op.kind     = PendingOpKind::AddComponent;
        op.entity   = entity;
        op.type     = tid;
        op.typeSize = size;
        op.destructor = dtor;
        // 関数ポインタの保存
        // ChunkManagerの ScrutinyAndApply で TypeData を生成する際、
        // これらのポインタがないと nullptr になり、memcpy へのフォールバックが発生してしまう
        op.constructor = copyCtor;
        op.assigner    = assigner;
        op.mover       = mover;
        op.name        = name; // カプセルに名前を入れる

        // ここでコンストラクタを即時実行せず、factoryに保存する
        if (ctor) {
            op.factory = ctor;
            op.data    = nullptr; // dataは使わない
        }
        else {
            // ctorがない場合（従来通り）はメモリ確保してコピー用データを作る
            void *mem = _frameAllocator.Alloc(size);
            std::memset(mem, 0, size); // またはデフォルト構築
            op.data    = mem;
            op.factory = nullptr;
        }

        _pendingOps.Add(op);
    }

    void World::RequestRemoveComponent(EntityID entity, TypeID tid)
    {
        // 世代チェック
        if (!IsEntityValid(entity)) return;

        PendingOp op;
        op.kind     = PendingOpKind::RemoveComponent;
        op.entity   = entity;
        op.type     = tid;
        op.typeSize = 0; // 削除時はサイズ情報は不要
        op.data     = nullptr;

        _pendingOps.Add(op);
    }

    // ============================================================================
    // World::RequestSpawnEntity  ─ エンティティ生成を予約し、ID だけ即座に返す
    // ----------------------------------------------------------------------------
    //  一言で : 「予約番号」を発行して返す。実体（Chunk 内の席）はまだ存在しない。
    //
    //  戻り値 : 世代付き EntityID。呼び出し直後から AddComponent 等に使える。
    //
    //  Phase 1 : 世代付き ID の発行
    //    _freeIndices に空きがあれば再利用（既に世代は上がっている）
    //    無ければ _nextUnusedIndex++ で新規発行し、世代は 1 から始める
    //    最後に EntityHandle::Combine(index, generation) で
    //    上位32bit=世代 / 下位32bit=index の 64bit ID に合成する
    //
    //  Phase 2 : PendingOp への登録
    //    op.spawnArchetype に Archetype を【値でコピー】して持たせる。
    //
    //  ★なぜ値コピーなのか（過去の不具合の名残）★
    //    昔は FrameAllocator 上に Archetype を置いてポインタを渡していたが、
    //    FrameAllocator::Reset() のタイミング次第で破棄済みメモリを指してしまい
    //    クラッシュした。値で持てば PendingOp が生きている限り安全。
    //
    //  実験2で「Spawn 直後は GetComponent が nullptr」なのは、
    //  この関数が予約を積むだけで、Chunk には一切触れていないから。
    // ============================================================================
    EntityID World::RequestSpawnEntity(const Archetype &archetype)
    {
        uint32_t index;
        uint32_t generation;

        // --- Phase 1: 世代付きIDの発行 ---
        {
            // 再利用リストに空きがあればそれを使う
            if (!_freeIndices.empty()) {
                index = _freeIndices.back();
                _freeIndices.pop_back();
                generation = _entityGenerations[index];
            }
            // 空きがなければ新規発行
            else {
                index = _nextUnusedIndex++;
                if (index >= _entityGenerations.size()) {
                    _entityGenerations.resize(index + 1024, 1);
                }
                generation                = 1;
                _entityGenerations[index] = 1;
            }
        }

        EntityID id = EntityHandle::Combine(index, generation);

        // --- Phase 2: PendingOp への登録 ---
         // ★ 修正: FrameAllocator に Archetype を置いてポインタを渡す方式をやめる
         //   旧: Archetype* heapArch = _frameAllocator.New<Archetype>(archetype);
         //       op.data = reinterpret_cast<void*>(heapArch);
         //   ↓ FrameAllocator::Reset() のタイミング次第で heapArch が破棄済みになる
         //
         //   新: op.spawnArchetype にコピーして保存（PendingOp が生きている限り安全）

        PendingOp op;
        op.kind   = PendingOpKind::Spawn;
        op.entity = id; // 新しい世代付きIDをセット
        op.spawnArchetype = archetype; // ★ 値コピーで保存
        op.data = nullptr;   // 使わない


        _pendingOps.Add(op);

        // 生成したIDを即座に返す（ユーザーはすぐこのIDを使える）
        return id;
    }

    // ============================================================================
    // World::RecycleEntityID  ─ 破棄された index の世代を上げて再利用プールへ戻す
    // ----------------------------------------------------------------------------
    //  一言で : index はまた使うが、世代を +1 することで古い ID を無効化する。
    //
    //  呼ばれる場所
    //    World::ScrutinyAndApply。ApplyPendingOperations が返した「実際に
    //    破棄された ID のリスト」を1件ずつここへ渡す。
    //
    //  ★これがダングリングID対策の本体★
    //    index を再利用すると、古い EntityID が新しい別のエンティティを
    //    指してしまう事故が起きる。世代を上げておけば、
    //    IsEntityValid() の世代照合で古い ID を確実に弾ける。
    //
    //    実験3の出力がまさにこれ:
    //      破棄した古いID = index:3 / 世代:1  → IsEntityValid = false
    //      新しいID       = index:3 / 世代:2  → IsEntityValid = true
    //
    //  オーバーフロー対策
    //    世代が UINT32_MAX に達したら 1 に戻す（0 は無効値として避けている）。
    // ============================================================================
    void World::RecycleEntityID(EntityID id)
    {
        uint32_t index = EntityHandle::GetIndex(id);

        // ここが世代オーバーフロー対策
        // UINT32_MAXまで行ったら1に戻す（0は無効値として避ける）
        if (_entityGenerations[index] == UINT32_MAX) {
            _entityGenerations[index] = 1;
        }
        else {
            _entityGenerations[index]++;
        }

        // インデックスを再利用リストへ
        _freeIndices.push_back(index);
    }

    void World::RequestDestroyEntity(EntityID entity)
    {
        PendingOp op;
        op.kind     = PendingOpKind::Destroy;
        op.entity   = entity;
        op.type     = InvalidTypeID; // destroy marker
        op.typeSize = 0;
        op.data     = nullptr;

        _pendingOps.Add(op);
    }

    // --- ScrutinyAndApply: 遅延実行関数 ---
    //  やってはいけないこと
    //  ────────────────────────
    //  System実行中に...
    //  entity.AddComponent<Velocity>()
    //  ↓
    //  即座にChunk移動が発生すると...
    //  ↓
    //  今まさにループ中のChunk構造が変わり、イテレータが無効化される
    //  ↓
    //  クラッシュ / メモリ破壊
    //
    //  安全な遅延実行モデル
    //  ────────────────────────
    //  [System実行中]
    //    → 変更要求だけをPendingOpsに記録（Request...）
    //
    //  [Systemが全部終わった後]
    //    → ScrutinyAndApply()
    //      → まとめて整合性を保ちながら実行
    //      → 次フレームのSystemは最新状態で動く
    //
    // ============================================================================
    // World::ScrutinyAndApply  ─ ★フレームの境界線そのもの★
    // ----------------------------------------------------------------------------
    //  一言で : 溜めた予約を、実際のメモリ操作にまとめて変換する。
    //
    //  この関数の前後で世界が変わる
    //    入る前 : Spawn/AddComponent/Destroy は全て「予約」でしかない。
    //             Chunk 数 0、GetComponent は nullptr。
    //    抜けた後: 実体が Chunk に載っていて、GetComponent がアドレスを返す。
    //
    //  呼ばれる場所
    //    ECSContext::UpdateLogic() の最初。System が1つも動いていない
    //    このタイミングでしか構造を変えない、というのがこの ECS の最重要ルール。
    //
    //  処理の流れ
    //    1. ApplyPendingOperations : 予約を全部実行し、破棄された ID を受け取る
    //    2. RecycleEntityID        : 破棄された index の世代を上げて再利用へ
    //    3. _pendingOps.Clear()    : 予約リストを空にする
    //    4. _frameAllocator.Reset(): 一時メモリを一括解放（delete は不要）
    //    5. _isStructureDirty = true : 「構造が変わった」印。これを見て
    //                                  ECSContext が ResolveTargetChunks を呼ぶ
    //
    //  実験2で追っているのがこの関数。F11 で 1. の中へ潜ると本体が見える。
    // ============================================================================
    void World::ScrutinyAndApply()
    {
        // 構造変更の適用時間を計測
        ZoneScopedN("World::ScrutinyAndApply");

        // 1. 溜まっていた予約（AddComponent, RemoveComponent, Spawn, Destroy）を
        //    ChunkManager に渡して、物理的なメモリ移動や生成を実行させる
        //    戻り値として、実際に削除されたEntityIDのリストを受け取る

        // 1. 保留されていた構造変更を適用
        //    この内部で SetEntityMoveCallback で登録したラムダが呼ばれ、
        //    _entityCache が自動的に更新されます！
        std::vector<EntityID> deadList = _chunkManager.ApplyPendingOperations(_pendingOps);



        // 3. IDをリサイクル（世代を上げてフリーリストへ）
        for (EntityID id : deadList) {
            RecycleEntityID(id);
        }


        // 3. アロケータのリセット
        // これで一時メモリは一瞬で解放されます。delete op.data は不要です。
        // (デストラクタが必要な場合は PendingOps::Clear 内で呼ぶ必要がありますが、
        //  生データコピーなら不要、管理が必要なら工夫がいりますが、一旦メモリ解放優先で)
        _pendingOps.Clear();
        _frameAllocator.Reset(); // ★ 一瞬でリセット

        // 構造変更があったことを記録
        _isStructureDirty = true;
    }

    // GetEntityArchetype の実装
    const Archetype &World::GetEntityArchetype(EntityID entity) const
    {
        size_t entityIdx = 0;
        // ChunkManagerのO(1) vectorアクセスを利用
        size_t chunkIdx = _chunkManager.SearchEntityIn(entity, &entityIdx);

        if (chunkIdx == InvalidIndex) {
            // 存在しない、または削除済みの場合は空のアーキタイプを返す
            static const Archetype emptyArchetype;
            return emptyArchetype;
        }

        return _chunkManager.GetChunks()[chunkIdx]->GetArchetype();
    }

    bool World::HasComponent(EntityID entity, TypeID tid)
    {
        return GetComponentOfEntity(entity, tid) != nullptr;
    }

    // ============================================================================
    // World::GetComponentOfEntity  ─ 型ID指定でコンポーネントの生アドレスを引く
    // ----------------------------------------------------------------------------
    //  一言で : 住所録で場所を引き、その Chunk から番地を計算して返す。
    //
    //  流れ
    //    SearchEntityIn で (どの Chunk / 何番目) を O(1) で取得
    //      → その Chunk の GetComponentPtrByType で番地を計算
    //
    //  ★注意：この関数は世代チェックをしない★
    //    世代照合は呼び出し元の GetComponent<T>（World.h のテンプレ版）が
    //    IsEntityValid() で先に済ませている。役割分担になっている。
    //    ここを直接呼ぶと、古い世代の ID でも中身が返ってきてしまう。
    //
    //  戻り値は void*。呼び出し元が正しい型へキャストして使う。
    // ============================================================================
    // Low-level access
    void *World::GetComponentOfEntity(EntityID entity, TypeID tid)
    {
        size_t entityIdx = 0;
        size_t chunkIdx  = _chunkManager.SearchEntityIn(entity, &entityIdx);
        if (chunkIdx == InvalidIndex) return nullptr;
        return _chunkManager.GetChunks()[chunkIdx]->GetComponentPtrByType(tid, entityIdx);
    }

} // namespace CCL::ECS