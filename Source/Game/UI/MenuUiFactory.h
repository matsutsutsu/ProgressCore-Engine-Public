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

#include "Engine/Transform/Components/TransformComponent.h" // UI階層（親子）はこれを再利用
#include "Engine/UI/ECS/Components/UITransformComponent.h"
#include "Engine/UI/ECS/Components/UICanvasComponent.h"
#include "Engine/UI/ECS/Components/UIImageComponent.h"
#include "Engine/UI/ECS/Components/UIInteractableComponent.h"
#include "Engine/UI/ECS/Components/UIButtonComponent.h"
#include "Engine/UI/ECS/Components/UIAnimationComponent.h"
#include "Engine/UI/ECS/Components/UIAppearComponent.h"
#include "Engine/UI/ECS/Components/UITextComponent.h"

#include <string>

namespace CCL::GameUI
{
    /// @brief SpawnButton が返す、生成したボタンとラベルの EntityID ペア。
    ///        （解除時に両方を破棄できるよう、ラベルの ID も返す）
    struct MenuButtonIds
    {
        CCL::ECS::EntityID button = CCL::ECS::InvalidEntityID;
        CCL::ECS::EntityID label  = CCL::ECS::InvalidEntityID;
    };

    /// @brief 出現アニメを設定するヘルパ。appearDelay>=0 なら再生ON＋開始を不可視(alpha=0)にする。
    inline void ConfigureAppear(UITransformComponent &t, UIAppearComponent &ap, float appearDelay)
    {
        if (appearDelay >= 0.0f)
        {
            ap.play  = true;
            ap.delay = appearDelay;
            t.alpha  = 0.0f; // 出現前は見えないように（システムが 0→1 へ上げる）
        }
    }

    /// @brief UI階層用の TransformComponent を作る。parent 未指定(Invalid)ならルート(0)にする。
    ///        UI のレイアウトは UITransform が担うが、親子リンクはこの TransformComponent を再利用する
    ///        （エディタの Hierarchy 表示・ドラッグ親子付け・UILayout の入れ子解決をこれ1本で賄う）。
    inline TransformComponent MakeUINode(CCL::ECS::EntityID parent)
    {
        TransformComponent tr;
        tr.parent   = (parent != CCL::ECS::InvalidEntityID) ? parent : 0; // 0 = ルート
        tr.isStatic = false;
        return tr;
    }

    /// @brief UI階層のルート「キャンバス」を生成する。矩形は常に画面全体。配下UIはこの子にする。
    inline CCL::ECS::EntityID SpawnCanvas(CCL::ECS::World &world, const char *name)
    {
        auto arch = CCL::ECS::ArchetypeHelper::Generate<TransformComponent, UITransformComponent,
            UICanvasComponent>();
        auto e = EntityFactory::SpawnNamed(world, name, arch);
        e.Set(MakeUINode(CCL::ECS::InvalidEntityID)).Set(UITransformComponent{}).Set(UICanvasComponent{});
        return e.ID();
    }

    /// @brief 画面中央アンカーのテキストラベルを生成する。
    /// @param appearDelay >=0 で出現アニメを再生（秒・遅延）。負(既定)ならアニメ無し。
    /// @param parent      UI親（Canvas等）。指定するとその子になり矩形も親基準で解決される。
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

    /// @brief 単色パネル（暗転オーバーレイ等）を生成する。size を大きくすれば全画面を覆える。
    /// @param appearDelay >=0 で出現アニメを再生（秒・遅延）。負(既定)ならアニメ無し。
    /// @param appearSlideY 出現時の上スライド量(px)。暗転パネルは 0（フェードのみ）が自然。
    inline CCL::ECS::EntityID SpawnPanel(CCL::ECS::World &world, const char *name,
        DirectX::XMFLOAT2 offset, DirectX::XMFLOAT2 size, DirectX::XMFLOAT4 color, int sortOrder,
        float appearDelay = -1.0f, float appearSlideY = 28.0f,
        CCL::ECS::EntityID parent = CCL::ECS::InvalidEntityID)
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

        UIAppearComponent ap;
        ap.slideY = appearSlideY;
        ConfigureAppear(t, ap, appearDelay);

        e.Set(MakeUINode(parent)).Set(t).Set(img).Set(ap);
        return e.ID();
    }

    /// @brief ボタン（背景パネル＋当たり判定＋ボタン見た目＋拡大演出）と、その上に重ねる
    ///        ラベル（テキスト）を生成する。戻り値＝生成したボタン/ラベルの EntityID ペア。
    /// @param sortOrder ボタン背景の描画順。ラベルは +1 で少し手前に置く。
    /// @param appearDelay >=0 で出現アニメを再生（秒・遅延）。負(既定)ならアニメ無し。
    ///        ボタン本体とラベルの両方に同じ遅延で適用する。
    inline MenuButtonIds SpawnButton(CCL::ECS::World &world, const char *btnName,
        const char *labelName, const std::string &label, DirectX::XMFLOAT2 offset,
        const std::string &action, bool focused, int sortOrder = 10, float appearDelay = -1.0f,
        CCL::ECS::EntityID parent = CCL::ECS::InvalidEntityID)
    {
        using namespace CCL::ECS;

        // --- ボタン本体 ---
        auto btnArch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent,
            UIImageComponent, UIInteractableComponent, UIButtonComponent, UIAnimationComponent,
            UIAppearComponent>();
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

        UIAppearComponent bap;
        ConfigureAppear(bt, bap, appearDelay);

        btn.Set(MakeUINode(parent)).Set(bt).Set(bimg).Set(inter).Set(bcomp).Set(banim).Set(bap);
        const EntityID btnID = btn.ID();

        // --- ラベル（別エンティティ・左パディングで配置。ボタンと同じ親の下に置く） ---
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
} // namespace CCL::GameUI
