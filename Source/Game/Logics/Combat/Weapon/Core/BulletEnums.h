#pragma once
#include <cstdint>

// フェーズ定義
enum class BulletPhase : uint8_t {
    Spawn,  // 発射瞬間（Gun側で処理するためComponentには不要だが定義として存在）
    Travel, // 飛行中
    OnHit,  // 命中時
    End     // 消滅時
};

// 全てのアタッチメント効果を一意なIDで管理
enum class EffectID : uint16_t {
    None = 0,

    // Travel系
    Homing,         // 誘導
    Accelerate,     // 加速
    Slow,           // 減速

    // OnHit系
    Pierce,         // 貫通（処理自体はロジックだが、回数加算などの効果として）
    Knockback,      // ノックバック強化

    // End系
    Explosion,      // 爆発
    Split,          // 分裂
    BlackHole,       // ブラックホール生成

	Chaos, 	        // 不安定化（ランダムで方向がブレる）
	Reflection,     // 反射
};


// シナジー効果をビットフラグで管理
enum class SynergyBit : uint32_t {
    None            = 0,
    ReflectionChaos = 1 << 0, // 1 (反射時にランダム方向へ飛ぶ)
    PierceExplosion = 1 << 1, // 2 (貫通するたびに爆発する)
    HomingSplit     = 1 << 2  // 4 (分裂した子弾も親のターゲットを追尾する)
};