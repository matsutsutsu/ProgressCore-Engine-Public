#include "GameViewWindow.h"
#include "Engine/Graphics/RHI/Camera.h"
#include "Engine/Camera/Components/CameraDollyCartComponent.h"
#include "Engine/Camera/Systems/CameraDollySystem.h"
#include "Editor/Windows/CameraPathEditorWindow.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Platform/Core/Logger.h"

#include "Engine/Graphics/Renderer/ShapeRenderer.h"

// ピッキング用: スクリーン→レイ変換 と カリング用ワールドAABB
#include "Engine/Core/Math/ScreenToWorld.h"
#include "Engine/Graphics/Components/Culling/CullingComponents.h"
#include "Engine/UI/ECS/GameViewportInfo.h" // UIクリック判定へゲーム画像の表示矩形を橋渡し

#include <json.hpp>
#include <algorithm>
#include <cmath>
#include <cfloat>

#ifdef ENABLE_EDITOR
#include <ImGuizmo.h>
#endif

using namespace DirectX;
using namespace DirectX::SimpleMath;

// スプライン評価と LUT 構築は CameraDollySystem の public static を使う。
// ここへ複製しない。複製すると CameraDollySystem を直したときに、エディタの
// プレビュー線だけ古い計算のまま、という食い違いが起きる。
// JSON への書き出しも CameraPathEditorWindow::SavePathToJson の1か所だけを使う。
namespace
{
    /**
     * @brief レイと AABB で、最も手前に当たったエンティティを返す（ピッキング）。
     *
     * カリング用に各描画エンティティが持つ worldAABB（DirectX::BoundingBox）を使うので、追加のデータは要らない。
     * BoundingBoxComponent は ModelCullingSetupSystem が自動で付ける。
     *
     * @param world  探す World。
     * @param origin レイの始点（ワールド）。
     * @param dir    レイの向き。
     * @return 最も手前に当たったエンティティ。当たらなければ InvalidEntityID。
     */
    CCL::ECS::EntityID PickEntity(CCL::ECS::World &world,
                                  const Vector3 &origin, const Vector3 &dir)
    {
        using namespace CCL::ECS;

        const XMVECTOR o = XMLoadFloat3(&origin);
        const XMVECTOR d = XMLoadFloat3(&dir);

        EntityID best     = InvalidEntityID;
        float    bestDist = FLT_MAX;

        auto &chunks = world.GetChunkManager().GetChunks();
        for (auto &chunkPtr : chunks)
        {
            if (!chunkPtr) continue;
            Chunk *chunk = chunkPtr.get();
            if (!chunk->HasComponent<BoundingBoxComponent>()) continue;

            BoundingBoxComponent *boxes = chunk->GetComponentArray<BoundingBoxComponent>();
            VisibilityComponent  *vis   = chunk->HasComponent<VisibilityComponent>()
                                              ? chunk->GetComponentArray<VisibilityComponent>()
                                              : nullptr;

            const size_t count = chunk->GetEntityCount();
            for (size_t i = 0; i < count; ++i)
            {
                if (chunk->IsEntityDestroyed(i)) continue;
                if (vis && !vis[i].isVisible) continue; // 非表示は拾わない

                float dist = 0.0f;
                if (boxes[i].worldAABB.Intersects(o, d, dist) && dist < bestDist)
                {
                    bestDist = dist;
                    best     = chunk->GetEntityByIndex(i);
                }
            }
        }
        return best;
    }
} // namespace

void GameViewWindow::DrawContents(EditorContext &context)
{
    // この関数は ImGui::Begin("Game View") と End の間で呼ばれるので、
    //   ここでの IsWindowFocused は「ゲームビュー窓」のフォーカス状態を指す。
    //   これを EditorWindowManager が拾い、フォーカス中のみマウス視点を通す。
    context.gameViewFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    ImVec2 avail = ImGui::GetContentRegionAvail();

    // 16:9 を保ったまま、ウィンドウに収まる最大のサイズを求める
    const float targetAspect = 16.0f / 9.0f;
    float       windowAspect = avail.x / avail.y;
    ImVec2      imageSize    = (windowAspect > targetAspect)
                                   ? ImVec2(avail.y * targetAspect, avail.y)
                                   : ImVec2(avail.x, avail.x / targetAspect);

    // ウィンドウの中央に置く
    ImVec2 cursorPos = ImGui::GetCursorPos();
    ImVec2 drawPos   = ImVec2(
        cursorPos.x + (avail.x - imageSize.x) * 0.5f,
        cursorPos.y + (avail.y - imageSize.y) * 0.5f);
    ImGui::SetCursorPos(drawPos);

    // ImGuizmo 用にスクリーン座標を取得
    ImVec2 screenPos = ImGui::GetCursorScreenPos();

    if (context.gameSRV)
    {
        ImGui::Image(context.gameSRV, imageSize);

        // ゲーム画像の表示矩形を UIInputSystem へ橋渡し（UIクリック判定の座標写像用）。
        //   screenPos は ImGui スクリーン座標(マルチビューポートでは OS デスクトップ基準)なので、
        //   メインビューポート原点を引いて「メインウィンドウのクライアント座標」へ直す。
        //   これはプラットフォーム Mouse::GetPositionX/Y と同じ空間。
        if (context.world && context.world->HasResource<GameViewportInfo>())
        {
            const ImVec2      mainPos = ImGui::GetMainViewport()->Pos;
            GameViewportInfo &vp      = context.world->GetResource<GameViewportInfo>();
            vp.x                      = screenPos.x - mainPos.x;
            vp.y                      = screenPos.y - mainPos.y;
            vp.w                      = imageSize.x;
            vp.h                      = imageSize.y;
            vp.valid                  = true;
            // キーボードをゲームへ渡すのは、GameView を触っていて、UI 操作モード（F2）でないときだけ
            vp.keyboardFocus = context.gameViewFocused && !context.cursorFreeToggle && !context.isAnimEditMode;
        }
    }
    else
    {
        ImGui::Text("No Render Target Available.");
        return; // 描画先が無いならギズモ系もスキップ
    }

#ifdef ENABLE_EDITOR
    // ============================================================
    // カメラ台車の軌道の表示と、制御点のギズモ操作
    // ============================================================
    if (context.world && context.selectedEntity != CCL::ECS::InvalidEntityID)
    {
        auto *cart = context.world->GetComponent<CameraDollyCartComponent>(context.selectedEntity);
        if (cart && cart->loadedPath)
        {
            const auto &asset = *cart->loadedPath;

            // ---- 軌道と制御点の表示 ----
            ShapeRenderer *shapeRenderer = nullptr;
            if (context.world->HasResource<ShapeRenderer *>())
            {
                shapeRenderer = context.world->GetResource<ShapeRenderer *>();
            }

            if (shapeRenderer)
            {
                // スプライン曲線の描画（LUTに従って細かく線を引くことで滑らかなカーブを表現）
                if (!asset.lut.empty())
                {
                    for (size_t i = 0; i < asset.lut.size() - 1; ++i)
                    {
                        Vector3 p0 = CameraDollySystem::EvaluateCatmullRom(asset.points, asset.lut[i].t, asset.isLoop);
                        Vector3 p1 = CameraDollySystem::EvaluateCatmullRom(asset.points, asset.lut[i + 1].t, asset.isLoop);
                        // スプラインは黄色で描画
                        shapeRenderer->DrawLine(p0, p1, XMFLOAT4(1.0f, 1.0f, 0.0f, 1.0f));
                    }
                }

                // 制御点（ウェイポイント）の描画
                for (int i = 0; i < asset.points.size(); ++i)
                {
                    // 選択中のノードは赤、それ以外はシアン
                    XMFLOAT4 nodeColor = (i == context.editingPathNodeIndex)
                                             ? XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f)
                                             : XMFLOAT4(0.0f, 1.0f, 1.0f, 1.0f);
                    shapeRenderer->DrawSphere(asset.points[i], 0.2f, nodeColor);
                }
            }

            // ---- 選択中の制御点のギズモ操作（選択は CameraPathEditorWindow が行う） ----
            int idx = context.editingPathNodeIndex;
            if (idx >= 0 && idx < asset.points.size())
            {
                Camera *camera = context.world->GetResource<Camera *>();
                if (camera)
                {
                    ImGuizmo::SetOrthographic(false);
                    ImGuizmo::SetDrawlist();
                    ImGuizmo::SetRect(screenPos.x, screenPos.y, imageSize.x, imageSize.y);

                    Matrix viewMat = camera->GetView();
                    Matrix projMat = camera->GetProjection();

                    Vector3 &targetPt     = cart->loadedPath->points[idx];
                    Matrix   transformMat = Matrix::CreateTranslation(targetPt);

                    ImGuizmo::Manipulate(
                        reinterpret_cast<float *>(&viewMat),
                        reinterpret_cast<float *>(&projMat),
                        context.currentGizmoOperation,
                        context.currentGizmoMode,
                        reinterpret_cast<float *>(&transformMat));

                    // 操作中は座標だけを更新し、LUT の再構築と保存は操作を終えた瞬間に1回だけ行う
                    static bool wasUsingGizmo = false;
                    bool        isUsingGizmo  = ImGuizmo::IsUsing();

                    if (isUsingGizmo)
                    {
                        // ドラッグ中は座標だけを更新し、軌道の表示にすぐ反映させる。
                        // LUT（距離→t の対応表）の再計算は重いのでここでは行わない
                        targetPt = transformMat.Translation();
                    }
                    else if (wasUsingGizmo && !isUsingGizmo)
                    {
                        // マウスを離した瞬間に LUT を再構築し、JSON に上書き保存する
                        CameraDollySystem::BuildLookupTable(*cart->loadedPath);
                        CameraPathEditorWindow::SavePathToJson(cart->pathAsset, *cart->loadedPath);
                    }

                    wasUsingGizmo = isUsingGizmo;
                }
            }
        }
    }

    // ============================================================
    // クリックによるエンティティの選択（ピッキング）
    //   ・一時停止中だけ有効。実行中はクリックをゲームの入力に回すため選択しない。
    //   ・ゲーム画像の左上からの座標（local）で判定するので、ウィンドウの位置や
    //     内部解像度に依存しない。
    //   ・ギズモの操作中・ホバー中は選択しない。ドラッグ（カメラ回転等）の後のリリースも除外する。
    // ============================================================
    if (context.isPaused && context.world &&
        context.world->HasResource<Camera *>() &&
        ImGui::IsWindowHovered() &&
        !ImGuizmo::IsOver() && !ImGuizmo::IsUsing())
    {
        const ImVec2 mouse = ImGui::GetMousePos();
        const ImVec2 local = ImVec2(mouse.x - screenPos.x, mouse.y - screenPos.y);
        const bool   insideImage =
            local.x >= 0.0f && local.y >= 0.0f &&
            local.x <= imageSize.x && local.y <= imageSize.y;

        if (insideImage && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            // ドラッグ(カメラ操作)後のリリースは選択に使わない (約5px以上動いたら無視)
            const ImVec2 dd      = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
            const bool   wasDrag = (dd.x * dd.x + dd.y * dd.y) > 25.0f;

            if (!wasDrag)
            {
                Camera *camera = context.world->GetResource<Camera *>();
                Vector3 rayOrigin, rayDir;
                ScreenToWorld::GetRay(local.x, local.y, imageSize.x, imageSize.y,
                                      *camera, rayOrigin, rayDir);

                const CCL::ECS::EntityID hit = PickEntity(*context.world, rayOrigin, rayDir);

                ImGuiIO &io = ImGui::GetIO();
                if (hit != CCL::ECS::InvalidEntityID)
                {
                    if (io.KeyCtrl)
                        context.ToggleSelection(hit); // 選択に追加 / 選択から外す
                    else
                        context.SetSingleSelection(hit);
                }
                else if (!io.KeyCtrl)
                {
                    // 何もない場所を通常クリックしたら選択を解除する
                    context.ClearSelection();
                }
            }
        }
    }
#endif
}