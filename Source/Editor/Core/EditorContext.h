#pragma once
// ===========================================================================
// 概要: エディタの各ウィンドウが共有する状態（EditorContext）。
//
// 責務:
//   - 対象の World / SystemManager を指す（所有しない）
//   - エンティティの選択（主選択と複数選択）と、その操作
//   - シーン・レベルの読み込み／保存の予約、一時停止・コマ送りの要求
//   - ウィンドウ間の連携用の値（ギズモ・パス編集・ゲームビューのフォーカスなど）
//
// 備考: 読み込み・保存は Framework が安全なタイミングで実行するので、ここにはパスを
//       予約するだけにする。
// ===========================================================================
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

struct EditorContext
{
    CCL::ECS::World         *world         = nullptr; // 編集対象の World（所有しない）
    CCL::ECS::SystemManager *systemManager = nullptr; // その World の SystemManager（所有しない）

    // 主選択。Inspector・ギズモ・各種エディタが「今フォーカス中の1体」として参照する。
    //   複数選択時は「最後に触れた1体」を指す。
    CCL::ECS::EntityID selectedEntity = CCL::ECS::InvalidEntityID;

    // 選択セット（複数選択）。selectedEntity は常にこの中の1つ（主選択）。
    //   ハイライトや一括操作（複製・削除）はこのセット全体を対象にする。
    //   単一選択時は要素1、未選択時は空。
    std::vector<CCL::ECS::EntityID> selectedEntities;

    // ---- 選択の操作（選択の変更は必ずこれらを通す） ----
    bool IsSelected(CCL::ECS::EntityID e) const
    {
        for (auto id : selectedEntities)
            if (id == e) return true;
        return false;
    }

    void ClearSelection()
    {
        selectedEntities.clear();
        selectedEntity = CCL::ECS::InvalidEntityID;
    }

    // 単一選択に置き換える（通常のクリック）
    void SetSingleSelection(CCL::ECS::EntityID e)
    {
        selectedEntities.clear();
        if (e != CCL::ECS::InvalidEntityID) selectedEntities.push_back(e);
        selectedEntity = e;
    }

    // セットへ追加する（既にあれば主選択にするだけ）
    void AddSelection(CCL::ECS::EntityID e)
    {
        if (e == CCL::ECS::InvalidEntityID) return;
        if (!IsSelected(e)) selectedEntities.push_back(e);
        selectedEntity = e; // 最後に触れたものを主選択にする
    }

    // 選択を反転する（Ctrl+クリック）
    void ToggleSelection(CCL::ECS::EntityID e)
    {
        if (e == CCL::ECS::InvalidEntityID) return;
        for (size_t i = 0; i < selectedEntities.size(); ++i)
        {
            if (selectedEntities[i] == e)
            {
                selectedEntities.erase(selectedEntities.begin() + i);
                // 主選択が外れたら、残りの末尾を主選択にする
                if (selectedEntity == e)
                    selectedEntity = selectedEntities.empty()
                                         ? CCL::ECS::InvalidEntityID
                                         : selectedEntities.back();
                return;
            }
        }
        selectedEntities.push_back(e);
        selectedEntity = e;
    }

    // 選択から取り除く（エンティティの削除時など）
    void RemoveFromSelection(CCL::ECS::EntityID e)
    {
        for (size_t i = 0; i < selectedEntities.size(); ++i)
        {
            if (selectedEntities[i] == e)
            {
                selectedEntities.erase(selectedEntities.begin() + i);
                break;
            }
        }
        if (selectedEntity == e)
            selectedEntity = selectedEntities.empty()
                                 ? CCL::ECS::InvalidEntityID
                                 : selectedEntities.back();
    }

    // 一括操作（複製・削除）用。選択セットから「祖先も選択されている」ものを除いた
    // 集合を返す。親を操作すれば子も一緒に処理されるので、二重に処理しないようにする。
    //   guard は親子関係が循環していた場合に止めるための上限。
    std::vector<CCL::ECS::EntityID> GetTopLevelSelection() const
    {
        std::vector<CCL::ECS::EntityID> result;
        if (!world) return result;
        for (CCL::ECS::EntityID id : selectedEntities)
        {
            bool  ancestorSelected = false;
            auto *t                = world->GetComponent<TransformComponent>(id);
            int   guard            = 0;
            while (t && t->parent.IsValid() && guard++ < 4096)
            {
                CCL::ECS::EntityID p = t->parent.id;
                if (IsSelected(p))
                {
                    ancestorSelected = true;
                    break;
                }
                t = world->GetComponent<TransformComponent>(p);
            }
            if (!ancestorSelected) result.push_back(id);
        }
        return result;
    }

    // Assets/Prefabs 以下のプレハブのパス。メインメニューの Prefab メニューが使う
    std::vector<std::string> prefabFiles;

    // InspectorWindow が名前を編集・追加したときに控える名前。
    //   NameComponent を持たないエンティティの、プレハブ保存名の初期値に使う
    std::map<CCL::ECS::EntityID, std::string> entityNames;

    // シーン（ステージ層）の読み込み・保存の予約パス。空なら予約なし
    std::string pendingLoadScenePath;
    std::string pendingSaveScenePath;

    // レベル（ステージ＋プレハブ参照）の読み込み・保存の予約パス。
    //   シーンと同じく、安全なタイミングで Framework が実行する（遅延実行）。
    std::string pendingLoadLevelPath;
    std::string pendingSaveLevelPath;

    // 一時停止。
    //   true の間だけゲームロジック(FixedUpdate)の進行を止める。
    //   セーブ・ロードやシーンのバックアップは行わない。起動時は false（実行中）。
    bool isPaused = false;

    // コマ送り要求。一時停止中に true にすると、次の1フレームだけ物理を進めて
    //   すぐ false に戻る（Framework が消費）。停止中の1フレーム送り用。
    bool stepOneFrame = false;

    // 「カーソル自由(UI操作)モード」の手動切り替えの状態（F2）。
    //   これ または isAnimEditMode が true のとき、GameFlowState.uiInteract が立ち、
    //   ゲームを止めずにカーソルが解放される（実行中に ImGui を操作できる）。
    bool cursorFreeToggle = false;

    // アニメーションシーケンサーの Edit Mode（プレビュー）
    bool  isAnimEditMode = false; // true の間、選択中のエンティティのアニメを手で送る
    float animEditTime   = 0.0f;  // プレビュー中の再生位置（秒）

#ifdef ENABLE_EDITOR
    // ImGuizmo の操作（移動・回転・拡縮）と座標系（ローカル・ワールド）
    ImGuizmo::OPERATION currentGizmoOperation = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE      currentGizmoMode      = ImGuizmo::LOCAL;
#endif

    // カメラパスエディタで選択中の制御点。GameViewWindow が強調表示する。-1 なら未選択
    int editingPathNodeIndex = -1;

    // ゲーム画面のテクスチャ（ID3D11ShaderResourceView*。所有しない）。ImGui に渡して描く
    void *gameSRV = nullptr;

    // ゲームビュー(ImGuiウィンドウ)にフォーカスがあるか。
    //   GameViewWindow が毎フレーム更新し、EditorWindowManager が
    //   「全画面 or フォーカス中」を InputFacade へ伝えてマウス視点の可否を切り替える。
    bool gameViewFocused = false;

    // 切り離した OS ウィンドウ(ImGuiマルチビューポート)を実際に描画するか。
    //   true の間だけ Exe 外へドラッグしたウィンドウが表示・操作できる。
    //   各外部ウィンドウは個別スワップチェーンを毎フレーム Present するため、
    //   負荷が気になる場面では false にして描画を止められる。既定は有効。
    bool renderExternalWindows = true;

    // 切り離した OS ウィンドウを、操作していない間だけ低頻度で描くか。
    //   触っていない窓をゲームと同じ 60fps で Present し続けるのをやめる。
    //   プレイ中に別画面の数値を滑らかに眺めたいときは false にする。既定は有効。
    bool throttleExternalWindows = true;
};