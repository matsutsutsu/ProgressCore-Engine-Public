#include "AIDebugOverlaySystem.h"

#include "Game/Logics/AI/BehaviorTree/Data/BTActionIds.h"
#include "Game/Logics/AI/Battle/BattleContext.h" // ドローン残数
#include "Game/Logics/Combat/Components/HealthComponent.h"  // ボスHP
#include "Engine/UI/ECS/GameViewportInfo.h"      // エディタでゲーム画面を表示している矩形
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Graphics/SceneView/Camera.h"

#include <SimpleMath.h>
#include <algorithm>
#include <string>

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

    /**
     * @brief ワールド座標を画面座標へ変換する。
     *
     * Vector3::Transform(coord) は w 除算込みなので、結果はそのまま NDC になる。
     *
     * @param world    変換する位置（ワールド）。
     * @param viewProj ビュー × プロジェクション行列。
     * @param rect     描画先の画面の矩形（px）。
     * @param out      画面座標（px）を受け取る。
     * @return 描ける位置なら true。カメラの後ろ・奥の切り捨て面より先なら false。
     */
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

    /**
     * @brief 頭上に出す行動名を返す。BT が今 Running にしている行動を優先する。
     *
     * lastLogActionId（行動が自分で書く「最後に実行した行動」）だけだと、A_Wait のように書かない行動の間は前の技の名前が残る。
     * 突進の後の待機中も「突進攻撃」と出続ける。
     *
     * runningNodeId は評価が子から先に返るので、実際に動いている末端の行動を指す。
     *
     * @param bt 表示する BT。
     * @return 行動名（文字列リテラルか ActionRegistry の文字列）。停止中は "(AI 停止中)"、何も無ければ "Idle"。
     */
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
    if (!m_enabled && !m_showScores) return;

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

    // --- 技選択のパネル（ゲーム画面の左上）---
    //   ボスが複数いれば下へ積む
    if (m_showScores)
    {
        const float margin = 8.0f;
        float       top    = rect.y + margin;
        auto        bosses = m_world->View<BossPhaseComponent, BehaviorTreeComponent, TransformComponent>();
        for (auto e : bosses)
        {
            if (const auto *bt = m_world->GetComponent<BehaviorTreeComponent>(e))
            {
                top += DrawScorePanel(*bt, rect.x + margin, top) + margin;
            }
        }
    }

    // --- ここから頭上の文字 ---
    if (!m_enabled) return;

    const auto *camera = m_world->HasResource<Camera *>() ? m_world->GetResource<Camera *>() : nullptr;
    if (!camera) return;

    const Matrix viewProj = camera->GetView() * camera->GetProjection();

    ImDrawList *drawList = ImGui::GetForegroundDrawList();
    ImFont     *font     = ImGui::GetFont();

    // プレイヤーが居るか（距離の行を出すか・頭上をどれだけ持ち上げるか）を見るのに使う。
    const BattleContext *battle = m_world->HasResource<BattleContext>() ? &m_world->GetResource<BattleContext>() : nullptr;

    auto view = m_world->View<BossPhaseComponent, BehaviorTreeComponent, TransformComponent>();
    for (auto e : view)
    {
        auto *ph    = m_world->GetComponent<BossPhaseComponent>(e);
        auto *bt    = m_world->GetComponent<BehaviorTreeComponent>(e);
        auto *trans = m_world->GetComponent<TransformComponent>(e);
        if (!ph || !bt || !trans) continue;

        const bool hasPlayer = battle && battle->HasPlayer();

        // 距離は自分で測り直さず、BT の距離条件が比べている blackboard の値をそのまま使う。
        // 測り直すと、測る点（足元か中心か）や更新のタイミングが違ったときに、表示と判定が食い違う。
        const float distance = hasPlayer ? bt->blackboard.distanceToTarget : 0.0f;

        // --- 頭上の位置 ---
        //   離れるほど高く出す（プレイヤーが居なければ足さない）。
        Vector3 worldPos = trans->position;
        worldPos.y += m_headOffsetY + distance * m_headOffsetPerMeter;

        ImVec2 head;
        if (!WorldToScreen(worldPos, viewProj, rect, head)) continue; // 画面外・カメラ後方
        const float pixelX = head.x;
        const float pixelY = head.y;

        struct DbgLine
        {
            const char *text;
            ImU32       col;
        };
        DbgLine lines[4];
        int     lineCount = 0;

        // 1) 形態（BossPhaseSystem が決めたもの）：オレンジ
        char phaseText[80];
        snprintf(phaseText, sizeof(phaseText), "[%s]", ToString(ph->phase));
        lines[lineCount++] = {phaseText, IM_COL32(255, 200, 80, 255)};

        // 2) 行動（BT）：今 Running にしている行動：白
        lines[lineCount++] = {CurrentActionName(*bt), IM_COL32(255, 255, 255, 255)};

        // 3) 距離：行動のすぐ下に置き、「この距離だからこの行動を選んだ」を続けて読めるようにする。
        //    色は今いる帯（地面に塗られている帯）の色。赤なら「〜m 未満」、青なら「〜m 超」の
        //    条件が通っている帯なので、数字の色だけで「今どちら側の枝が選ばれうるか」が読める。
        //    どのリングよりも外なら白。
        char distanceText[32];
        if (m_showDistance && hasPlayer)
        {
            snprintf(distanceText, sizeof(distanceText), "%.1f m", distance);

            ImU32 color = IM_COL32(255, 255, 255, 255);
            if (bt->sharedAsset)
            {
                AIDebugRanges::CollectRings(*bt->sharedAsset, m_rings, m_edgeRings);
                if (const AIDebugRanges::RangeRing *band = AIDebugRanges::BandOf(m_rings, distance))
                    color = ToImColor(AIDebugRanges::ColorOf(*band));
            }
            lines[lineCount++] = {distanceText, color};
        }

        // 4) ステータス：ボスの HP とドローンの残数：緑
        char statusText[96] = "";
        {
            const auto *hp     = m_world->GetComponent<HealthComponent>(e);
            const int   drones = battle ? battle->droneAliveCount : -1;
            if (hp && drones >= 0)
                snprintf(statusText, sizeof(statusText), "HP %.0f/%.0f  Drone %d",
                         hp->currentHealth, hp->maxHealth, drones);
            else if (hp)
                snprintf(statusText, sizeof(statusText), "HP %.0f/%.0f", hp->currentHealth, hp->maxHealth);
        }
        if (statusText[0] != '\0') lines[lineCount++] = {statusText, IM_COL32(150, 235, 150, 255)};

        // --- 描画（アウトライン付き・中央揃え・N行対応）---
        ImVec2 sizes[4];
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
    ImGui::TextDisabled("ボス頭上に 形態/行動/距離/HP・ドローン数 を常時表示");

    ImGui::Separator();
    ImGui::Checkbox("Show Distance", &m_showDistance);
    ImGui::TextDisabled("行動の下の行に、プレイヤーとの距離を表示（BT の距離条件と同じ値）");
    ImGui::TextDisabled("数字の色は今いる帯の色（赤=〜m未満 / 青=〜m超 / 緑=境目 / 白=どの輪よりも外）");

    ImGui::Separator();
    ImGui::Checkbox("技選択のパネル", &m_showScores);
    ImGui::SliderFloat("パネルの文字の大きさ", &m_panelFontSize, 10.0f, 40.0f);
    ImGui::SliderInt("パネルの履歴の数", &m_historyLines, 0, kPickLogSize - 1);
    ImGui::TextDisabled("ゲーム画面の左上に、技を選んだ瞬間の上位3つの点数と、それまでに選んだ技を出す");
    ImGui::TextDisabled("点数 = 優先度 × 距離 × 状況 × 減点。抽選確率は、上位3つの点数の割合");
    ImGui::TextDisabled("黄色の行 = 選ばれた技　倍率の色：赤 = 点数を下げた　緑 = 上げた　灰 = 効いていない");
    ImGui::TextDisabled("灰色の行 = 候補外。抽選確率の欄に理由（CD 残り秒 / 条件 / 距離外 / 0点）");
}

// ---------------------------------------------------------------------------
// 技選択のパネル
// ---------------------------------------------------------------------------

float AIDebugOverlaySystem::DrawScorePanel(const BehaviorTreeComponent &bt, float left, float top)
{
    // 処理フロー:
    // 1. 最後の選択の表を作る（上位3つの点数と、点数に掛かった倍率。3つに足りなければ候補外の子とその理由）
    // 2. それまでの選択の表を作る
    // 3. 2つの表を縦に並べて描く

    // 【備考】
    //   数字はどれも技を選んだ瞬間のもの（BehaviorTreeSystem::RecordUtilityPick が写したもの）。
    //   毎フレームの点数ではないので、技を出している間は止まって見える。
    //   「何秒前」は木を評価した時間で数える（一時停止・カットシーン中は進まない）。

    if (!bt.sharedAsset)
    {
        return 0.0f;
    }
    const BTAsset &asset = *bt.sharedAsset;

    const ImU32 white  = IM_COL32(255, 255, 255, 255);
    const ImU32 gray   = IM_COL32(150, 150, 150, 255); // 効いていない値
    const ImU32 yellow = IM_COL32(255, 210, 80, 255);  // 選ばれた技
    const ImU32 red    = IM_COL32(255, 120, 120, 255); // 点数を下げた倍率
    const ImU32 green  = IM_COL32(120, 230, 120, 255); // 点数を上げた倍率

    // まだ一度も選んでいなければ出さない
    if (bt.debugPickCount == 0)
    {
        return 0.0f;
    }

    char buf[128];

    // back 件前の記録（0 が最新）
    auto recordAt = [&](int back) -> const BTPickRecord & {
        return bt.debugPickLog[(bt.debugPickCount - 1 - back) % kPickLogSize];
    };

    // 確率のセルの文字列。割り込みで選ばれた子は抽選していないので確率ではなく「割り込み」。
    // 確率 0 は上位3つの外から繰り上がって選ばれた子
    auto chanceText = [&](const BTScoreDebug &row) {
        if (row.interrupted)
        {
            return std::string("割り込み");
        }
        if (row.chance <= 0.0f)
        {
            return std::string("繰上");
        }
        snprintf(buf, sizeof(buf), "%.0f%%", row.chance * 100.0f);
        return std::string(buf);
    };

    // 候補外の子の理由を、抽選確率の列に入れる短い文字にする。
    // 列を足すと候補外が出るたびに表の幅が変わるので、見出し「抽選確率」の幅（全角4文字）に収める
    auto excludedText = [&](const BTScoreDebug &row) {
        switch (row.state)
        {
        case BTCandidateState::Cooldown:
            snprintf(buf, sizeof(buf), "CD %.1f", row.cooldownLeft); // 残り秒数（いつ出られるようになるか）
            return std::string(buf);
        case BTCandidateState::Condition: return std::string("条件");
        case BTCandidateState::OutOfRange: return std::string("距離外");
        case BTCandidateState::ZeroScore: return std::string("0点");
        default: return std::string("-");
        }
    };

    // 倍率のセル。1 より小さければ赤（下げた）、大きければ緑（上げた）、1 なら灰色（効いていない）
    auto factorCell = [&](float factor) {
        snprintf(buf, sizeof(buf), "×%.2f", factor);
        const ImU32 color = (factor < 1.0f) ? red : (factor > 1.0f) ? green : gray;
        return PanelCell{buf, color};
    };

    // ---------------------------------------------------------
    // 1. 最後の選択
    // ---------------------------------------------------------
    const BTPickRecord &latest  = recordAt(0);
    const bool          hasName = latest.selectorId < asset.nodeNames.size() &&
                                  !asset.nodeNames[latest.selectorId].empty();
    const char         *selectorName = hasName ? asset.nodeNames[latest.selectorId].c_str() : "技選択";

    PanelTable result;
    snprintf(buf, sizeof(buf), "%s の結果（%.1f 秒前・相手まで %.1f m）", selectorName,
             bt.debugClock - latest.time, latest.distance);
    result.title      = buf;
    result.headers    = {"技", "抽選確率", "点数", "優先度", "距離", "状況", "減点"};
    result.rightAlign = {false, true, true, true, true, true, true};

    // 候補が3つに足りないと、空いた枠に候補外の子が入っている（RecordUtilityPick）。
    // 候補外の子には抽選確率が無いので、その欄に理由を書く
    for (int i = 0; i < latest.rowCount; ++i)
    {
        const BTScoreDebug &row       = latest.rows[i];
        const BTNodeID      childId   = latest.rowIds[i];
        const bool          excluded  = row.state != BTCandidateState::Candidate;
        const ImU32         mainColor = row.picked ? yellow : excluded ? gray : white;

        PanelRow line;
        line.highlight = row.picked;
        line.cells.push_back({TechniqueName(asset, childId), mainColor});
        line.cells.push_back(excluded ? PanelCell{excludedText(row), white} : PanelCell{chanceText(row), mainColor});
        snprintf(buf, sizeof(buf), "%.1f", row.total);
        line.cells.push_back({buf, mainColor});
        snprintf(buf, sizeof(buf), "%g", BasePriority(asset, childId));
        line.cells.push_back({buf, white});

        // 距離：-1 は「距離で点数を変える」を使っていない子
        line.cells.push_back(row.distance < 0.0f ? PanelCell{"使わない", gray} : factorCell(row.distance));
        line.cells.push_back(factorCell(row.bonus));
        line.cells.push_back(factorCell(row.repeat));
        result.rows.push_back(line);
    }

    // ---------------------------------------------------------
    // 2. それまでの選択
    // ---------------------------------------------------------
    PanelTable history;
    history.title      = "それまでの選択（新しい順）";
    history.headers    = {"技", "抽選確率", "相手との距離"};
    history.rightAlign = {false, true, true};

    const int historyCount = (std::min)((std::min)(bt.debugPickCount, kPickLogSize) - 1, m_historyLines);
    for (int back = 1; back <= historyCount; ++back)
    {
        const BTPickRecord &rec = recordAt(back);

        // 選ばれた子の行（確率・割り込みか）を記録から探す
        BTScoreDebug picked;
        for (int i = 0; i < rec.rowCount; ++i)
        {
            if (rec.rowIds[i] == rec.pickedId)
            {
                picked = rec.rows[i];
            }
        }

        PanelRow line;
        line.cells.push_back({TechniqueName(asset, rec.pickedId), white});
        line.cells.push_back({chanceText(picked), white});
        snprintf(buf, sizeof(buf), "%.1f m", rec.distance);
        line.cells.push_back({buf, white});
        history.rows.push_back(line);
    }

    // ---------------------------------------------------------
    // 3. 描く
    // ---------------------------------------------------------

    // 2つの表を1枚の背景にまとめる。背景の大きさは表を描き終えるまで分からないので、
    // 描画リストを2層に分け、表を上の層に描いてから、下の層に背景を敷く
    ImDrawList *drawList = ImGui::GetForegroundDrawList();
    const float padding  = m_panelFontSize * 0.5f; // 背景の余白
    float       width    = 0.0f;                   // 一番広い表の幅
    float       y        = top + padding;

    drawList->ChannelsSplit(2);
    drawList->ChannelsSetCurrent(1);
    y += DrawPanelTable(result, left + padding, y, width);
    if (!history.rows.empty())
    {
        y += m_panelFontSize * 0.6f; // 表と表の間
        y += DrawPanelTable(history, left + padding, y, width);
    }
    const float bottom = y + padding;

    drawList->ChannelsSetCurrent(0);
    drawList->AddRectFilled(ImVec2(left, top), ImVec2(left + width + padding * 2.0f, bottom), IM_COL32(0, 0, 0, 170),
                            4.0f);
    drawList->ChannelsMerge();
    return bottom - top;
}

float AIDebugOverlaySystem::DrawPanelTable(const PanelTable &table, float left, float top, float &inOutWidth)
{
    // 処理フロー:
    // 1. 列の幅を測る
    // 2. 見出しを描く
    // 3. 表（列の見出し・行・罫線）を描く

    // 【備考】
    //   背景は描かない（DrawScorePanel が全部の表の下に1枚敷く）。
    //   色と点数の読み方は画面に出さず、System Manager の OnGui に書く（パネルを小さく保つため）。

    ImDrawList *drawList = ImGui::GetForegroundDrawList();
    ImFont     *font     = ImGui::GetFont();

    const float size    = m_panelFontSize;
    const float rowH    = size * 1.4f; // 行の高さ
    const float cellPad = size * 0.4f; // セルの左右の余白

    const ImU32 lineColor = IM_COL32(255, 255, 255, 60); // 罫線

    auto textWidth = [&](float fontSize, const std::string &text) {
        return font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text.c_str()).x;
    };

    // ---------------------------------------------------------
    // 1. 測る
    // ---------------------------------------------------------

    // 列の幅は「見出しと全部の行のうち一番長い文字」＋左右の余白
    const size_t       colCount = table.headers.size();
    std::vector<float> colW(colCount, 0.0f);
    for (size_t c = 0; c < colCount; ++c)
    {
        colW[c] = textWidth(size, table.headers[c]);
    }
    for (const PanelRow &row : table.rows)
    {
        for (size_t c = 0; c < colCount && c < row.cells.size(); ++c)
        {
            colW[c] = (std::max)(colW[c], textWidth(size, row.cells[c].text));
        }
    }

    float tableW = 0.0f;
    for (float &w : colW)
    {
        w += cellPad * 2.0f;
        tableW += w;
    }

    inOutWidth = (std::max)(inOutWidth, (std::max)(tableW, textWidth(size, table.title)));

    // ---------------------------------------------------------
    // 2. 見出し
    // ---------------------------------------------------------
    const float textOffsetY = (rowH - size) * 0.5f; // 行の中で文字を縦の真ん中に置く
    const float tableX      = left;
    float       y           = top;
    drawList->AddText(font, size, ImVec2(tableX, y + textOffsetY), IM_COL32(255, 255, 255, 255), table.title.c_str());
    y += rowH;

    // ---------------------------------------------------------
    // 3. 表
    // ---------------------------------------------------------
    if (colCount > 0)
    {
        // セル1つの文字を描く。数字の列は右揃えにして、桁の位置を縦にそろえる
        auto drawCell = [&](size_t c, float cellX, float rowTop, const std::string &text, ImU32 color) {
            const bool  right = c < table.rightAlign.size() && table.rightAlign[c];
            const float textX = right ? cellX + colW[c] - cellPad - textWidth(size, text) : cellX + cellPad;
            drawList->AddText(font, size, ImVec2(textX, rowTop + textOffsetY), color, text.c_str());
        };

        const float tableTop = y;

        // 列の見出し
        drawList->AddRectFilled(ImVec2(tableX, y), ImVec2(tableX + tableW, y + rowH), IM_COL32(70, 70, 90, 220));
        float x = tableX;
        for (size_t c = 0; c < colCount; ++c)
        {
            drawCell(c, x, y, table.headers[c], IM_COL32(210, 210, 230, 255));
            x += colW[c];
        }
        y += rowH;

        // 行。選ばれた技の行は黄色、それ以外は1行おきに薄く塗って横に読みやすくする
        for (size_t r = 0; r < table.rows.size(); ++r)
        {
            const PanelRow &row = table.rows[r];
            if (row.highlight)
            {
                drawList->AddRectFilled(ImVec2(tableX, y), ImVec2(tableX + tableW, y + rowH), IM_COL32(255, 210, 80, 55));
            }
            else if (r % 2 == 1)
            {
                drawList->AddRectFilled(ImVec2(tableX, y), ImVec2(tableX + tableW, y + rowH), IM_COL32(255, 255, 255, 14));
            }

            x = tableX;
            for (size_t c = 0; c < colCount && c < row.cells.size(); ++c)
            {
                drawCell(c, x, y, row.cells[c].text, row.cells[c].color);
                x += colW[c];
            }

            // 行の境目の横線
            drawList->AddLine(ImVec2(tableX, y), ImVec2(tableX + tableW, y), lineColor);
            y += rowH;
        }

        // 外枠と列の境目の縦線
        drawList->AddRect(ImVec2(tableX, tableTop), ImVec2(tableX + tableW, y), lineColor);
        x = tableX;
        for (size_t c = 0; c + 1 < colCount; ++c)
        {
            x += colW[c];
            drawList->AddLine(ImVec2(x, tableTop), ImVec2(x, y), lineColor);
        }
    }
    return y - top;
}

const char *AIDebugOverlaySystem::TechniqueName(const BTAsset &asset, BTNodeID nodeId)
{
    if (nodeId >= asset.nodes.size())
    {
        return "(不明)";
    }

    // 技の枝は「Cooldown → Sequence（名前付き）」の形が多い。修飾を下へたどり、名前の付いたまとまりを探す
    BTNodeID id = nodeId;
    while (asset.nodes[id].type == BTNodeType::Decorator && !asset.nodes[id].children.empty())
    {
        id = asset.nodes[id].children.front();
    }
    if (id < asset.nodeNames.size() && !asset.nodeNames[id].empty())
    {
        return asset.nodeNames[id].c_str();
    }

    // 名前が無い（エディタで名前を付けていない・名前を書き出す前の古いファイル）
    const char *name = FirstActionName(asset, nodeId);
    return name ? name : "(名前なし)";
}

const char *AIDebugOverlaySystem::FirstActionName(const BTAsset &asset, BTNodeID nodeId)
{
    const BTNode &node = asset.nodes[nodeId];
    if (node.type == BTNodeType::Action)
    {
        if (const ActionEntry *e = FindActionEntry(node.actionOrConditionId))
        {
            return e->name;
        }
    }
    for (BTNodeID child : node.children)
    {
        if (const char *name = FirstActionName(asset, child))
        {
            return name;
        }
    }
    return nullptr;
}

float AIDebugOverlaySystem::BasePriority(const BTAsset &asset, BTNodeID nodeId)
{
    const BTNode &node = asset.nodes[nodeId];
    return (node.scoreRuleIndex >= 0) ? asset.scoreRules[node.scoreRuleIndex].base : node.weight;
}

// ImGui::NewFrame の後（SceneManager::Render の中）に走らせる必要があるので、Render ステージに置く。
// R10_UI は最後の段なので、頭上のテキストが他の描画に隠れない。
//
// 登録ごと #ifdef で外し、製品ビルドにはこの System が存在しないようにする。
// 中身だけを #ifdef で外すと、何もしない System が実行順の解決の対象としてだけ残る。
#ifdef ENABLE_EDITOR
REGISTER_RENDER_SYSTEM(AIDebugOverlaySystem, Priority::RenderStage::R10_UI);
#endif
