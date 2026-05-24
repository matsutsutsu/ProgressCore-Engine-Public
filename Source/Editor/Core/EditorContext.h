#pragma once
#include "ECS/Core/CCL_World.h"
#include "ECS/System/CCL_SystemManager.h"
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include <map>
#include <string>
#include <vector>

#if ENABLE_EDITOR
#include <ImGuizmo.h>
#endif

// ゲームの再生状態
enum class EditorPlayState {
    Stopped, // 編集モード（時間は止まっている）
    Playing, // 再生モード（ゲーム進行中）
    Paused   // 一時停止モード（コマ送りやデバッグ用）
};

// エディタ内で共有されるコンテキストデータ
struct EditorContext {
    CCL::ECS::Core::World *world          = nullptr;
    CCL::ECS::SystemManager *systemManager  = nullptr;
    CCL::ECS::EntityID       selectedEntity = CCL::ECS::InvalidEntityID;

    // Prefabリスト (AssetBrowserなどが使う)
    std::vector<std::string> prefabFiles;

    // Hierarchy表示用の名前キャッシュ
    std::map<CCL::ECS::EntityID, std::string> entityNames;

    // シーンロード予約パス
    std::string pendingLoadScenePath;

    // シーンセーブ予約パス
    std::string pendingSaveScenePath;

    // 再生状態とバックアップ用パス
    EditorPlayState playState = EditorPlayState::Playing;
    std::string tempBackupScenePath = "Assets/Scene/_TempPlayBackup.json";

    // アニメーションエディタとの連携用
     bool  isAnimEditMode = false; // これらもポインタではなく実体値にするのが安全です
     float animEditTime   = 0.0f;

#if ENABLE_EDITOR
     // ★ ImGuizmo用の状態管理
     ImGuizmo::OPERATION currentGizmoOperation = ImGuizmo::TRANSLATE;
     ImGuizmo::MODE      currentGizmoMode = ImGuizmo::LOCAL;
#endif

     // ★ ImGuiに渡すためのゲーム画面テクスチャ
     void* gameSRV = nullptr;
};