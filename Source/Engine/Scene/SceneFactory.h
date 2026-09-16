#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

#include "Scene.h"

class SceneFactory
{
public:
    using CreateFunc = std::function<Scene *()>;

    static SceneFactory &Instance()
    {
        static SceneFactory instance;
        return instance;
    }

    // シーン登録
    void Register(const std::string &name, CreateFunc func);

    // シーン生成
    Scene *Create(const std::string &name);

    // 登録済みシーン一覧を取得 (ImGuiでリスト表示に使える)
    std::vector<std::string> GetSceneNames() const;

private:
    SceneFactory() {}
    std::unordered_map<std::string, CreateFunc> registry;
};

// =========================================================================
// 自動登録用ヘルパー
//   グローバル変数の初期化タイミングを利用して登録を済ませる
//   （CCL_SystemRegistry.h の SystemRegistrar と同じ方式）。
//   SceneFactory::Instance() は関数ローカル static なので、
//   静的初期化順序の問題は起きない（初回参照時に構築される）。
// =========================================================================
struct SceneRegistrar
{
    SceneRegistrar(const std::string &name, SceneFactory::CreateFunc func)
    {
        SceneFactory::Instance().Register(name, func);
    }
};

// =========================================================================
// 各シーンの .cpp の末尾にこれを書くだけで SceneFactory への登録が完了する。
// これにより SceneManager(Engine) が具体シーン(Game)を include せずに済む。
// =========================================================================
#define REGISTER_SCENE(SceneClass, SceneName)                         \
    namespace                                                         \
    {                                                                 \
        static SceneRegistrar const auto_scene_reg_##SceneClass(      \
            SceneName, []() -> Scene * { return new SceneClass(); }); \
    }
