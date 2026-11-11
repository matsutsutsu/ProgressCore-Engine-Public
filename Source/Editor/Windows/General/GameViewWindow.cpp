#include "Editor/Windows/General/GameViewWindow.h"
#include "Engine/Graphics/SceneView/Camera.h"
#include "Engine/ECS/Core/CCL_World.h"

// ピッキング用: スクリーン→レイ変換 と カリング用ワールドAABB
#include "Engine/Core/Math/ScreenToWorld.h"
#include "Engine/Graphics/Components/Culling/CullingComponents.h"
#include "Engine/UI/ECS/GameViewportInfo.h"

#include <algorithm>
#include <cmath>
#include <cfloat>

#ifdef ENABLE_EDITOR
#include <ImGuizmo.h>
#endif

using namespace DirectX;
using namespace DirectX::SimpleMath;

namespace
{
    /**
     * @brief レイと AABB で、最も手前に当たったエンティティを返す（ピッキング）
     *
     * カリング用に各描画エンティティが持つ worldAABB（DirectX::BoundingBox）を使うので、追加のデータは要らない
     * BoundingBoxComponent は ModelCullingSetupSystem が自動で付ける
     * カメラを包んでいる箱（ステージ全体など）は拾わない。それらはヒエラルキーから選ぶ
     *
     * @param world  探す World
     * @param origin レイの始点（ワールド）
     * @param dir    レイの向き
     * @return 最も手前に当たったエンティティ。当たらなければ InvalidEntityID
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

                // カメラを包む箱は無視する
                //   始点が箱の中だと Intersects は負の dist を返し、何をクリックしてもその箱が最も手前になるため
                float dist = 0.0f;
                if (boxes[i].worldAABB.Intersects(o, d, dist) && dist > 0.0f && dist < bestDist)
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
    //   ここでの IsWindowFocused は「ゲームビュー窓」のフォーカス状態を指す
    //   これを EditorWindowManager が拾い、フォーカス中のみマウス視点を通す
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

        // ゲーム画像の表示矩形を UIInputSystem へ橋渡し（UIクリック判定の座標写像用）
        //   screenPos は ImGui スクリーン座標(マルチビューポートでは OS デスクトップ基準)なので、
        //   メインビューポート原点を引いて「メインウィンドウのクライアント座標」へ直す
        //   これはプラットフォーム Mouse::GetPositionX/Y と同じ空間
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
        return; // 描画先がないならギズモ系もスキップ
    }

#ifdef ENABLE_EDITOR
    // ============================================================
    // クリックによるエンティティの選択（ピッキング）
    //   ・一時停止中だけ有効。実行中はクリックをゲームの入力に回すため選択しない
    //   ・ゲーム画像の左上からの座標（local）で判定するので、ウィンドウの位置や
    //     内部解像度に依存しない
    //   ・ギズモの操作中・ホバー中は選択しない。ドラッグ（カメラ回転等）の後のリリースも除外する
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