/**
 * @file   CCL_ECSContext.h
 * @brief  World + SystemManager + JobSystem をひとつにまとめた最上位ファサード
 *
 * @details
 *  ゲーム側からECSを利用する際の最も一般的な起点となるクラスです。
 *  これ1つを保持すれば、Entity生成・System登録・並列実行・構造変更の確定まで
 *  全て扱えます。
 *
 *  @par 典型的な利用フロー
 *  @code
 *      using namespace CCL::ECS;
 *      ECSContext ctx;
 *      ctx.RegisterSystem<MovementSystem>();
 *      ctx.RegisterSystem<RenderSystem>();
 *
 *      while (running) {
 *          ctx.UpdateLogic(dt);   // SystemUpdate + ScrutinyAndApply
 *          ctx.UpdateRender();    // 描画専用Systemなど
 *      }
 *  @endcode
 *
 *  【依存】
 *   - CCL_World.h
 *   - CCL_SystemManager.h
 *   - CCL_JobSystem.h
 *
 * @author CCL ECS Library
 */
#pragma once

#include "Core/CCL_World.h"
#include "Core/CCL_JobSystem.h"
#include "System/CCL_SystemManager.h"


namespace CCL::ECS
{
    /**
     * @class ECSContext
     * @brief World + JobSystem + SystemManager の所有・連携を提供する
     */
    class ECSContext
    {
    public:
        /**
         * @brief コンストラクタ。各サブシステムを初期化し連結する
         */
        ECSContext()
        {
            _logicSystemManager.SetWorld(&_world);
            _logicSystemManager.SetJobSystem(&_jobSystem);

            _renderSystemManager.SetWorld(&_world);
            _renderSystemManager.SetJobSystem(&_jobSystem);
        }

        ~ECSContext() = default;

        ECSContext(const ECSContext&)            = delete;
        ECSContext& operator=(const ECSContext&) = delete;


        // =====================================================================
        //  System 登録
        // =====================================================================

        /// @brief ロジック更新Systemを登録 (UpdateLogic で実行される)
        template <class T, typename... Args>
        T& RegisterLogicSystem(Args&&... args)
        {
            return _logicSystemManager.RegisterSystem<T>(std::forward<Args>(args)...);
        }

        /// @brief レンダー更新Systemを登録 (UpdateRender で実行される)
        template <class T, typename... Args>
        T& RegisterRenderSystem(Args&&... args)
        {
            return _renderSystemManager.RegisterSystem<T>(std::forward<Args>(args)...);
        }

        /// @brief デフォルト(=Logic) 側にSystemを登録
        template <class T, typename... Args>
        T& RegisterSystem(Args&&... args)
        {
            return RegisterLogicSystem<T>(std::forward<Args>(args)...);
        }


        // =====================================================================
        //  更新
        // =====================================================================

        /**
         * @brief ロジック更新を実行
         * @param dt 経過時間(秒)
         * @details
         *  1. logicSystems.Update(dt)
         *  2. world.ScrutinyAndApply() で構造変更を確定
         */
        void UpdateLogic(float dt)
        {
            _logicSystemManager.Update(dt);
            _world.ScrutinyAndApply();
        }

        /**
         * @brief 描画系Systemを実行
         * @param dt 経過時間(秒)
         * @details
         *  描画ロジックは構造変更を起こさない前提なので、ScrutinyAndApplyを呼びません。
         */
        void UpdateRender(float dt = 0.0f)
        {
            _renderSystemManager.Update(dt);
        }

        /**
         * @brief ポーズ中の更新(UIのみ動かすなど)
         * @details 派生プロジェクトの好みで使い分けてください。
         *          現在は UpdateRender と同等です。
         */
        void UpdatePaused(float dt = 0.0f)
        {
            _renderSystemManager.Update(dt);
        }


        // =====================================================================
        //  アクセサ
        // =====================================================================

        Core::World&         World()              { return _world; }
        const Core::World&   World() const        { return _world; }

        Core::JobSystem&     JobSystem()          { return _jobSystem; }

        SystemManager&       LogicSystems()       { return _logicSystemManager; }
        SystemManager&       RenderSystems()      { return _renderSystemManager; }


    private:
        Core::World      _world;
        Core::JobSystem  _jobSystem;
        SystemManager    _logicSystemManager;
        SystemManager    _renderSystemManager;
    };

} // namespace CCL::ECS
