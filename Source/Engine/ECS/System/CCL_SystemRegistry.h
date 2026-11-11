#pragma once
// ===========================================================================
// 概要: System を各 .cpp から自動登録するための受付窓口とマクロ
//
// 責務:
//   - 静的初期化時に登録処理を溜め込む（SystemRegistry / SystemRegistrar）
//   - 起動時に溜めた登録処理を SystemManager へまとめて流す（RegisterAll）
//   - REGISTER_LOGIC_SYSTEM / REGISTER_RENDER_SYSTEM マクロの提供
//
// 備考: 溜め込む順番は翻訳単位をまたぐ静的初期化順序で決まり、不定
//       実行順は SystemManager 側でステージと System 名で並べ直すので影響しない
//
// @code
//   // MovementSystem.cpp の末尾
//   REGISTER_LOGIC_SYSTEM(MovementSystem, Priority::LogicStage::L02_Update)
// @endcode
// ===========================================================================
#include "CCL_SystemManager.h"
#include <functional>
#include <vector>

namespace CCL::ECS
{

    // 全 System の登録手続きを起動時まで溜め込む受付窓口
    class SystemRegistry
    {
    public:
        using RegisterFunc = std::function<void(SystemManager &)>;

        // main より前（静的初期化中）に呼ばれるため、静的ローカル変数で初回アクセス時に初期化する
        static SystemRegistry &Instance()
        {
            static SystemRegistry instance;
            return instance;
        }

        void AddRegistration(RegisterFunc func) { m_registrations.push_back(func); }

        // 溜め込んだ登録処理を一斉に実行する（起動時に1回だけ呼ばれる）
        void RegisterAll(SystemManager &manager)
        {
            for (auto &reg : m_registrations)
            {
                reg(manager);
            }
        }

    private:
        std::vector<RegisterFunc> m_registrations;
    };

    // グローバル変数のコンストラクタで受付を済ませるためのヘルパー
    struct SystemRegistrar
    {
        SystemRegistrar(SystemRegistry::RegisterFunc func)
        {
            SystemRegistry::Instance().AddRegistration(func);
        }
    };
} // namespace CCL::ECS

// ============================================================
// 自動登録マクロ
// ============================================================
// 各 System の .cpp の末尾に書くだけで登録が完了する
// 無名名前空間に置くので、変数名が他の翻訳単位と衝突しない

#define REGISTER_LOGIC_SYSTEM(SystemClass, Stage)                                      \
    namespace                                                                          \
    {                                                                                  \
        static CCL::ECS::SystemRegistrar const auto_reg_##SystemClass(                 \
            [](CCL::ECS::SystemManager &m) { m.RegisterSystem<SystemClass>(Stage); }); \
    }

// 可変長引数は System のコンストラクタへそのまま転送される
#define REGISTER_RENDER_SYSTEM(SystemClass, Stage, ...)                \
    namespace                                                          \
    {                                                                  \
        static CCL::ECS::SystemRegistrar const auto_reg_##SystemClass( \
            [](CCL::ECS::SystemManager &m) {                           \
                m.RegisterSystem<SystemClass>(Stage, __VA_ARGS__);     \
            });                                                        \
    }

// デバッグ表示専用の System はこちらで登録する。製品ビルド（ENABLE_EDITOR なし）では登録ごと消える
// 中身だけを空にすると、何もしない System が実行順の解決の対象としてだけ残るため、登録側で外す
#ifdef ENABLE_EDITOR
#define REGISTER_DEBUG_RENDER_SYSTEM REGISTER_RENDER_SYSTEM
#else
#define REGISTER_DEBUG_RENDER_SYSTEM(SystemClass, Stage, ...)
#endif
