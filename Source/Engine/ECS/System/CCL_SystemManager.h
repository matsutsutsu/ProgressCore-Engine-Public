#pragma once
// ===========================================================================
// 概要: System の登録・並び替え・更新を一括で管理するクラス。
//
// 責務:
//   - System を Logic / Render の2グループに分けて保持する
//   - 登録時に World・ジョブプール・優先度を注入し、Initialize を呼ぶ
//   - TaskScheduler で依存グラフを構築し、グループごとに並列実行する
//   - 型指定での System 取得（GetSystem<T>）
//
// 備考: 実行順は「ステージ（優先度）→ 同値なら System 名」で決まる。
//       順序を保証したい組は GetRunAfter / GetRunBefore で明示すること。
// ===========================================================================

#include <vector>
#include <memory>
#include <algorithm>
#include <chrono>
#include "Engine/ECS/System/CCL_TaskScheduler.h"
#include "Engine/ECS/System/CCL_SystemBase.h"
#include "Engine/ECS/Core/CCL_ChunkManager.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/Jobs/CCL_JobSystem.h"

// Priority::LogicStage / RenderStage の定義
#include "Engine/Core/Systems/SystemPriority.h"

namespace CCL::ECS
{
    // System の実行タイミングの区分
    enum class SystemGroup
    {
        Logic, // 固定時間で動く（物理、AI、入力、移動）
        Render // 毎フレーム動く（描画、UI、カメラ補間）
    };

    class SystemManager
    {
    public:
        SystemManager()  = default;
        ~SystemManager() = default;

        // ============================================================
        // 初期設定
        // ============================================================

        // 登録する System へ配る World を設定する（EditorScene::Initialize などで呼ぶ）
        void SetWorld(World *world) { m_world = world; }

        /**
         * @brief 共有ジョブプールを注入する（EngineServices が持つ唯一のプールを渡す）。
         *
         * @note RegisterSystem より前に呼ぶこと。各 System へ配るのは登録時のため。
         */
        void SetJobSystem(JobSystem *js) { m_jobSystem = js; }

        // ============================================================
        // System 登録
        // ============================================================

        /**
         * @brief Logic グループへ System を登録する。
         *
         * @tparam T    登録する System 型（SystemBase 派生）
         * @param stage 実行ステージ。値が小さいほど先に実行される
         * @param args  T のコンストラクタへ転送する引数
         * @return 登録した System へのポインタ（所有は SystemManager）
         */
        template <class T, class... Args>
        T *RegisterSystem(Priority::LogicStage stage, Args &&...args)
        {
            return RegisterSystemInternal<T>(
                SystemGroup::Logic, static_cast<int>(stage), std::forward<Args>(args)...);
        }

        // Render グループへ System を登録する。引数は Logic 版と同じ
        template <class T, class... Args>
        T *RegisterSystem(Priority::RenderStage stage, Args &&...args)
        {
            return RegisterSystemInternal<T>(
                SystemGroup::Render, static_cast<int>(stage), std::forward<Args>(args)...);
        }

        // 登録済みの全 System を破棄する
        void SystemClear()
        {
            m_logicSystems.clear();
            m_renderSystems.clear();

            m_isGraphDirty = true;
        }

        // 各 System の処理対象 Chunk リストを、Filter に合致する Chunk で更新する
        void ResolveTargetChunks(ChunkManager *cm);

        // ============================================================
        // 更新
        // ============================================================

        // Logic グループを実行する（FixedUpdate から呼ばれる）
        void UpdateLogic(float dt)
        {
            BuildGraphsIfDirty();
            m_logicScheduler.Execute(dt, m_jobSystem);
        }

        // Render グループを実行する（Render から呼ばれる）
        void UpdateRender(float dt)
        {
            // シーンを切り替えたフレームは、ロジックの更新より先にここへ来る。
            // ここで作らないと、そのフレームは描画の System が1つも走らない。
            BuildGraphsIfDirty();
            m_renderScheduler.Execute(dt, m_jobSystem);
        }

        // ============================================================
        // 取得・デバッグ
        // ============================================================

        // System Manager ウィンドウ（ImGui）を描画する
        void OnGui();

        const std::vector<std::unique_ptr<SystemBase>> &GetLogicSystems() const { return m_logicSystems; }
        const std::vector<std::unique_ptr<SystemBase>> &GetRenderSystems() const { return m_renderSystems; }

        // エディタが依存グラフを表示するために読む
        const TaskScheduler &GetLogicScheduler() const { return m_logicScheduler; }
        const TaskScheduler &GetRenderScheduler() const { return m_renderScheduler; }

        /**
         * @brief 指定した型の System を取得する。Logic → Render の順に探す。
         *
         * @return 見つかった System。未登録なら nullptr
         *
         * @code
         *   auto *animSys = systemManager->GetSystem<AnimationSystem>();
         * @endcode
         */
        template <typename T>
        T *GetSystem() const
        {
            for (const auto &sys : m_logicSystems)
            {
                if (T *casted = dynamic_cast<T *>(sys.get()))
                {
                    return casted;
                }
            }

            for (const auto &sys : m_renderSystems)
            {
                if (T *casted = dynamic_cast<T *>(sys.get()))
                {
                    return casted;
                }
            }

            return nullptr;
        }

        // 全 System のマルチスレッドを一括で OFF にする（並列起因のバグ切り分け用）
        bool globalMultiThreadEnabled = true;

        // 依存グラフ構築時に「順序が不定なペア」をログへ出すか。
        // 一覧が長くて邪魔なときだけ false にする。
        bool logSchedulerAmbiguities = true;

    private:
        /**
         * @brief RegisterSystem の実体。System を生成し、依存を注入して初期化する。
         *
         * 生成 → World/ミューテックス/ジョブプール/優先度の注入 → Initialize →
         * グループのリストへ追加 → 並び替え、の順で行う。
         * Initialize は注入が全て済んでから呼ぶ必要がある（中で World を使う System があるため）。
         */
        template <class T, class... Args>
        T *RegisterSystemInternal(SystemGroup group, int priority, Args &&...args)
        {
            // T が SystemBase 派生であることをコンパイル時に強制する
            static_assert(std::is_base_of<SystemBase, T>::value, "T must be derived from SystemBase");

            auto sys = std::make_unique<T>(std::forward<Args>(args)...);

            // 所有権は sys がリストへ移すので、戻り値用に生ポインタを先に取っておく
            T *ptr = sys.get();

            // インスペクタに出すかは OnGui を override したかで決まる。
            //   派生側にフラグを書かせない。書き忘れると一覧から静かに消えるだけで
            //   気付けないので、宣言そのものを唯一の情報源にする。
            ptr->hasGui = HasOwnOnGui<T>;

            if (m_world) ptr->SetWorld(m_world);

            ptr->SetPriority(priority);

            // 構造変更（Entity 生成・削除）を ChunkManager と排他するためのミューテックス
            if (m_world)
            {
                ptr->SetStructureMutex(&m_world->GetChunkManager().GetStructureMutex());
            }

            // ジョブプールを渡しておくと、System 側で ForEachParallel などが使える
            ptr->SetJobSystem(m_jobSystem);

            ptr->Initialize();

            if (group == SystemGroup::Logic)
            {
                m_logicSystems.push_back(std::move(sys));
            }
            else
            {
                m_renderSystems.push_back(std::move(sys));
            }

            // System が増えたので、次の更新で依存グラフを作り直させる
            m_isGraphDirty = true;

            // 並び替えは登録したグループのリストだけでよい
            SortSystems(group);

            return ptr;
        }

        // 依存グラフを、System の追加で古くなっていれば作り直す（起動時と System 追加時だけ走る）
        void BuildGraphsIfDirty()
        {
            if (!m_isGraphDirty) return;

            // Logic と Render は同じ System の追加で古くなるので、まとめて作り直す。
            m_logicScheduler.BuildGraph(m_logicSystems);
            m_renderScheduler.BuildGraph(m_renderSystems);
            m_isGraphDirty = false;

            // 「同一ステージで型は競合するのに前後が宣言されていない」組を報告する。
            // 直列化はされるので同時実行事故は起きないが、どちらが先かは
            // ソートの並び順まかせ＝潜在的な順序バグ。ここに出たペアに実依存が
            // あるなら GetRunAfter を書くか、ステージを分けること。
            if (logSchedulerAmbiguities)
            {
                m_logicScheduler.LogAmbiguities("Logic");
                m_renderScheduler.LogAmbiguities("Render");
            }
        }

        // 優先度（昇順）で System を並び替える
        void SortSystems(SystemGroup group)
        {
            auto &targetList = (group == SystemGroup::Logic) ? m_logicSystems : m_renderSystems;

            // 優先度が同値のときは System 名で決着を付ける。
            //
            // なぜ必要か:
            //   std::sort は安定ソートではないため、同値のシステム同士の相対順序は
            //   保証されない。しかも本関数は RegisterSystemInternal のたびに呼ばれ、
            //   その登録順自体が REGISTER_LOGIC_SYSTEM マクロによるグローバル変数の
            //   初期化順（＝翻訳単位をまたぐ静的初期化順序）に依存する。
            //   名前で決着を付けないと、.vcxproj にファイルを足しただけで実行順が
            //   入れ替わる（BoneAttachment と JoltPush の順序逆転がこれ）。
            //
            //   名前で決着を付けても「正しい順序」にはならないが、少なくとも
            //   毎回同じ順序になる。順序を実際に保証したい箇所は
            //   GetRunAfter / GetRunBefore で明示的に宣言すること。
            std::sort(targetList.begin(), targetList.end(),
                      [](const std::unique_ptr<SystemBase> &a, const std::unique_ptr<SystemBase> &b) {
                          if (a->GetPriority() != b->GetPriority())
                              return a->GetPriority() < b->GetPriority();
                          return a->GetName() < b->GetName();
                      });
        }

        // 共有ジョブプールへのポインタ（所有しない）。
        // 実体は EngineServices が1つだけ持ち、SetJobSystem で注入される。
        JobSystem *m_jobSystem = nullptr;

        std::vector<std::unique_ptr<SystemBase>> m_logicSystems;
        std::vector<std::unique_ptr<SystemBase>> m_renderSystems;

        World *m_world = nullptr; // 登録時に各 System へ配る（所有しない）

        TaskScheduler m_logicScheduler;
        TaskScheduler m_renderScheduler;
        bool          m_isGraphDirty = true; // true なら次の更新（Logic / Render の先に来た方）で依存グラフを作り直す

        // 前回 ResolveTargetChunks で配った Chunk のポインタ列。変化が無ければ再分配を省く
        std::vector<void *> m_cachedChunkPointers;
    };

} // namespace CCL::ECS
