#include "AIDebugOverlaySystem.h"

#include "Game/Logics/AI/BehaviorTree/Data/ActionRegistry.h"
#include "Game/Logics/AI/Battle/BattleContext.h" // ドローン残数
#include "Game/Logics/Combat/HealthComponent.h"  // ボスHP
#include "Engine/UI/ECS/GameViewportInfo.h"      // エディタでゲーム画面を表示している矩形
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Graphics/RHI/Camera.h"

#include <SimpleMath.h>
#include <algorithm>

#include <imgui.h>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

namespace
{
    // ゲーム画面が映っている矩形（ImGui の画面座標）。
    struct ScreenRect
    {
        float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    };

    // ワールド座標を画面座標へ変換する。カメラの後ろ・奥の切り捨て面より先なら false。
    // Vector3::Transform(coord) は w 除算込みなので、結果はそのまま NDC になる。
    bool WorldToScreen(const Vector3 &world, const Matrix &viewProj, const ScreenRect &rect, ImVec2 &out)
    {
        const Vector3 ndc = Vector3::Transform(world, viewProj);
        if (ndc.z <= 0.0f || ndc.z >= 1.0f) return false;

        out.x = rect.x + (ndc.x + 1.0f) * 0.5f * rect.w;
        out.y = rect.y + (1.0f - ndc.y) * 0.5f * rect.h;
        return true;
    }

    // 黒い縁取り付きで文字を描く。背景が明るくても暗くても読めるようにする。
    void DrawOutlinedText(ImDrawList *drawList, ImFont *font, float size, const ImVec2 &pos, ImU32 col,
                          const char *text)
    {
        static const float offs[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
        for (auto &o : offs)
            drawList->AddText(font, size, ImVec2(pos.x + o[0], pos.y + o[1]), IM_COL32(0, 0, 0, 255), text);
        drawList->AddText(font, size, pos, col, text);
    }

    // 頭上に出す行動名。BT が今 Running にしている行動を優先する。
    //
    // lastLogActionId（行動が自分で書く「最後に実行した行動」）だけだと、A_Wait のように
    // 書かない行動の間は前の技の名前が残り、突進の後の待機中も「突進攻撃」と出続ける。
    // runningNodeId は評価が子から先に返るので、実際に動いている末端の行動を指す。
    const char *CurrentActionName(const BehaviorTreeComponent &bt)
    {
        if (bt.debugPaused && bt.debugForcedNodeId < 0) return "(AI 停止中)";

        if (bt.sharedAsset && bt.runningNodeId < bt.sharedAsset->nodes.size())
        {
            const BTNode &node = bt.sharedAsset->nodes[bt.runningNodeId];
            if (node.type == BTNodeType::Action)
                if (const ActionEntry *e = FindActionEntry(node.actionOrConditionId)) return e->name;
        }

        // Running の行動が無いフレーム（即 Success の行動だけで終わった等）は、最後に実行した行動を出す。
        if (const ActionEntry *e = FindActionEntry(bt.lastLogActionId)) return e->name;
        return "Idle";
    }

    ImU32 ToImColor(const DirectX::XMFLOAT4 &c)
    {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w));
    }
} // namespace

void AIDebugOverlaySystem::Update(float /*dt*/)
{
    if (!m_enabled) return;

    const auto *camera = m_world->HasResource<Camera *>() ? m_world->GetResource<Camera *>() : nullptr;
    if (!camera) return;

    const Matrix viewProj = camera->GetView() * camera->GetProjection();

    // --- 投影先の矩形を決める ---
    //   エディタ: ゲームは GameView パネル内の矩形に画像として表示されるので、その矩形へ変換する。
    //   フルスクリーン / 矩形が取れないとき: OS のウィンドウ全体を使う。
    const ImVec2 mainPos = ImGui::GetMainViewport()->Pos;
    ScreenRect   rect;
    if (m_world->HasResource<GameViewportInfo>() && m_world->GetResource<GameViewportInfo>().valid)
    {
        const auto &vp = m_world->GetResource<GameViewportInfo>();
        rect           = {mainPos.x + vp.x, mainPos.y + vp.y, vp.w, vp.h};
    }
    else
    {
        const ImVec2 disp = ImGui::GetIO().DisplaySize;
        rect              = {mainPos.x, mainPos.y, disp.x, disp.y};
    }
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;

    ImDrawList *drawList = ImGui::GetForegroundDrawList();
    ImFont     *font     = ImGui::GetFont();

    // プレイヤーの足元。距離の表示位置（ボスとの中点）に使う。
    const BattleContext *battle = m_world->HasResource<BattleContext>() ? &m_world->GetResource<BattleContext>() : nullptr;

    auto view = m_world->View<BossPhaseComponent, BehaviorTreeComponent, TransformComponent>();
    for (auto e : view)
    {
        auto *ph    = m_world->GetComponent<BossPhaseComponent>(e);
        auto *bt    = m_world->GetComponent<BehaviorTreeComponent>(e);
        auto *trans = m_world->GetComponent<TransformComponent>(e);
        if (!ph || !bt || !trans) continue;

        // --- 距離：ボスとプレイヤーの中点 ---
        //   値は自分で測り直さず、BT の距離条件が比べている blackboard の値をそのまま出す。
        //   測り直すと、測る点（足元か中心か）や更新のタイミングが違ったときに、表示と判定が食い違う。
        if (m_showDistance && battle && battle->HasPlayer())
        {
            Vector3 mid = (Vector3(trans->position) + Vector3(battle->playerPos)) * 0.5f;
            mid.y += m_distanceOffsetY;

            ImVec2 screen;
            if (WorldToScreen(mid, viewProj, rect, screen))
            {
                const float distance = bt->blackboard.distanceToTarget;

                char text[32];
                snprintf(text, sizeof(text), "%.1f m", distance);

                const ImVec2 size = font->CalcTextSizeA(m_distanceFontSize, FLT_MAX, 0.0f, text);
                const ImVec2 pos(screen.x - size.x * 0.5f, screen.y - size.y * 0.5f);
                const float  pad = m_distanceFontSize * 0.2f;
                drawList->AddRectFilled(ImVec2(pos.x - pad, pos.y - pad * 0.5f),
                                        ImVec2(pos.x + size.x + pad, pos.y + size.y + pad * 0.5f),
                                        IM_COL32(0, 0, 0, 160), 6.0f);

                // 色は今いる帯（地面に塗られている帯）の色。赤なら「〜m 未満」、青なら「〜m 超」の
                // 条件が通っている帯なので、数字の色だけで「今どちら側の枝が選ばれうるか」が読める。
                // どのリングよりも外なら白。
                ImU32 color = IM_COL32(255, 255, 255, 255);
                if (bt->sharedAsset)
                {
                    AIDebugRanges::CollectRings(*bt->sharedAsset, m_rings, m_edgeRings);
                    if (const AIDebugRanges::RangeRing *band = AIDebugRanges::BandOf(m_rings, distance))
                        color = ToImColor(AIDebugRanges::ColorOf(*band));
                }
                DrawOutlinedText(drawList, font, m_distanceFontSize, pos, color, text);
            }
        }

        // --- 頭上の文字 ---
        //   離れるほど高く出す。距離は上の数字と同じ blackboard の値を使う（プレイヤーが居なければ足さない）。
        const float headDistance = (battle && battle->HasPlayer()) ? bt->blackboard.distanceToTarget : 0.0f;

        Vector3 worldPos = trans->position;
        worldPos.y += m_headOffsetY + headDistance * m_headOffsetPerMeter;

        ImVec2 head;
        if (!WorldToScreen(worldPos, viewProj, rect, head)) continue; // 画面外・カメラ後方
        const float pixelX = head.x;
        const float pixelY = head.y;

        // 1) 形態（BossPhaseSystem が決めたもの）
        char line1[80];
        snprintf(line1, sizeof(line1), "[%s]", ToString(ph->phase));

        // 2) 行動（BT）：今 Running にしている行動
        const char *actionName = CurrentActionName(*bt);

        // 3) ステータス：ボスの HP とドローンの残数
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

        struct DbgLine
        {
            const char *text;
            ImU32       col;
        };
        const DbgLine lines[3] = {
            {line1, IM_COL32(255, 200, 80, 255)},               // 形態：オレンジ
            {actionName, IM_COL32(255, 255, 255, 255)},         // 行動：白
            {line3, IM_COL32(150, 235, 150, 255)},              // ステータス：緑
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

        float y = topY;
        for (int i = 0; i < lineCount; ++i)
        {
            DrawOutlinedText(drawList, font, m_fontSize, ImVec2(pixelX - sizes[i].x * 0.5f, y), lines[i].col, lines[i].text);
            y += sizes[i].y;
        }
    }
}

void AIDebugOverlaySystem::OnGui()
{
    // 別ウィンドウは作らず、System Manager の各システム欄（このヘッダ配下）に直接出す。
    ImGui::Checkbox("Show Floating Text", &m_enabled);
    ImGui::SliderFloat("Font Size", &m_fontSize, 10.0f, 100.0f);
    ImGui::SliderFloat("Head Offset Y", &m_headOffsetY, 0.0f, 15.0f);
    ImGui::SliderFloat("距離1mあたりの追加の高さ", &m_headOffsetPerMeter, 0.0f, 0.5f);
    ImGui::TextDisabled("プレイヤーから離れるほど頭上の表示を持ち上げ、遠くでボスに被らないようにする");
    ImGui::TextDisabled("ボス頭上に 形態/行動/HP・ドローン数 を常時表示");

    ImGui::Separator();
    ImGui::Checkbox("Show Distance", &m_showDistance);
    ImGui::SliderFloat("Distance Font Size", &m_distanceFontSize, 10.0f, 100.0f);
    ImGui::SliderFloat("Distance Offset Y", &m_distanceOffsetY, 0.0f, 10.0f);
    ImGui::TextDisabled("ボスとプレイヤーの中点に距離を表示（BT の距離条件と同じ値）");
    ImGui::TextDisabled("数字の色は今いる帯の色（赤=〜m未満 / 青=〜m超 / 緑=境目 / 白=どの輪よりも外）");
}

// ImGui::NewFrame の後（SceneManager::Render の中）に走らせる必要があるので、Render ステージに置く。
// R10_UI は最後の段なので、頭上のテキストが他の描画に隠れない。
//
// 登録ごと #ifdef で外し、製品ビルドにはこの System が存在しないようにする。
// 中身だけを #ifdef で外すと、何もしない System が実行順の解決の対象としてだけ残る。
#ifdef ENABLE_EDITOR
REGISTER_RENDER_SYSTEM(AIDebugOverlaySystem, Priority::RenderStage::R10_UI);
#endif
