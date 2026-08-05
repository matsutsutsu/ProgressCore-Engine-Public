/// @file  AIDebugOverlaySystem.cpp
/// @brief 敵AIの状態（戦術/行動/ステータス）をキャラ頭上に3Dテキストで表示する。
///        描画は ImGui::NewFrame 後に走る「レンダーステージの Update」で行うため、
///        System Manager のヘッダ開閉に依存せず m_enabled の間ずっと表示される。
///        位置は GameViewportInfo（エディタのゲーム画像矩形）で正しく写像する。
#include "AIDebugOverlaySystem.h"

#include "Game/Logics/AI/BehaviorTree/Data/ActionRegistry.h"
#include "Game/Logics/AI/StateTree/Core/StateTreeRegistry.h" // 戦術状態名の実名解決
#include "Game/Logics/AI/StateTree/Core/StateTreeAsset.h"    // GetStateName
#include "Game/Logics/AI/Battle/BattleContext.h"             // ドローン残数
#include "Game/Logics/Combat/HealthComponent.h"              // ボスHP
#include "Engine/UI/ECS/GameViewportInfo.h"                  // ゲーム画像の画面矩形（位置ズレ対策）
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Graphics/RHI/Camera.h"

#include <SimpleMath.h>
#include <algorithm>

#ifdef ENABLE_EDITOR
#include <imgui.h>
#endif

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

// 頭上テキスト本体の描画（毎フレーム・NewFrame後のレンダーステージで呼ばれる）。
void AIDebugOverlaySystem::Update(float /*dt*/)
{
#ifdef ENABLE_EDITOR
    if (!m_enabled) return;

    const auto *camera = m_world->HasResource<Camera *>() ? m_world->GetResource<Camera *>() : nullptr;
    if (!camera) return;

    const Matrix viewProj = camera->GetView() * camera->GetProjection();

    // --- 投影先の矩形を決める ---
    //   エディタ: ゲームは GameView パネル内の部分矩形に画像表示される → その矩形へ写像。
    //   フルスクリーン/未取得: OS ウィンドウ全体へフォールバック。
    const ImVec2 mainPos = ImGui::GetMainViewport()->Pos;
    float ox, oy, vw, vh;
    if (m_world->HasResource<GameViewportInfo>() && m_world->GetResource<GameViewportInfo>().valid)
    {
        const auto &vp = m_world->GetResource<GameViewportInfo>();
        ox = mainPos.x + vp.x;
        oy = mainPos.y + vp.y;
        vw = vp.w;
        vh = vp.h;
    }
    else
    {
        const ImVec2 disp = ImGui::GetIO().DisplaySize;
        ox = mainPos.x;
        oy = mainPos.y;
        vw = disp.x;
        vh = disp.y;
    }
    if (vw <= 0.0f || vh <= 0.0f) return;

    ImDrawList *drawList = ImGui::GetForegroundDrawList();
    ImFont     *font     = ImGui::GetFont();

    auto view = m_world->View<StateTreeComponent, BehaviorTreeComponent, TransformComponent>();
    for (auto e : view)
    {
        auto *st    = m_world->GetComponent<StateTreeComponent>(e);
        auto *bt    = m_world->GetComponent<BehaviorTreeComponent>(e);
        auto *trans = m_world->GetComponent<TransformComponent>(e);
        if (!st || !bt || !trans) continue;

        // --- World to Screen 変換（頭上オフセット）。Transform(coord) は w 除算込み ---
        Vector3 worldPos = trans->position;
        worldPos.y += m_headOffsetY;
        Vector3 ndcPos = Vector3::Transform(worldPos, viewProj);
        if (ndcPos.z <= 0.0f || ndcPos.z >= 1.0f) continue; // 画面外・カメラ後方

        const float pixelX = ox + (ndcPos.x + 1.0f) * 0.5f * vw;
        const float pixelY = oy + (1.0f - ndcPos.y) * 0.5f * vh;

        // === 3層の状態をまとめて（戦術 / 行動 / ステータス）===

        // 1) 戦術（StateTree）：GetStateName で実名解決
        std::string tacticName = "-";
        if (st->activeDepth > 0 && m_world->HasResource<CCL::AI::StateTreeRegistry>())
        {
            auto &reg = m_world->GetResource<CCL::AI::StateTreeRegistry>();
            if (const auto *asset = reg.Get(st->assetHandle))
                tacticName = asset->GetStateName(st->GetLeafState());
        }
        char line1[80];
        snprintf(line1, sizeof(line1), "[%s]", tacticName.c_str());

        // 2) 行動（BT）：現在実行中アクション名を辞書から逆引き
        std::string actionName = "Idle";
        for (int i = 0; i < g_ActionRegistryCount; ++i)
        {
            if (g_ActionRegistry[i].id == bt->lastLogActionId)
            {
                actionName = g_ActionRegistry[i].name;
                break;
            }
        }

        // 3) ステータス：ボスHP と ドローン残数（両者が一致しているかを目視で確認する）
        char line3[96] = "";
        {
            const auto *hp     = m_world->GetComponent<HealthComponent>(e);
            const int   drones = m_world->HasResource<BattleContext>()
                                     ? m_world->GetResource<BattleContext>().droneAliveCount
                                     : -1;
            if (hp && drones >= 0)
                snprintf(line3, sizeof(line3), "HP %.0f/%.0f  Drone %d",
                         hp->currentHealth, hp->maxHealth, drones);
            else if (hp)
                snprintf(line3, sizeof(line3), "HP %.0f/%.0f", hp->currentHealth, hp->maxHealth);
        }

        struct DbgLine { const char *text; ImU32 col; };
        const DbgLine lines[3] = {
            {line1,               IM_COL32(255, 200,  80, 255)},  // 戦術：オレンジ
            {actionName.c_str(),  IM_COL32(255, 255, 255, 255)},  // 行動：白
            {line3,               IM_COL32(150, 235, 150, 255)},  // ステータス：緑
        };
        const int lineCount = (line3[0] != '\0') ? 3 : 2;

        // --- 描画（アウトライン付き・中央揃え・N行対応）---
        ImVec2 sizes[3];
        float  totalH = 0.0f, maxW = 0.0f;
        for (int i = 0; i < lineCount; ++i)
        {
            sizes[i] = font->CalcTextSizeA(m_fontSize, FLT_MAX, 0.0f, lines[i].text);
            totalH += sizes[i].y;
            maxW = (std::max)(maxW, sizes[i].x);
        }

        const float topY     = pixelY - totalH * 0.5f;
        const float paddingX = m_fontSize * 0.3f;
        const float paddingY = m_fontSize * 0.15f;
        drawList->AddRectFilled(
            ImVec2(pixelX - maxW * 0.5f - paddingX, topY - paddingY),
            ImVec2(pixelX + maxW * 0.5f + paddingX, topY + totalH + paddingY),
            IM_COL32(0, 0, 0, 160), 6.0f);

        const ImU32 outlineCol = IM_COL32(0, 0, 0, 255);
        auto        DrawOutlined = [&](float x, float y, ImU32 col, const char *text) {
            static const float offs[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
            for (auto &o : offs) drawList->AddText(font, m_fontSize, ImVec2(x + o[0], y + o[1]), outlineCol, text);
            drawList->AddText(font, m_fontSize, ImVec2(x, y), col, text);
        };

        float y = topY;
        for (int i = 0; i < lineCount; ++i)
        {
            DrawOutlined(pixelX - sizes[i].x * 0.5f, y, lines[i].col, lines[i].text);
            y += sizes[i].y;
        }
    }
#endif
}

#ifdef ENABLE_EDITOR
void AIDebugOverlaySystem::OnGui()
{
    // 別ウィンドウは作らず、System Manager の各システム欄（このヘッダ配下）に直接出す。
    ImGui::Checkbox("Show Floating Text", &m_enabled);
    ImGui::SliderFloat("Font Size", &m_fontSize, 10.0f, 100.0f);
    ImGui::SliderFloat("Head Offset Y", &m_headOffsetY, 0.0f, 15.0f);
    ImGui::TextDisabled("ボス頭上に 戦術/行動/HP・ドローン数 を常時表示");
}
#else
void AIDebugOverlaySystem::OnGui() {}
#endif

// ImGui::NewFrame 後（SceneManager::Render 内）に走らせる必要があるためレンダーステージへ。
// R10_UI は最後段なので頭上テキストが他の描画に隠れない。
REGISTER_RENDER_SYSTEM(AIDebugOverlaySystem, Priority::RenderStage::R10_UI);
