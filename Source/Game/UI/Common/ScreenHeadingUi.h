#pragma once
// ===========================================================================
// 概要: リザルト・ポーズ・設定・テロップに共通する「左上の大きな見出し・線・記録の行」の組みを作る関数をまとめたヘッダ。
//
// 責務:
//   - 画面の左上を基準にした文字・単色の四角形・入れ物を作る
//   - 見出し（滑り込む）・線（伸びる）・線の先端の◆の組みを作る
//   - 打ち込まれる文字と、項目名と値の1行を作る
//   - 画面どうしでそろえる配置と色を定数で持つ
//
// 備考:
//   配置と色は、ローディング画面（SceneLoading）にそろえる。
//
//   出てくる演出（打ち込む・滑り込む・伸びる）は UIRevealSystem が動かす。ここでは設定を付けて作るだけ。
//
//   作ったエンティティを out に足す関数があるのは、ポーズのように、閉じるときにまとめて消す画面があるため。
// ===========================================================================
#include "Game/UI/Common/MenuUiFactory.h"
#include "Game/UI/Common/UIRevealComponent.h"
#include "Engine/UI/Text/TextRenderer.h" // UTF-8 の文字数

#include <string>
#include <vector>

namespace CCL::GameUI
{
    // 配置（画面の左上からの px）。見出し・線・行の左端を kLeft にそろえる
    namespace ScreenLayout
    {
        constexpr float kLeft      = 120.0f;                   // 左の余白
        constexpr float kHeadY     = 96.0f;                    // 見出しの上端
        constexpr float kHeadPx    = 88.0f;                    // 見出しの文字の高さ
        constexpr float kRuleY     = kHeadY + kHeadPx + 20.0f; // 見出しの下の線の高さ
        constexpr float kRuleW     = 820.0f;                   // 線の長さ
        constexpr float kSubY      = kRuleY + 16.0f;           // 線のすぐ下の1行の上端
        constexpr float kRowY      = kSubY + 72.0f;            // 記録の1行目の上端
        constexpr float kRowH      = 46.0f;                    // 記録の行の間隔
        constexpr float kRowPx     = 26.0f;                    // 記録の文字の高さ
        constexpr float kValueX    = kLeft + 300.0f;           // 記録の値の列の左端
        constexpr float kTypeSpeed = 70.0f;                    // 1秒に打ち込む文字数
    } // namespace ScreenLayout

    // 色（文字は少し温かみのある白、飾りと味方は白金、ボスと負けは深紅）
    namespace ScreenColor
    {
        constexpr DirectX::XMFLOAT4 kGold{0.96f, 0.88f, 0.62f, 1.0f};   // 飾り・勝ち
        constexpr DirectX::XMFLOAT4 kRed{0.95f, 0.18f, 0.22f, 1.0f};    // ボス・負け
        constexpr DirectX::XMFLOAT4 kLabel{0.93f, 0.91f, 0.86f, 0.55f}; // 項目名（沈める）
        constexpr DirectX::XMFLOAT4 kValue{0.93f, 0.91f, 0.86f, 1.0f};  // 値
        constexpr DirectX::XMFLOAT4 kSub{0.93f, 0.91f, 0.86f, 0.8f};    // 線の下の1行
    } // namespace ScreenColor

    /**
     * @brief 画面の左上を基準に、左上 (x, y) から文字を置く。
     * @param world     エンティティを作る World。
     * @param name      エンティティの名前（エディタの表示用）。
     * @param text      最初に表示する文字列。打ち込む演出なら空にして、reveal の text に全文を入れる。
     * @param x         左端（px、画面の左上から）。
     * @param y         上端（px、画面の左上から）。
     * @param px        文字の高さ（px）。
     * @param color     文字の色。
     * @param parent    親のエンティティ（Canvas など）。
     * @param sortOrder 描画順。
     * @param alpha     最初の不透明度。演出で出すものは 0 で作る（出る前の1フレームに見えないように）。
     * @param reveal    出てくる演出。nullptr でなければ UIRevealComponent も付ける。
     * @return 作ったエンティティ。中身が入るのは次のフレームから。
     * @note 生成は遅れて反映されるので、演出のコンポーネントは後から足さず、生成と一緒に付ける。
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
        if (reveal)
        {
            e.Set(*reveal);
        }
        return e.ID();
    }

    /**
     * @brief 画面の左上を基準に、中心 (x, y) で単色の四角形を置く。
     * @param world     エンティティを作る World。
     * @param name      エンティティの名前（エディタの表示用）。
     * @param x         中心の X（px、画面の左上から）。
     * @param y         中心の Y（px、画面の左上から）。
     * @param size      大きさ（px）。
     * @param rotation  中心まわりの回転（度）。45 で◆になる。
     * @param color     色。
     * @param parent    親のエンティティ（Canvas など）。
     * @param sortOrder 描画順。
     * @param alpha     最初の不透明度。
     * @param reveal    出てくる演出。nullptr でなければ UIRevealComponent も付ける。
     * @return 作ったエンティティ。中身が入るのは次のフレームから。
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
        if (reveal)
        {
            e.Set(*reveal);
        }
        return e.ID();
    }

    /**
     * @brief 画面の角や辺にそろえて、空の入れ物を置く。
     * @param world  エンティティを作る World。
     * @param name   エンティティの名前（エディタの表示用）。
     * @param corner 画面のどこにそろえるか（{0,0} = 左上、{0,1} = 左下）。入れ物の同じ角をそこに合わせる。
     * @param offset そろえた位置からのずれ（px）。左下にそろえて上へ離すなら y を負にする。
     * @param size   入れ物の大きさ（px）。
     * @param parent 親のエンティティ（Canvas など）。
     * @return 作ったエンティティ。
     * @note SpawnButton は親の中央を基準に置くので、ボタンをこの子にすると、画面の角から決めた位置に並べられる。
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
     * @brief 左上 (x, y) から、delay 秒後に全文を1文字ずつ打ち込む文字を置く。最初は空。
     * @param world  エンティティを作る World。
     * @param name   エンティティの名前（エディタの表示用）。
     * @param full   打ち終わったときの文字列。
     * @param x      左端（px、画面の左上から）。
     * @param y      上端（px、画面の左上から）。
     * @param px     文字の高さ（px）。
     * @param color  文字の色。
     * @param delay  打ち始めるまでの時間（秒、画面を出してから）。
     * @param parent 親のエンティティ（Canvas など）。
     * @return 作ったエンティティ。
     */
    inline CCL::ECS::EntityID SpawnTypedText(CCL::ECS::World &world, const char *name, const std::string &full, float x,
                                             float y, float px, DirectX::XMFLOAT4 color, float delay,
                                             CCL::ECS::EntityID parent)
    {
        UIRevealComponent rv;
        rv.mode  = UIRevealMode::Type;
        rv.delay = delay;
        rv.speed = ScreenLayout::kTypeSpeed;
        rv.text  = full;
        return SpawnTextTopLeft(world, name, "", x, y, px, color, parent, 220, 1.0f, &rv);
    }

    /**
     * @brief 左上の大きな見出しと、その下の線・線の先端の◆を作る。見出しは滑り込み、少し遅れて線が伸びる。
     * @param world     エンティティを作る World。
     * @param canvas    親の Canvas。
     * @param title     見出しの文字列。
     * @param color     見出しの色。
     * @param out       作ったエンティティを足す先。
     * @param sortOrder 描画順。別の画面の上に重ねる画面（設定）は大きくする。
     */
    inline void SpawnScreenHeading(CCL::ECS::World &world, CCL::ECS::EntityID canvas, const char *title,
                                   DirectX::XMFLOAT4 color, std::vector<CCL::ECS::EntityID> &out, int sortOrder = 220)
    {
        using namespace ScreenLayout;

        UIRevealComponent slide;
        slide.mode = UIRevealMode::SlideIn;
        const CCL::ECS::EntityID heading = SpawnTextTopLeft(world, "ScreenHeading", title, kLeft, kHeadY, kHeadPx, color,
                                                            canvas, sortOrder, 0.0f, &slide);

        const DirectX::XMFLOAT4  ruleColor{ScreenColor::kGold.x, ScreenColor::kGold.y, ScreenColor::kGold.z, 0.7f};
        const CCL::ECS::EntityID tip  = SpawnRectTopLeft(world, "ScreenRuleTip", kLeft, kRuleY, {9.0f, 9.0f}, 45.0f,
                                                         ScreenColor::kGold, canvas, sortOrder, 0.0f);
        UIRevealComponent grow;
        grow.mode   = UIRevealMode::Grow;
        grow.delay  = 0.15f;
        grow.length = kRuleW;
        grow.tip    = tip;
        const CCL::ECS::EntityID rule = SpawnRectTopLeft(world, "ScreenRule", kLeft, kRuleY, {0.0f, 2.0f}, 0.0f,
                                                         ruleColor, canvas, sortOrder, 1.0f, &grow);

        out.push_back(heading);
        out.push_back(tip);
        out.push_back(rule);
    }

    /**
     * @brief 記録の1行（左に沈めた色の項目名、値の列に明るい色の値）を作る。値は項目名を打ち終えてから打ち始める。
     * @param world      エンティティを作る World。
     * @param canvas     親の Canvas。
     * @param name       値のエンティティの名前。項目名は後ろに "Label" を付けた名前になる。
     * @param row        何行目か（0 から）。
     * @param label      項目名。
     * @param value      値の文字列。
     * @param delay      項目名を打ち始めるまでの時間（秒、画面を出してから）。
     * @param out        作ったエンティティを足す先。
     * @param valueColor 値の色。
     */
    inline void SpawnScreenRow(CCL::ECS::World &world, CCL::ECS::EntityID canvas, const char *name, int row,
                               const std::string &label, const std::string &value, float delay,
                               std::vector<CCL::ECS::EntityID> &out, DirectX::XMFLOAT4 valueColor = ScreenColor::kValue)
    {
        using namespace ScreenLayout;

        const float y = kRowY + kRowH * static_cast<float>(row);
        out.push_back(SpawnTypedText(world, (std::string(name) + "Label").c_str(), label, kLeft, y, kRowPx,
                                     ScreenColor::kLabel, delay, canvas));
        out.push_back(SpawnTypedText(world, name, value, kValueX, y, kRowPx, valueColor,
                                     delay + static_cast<float>(TextRenderer::Utf8Length(label)) / kTypeSpeed, canvas));
    }
} // namespace CCL::GameUI
