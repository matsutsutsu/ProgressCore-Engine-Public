#pragma once
// ===========================================================================
// 概要: ワールド座標に VFX（パーティクル）を1行で出すための System と API（L08_Prefab）。
//
// 責務:
//   - プレハブから VFX の Entity を作り、指定位置に置いてバーストを起こす
//   - 粒子が消えたころに Entity を破棄する
//
// 備考: VFX のプレハブは Transform・GPUParticle・TimeState を持つ1 Entity であること。
//       親子を持つプレハブは扱わない。複数の層を持つエフェクトはエミッタを増やして作る。
//
//       後始末の決まり（間違えやすい）:
//       - 全エミッタがバーストなら、粒子が消えた後に自動で破棄する。呼んだら放っておいてよい。
//       - 継続放出のエミッタを1つでも含むと、自動では破棄しない。継続放出は自分では
//         終わらないので、いつ消すかをエンジンは決められない。戻り値を持っておき、
//         要らなくなったら Stop() を呼ぶ。勝手に消すと、ループする VFX が再生中に突然消える。
//
// @code
//   VFX::Play(*m_world, "Assets/Prefabs/Particles/VFX_HitSpark", hitPos);
// @endcode
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/ECS/Common/CCL_Types.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Graphics/Components/Particle/GPUParticleComponent.h"
#include "Engine/Time/TimeState.h"

#include <DirectXMath.h>
#include <string>
#include <vector>

class VFXSystem : public CCL::ECS::IfSystem<VFXSystem,
                                            CCL::ECS::Write<TransformComponent>,
                                            CCL::ECS::Write<GPUParticleComponent>,
                                            CCL::ECS::Read<TimeStateComponent>>
{
public:
    VFXSystem()
        : IfSystem("VFXSystem")
    {
    }

    // 自分を World のリソースに登録する（VFX::Play から引くため）
    void Initialize() override;

    // 破棄待ちの VFX の残り時間を減らし、尽きたものを破棄する
    void Update(float dt) override;

    /**
     * @brief 指定位置に VFX を1つ出す。
     * @return 作った EntityID。失敗したら InvalidEntityID。
     * @note 継続放出を含むと自動では破棄しないので、戻り値を持っておいて Stop() する。
     *       全エミッタがバーストなら戻り値は捨ててよい。
     */
    CCL::ECS::EntityID Play(const std::string &path, const DirectX::XMFLOAT3 &worldPos);

    // 再生中の VFX を止める。
    // 発生だけを止め、出ている粒子は寿命まで残してから破棄する（すぐ消すと粒子が途中で途切れる）。
    // 破棄待ちの VFX には何もしない（毎フレーム呼ばれても寿命が延びない）。
    void Stop(CCL::ECS::EntityID handle);

private:
    // 破棄待ちの VFX 1件
    struct ActiveVFX
    {
        CCL::ECS::EntityID id     = CCL::ECS::InvalidEntityID;
        float              remain = 0.0f; // 破棄までの残り時間（秒）
    };

    // 継続放出（emitMode==0）のエミッタを含むか。含むなら自動では破棄しない
    bool HasContinuousEmitter(const GPUParticleComponent &particle) const;

    // 今いる粒子が全部消えるまでの時間（秒）
    float CalcParticleLifetime(const GPUParticleComponent &particle) const;

    // handle が破棄待ちに入っているか
    bool IsPendingDestroy(CCL::ECS::EntityID handle) const;

    std::vector<ActiveVFX> m_active; // 破棄待ちの VFX

    // 破棄までに足す余裕（秒）。バーストは作った次のフレームに出るので、その分を吸収する
    static constexpr float kLifetimeMargin = 0.2f;
};

// 呼び出し側の入口。World を渡すだけで使える
namespace VFX
{
    // 指定位置に VFX を1つ出す（VFXSystem::Play を呼ぶ）
    CCL::ECS::EntityID Play(CCL::ECS::World &world, const std::string &path, const DirectX::XMFLOAT3 &worldPos);

    // 再生中の VFX を止める（VFXSystem::Stop を呼ぶ）
    void Stop(CCL::ECS::World &world, CCL::ECS::EntityID handle);
} // namespace VFX
