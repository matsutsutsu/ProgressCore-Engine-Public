#pragma once
// ===========================================================================
// 概要: シーン名から Scene を生成するファクトリ SceneFactory と、その自動登録マクロ。
//
// 責務:
//   - シーン名 → 生成関数の登録と、名前からの生成
//   - REGISTER_SCENE マクロによる、各シーンの .cpp からの自動登録
//
// 備考: これにより SceneManager（Engine 層）が具体的なシーン（Game 層）を
//       include せず、名前だけで扱える。
// ===========================================================================
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

    void Register(const std::string &name, CreateFunc func);

    // 名前のシーンを new して返す（所有権は呼び出し側）。未登録なら nullptr
    Scene *Create(const std::string &name);

    // 登録済みのシーン名の一覧（エディタのシーン選択に使う）
    std::vector<std::string> GetSceneNames() const;

private:
    SceneFactory() {}
    std::unordered_map<std::string, CreateFunc> registry; // シーン名 → 生成関数
};

// ============================================================
// 自動登録
// ============================================================

// グローバル変数のコンストラクタで登録を済ませる（CCL_SystemRegistry.h の SystemRegistrar と同じ方式）。
// SceneFactory::Instance() は関数ローカル static なので、静的初期化順序の問題は起きない
struct SceneRegistrar
{
    SceneRegistrar(const std::string &name, SceneFactory::CreateFunc func)
    {
        SceneFactory::Instance().Register(name, func);
    }
};

// 各シーンの .cpp の末尾に書くだけで SceneFactory への登録が完了する
#define REGISTER_SCENE(SceneClass, SceneName)                         \
    namespace                                                         \
    {                                                                 \
        static SceneRegistrar const auto_scene_reg_##SceneClass(      \
            SceneName, []() -> Scene * { return new SceneClass(); }); \
    }
