#include "ECS/Core/CCL_World.h"
#include "LuaBind_Scene.h"
#include "Engine/GamePlay/Core/Scene/SceneManager.h"

// 必要なシーンやローダーをインクルード
#include "Game/Logics/World/LevelLoader.h"
#include "Game/Scenes/GameScene.h"
#include "Game/Scenes/SceneLoading.h"
#include "Game/Scenes/ShopScene.h"

void LuaBind_Scene::Bind(sol::state &lua, CCL::ECS::Core::World *world)
{
    auto &sm = SceneManager::Instance();

    // ---------------------------------------------------
    // 1. Sceneテーブル (シーン管理)
    // ---------------------------------------------------
    auto scene = lua.create_named_table("Scene");

    // 現在のシーン名を取得
    scene.set_function("GetName", [&sm]() -> std::string { return sm.GetCurrentSceneName(); });

    // シーン遷移 (文字列からクラスを生成してロード)
    scene.set_function("Load", [&sm](std::string nextName) {
        BaseScene *target = nullptr;

        if (nextName == "Game") {
            target = new GameScene();
        }
        else if (nextName == "Shop") {
            target = new ShopScene();
        }

        if (target) {
            // 名前更新
            sm.SetCurrentSceneName(nextName);
            // ロード画面を挟んで遷移
            sm.ChangeScene(new SceneLoading(target));
            printf("[Lua] Scene Changing to: %s\n", nextName.c_str());
        }
    });

    // ---------------------------------------------------
    // 2. Playerテーブル (レベル管理など)
    // ---------------------------------------------------
    auto player = lua.create_named_table("Player");

    // レベル取得
    player.set_function("GetLevel", [&sm]() { return sm.GetPlayerData().stageLevel; });

    // レベル設定
    player.set_function("SetLevel", [&sm](int level) { sm.GetPlayerData().stageLevel = level; });

    // レベルリセット
    player.set_function("ResetLevel", [&sm]() { sm.GetPlayerData().Reset(); });

    // ---------------------------------------------------
    // 3. Gameテーブル (全体システム)
    // ---------------------------------------------------
    auto game = lua.create_named_table("Game");

    // セーブ実行 (Worldポインタが必要なためキャプチャする)
    game.set_function("Save", [world]() {
        LevelLoader::SavePlayerData(world);
        printf("[Lua] Game Saved.\n");
    });
}