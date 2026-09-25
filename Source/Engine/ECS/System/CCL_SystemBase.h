#pragma once
// ===========================================================================
// 概要: 全 System の基底となる仮想インターフェース SystemBase。
//
// 責務:
//   - 処理対象 Chunk の集合と Filter（必要/除外 Component 構成）の保持
//   - Update の計測（Tracy + 経過時間の指数移動平均）
//   - World / JobSystem / 構造変更 mutex への参照の保持
//   - 優先度・名前などのメタ情報と、実行順序・Read/Write 型の申告口
//
// 備考: 重いテンプレート展開（ForEach 等）は派生テンプレートの IfSystem に集約する。
//       SystemManager / TaskScheduler のように SystemBase だけを扱うコードが、
//       IfSystem の重いヘッダを引かずに済むようにするため。
// ===========================================================================

#include <chrono>
#include <type_traits>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

#include "Engine/ECS/Common/CCL_Archetype.h"

// UpdateImpl のインライン実装で Tracy を使う
#include "tracy/Tracy.hpp"

// Chunk / World / JobSystem はポインタでしか使わないので前方宣言で足りる
namespace CCL::ECS
{
    class Chunk;
    class World;
    class JobSystem;
} // namespace CCL::ECS

namespace CCL::ECS
{
    /**
     * @brief 全ての System に共通するインターフェースとデータを提供する基底クラス。
     *
     * 派生クラスは通常 `IfSystem<Derived, ...>` を経由して実装する。
     * Filter の自動生成や型安全な ForEach は IfSystem 側が提供する。
     */
    class SystemBase
    {
    public:
        /**
         * @brief System を生成し、ユニークな SystemID を発行する。
         * @param name デバッグ・GUI 表示用の名前
         */
        SystemBase(const std::string &name) : m_name(name)
        {
            m_systemID = ID_Emitter::EmitSystemID();
        }

        virtual ~SystemBase() = default;

        // ============================================================
        // ライフサイクル
        // ============================================================

        /**
         * @brief System 登録時に1度だけ呼ばれる初期化処理。
         *
         * EventBus の購読や、事前計算したいデータの構築に使う。デフォルト実装は空。
         */
        virtual void Initialize() {}

        /**
         * @brief 派生クラスが必ず実装する更新ロジック本体。
         * @param dt 前フレームからの経過時間（秒）
         */
        virtual void Update(float dt) = 0;

        /**
         * @brief 計測付きで Update を呼び出すラッパー。
         *
         * TaskScheduler からはこの関数が呼ばれる。Tracy のゾーンを張り、
         * `m_lastUpdateTime`（瞬間値）と `m_displayUpdateTime`（指数移動平均）を更新する。
         */
        virtual void UpdateImpl(float dt)
        {
            ZoneScoped;
            ZoneName(m_name.c_str(), m_name.size());

            auto start = std::chrono::high_resolution_clock::now();

            Update(dt);

            auto                                     end     = std::chrono::high_resolution_clock::now();
            std::chrono::duration<float, std::milli> elapsed = end - start;
            m_lastUpdateTime                                 = elapsed.count();
            // 指数移動平均（係数 0.1）。GUI の表示が毎フレーム暴れないようにする
            m_displayUpdateTime = m_displayUpdateTime * 0.9f + m_lastUpdateTime * 0.1f;
        }

        // ============================================================
        // 処理対象 Chunk・Filter
        // ============================================================

        // 必要 Component 構成
        const Archetype &GetFilter() const { return m_filter; }

        // 除外 Component 構成（1つでも持つ Entity は処理対象外）
        const Archetype &GetExcludeFilter() const { return m_excludeFilter; }

        // 処理対象 Chunk を追加する（SystemManager::ResolveTargetChunks から呼ばれる）
        void AddTargetChunk(CCL::ECS::Chunk *chunk)
        {
            m_targetChunks.push_back(chunk);
        }

        void ClearTargetChunks()
        {
            m_targetChunks.clear();
        }

        const std::vector<CCL::ECS::Chunk *> &GetTargetChunks() const
        {
            return m_targetChunks;
        }

        // ============================================================
        // 依存の注入（SystemManager が登録時に呼ぶ）
        // ============================================================

        void SetWorld(World *world) { m_world = world; }

        void SetPriority(int priority) { m_priority = priority; }

        void SetJobSystem(JobSystem *js) { m_jobSystem = js; }

        // ChunkManager の構造変更用 mutex（読み取りロックに使う）
        void SetStructureMutex(std::shared_mutex *mutex) { m_structureMutex = mutex; }

        // ============================================================
        // メタ情報・計測
        // ============================================================

        SystemID GetSystemID() const { return m_systemID; }

        // System 名（デバッグ・GUI 表示用。実行順序の宣言でも使う）
        const std::string &GetName() const { return m_name; }

        // 登録時に渡された優先度（ステージ値）を返す
        virtual int GetPriority() const { return m_priority; }

        float GetLastUpdateTime() const { return m_lastUpdateTime; }
        void  SetLastUpdateTime(float ms) { m_lastUpdateTime = ms; }

        float GetDisplayUpdateTime() const { return m_displayUpdateTime; }
        void  SetDisplayUpdateTime(float ms) { m_displayUpdateTime = ms; }

        // ============================================================
        // デバッグ・エディタ
        // ============================================================

        // ImGui によるインスペクタ描画（必要なら派生側で override）
        virtual void OnGui() {}

        // エディタ用の GUI を持っているか。
        //   登録時に SystemManager が OnGui の override 有無から自動で立てる。
        //   派生側で手書きしない（書き忘れて一覧から消える事故を無くすため）。
        bool hasGui = false;

        bool isDebugVisible = false; // デバッグ描画を表示するか

        bool IsDebugVisible() const { return isDebugVisible; }

        bool enableMultiThread = true; // 並列 ForEach を使うか（並列起因のバグ切り分け用）

        // ============================================================
        // 依存解析（TaskScheduler が並列実行してよいかの判定に使う）
        // ============================================================

        // 読み取りで使う Component 型 ID のリスト
        virtual std::vector<TypeID> GetReadTypes() const { return {}; }

        // 書き込みで使う Component 型 ID のリスト
        virtual std::vector<TypeID> GetWriteTypes() const { return {}; }

        // ============================================================
        // 実行順序の宣言（TaskScheduler がトポロジカルソートに使う）
        // ============================================================

        /**
         * @brief この System より「先に」実行されるべき System 名のリスト。
         *
         * Read/Write 型による依存解析は「同時に走らせてよいか」しか判定できない。
         * 「どちらが先か」は決められないため、ステージ値が同じ System 同士の
         * 順序はソートの並び順まかせになる（＝リンカ依存で不定）。
         * ここで「A は B の後」と書けば、同一ステージ内でも順序が保証される。
         *
         * 型ではなく名前で指定するのは、typeid で書くと相手のヘッダを include する
         * 必要が生じ、System 間にコンパイル時の依存が増えるため。名前なら疎結合の
         * まま書ける。綴り間違いはグラフ構築時に検証してログへ出す（黙って無視しない）。
         *
         * @note ステージ（優先度）をまたぐ指定は無意味。ステージが違えば順序は
         *       もともと保証されているため、同一ステージ内でのみ効く。
         *
         * @code
         *   std::vector<const char*> GetRunAfter() const override {
         *       return { "AnimSetupSystem" };
         *   }
         * @endcode
         */
        virtual std::vector<const char *> GetRunAfter() const { return {}; }

        // この System より「後に」実行されるべき System 名のリスト。規則は GetRunAfter と同じ
        virtual std::vector<const char *> GetRunBefore() const { return {}; }

    protected:
        std::vector<CCL::ECS::Chunk *> m_targetChunks; // 今フレーム処理する Chunk 群（所有しない）

        Archetype m_filter;        // 必要 Component 構成（IfSystem のコンストラクタが自動設定）
        Archetype m_excludeFilter; // 除外 Component 構成（IfSystem::SetExclude で設定）

        SystemID m_systemID = InvalidSystemID;

        World *m_world = nullptr; // 所属する World（所有しない）

        int m_priority = 1000; // 未設定時の既定値（中間値）。登録時に上書きされる

        JobSystem *m_jobSystem = nullptr; // 並列 ForEach で使う（所有しない）

        std::shared_mutex *m_structureMutex = nullptr; // ChunkManager の構造変更用 mutex（所有しない）

        std::string m_name;                     // System の表示名
        float       m_lastUpdateTime    = 0.0f; // 直近の Update 時間（ms）
        float       m_displayUpdateTime = 0.0f; // 表示用に滑らかにした Update 時間（ms、EMA）
    };

    /**
     * @brief T が自分で OnGui() を宣言しているか（＝インスペクタに出すべきか）。
     *
     * &T::OnGui の「型」で判る。override していれば void (T::*)() になり、
     * 基底のものを継承しているだけなら void (SystemBase::*)() のままになる
     * （&X::member の型は、名前を書いたクラスではなく宣言元のクラスで決まる）。
     *
     * これで派生側はフラグを書く必要が無くなる。OnGui を宣言する以外に
     * 一覧へ載せる方法が存在しない＝載せ忘れが起こらない。
     */
    template <class T>
    inline constexpr bool HasOwnOnGui =
        !std::is_same_v<decltype(&T::OnGui), void (SystemBase::*)()>;
    // decltype(式)                     … 式の型を返す（ここではメンバ関数ポインタの型）
    // std::is_same_v<type1, type2>     … 2つの型が同じかどうかを判定する

} // namespace CCL::ECS
