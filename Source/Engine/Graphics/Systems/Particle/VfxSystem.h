#pragma once
// ===========================================================================
// 概要: ワールド座標に VFX（パーティクル）を1行で出すための System と API（L08_Prefab）
//
// 責務:
//   - プレハブから VFX の Entity を作り、指定位置に置いてバーストを起こす
//   - 粒子が消えたころに Entity を破棄する
//   - 初期化のときに VFX のプレハブを先読みし、初回の再生でカクつかないようにする
//   - アニメーションの Effect イベントを受け、持ち主の向き・オフセット・追従を反映して出す
//
// 備考: VFX のプレハブは Transform・GPUParticle・TimeState を持つ1 Entity にする。親子は扱わない
//       全エミッタがバーストなら自動で破棄する。継続放出を1つでも含むと、いつ消すかを決められないので破棄しない
//       そのときは戻り値を持っておき、要らなくなったら Stop() を呼ぶ（アニメのイベントから出したものはブロック長で止める）
//
// @code
//   VFX::Play(*m_world, "Assets/Prefabs/Particles/VFX_HitSpark", hitPos);
// @endcode
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/ECS/Common/CCL_Types.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Graphics/Components/Particle/GPUParticleComponent.h"
#include "Engine/Time/Components/TimeState.h"

#include <DirectXMath.h>
#include <string>
#include <vector>

struct AnimEventEffectMessage;

class VFXSystem : public CCL::ECS::IfSystem<
    VFXSystem,
    CCL::ECS::Write<TransformComponent>,
    CCL::ECS::Write<GPUParticleComponent>,
    CCL::ECS::Read<TimeStateComponent>>
{
public:
    VFXSystem() : IfSystem("VFXSystem") {}

    // 自分を World のリソースに登録し（VFX::Play から引くため）、アニメの Effect イベントを購読する
    // VFX のプレハブも先読みする
    void Initialize() override;

    // 停止待ち・破棄待ちの VFX の残り時間を減らし、尽きたものを止める・破棄する
    void Update(float dt) override;

    // 指定位置に VFX を1つ出す。失敗したら InvalidEntityID
    // 継続放出を含むものは自動で破棄しないので、戻り値を持っておいて Stop する
    CCL::ECS::EntityID Play(const std::string &path, const DirectX::XMFLOAT3 &worldPos);

    // 発生だけを止め、出ている粒子は寿命まで残してから破棄する（すぐ消すと粒子が途中で切れる）
    // 破棄待ちの VFX には何もしないので、毎フレーム呼んでも寿命は延びない
    void Stop(CCL::ECS::EntityID handle);

private:
    // 破棄待ちの VFX 1件
    struct ActiveVFX
    {
        CCL::ECS::EntityID id     = CCL::ECS::InvalidEntityID;
        float              remain = 0.0f; // 破棄までの残り時間（秒）
    };

    // アニメの Effect イベント1つから VFX を出す。持ち主に Transform がなければ出さない
    void OnAnimEffect(const AnimEventEffectMessage &msg);

    // Effect イベントの基準の Entity を返す。attachTag が空ならキャラ、あれば CombatRoster から引いた武器
    // 引けなければ警告してキャラを返す
    CCL::ECS::EntityID ResolveAnchor(const AnimEventEffectMessage &msg) const;

    // VFX 全体の大きさを s 倍する（粒子の大きさ・発生範囲・速度・加速度）
    // GPU パーティクルは Transform の scale を見ないので、エミッタの設定へ直接掛ける。プレハブから作った直後にだけ使う
    void ApplyScale(GPUParticleComponent &particle, float s) const;

    // 停止待ちの VFX の残り時間を減らし、尽きたものを Stop() する
    void UpdateTimedStops(float dt);

    // VFX のプレハブ（JSON と中のテクスチャ）を全部先読みする
    // 初めて出したフレームに PNG のデコードが来てカクつくのを、シーンの切り替え中に済ませておく
    void PreloadPrefabs();

    // 継続放出（emitMode==0）のエミッタを含むか。含むなら自動では破棄しない
    bool HasContinuousEmitter(const GPUParticleComponent &particle) const;

    // 今いる粒子が全部消えるまでの時間（秒）
    float CalcParticleLifetime(const GPUParticleComponent &particle) const;

    // handle が破棄待ちに入っているか
    bool IsPendingDestroy(CCL::ECS::EntityID handle) const;

    std::vector<ActiveVFX> m_active;     // 破棄待ちの VFX
    std::vector<ActiveVFX> m_timedStops; // 停止待ちの VFX（アニメから出した継続放出。remain は Stop までの時間）

    // 区間の長さが 0（点）で来た継続放出の VFX を出し続ける最短の時間（秒）
    static constexpr float kMinEmitDuration = 0.1f;

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
