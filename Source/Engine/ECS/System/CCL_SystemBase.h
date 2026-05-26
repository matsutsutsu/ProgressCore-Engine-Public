/**
 * @file   CCL_SystemBase.h
 * @brief  全Systemの基底クラス (テンプレートを含まない軽量ヘッダ)
 *
 * @details
 *  SystemBaseはSystemの最小骨格を提供します。
 *  本ヘッダは Chunk と World を前方宣言のみで扱うため、
 *  「Systemを定義するだけ」のファイル(○○System.h)からの include コストを最小化します。
 *
 *  ForEachなどテンプレートを多用するAPIは CCL_IfSystem.h に分離されています。
 *  実装側(○○System.cpp)で IfSystem を継承する場合のみ、IfSystem.h を include してください。
 *
 *  【ビルド速度設計】
 *   - Chunk/Worldは前方宣言だけ → Chunk.h, World.hを引きずらない
 *   - <chrono>を含まない → UpdateImplのタイミング計測は.cppに移動
 *
 *  【依存】
 *   - CCL_IDs.h, CCL_Archetype.h
 *   - CCL_Chunk_fwd.h, CCL_World_fwd.h
 *   - \<vector\>, \<string\>, \<shared_mutex\>
 *
 * @author CCL ECS Library
 */
#pragma once

#include "../Common/CCL_IDs.h"
#include "../Common/CCL_Archetype.h"
#include "../Core/CCL_Chunk_fwd.h"
#include "../Core/CCL_World_fwd.h"

#include <vector>
#include <string>
#include <shared_mutex>


namespace CCL::ECS
{
    // 前方宣言 (JobSystem は SystemBase.cpp と IfSystem.h で必要)
    namespace Core { class JobSystem; }


    /**
     * @class SystemBase
     * @brief 全Systemの基底
     *
     * @details
     *  Systemは「特定のArchetypeに合致するEntity群に毎フレーム処理を実行する」
     *  存在です。直接継承するよりは、IfSystem<Derived, AccessTypes...> 経由で
     *  継承するほうがフィルタ自動生成・ForEachヘルパが使えて便利です。
     */
    class SystemBase
    {
    public:
        /**
         * @brief コンストラクタ
         * @param name デバッグ表示用システム名
         */
        explicit SystemBase(const std::string& name);

        virtual ~SystemBase() = default;

        // コピー禁止
        SystemBase(const SystemBase&)            = delete;
        SystemBase& operator=(const SystemBase&) = delete;


        // =====================================================================
        //  ID / 名前
        // =====================================================================

        /// @brief 個体識別ID
        SystemID GetSystemID() const noexcept { return _systemID; }

        /// @brief デバッグ名
        const std::string& GetName() const noexcept { return _name; }


        // =====================================================================
        //  フィルタ / 対象Chunk
        // =====================================================================

        /// @brief このSystemが要求するコンポーネント構成
        const Archetype& GetFilter() const noexcept { return _filter; }

        /// @brief 対象Chunkを追加(SystemManagerが呼ぶ)
        void AddTargetChunk(Core::Chunk* chunk) { _targetChunks.push_back(chunk); }

        /// @brief 対象Chunkリストをクリア
        void ClearTargetChunks() noexcept { _targetChunks.clear(); }

        /// @brief 対象Chunk取得
        const std::vector<Core::Chunk*>& GetTargetChunks() const noexcept { return _targetChunks; }


        // =====================================================================
        //  ライフサイクル
        // =====================================================================

        /// @brief 登録時に1度だけ呼ばれる(EventBus購読等)
        virtual void Initialize() {}

        /**
         * @brief 派生クラスが実装する更新処理
         * @param dt 経過時間(秒)
         */
        virtual void Update(float dt) = 0;

        /**
         * @brief Update実行のラッパー(タイミング計測等を行う)
         * @param dt 経過時間
         * @note  実装は .cpp 側にあるため、本ヘッダに \<chrono\> を含めずに済みます。
         */
        virtual void UpdateImpl(float dt);


        // =====================================================================
        //  GUI / デバッグ
        // =====================================================================

        bool isDebugVisible = false;
        bool IsDebugVisible() const noexcept { return isDebugVisible; }

        /// @brief デバッグGUI描画 (派生側でImGui等を使ってオーバーライド)
        virtual void OnGui() {}


        // =====================================================================
        //  World / JobSystem 接続
        // =====================================================================

        /// @brief Worldをセット(SystemManagerが呼ぶ)
        void SetWorld(Core::World* world) noexcept { _world = world; }

        /// @brief JobSystemをセット
        void SetJobSystem(Core::JobSystem* js) noexcept { _jobSystem = js; }

        /// @brief 構造変更ロック用Mutexをセット
        void SetStructureMutex(std::shared_mutex* m) noexcept { _structureMutex = m; }


        // =====================================================================
        //  優先度 / マルチスレッド
        // =====================================================================

        void SetPriority(int p) noexcept { _priority = p; }
        virtual int GetPriority() const noexcept { return _priority; }

        bool enableMultiThread = true;


        // =====================================================================
        //  タイミング情報
        // =====================================================================

        /// @brief 前回のUpdate所要時間(ms)
        float GetLastUpdateTime()    const noexcept { return _lastUpdateTime; }

        /// @brief 平滑化された表示用時間(ms)
        float GetDisplayUpdateTime() const noexcept { return _displayUpdateTime; }


        // =====================================================================
        //  データアクセス宣言 (TaskScheduler用)
        // =====================================================================

        /// @brief 読み取りで触る型のリスト (派生側でoverrideされる)
        virtual std::vector<TypeID> GetReadTypes()  const { return {}; }

        /// @brief 書き込みで触る型のリスト (派生側でoverrideされる)
        virtual std::vector<TypeID> GetWriteTypes() const { return {}; }


    protected:
        // =====================================================================
        //  メンバ
        // =====================================================================

        std::vector<Core::Chunk*>  _targetChunks;
        Archetype                  _filter;
        SystemID                   _systemID = InvalidSystemID;

        Core::World*               _world          = nullptr;
        Core::JobSystem*           _jobSystem      = nullptr;
        std::shared_mutex*         _structureMutex = nullptr;

        int                        _priority = 1000;

        std::string                _name;
        float                      _lastUpdateTime    = 0.0f;
        float                      _displayUpdateTime = 0.0f;
    };

} // namespace CCL::ECS
