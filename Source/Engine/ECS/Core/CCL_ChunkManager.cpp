#include "CCL_ChunkManager.h"
#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <cassert>

namespace CCL::ECS
{
    using namespace CCL::ECS;

    // ----------------------------------------------------------------
    // 内部ユーティリティ
    // ----------------------------------------------------------------

    // ============================================================================
    // GetPureIndex  ─ EntityID から世代を捨てて index だけ取り出す
    // ----------------------------------------------------------------------------
    //  一言で : 下位32bit（index）だけを残すビット演算。
    //
    //  EntityID は 64bit で [上位32bit = 世代][下位32bit = index] の構造。
    //  0xFFFFFFFF と & を取ると、上位32bit が 0 になり index だけが残る。
    //
    //  なぜ世代を捨てるのか
    //    住所録 m_entityLocationMap は「index を配列の添字」にして引く。
    //    世代が何であれ「配列のどこか」は同じなので、index だけあればよい。
    //    世代の照合は World::IsEntityValid の担当（役割分担）。
    //
    //  実装メモ
    //    ビットレイアウト（マスク・シフト量）の定義は CCL_Types.h の EntityHandle
    //    に一本化してある。ここで 0xFFFFFFFF を直接書くとレイアウトの定義が
    //    二箇所に散るため、EntityHandle::GetIndex を経由している。
    //    戻り値が size_t なのは、呼び出し側が vector の添字として使うため。
    // ============================================================================
    /// @brief EntityID から世代情報を切り捨て、純粋なインデックスを取り出す
    inline size_t GetPureIndex(EntityID id)
    {
        return static_cast<size_t>(EntityHandle::GetIndex(id));
    }

    // ================================================================
    // 公開 API
    // ================================================================

    // ============================================================================
    // ChunkManager::CreateChunk  ─ その型構成の箱が無ければ1つ作る
    // ----------------------------------------------------------------------------
    //  一言で : 既に同じ Archetype の Chunk があれば何もしないで return。
    //  定員は CalculateChunkCapacity（16KB ÷ 1体のバイト数）で決める。
    //  注意   : 「空きがあるか」は見ない。空きも見たいなら GetOrCreateAvailableChunk。
    // ============================================================================
    void ChunkManager::CreateChunk(const Archetype& archetype)
    {
        for (auto& c : m_chunks)
        {
            if (c->GetArchetype() == archetype)
                return;
        }
        m_chunks.emplace_back(std::make_unique<Chunk>());

        // アーキタイプから計算した定員を渡す
        size_t capacity = CalculateChunkCapacity(archetype);
        m_chunks.back()->Create(archetype, capacity);
    }

    // ============================================================================
    // ChunkManager::FinalizeAll  ─ 全 Chunk を畳んで住所録も空にする
    // ----------------------------------------------------------------------------
    //  一言で : World の終了時・Clear 時に呼ばれる後片付け。
    //  unique_lock で排他するのは、構造を壊す操作だから。
    // ============================================================================
    void ChunkManager::FinalizeAll()
    {
        std::unique_lock<std::shared_mutex> lock(m_structureMutex);

        for (auto& c : m_chunks)
            c->Finalize();
        m_chunks.clear();

        m_entityLocationMap.clear();
    }

    // ============================================================================
    // ChunkManager::AddEntity  ─ エンティティを箱に入れる（即時実行版）
    // ----------------------------------------------------------------------------
    //  一言で : 空きのある Chunk を探し（無ければ作り）、席を確保して住所録に登録。
    //
    //  流れ
    //    1. find_if で「型が一致 かつ 満員でない」Chunk を探す
    //    2. 無ければ新規作成（定員は CalculateChunkCapacity で計算）
    //    3. InsertEntity で席を確保（ゼロクリア込み）
    //    4. UpdateEntityLocation で住所録に登録
    //
    //  注意
    //    こちらは予約を経由しない【即時実行】ルート。
    //    通常のゲームコードから来る Spawn は PendingOps 経由で
    //    ApplyPendingOperations → InsertNewEntity を通る。
    // ============================================================================
    void ChunkManager::AddEntity(EntityID entity, const Archetype& archetype)
    {
        std::unique_lock<std::shared_mutex> lock(m_structureMutex);

        // 1. 同 Archetype かつ空きがある Chunk を探す
        auto it = std::find_if(m_chunks.begin(), m_chunks.end(),
            [&](const std::unique_ptr<Chunk>& c) {
                return c->GetArchetype() == archetype &&
                       c->GetEntityCount() < c->GetCapacity();
            });

        // 2. 無ければ新規作成
        if (it == m_chunks.end())
        {
            m_chunks.emplace_back(std::make_unique<Chunk>());
            size_t capacity = CalculateChunkCapacity(archetype);
            m_chunks.back()->Create(archetype, capacity);
            it = std::prev(m_chunks.end());
        }

        // 3. 挿入
        (*it)->InsertEntity(entity);

        // 4. 位置を登録
        size_t chunkIdx  = std::distance(m_chunks.begin(), it);
        size_t entityIdx = (*it)->GetEntityCount() - 1;
        UpdateEntityLocation(entity, chunkIdx, entityIdx);
    }

    // ============================================================================
    // ChunkManager::DestroyEntity  ─ 削除マークを付けて住所録から消す（即時実行版）
    // ----------------------------------------------------------------------------
    //  一言で : MarkDestroyEntity（印を付けるだけ）+ 住所録の抹消。
    //  注意   : デストラクタは呼んでいない。予約経由の破棄は ApplyDestroyOp が
    //           destructor を呼んでからここと同じことをする。
    // ============================================================================
    void ChunkManager::DestroyEntity(EntityID entity)
    {
        std::unique_lock<std::shared_mutex> lock(m_structureMutex);

        size_t entityIndex;
        size_t chunkIndex = SearchEntityIn(entity, &entityIndex);
        if (chunkIndex != InvalidIndex)
        {
            m_chunks[chunkIndex]->MarkDestroyEntity(entityIndex);
            RemoveEntityLocation(entity);
        }
    }

    // ============================================================================
    // ChunkManager::SearchEntityIn  ─ 「その Entity は今どこにいるか」を O(1) で答える
    // ----------------------------------------------------------------------------
    //  一言で : 住所録を index で引くだけ。全 Chunk を探し回ったりはしない。
    //
    //  引数
    //    entity         : 探したい EntityID
    //    outEntityIndex : ポインタを渡すと「Chunk 内の席番号」が書き込まれる。
    //                     席番号が要らないときは nullptr でよい（既定値）
    //
    //  戻り値
    //    見つかれば Chunk のインデックス（m_chunks の添字）
    //    見つからなければ InvalidIndex
    //
    //  処理
    //    1. GetPureIndex で世代を捨てて index にする
    //    2. 範囲外なら InvalidIndex
    //    3. m_entityLocationMap[index] を配列アクセス一発で取る ← O(1)の正体
    //    4. chunkIndex が InvalidIndex なら「墓石」＝破棄済み
    //    5. 席番号を書き戻して Chunk 番号を返す
    //
    //  注意: 世代チェックをしない
    //    古い世代の ID を渡すと、index を再利用した【別の】エンティティの
    //    住所が返ってくる。世代照合は World::IsEntityValid の担当。
    //    World::GetComponent は先に IsEntityValid を通してからここへ来る。
    //
    //  「墓石」とは
    //    RemoveEntityLocation は要素を erase せず InvalidIndex で上書きする。
    //    配列の添字が index と対応している以上、途中を削ると全部ずれるため。
    // ============================================================================
    size_t ChunkManager::SearchEntityIn(EntityID entity, size_t* outEntityIndex) const
    {
        size_t index = GetPureIndex(entity);

        if (index >= m_entityLocationMap.size()) return InvalidIndex;

        // O(1) のキャッシュヒット確定アクセス
        const auto& loc = m_entityLocationMap[index];

        // 墓石（無効化済み）
        if (loc.chunkIndex == InvalidIndex) return InvalidIndex;

        if (outEntityIndex) *outEntityIndex = loc.entityIndex;
        return loc.chunkIndex;
    }

    // ============================================================================
    // ChunkManager::GetOrCreateAvailableChunk  ─ 空きのある箱を返す（無ければ作る）
    // ----------------------------------------------------------------------------
    //  一言で : 「型が一致 かつ 満員でない」Chunk を探し、無ければ新規作成して返す。
    //
    //  戻り値は Chunk& （参照）。必ず有効な箱が返るので nullptr チェックは不要。
    //
    //  定員を超えたときに2個目の Chunk が生まれるのはここ
    //    条件に GetEntityCount() < GetCapacity() が入っているため、
    //    683体目を入れようとすると既存 Chunk が弾かれて新しい箱が作られる。
    //
    //  std::find_if(begin, end, ラムダ)
    //    条件に合う最初の要素を探す標準関数。見つからなければ end() が返る。
    // ============================================================================
    Chunk& ChunkManager::GetOrCreateAvailableChunk(const Archetype& archetype)
    {
        // ? 何やっているのか全体的にわからん
        auto it = std::find_if(m_chunks.begin(), m_chunks.end(),
            [&](const std::unique_ptr<Chunk>& c) {
                return c->GetArchetype() == archetype &&
                       c->GetEntityCount() < c->GetCapacity();
            });

        if (it == m_chunks.end())
        {
            m_chunks.emplace_back(std::make_unique<Chunk>());
            size_t capacity = CalculateChunkCapacity(archetype);
            m_chunks.back()->Create(archetype, capacity);
            return *m_chunks.back();
        }
        return **it;
    }

    // ============================================================================
    // ChunkManager::InsertNewEntity  ─ Spawn 経路の実体化処理
    // ----------------------------------------------------------------------------
    //  一言で : 新しいエンティティの席を取り、初期値を全部書き込む。
    //
    //  引数
    //    dstChunk : 入れる先の箱
    //    entityID : 実体化するエンティティ
    //    addOps   : この Entity への AddComponent 予約の一覧（＝初期値の供給源）
    //
    //  流れ
    //    1. AddEntity で席を確保し、住所録を更新
    //    2. 箱が持つ全ての型について、初期値を書き込んでいく
    //    3. addOps を【逆順】に探して、その型の初期値があるか調べる
    //    4. あれば構築、無ければデフォルト構築 or ゼロクリア
    //    5. m_onEntityMove で「実体化した」ことを外へ通知
    //
    //  なぜ addOps を逆順ループするのか
    //    同じ型に対して Set を複数回呼んだとき、【最後の値を勝たせる】仕様のため。
    //    後ろから探して最初に見つかったものを採用し、break する。
    //
    //  構築の優先順位（大事）
    //    factory     : 構築関数があればそれで直接構築（コピー禁止型もOK）
    //    mover       : op->data からムーブ構築。元は「抜け殻」になる
    //    constructor : ムーブが無ければコピー構築
    //    memcpy      : どれも無ければ生バイトコピー
    //
    //    mover を最優先するのは、後で PendingOps::Clear が元データに
    //    destructor を呼んでも、RAII リソース（ハンドル等）が解放されないようにするため。
    // ============================================================================
    void ChunkManager::InsertNewEntity(
        Chunk& dstChunk,
        EntityID entityID,
        const std::vector<PendingOp*>& addOps)
    {
        // 1. 席を確保して、正しいインデックスを直接受け取る
        size_t newIndex = dstChunk.AddEntity(entityID);

        // 位置を登録
        size_t chunkIdx = GetChunkIndex(&dstChunk);
        UpdateEntityLocation(entityID, chunkIdx, newIndex);

        // 2. チャンクの全構成要素を初期化
        for (const TypeData& td : dstChunk.GetTypeDatas())
        {
            void* dstPtr = dstChunk.GetComponentPtrByType(td.id, newIndex);
            if (!dstPtr) continue;

            bool filled = false;

            // 3. PendingOp に該当する初期データがあるか探す
            // 最後にした AddComponent の値で上書きする仕様のため逆順ループ
            for (int i = (int)addOps.size() - 1; i >= 0; --i)
            {
                auto* op = addOps[i];
                if (op->type == td.id)
                {
                    // factory があれば直接構築 (Emplace)
                     // コピー禁止型もここで Move 構築される
                    if (op->factory) {
                        op->factory(dstPtr);
                    }
                    // 一時メモリからの移送はムーブを最優先する。
                    //    （元の構造を保ち、constructor の if だけを mover 優先に変更）
                    //    mover があれば op->data を「抜け殻」にしてムーブ構築。
                    //    これにより後段の PendingOps::Clear で元を destructor しても
                    //    RAII リソース（ModelInstanceHandle 等）が解放されない。
                    if (op->data && td.mover)
                    {
                        td.mover(dstPtr, op->data);       // ムーブ構築（元は無効化される）
                    }
                    else if (op->data && td.constructor)
                    {
                        td.constructor(dstPtr, op->data); // フォールバック: コピー構築
                    }
                    else if (op->data)
                    {
                        std::memcpy(dstPtr, op->data, td.typeSize);
                    }
                    filled = true;
                    break;


                    //  ----------------------------------------------------------------------------
                    //  型について:
                    //    PendingOp::data は非const の void* で、td.mover の (void* dst, void* src)
                    //    にそのまま渡せる。constructor 側は (void*, const void*) だが実体は非const。
                    //
                    //  チャンク間移動（MoveEntityBetweenChunks）は元から mover を優先している。
                    //  ムーブ専用型を壊すのは AddComponent 適用時（InsertNewEntity）の経路だけなので、
                    //  ここでも mover があればそちらを使う。
                    //============================================================================
                }
            }

            // 4. データが無ければデフォルト構築 or ゼロクリア
            if (!filled)
            {
                if (td.defaultConstructor)
                {
                    td.defaultConstructor(dstPtr);
                }
                else
                {
                    std::memset(dstPtr, 0, td.typeSize);
                }
            }
        }

        // 5. 生成完了の通知
        if (m_onEntityMove) {
            m_onEntityMove(entityID, &dstChunk, newIndex);
        }
    }

    // ============================================================================
    // ChunkManager::SearchEqualChunk  ─ 型構成が完全一致する箱を探す
    // ----------------------------------------------------------------------------
    //  一言で : 先頭から線形探索して、Archetype が == の Chunk の添字を返す。
    //  「空きがあるか」は見ないので、満員の箱も返る点に注意。
    //  見つからなければ InvalidIndex。
    // ============================================================================
    size_t ChunkManager::SearchEqualChunk(const Archetype& a) const
    {
        for (size_t i = 0; i < m_chunks.size(); ++i)
        {
            if (m_chunks[i]->GetArchetype() == a)
                return i;
        }
        return InvalidIndex;
    }

    // ============================================================================
    // ChunkManager::UpdateEntityLocation  ─ 住所録に「今どこにいるか」を書き込む
    // ----------------------------------------------------------------------------
    //  一言で : m_entityLocationMap[index] = (Chunk番号, 席番号) を書くだけ。
    //  index が配列サイズを超えていたら resize して伸ばす。
    //  席が動くたび（生成/引っ越し/穴詰め）に必ず呼ばれる。ここが漏れると
    //  SearchEntityIn が古い場所を返してバグる。
    // ============================================================================
    void ChunkManager::UpdateEntityLocation(EntityID entity, size_t chunkIdx, size_t entityIdx)
    {
        size_t index = GetPureIndex(entity);

        if (index >= m_entityLocationMap.size()) {
            m_entityLocationMap.resize(index + 1);
        }

        m_entityLocationMap[index] = EntityLocation(chunkIdx, entityIdx);
    }

    // ============================================================================
    // ChunkManager::RemoveEntityLocation  ─ 住所録から抹消する
    // ----------------------------------------------------------------------------
    //  一言で : erase せず InvalidIndex で上書きして「墓石」を残す。
    //
    //  なぜ erase しないのか
    //    m_entityLocationMap は「index を配列の添字」にして引く設計。
    //    途中の要素を erase すると後ろが全部前にずれて、
    //    他の全エンティティの住所が壊れてしまう。
    //    だから穴は開けたまま、無効値で塗り潰す。
    // ============================================================================
    void ChunkManager::RemoveEntityLocation(EntityID entity)
    {
        size_t index = GetPureIndex(entity);

        if (index < m_entityLocationMap.size()) {
            // erase ではなく無効値で上書き（再利用時の整合性のため）
            m_entityLocationMap[index] = EntityLocation(InvalidIndex, InvalidIndex);
        }
    }

    // ============================================================================
    // ChunkManager::GetChunkIndex  ─ Chunk ポインタ → m_chunks の添字
    // ----------------------------------------------------------------------------
    //  一言で : ポインタが一致する Chunk を先頭から探すだけ。
    //  注意   : 【線形探索】なので Chunk が増えると遅くなる（既知の弱点）。
    //           引っ越しが多いフレームでは O(移動数 × Chunk数) になる。
    // ============================================================================
    size_t ChunkManager::GetChunkIndex(const Chunk* chunk) const
    {
        for (size_t i = 0; i < m_chunks.size(); ++i) {
            if (m_chunks[i].get() == chunk) {
                return i;
            }
        }
        return InvalidIndex;
    }

    // ============================================================================
    // ChunkManager::CalculateChunkCapacity  ─ 1つの箱に何体入るかを決める
    // ----------------------------------------------------------------------------
    //  一言で : 16384バイト ÷ 1体あたりのバイト数。
    //
    //  実験1の「定員682体」はここで出た値
    //    Position(12) + Velocity(12) = 24 バイト
    //    16384 / 24 = 682
    //
    //  なぜ 16KB 固定なのか
    //    CPU の L1/L2 キャッシュに収まりやすいサイズだから。
    //    箱ごとキャッシュに載れば、ループが劇的に速くなる。
    //
    //  例外処理
    //    stride == 0（タグ型のみでサイズ0）→ 割り算できないので 1024 を返す
    //    1体が16KBを超える            → 0 になってしまうので最低 1 を返す
    // ============================================================================
    size_t ChunkManager::CalculateChunkCapacity(const Archetype& archetype) const
    {
        size_t stride = 0;
        for (const auto& t : archetype) {
            stride += t.typeSize;
        }

        // 例外1: タグのみ（サイズ 0）
        if (stride == 0) return 1024;

        // 例外2: 1 体が大きすぎて 16KB に収まらない場合、最低 1 体は確保
        size_t capacity = CHUNK_SIZE_BYTES / stride;
        return capacity > 0 ? capacity : 1;
    }

    // ================================================================
    // Archetype 遷移（既存メソッド・挙動は元のまま）
    // ================================================================
    // ============================================================================
    // ChunkManager::MoveEntityBetweenChunks  ─ アーキタイプ遷移の本体（引っ越し）
    // ----------------------------------------------------------------------------
    //  一言で : コンポーネント構成が変わったエンティティを、別の箱へ引っ越させる。
    //
    //  なぜ引っ越しが必要か
    //    Chunk は「同じ型構成のエンティティだけ」を集める箱。
    //    Health を1つ足しただけで型構成が変わるので、元の箱には居られなくなる。
    //
    //  引数
    //    srcChunk/srcIndex : 今いる箱と席番号
    //    dstChunk          : 引っ越し先（GetOrCreateAvailableChunk が用意済み）
    //    addOps            : 新しく足す型の初期値
    //    removeTypeIDs     : 外す型の一覧
    //
    //  流れ
    //    1. 引っ越し先に席を確保し、住所録を【先に】更新する
    //    2. 両者の Archetype から TypeID→添字 の逆引きマップを作る
    //    3. 引っ越し先の各列を、次の3パターンで埋める
    //         パターンA: 元の箱にもある型 → mover でデータを運ぶ（値が保持される）
    //         パターンB: 新しく足された型 → addOps の初期値で構築
    //         パターンC: どちらでもない   → ゼロクリア
    //    4. 元の箱の全コンポーネントに destructor を呼ぶ
    //    5. m_onEntityMove で引っ越し完了を通知
    //    6. 元の席に削除マークを付ける（実際に詰まるのは CompactHoles）
    //
    //  戻り値 : 引っ越せたら true。既に破棄済みだった場合は false。
    //
    //  実験4で追っているのがこの関数。
    //  Position の値が引っ越し後も (7,8,9) のままなのはパターンAのおかげ。
    // ============================================================================
    bool ChunkManager::MoveEntityBetweenChunks(
        Chunk& srcChunk,
        size_t srcIndex,
        Chunk& dstChunk,
        EntityID entity,
        const std::vector<PendingOp*>& addOps,
        const std::vector<TypeID>& removeTypeIDs)
    {
        // 既に削除されていれば移動不可
        if (srcChunk.IsEntityDestroyed(srcIndex)) {
            return false;
        }

        // 1. 宛先 Chunk に席を確保
        dstChunk.InsertEntity(entity);
        size_t dstIndex = dstChunk.GetEntityCount() - 1;

        // 新しい住所をマスターマップに登録（重要）
        size_t dstChunkIdx = GetChunkIndex(&dstChunk);
        UpdateEntityLocation(entity, dstChunkIdx, dstIndex);

        const Archetype& srcArch = srcChunk.GetArchetype();
        const Archetype& dstArch = dstChunk.GetArchetype();

        // TypeID → インデックスの逆引きマップを作成
        auto BuildIndexMap = [](const Archetype& a) {
            std::unordered_map<TypeID, size_t> m;
            for (size_t i = 0; i < a.size(); ++i) m[a[i].id] = i;
            return m;
        };
        auto srcMap = BuildIndexMap(srcArch);
        auto dstMap = BuildIndexMap(dstArch);

        // 2. 宛先 Chunk の各列を順に埋める
        for (size_t di = 0; di < dstArch.size(); ++di)
        {
            const TypeData& tdDst = dstArch[di];
            void* dstPtr = dstChunk.GetComponentPtrByType(tdDst.id, dstIndex);

            // パターンA: 元の家からそのまま持ってくる
            auto itSrc = srcMap.find(tdDst.id);
            if (itSrc != srcMap.end())
            {
                void* srcPtr = srcChunk.GetComponentPtrByType(tdDst.id, srcIndex);

                if (tdDst.mover) {
                    tdDst.mover(dstPtr, srcPtr);
                }
                else if (tdDst.constructor) {
                    tdDst.constructor(dstPtr, srcPtr);
                }
                else {
                    std::memcpy(dstPtr, srcPtr, tdDst.typeSize);
                }
            }
            // パターンB: 新規追加された荷物
            else
            {
                bool filled = false;
                for (PendingOp* op : addOps) {
                    if (op->type == tdDst.id) {
                        if (op->factory) {
                            op->factory(dstPtr);
                            filled = true;
                            break;
                        }
                        else if (op->data) {
                            if (tdDst.constructor)
                                tdDst.constructor(dstPtr, op->data);
                            else
                                std::memcpy(dstPtr, op->data, tdDst.typeSize);
                            filled = true;
                            break;
                        }
                    }
                }
                // パターンC: 何も指定がなければ 0 クリア
                if (!filled)
                {
                    std::memset(dstPtr, 0, tdDst.typeSize);
                }
            }
        }

        // 元 Chunk のデータを全て破棄
        for (size_t si = 0; si < srcArch.size(); ++si)
        {
            const TypeData& tdSrc = srcArch[si];
            void* srcPtr = srcChunk.GetComponentPtrByType(tdSrc.id, srcIndex);
            tdSrc.destructor.Destruct(srcPtr);
        }

        // 引っ越し完了を通知
        if (m_onEntityMove) {
            m_onEntityMove(entity, &dstChunk, dstIndex);
        }

        // 元 Chunk のエンティティを削除マーク
        srcChunk.MarkDestroyEntity(srcIndex);

        return true;
    }


    // ================================================================
    // ApplyPendingOperations - 分割版オーケストレータ
    // ================================================================
    //
    // 設計方針:
    //   かつて 280 行あった巨大関数を以下の責務に分解しています。
    //   挙動は完全に元と同一を保証しています。
    //
    //   ApplyPendingOperations (本関数)
    //     ├─ GroupOpsByEntity()
    //     ├─ for each entity:
    //     │   └─ ProcessEntityOps()
    //     │       ├─ HasDestroyOp()
    //     │       ├─ ApplyDestroyOp()
    //     │       ├─ DetermineNextArchetype()
    //     │       ├─ CalculateArchetypeDiff()
    //     │       ├─ UpdateExistingComponents()
    //     │       └─ ApplyPatchOps()
    //     └─ CompactAllChunks()
    // ================================================================

    // ============================================================================
    // ChunkManager::ApplyPendingOperations  ─ 溜まった予約を全部実行する総司令塔
    // ----------------------------------------------------------------------------
    //  一言で : 予約リストを実際のメモリ操作に変換する。ここが構造変更の心臓部。
    //
    //  呼ばれる場所
    //    World::ScrutinyAndApply の1箇所だけ（＝フレームの境界でしか呼ばれない）。
    //
    //  戻り値
    //    実際に破棄された EntityID のリスト。
    //    World 側がこれを受け取って RecycleEntityID（世代++）に回す。
    //
    //  流れ
    //    Phase 0 : unique_lock で排他（この間 System は動けない）
    //    Phase 0': 予約が空なら即 return（何もしないのが一番速い）
    //    Phase 1 : GroupOpsByEntity()   予約を Entity ごとに束ね直す
    //    Phase 2 : Entity ごとに ProcessEntityOps() を呼ぶ
    //    Phase 3 : CompactAllChunks()   全 Chunk の穴をまとめて詰める
    //
    //  なぜ穴詰めを最後にまとめるのか
    //    1体消すたびに詰め直すと無駄が多いうえ、席がずれるたびに
    //    住所録を更新し直すことになるため。
    //
    //  ＜デバッガの道しるべ＞
    //    Phase1 → Phase2 の順に F11 で入っていくと流れが掴める。
    //    実際の処理の本体は Phase2 の ProcessEntityOps。
    // ============================================================================
    std::vector<EntityID> ChunkManager::ApplyPendingOperations(PendingOps& pendingOps)
    {
        // 排他ロックを取得（この間 System は待機）
        std::unique_lock<std::shared_mutex> lock(m_structureMutex);

        std::vector<EntityID> destroyedEntities;
        if (pendingOps.Ops().empty()) return destroyedEntities;

        // Phase 1: PendingOps を Entity ごとに束ねる
        //
        //  ＜なぜ auto なのか＞
        //    実際の型は std::unordered_map<EntityID, std::vector<PendingOp*>>。
        //    書き下すと長いので auto にしているだけで、意味は一切変わらない。
        //    デバッガで型を確かめたいときは、変数にマウスを乗せるか
        //    「自動変数」ウィンドウを見れば実際の型が表示される。
        //
        //  ＜なぜ束ねる必要があるのか（こちらが本質）＞
        //    PendingOps は「予約された順」に一列に並んでいるだけなので、
        //    同じ Entity への操作があちこちに散らばっている。
        //    先に Entity 単位へまとめておくと「この Entity の最終的な型構成は何か」を
        //    一度で計算でき、箱の引っ越しを1回で済ませられる。
        //    束ねないと、コンポーネントを足すたびに引っ越しが発生してしまう。
        auto opsByEntity = GroupOpsByEntity(pendingOps);

        // Phase 2: 各 Entity の操作を処理
        //
        //  ＜unordered_set とは＞
        //    「重複を許さない入れ物」で、中身の有無を高速に判定できるもの。
        //      std::vector        : 順番あり / 重複OK  / 存在確認は線形探索 O(n)
        //      std::unordered_set : 順番なし / 重複なし / 存在確認はハッシュで O(1)
        //    map との違いは「キーと値のペア」か「キーだけ」かの一点。
        //
        //  ＜ここで使う理由＞
        //    alreadyDestroyed は「この Entity はもう破棄済みか？」を何度も
        //    問い合わせる用途（ProcessEntityOps 内の find がそれ）。
        //    vector だと毎回全走査になるが、set なら一発で判定できる。
        std::unordered_set<EntityID> alreadyDestroyed;

        // 構造化束縛：map から「キー(EntityID)」と「値(予約リスト)」を同時に取り出す書き方
        for (auto& [entityID, ops] : opsByEntity)
        {
            ProcessEntityOps(entityID, ops, alreadyDestroyed, destroyedEntities);
        }

        // Phase 3: 全 Chunk の穴詰め
        CompactAllChunks();

        return destroyedEntities;
    }


    // ----------------------------------------------------------------
    // Phase 1: PendingOps を Entity ID ごとにグループ化
    // ----------------------------------------------------------------
    // ============================================================================
    // ChunkManager::GroupOpsByEntity  ─ バラバラの予約を Entity ごとに束ね直す
    // ----------------------------------------------------------------------------
    //  一言で : 一列に並んだ予約を、EntityID をキーにした map へ詰め替えるだけ。
    //
    //   入力（予約された順に一列）        出力（Entity ごとにまとまる）
    //     Entity10 AddTransform             Entity10 → [AddTransform, AddModel]
    //     Entity20 RemovePhysics       →    Entity20 → [RemovePhysics]
    //     Entity10 AddModel                 Entity30 → [Spawn]
    //     Entity30 Spawn
    //
    //  値が PendingOp*（ポインタ）なのは、実体をコピーせず参照するだけで済むから。
    //  元の pendingOps が生きている間だけ有効。
    // ============================================================================
    std::unordered_map<EntityID, std::vector<PendingOp*>>
    ChunkManager::GroupOpsByEntity(PendingOps& pendingOps)
    {
        std::unordered_map<EntityID, std::vector<PendingOp*>> opsByEntity;
        for (auto& op : pendingOps.Ops()) {
            opsByEntity[op.entity].push_back(&op);
        }
        return opsByEntity;
    }


    // ----------------------------------------------------------------
    // Phase 2: 1 Entity 分の処理オーケストレータ
    // ----------------------------------------------------------------
    // ============================================================================
    // ChunkManager::ProcessEntityOps  ─ 1体分の予約をまとめて処理する
    // ----------------------------------------------------------------------------
    //  一言で : 最重要のオーケストレータ。この Entity の「最終形」を決めて確定させる。
    //
    //  引数
    //    ops               : この Entity への予約すべて
    //    alreadyDestroyed  : もう破棄した Entity の集合（多重破棄の防止）
    //    destroyedEntities : 実際に破棄した ID の蓄積先（World へ返る）
    //
    //  6ステップの流れ
    //    1. Destroy が混ざっていたら最優先で破棄して即 return
    //       （消えるものに他の操作をしても無意味なので）
    //    2. 今どこにいるか調べ、既に死んでいないか二重チェック
    //    3. DetermineNextArchetype : 次の型構成の「出発点」を決める
    //    4. CalculateArchetypeDiff : Add/Remove を反映して「最終形」にする
    //    5. 3分岐で確定
    //         Spawn なら         → InsertNewEntity（新規に席を作る）
    //         型構成が変わった   → MoveEntityBetweenChunks（引っ越し）
    //         型構成が同じ       → UpdateExistingComponents（値の上書きだけ）
    //    6. ApplyPatchOps : 部分書き換えを最後に適用
    //
    //  ステップ5の3分岐がこの ECS の要
    //    「型構成が変わったかどうか」で引っ越すか否かが決まる。
    //    同じ型に値を入れ直すだけなら引っ越しは起きない（速い）。
    // ============================================================================
    void ChunkManager::ProcessEntityOps(
        EntityID entityID,
        const std::vector<PendingOp*>& ops,
        std::unordered_set<EntityID>& alreadyDestroyed,
        std::vector<EntityID>& destroyedEntities)
    {
        // -------------------------------------------------------
        // ステップ 1: 破棄(Destroy) の優先チェック
        // -------------------------------------------------------
        if (HasDestroyOp(ops)) {
            ApplyDestroyOp(entityID, alreadyDestroyed, destroyedEntities);
            return; // 破棄が確定したら他の操作は全て無視
        }

        // -------------------------------------------------------
        // ステップ 2: 既存状態の確認と生存チェック
        // -------------------------------------------------------
        size_t srcIndex = 0;
        // EntityIDを入れるとどのChunkのIndexにあるかを出す関数
        size_t chunkIdx = SearchEntityIn(entityID, &srcIndex);
        // Chunkがあるなら実際に取得する
        Chunk* srcChunk = (chunkIdx != InvalidIndex) ? m_chunks[chunkIdx].get() : nullptr;

        // 削除済みエンティティへの操作をスキップ
        // ? なんでここスキップする必要あるの？最初に破棄チェックでreturn返しているのに
        if (alreadyDestroyed.find(entityID) != alreadyDestroyed.end()) {
            return;
        }

        // 既に削除マークがついている場合もスキップ
        // ? ここもどういった時に削除マークがつくのかどういった処理なのかしりたい
        if (srcChunk && srcChunk->IsEntityDestroyed(srcIndex)) {
            return;
        }

        // -------------------------------------------------------
        // ステップ 3: 次の Archetype のベースを決定
        // -------------------------------------------------------
        Archetype nextArch;
        bool isNewSpawn = false;
        // 変更する予約のArchetypeを決める　Spawnならあたらしく　既存ならそのままなど
        if (!DetermineNextArchetype(ops, srcChunk, nextArch, isNewSpawn)) {
            return; // 存在しないし Spawn でもないなら何もしない
        }

        // -------------------------------------------------------
        // ステップ 4: Add/Remove 差分の計算と nextArch への反映
        // -------------------------------------------------------
        std::vector<PendingOp*> addOps;
        std::vector<TypeID> removeIDs;
        CalculateArchetypeDiff(ops, nextArch, addOps, removeIDs);

        // -------------------------------------------------------
        // ステップ 5: 最終確定処理
        // -------------------------------------------------------
        // 新しいSpawnなら
        if (isNewSpawn) {
            // Spawn: 直接 nextArch を持つ Chunk へ挿入
            // 指定したArchetypeの中から定員が空いているものを探す、なかったらChunk作成する
            Chunk& dstChunk = GetOrCreateAvailableChunk(nextArch);
            // 見つけたらそのチャンクにEntityを入れてComponentなどのデータの初期値もセットする
            InsertNewEntity(dstChunk, entityID, addOps);
        }
        // 元のArchetypeと比べて変更されていたら
        else if (!(nextArch == srcChunk->GetArchetype())) {
            // Archetype 変更: 別 Chunk へ引っ越し
            Chunk& dstChunk = GetOrCreateAvailableChunk(nextArch);

            // MoveEntityBetweenChunks 前に再度削除チェック
            if (!srcChunk->IsEntityDestroyed(srcIndex)) {
                MoveEntityBetweenChunks(*srcChunk, srcIndex, dstChunk, entityID, addOps, removeIDs);
            }
        }
        else {
            // Archetype が変わらない（既存 Component を上書き）
            UpdateExistingComponents(srcChunk, srcIndex, ops);
        }

        // -------------------------------------------------------
        // ステップ 6: Patch（部分修正）の適用
        // -------------------------------------------------------
        // データが引っ越しを終わった後に一部のデータを書き換える
        // Patchを適応する。
        ApplyPatchOps(entityID, ops);
    }


    // ----------------------------------------------------------------
    // ステップ 1 補助: Destroy マーカー検出
    // ----------------------------------------------------------------
    // ============================================================================
    // ChunkManager::HasDestroyOp  ─ 予約に Destroy が混ざっているか調べるだけ
    // ----------------------------------------------------------------------------
    //  一言で : ops を先頭から見て kind == Destroy が1つでもあれば true。
    //  Destroy を最優先で判定するために、他の処理より先に呼ばれる。
    // ============================================================================
    bool ChunkManager::HasDestroyOp(const std::vector<PendingOp*>& ops) const
    {
        for (auto* op : ops) {
            if (op->kind == PendingOpKind::Destroy) {
                return true;
            }
        }
        return false;
    }


    // ----------------------------------------------------------------
    // ステップ 1 補助: Destroy 確定処理
    // ----------------------------------------------------------------
    // ============================================================================
    // ChunkManager::ApplyDestroyOp  ─ 破棄の確定処理
    // ----------------------------------------------------------------------------
    //  一言で : 全コンポーネントの destructor を呼び、削除マークを付け、住所録から消す。
    //
    //  流れ
    //    1. alreadyDestroyed に居たらスキップ（多重破棄の防止）
    //    2. SearchEntityIn で存在確認。居なければ何もしない
    //    3. 既に削除マークがあれば、記録だけして return
    //    4. Archetype を回して全コンポーネントに destructor を呼ぶ
    //       （生メモリなので型ごとの関数ポインタ経由で破棄する＝型消去）
    //    5. MarkDestroyEntity で席に印を付ける（穴を詰めるのは後）
    //    6. 住所録から抹消し、結果リストへ積む
    //
    //  ここで destructor を呼ぶのに MarkDestroyEntity が印を付けるだけなのは、
    //  「中身の破棄」と「席の詰め直し」を分離しているから。
    //  席を詰めるのはフレーム最後の CompactAllChunks の仕事。
    // ============================================================================
    void ChunkManager::ApplyDestroyOp(
        EntityID entityID,
        std::unordered_set<EntityID>& alreadyDestroyed,
        std::vector<EntityID>& destroyedEntities)
    {
        // 既に削除済みならスキップ
        if (alreadyDestroyed.find(entityID) != alreadyDestroyed.end()) {
            return;
        }

        // 既存エンティティの存在確認
        size_t entityIndex = 0;
        size_t chunkIdx = SearchEntityIn(entityID, &entityIndex);
        if (chunkIdx == InvalidIndex) return; // 存在しないなら何もしない

        Chunk* cptr = m_chunks[chunkIdx].get();

        // 既に削除マークがあれば二重処理を回避
        if (cptr->IsEntityDestroyed(entityIndex)) {
            alreadyDestroyed.insert(entityID);
            return;
        }

        // 全コンポーネントのデストラクタを順に呼び出す
        for (const TypeData& td : cptr->GetArchetype()) {
            // td.id 型の entityIndex 番目データの住所を取得
            void* ptr = cptr->GetComponentPtrByType(td.id, entityIndex);

            // その型専用のデストラクタで適切に破棄
            td.destructor.Destruct(ptr);
        }

        // 削除マーク
        cptr->MarkDestroyEntity(entityIndex);

        // 住所録から完全抹消
        RemoveEntityLocation(entityID);

        // 結果リストへ登録
        alreadyDestroyed.insert(entityID);
        destroyedEntities.push_back(entityID);
    }


    // ----------------------------------------------------------------
    // ステップ 3 補助: 次の Archetype を決定
    // ----------------------------------------------------------------
    // ============================================================================
    // ChunkManager::DetermineNextArchetype  ─ 次の型構成の「出発点」を決める
    // ----------------------------------------------------------------------------
    //  一言で : Add/Remove を適用する前の【ベースとなる型構成】を選ぶ。
    //
    //  判定
    //    ops に Spawn がある     → op.spawnArchetype を採用、isNewSpawn = true
    //    Spawn が無く既存がいる  → 今いる Chunk の Archetype を引き継ぐ
    //    どちらも無い            → false を返す（存在しないし Spawn でもない＝何もできない）
    //
    //  出力は参照引数で返す
    //    outArchetype  : 決まったベースの型構成
    //    outIsNewSpawn : 新規生成かどうか（ProcessEntityOps の3分岐で使う）
    //
    //  spawnArchetype が【値】で保持されている理由は
    //  World::RequestSpawnEntity のコメントを参照（FrameAllocator のダングリング対策）。
    // ============================================================================
    bool ChunkManager::DetermineNextArchetype(
        const std::vector<PendingOp*>& ops,
        Chunk* srcChunk,
        Archetype& outArchetype,
        bool& outIsNewSpawn) const
    {
        outIsNewSpawn = false;

        // ops の中に Spawn があるか探す
        for (auto* op : ops) {
            if (op->kind == PendingOpKind::Spawn) {
                // spawnArchetype は PendingOp の値メンバなので
                // FrameAllocator のライフタイムに無関係で常に安全
                outArchetype = op->spawnArchetype;
                outIsNewSpawn = true;
                return true;
            }
        }

 
        if (!srcChunk) {
            // 存在しないし Spawn でもない → 何もできない
            return false;
        }
        // Spawn でないなら、既存 Chunk の Archetype を引き継ぐ
        outArchetype = srcChunk->GetArchetype();
        return true;
    }


    // ----------------------------------------------------------------
    // ステップ 4 補助: Add/Remove 差分計算
    // ----------------------------------------------------------------
    // ============================================================================
    // ChunkManager::CalculateArchetypeDiff  ─ Add/Remove を反映して「最終形」を作る
    // ----------------------------------------------------------------------------
    //  一言で : ベースの型構成に予約を適用して、この Entity が最終的に持つ形にする。
    //
    //  引数
    //    nextArch     : [入出力] 入ってきたベースを、この関数が書き換えて最終形にする
    //    outAddOps    : 初期値として使う AddComponent 予約の抽出先
    //    outRemoveIDs : 外す型の TypeID の抽出先
    //
    //  AddComponent のとき
    //    ・outAddOps には【必ず】積む（既に型を持っていても値の上書きに使うため）
    //    ・型がまだ無ければ TypeData を組み立てて nextArch.Add()
    //      このとき destructor/constructor/assigner/mover の関数ポインタも
    //      一緒に詰める。これが無いと後で memcpy にフォールバックしてしまう
    //
    //  RemoveComponent のとき
    //    outRemoveIDs に積み、nextArch.Sub() で型を外す
    //
    //  Archetype は TypeID 昇順ソートを保つ決まりなので、
    //  Add/Sub は内部で二分探索して正しい位置に挿入・削除する。
    // ============================================================================
    void ChunkManager::CalculateArchetypeDiff(
        const std::vector<PendingOp*>& ops,
        Archetype& nextArch,
        std::vector<PendingOp*>& outAddOps,
        std::vector<TypeID>& outRemoveIDs) const
    {
        for (auto* op : ops) {
            if (op->kind == PendingOpKind::AddComponent) {
                // 「初期値データ」として addOps には必ず入れる
                outAddOps.push_back(op);

                // 既に Archetype に含まれていなければ追加
                bool has = false;
                // ? nextArchってvectorでもないのにfor文で回すってどういうこと
                for (auto& td : nextArch) {
                    if (td.id == op->type) { has = true; break; }
                }
                if (!has) {
                    // 関数ポインタを含めた完全な TypeData を構築して追加
                    // ? わからん
                    TypeData newType;
                    newType.id          = op->type;
                    newType.typeSize    = op->typeSize;
                    newType.destructor  = op->destructor;
                    newType.constructor = op->constructor;
                    newType.assigner    = op->assigner;
                    newType.mover       = op->mover;
                    newType.name        = op->name;

                    nextArch.Add(newType);
                }
            }
            else if (op->kind == PendingOpKind::RemoveComponent) {
                outRemoveIDs.push_back(op->type);
                nextArch.Sub(op->type);
            }
        }
    }


    // ----------------------------------------------------------------
    // ステップ 5 補助: 同一 Archetype 内のコンポーネント値更新
    // ----------------------------------------------------------------
    // ============================================================================
    // ChunkManager::UpdateExistingComponents  ─ 型構成が変わらないときの値の上書き
    // ----------------------------------------------------------------------------
    //  一言で : 引っ越し無しで、既にあるコンポーネントの中身だけを書き換える。
    //
    //  いつ呼ばれるか
    //    ProcessEntityOps のステップ5で「最終形 == 今の型構成」だったとき。
    //    例：既に Position を持っている Entity に、もう一度 Position を Set した場合。
    //
    //  2つのパターン
    //    factory がある : 上書き代入ができない型（ムーブ専用など）なので
    //                     「destructor で破棄 → factory で新規構築」の手順を踏む
    //    data がある    : assigner（代入演算子）で上書き。
    //                     assigner が無ければ memcpy で生バイトコピー
    //
    //  既に構築済みのメモリなので、ここで代入演算子を使うのが安全。
    //  （まだ構築されていない場所には代入できないので、新規時は constructor を使う）
    // ============================================================================
    void ChunkManager::UpdateExistingComponents(
        Chunk* srcChunk, size_t srcIndex,
        const std::vector<PendingOp*>& ops)
    {
        for (auto* op : ops) {
            // 値上書きなので AddComponent 以外は無視
            if (op->kind != PendingOpKind::AddComponent) continue;

            // チャンク内の番号を指定して、該当するコンポーネントのアドレスを取得する
            void* dstPtr = srcChunk->GetComponentPtrByType(op->type, srcIndex);
            // アドレスが無効、または factory と data の両方が無い場合はスキップ
            if (!dstPtr || (!op->factory && !op->data)) continue;

            const Archetype& arch = srcChunk->GetArchetype();
            // そういえばArchetypeをfor文で回すのってどういうこと？
            // って思ったけど、Archetypeはstd::vector<TypeData>のtypedefだからfor文で回せるのか
            for (const auto& td : arch) {
                if (td.id != op->type) continue;

                // パターン1: factory がある場合 (Move-Only 型など)
                //   上書き代入ができないため「破棄 → 新規構築」の手順を踏む
                if (op->factory) {
                    td.destructor.Destruct(dstPtr); // 既存を破棄
                    op->factory(dstPtr);            // 新規構築
                }
                // パターン2: data がある場合（従来のコピー可能型）
                else if (op->data) {
                    if (td.assigner) {
                        // 既に構築済みなので代入演算子が安全
                        td.assigner(dstPtr, op->data);
                    }
                    else {
                        std::memcpy(dstPtr, op->data, td.typeSize);
                    }
                }
                break;
            }
        }
    }


    // ----------------------------------------------------------------
    // ステップ 6 補助: Patch（部分書き換え）の適用
    // ----------------------------------------------------------------
    // ============================================================================
    // ChunkManager::ApplyPatchOps  ─ 部分書き換え（Patch）を最後に適用する
    // ----------------------------------------------------------------------------
    //  一言で : PatchComponent で登録したラムダを、確定した最終アドレスに対して呼ぶ。
    //
    //  なぜ一番最後なのか
    //    引っ越し（MoveEntityBetweenChunks）が起きるとアドレスが変わるため。
    //    先に patch すると、これから捨てられる古い場所を書いてしまう。
    //    だから移動が全部終わってから SearchEntityIn で【最終的な場所】を引き直す。
    //
    //  op->patcher は void* を受け取るラムダ。
    //  World::PatchComponent が、型安全なラムダをこの形に包んで積んでいる。
    // ============================================================================
    void ChunkManager::ApplyPatchOps(
        EntityID entityID,
        const std::vector<PendingOp*>& ops)
    {
        // 移動・生成が完了した「最終的な場所」を取得
        size_t finalIndex = 0;
        // 戻り値にChunkManager内のChunkのID、引数にそのChunk内のIndexを返す
        size_t finalChunkIdx = SearchEntityIn(entityID, &finalIndex);
        if (finalChunkIdx == InvalidIndex) return;

        Chunk* finalChunk = m_chunks[finalChunkIdx].get();

        for (auto* op : ops) {
            if (op->kind != PendingOpKind::PatchComponent) continue;

            // 指定されたチャンクの指定されたコンポーネントのアドレスを取得
            void* ptr = finalChunk->GetComponentPtrByType(op->type, finalIndex);
            // アドレスが有効で、かつ patcher が登録されていれば呼び出す
            if (ptr && op->patcher) {
                op->patcher(ptr);
            }
        }
    }


    // ----------------------------------------------------------------
    // Phase 3: 全 Chunk の穴詰め
    // ----------------------------------------------------------------
    // ============================================================================
    // ChunkManager::CompactAllChunks  ─ 全 Chunk の穴をまとめて詰める（Phase 3）
    // ----------------------------------------------------------------------------
    //  一言で : 全部の箱に CompactHoles を呼び、席がずれた分だけ住所録を直す。
    //
    //  渡しているラムダが重要
    //    CompactHoles は要素を動かすたびにこのコールバックを呼ぶ。
    //    その中で
    //      ・UpdateEntityLocation で内部の住所録を更新
    //      ・m_onEntityMove で World 側のキャッシュにも通知
    //    の両方をやっている。
    //
    //  これを忘れると、詰めた後に SearchEntityIn が古い席を返して
    //  「別のエンティティのデータが見える」という嫌なバグになる。
    //
    //  実験3で最後尾の entity が穴の位置へ移動して見えるのは、この関数の結果。
    // ============================================================================
    void ChunkManager::CompactAllChunks()
    {
        for (auto& c : m_chunks) {
            c->CompactHoles([this](EntityID entity, Chunk* chunk, size_t newIndex) {
                // 位置キャッシュを更新
                size_t chunkIdx = GetChunkIndex(chunk);
                UpdateEntityLocation(entity, chunkIdx, newIndex);

                // 外部コールバック（World 側のキャッシュ更新）も呼ぶ
                if (m_onEntityMove) {
                    m_onEntityMove(entity, chunk, newIndex);
                }
            });
        }
    }

} // namespace CCL::ECS
