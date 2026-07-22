/// @file GameViewWindow.cpp

#include "GameViewWindow.h"
#include "Engine/Graphics/RHI/Camera.h"
#include "Engine/Camera/Components/CameraDollyCartComponent.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Platform/Core/Logger.h"

// ★ ShapeRenderer のインクルード
#include "Engine/Graphics/Renderer/ShapeRenderer.h"

// ★ ピッキング用: スクリーン→レイ変換 と カリング用ワールドAABB
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

// =========================================================================
// ローカルヘルパー群 (エディタ描画と保存を自己完結させるための一時的配置)
// =========================================================================
namespace
{
const DirectX::SimpleMath::Vector3 &GetPointClamped(const std::vector<Vector3> &pts, int index)
{
    int clamped = std::clamp(index, 0, static_cast<int>(pts.size()) - 1);
    return pts[clamped]; // 実体への参照が返るため、&演算子でのアドレス取得が合法になる
}

Vector3 EvaluateCatmullRom(const std::vector<Vector3> &pts, float t)
{
    if (pts.empty())
        return Vector3::Zero;
    if (pts.size() == 1)
        return pts[0];

    int   index = static_cast<int>(std::floor(t));
    float frac  = t - std::floor(t);

    XMVECTOR p0 = XMLoadFloat3(&GetPointClamped(pts, index - 1));
    XMVECTOR p1 = XMLoadFloat3(&GetPointClamped(pts, index));
    XMVECTOR p2 = XMLoadFloat3(&GetPointClamped(pts, index + 1));
    XMVECTOR p3 = XMLoadFloat3(&GetPointClamped(pts, index + 2));

    XMVECTOR result = XMVectorCatmullRom(p0, p1, p2, p3, frac);

    Vector3 finalPos;
    XMStoreFloat3(&finalPos, result);
    return finalPos;
}

// ギズモで点が動かされた後、等速移動用の LUT を再構築する
void BuildLookupTable(CameraPathAsset &asset)
{
    asset.lut.clear();
    if (asset.points.size() < 2)
        return;

    int   resolutionPerSegment = 20;
    int   totalSamples         = static_cast<int>(asset.points.size() - 1) * resolutionPerSegment;
    float maxT                 = static_cast<float>(asset.points.size() - 1);

    asset.lut.push_back({0.0f, 0.0f});
    Vector3 prevPos             = EvaluateCatmullRom(asset.points, 0.0f);
    float   accumulatedDistance = 0.0f;

    for (int i = 1; i <= totalSamples; ++i)
    {
        float   t       = (static_cast<float>(i) / totalSamples) * maxT;
        Vector3 currPos = EvaluateCatmullRom(asset.points, t);
        accumulatedDistance += Vector3::Distance(prevPos, currPos);
        asset.lut.push_back({accumulatedDistance, t});
        prevPos = currPos;
    }
    asset.totalLength = accumulatedDistance;
}

// =========================================================================
// レイ-AABB による最近傍エンティティ選択(ピッキング)
//   カリング用に各描画エンティティが持つ worldAABB(DirectX::BoundingBox)へ
//   レイを飛ばし、最も手前でヒットしたエンティティを返す。
//   ※ 追加データ不要。BoundingBoxComponent は ModelCullingSetupSystem が自動付与する。
// =========================================================================
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

// エディタからの自動保存用
void SavePathAssetToJson(const std::string &path, const std::vector<Vector3> &points)
{
    if (path.empty())
        return;
    nlohmann::json j;
    j["points"] = nlohmann::json::array();
    for (const auto &p : points)
    {
        j["points"].push_back({{"x", p.x}, {"y", p.y}, {"z", p.z}});
    }
    FileSystem::WriteAllText(path, j.dump(4));
    CCL_LOG_INFO(LogCategory::Editor, "Camera Path Auto-Saved and LUT Rebuilt: %s", path.c_str());
}
} // namespace

// =========================================================================
// GameViewWindow メイン描画
// =========================================================================
void GameViewWindow::DrawContents(EditorContext &context)
{
    // ★ この関数は ImGui::Begin("Game View") と End の間で呼ばれるので、
    //   ここでの IsWindowFocused は「ゲームビュー窓」のフォーカス状態を指す。
    //   これを EditorWindowManager が拾い、フォーカス中のみマウス視点を通す。
    context.gameViewFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    ImVec2 avail = ImGui::GetContentRegionAvail();

    // 16:9 のアスペクト比を維持してピッタリ収まるサイズを計算
    const float targetAspect = 16.0f / 9.0f;
    float       windowAspect = avail.x / avail.y;
    ImVec2      imageSize    = (windowAspect > targetAspect)
                                   ? ImVec2(avail.y * targetAspect, avail.y)
                                   : ImVec2(avail.x, avail.x / targetAspect);

    // ウィンドウ内で中央に配置するための計算
    ImVec2 cursorPos = ImGui::GetCursorPos();
    ImVec2 drawPos   = ImVec2(
        cursorPos.x + (avail.x - imageSize.x) * 0.5f,
        cursorPos.y + (avail.y - imageSize.y) * 0.5f);
    ImGui::SetCursorPos(drawPos);

    // ImGuizmo 用にスクリーン座標を取得
    ImVec2 screenPos = ImGui::GetCursorScreenPos();

    // 描画先テクスチャの描画
    if (context.gameSRV)
    {
        ImGui::Image(context.gameSRV, imageSize);

        // ★ ゲーム画像の表示矩形を UIInputSystem へ橋渡し（UIクリック判定の座標写像用）。
        //   screenPos は ImGui スクリーン座標(マルチビューポートでは OS デスクトップ基準)なので、
        //   メインビューポート原点を引いて「メインウィンドウのクライアント座標」へ直す。
        //   これはプラットフォーム Mouse::GetPositionX/Y と同じ空間。
        if (context.world && context.world->HasResource<GameViewportInfo>())
        {
            const ImVec2 mainPos = ImGui::GetMainViewport()->Pos;
            GameViewportInfo &vp = context.world->GetResource<GameViewportInfo>();
            vp.x     = screenPos.x - mainPos.x;
            vp.y     = screenPos.y - mainPos.y;
            vp.w     = imageSize.x;
            vp.h     = imageSize.y;
            vp.valid = true;
        }
    }
    else
    {
        ImGui::Text("No Render Target Available.");
        return; // 描画先が無いならギズモ系もスキップ
    }

#ifdef ENABLE_EDITOR
    // =========================================================================
    // インゲーム・パスエディタ (軌道描画 & ImGuizmo連携)
    // =========================================================================
    if (context.world && context.selectedEntity != CCL::ECS::InvalidEntityID)
    {

        auto *cart = context.world->GetComponent<CameraDollyCartComponent>(context.selectedEntity);
        if (cart && cart->loadedPath)
        {
            const auto &asset = *cart->loadedPath;

            // -----------------------------------------------------------------
            // [Phase 1] 軌道と制御点の可視化
            // -----------------------------------------------------------------
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
                        Vector3 p0 = EvaluateCatmullRom(asset.points, asset.lut[i].t);
                        Vector3 p1 = EvaluateCatmullRom(asset.points, asset.lut[i + 1].t);
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

            // -----------------------------------------------------------------
            // [Phase 2] ギズモ操作とデータエッジ同期
            // -----------------------------------------------------------------
            int idx = context.editingPathNodeIndex;
            if (idx >= 0 && idx < asset.points.size())
            {

                Camera *camera = context.world->GetResource<Camera *>();
                if (camera)
                {
                    ImGuizmo::SetOrthographic(false);
                    ImGuizmo::SetDrawlist();
                    ImGuizmo::SetRect(screenPos.x, screenPos.y, imageSize.x, imageSize.y);

                    // ※エンジンのCamera仕様に合わせて GetViewMatrix() 等に変更済み
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

                    // ★ エッジトリガー：操作中の処理と終了時の重い処理の分離
                    static bool wasUsingGizmo = false;
                    bool        isUsingGizmo  = ImGuizmo::IsUsing();

                    if (isUsingGizmo)
                    {
                        // ドラッグ中：座標を更新してShapeRendererの描画に即時反映させる
                        // ただしLUT(等速移動データ)の再計算は重いのでここではやらない
                        targetPt = transformMat.Translation();
                    }
                    else if (wasUsingGizmo && !isUsingGizmo)
                    {
                        // ★ マウスを離した瞬間 (立ち下がりエッジ)
                        // ここで初めてLUTを再構築し、JSONアセットとして上書き保存する
                        BuildLookupTable(*cart->loadedPath);
                        SavePathAssetToJson(cart->pathAsset, cart->loadedPath->points);
                    }

                    wasUsingGizmo = isUsingGizmo;
                }
            }
        }
    }

    // =========================================================================
    // クリックによるオブジェクト選択 (ピッキング)
    //   ・一時停止中(=編集中)のみ有効。プレイ中はクリックをゲーム入力に回すため選択しない。
    //   ・GameView画像の相対座標(local)で判定するので、ウィンドウ位置や内部解像度に依存しない
    //     (= GameView越しでもクリック座標がズレない)。
    //   ・ギズモ操作中/ホバー中は誤選択しない。ドラッグ(カメラ回転等)後のリリースも除外。
    // =========================================================================
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
                    if (io.KeyCtrl) context.ToggleSelection(hit); // 追加/解除
                    else            context.SetSingleSelection(hit);
                }
                else if (!io.KeyCtrl)
                {
                    // 何もない場所を通常クリック → 選択解除
                    context.ClearSelection();
                }
            }
        }
    }
#endif
}