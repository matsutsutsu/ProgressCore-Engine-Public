#include "CCL_Chunk.h"
#include <algorithm>
#include <cstring>
#include <cassert>


namespace CCL::ECS
{
    using namespace CCL::ECS;

    // ヘルパー：指定したアライメント境界へサイズを切り上げる
    inline size_t AlignUp(size_t size, size_t alignment)
    {
        return (size + alignment - 1) & ~(alignment - 1);
    }

    // ============================================================================
    // デストラクタ
    //   一言で : 箱が壊れるときに中身も全部片付ける。
    //   中身   : Finalize() を呼ぶだけ。二重解放の防止は Finalize 側が持つ。
    // ============================================================================
    Chunk::~Chunk()
    {
        Finalize();
    }


    // ============================================================================
    // Chunk::Create  ─ 箱を作ってメモリを一括確保する
    // ----------------------------------------------------------------------------
    //  一言で
    //    「この型構成の箱」を1つ作り、定員いっぱい分のメモリを最初に1回だけ確保する。
    //
    //  引数
    //    archetype             : この箱が入れる型の一覧（何のコンポーネントを持つか）
    //    initialEntityCapacity : 定員。ChunkManager::CalculateChunkCapacity() が
    //                            16KB ÷ 1体のバイト数 で計算して渡してくる
    //
    //  呼ばれる場所
    //    ChunkManager::CreateChunk / AddEntity / GetOrCreateAvailableChunk から。
    //    「その型構成の箱がまだ無い」ときに一度だけ呼ばれる。
    //
    //  やること
    //    1. 全型のサイズを足して _entityStride（1体あたりのバイト数）を出す
    //    2. 各コンポーネント配列の開始位置を16バイト境界に揃えて _componentOffsets に記録
    //    3. 合計バイト数で _memoryBlock.resize() を「1回だけ」呼ぶ
    //
    //  ★ここが一番の勘所★
    //    各コンポーネント配列の幅は「今いる数」ではなく【定員】で決まる。
    //    定員682体なら、8体しか居なくても Position 配列は682体分の幅を持つ。
    //    そのため Velocity 配列は 8192 バイトも先から始まる（実験1で見る値）。
    //    この設計のおかげで、以降エンティティを何体足してもメモリ確保は二度と起きない。
    // ============================================================================
    void Chunk::Create(const Archetype& archetype, size_t initialEntityCapacity)
    {

        _archetype = archetype;
		_capacity = initialEntityCapacity;
        _entityStride = 0;
        for (const TypeData& t : _archetype) _entityStride += t.typeSize;

        _ownerEntities.assign(initialEntityCapacity, InvalidEntityID);

        // 単一メモリブロックの一括確保ロジック
        size_t compCount = _archetype.size();
        _componentOffsets.resize(compCount); // 白線を引く準備

        size_t       currentOffset = 0; // 現在の白線の位置（0バイト目からスタート）
        const size_t ALIGNMENT     = 16; // SIMD命令等を見据えた16バイトアライメント

        for (size_t i = 0; i < compCount; ++i)
        {
            // 1. CPUが計算しやすいように、白線の位置を「16の倍数」にキリよく調整する
            currentOffset = AlignUp(currentOffset, ALIGNMENT);

            // 2. 「このコンポーネントのエリアはここから始まる」とメモ帳に記録
            _componentOffsets[i] = currentOffset;

            // 3. 次の白線を引くために、このコンポーネントが占領するサイズ分だけ位置を進める
            // （例：Position(12byte)が100人分なら、1200バイト先へ）
            currentOffset += _archetype[i].typeSize * _capacity;
        }

        // 4. 計算し尽くした「総面積(currentOffset)」で、1回だけ巨大な体育館を借りる！
        // 計算し尽くした「必要な総バイト数」で、1回だけメモリをドカンと確保する
        _memoryBlock.resize(currentOffset);

        _entityCount = 0;
        _destroyedEntities.clear();
    }

    // ============================================================================
    // Chunk::CreateNull  ─ 型を1つも持たない空の箱を作る
    // ----------------------------------------------------------------------------
    //  一言で : Archetype が空っぽの箱。実データ領域は確保しない。
    //  違い   : Create と違い _memoryBlock も _componentOffsets も空のまま。
    //           名簿(_ownerEntities)だけは定員分用意する。
    //  用途   : 「まだ何も持っていないエンティティ」の置き場所として使う想定。
    // ============================================================================
    void Chunk::CreateNull(size_t initialEntityCapacity)
    {
        _archetype = Archetype();
        _capacity = initialEntityCapacity;
        _entityStride = 0;
        _ownerEntities.assign(initialEntityCapacity, InvalidEntityID);


        _componentOffsets.clear();
        _memoryBlock.clear();

        _entityCount = 0;
        _destroyedEntities.clear();
    }

    // ============================================================================
    // Chunk::Finalize  ─ 箱を畳む
    // ----------------------------------------------------------------------------
    //  一言で : 中の全員のデストラクタを呼んでから、メモリを解放する。
    //
    //  呼ばれる場所
    //    ~Chunk()（箱が壊れるとき）と ChunkManager::FinalizeAll()（World の終了時）。
    //
    //  やること
    //    1. 生きている要素だけに、型ごとの destructor を呼ぶ
    //       （_memoryBlock は生メモリなので、型が分からないと正しく破棄できない。
    //         そのために TypeData が destructor の関数ポインタを持っている＝型消去）
    //    2. 全部のコンテナを clear してメモリを返す
    //
    //  注意
    //    先頭の early return は二重解放の防止。_componentOffsets が空＝
    //    「まだ Create していない」か「もう Finalize 済み」なので何もしない。
    // ============================================================================
    void Chunk::Finalize()
    {
        // 既にFinalize済み（または初期化前）なら何もしない（二重解放・クラッシュ防止）
        if (_componentOffsets.empty()) return;

        // 1. 体育館の中にいる全員のデストラクタ（お掃除関数）を呼ぶ
        for (size_t ci = 0; ci < _archetype.size(); ++ci) {

            size_t sz         = _archetype[ci].typeSize;
            size_t baseOffset = _componentOffsets[ci]; // 白線の位置

            for (size_t i = 0; i < _entityCount; ++i) {
                if (!IsEntityDestroyed(i)) {
                    // 体育館の正確な場所を特定して、お掃除を実行
                    void *ptr = _memoryBlock.data() + baseOffset + (i * sz);
                    _archetype[ci].destructor.Destruct(ptr);
                }
            }
        }

        // 2. 最後に体育館をドカンと一括で返却する
        _memoryBlock.clear();
        _componentOffsets.clear(); // 白線の情報を消去（旧 _components.clear() の代わり）
        _ownerEntities.clear();    // 座席表をクリア
        _destroyedEntities.clear();
        _entityCount = 0;

    }


    // ============================================================================
    // Chunk::InsertEntity  ─ 席を取ってゼロ初期化する
    // ----------------------------------------------------------------------------
    //  一言で : AddEntity で席を確保し、その席を全部ゼロで塗ってから先頭を返す。
    //
    //  戻り値 : 確保した席の「先頭コンポーネント」のポインタ（型は不明なので void*）
    //
    //  AddEntity との違い
    //    AddEntity : 名簿に書いてカウンタを増やすだけ（中身は前の住人のゴミが残る）
    //    InsertEntity : それに加えて memset でゼロクリアする
    //
    //  呼ばれる場所
    //    ChunkManager::AddEntity と MoveEntityBetweenChunks（引っ越し先の席確保）。
    // ============================================================================
    void* Chunk::InsertEntity(EntityID entity)
    {
        // 1. AddEntity内で「満員でないか」だけをチェックし、座席番号をもらう
        size_t writeIndex = AddEntity(entity);

        // 2. 各コンポーネントのエリアに行き、そこを「ゼロ」で水拭き（初期化）する
        for (size_t ci = 0; ci < _archetype.size(); ++ci) {
            void *dst = GetComponentPtr(ci, writeIndex); // 体育館の正確な場所
            std::memset(dst, 0, _archetype[ci].typeSize);
        }
        return GetComponentPtr(0, writeIndex);
    }



    // ============================================================================
    // Chunk::RemoveEntity  ─ その席のデストラクタを呼んで名簿を無効化する
    // ----------------------------------------------------------------------------
    //  一言で : 中身を正しく破棄して「空席」にする。ただしメモリの穴は詰めない。
    //
    //  MarkDestroyEntity との違い（ここ紛らわしいので注意）
    //    RemoveEntity      : destructor を「呼ぶ」。_destroyedEntities には積まない
    //    MarkDestroyEntity : destructor を「呼ばない」。印を付けるだけ
    //
    //  穴を詰めるのは後の CompactHoles。
    //  破棄が2段階なのは、ループ中に席がずれると持っているポインタが壊れるから。
    // ============================================================================
    void Chunk::RemoveEntity(size_t entityIndex)
    {
        // 不正なインデックスなら何もしない
        if (entityIndex >= _entityCount) return;

        // このエンティティが持つ全てのコンポーネントの「お掃除関数」を実行
        for (size_t ci = 0; ci < _archetype.size(); ++ci) {
            // 新設計：専用関数を使って、体育館の床の正確なポインタを一発で取得
            void *ptr = GetComponentPtr(ci, entityIndex);

            // お掃除実行（中身のメモリ解放など）
            _archetype[ci].destructor.Destruct(ptr);
        }

        // 持ち主の座席を「無効」としてマーク。
        // ※実際にメモリの穴を詰める(Compact)のは、あとで一括でやります。
        _ownerEntities[entityIndex] = InvalidEntityID;
    }

    // ============================================================================
    // Chunk::MarkDestroyEntity  ─ 「この席は後で消す」と印を付けるだけ
    // ----------------------------------------------------------------------------
    //  一言で : デストラクタは呼ばない。名簿を無効にして席番号をゴミ箱リストへ積む。
    //
    //  呼ばれる場所
    //    ChunkManager::ApplyDestroyOp（destructor は呼び終わった後にここへ来る）
    //    ChunkManager::MoveEntityBetweenChunks（引っ越し元の席を空ける）
    //
    //  ★破棄が2段階になっている理由★
    //    System が ForEach でループしている最中に席が前に詰まると、
    //    ループが握っているポインタやインデックスが壊れる。
    //    そこで「印を付けるだけ」に留め、実際に詰めるのは
    //    フレーム境界の CompactAllChunks → CompactHoles でまとめて行う。
    // ============================================================================
    void Chunk::MarkDestroyEntity(size_t entityIndex)
    {

        if (entityIndex >= _entityCount) return;

        EntityID id = _ownerEntities[entityIndex];

        // mark owner slot invalid; actual data will be compacted later
        _ownerEntities[entityIndex] = InvalidEntityID;
        _destroyedEntities.push_back(entityIndex);
    }



    // ============================================================================
    // Chunk::CompactHoles  ─ 穴を詰めて生存要素を前へ寄せる
    // ----------------------------------------------------------------------------
    //  一言で : 印の付いた空席を飛ばし、生きている要素を前から順に詰め直す。
    //
    //  引数
    //    onMoveCallback : 要素が動くたびに「この Entity がここへ移った」と外へ通知する関数。
    //                     ChunkManager::CompactAllChunks がここで住所録を更新している。
    //                     これが無いと、詰めた後に SearchEntityIn が古い席を返してしまう。
    //
    //  アルゴリズム（2本指の古典パターン）
    //    read  = 走査位置（前から順に全部見ていく指）
    //    write = 詰め先位置（生きている要素を置く指）
    //
    //    空席なら          → read だけ進める（write は据え置き＝穴を飛ばす）
    //    生きていて穴が有る → read の中身を write へ移動し、両方進める
    //    生きていて穴が無い → 何もせず両方進める（read == write なので移動不要）
    //
    //  移動のしかた
    //    型の mover（ムーブ）があればそれを使い、無ければ memmove で生コピー。
    //    ムーブした後の src は「抜け殻」になるので destructor を呼んで始末する
    //    （std::string や shared_ptr をムーブした後を想像すると分かりやすい）。
    //
    //  最後に _entityCount を生存数（write）へ更新し、ゴミ箱リストを空にする。
    //
    //  実験3で「最後尾の entity が空いた穴へ移動する」のがこの関数の結果。
    // ============================================================================
    // CCL_Chunk.cpp の CompactHoles 修正版
    void Chunk::CompactHoles(const std::function<void(EntityID, Chunk *, size_t)> &onMoveCallback)
    {
        // 削除されたエンティティが一つもなければ、穴埋めする必要なし
        if (_destroyedEntities.empty()) return;


        // ? ２ポインタアルゴリズム　もっとイメージ持てるように理解する
        size_t read  = 0; // 読み込み用の指（走査用）
        size_t write = 0; // 書き込み用の指（詰め先用）

        while (read < _entityCount) 
        {
            EntityID currentID = _ownerEntities[read];

            // この座席が「空席（すでに死んでいる）」かどうかの判定
            bool isDead = (currentID == InvalidEntityID);

            if (isDead) {
                // すでに RemoveEntity でデストラクタは呼ばれている。
                // したがって、ここは「書き込み位置(write)」を進めずに、
                // 「読み込み位置(read)」だけを1つ進める（穴をスキップする）だけでよい。

                ++read;
                continue;
            }

            // 生きているエンティティで、かつ前に「穴」があった（read と write がズレた）場合のみ前に詰める
            if (read != write) {
                // 名簿の書き換え
                _ownerEntities[write] = currentID;
               

                // 各コンポーネントのデータを、後ろの席(read)から前の席(write)へ移動
                for (size_t ci = 0; ci < _archetype.size(); ++ci) {
                    size_t baseOffset = _componentOffsets[ci];
                    void  *dst =
                        _memoryBlock.data() + baseOffset + (write * _archetype[ci].typeSize);
                    void *src = _memoryBlock.data() + baseOffset + (read * _archetype[ci].typeSize);

                    if (_archetype[ci].mover) {
                        // ムーブコンストラクタ等の専用移動関数があればそれを使う
                        _archetype[ci].mover(dst, src);
                    }
                    else {
                        // なければメモリごと物理コピー
                        std::memmove(dst, src, _archetype[ci].typeSize);
                    }

                    // 移動元の「抜け殻」の後始末
                    // srcのデータはdstに移動（ムーブ）したため、srcには空っぽのデータが残っています。
                    // （std::stringのムーブ後や、shared_ptrのムーブ後など）
                    // これを安全に消滅させるために、ここで一度だけデストラクタを呼びます。
                    _archetype[ci].destructor.Destruct(src);
                }

                // 移動が発生した事実を、ChunkManagerに報告して全体マップを更新してもらう
                if (onMoveCallback) {
                    onMoveCallback(currentID, this, write);
                }
            }

            // 次の座席へ
            ++read;
            ++write;
        }

        // 最後に、生き残った人数（write）に定員カウンターを更新
        _entityCount = write;
        // ゴミ箱を空にする
        _destroyedEntities.clear();
    }


    // ============================================================================
    // Chunk::AddEntity  ─ 席を1つ確保して席番号を返す
    // ----------------------------------------------------------------------------
    //  一言で : 名簿に EntityID を書いて _entityCount を +1 するだけ。
    //
    //  戻り値 : 確保した席番号。満員なら assert して InvalidIndex を返す。
    //
    //  ★この関数ではメモリ確保が一切発生しない★
    //    Create() の時点で定員いっぱい分を確保済みなので、ここは
    //    「空いている席に名前を書く」だけで終わる。
    //    ECS が大量のエンティティ生成に強いのはこの構造のおかげ。
    //
    //  注意
    //    満員時は assert のみ。NDEBUG（Release）では assert が消えるため、
    //    呼び出し側が定員を守る前提になっている（既知の弱点）。
    // ============================================================================
    size_t Chunk::AddEntity(EntityID entity) {

        // 満員でないかチェック
        if (_entityCount >= _capacity)
        {
            assert(false && "Chunk is full!");
            return InvalidIndex; // クラッシュを防ぐ
        }

        // 新しい人の番号（現在の入場者数）
        size_t index = _entityCount;

        // 誰が入っているか名簿に記録
        _ownerEntities[index] = entity;

        // ★旧コードではここにあった _components へのアクセスや
        // メモリのresize処理は、事前一括確保済みのため完全に不要です。
        // エンティティの数を1つ増やすだけで完了します。

        // 入場者カウンターを+1するだけ！ メモリの確保は一切発生しない。
        _entityCount++;
        return index; // ① インデックスを返す
    }

}
