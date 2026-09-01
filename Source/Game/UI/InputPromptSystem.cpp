/// @file InputPromptSystem.cpp
/// 役割・設計方針は InputPromptSystem.h を参照。
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
#include "Game/UI/MenuUiFactory.h" // SpawnCanvas / MakeUINode

#include <fstream>
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
        outRows = {{"", "Move", "MOVE"},      {"", "Camera", "CAMERA"},
                   {"Attack", "", "ATTACK"},  {"Cleave", "", "CLEAVE"},
                   {"Dash", "", "DODGE"},     {"Jump", "", "JUMP"},
                   {"LockOn", "", "LOCK ON"}};
    }
}

void InputPromptSystem::Initialize()
{
    std::vector<ConfigRow> rows;
    LoadConfig(rows);

    // UI階層のルート。配下をこの Canvas の子にしてヒエラルキーで束ねる。
    const EntityID canvas = CCL::GameUI::SpawnCanvas(*m_world, "InputPromptCanvas");

    auto iconArch  = ArchetypeHelper::Generate<TransformComponent, UITransformComponent,
        UIImageComponent, InputIconComponent>();
    auto labelArch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent,
        UITextComponent>();

    float y = kMarginTop;
    for (const ConfigRow &cfg : rows)
    {
        const std::string suffix = cfg.action.empty() ? cfg.icon : cfg.action;

        // --- アイコン（右端に寄せる） ---
        auto icon = EntityFactory::SpawnNamed(*m_world, ("Prompt_" + suffix).c_str(), iconArch);

        UITransformComponent it;
        it.anchor = {1.0f, 0.0f};   // 画面右上
        it.pivot  = {1.0f, 0.0f};   // 自身の右上をアンカーへ合わせる
        it.offset = {-kMarginRight, y};
        it.size   = {kIconSize, kIconSize};
        it.alpha  = kAlpha;

        UIImageComponent img;
        img.sortOrder = 120;        // ゲージ(100〜102)より手前
        img.visible   = false;      // InputIconSystem が張り替えるまで出さない

        // どのアイコンかは宣言だけして、解決は InputIconSystem に任せる。
        InputIconComponent ic;
        if (cfg.action.empty())
        {
            if (cfg.icon == "Camera") ic.kind = InputIconKind::Camera;
            else                      ic.kind = InputIconKind::Move;
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

        UITransformComponent lt;
        lt.anchor = {1.0f, 0.0f};
        lt.pivot  = {1.0f, 0.0f};
        // 文字は computedPos の左上から描かれるので、アイコンの縦中央に来るよう少し下げる。
        lt.offset = {-(kMarginRight + kIconSize + 10.0f), y + (kIconSize - kTextSize) * 0.5f};
        lt.size   = {kLabelWidth, kTextSize};
        lt.alpha  = kAlpha;

        UITextComponent tx;
        tx.text      = cfg.label;
        tx.pixelSize = kTextSize;
        tx.sortOrder = 121;

        lbl.Set(CCL::GameUI::MakeUINode(canvas)).Set(lt).Set(tx);
        m_labelIDs.push_back(lbl.ID());

        y += kRowHeight;
    }
}

void InputPromptSystem::Update(float /*dt*/)
{
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

// HUD同期の帯。ゲージ類と同じタイミングで表示だけを更新する。
REGISTER_LOGIC_SYSTEM(InputPromptSystem, Priority::LogicStage::L06_HudSync);
