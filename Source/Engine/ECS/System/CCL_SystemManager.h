#pragma once
// CCL_SystemManager.h
// System登録 & Update管理

#include <vector>
#include <memory>
#include <algorithm>
#include <chrono>
#include "Engine/ECS/System/CCL_TaskScheduler.h"
#include "Engine/ECS/System/CCL_SystemBase.h"
#include "Engine/ECS/Core/CCL_ChunkManager.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/Jobs/CCL_JobSystem.h"

// Priority定義を知る必要がある
#include "Engine/Core/Systems/SystemPriority.h"


namespace CCL::ECS
{
    // システムの種類を定義
    enum class SystemGroup {
        Logic,  // 固定時間で動く（物理、AI、入力、移動）
        Render  // 毎フレーム動く（描画、UI、カメラ補間）
    };

    class SystemManager
    {
    public:
        SystemManager() = default;
        ~SystemManager() = default;

        // Worldをセットする（EditorScene::Initializeなどで呼ぶ）
        void SetWorld(World* world) { m_world = world; }

        // 共有ジョブプールを注入する（上層=EngineServicesが持つ唯一のプールを渡す）。
        // ※ RegisterSystems の前に呼ぶこと（各システムへ配るのは登録時のため）。
        void SetJobSystem(JobSystem* js) { m_jobSystem = js; }


        // ------------------------------------------------------------------
        // ★改良: ステージEnumを使った登録関数 (推奨)
        // ------------------------------------------------------------------

        // Logic用: SystemGroup::Logic を自動指定
        template <class T, class... Args>
        T *RegisterSystem(Priority::LogicStage stage, Args &&...args)
        {
            // Enumをintにキャストして内部登録へ
            return RegisterSystemInternal<T>(
                SystemGroup::Logic, static_cast<int>(stage), std::forward<Args>(args)...);
        }


        // ------------------------------------------------------------------
        // 旧登録関数 / 内部実装
        // ------------------------------------------------------------------
        template <class T, class... Args>
        T *RegisterSystem(Priority::RenderStage stage, Args &&...args)
        {
            return RegisterSystemInternal<T>(
                SystemGroup::Render, static_cast<int>(stage), std::forward<Args>(args)...);
        }


        // 登録関数：グループを指定できるようにする
        template <class T, class... Args>
        T *RegisterSystemInternal(SystemGroup group, int priority, Args &&...args) 
        {
            // 追加しようとしている型Tが必ずSystemBaseの派生クラスであることを
            // コンパイル時に強制している
            static_assert(std::is_base_of<SystemBase, T>::value, "T must be derived from SystemBase");

            // Systemオブジェクトをヒープ上に構築する
            auto sys = std::make_unique<T>(std::forward<Args>(args)...);

            // 生ポインタを取得（戻り値用）
            T* ptr = sys.get();

            // --- 重要：初期化処理 ---
            // 生成時にWorldを渡す
            if (m_world) ptr->SetWorld(m_world);

            // 優先度もセット
            ptr->SetPriority(priority);

            // ChunkManagerのmutexを設定
            if (m_world) {
                ptr->SetStructureMutex(&m_world->GetChunkManager().GetStructureMutex());
            }

            // 生成したシステムに共有ジョブプールを渡しておく
            // これで各システムは自動的に並列処理機能を使えるようになる
            // （m_jobSystem は EngineServices が持つ共有プールへのポインタ）
            sys->SetJobSystem(m_jobSystem);

            // =======================================================
            // 全ての準備が整ったので、システムの初期化を自動実行する
            // =======================================================
            ptr->Initialize();


            // グループに応じてリストに振り分け
            if (group == SystemGroup::Logic) {
                m_logicSystems.push_back(std::move(sys));
            }
            else {
                m_renderSystems.push_back(std::move(sys));
            }

            // システムが増えたので依存関係グラフを再構築する必要がある
            m_isGraphDirty = true;

            // --- 重要：ソート処理 ---
            // 登録されたグループのリストだけソートすればOK
            SortSystems(group);

            return ptr;
        }


        // 登録済みの全システムを破棄する
        void SystemClear() {
            m_logicSystems.clear();
            m_renderSystems.clear();

            // システムが空になったので、グラフもリセットさせる
            m_isGraphDirty = true;
        }

        // クエリ解決とデータ割り当て
        // 全てのSystemの_targetChunksリストを、そのSystemのArchetypeフィルタに
        // 合致するデータ（Chunk）で更新することです
        void ResolveTargetChunks(ChunkManager* cm);

        // 物理・ロジック系の更新（FixedUpdateから呼ばれる）
        void UpdateLogic(float dt) {


            // グラフが古ければ再構築（ゲーム起動時やシステム動的追加時に1回だけ走る）
            if (m_isGraphDirty) {
                m_logicScheduler.BuildGraph(m_logicSystems);
                m_renderScheduler.BuildGraph(m_renderSystems);
                m_isGraphDirty = false;
            }

            // スケジューラによる超並列実行！
            m_logicScheduler.Execute(dt, m_jobSystem);
        }


        // 描画系の更新（Renderから呼ばれる）
        void UpdateRender(float dt) {     

            // Render側のシステムもスケジューラで実行
            m_renderScheduler.Execute(dt, m_jobSystem);
        }

		// ImGuiによるデバッグGUI描画
        void OnGui();

     
        // 外部（Worldクラスなど）からシステムリストを取得できる関数を追加
        const std::vector<std::unique_ptr<SystemBase>>& GetLogicSystems() const { return m_logicSystems; }
        const std::vector<std::unique_ptr<SystemBase>>& GetRenderSystems() const { return m_renderSystems; }

        // 外部（エディタ）からタスクスケジューラを読み取るための関数
        const TaskScheduler& GetLogicScheduler() const { return m_logicScheduler; }
        const TaskScheduler& GetRenderScheduler() const { return m_renderScheduler; }

        // ★追加: 指定した型のシステムを取得する関数
        // 使用例: auto* animSys = systemManager->GetSystem<AnimationSystem>();
        template <typename T> T *GetSystem() const
        {
            // 1. Logic Systems から検索
            for (const auto &sys : m_logicSystems) {
                // dynamic_cast で型チェックを行う
                if (T *casted = dynamic_cast<T *>(sys.get())) {
                    return casted;
                }
            }

            // 2. Render Systems から検索
            for (const auto &sys : m_renderSystems) {
                if (T *casted = dynamic_cast<T *>(sys.get())) {
                    return casted;
                }
            }



            // 見つからなければ nullptr
            return nullptr;
        }


        // 全システムのマルチスレッドを一括で強制OFFにするフラグ
        // (デバッグ時に「とりあえず全部シングルで動かしてみる」用)
        bool globalMultiThreadEnabled = true;

    private:

        // 優先度（昇順）でシステムを並び替える
        void SortSystems(SystemGroup group) {
            auto& targetList = (group == SystemGroup::Logic) ? m_logicSystems : m_renderSystems;

            std::sort(targetList.begin(), targetList.end(),
                [](const std::unique_ptr<SystemBase>& a, const std::unique_ptr<SystemBase>& b) {
                    return a->GetPriority() < b->GetPriority();
                }
            );
        }

        // 共有ジョブプールへのポインタ（所有しない）。
        // 実体は EngineServices が1つだけ持ち、SetJobSystem で注入される。
        JobSystem* m_jobSystem = nullptr;

        // 2つのリストに分離
        std::vector<std::unique_ptr<SystemBase>> m_logicSystems;
        std::vector<std::unique_ptr<SystemBase>> m_renderSystems;


        World* m_world = nullptr;


        TaskScheduler m_logicScheduler;
        TaskScheduler m_renderScheduler;
        bool          m_isGraphDirty = true; // システムが追加されたらtrueにする

        // ==========================================
        //  キャッシュ用: 前回分配したチャンクのポインタ配列
        // ==========================================
        std::vector<void*> m_cachedChunkPointers;
    };

} // namespace CCL::ECS
