// 役割・設計方針は InputPromptSystem.h を参照。
#include "InputPromptSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/Common/CCL_Archetype.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/Serialization/Factory/EntityFactory.h"

#include "Engine/Transform/Components/TransformComponent.h" // UI階層（親子）
#include "Engine/UI/ECS/Components/UITransformComponent.h"
#include "Engine/UI/ECS/Components/UIImageComponent.h"
#include "Engine/UI/ECS/Components/UITextComponent.h"
#include "Game/UI/InputIconComponent.h"
#include "Game/UI/MenuUiFactory.h"               // SpawnCanvas / MakeUINode
#include "Game/Logics/AI/Battle/BattleContext.h" // プレイヤーの有無で生成を決める

#include <cstdio>
#include <fstream>
#include <imgui.h>
#include <json.hpp>

using namespace CCL::ECS;
using json = nlohmann::json;

namespace
{
    constexpr const char *kConfigPath = "Assets/Config/InputPromptConfig.json";
} // namespace

void InputPromptSystem::LoadConfig(std::vector<ConfigRow> &outRows) const
{
    std::ifstream file(kConfigPath);
    if (file.is_open())
    {
        try
        {
            json doc;
            file >> doc;
            for (const auto &p : doc.at("Prompts"))
            {
                ConfigRow row;
                row.action = p.value("Action", std::string{});
                row.icon   = p.value("Icon", std::string{});
                row.label  = p.at("Label").get<std::string>();
                outRows.push_back(std::move(row));
            }
        }
        catch (...)
        {
            outRows.clear(); // 壊れたJSONは既定へ落とす（下で埋める）
        }
    }

    // 設定が読めなくてもHUDが消えないよう既定を持つ。
    //   操作説明が出ないのは「機能が無い」に見えるので、静かに空にしない。
    if (outRows.empty())
    {
        outRows = {{"", "Move", "MOVE"}, {"", "Camera", "CAMERA"}, {"Attack", "", "ATTACK"}, {"Cleave", "", "CLEAVE"}, {"Dash", "", "DODGE"}, {"Jump", "", "JUMP"}, {"LockOn", "", "LOCK ON"}};
    }
}

void InputPromptSystem::ComputeRow(size_t index, UITransformComponent &outIcon,
                                   UITransformComponent &outLabel) const
{
    const float y = m_marginTop + m_rowHeight * static_cast<float>(index);

    // アイコン：画面右上へ、自身の右上を合わせて右端から一定距離に置く。
    outIcon.anchor = {1.0f, 0.0f};
    outIcon.pivot  = {1.0f, 0.0f};
    outIcon.offset = {-m_marginRight, y};
    outIcon.size   = {m_iconSize, m_iconSize};
    outIcon.alpha  = m_alpha;

    // 行名：アイコンの左。
    //   文字は computedPos の左上から描かれるので、アイコンの縦中央に来るよう少し下げる。
    outLabel.anchor = {1.0f, 0.0f};
    outLabel.pivot  = {1.0f, 0.0f};
    outLabel.offset = {-(m_marginRight + m_iconSize + m_labelGap),
                       y + (m_iconSize - m_textSize) * 0.5f};
    outLabel.size   = {m_labelWidth, m_textSize};
    outLabel.alpha  = m_alpha;
}

void InputPromptSystem::ApplyLayout()
{
    const size_t count = (std::min)(m_iconIDs.size(), m_labelIDs.size());
    for (size_t i = 0; i < count; ++i)
    {
        UITransformComponent it{};
        UITransformComponent lt{};
        ComputeRow(i, it, lt);

        // computed* は UILayoutSystem が毎フレーム書くので触らない。
        //   丸ごと代入すると計算済みの位置を1フレーム空にしてしまう。
        if (auto *p = m_world->GetComponent<UITransformComponent>(m_iconIDs[i]))
        {
            p->anchor = it.anchor;
            p->pivot  = it.pivot;
            p->offset = it.offset;
            p->size   = it.size;
            p->alpha  = it.alpha;
        }
        if (auto *p = m_world->GetComponent<UITransformComponent>(m_labelIDs[i]))
        {
            p->anchor = lt.anchor;
            p->pivot  = lt.pivot;
            p->offset = lt.offset;
            p->size   = lt.size;
            p->alpha  = lt.alpha;
        }
        if (auto *tx = m_world->GetComponent<UITextComponent>(m_labelIDs[i]))
        {
            tx->pixelSize = m_textSize;
        }
    }
}

void InputPromptSystem::BuildRows()
{
    std::vector<ConfigRow> rows;
    LoadConfig(rows);

    // UI階層のルート。配下をこの Canvas の子にしてヒエラルキーで束ねる。
    const EntityID canvas = CCL::GameUI::SpawnCanvas(*m_world, "InputPromptCanvas");

    auto iconArch  = ArchetypeHelper::Generate<TransformComponent, UITransformComponent,
                                               UIImageComponent, InputIconComponent>();
    auto labelArch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent,
                                               UITextComponent>();

    for (size_t i = 0; i < rows.size(); ++i)
    {
        const ConfigRow  &cfg    = rows[i];
        const std::string suffix = cfg.action.empty() ? cfg.icon : cfg.action;

        UITransformComponent it{};
        UITransformComponent lt{};
        ComputeRow(i, it, lt);

        // --- アイコン（右端に寄せる） ---
        auto icon = EntityFactory::SpawnNamed(*m_world, ("Prompt_" + suffix).c_str(), iconArch);

        UIImageComponent img;
        img.sortOrder = 120;   // ゲージ(100〜102)より手前
        img.visible   = false; // InputIconSystem が張り替えるまで出さない

        // どのアイコンかは宣言だけして、解決は InputIconSystem に任せる。
        InputIconComponent ic;
        if (cfg.action.empty())
        {
            if (cfg.icon == "Camera")
                ic.kind = InputIconKind::Camera;
            else
                ic.kind = InputIconKind::Move;
        }
        else
        {
            ic.kind       = InputIconKind::Action;
            ic.actionHash = CCL::Utils::HashString(cfg.action.c_str());
        }

        icon.Set(CCL::GameUI::MakeUINode(canvas)).Set(it).Set(img).Set(ic);
        m_iconIDs.push_back(icon.ID());

        // --- 行名（アイコンの左） ---
        auto lbl = EntityFactory::SpawnNamed(*m_world, ("PromptLabel_" + suffix).c_str(), labelArch);

        UITextComponent tx;
        tx.text      = cfg.label;
        tx.pixelSize = m_textSize;
        tx.sortOrder = 121;

        lbl.Set(CCL::GameUI::MakeUINode(canvas)).Set(lt).Set(tx);
        m_labelIDs.push_back(lbl.ID());
    }
}

void InputPromptSystem::Update(float /*dt*/)
{
    // プレイヤーが居ない画面（タイトル等）には操作説明を出さない。
    // 生成そのものを遅らせるので、UIエンティティも作られない。
    if (!m_built)
    {
        if (!m_world->HasResource<BattleContext>()) return;
        if (!m_world->GetResource<BattleContext>().HasPlayer()) return;

        BuildRows();
        m_built = true;
    }

    // 行名の表示は、対になるアイコンが出ているかに合わせる。
    //   割り当ての無い操作（パッドに無い FreeLook 等）でアイコンだけ消えると、
    //   文字が宙に浮いて「アイコンの読み込みに失敗した」ように見えるため。
    const size_t count = (std::min)(m_iconIDs.size(), m_labelIDs.size());
    for (size_t i = 0; i < count; ++i)
    {
        const auto *img = m_world->GetComponent<UIImageComponent>(m_iconIDs[i]);
        auto       *txt = m_world->GetComponent<UITextComponent>(m_labelIDs[i]);
        if (img && txt) txt->visible = img->visible;
    }
}

void InputPromptSystem::OnGui()
{
    ImGui::TextDisabled("%zu 行を表示中", m_iconIDs.size());
    ImGui::TextDisabled("行の増減・文言は Assets/Config/InputPromptConfig.json");
    ImGui::Separator();

    bool changed = false;
    changed |= ImGui::SliderFloat("Icon Size", &m_iconSize, 8.0f, 128.0f, "%.0f px");
    changed |= ImGui::SliderFloat("Row Height", &m_rowHeight, 8.0f, 160.0f, "%.0f px");
    changed |= ImGui::SliderFloat("Margin Right", &m_marginRight, 0.0f, 400.0f, "%.0f px");
    changed |= ImGui::SliderFloat("Margin Top", &m_marginTop, 0.0f, 600.0f, "%.0f px");
    changed |= ImGui::SliderFloat("Label Width", &m_labelWidth, 20.0f, 320.0f, "%.0f px");
    changed |= ImGui::SliderFloat("Label Gap", &m_labelGap, 0.0f, 80.0f, "%.0f px");
    changed |= ImGui::SliderFloat("Text Size", &m_textSize, 6.0f, 64.0f, "%.0f px");
    changed |= ImGui::SliderFloat("Alpha", &m_alpha, 0.0f, 1.0f, "%.2f");

    // 行送りがアイコンより狭いと上下で重なる。値だけでは気付けないので明示する。
    if (m_rowHeight < m_iconSize)
    {
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f),
                           "Row Height < Icon Size：行が %.0fpx 重なります", m_iconSize - m_rowHeight);
    }

    ImGui::Separator();
    if (ImGui::Button("既定値に戻す"))
    {
        // 調整値だけを戻す。既定値の定義はヘッダのメンバ初期化子1箇所しかないので、
        // 一時オブジェクトから写して二重管理にしない（m_iconIDs 等は触らない）。
        const InputPromptSystem def{};
        m_iconSize    = def.m_iconSize;
        m_rowHeight   = def.m_rowHeight;
        m_marginRight = def.m_marginRight;
        m_marginTop   = def.m_marginTop;
        m_labelWidth  = def.m_labelWidth;
        m_labelGap    = def.m_labelGap;
        m_textSize    = def.m_textSize;
        m_alpha       = def.m_alpha;
        changed       = true;
    }

    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("InputPromptSystem.h の調整用ブロックへ貼り付けてください。");

    if (changed) ApplyLayout();
}

// HUD同期の帯。ゲージ類と同じタイミングで表示だけを更新する。
REGISTER_LOGIC_SYSTEM(InputPromptSystem, Priority::LogicStage::L06_HudSync);
