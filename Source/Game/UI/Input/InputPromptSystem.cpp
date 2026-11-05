#include "Game/UI/Input/InputPromptSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/Common/CCL_Archetype.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/Serialization/Factory/EntityFactory.h"
#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Platform/Core/FileSystem.h"

#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/UI/ECS/Components/UITransformComponent.h"
#include "Engine/UI/ECS/Components/UIImageComponent.h"
#include "Engine/UI/ECS/Components/UITextComponent.h"
#include "Game/UI/Input/InputIconComponent.h"
#include "Game/UI/Common/MenuUiFactory.h"
#include "Game/Logics/AI/Battle/BattleContext.h"

#include <cstdio>
#include <imgui.h>
#include <json.hpp>

using namespace CCL::ECS;
using json = nlohmann::json;

namespace
{
    constexpr const char *kConfigPath = "Assets/Config/InputPromptConfig.json";
    constexpr const char *kPrefabPath = "Assets/Prefabs/UI/InputPrompt.json";
    constexpr const char *kStyleName  = "InputPrompt"; // 調整値を持つプレハブのエンティティ名
} // namespace



// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------

void InputPromptSystem::Update(float /*dt*/)
{
    // --- 生成 ---
    // プレイヤーの操作説明なので、プレイヤーが現れてから作る
    // Initialize の時点では、まだレベル（プレイヤー）を読み込んでいない
    if (!m_built)
    {
        if (!m_world->HasResource<BattleContext>()) return;
        if (!m_world->GetResource<BattleContext>().HasPlayer()) return;

        Build();
        m_built = true;
    }

    // --- 並べ方 ---
    // インスペクタで調整値を変えたとき、その場で反映されるよう毎フレーム並べ直す（数行なので軽い）
    ApplyLayout(GetStyle());

    // --- 行名の表示 ---
    // 行名は、同じ行のアイコンが出ているときだけ出す
    // 割り当てのない操作でアイコンだけが消えると、読み込みに失敗したように見えるため
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
    // 値の調整はインスペクタ、ここは今の状態を確かめる場所
    ImGui::TextDisabled("%zu 行を表示中", m_iconIDs.size());
    ImGui::TextDisabled("並べ方は Hierarchy の %s をインスペクタで調整し、プレハブ保存で %s へ残す", kStyleName, kPrefabPath);
    ImGui::TextDisabled("行の増減・文言は %s", kConfigPath);

    // 行の高さがアイコンより小さいと上下で重なる。値だけでは気付きにくいので表示する
    const InputPromptStyleComponent &style = GetStyle();
    if (style.rowHeight < style.iconSize)
    {
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f),
                           "行の高さ < アイコンの大きさ：行が %.0fpx 重なります", style.iconSize - style.rowHeight);
    }
}



// ---------------------------------------------------------------------------
// 行の組み立てと配置
// ---------------------------------------------------------------------------

void InputPromptSystem::Build()
{
    // --- 調整値 ---
    // 読めなければ既定値で並べる（理由は Prefab がログに出す）
    m_styleID = Prefab::SpawnPrefab(*m_world, kPrefabPath);

    // --- 行 ---
    // 行はプレハブの子にせず、別の Canvas に置く
    // 子にすると、プレハブ保存で実行中に作った行まで保存され、次に作ったとき行が二重になる
    std::vector<ConfigRow> rows;
    LoadConfig(rows);

    const EntityID canvas = CCL::GameUI::SpawnCanvas(*m_world, "InputPromptCanvas");

    auto iconArch  = ArchetypeHelper::Generate<TransformComponent, UITransformComponent,
                                               UIImageComponent, InputIconComponent>();
    auto labelArch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent,
                                               UITextComponent>();

    const InputPromptStyleComponent &style = GetStyle();
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const ConfigRow  &cfg    = rows[i];
        const std::string suffix = cfg.action.empty() ? cfg.icon : cfg.action;

        UITransformComponent it{};
        UITransformComponent lt{};
        ComputeRow(i, style, it, lt);

        // --- アイコン ---
        auto icon = EntityFactory::SpawnNamed(*m_world, ("Prompt_" + suffix).c_str(), iconArch);

        UIImageComponent img;
        img.sortOrder = 120;   // ゲージ（100〜102）より手前
        img.visible   = false; // InputIconSystem が画像を決めるまで出さない

        // どのアイコンかだけを指定し、画像は InputIconSystem が決める
        InputIconComponent ic;
        if (cfg.action.empty())
        {
            if (cfg.icon == "Camera")
            {
                ic.kind = InputIconKind::Camera;
            }
            else
            {
                ic.kind = InputIconKind::Move;
            }
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
        tx.pixelSize = style.textSize;
        tx.sortOrder = 121;

        lbl.Set(CCL::GameUI::MakeUINode(canvas)).Set(lt).Set(tx);
        m_labelIDs.push_back(lbl.ID());
    }
}



void InputPromptSystem::ComputeRow(size_t index, const InputPromptStyleComponent &style,
                                   UITransformComponent &outIcon, UITransformComponent &outLabel) const
{
    const float y = style.marginTop + style.rowHeight * static_cast<float>(index);

    // アイコン: 自分の右上を画面の右上に合わせ、右端から marginRight だけ離す
    outIcon.anchor = {1.0f, 0.0f};
    outIcon.pivot  = {1.0f, 0.0f};
    outIcon.offset = {-style.marginRight, y};
    outIcon.size   = {style.iconSize, style.iconSize};
    outIcon.alpha  = style.alpha;

    // 行名: アイコンの左に置く
    // 文字は左上から描かれるので、アイコンの縦の中央に来るよう少し下げる
    outLabel.anchor = {1.0f, 0.0f};
    outLabel.pivot  = {1.0f, 0.0f};
    outLabel.offset = {-(style.marginRight + style.iconSize + style.labelGap),
                       y + (style.iconSize - style.textSize) * 0.5f};
    outLabel.size   = {style.labelWidth, style.textSize};
    outLabel.alpha  = style.alpha;
}



void InputPromptSystem::ApplyLayout(const InputPromptStyleComponent &style)
{
    const size_t count = (std::min)(m_iconIDs.size(), m_labelIDs.size());
    for (size_t i = 0; i < count; ++i)
    {
        UITransformComponent it{};
        UITransformComponent lt{};
        ComputeRow(i, style, it, lt);

        // computed* は UILayoutSystem が毎フレーム書くので触らない
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
        if (auto *tx = m_world->GetComponent<UITextComponent>(m_labelIDs[i])) tx->pixelSize = style.textSize;
    }
}



const InputPromptStyleComponent &InputPromptSystem::GetStyle() const
{
    static const InputPromptStyleComponent kDefaultStyle{};
    const auto *style = m_world->GetComponent<InputPromptStyleComponent>(m_styleID);
    return style ? *style : kDefaultStyle;
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
            // 壊れた JSON なら、下で既定の並びにする
            outRows.clear();
        }
    }

    // 設定が読めなくても、操作の説明は消さない
    // 説明が出ないと、その操作がないように見えるため
    if (outRows.empty())
    {
        outRows = {
            {"", "Move", "MOVE"},
            {"", "Camera", "CAMERA"},
            {"Attack", "", "ATTACK"},
            {"Dash", "", "DODGE"},
            {"Jump", "", "JUMP"},
            {"LockOn", "", "LOCK ON"},
        };
    }
}



// 全体登録はしない。TutorialScene::RegisterSystems だけが登録する
