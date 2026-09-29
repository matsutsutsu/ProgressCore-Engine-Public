#pragma once
// ===========================================================================
// 概要: メニュー（タイトル・ポーズ・リザルト）と HUD の UI エンティティを作る関数をまとめたヘッダー。
//
// 責務:
//   - Canvas・ラベル・単色のパネル・ボタン・操作のヒントを作る
//   - UI の親子関係に使う TransformComponent と、出てくるときのアニメーションを設定する
//
// 備考: 作るだけで、状態の管理やシーンの切り替えは呼び出す System が行う。
// ===========================================================================
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/Common/CCL_Archetype.h"
#include "Engine/Serialization/Factory/EntityFactory.h"

#include "Engine/Transform/Components/TransformComponent.h" // UI の親子関係に使う
#include "Engine/UI/ECS/Components/UITransformComponent.h"
#include "Engine/UI/ECS/Components/UICanvasComponent.h"
#include "Engine/UI/ECS/Components/UIImageComponent.h"
#include "Engine/UI/ECS/Components/UIInteractableComponent.h"
#include "Engine/UI/ECS/Components/UIButtonComponent.h"
#include "Engine/UI/ECS/Components/UIAnimationComponent.h"
#include "Engine/UI/ECS/Components/UIAppearComponent.h"
#include "Engine/UI/ECS/Components/UITextComponent.h"
#include "Game/UI/InputIconComponent.h" // 操作のヒントのアイコン

#include <string>
#include <vector>

namespace CCL::GameUI
{
    // SpawnButton が作ったボタンとラベルの EntityID（消すときに両方消せるよう、ラベルも返す）
    struct MenuButtonIds
    {
        CCL::ECS::EntityID button = CCL::ECS::InvalidEntityID;
        CCL::ECS::EntityID label  = CCL::ECS::InvalidEntityID;
    };

    // 出てくるときのアニメーションを設定する。appearDelay が 0 以上なら再生し、最初は見えなく（alpha = 0）する
    inline void ConfigureAppear(UITransformComponent &t, UIAppearComponent &ap, float appearDelay)
    {
        if (appearDelay >= 0.0f)
        {
            ap.play  = true;
            ap.delay = appearDelay;
            t.alpha  = 0.0f; // 出てくるまでは見えない（UIAppearSystem が 0 から 1 へ上げる）
        }
    }

    // UI の親子関係に使う TransformComponent を作る。parent が無効ならルート（0）にする。
    // 配置は UITransformComponent が決めるが、親子関係は 3D と同じ TransformComponent を使う。
    // エディタの Hierarchy の表示・ドラッグでの親子付け・UILayoutSystem の入れ子の計算がこれ1つで済む
    inline TransformComponent MakeUINode(CCL::ECS::EntityID parent)
    {
        TransformComponent tr;
        tr.parent   = (parent != CCL::ECS::InvalidEntityID) ? parent : 0;
        tr.isStatic = false;
        return tr;
    }

    /**
     * @brief UI の親子関係のルートになる Canvas を作る。矩形は常に画面全体で、他の UI はこの子にする。
     * @param offset 子の UI をまとめて平行移動する量（px）。「メニュー全体を左下へ寄せる」ような調整を、
     *        要素ごとではなく1か所で決められる（UILayoutUtil::ResolveUINode が Canvas を特別に扱う）。
     *        エディタでは Canvas の「UI Transform (矩形)」の Offset がこれに当たる。
     * @note 3D の Transform は親子関係のためだけに使うので、動かしても UI の位置は変わらない。
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
     * @brief 画面の中央を基準にしたテキストのラベルを作る。
     * @param appearDelay 0 以上なら、その秒数だけ待ってから出てくるアニメーションを再生する。負なら再生しない
     * @param parent      UI の親（Canvas など）。指定するとその子になり、矩形も親を基準に求める
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
     * @brief 単色のパネル（暗転など）を作る。size を大きくすれば画面全体を覆える。
     * @param appearDelay  0 以上なら、その秒数だけ待ってから出てくるアニメーションを再生する。負なら再生しない
     * @param appearSlideY 出てくるときに上へスライドする量（px）。暗転は 0（フェードだけ）にする
     * @param colorBottom  指定すると、上端 color から下端 colorBottom への縦のグラデーションになる。
     *        暗転は一色で塗るより、上下で濃さを変えた方が自然に見える
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

    /**
     * @brief ボタン（背景・当たり判定・見た目・選んだときの拡大）と、その上に重ねるラベルを作る。
     * @param sortOrder   ボタンの背景の描画順。ラベルは +1 で手前に置く
     * @param appearDelay 0 以上なら、その秒数だけ待ってから出てくるアニメーションを再生する（ボタンとラベルの両方）。
     *        負なら再生しない
     * @return 作ったボタンとラベルの EntityID
     */
    inline MenuButtonIds SpawnButton(CCL::ECS::World &world, const char *btnName,
                                     const char *labelName, const std::string &label, DirectX::XMFLOAT2 offset,
                                     const std::string &action, bool focused, int sortOrder = 10, float appearDelay = -1.0f,
                                     CCL::ECS::EntityID parent = CCL::ECS::InvalidEntityID)
    {
        using namespace CCL::ECS;

        // --- ボタン ---
        auto btnArch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent,
                                                 UIImageComponent, UIInteractableComponent, UIButtonComponent, UIAnimationComponent,
                                                 UIAppearComponent>();
        auto btn     = EntityFactory::SpawnNamed(world, btnName, btnArch);

        UITransformComponent bt;
        bt.anchor = {0.5f, 0.5f};
        bt.pivot  = {0.5f, 0.5f};
        bt.offset = offset;
        bt.size   = {320.0f, 64.0f};

        UIImageComponent bimg;
        bimg.sortOrder = sortOrder;

        UIInteractableComponent inter;
        inter.focusable = true;
        inter.isFocused = focused; // true なら最初から選ばれた状態にする

        UIButtonComponent bcomp;
        bcomp.onClickAction = action;

        UIAnimationComponent banim;

        UIAppearComponent bap;
        ConfigureAppear(bt, bap, appearDelay);

        btn.Set(MakeUINode(parent)).Set(bt).Set(bimg).Set(inter).Set(bcomp).Set(banim).Set(bap);
        const EntityID btnID = btn.ID();

        // --- ラベル（別のエンティティ。ボタンの左寄りに置き、ボタンと同じ親の子にする） ---
        auto labelArch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent,
                                                   UITextComponent, UIAppearComponent>();
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

        UIAppearComponent lap;
        ConfigureAppear(lt, lap, appearDelay);

        lbl.Set(MakeUINode(parent)).Set(lt).Set(lx).Set(lap);
        const EntityID lblID = lbl.ID();

        return {btnID, lblID};
    }

    /**
     * @brief メニューの下に出す操作のヒント（決定・選択）を作る。
     *
     * タイトル・ポーズ・リザルトの3画面が同じ行を出すので、ここにまとめる。
     * それぞれに書くと、座標やアイコンの種類が画面ごとにずれていく。
     *
     * @param yOffset 画面の中央からの縦の位置（px）。ボタンの下に来る値を渡す
     * @param out     作ったエンティティを追加する（呼び出し側がまとめて消すため）
     * @param xOffset 行全体を横に動かす量（px）。0 なら画面の中央。メニューを左に寄せた画面（タイトル）で、
     *        ヒントだけ中央に残らないようにするため
     * @note アイコンの画像は InputIconSystem が毎フレーム決める。ここは種類を指定するだけ
     */
    inline void SpawnMenuHints(CCL::ECS::World &world, CCL::ECS::EntityID parent, float yOffset,
                               std::vector<CCL::ECS::EntityID> &out, int sortOrder = 230, float appearDelay = -1.0f,
                               float xOffset = 0.0f)
    {
        using namespace CCL::ECS;

        struct HintDef
        {
            InputIconKind kind;
            const char   *label;
            const char   *name;
            float         x;
        };
        // 決定を左、選択を右にする。よく押す方を先に読ませる
        const HintDef defs[] = {
            {InputIconKind::MenuConfirm, "CONFIRM", "HintConfirm", -150.0f},
            {InputIconKind::MenuNavigate, "SELECT", "HintNavigate", 30.0f},
        };

        constexpr float kIcon = 28.0f;
        constexpr float kText = 16.0f;

        for (const HintDef &d : defs)
        {
            auto iconArch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent,
                                                      UIImageComponent, InputIconComponent, UIAppearComponent>();
            auto icon     = EntityFactory::SpawnNamed(world, d.name, iconArch);

            UITransformComponent it;
            it.anchor = {0.5f, 0.5f};
            it.pivot  = {0.5f, 0.5f};
            it.offset = {d.x + xOffset, yOffset};
            it.size   = {kIcon, kIcon};

            UIImageComponent img;
            img.sortOrder = sortOrder;
            img.visible   = false; // InputIconSystem が画像を決めるまで出さない

            InputIconComponent ic;
            ic.kind = d.kind;

            UIAppearComponent ap;
            ap.slideY = 0.0f; // ヒントはスライドさせずフェードだけにする（目立たせたいのはボタン）
            ConfigureAppear(it, ap, appearDelay);

            icon.Set(MakeUINode(parent)).Set(it).Set(img).Set(ic).Set(ap);
            out.push_back(icon.ID());

            // ラベルはアイコンの右。文字は左上を基準に描くので、縦の中央に来るよう上へずらす
            auto lblArch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent,
                                                     UITextComponent, UIAppearComponent>();
            auto lbl     = EntityFactory::SpawnNamed(world, (std::string(d.name) + "Label").c_str(), lblArch);

            UITransformComponent lt;
            lt.anchor = {0.5f, 0.5f};
            lt.pivot  = {0.5f, 0.5f};
            lt.offset = {d.x + xOffset + kIcon + 44.0f, yOffset - (kText * 0.5f)};
            lt.size   = {120.0f, kText};

            UITextComponent tx;
            tx.text      = d.label;
            tx.pixelSize = kText;
            tx.color     = {0.78f, 0.80f, 0.86f, 1.0f}; // ボタンの文字より少し暗くして目立たせない
            tx.sortOrder = sortOrder + 1;

            UIAppearComponent lap;
            lap.slideY = 0.0f;
            ConfigureAppear(lt, lap, appearDelay);

            lbl.Set(MakeUINode(parent)).Set(lt).Set(tx).Set(lap);
            out.push_back(lbl.ID());
        }
    }
} // namespace CCL::GameUI
