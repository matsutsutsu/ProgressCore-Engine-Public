#pragma once
// ===========================================================================
// CCL_SystemBase.h
// ----------------
// 全ての System クラスの基底となる仮想インターフェース SystemBase を定義します。
//
// ECS における「System」とは、Chunk に詰められたデータを毎フレーム処理する
// ロジック単位であり、Movement / Render / AI といった具体的なロジックは
// すべて SystemBase を継承して実装されます。
//
// 設計方針:
//   - SystemBase 自体は「データの保持」と「インターフェース」だけを持つ軽量
//     なクラス。重いテンプレート展開（ForEach 等）は派生テンプレート
//     クラスである IfSystem 側に集約されています。
//   - これにより SystemBase だけを必要とするコード（System 群を管理する
//     SystemManager / TaskScheduler 等）は IfSystem の重いヘッダを引かずに
//     済みます。
// ===========================================================================

#include <chrono>
#include <type_traits>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

#include "Engine/ECS/Common/CCL_Archetype.h"

// Tracy（プロファイラ）は UpdateImpl 内のインライン実装で参照される
#include "tracy/Tracy.hpp"

// Chunk / World / JobSystem はポインタとしてしか使わないため前方宣言で十分。
// これにより SystemBase.h は重い実装ヘッダに依存しません。
namespace CCL::ECS {
    class Chunk;
    class World;
    class JobSystem;
}





namespace CCL::ECS
{
    /**
     * @brief 全ての System に共通するインターフェースとデータを提供する基底クラス。
     *
     * 主な責務:
     *   - 自身が処理すべき Chunk の集合 (m_targetChunks) を保持
     *   - System の Update 計測（Tracy + 経過時間 EMA）
     *   - World / JobSystem / 構造変更 mutex への参照を保持
     *   - 優先度 (m_priority) や名前 (m_name) 等のメタ情報
     *   - Read/Write 依存型の宣言（TaskScheduler が解析）
     *
     * 派生クラスは通常 `IfSystem<Derived, ...>` を経由して実装します。
     * Filter（必要 Component 構成）の自動生成や型安全な ForEach も
     * IfSystem 側で提供されます。
     */
    class SystemBase
    {
    public:
        /**
         * @brief System を生成。コンストラクタで自動的にユニークな SystemID を発行。
         * @param name デバッグ・GUI 表示用の名前
         */
        SystemBase(const std::string& name) : m_name(name)
        {
            m_systemID = ID_Emitter::EmitSystemID();
        }

        virtual ~SystemBase() = default;

        // ID_Emitter から発行されたユニーク SystemID
        SystemID GetSystemID() const { return m_systemID; }

        // アーキタイプフィルタ（このSystemが必要とするコンポーネント構成）
        const Archetype& GetFilter() const { return m_filter; }

        // 除外フィルタ（このコンポーネントを持つEntityは処理対象外にする）
        const Archetype& GetExcludeFilter() const { return m_excludeFilter; }

        // このフレームで処理対象とする Chunk を追加（SystemManager から呼ばれる）
        void AddTargetChunk(CCL::ECS::Chunk* chunk)
        {
            m_targetChunks.push_back(chunk);
        }

        // 処理対象 Chunk のリストをクリアする
        void ClearTargetChunks()
        {
            m_targetChunks.clear();
        }

        /**
         * @brief System 登録時に1度だけ呼ばれる初期化処理。
         *
         * EventBus の購読や、事前計算したいデータの構築などに使用します。
         * デフォルト実装は空。
         */
        virtual void Initialize() {}

        /**
         * @brief 派生クラスが必ず実装する更新ロジック本体。
         * @param dt 前フレームからの経過時間（秒）
         */
        virtual void Update(float dt) = 0;

        /**
         * @brief 計測・プロファイラ統合付きで Update を呼び出すラッパー。
         *
         * TaskScheduler / JobSystem からはこの関数が呼ばれ、Tracy 計測と
         * `m_lastUpdateTime`（瞬間値）/ `m_displayUpdateTime`（指数移動平均）
         * を更新したうえで派生の Update(dt) を呼び出します。
         */
        virtual void UpdateImpl(float dt)
        {
            ZoneScoped;
            ZoneName(m_name.c_str(), m_name.size());

            auto start = std::chrono::high_resolution_clock::now();

            Update(dt);

            auto end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<float, std::milli> elapsed = end - start;
            m_lastUpdateTime    = elapsed.count();
            // 指数移動平均（係数 0.1）。表示の安定化用。
            m_displayUpdateTime = m_displayUpdateTime * 0.9f + m_lastUpdateTime * 0.1f;
        }


        // デバッグ描画の有効/無効を切り替えるフラグ
        bool isDebugVisible = false;

        bool IsDebugVisible() const { return isDebugVisible; }

        // このシステムがエディタ用のGUIを持っているかどうか。
        //   登録時に SystemManager が OnGui の override 有無から自動で立てる。
        //   派生側で手書きしない（書き忘れて一覧から消える事故を無くすため）。
        bool hasGui = false;

        // ImGui によるインスペクタ描画（必要なら派生側で override）
        virtual void OnGui() {}

        // 今フレームの処理対象 Chunk リストを取得
        const std::vector<CCL::ECS::Chunk*>& GetTargetChunks() const
        {
            return m_targetChunks;
        }

        // World 参照をセット（SystemManager が登録時に呼ぶ）
        void SetWorld(World* world) { m_world = world; }

        // --------------------------------------------------------
        // 優先度
        // --------------------------------------------------------

        // 優先度をセット（SystemManager から呼ばれる）
        void SetPriority(int priority) { m_priority = priority; }

        // 保存された優先度を返す（オーバーライド不要）
        virtual int GetPriority() const { return m_priority; }


        // --------------------------------------------------------
        // メタ情報・計測
        // --------------------------------------------------------

        // System 名（デバッグ・GUI 表示用）
        const std::string& GetName() const { return m_name; }

        float GetLastUpdateTime() const { return m_lastUpdateTime; }
        void  SetLastUpdateTime(float ms) { m_lastUpdateTime = ms; }

        float GetDisplayUpdateTime() const { return m_displayUpdateTime; }
        void  SetDisplayUpdateTime(float ms) { m_displayUpdateTime = ms; }


        // --------------------------------------------------------
        // ジョブシステム・mutex
        // --------------------------------------------------------

        // 並列化に使う JobSystem を渡す（SystemManager から呼ばれる）
        void SetJobSystem(JobSystem* js) { m_jobSystem = js; }

        // ChunkManager の構造変更用 mutex を渡す（読み取りロックに使用）
        void SetStructureMutex(std::shared_mutex* mutex) { m_structureMutex = mutex; }


        // このSystemをマルチスレッド実行するか（デバッグ用に切り替え可）
        bool enableMultiThread = true;


        // --------------------------------------------------------
        // 依存解析用（TaskScheduler が並列実行の安全性を判定するために使用）
        // --------------------------------------------------------

        // このSystemが読み取りで使う Component 型 ID のリスト
        virtual std::vector<TypeID> GetReadTypes()  const { return {}; }

        // このSystemが書き込みで使う Component 型 ID のリスト
        virtual std::vector<TypeID> GetWriteTypes() const { return {}; }


        // --------------------------------------------------------
        // 明示的な実行順序の宣言（TaskScheduler がトポロジカルソートに使用）
        // --------------------------------------------------------
        //
        // 何のためにあるか:
        //   Read/Write 型による依存解析は「同時に走らせてよいか」しか判定できない。
        //   「どちらが先か」は決められないため、ステージ値が同じシステム同士の
        //   順序はソートの並び順まかせになっていた（＝リンカ依存で不定）。
        //   ここで「AはBの後」と書けば、同一ステージ内でも順序が保証される。
        //
        // 使い方（派生クラスでオーバーライド）:
        //   std::vector<const char*> GetRunAfter() const override {
        //       return { "AnimSetupSystem" };
        //   }
        //
        // 型ではなく名前で指定する理由:
        //   typeid で書くと相手のヘッダを include する必要が生じ、システム間に
        //   コンパイル時の依存が増えてしまう。名前なら疎結合のまま書ける。
        //   綴り間違いはグラフ構築時に検証してログへ出す（黙って無視しない）。
        //
        // 注意:
        //   ステージ（優先度）をまたぐ指定は無意味。ステージが違えば順序は
        //   もともと保証されているため、同一ステージ内でのみ効く。

        // このSystemより「先に」実行されるべき System 名のリスト
        virtual std::vector<const char *> GetRunAfter() const { return {}; }

        // このSystemより「後に」実行されるべき System 名のリスト
        virtual std::vector<const char *> GetRunBefore() const { return {}; }


    protected:
        // ----------------------------------------------------------
        // データメンバ（派生クラスからアクセス可能）
        // ----------------------------------------------------------

        // このSystemが今フレーム処理するべき Chunk 群
        std::vector<CCL::ECS::Chunk*> m_targetChunks;

        // 必要 Component 構成（派生側で IfSystem コンストラクタが自動設定）
        Archetype m_filter;

        // 除外コンポーネント構成（IfSystem::SetExclude で設定）
        Archetype m_excludeFilter;

        // ユニークな SystemID
        SystemID m_systemID = InvalidSystemID;

        // 所属する World への参照（SystemManager が登録時に注入）
        World* m_world = nullptr;

        // デフォルト優先度（中間値）。SystemManager 経由で上書き
        int m_priority = 1000;

        // JobSystem への参照（並列 ForEach 等で使用）
        JobSystem* m_jobSystem = nullptr;

        // ChunkManager の構造変更用 mutex（読み取りロック用）
        std::shared_mutex* m_structureMutex = nullptr;


        // ----------------------------------------------------------
        // メタ情報・計測関連
        // ----------------------------------------------------------
        std::string m_name;                  // System の表示名
        float       m_lastUpdateTime    = 0.0f; // 直近の Update 時間 (ms)
        float       m_displayUpdateTime = 0.0f; // 表示用に滑らかにした値 (EMA)
    };


    // -----------------------------------------------------------------------
    // T が自分で OnGui() を宣言しているか（＝インスペクタに出すべきか）。
    //
    //   &T::OnGui の「型」で判る。override していれば void (T::*)() になり、
    //   基底のものを継承しているだけなら void (SystemBase::*)() のままになる
    //   （&X::member の型は、名前を書いたクラスではなく宣言元のクラスで決まる）。
    //
    //   これで派生側はフラグを書く必要が無くなる。OnGui を宣言する以外に
    //   一覧へ載せる方法が存在しない＝載せ忘れが起こらない。
    // -----------------------------------------------------------------------
    template <class T>
    inline constexpr bool HasOwnOnGui =
        !std::is_same_v<decltype(&T::OnGui), void (SystemBase::*)()>;
    // decltype( - )  ...引数の型を返す
    // std::is_same_v<type1, type2> ...2つの型が同じかどうかを判定する

} 
