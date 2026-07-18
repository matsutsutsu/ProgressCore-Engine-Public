#pragma once
/// @file MenuUiFactory.h
/// @brief メニュー系UI（タイトル/リザルト等）の共通生成ヘルパー。
///        ボタン・ラベル・暗転パネルを ECS UI エンティティとして生成する。
///
/// 設計方針:
///   - タイトルとリザルトで同じボタン生成をするため、重複を1か所に集約。
///   - 生成のみを担当（状態管理や遷移は各システム側の責務）。
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/Common/CCL_Archetype.h"
#include "Engine/Serialization/Factory/EntityFactory.h"

#include "Engine/UI/ECS/Components/UITransformComponent.h"
#include "Engine/UI/ECS/Components/UIImageComponent.h"
#include "Engine/UI/ECS/Components/UIInteractableComponent.h"
#include "Engine/UI/ECS/Components/UIButtonComponent.h"
#include "Engine/UI/ECS/Components/UIAnimationComponent.h"
#include "Engine/UI/ECS/Components/UITextComponent.h"

#include <string>

namespace CCL::GameUI
{
    /// @brief 画面中央アンカーのテキストラベルを生成する。
    inline CCL::ECS::EntityID SpawnLabel(CCL::ECS::Core::World &world, const char *name,
        const std::string &text, DirectX::XMFLOAT2 offset, float pixelSize, int sortOrder = 11)
    {
        auto arch = CCL::ECS::ArchetypeHelper::Generate<UITransformComponent, UITextComponent>();
        auto e    = EntityFactory::SpawnNamed(world, name, arch);

        UITransformComponent t;
        t.anchor = {0.5f, 0.5f};
        t.pivot  = {0.5f, 0.5f};
        t.offset = offset;
        t.size   = {400.0f, 48.0f};

        UITextComponent tx;
        tx.text      = text;
        tx.pixelSize = pixelSize;
        tx.sortOrder = sortOrder;

        e.Set(t).Set(tx);
        return e.ID();
    }

    /// @brief 単色パネル（暗転オーバーレイ等）を生成する。size を大きくすれば全画面を覆える。
    inline CCL::ECS::EntityID SpawnPanel(CCL::ECS::Core::World &world, const char *name,
        DirectX::XMFLOAT2 offset, DirectX::XMFLOAT2 size, DirectX::XMFLOAT4 color, int sortOrder)
    {
        auto arch = CCL::ECS::ArchetypeHelper::Generate<UITransformComponent, UIImageComponent>();
        auto e    = EntityFactory::SpawnNamed(world, name, arch);

        UITransformComponent t;
        t.anchor = {0.5f, 0.5f};
        t.pivot  = {0.5f, 0.5f};
        t.offset = offset;
        t.size   = size;

        UIImageComponent img;
        img.color     = color;
        img.sortOrder = sortOrder;

        e.Set(t).Set(img);
        return e.ID();
    }

    /// @brief ボタン（背景パネル＋当たり判定＋ボタン見た目＋拡大演出）と、その上に重ねる
    ///        ラベル（テキスト）を生成する。戻り値＝操作対象のボタン EntityID（初期フォーカス用）。
    /// @param sortOrder ボタン背景の描画順。ラベルは +1 で少し手前に置く。
    inline CCL::ECS::EntityID SpawnButton(CCL::ECS::Core::World &world, const char *btnName,
        const char *labelName, const std::string &label, DirectX::XMFLOAT2 offset,
        const std::string &action, bool focused, int sortOrder = 10)
    {
        using namespace CCL::ECS;

        // --- ボタン本体 ---
        auto btnArch = ArchetypeHelper::Generate<UITransformComponent, UIImageComponent,
            UIInteractableComponent, UIButtonComponent, UIAnimationComponent>();
        auto btn = EntityFactory::SpawnNamed(world, btnName, btnArch);

        UITransformComponent bt;
        bt.anchor = {0.5f, 0.5f};
        bt.pivot  = {0.5f, 0.5f};
        bt.offset = offset;
        bt.size   = {320.0f, 64.0f};

        UIImageComponent bimg;
        bimg.sortOrder = sortOrder;

        UIInteractableComponent inter;
        inter.focusable = true;
        inter.isFocused = focused; // true にすると起動時から選択状態

        UIButtonComponent bcomp;
        bcomp.onClickAction = action;

        UIAnimationComponent banim;

        btn.Set(bt).Set(bimg).Set(inter).Set(bcomp).Set(banim);
        const EntityID btnID = btn.ID();

        // --- ラベル（別エンティティ・左パディングで配置） ---
        auto labelArch = ArchetypeHelper::Generate<UITransformComponent, UITextComponent>();
        auto lbl       = EntityFactory::SpawnNamed(world, labelName, labelArch);

        UITransformComponent lt;
        lt.anchor = {0.5f, 0.5f};
        lt.pivot  = {0.5f, 0.5f};
        lt.offset = {offset.x - 128.0f, offset.y - 14.0f};
        lt.size   = {280.0f, 32.0f};

        UITextComponent lx;
        lx.text      = label;
        lx.pixelSize = 28.0f;
        lx.sortOrder = sortOrder + 1;

        lbl.Set(lt).Set(lx);

        return btnID;
    }
} // namespace CCL::GameUI
