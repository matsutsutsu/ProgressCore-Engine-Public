#include "Game/UI/Input/InputPromptSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/Common/CCL_Archetype.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/Serialization/Factory/EntityFactory.h"
#include "Engine/Platform/Core/FileSystem.h"

#include "Engine/Transform/Components/TransformComponent.h" // UI の親子関係に使う
#include "Engine/UI/ECS/Components/UITransformComponent.h"
#include "Engine/UI/ECS/Components/UIImageComponent.h"
#include "Engine/UI/ECS/Components/UITextComponent.h"
#include "Game/UI/Input/InputIconComponent.h"
#include "Game/UI/Common/MenuUiFactory.h"               // SpawnCanvas / MakeUINode
#include "Game/Logics/AI/Battle/BattleContext.h" // プレイヤーの有無で生成を決める

#include <cstdio>
#include <imgui.h>
#include <json.hpp>

using namespace CCL::ECS;
using json = nlohmann::json;

namespace
{
    constexpr const char *kConfigPath = "Assets/Config/InputPromptConfig.json";
} 

// ---------------------------------------------------------------------------
// 設定の読み込み
// ---------------------------------------------------------------------------

void InputPromptSystem::LoadConfig(std::vector<ConfigRow> &outRows) const
{
    const std::string text = FileSystem::ReadAllText(kConfigPath);
    if (!text.empty())
    {
        try
        {
            json doc = json::parse(text);
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
            outRows.clear(); // 壊れた JSON なら既定の並びにする（下で入れる）
        }
    }

    // 設定が読めなくても操作の説明が消えないよう、既定の並びを持つ。
    // 説明が出ないと、その操作が無いように見えるため
    if (outRows.empty())
    {
        outRows = {{"", "Move", "MOVE"}, {"", "Camera", "CAMERA"}, {"Attack", "", "ATTACK"}, {"Dash", "", "DODGE"}, {"Jump", "", "JUMP"}, {"LockOn", "", "LOCK ON"}};
    }
}

// ---------------------------------------------------------------------------
// 行の組み立てと配置
// ---------------------------------------------------------------------------

void InputPromptSystem::ComputeRow(size_t index, UITransformComponent &outIcon,
                                   UITransformComponent &outLabel) const
{
    const float y = m_marginTop + m_rowHeight * static_cast<float>(index);

    // アイコン: 自分の右上を画面の右上に合わせ、右端から m_marginRight だけ離す
    outIcon.anchor = {1.0f, 0.0f};
    outIcon.pivot  = {1.0f, 0.0f};
    outIcon.offset = {-m_marginRight, y};
    outIcon.size   = {m_iconSize, m_iconSize};
    outIcon.alpha  = m_alpha;

    // 行名: アイコンの左。文字は computedPos を左上にして描かれるので、
    // アイコンの縦の中央に来るよう少し下げる
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
        // まるごと代入すると、求めてある位置が1フレームだけ消えてしまう
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

    // アイコンと行名はこの Canvas の子にする
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
        img.sortOrder = 120;   // ゲージ（100〜102）より手前
        img.visible   = false; // InputIconSystem が画像を決めるまで出さない

        // どのアイコンかだけを指定し、画像は InputIconSystem が決める
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

// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------

void InputPromptSystem::Update(float /*dt*/)
{
    // プレイヤーが居ない画面（タイトルなど）には出さない。作るのを遅らせるので、UI エンティティも作られない
    if (!m_built)
    {
        if (!m_world->HasResource<BattleContext>()) return;
        if (!m_world->GetResource<BattleContext>().HasPlayer()) return;

        BuildRows();
        m_built = true;
    }

    // 行名は、同じ行のアイコンが出ているときだけ出す。割り当ての無い操作（パッドに無い FreeLook など）で
    // アイコンだけが消えると、文字だけが残ってアイコンの読み込みに失敗したように見えるため
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

    // 行の高さがアイコンより小さいと上下で重なる。値だけでは気付きにくいので表示する
    if (m_rowHeight < m_iconSize)
    {
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f),
                           "Row Height < Icon Size：行が %.0fpx 重なります", m_iconSize - m_rowHeight);
    }

    ImGui::Separator();
    if (ImGui::Button("既定値に戻す"))
    {
        // 調整値だけを戻す（m_iconIDs などは触らない）。既定値はヘッダーのメンバ初期化子にしか
        // 書いていないので、一時オブジェクトから写して、既定値を2か所に書かないようにする
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

// ゲージと同じ HUD のステージで表示を更新する
REGISTER_LOGIC_SYSTEM(InputPromptSystem, Priority::LogicStage::L06_HudSync);
