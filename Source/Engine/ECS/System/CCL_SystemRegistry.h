/**
 * @file   CCL_SystemRegistry.h
 * @brief  Systemの自動登録支援ヘッダ
 *
 * @details
 *  ゲーム側で多数のSystemを定義する際、main()でひとつずつ RegisterSystem<T>()
 *  を呼ぶのは煩雑です。本ヘッダの CCL_REGISTER_SYSTEM マクロを使うと、
 *  System.cpp 内で 1行記述するだけで自動登録されます。
 *
 *  @par 使用例
 *  @code
 *      // MovementSystem.cpp
 *      #include "CCL_SystemRegistry.h"
 *      class MovementSystem : public IfSystem<...> { ... };
 *
 *      CCL_REGISTER_SYSTEM(MovementSystem);
 *  @endcode
 *
 *  @par 仕組み
 *  グローバル変数の初期化を利用して、登録関数を s_pendingRegistrations へ追加します。
 *  ECSContext::FlushPendingSystemRegistrations() を起動時に呼ぶことで、全登録が確定します。
 *
 *  @note Steam版のリリース用ビルドではグローバル静的初期化順問題に注意してください。
 *        別TU(翻訳単位)に登録があっても順序は不定なので、SystemPriorityで実行順序を制御してください。
 *
 *  【依存】
 *   - CCL_SystemManager.h
 *
 * @author CCL ECS Library
 */
#pragma once

#include "CCL_SystemManager.h"

#include <functional>
#include <vector>


namespace CCL::ECS
{
    /**
     * @class SystemRegistry
     * @brief プログラム開始時に登録される「Systemインスタンス化関数」のリスト
     */
    class SystemRegistry
    {
    public:
        using Registrar = std::function<void(SystemManager&)>;

        /// @brief 登録関数を追加(マクロから呼ばれる)
        static void Add(Registrar r)
        {
            Registrations().emplace_back(std::move(r));
        }

        /// @brief 蓄積された登録関数を全てSystemManagerに流し込む
        static void FlushTo(SystemManager& manager)
        {
            for (auto& r : Registrations()) r(manager);
            Registrations().clear();
        }

    private:
        /// @brief プロセス内グローバルな登録リスト
        static std::vector<Registrar>& Registrations()
        {
            static std::vector<Registrar> s_list;
            return s_list;
        }
    };

} // namespace CCL::ECS


// =============================================================================
//  CCL_REGISTER_SYSTEM マクロ
// =============================================================================

/**
 * @def    CCL_REGISTER_SYSTEM
 * @brief  ファイルスコープで System を自動登録する
 *
 * @param TYPE 登録するSystemクラス名 (デフォルトコンストラクト可能であること)
 *
 * @details
 *  内部で匿名namespace変数を作って初期化時に登録関数を呼ばせます。
 *  リンク時に弾かれないよう、cppでマクロを呼ぶことを推奨します。
 */
#define CCL_REGISTER_SYSTEM(TYPE)                                              \
    namespace {                                                                \
        const bool s_ccl_register_##TYPE = []{                                 \
            CCL::ECS::SystemRegistry::Add(                                     \
                [](CCL::ECS::SystemManager& m){ m.RegisterSystem<TYPE>(); }    \
            );                                                                 \
            return true;                                                       \
        }();                                                                   \
    }
