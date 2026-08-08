#pragma once
// ===========================================================================
// VfxSystem.h
// -----------
// 役割:
//   ワールド座標に VFX(パーティクル)を1行で出すためのAPI。
//
//   @code
//     Vfx::Play(*m_world, "Assets/Prefabs/Particles/VFX_HitSpark", hitPos);
//   @endcode
//
//   出したVFXは寿命が来たら自動で破棄する。呼び出し側は後始末をしなくてよい。
//
// 前提:
//   VFXプレハブは「Transform + GPUParticle + TimeState を持つ1エンティティ」であること。
//   階層(親子)を持つプレハブは扱わない。これは意図的な割り切りで、階層を扱おうとすると
//   「子の行列を親から順に計算する」処理が必要になり、コード量が一気に増えるため。
//   複数エミッタが必要になったら、その時に Play を複数回呼ぶか、階層対応を足す。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/ECS/Common/CCL_Types.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Graphics/Components/Particle/GPUParticleComponent.h"

#include <DirectXMath.h>
#include <string>
#include <vector>

class VfxSystem : public CCL::ECS::IfSystem<VfxSystem,
                                            CCL::ECS::Write<TransformComponent>,
                                            CCL::ECS::Write<GPUParticleComponent>>
{
  public:
    VfxSystem()
        : IfSystem("VfxSystem")
    {
    }

    /// @brief 自身を World Resource に登録する(Vfx::Play から引くため)。
    void Initialize() override;

    /// @brief 再生中VFXの寿命を減らし、尽きたものを破棄する。
    void Update(float dt) override;

    /// @brief 指定位置にVFXを1つ出す。
    /// @return 生成したEntityID。失敗時は InvalidEntityID。
    CCL::ECS::EntityID Play(const std::string &path, const DirectX::XMFLOAT3 &worldPos);

  private:
    /// @brief 再生中VFX1件（EntityIDと残り寿命）。
    struct ActiveVfx
    {
        CCL::ECS::EntityID id     = CCL::ECS::InvalidEntityID;
        float              remain = 0.0f; ///< 残り寿命(秒)
    };

    std::vector<ActiveVfx> m_active;

    /// 寿命に足す余裕(秒)。バーストは生成の次フレームに出るので、その分を吸収する。
    static constexpr float kLifetimeMargin = 0.2f;
};

/// @brief 呼び出し側向けの入口。World を渡すだけで使える。
namespace Vfx
{
CCL::ECS::EntityID Play(CCL::ECS::World &world, const std::string &path, const DirectX::XMFLOAT3 &worldPos);
}
