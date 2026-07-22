#pragma once
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemManager.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include <map>
#include <string>
#include <vector>

#ifdef ENABLE_EDITOR
#include <ImGuizmo.h>
#endif

// エディタ内で共有されるコンテキストデータ
struct EditorContext {
    CCL::ECS::World *world          = nullptr;
    CCL::ECS::SystemManager *systemManager  = nullptr;

    // ★ アクティブ(主)選択。Inspector/Gizmo/各種エディタが「今フォーカス中の1体」
    //   として参照する。複数選択時は「最後に触れた1体」を指す。
    CCL::ECS::EntityID       selectedEntity = CCL::ECS::InvalidEntityID;

    // ★ 選択セット (複数選択)。selectedEntity は常にこの中の1つ(主選択)。
    //   ハイライトや一括操作(複製/削除)はこのセット全体を対象にする。
    //   単一選択時は要素1、未選択時は空。
    std::vector<CCL::ECS::EntityID> selectedEntities;

    // ---- 選択操作ヘルパー (選択の変更は必ずこれらを通す) --------------------
    bool IsSelected(CCL::ECS::EntityID e) const {
        for (auto id : selectedEntities) if (id == e) return true;
        return false;
    }

    void ClearSelection() {
        selectedEntities.clear();
        selectedEntity = CCL::ECS::InvalidEntityID;
    }

    // 単一選択に置き換える (通常クリック)
    void SetSingleSelection(CCL::ECS::EntityID e) {
        selectedEntities.clear();
        if (e != CCL::ECS::InvalidEntityID) selectedEntities.push_back(e);
        selectedEntity = e;
    }

    // セットへ追加 (既にあれば主選択に昇格するだけ)
    void AddSelection(CCL::ECS::EntityID e) {
        if (e == CCL::ECS::InvalidEntityID) return;
        if (!IsSelected(e)) selectedEntities.push_back(e);
        selectedEntity = e; // 最後に触れたものを主選択に
    }

    // トグル (Ctrl+クリック)
    void ToggleSelection(CCL::ECS::EntityID e) {
        if (e == CCL::ECS::InvalidEntityID) return;
        for (size_t i = 0; i < selectedEntities.size(); ++i) {
            if (selectedEntities[i] == e) {
                selectedEntities.erase(selectedEntities.begin() + i);
                // 主選択が外れたら、残りの末尾を主選択にする
                if (selectedEntity == e)
                    selectedEntity = selectedEntities.empty()
                        ? CCL::ECS::InvalidEntityID : selectedEntities.back();
                return;
            }
        }
        selectedEntities.push_back(e);
        selectedEntity = e;
    }

    // 選択から取り除く (削除時などに使用)
    void RemoveFromSelection(CCL::ECS::EntityID e) {
        for (size_t i = 0; i < selectedEntities.size(); ++i) {
            if (selectedEntities[i] == e) {
                selectedEntities.erase(selectedEntities.begin() + i);
                break;
            }
        }
        if (selectedEntity == e)
            selectedEntity = selectedEntities.empty()
                ? CCL::ECS::InvalidEntityID : selectedEntities.back();
    }

    // 一括操作(複製/削除)用: 選択セットから「祖先も選択されている」ものを除いた
    // トップレベル集合を返す。親を操作すれば子も巻き込まれるため二重処理を防ぐ。
    std::vector<CCL::ECS::EntityID> GetTopLevelSelection() const {
        std::vector<CCL::ECS::EntityID> result;
        if (!world) return result;
        for (CCL::ECS::EntityID id : selectedEntities) {
            bool ancestorSelected = false;
            auto* t = world->GetComponent<TransformComponent>(id);
            int guard = 0;
            while (t && t->parent.IsValid() && guard++ < 4096) {
                CCL::ECS::EntityID p = t->parent.id;
                if (IsSelected(p)) { ancestorSelected = true; break; }
                t = world->GetComponent<TransformComponent>(p);
            }
            if (!ancestorSelected) result.push_back(id);
        }
        return result;
    }

    // Prefabリスト (AssetBrowserなどが使う)
    std::vector<std::string> prefabFiles;

    // Hierarchy表示用の名前キャッシュ
    std::map<CCL::ECS::EntityID, std::string> entityNames;

    // シーンロード予約パス
    std::string pendingLoadScenePath;

    // シーンセーブ予約パス
    std::string pendingSaveScenePath;

    // レベル(ステージ+プレハブ参照)のロード/セーブ予約パス。
    //   シーンと同じく「安全な瞬間」に Framework が消化する(遅延実行)。
    std::string pendingLoadLevelPath;
    std::string pendingSaveLevelPath;

    // ★ 一時停止トグル。
    //   true の間だけゲームロジック(FixedUpdate)の進行を止める「ただの一時停止」。
    //   セーブ/ロードやシーンのバックアップは一切行わない。
    //   起動時は false（＝従来通り実行状態）で始まる。
    bool isPaused = false;

    // ★ コマ送り要求。一時停止中に true にすると、次の1フレームだけ物理を進めて
    //   すぐ false に戻る（Framework が消費）。停止中の1フレーム送り用。
    bool stepOneFrame = false;

    // ★ 「カーソル自由(UI操作)モード」の手動トグル状態（F2）。
    //   これ または isAnimEditMode が true のとき、GameFlowState.uiInteract が立ち、
    //   ゲームを止めずにカーソルが解放される（実行中に ImGui を操作できる）。
    bool cursorFreeToggle = false;

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

     // ★ 切り離した OS ウィンドウ(ImGuiマルチビューポート)を実際に描画するか。
     //   true の間だけ Exe 外へドラッグしたウィンドウが表示・操作できる。
     //   各外部ウィンドウは個別スワップチェーンを毎フレーム Present するため、
     //   負荷が気になる場面では false にして描画を止められる。既定は有効。
     bool renderExternalWindows = true;
};