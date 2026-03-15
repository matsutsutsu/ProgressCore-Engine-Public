#pragma once
#include "ECS/Common/CCL_Common.h"

// ===========================================================================
// 【コアデータ】(フェーズを問わず共通して使われる純粋なパラメータ)
// ===========================================================================
struct HomingData    { CCL::ECS::EntityID targetID; float turnSpeed; };
struct AccelerateData{ float rate; };
struct SlowData      { float brakePower; };
struct ExplosionData { float radius; float damageMult; };
struct SplitData     { int count; float angleSpread; };
struct ChaosData     { float speedMultRange; };
struct PierceData    { int addCount; };
struct KnockbackData { float powerMult; };
struct ReflectData   { int addCount; };

// ===========================================================================
// 【Travelフェーズ用】 (飛行中・毎フレーム処理)
// 間隔制御用のタイマー（intervalTimer）や、状態フラグを個別に持つ
// ===========================================================================
struct TravelHomingComponent     { HomingData data; };
struct TravelAccelerateComponent { AccelerateData data; };
struct TravelSlowComponent       { SlowData data; };
struct TravelExplosionComponent  { ExplosionData data; float intervalTimer; };
struct TravelSplitComponent      { SplitData data; float intervalTimer; bool hasSplit; };
struct TravelChaosComponent      { ChaosData data; float changeTimer; };
struct TravelPierceComponent     { PierceData data; };
struct TravelKnockbackComponent  { KnockbackData data; };
struct TravelReflectComponent    { ReflectData data; };

// ===========================================================================
// 【OnHitフェーズ用】 (命中時・BulletHitStateComponentがtrueのフレームのみ処理)
// ===========================================================================
struct OnHitHomingComponent     { HomingData data; };
struct OnHitAccelerateComponent { AccelerateData data; };
struct OnHitSlowComponent       { SlowData data; };
struct OnHitExplosionComponent  { ExplosionData data; };
struct OnHitSplitComponent      { SplitData data; };
struct OnHitChaosComponent      { ChaosData data; };
struct OnHitPierceComponent     { PierceData data; }; 
struct OnHitKnockbackComponent  { KnockbackData data; };
struct OnHitReflectComponent    { ReflectData data; int remainingCount; }; // 反射は残り回数を管理
