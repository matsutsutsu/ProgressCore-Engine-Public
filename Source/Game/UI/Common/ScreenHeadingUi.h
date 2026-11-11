#pragma once
// ===========================================================================
// 概要: リザルト・ポーズ・設定・テロップに共通する「左上の見出し・線・記録の行」を作る関数をまとめたヘッダー
//
// 責務:
//   - 画面の左上を基準にした文字・単色の四角形・入れ物を作る
//   - 見出し（滑り込む）・線（伸びる）・線の先端の◆の組みを作る
//   - 打ち込まれる文字と、項目名と値の1行を作る
//
// 備考: 配置と色は MenuStyle（GetMenuStyle）から取る。出てくる演出は UIRevealSystem が動かす
// ===========================================================================
#include "Game/UI/Common/MenuUiFactory.h"
#include "Game/UI/Common/UIRevealComponent.h"
#include "Engine/UI/Text/TextRenderer.h"

#include <string>
#include <vector>

namespace CCL::GameUI
{
    // ---------------------------------------------------------------------------
    // 左上を基準にした部品
    // ---------------------------------------------------------------------------

    /**
     * @brief 画面の左上を基準に、左上 (x, y) から文字を置く
     * @param text   最初に出す文字列。打ち込む演出なら空にして、reveal の text に全文を入れる
     * @param px     文字の高さ（px）
     * @param alpha  最初の不透明度。演出で出すものは 0 で作る（出る前の1フレームに見えないように）
     * @param reveal 出てくる演出。nullptr でなければ UIRevealComponent も付ける
     * @note 生成は遅れて反映されるので、演出のコンポーネントは後から足さず、生成と一緒に付ける
     */
    inline CCL::ECS::EntityID SpawnTextTopLeft(CCL::ECS::World &world, const char *name, const std::string &text, float x,
                                               float y, float px, DirectX::XMFLOAT4 color, CCL::ECS::EntityID parent,
                                               int sortOrder = 220, float alpha = 1.0f,
                                               const UIRevealComponent *reveal = nullptr)
    {
        auto arch = reveal ? CCL::ECS::ArchetypeHelper::Generate<TransformComponent, UITransformComponent, UITextComponent,
                                                                 UIRevealComponent>()
                           : CCL::ECS::ArchetypeHelper::Generate<TransformComponent, UITransformComponent, UITextComponent>();
        auto e    = EntityFactory::SpawnNamed(world, name, arch);

        UITransformComponent t;
        t.anchor = {0.0f, 0.0f};
        t.pivot  = {0.0f, 0.0f};
        t.offset = {x, y};
        t.size   = {1200.0f, px};
        t.alpha  = alpha;

        UITextComponent tx;
        tx.text      = text;
        tx.pixelSize = px;
        tx.color     = color;
        tx.sortOrder = sortOrder;

        e.Set(MakeUINode(parent)).Set(t).Set(tx);
        if (reveal) e.Set(*reveal);
        return e.ID();
    }

    /**
     * @brief 画面の左上を基準に、中心 (x, y) で単色の四角形を置く
     * @param rotation 中心まわりの回転（度）。45 で◆になる
     * @param reveal   出てくる演出。nullptr でなければ UIRevealComponent も付ける
     */
    inline CCL::ECS::EntityID SpawnRectTopLeft(CCL::ECS::World &world, const char *name, float x, float y,
                                               DirectX::XMFLOAT2 size, float rotation, DirectX::XMFLOAT4 color,
                                               CCL::ECS::EntityID parent, int sortOrder = 220, float alpha = 1.0f,
                                               const UIRevealComponent *reveal = nullptr)
    {
        auto arch = reveal ? CCL::ECS::ArchetypeHelper::Generate<TransformComponent, UITransformComponent, UIImageComponent,
                                                                 UIRevealComponent>()
                           : CCL::ECS::ArchetypeHelper::Generate<TransformComponent, UITransformComponent, UIImageComponent>();
        auto e    = EntityFactory::SpawnNamed(world, name, arch);

        UITransformComponent t;
        t.anchor   = {0.0f, 0.0f};
        t.pivot    = {0.5f, 0.5f};
        t.offset   = {x, y};
        t.size     = size;
        t.rotation = rotation;
        t.alpha    = alpha;

        UIImageComponent img;
        img.color     = color;
        img.sortOrder = sortOrder;

        e.Set(MakeUINode(parent)).Set(t).Set(img);
        if (reveal) e.Set(*reveal);
        return e.ID();
    }

    /**
     * @brief 画面の角や辺にそろえて、空の入れ物を置く
     * @param corner 画面のどこにそろえるか（{0,0} = 左上、{0,1} = 左下）。入れ物の同じ角をそこに合わせる
     * @param offset そろえた位置からのずれ（px）。左下にそろえて上へ離すなら y を負にする
     * @note SpawnButton は親の中央を基準に置くので、ボタンをこの子にすると、画面の角から決めた位置に並べられる
     */
    inline CCL::ECS::EntityID SpawnBox(CCL::ECS::World &world, const char *name, DirectX::XMFLOAT2 corner,
                                       DirectX::XMFLOAT2 offset, DirectX::XMFLOAT2 size, CCL::ECS::EntityID parent)
    {
        auto arch = CCL::ECS::ArchetypeHelper::Generate<TransformComponent, UITransformComponent>();
        auto e    = EntityFactory::SpawnNamed(world, name, arch);

        UITransformComponent t;
        t.anchor = corner;
        t.pivot  = corner;
        t.offset = offset;
        t.size   = size;

        e.Set(MakeUINode(parent)).Set(t);
        return e.ID();
    }

    /**
     * @brief 左上 (x, y) から、delay 秒後に全文を1文字ずつ打ち込む文字を置く。最初は空
     * @param full  打ち終わったときの文字列
     * @param delay 打ち始めるまでの時間（秒、画面を出してから）
     */
    inline CCL::ECS::EntityID SpawnTypedText(CCL::ECS::World &world, const char *name, const std::string &full, float x,
                                             float y, float px, DirectX::XMFLOAT4 color, float delay,
                                             CCL::ECS::EntityID parent)
    {
        UIRevealComponent rv;
        rv.mode  = UIRevealMode::Type;
        rv.delay = delay;
        rv.speed = GetMenuStyle(world).typeSpeed;
        rv.text  = full;
        return SpawnTextTopLeft(world, name, "", x, y, px, color, parent, 220, 1.0f, &rv);
    }

    // ---------------------------------------------------------------------------
    // 見出しと記録の行
    // ---------------------------------------------------------------------------

    /**
     * @brief 左上の大きな見出しと、その下の線・線の先端の◆を作る。見出しは滑り込み、少し遅れて線が伸びる
     * @param out       作ったエンティティを足す先
     * @param sortOrder 描画順。別の画面の上に重ねる画面（設定）は大きくする
     */
    inline void SpawnScreenHeading(CCL::ECS::World &world, CCL::ECS::EntityID canvas, const char *title,
                                   DirectX::XMFLOAT4 color, std::vector<CCL::ECS::EntityID> &out, int sortOrder = 220)
    {
        const MenuStyleComponent style = GetMenuStyle(world);
        const float              ruleY = style.RuleY();

        // --- 見出し ---
        UIRevealComponent slide;
        slide.mode = UIRevealMode::SlideIn;
        const CCL::ECS::EntityID heading = SpawnTextTopLeft(world, "ScreenHeading", title, style.left, style.headY,
                                                            style.headPx, color, canvas, sortOrder, 0.0f, &slide);

        // --- 線と先端の◆ ---
        // ◆は線の伸びる先についていく（UIRevealSystem の Grow）
        const DirectX::XMFLOAT4  ruleColor{style.gold.x, style.gold.y, style.gold.z, 0.7f};
        const CCL::ECS::EntityID tip = SpawnRectTopLeft(world, "ScreenRuleTip", style.left, ruleY, {9.0f, 9.0f}, 45.0f,
                                                        style.gold, canvas, sortOrder, 0.0f);

        UIRevealComponent grow;
        grow.mode   = UIRevealMode::Grow;
        grow.delay  = 0.15f;
        grow.length = style.ruleW;
        grow.tip    = tip;
        const CCL::ECS::EntityID rule = SpawnRectTopLeft(world, "ScreenRule", style.left, ruleY, {0.0f, 2.0f}, 0.0f,
                                                         ruleColor, canvas, sortOrder, 1.0f, &grow);

        out.push_back(heading);
        out.push_back(tip);
        out.push_back(rule);
    }

    /**
     * @brief 記録の1行（沈めた色の項目名と、値の列の明るい値）を作る。値は項目名を打ち終えてから打ち始める
     * @param name       値のエンティティの名前。項目名は後ろに "Label" を付けた名前になる
     * @param row        何行目か（0 から）
     * @param delay      項目名を打ち始めるまでの時間（秒、画面を出してから）
     * @param out        作ったエンティティを足す先
     * @param valueColor 値の色。nullptr なら MenuStyle の値の色
     */
    inline void SpawnScreenRow(CCL::ECS::World &world, CCL::ECS::EntityID canvas, const char *name, int row,
                               const std::string &label, const std::string &value, float delay,
                               std::vector<CCL::ECS::EntityID> &out, const DirectX::XMFLOAT4 *valueColor = nullptr)
    {
        const MenuStyleComponent style = GetMenuStyle(world);
        const float              y     = style.RowY() + style.rowH * static_cast<float>(row);

        // 値は、項目名の文字数ぶん打ち終えるのを待ってから打ち始める
        const float valueDelay = delay + static_cast<float>(TextRenderer::Utf8Length(label)) / style.typeSpeed;

        out.push_back(SpawnTypedText(world, (std::string(name) + "Label").c_str(), label, style.left, y, style.rowPx,
                                     style.label, delay, canvas));
        out.push_back(SpawnTypedText(world, name, value, style.ValueX(), y, style.rowPx,
                                     valueColor ? *valueColor : style.value, valueDelay, canvas));
    }
} // namespace CCL::GameUI
