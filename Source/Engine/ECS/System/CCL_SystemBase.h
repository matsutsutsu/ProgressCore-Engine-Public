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
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

#include "Engine/ECS/Common/CCL_Common.h"

// Chunk / World / JobSystem はポインタとしてしか使わないため前方宣言で十分。
// これにより SystemBase.h は重い実装ヘッダに依存しません。
namespace CCL::ECS::Core {
    class Chunk;
    class World;
    class JobSystem;
}

// Tracy（プロファイラ）は UpdateImpl 内のインライン実装で参照される
#include "tracy/Tracy.hpp"



namespace CCL::ECS
{
    /**
     * @brief 全ての System に共通するインターフェースとデータを提供する基底クラス。
     *
     * 主な責務:
     *   - 自身が処理すべき Chunk の集合 (_targetChunks) を保持
     *   - System の Update 計測（Tracy + 経過時間 EMA）
     *   - World / JobSystem / 構造変更 mutex への参照を保持
     *   - 優先度 (_priority) や名前 (_name) 等のメタ情報
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
        SystemBase(const std::string& name) : _name(name)
        {
            _systemID = ID_Emitter::EmitSystemID();
        }

        virtual ~SystemBase() = default;

        /// @brief ID_Emitter から発行されたユニーク SystemID
        SystemID GetSystemID() const { return _systemID; }

        /// @brief アーキタイプフィルタ（このSystemが必要とするコンポーネント構成）
        const Archetype& GetFilter() const { return _filter; }

        /// @brief このフレームで処理対象とする Chunk を追加（SystemManager から呼ばれる）
        void AddTargetChunk(CCL::ECS::Core::Chunk* chunk)
        {
            _targetChunks.push_back(chunk);
        }

        /// @brief 処理対象 Chunk のリストをクリアする
        void ClearTargetChunks()
        {
            _targetChunks.clear();
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
         * `_lastUpdateTime`（瞬間値）/ `_displayUpdateTime`（指数移動平均）
         * を更新したうえで派生の Update(dt) を呼び出します。
         */
        virtual void UpdateImpl(float dt)
        {
            ZoneScoped;
            ZoneName(_name.c_str(), _name.size());

            auto start = std::chrono::high_resolution_clock::now();

            Update(dt);

            auto end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<float, std::milli> elapsed = end - start;
            _lastUpdateTime    = elapsed.count();
            // 指数移動平均（係数 0.1）。表示の安定化用。
            _displayUpdateTime = _displayUpdateTime * 0.9f + _lastUpdateTime * 0.1f;
        }


        /// @brief デバッグ描画の有効/無効を切り替えるフラグ
        bool isDebugVisible = false;

        bool IsDebugVisible() const { return isDebugVisible; }

        // このシステムがエディタ用のGUIを持っているかどうか
        bool hasGui = false;

        /// @brief ImGui によるインスペクタ描画（必要なら派生側で override）
        virtual void OnGui() {}

        /// @brief 今フレームの処理対象 Chunk リストを取得
        const std::vector<CCL::ECS::Core::Chunk*>& GetTargetChunks() const
        {
            return _targetChunks;
        }

        /// @brief World 参照をセット（SystemManager が登録時に呼ぶ）
        void SetWorld(Core::World* world) { _world = world; }

        // --------------------------------------------------------
        // 優先度
        // --------------------------------------------------------

        /// @brief 優先度をセット（SystemManager から呼ばれる）
        void SetPriority(int priority) { _priority = priority; }

        /// @brief 保存された優先度を返す（オーバーライド不要）
        virtual int GetPriority() const { return _priority; }


        // --------------------------------------------------------
        // メタ情報・計測
        // --------------------------------------------------------

        /// @brief System 名（デバッグ・GUI 表示用）
        const std::string& GetName() const { return _name; }

        float GetLastUpdateTime() const { return _lastUpdateTime; }
        void  SetLastUpdateTime(float ms) { _lastUpdateTime = ms; }

        float GetDisplayUpdateTime() const { return _displayUpdateTime; }
        void  SetDisplayUpdateTime(float ms) { _displayUpdateTime = ms; }


        // --------------------------------------------------------
        // ジョブシステム・mutex
        // --------------------------------------------------------

        /// @brief 並列化に使う JobSystem を渡す（SystemManager から呼ばれる）
        void SetJobSystem(Core::JobSystem* js) { _jobSystem = js; }

        /// @brief ChunkManager の構造変更用 mutex を渡す（読み取りロックに使用）
        void SetStructureMutex(std::shared_mutex* mutex) { _structureMutex = mutex; }


        /// @brief このSystemをマルチスレッド実行するか（デバッグ用に切り替え可）
        bool enableMultiThread = true;


        // --------------------------------------------------------
        // 依存解析用（TaskScheduler が並列実行の安全性を判定するために使用）
        // --------------------------------------------------------

        /// @brief このSystemが読み取りで使う Component 型 ID のリスト
        virtual std::vector<TypeID> GetReadTypes()  const { return {}; }

        /// @brief このSystemが書き込みで使う Component 型 ID のリスト
        virtual std::vector<TypeID> GetWriteTypes() const { return {}; }


    protected:
        // ----------------------------------------------------------
        // データメンバ（派生クラスからアクセス可能）
        // ----------------------------------------------------------

        /// @brief このSystemが今フレーム処理するべき Chunk 群
        std::vector<CCL::ECS::Core::Chunk*> _targetChunks;

        /// @brief 必要 Component 構成（派生側で IfSystem コンストラクタが自動設定）
        Archetype _filter;

        /// @brief ユニークな SystemID
        SystemID _systemID = InvalidSystemID;

        /// @brief 所属する World への参照（SystemManager が登録時に注入）
        Core::World* _world = nullptr;

        /// @brief デフォルト優先度（中間値）。SystemManager 経由で上書き
        int _priority = 1000;

        /// @brief JobSystem への参照（並列 ForEach 等で使用）
        Core::JobSystem* _jobSystem = nullptr;

        /// @brief ChunkManager の構造変更用 mutex（読み取りロック用）
        std::shared_mutex* _structureMutex = nullptr;


        // ----------------------------------------------------------
        // メタ情報・計測関連
        // ----------------------------------------------------------
        std::string _name;                  ///< System の表示名
        float       _lastUpdateTime    = 0.0f; ///< 直近の Update 時間 (ms)
        float       _displayUpdateTime = 0.0f; ///< 表示用に滑らかにした値 (EMA)
    };

} // namespace CCL::ECS
