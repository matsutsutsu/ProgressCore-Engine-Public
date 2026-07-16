#pragma once
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemManager.h"
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include <map>
#include <string>
#include <vector>

#ifdef ENABLE_EDITOR
#include <ImGuizmo.h>
#endif

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

    // ★ 一時停止トグル。
    //   true の間だけゲームロジック(FixedUpdate)の進行を止める「ただの一時停止」。
    //   セーブ/ロードやシーンのバックアップは一切行わない。
    //   起動時は false（＝従来通り実行状態）で始まる。
    bool isPaused = false;

    // ★ コマ送り要求。一時停止中に true にすると、次の1フレームだけ物理を進めて
    //   すぐ false に戻る（Framework が消費）。停止中の1フレーム送り用。
    bool stepOneFrame = false;

    // アニメーションエディタとの連携用
     bool  isAnimEditMode = false; // これらもポインタではなく実体値にするのが安全です
     float animEditTime   = 0.0f;

#ifdef ENABLE_EDITOR
     // ★ ImGuizmo用の状態管理
     ImGuizmo::OPERATION currentGizmoOperation = ImGuizmo::TRANSLATE;
     ImGuizmo::MODE      currentGizmoMode = ImGuizmo::LOCAL;
#endif

     // ★ パスエディタと3Dビューポートの連携用
     int editingPathNodeIndex = -1; // -1 なら何も選択していない

     // ★ ImGuiに渡すためのゲーム画面テクスチャ
     void* gameSRV = nullptr;

     // ★ ゲームビュー(ImGuiウィンドウ)にフォーカスがあるか。
     //   GameViewWindow が毎フレーム更新し、EditorWindowManager が
     //   「全画面 or フォーカス中」を InputFacade へ伝えてマウス視点の可否を切り替える。
     bool gameViewFocused = false;
};