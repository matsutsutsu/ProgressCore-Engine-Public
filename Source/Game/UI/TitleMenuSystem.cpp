/// @file TitleMenuSystem.cpp
/// 役割・設計方針は TitleMenuSystem.h を参照。
#include "TitleMenuSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/Common/CCL_Archetype.h"
#include "Engine/Serialization/Factory/EntityFactory.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Core/Math/StringHash.h"

#include "Engine/UI/ECS/UIEvents.h"
#include "Engine/UI/ECS/Components/UITransformComponent.h"
#include "Engine/UI/ECS/Components/UIImageComponent.h"
#include "Engine/UI/ECS/Components/UIInteractableComponent.h"
#include "Engine/UI/ECS/Components/UIAnimationComponent.h"
#include "Engine/UI/ECS/Components/UITextComponent.h"

#include <cstdint>

using namespace CCL::ECS;

namespace
{
// ボタン（画像＋操作＋演出）と、その上に重ねるラベル（テキスト）を生成する。
// ラベルはボタンとは別エンティティ（子要素的な構成）にして左パディングで配置する。
// 戻り値＝操作対象であるボタン本体の EntityID（初期フォーカス設定などに使う）。
EntityID SpawnMenuButton(Core::World &world, const char *btnName, const char *labelName,
    const std::string &label, DirectX::XMFLOAT2 offset, const std::string &action, bool focused)
{
    // --- ボタン本体（背景パネル＋当たり判定＋ボタン見た目＋拡大演出） ---
    auto btnArch = ArchetypeHelper::Generate<UITransformComponent, UIImageComponent,
        UIInteractableComponent, UIButtonComponent, UIAnimationComponent>();
    auto btn = EntityFactory::SpawnNamed(world, btnName, btnArch);

    UITransformComponent bt;
    bt.anchor = {0.5f, 0.5f}; // 画面中央基準
    bt.pivot  = {0.5f, 0.5f}; // 自身の中心をアンカーへ
    bt.offset = offset;
    bt.size   = {320.0f, 64.0f};

    UIImageComponent bimg;
    bimg.sortOrder = 10; // 背景パネル

    UIInteractableComponent inter;
    inter.focusable = true;
    inter.isFocused = focused; // 先頭ボタンを起動時から選択状態にする

    UIButtonComponent bcomp;
    bcomp.onClickAction = action;

    UIAnimationComponent banim;

    btn.Set(bt).Set(bimg).Set(inter).Set(bcomp).Set(banim);
    const EntityID btnID = btn.ID();

    // --- ラベル（テキスト・別エンティティ） ---
    auto labelArch = ArchetypeHelper::Generate<UITransformComponent, UITextComponent>();
    auto lbl       = EntityFactory::SpawnNamed(world, labelName, labelArch);

    UITransformComponent lt;
    lt.anchor = {0.5f, 0.5f};
    lt.pivot  = {0.5f, 0.5f};
    // ボタン中心からやや左上に寄せて、左パディングのメニュー表記にする
    lt.offset = {offset.x - 128.0f, offset.y - 14.0f};
    lt.size   = {280.0f, 32.0f};

    UITextComponent lx;
    lx.text      = label;
    lx.pixelSize = 28.0f;
    lx.sortOrder = 11; // 背景パネルより手前

    lbl.Set(lt).Set(lx);

    return btnID;
}
} // namespace

void TitleMenuSystem::Initialize()
{
    Core::World &world = *_world;

    // --- タイトルロゴ（テキスト） ---
    {
        auto arch  = ArchetypeHelper::Generate<UITransformComponent, UITextComponent>();
        auto title = EntityFactory::SpawnNamed(world, "TitleLogo", arch);

        UITransformComponent tt;
        tt.anchor = {0.5f, 0.5f};
        tt.pivot  = {0.5f, 0.5f};
        tt.offset = {-150.0f, -180.0f};
        tt.size   = {400.0f, 64.0f};

        UITextComponent tx;
        tx.text      = "RuneForge";
        tx.pixelSize = 56.0f;
        tx.sortOrder = 11;

        title.Set(tt).Set(tx);
    }

    // --- ボタン（START が初期フォーカス） ---
    SpawnMenuButton(world, "BtnStart", "LblStart", "START", {0.0f, -20.0f}, "start_game", true);
    SpawnMenuButton(world, "BtnQuit", "LblQuit", "QUIT", {0.0f, 70.0f}, "quit", false);

    // --- ボタンクリックの購読（actionHash で遷移を分岐） ---
    const uint32_t kStart = CCL::Utils::HashString("start_game");
    const uint32_t kQuit  = CCL::Utils::HashString("quit");

    world.GetEventBus().Subscribe<UIButtonClickedEvent>(
        [this, kStart, kQuit](const UIButtonClickedEvent &e) {
            if (_transitioning) return; // 遷移予約後は無視（二重遷移防止）

            if (e.actionHash == kStart)
            {
                _transitioning = true;
                // ゲーム本編へ。重い Initialize は SceneLoading が裏読みする。
                SceneManager::Instance().ChangeSceneAsync("Game");
            }
            else if (e.actionHash == kQuit)
            {
                // TODO(Slice後半): アプリ終了要求。現状は何もしない。
            }
        });
}
