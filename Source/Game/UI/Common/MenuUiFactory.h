#pragma once
// ===========================================================================
// 概要: メニューと HUD の UI エンティティを作る関数をまとめたヘッダー
//
// 責務:
//   - Canvas・ラベル・単色のパネル・ボタン・操作のヒントを作る
//   - UI の親子関係と、出てくるときのアニメーションを設定する
//   - メニュー共通の見た目（MenuStyle）を引く
//
// 備考: 作るだけで、状態の管理やシーンの切り替えは呼び出す System が行う
// ===========================================================================
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/Common/CCL_Archetype.h"
#include "Engine/Serialization/Factory/EntityFactory.h"

#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/UI/ECS/Components/UITransformComponent.h"
#include "Engine/UI/ECS/Components/UICanvasComponent.h"
#include "Engine/UI/ECS/Components/UIImageComponent.h"
#include "Engine/UI/ECS/Components/UIInteractableComponent.h"
#include "Engine/UI/ECS/Components/UIButtonComponent.h"
#include "Engine/UI/ECS/Components/UIProgressBarComponent.h"
#include "Engine/UI/ECS/Components/UIAppearComponent.h"
#include "Engine/UI/ECS/Components/UITextComponent.h"
#include "Game/UI/Input/InputIconComponent.h"
#include "Game/UI/Common/MenuButtonVisualComponent.h"
#include "Game/UI/Common/MenuStyleComponent.h"

#include <string>
#include <vector>

namespace CCL::GameUI
{
    // ---------------------------------------------------------------------------
    // メニュー共通の見た目
    // ---------------------------------------------------------------------------

    // MenuStyle（MenuStyleSystem が置く）を探す。まだないシーンでは InvalidEntityID
    inline CCL::ECS::EntityID FindMenuStyle(CCL::ECS::World &world)
    {
        for (const CCL::ECS::EntityID id : world.View<MenuStyleComponent>())
        {
            if (world.GetComponent<MenuStyleComponent>(id)) return id;
        }
        return CCL::ECS::InvalidEntityID;
    }

    // メニュー共通の見た目を返す。MenuStyle がまだないシーンでは既定値
    // 値で返すのは、呼び出し側が途中で生成を挟んでも参照が無効にならないようにするため
    inline MenuStyleComponent GetMenuStyle(CCL::ECS::World &world)
    {
        const auto *style = world.GetComponent<MenuStyleComponent>(FindMenuStyle(world));
        return style ? *style : MenuStyleComponent{};
    }

    // ---------------------------------------------------------------------------
    // 共通の部品
    // ---------------------------------------------------------------------------

    // SpawnButton が作ったボタンとラベル（消すときに両方消せるよう、ラベルも返す）
    struct MenuButtonIds
    {
        CCL::ECS::EntityID button = CCL::ECS::InvalidEntityID; // ボタン（当たり判定と帯）
        CCL::ECS::EntityID label  = CCL::ECS::InvalidEntityID; // ラベル（UIText）
    };

    // 出てくるときのアニメーションを設定する
    // appearDelay が 0 以上なら再生し、出てくるまでは見えなく（alpha = 0）する
    inline void ConfigureAppear(UITransformComponent &t, UIAppearComponent &ap, float appearDelay)
    {
        if (appearDelay < 0.0f) return;

        ap.play  = true;
        ap.delay = appearDelay;
        t.alpha  = 0.0f;
    }

    /**
     * @brief UI の親子関係に使う TransformComponent を作る
     *
     * 配置は UITransformComponent が決めるが、親子関係は 3D と同じ TransformComponent を使う
     * Hierarchy の表示・ドラッグでの親子付け・入れ子の配置の計算がこれ1つで済む
     *
     * @param parent 親のエンティティ。無効ならルート（0）にする
     */
    inline TransformComponent MakeUINode(CCL::ECS::EntityID parent)
    {
        TransformComponent tr;
        tr.parent   = (parent != CCL::ECS::InvalidEntityID) ? parent : 0;
        tr.isStatic = false;
        return tr;
    }

    /**
     * @brief UI の親子関係のルートになる Canvas を作る。矩形は常に画面全体
     * @param offset 子の UI をまとめて平行移動する量（px）
     * @note 3D の Transform は親子関係のためだけに使うので、動かしても UI の位置は変わらない
     */
    inline CCL::ECS::EntityID SpawnCanvas(CCL::ECS::World &world, const char *name,
                                          DirectX::XMFLOAT2 offset = {0.0f, 0.0f})
    {
        auto arch = CCL::ECS::ArchetypeHelper::Generate<TransformComponent, UITransformComponent,
                                                        UICanvasComponent>();
        auto e    = EntityFactory::SpawnNamed(world, name, arch);

        UITransformComponent t;
        t.offset = offset;

        e.Set(MakeUINode(CCL::ECS::InvalidEntityID)).Set(t).Set(UICanvasComponent{});
        return e.ID();
    }

    /**
     * @brief 画面の中央を基準にした文字のラベルを作る
     * @param appearDelay 0 以上なら、その秒数だけ待ってから出てくる。負なら出てくる演出なし
     * @param parent      UI の親（Canvas など）。矩形も親を基準に求める
     */
    inline CCL::ECS::EntityID SpawnLabel(CCL::ECS::World &world, const char *name,
                                         const std::string &text, DirectX::XMFLOAT2 offset, float pixelSize, int sortOrder = 11,
                                         float appearDelay = -1.0f, CCL::ECS::EntityID parent = CCL::ECS::InvalidEntityID)
    {
        auto arch = CCL::ECS::ArchetypeHelper::Generate<TransformComponent, UITransformComponent,
                                                        UITextComponent, UIAppearComponent>();
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

        UIAppearComponent ap;
        ConfigureAppear(t, ap, appearDelay);

        e.Set(MakeUINode(parent)).Set(t).Set(tx).Set(ap);
        return e.ID();
    }

    /**
     * @brief 単色のパネル（暗転など）を作る。size を大きくすれば画面全体を覆える
     * @param appearDelay  0 以上なら、その秒数だけ待ってから出てくる。負なら出てくる演出なし
     * @param appearSlideY 出てくるときに上へスライドする量（px）。暗転は 0（フェードだけ）
     * @param colorBottom  指定すると、上端 color から下端 colorBottom への縦のグラデーションになる
     */
    inline CCL::ECS::EntityID SpawnPanel(CCL::ECS::World &world, const char *name,
                                         DirectX::XMFLOAT2 offset, DirectX::XMFLOAT2 size, DirectX::XMFLOAT4 color, int sortOrder,
                                         float appearDelay = -1.0f, float appearSlideY = 28.0f,
                                         CCL::ECS::EntityID       parent      = CCL::ECS::InvalidEntityID,
                                         const DirectX::XMFLOAT4 *colorBottom = nullptr)
    {
        auto arch = CCL::ECS::ArchetypeHelper::Generate<TransformComponent, UITransformComponent,
                                                        UIImageComponent, UIAppearComponent>();
        auto e    = EntityFactory::SpawnNamed(world, name, arch);

        UITransformComponent t;
        t.anchor = {0.5f, 0.5f};
        t.pivot  = {0.5f, 0.5f};
        t.offset = offset;
        t.size   = size;

        UIImageComponent img;
        img.color     = color;
        img.sortOrder = sortOrder;
        if (colorBottom)
        {
            img.useGradient = true;
            img.colorBottom = *colorBottom;
        }

        UIAppearComponent ap;
        ap.slideY = appearSlideY;
        ConfigureAppear(t, ap, appearDelay);

        e.Set(MakeUINode(parent)).Set(t).Set(img).Set(ap);
        return e.ID();
    }

    // ---------------------------------------------------------------------------
    // ボタンと操作のヒント
    // ---------------------------------------------------------------------------

    /**
     * @brief ボタン（当たり判定・斜めの帯）と、その上に重ねるラベルを作る
     *
     * 普段は暗い板だけが見え、選ぶと帯が左から伸び、押すと白く飛んでグリッチが走る
     * 動かすのは MenuButtonStyleSystem。大きさと色は MenuStyle から取る
     *
     * @param focused     最初から選ばれた状態にするか
     * @param sortOrder   帯の描画順。ラベルは +1 で手前に置く
     * @param appearDelay 0 以上なら、その秒数だけ待ってから出てくる（ボタンとラベルの両方）。負なら出てくる演出なし
     */
    inline MenuButtonIds SpawnButton(CCL::ECS::World &world, const char *btnName,
                                     const char *labelName, const std::string &label, DirectX::XMFLOAT2 offset,
                                     const std::string &action, bool focused, int sortOrder = 10, float appearDelay = -1.0f,
                                     CCL::ECS::EntityID parent = CCL::ECS::InvalidEntityID)
    {
        using namespace CCL::ECS;

        const MenuStyleComponent style = GetMenuStyle(world);
        const DirectX::XMFLOAT2  size  = style.buttonSize;

        // --- ボタン ---
        auto btnArch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent,
                                                 UIImageComponent, UIInteractableComponent, UIButtonComponent,
                                                 UIProgressBarComponent, MenuButtonVisualComponent, UIAppearComponent>();
        auto btn     = EntityFactory::SpawnNamed(world, btnName, btnArch);

        UITransformComponent bt;
        bt.anchor = {0.5f, 0.5f};
        bt.pivot  = {0.5f, 0.5f};
        bt.offset = offset;
        bt.size   = size;

        // UIButtonSystem が状態ごとに UIImage の色を差し替えるので画像は残す
        // 見た目は帯が受け持つので、色は全部透明にする
        UIImageComponent bimg;
        bimg.sortOrder = sortOrder;

        UIInteractableComponent inter;
        inter.focusable = true;
        inter.isFocused = focused;

        UIButtonComponent bcomp;
        bcomp.onClickAction = action;
        bcomp.normalColor   = {0.0f, 0.0f, 0.0f, 0.0f};
        bcomp.hoverColor    = bcomp.normalColor;
        bcomp.pressedColor  = bcomp.normalColor;
        bcomp.disabledColor = bcomp.normalColor;

        // 選択の帯。value（伸びた量）は MenuButtonStyleSystem が選択に合わせて動かす
        UIProgressBarComponent bar;
        bar.value          = 0.0f;
        bar.sortOrder      = sortOrder;
        bar.bgColor        = style.buttonBg;
        bar.fillColor      = style.buttonFill;
        bar.gradient       = 0.25f;
        bar.slant          = style.buttonSlant;
        bar.glowColor      = style.buttonGlow;
        bar.glowWidth      = 10.0f;
        bar.sheen          = style.buttonSheen;
        bar.glitch         = 1.0f; // 押した瞬間のグリッチ
        bar.hitFlash       = 0.6f;
        bar.shakeAmplitude = 4.0f;

        // 最初から選ばれているボタンは、帯が伸びた状態から始める
        MenuButtonVisualComponent vis;
        vis.focus = focused ? 1.0f : 0.0f;

        UIAppearComponent bap;
        ConfigureAppear(bt, bap, appearDelay);

        // --- ラベル ---
        // 別のエンティティにして、帯の左寄りに置く。親はボタンと同じ
        auto labelArch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent,
                                                   UITextComponent, UIAppearComponent>();
        auto lbl       = EntityFactory::SpawnNamed(world, labelName, labelArch);

        UITransformComponent lt;
        lt.anchor = {0.5f, 0.5f};
        lt.pivot  = {0.0f, 0.5f}; // 左端で置く（帯の左端からの距離で決められる）
        lt.offset = {offset.x - size.x * 0.5f + style.labelPad, offset.y - style.labelPx * 0.5f};
        lt.size   = {size.x - style.labelPad, style.labelPx + 4.0f};

        // 色は MenuButtonStyleSystem が毎フレーム付けるが、出てすぐの1フレームも同じ色にしておく
        UITextComponent lx;
        lx.text      = label;
        lx.pixelSize = style.labelPx;
        lx.color     = style.labelIdle;
        lx.sortOrder = sortOrder + 1;

        UIAppearComponent lap;
        ConfigureAppear(lt, lap, appearDelay);

        lbl.Set(MakeUINode(parent)).Set(lt).Set(lx).Set(lap);
        const EntityID lblID = lbl.ID();

        vis.label = lblID;
        btn.Set(MakeUINode(parent)).Set(bt).Set(bimg).Set(inter).Set(bcomp).Set(bar).Set(vis).Set(bap);

        return {btn.ID(), lblID};
    }

    /**
     * @brief メニューの下に出す操作のヒント（決定・選択）を作る
     *
     * 全画面が同じ行を出すので、ここにまとめる
     * 画面ごとに書くと、座標やアイコンの種類がずれていく
     *
     * @param yOffset 画面の中央からの縦の位置（px）。ボタンの下に来る値を渡す
     * @param out     作ったエンティティを足す先（呼び出し側がまとめて消すため）
     * @param xOffset 行全体を横に動かす量（px）。メニューを左に寄せた画面で使う
     * @note アイコンの画像は InputIconSystem が毎フレーム決める。ここは種類を指定するだけ
     */
    inline void SpawnMenuHints(CCL::ECS::World &world, CCL::ECS::EntityID parent, float yOffset,
                               std::vector<CCL::ECS::EntityID> &out, int sortOrder = 230, float appearDelay = -1.0f,
                               float xOffset = 0.0f)
    {
        using namespace CCL::ECS;

        struct HintDef
        {
            InputIconKind kind;  // アイコンの種類
            const char   *label; // アイコンの右に出す文字
            const char   *name;  // エンティティの名前
            float         x;     // 行の中で置く横位置（px）
        };

        // 決定を左、選択を右にする。よく押す方を先に読ませる
        const HintDef defs[] = {
            {InputIconKind::MenuConfirm, "決定", "HintConfirm", -150.0f},
            {InputIconKind::MenuNavigate, "選択", "HintNavigate", 30.0f},
        };

        const MenuStyleComponent style  = GetMenuStyle(world);
        const float              iconPx = style.hintIconPx;
        const float              textPx = style.hintTextPx;

        for (const HintDef &d : defs)
        {
            // --- アイコン ---
            auto iconArch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent,
                                                      UIImageComponent, InputIconComponent, UIAppearComponent>();
            auto icon     = EntityFactory::SpawnNamed(world, d.name, iconArch);

            UITransformComponent it;
            it.anchor = {0.5f, 0.5f};
            it.pivot  = {0.5f, 0.5f};
            it.offset = {d.x + xOffset, yOffset};
            it.size   = {iconPx, iconPx};

            UIImageComponent img;
            img.sortOrder = sortOrder;
            img.visible   = false; // InputIconSystem が画像を決めるまで出さない

            InputIconComponent ic;
            ic.kind = d.kind;

            // スライドさせずフェードだけにする（目立たせたいのはボタン）
            UIAppearComponent ap;
            ap.slideY = 0.0f;
            ConfigureAppear(it, ap, appearDelay);

            icon.Set(MakeUINode(parent)).Set(it).Set(img).Set(ic).Set(ap);
            out.push_back(icon.ID());

            // --- ラベル ---
            // アイコンの右に置く。文字は左上を基準に描くので、縦の中央に来るよう上へずらす
            auto lblArch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent,
                                                     UITextComponent, UIAppearComponent>();
            auto lbl     = EntityFactory::SpawnNamed(world, (std::string(d.name) + "Label").c_str(), lblArch);

            UITransformComponent lt;
            lt.anchor = {0.5f, 0.5f};
            lt.pivot  = {0.5f, 0.5f};
            lt.offset = {d.x + xOffset + iconPx + 44.0f, yOffset - (textPx * 0.5f)};
            lt.size   = {120.0f, textPx};

            UITextComponent tx;
            tx.text      = d.label;
            tx.pixelSize = textPx;
            tx.color     = style.hintColor;
            tx.sortOrder = sortOrder + 1;

            UIAppearComponent lap;
            lap.slideY = 0.0f;
            ConfigureAppear(lt, lap, appearDelay);

            lbl.Set(MakeUINode(parent)).Set(lt).Set(tx).Set(lap);
            out.push_back(lbl.ID());
        }
    }
} // namespace CCL::GameUI
