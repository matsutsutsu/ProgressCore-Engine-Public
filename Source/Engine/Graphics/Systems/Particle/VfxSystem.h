#pragma once
// ===========================================================================
// VFXSystem.h
// -----------
// 役割:
//   ワールド座標に VFX(パーティクル)を1行で出すためのAPI。
//
//   @code
//     VFX::Play(*m_world, "Assets/Prefabs/Particles/VFX_HitSpark", hitPos);
//   @endcode
//
// 前提:
//   VFXプレハブは「Transform + GPUParticle + TimeState を持つ1エンティティ」であること。
//   階層(親子)を持つプレハブは扱わない。1エンティティに複数エミッタを持てるので、
//   複合エフェクトはエミッタを増やして表現する。
//
// 後始末の約束（ここが一番間違えやすい）:
//   - 全エミッタが Burst なら、粒子が消えたあと自動で破棄される。呼びっぱなしでよい。
//   - 継続放出(Continuous)のエミッタを1つでも含むと自動破棄しない。
//     継続放出は自分では終わらないので、いつ消すかはエンジンには判断できない。
//     戻り値のハンドルを保持して、不要になったら Stop() を呼ぶこと。
//     （勝手に消すと「ループVFXが再生中に突然消える」事故になるため、あえて消さない）
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

    /// @brief 自身を World Resource に登録する(VFX::Play から引くため)。
    void Initialize() override;

    /// @brief 破棄待ちVFXの残り時間を減らし、尽きたものを破棄する。
    void Update(float dt) override;

    /// @brief 指定位置にVFXを1つ出す。
    /// @return 生成したEntityID。失敗時は InvalidEntityID。
    /// @note   継続放出を含む場合は自動破棄されない。戻り値を保持して Stop() すること。
    ///         全エミッタが Burst なら戻り値は捨ててよい。
    CCL::ECS::EntityID Play(const std::string &path, const DirectX::XMFLOAT3 &worldPos);

    /// @brief 再生中のVFXを止める。
    /// @note  発生だけ止め、既にいる粒子は寿命まで生かしてから破棄する
    ///        （即座に消すと粒子がブツッと途切れて不自然なため）。
    ///        既に破棄待ちのVFXに対しては何もしない（毎フレーム呼ばれても寿命が延びない）。
    void Stop(CCL::ECS::EntityID handle);

  private:
    /// @brief 破棄待ちのVFX1件（EntityIDと残り時間）。
    struct ActiveVFX
    {
        CCL::ECS::EntityID id     = CCL::ECS::InvalidEntityID;
        float              remain = 0.0f; ///< 残り時間(秒)
    };

    /// @brief 継続放出(emitMode==0)のエミッタを含むか。含むなら自動破棄しない。
    bool HasContinuousEmitter(const GPUParticleComponent &particle) const;

    /// @brief いま生きている粒子が全部消えるまでの時間(秒)。
    float CalcParticleLifetime(const GPUParticleComponent &particle) const;

    /// @brief handle が既に破棄待ちに入っているか。
    bool IsPendingDestroy(CCL::ECS::EntityID handle) const;

    std::vector<ActiveVFX> m_active;

    /// 破棄までに足す余裕(秒)。バーストは生成の次フレームに出るので、その分を吸収する。
    static constexpr float kLifetimeMargin = 0.2f;
};

/// @brief 呼び出し側向けの入口。World を渡すだけで使える。
namespace VFX
{
/// @brief 指定位置にVFXを1つ出す。@see VFXSystem::Play
CCL::ECS::EntityID Play(CCL::ECS::World &world, const std::string &path, const DirectX::XMFLOAT3 &worldPos);

/// @brief 再生中のVFXを止める。@see VFXSystem::Stop
void Stop(CCL::ECS::World &world, CCL::ECS::EntityID handle);
} // namespace VFX
