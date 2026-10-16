#include "PlayerRushEffectSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Game/Logics/Character/Player/Rush/PlayerRushEffectComponent.h"
#include "Engine/Graphics/Components/PostProcess/PostFx.h"

#include "Engine/Core/Math/StringHash.h"

#include <algorithm>

using namespace CCL::ECS;

namespace
{
    // スロットの識別子と優先度。エンジンは演出の中身を知らないので、名前と優先度は使う側が決める。
    // 命中の演出（10）より高くして、同時に出たらジャスト回避の演出を優先する。
    const uint32_t kSource   = CCL::Utils::HashString("JustEvade");
    constexpr int  kPriority = 20;

    /**
     * @brief current を target へ、1フレームぶんの歩幅 rate で線形に近づける。
     *
     * 目標を飛び越さない（オーバーシュートしない）ので、フェードの行き過ぎが起きない。
     *
     * @param current 今の値。
     * @param target  目標値。
     * @param rate    1フレームで詰める量（dt / 所要時間）。0以下なら即 target を返す。
     * @return 近づけた後の値。
     */
    inline float MoveToward(float current, float target, float rate)
    {
        if (rate <= 0.0f)
        {
            return target;
        }
        if (current < target)
        {
            return (std::min)(current + rate, target);
        }
        return (std::max)(current - rate, target);
    }
} // namespace

std::vector<CCL::ECS::TypeID> PlayerRushEffectSystem::GetReadTypes() const
{
    return {TypeInfo<PlayerRushEffectComponent>::ID()};
}

std::vector<CCL::ECS::TypeID> PlayerRushEffectSystem::GetWriteTypes() const
{
    // 値を書くのはビネットだけだが、後始末の PostFx::ReleaseAll は
    // 上書きを受け付ける全コンポーネントへ書くので、全部を申告する。
    return CCL::PostFx::AllWriteTypes();
}

void PlayerRushEffectSystem::Update(float dt)
{
    // 処理フロー:
    // 1. フェード重み weight を実時間で目標へ寄せる
    // 2. 完全に消えている間は、自分のスロットを手放して終える
    // 3. 見えている間は、weight をビネットの設定へ書き込む
    // 4. 同じ weight を放射ブラーの設定へ書き込む

    auto *justEvadeEffectComp = m_world->GetSingletonComponent<PlayerRushEffectComponent>();
    if (!justEvadeEffectComp) return;

    // ---------------------------------------------------------
    // 1. フェード重みの更新
    // ---------------------------------------------------------
    // rushActive の間は fadeInTime でピーク(1)へ、降ろすと fadeOutTime で 0 へ。
    // dt は実時間なので、世界スロー中でもフェード速度は変わらない。

    // ジャスト回避中かどうか
    const bool active = justEvadeEffectComp->rushActive;
    // 回避中なら1.0f ちがうなら 0.0f
    const float target = active ? 1.0f : 0.0f;
    // フェードにかかる時間を取得
    const float fadeTime = active ? justEvadeEffectComp->fadeInTime : justEvadeEffectComp->fadeOutTime;
    // 1フレームで詰める量を計算（0.0f 以下なら即 target へ）
    const float rate = (fadeTime > 0.0001f) ? (dt / fadeTime) : 1.0f;

    // フェードの遷移をweight(0→1)に反映する
    justEvadeEffectComp->weight = MoveToward(justEvadeEffectComp->weight, target, rate);

    // ---------------------------------------------------------
    // 2. 消えている間の後始末
    // ---------------------------------------------------------
    // 他の演出や設定本体には触れず、自分のスロットだけを手放す。
    //
    // 確保していないスロットへの Release は何もしないので、毎フレーム呼んでよい。
    // 「前のフレームで出していたか」を覚えておく必要がない。
    const bool visible = (justEvadeEffectComp->weight > 0.001f);
    if (!visible)
    {
        CCL::PostFx::ReleaseAll(*m_world, kSource);
        return;
    }

    // ---------------------------------------------------------
    // 3. ビネットへ反映
    // ---------------------------------------------------------
    // weight を 0→1 で上げることで徐々に暗くする。
    // 最終的な合成と描画の有無は PostProcessSystem(R09) が決める。
    // 縁のぼかし（softness）は書かない。スロットを取った時点のシーン設定が土台に
    // 入っているので、書かなかった項目はそのままシーンの見た目になる。
    if (auto *ov = CCL::PostFx::Vignette(*m_world, kSource, kPriority))
    {
        ov->weight            = justEvadeEffectComp->weight;            // 0→1 で徐々に暗く
        ov->params.intensity  = justEvadeEffectComp->vignetteIntensity; // ピークの暗さ
        ov->params.radius     = justEvadeEffectComp->vignetteRadius;    // 半径
        ov->params.color      = {0.0f, 0.0f, 0.0f, 1.0f};
        ov->params.pulseSpeed = 0.0f; // 明滅なし
    }

    // ---------------------------------------------------------
    // 4. 放射ブラーへ反映
    // ---------------------------------------------------------
    // フェードはビネットと共有する。中心はシーンの設定のまま（書かない）。
    if (auto *ov = CCL::PostFx::RadialBlur(*m_world, kSource, kPriority))
    {
        ov->weight          = justEvadeEffectComp->weight;
        ov->params.strength = justEvadeEffectComp->radialStrength;
        ov->params.falloff  = justEvadeEffectComp->radialFalloff;
    }
}

// カメラが決まった後、PostProcessSystem（R09）の前に走らせる
REGISTER_RENDER_SYSTEM(PlayerRushEffectSystem, Priority::RenderStage::R02_Environment);
