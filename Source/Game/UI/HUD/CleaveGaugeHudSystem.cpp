#include "Game/UI/HUD/CleaveGaugeHudSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/Common/CCL_Archetype.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Core/Math/Interpolation.h" // Damp: フレームレートに依らない追従の係数
#include "Engine/Serialization/Factory/EntityFactory.h"

#include "Engine/Transform/Components/TransformComponent.h" // UI の親子関係に使う
#include "Engine/UI/ECS/Components/UITransformComponent.h"
#include "Engine/UI/ECS/Components/UIImageComponent.h"
#include "Engine/UI/ECS/Components/UITextComponent.h"
#include "Game/UI/Common/MenuUiFactory.h" // SpawnCanvas / MakeUINode

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace CCL::ECS;

namespace
{
    // 自分側の UI の色（PlayerHealthHudSystem の白金に揃える）
    const DirectX::XMFLOAT4 kColRim    = {0.96f, 0.88f, 0.62f, 0.40f}; // 縁
    const DirectX::XMFLOAT4 kColBg     = {0.00f, 0.00f, 0.00f, 0.60f}; // 地
    const DirectX::XMFLOAT4 kColReady  = {1.00f, 0.84f, 0.40f, 1.00f}; // たまったストック（金）
    const DirectX::XMFLOAT4 kColCharge = {0.80f, 0.68f, 0.42f, 0.85f}; // ためている途中（くすんだ金。まだ使えないことを色でも分ける）
    const DirectX::XMFLOAT4 kColFlash  = {1.00f, 0.90f, 0.60f, 1.00f}; // 増えた瞬間の光
    const DirectX::XMFLOAT4 kColLabel  = {0.86f, 0.86f, 0.88f, 0.85f}; // 見出し

    // --- 配置（画面左下。PlayerHealthHudSystem のゲージ（下端から 56px・高さ 16px）の上に乗せる） ---
    constexpr float kMarginLeft = 48.0f;  // 画面の左端から（HP ゲージと揃える）
    constexpr float kRowCenterY = -98.0f; // 粒の中心の、画面下端からの位置（HP ゲージの上辺 -72px から 26px 上）
    constexpr float kLabelPx    = 14.0f;  // 見出しの文字の高さ
    constexpr float kFirstPipX  = 92.0f;  // 最初の粒の中心の、左マージンからの距離（見出しの右）
    constexpr float kPipSpacing = 30.0f;  // 粒の中心の間隔
    constexpr float kPipSize    = 16.0f;  // 地と中身の正方形の一辺（45 度回すと対角線 ≒ 22.6px の◆になる）
    constexpr float kRimSize    = 20.0f;  // 縁の正方形の一辺
    constexpr float kFlashGrow  = 18.0f;  // 光が消えるまでに広がる量（px）
} // namespace

EntityID CleaveGaugeHudSystem::SpawnDiamond(EntityID canvas, const char *name, float centerX, float size,
                                            const DirectX::XMFLOAT4 &color, int sortOrder, bool additive)
{
    auto arch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent, UIImageComponent>();
    auto e    = EntityFactory::SpawnNamed(*m_world, name, arch);

    UITransformComponent t;
    t.anchor   = {0.0f, 1.0f}; // 画面の左下
    t.pivot    = {0.5f, 0.5f}; // 中心で置く。大きさを変えても中心が動かず、中身が中心から広がって見える
    t.offset   = {centerX, kRowCenterY};
    t.size     = {size, size};
    t.rotation = 45.0f;

    UIImageComponent img;
    img.color     = color;
    img.sortOrder = sortOrder;
    img.additive  = additive;

    e.Set(CCL::GameUI::MakeUINode(canvas)).Set(t).Set(img);
    return e.ID();
}

void CleaveGaugeHudSystem::Initialize()
{
    // 見出しと粒はこの Canvas の子にする
    const EntityID canvas = CCL::GameUI::SpawnCanvas(*m_world, "CleaveCanvas");

    // --- 見出し（粒の左） ---
    auto labelArch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent,
                                               UITextComponent>();
    auto label     = EntityFactory::SpawnNamed(*m_world, "CleaveLabel", labelArch);

    UITransformComponent lt;
    lt.anchor = {0.0f, 1.0f};
    lt.pivot  = {0.0f, 0.5f}; // 文字の縦の中央を粒の中心の高さに合わせる
    lt.offset = {kMarginLeft, kRowCenterY};
    lt.size   = {80.0f, kLabelPx};

    UITextComponent lx;
    lx.text      = "CLEAVE";
    lx.pixelSize = kLabelPx;
    lx.color     = kColLabel;
    lx.sortOrder = 110;
    lx.shadow    = true; // 明るい床の上でも読めるように

    label.Set(CCL::GameUI::MakeUINode(canvas)).Set(lt).Set(lx);
    m_labelID = label.ID();

    // --- 粒（縁 → 地 → 中身 → 光の順に重ねる） ---
    for (int i = 0; i < kPipCount; ++i)
    {
        const float x = kMarginLeft + kFirstPipX + kPipSpacing * static_cast<float>(i);
        char        name[32];

        std::snprintf(name, sizeof(name), "CleavePipRim%d", i);
        m_pips[i].rim = SpawnDiamond(canvas, name, x, kRimSize, kColRim, 100, false);
        std::snprintf(name, sizeof(name), "CleavePipBg%d", i);
        m_pips[i].bg = SpawnDiamond(canvas, name, x, kPipSize, kColBg, 101, false);
        std::snprintf(name, sizeof(name), "CleavePipFill%d", i);
        m_pips[i].fill = SpawnDiamond(canvas, name, x, kPipSize, kColReady, 102, false);
        std::snprintf(name, sizeof(name), "CleavePipFlash%d", i);
        m_pips[i].flash = SpawnDiamond(canvas, name, x, kPipSize, kColFlash, 103, true);
    }
}

void CleaveGaugeHudSystem::SetPipVisible(const Pip &pip, bool visible)
{
    for (EntityID id : {pip.rim, pip.bg, pip.fill, pip.flash})
    {
        if (auto *img = m_world->GetComponent<UIImageComponent>(id)) img->visible = visible;
    }
}

void CleaveGaugeHudSystem::Update(float dt)
{
    if (m_labelID == InvalidEntityID) return;

    // 生成した直後はコンポーネントがまだ反映されていないことがあるので、取れない部品は飛ばす
    auto *label = m_world->GetComponent<UITextComponent>(m_labelID);

    bool found = false;
    ForEach([&](const CleaveComponent &c) {
        if (found) return; // プレイヤーは1体の前提。最初の1つだけを表示する
        found = true;

        const int   pips  = std::min(c.maxStock, kPipCount);
        const int   stock = std::clamp(c.stock, 0, pips);
        const float ratio = (c.maxGauge > 0.0f) ? std::clamp(c.gauge / c.maxGauge, 0.0f, 1.0f) : 0.0f;

        // ストックが増えたら、増えた粒を光らせる。初回は光らせない（シーン開始で光って見えないように）
        if (m_prevStock >= 0)
        {
            for (int i = m_prevStock; i < stock; ++i)
                m_flash[i] = 1.0f;
        }
        m_prevStock = stock;

        if (label)
        {
            label->visible = true;
            label->color.w = (stock > 0) ? kColLabel.w : kColLabel.w * 0.5f; // 使えないときは見出しも沈める
        }

        for (int i = 0; i < kPipCount; ++i)
        {
            const Pip &p = m_pips[i];

            // maxStock が粒の数より少なければ、余った粒は出さない
            if (i >= pips)
            {
                SetPipVisible(p, false);
                continue;
            }
            SetPipVisible(p, true); // 中身と光は下で改めて決める

            // --- 中身: たまった粒は満杯、次にたまる1粒はたまり具合、それより先は空 ---
            //   一辺を sqrt(ratio) 倍にするのは、見た目の面積をたまり具合に比例させるため
            //   （一辺を比例させると、半分たまっても面積は 1/4 にしか見えない）
            auto *ft = m_world->GetComponent<UITransformComponent>(p.fill);
            auto *fi = m_world->GetComponent<UIImageComponent>(p.fill);
            if (ft && fi)
            {
                float scale = 0.0f;
                if (i < stock)
                {
                    scale     = 1.0f;
                    fi->color = kColReady;
                }
                else if (i == stock)
                {
                    scale     = std::sqrt(ratio);
                    fi->color = kColCharge;
                }
                ft->size    = {kPipSize * scale, kPipSize * scale};
                fi->visible = scale > 0.01f;
            }

            // --- 光: 増えた瞬間に満杯の大きさから広がりながら消える ---
            m_flash[i] -= m_flash[i] * CCL::Math::Damp(kFlashDecay, dt);
            if (m_flash[i] < 0.02f) m_flash[i] = 0.0f;

            auto *lt = m_world->GetComponent<UITransformComponent>(p.flash);
            auto *li = m_world->GetComponent<UIImageComponent>(p.flash);
            if (lt && li)
            {
                const float s = kPipSize + kFlashGrow * (1.0f - m_flash[i]);
                lt->size      = {s, s};
                li->color     = {kColFlash.x, kColFlash.y, kColFlash.z, kColFlash.w * m_flash[i]};
                li->visible   = m_flash[i] > 0.0f;
            }
        }
    });

    // CleaveComponent を持つエンティティが無ければ隠す
    if (!found)
    {
        if (label) label->visible = false;
        for (const Pip &p : m_pips)
            SetPipVisible(p, false);
        m_prevStock = -1;
        m_flash.fill(0.0f);
    }
}

REGISTER_LOGIC_SYSTEM(CleaveGaugeHudSystem, Priority::LogicStage::L06_HudSync);
