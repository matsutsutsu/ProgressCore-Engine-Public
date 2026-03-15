#pragma once
#include "ECS/Core/CCL_World.h"
#include "ECS/System/CCL_SystemManager.h"
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Editor/Utils/AssetBrowser.h"
#include <map>
#include <string>
#include <vector>

// エディタ内で共有されるコンテキストデータ
struct EditorContext {
    CCL::ECS::Core::World *world          = nullptr;
    CCL::ECS::SystemManager *systemManager  = nullptr;
    CCL::ECS::EntityID    *selectedEntity = nullptr;

    // Prefabリスト (AssetBrowserなどが使う)
    std::vector<std::string> prefabFiles;

    // Hierarchy表示用の名前キャッシュ
    std::map<CCL::ECS::EntityID, std::string> entityNames;

    // GUI描画ヘルパー
    ComponentGuiRegistry guiRegistry;

    // シーンロード予約パス
    std::string pendingLoadScenePath;

     AssetBrowser *assetBrowser = nullptr; // アセットへのアクセス

    // アニメーションエディタとの連携用
    bool  *isAnimEditMode = nullptr; // 編集モードかどうか
    float *animEditTime   = nullptr; // 現在のシーケンス時間
};