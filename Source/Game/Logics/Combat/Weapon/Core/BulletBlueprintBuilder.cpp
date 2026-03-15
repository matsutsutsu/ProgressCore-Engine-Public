#include "BulletBlueprintBuilder.h"
#include <algorithm> // for std::min

/* ==============================================================================
 * [CORE ARCHITECTURE: BulletBlueprintBuilder]
 * 役割: ビットパッキングされた設計図を安全に構築する「工場」。
 *
 * [ビット操作の視覚的プロセス (AddEffect内の処理)]
 * 目的: 隣の席のデータ(別のアタッチメント個数)を破壊せずに、目的の6bitだけを書き換える。
 *
 * 1. 掃除 (&= ~) : 目的の席だけをゼロクリアする「掃除機」をかける
 * [現在の箱] ... 101010 [ 古い値 ] 110011 ...
 * & [掃除機]   ... 111111 [ 000000 ] 111111 ... <- ~(EFFECT_MASK << shift)
 * ---------------------------------------------
 * = [結果]     ... 101010 [ 000000 ] 110011 ... (目的の席だけが0になる)
 *
 * 2. 挿入 (|=) : 掃除済みの席に新しい値をスタンプする
 * [掃除済]   ... 101010 [ 000000 ] 110011 ...
 * | [新値]     ... 000000 [ 000010 ] 000000 ... <- (newCount << shift)
 * ---------------------------------------------
 * = [完成]     ... 101010 [ 000010 ] 110011 ... (安全な上書き完了！)
 * ============================================================================== */

BulletBlueprintBuilder::BulletBlueprintBuilder()
{
    // デフォルト値で初期化
    _blueprint = BulletBlueprint();
}

BulletBlueprintBuilder &BulletBlueprintBuilder::SetBaseStats(float damage, float lifeTime)
{
    _blueprint.baseDamage   = damage;
    _blueprint.baseLifeTime = lifeTime;
    return *this;
}

// クリティカル設定の実装
BulletBlueprintBuilder &BulletBlueprintBuilder::SetCritical(float chance, float multiplier)
{
    _blueprint.criticalChance     = chance;
    _blueprint.criticalMultiplier = multiplier;
    return *this;
}

// ノックバック設定の実装
BulletBlueprintBuilder &BulletBlueprintBuilder::SetKnockback(float power)
{
    _blueprint.knockbackPower = power;
    return *this;
}

// ■ エフェクト追加（ビットパッキングの核心部分）
BulletBlueprintBuilder &BulletBlueprintBuilder::AddEffect(
    BulletPhase phase, EffectID effect, int count)
{
    // enumを整数に変換してIDを取得
    int id = static_cast<int>(effect);

    // 1. 箱と席を決める
    // 配列のどこに入れるかを計算します。例: ID 13 なら 1番の箱の3番目の席
    // blockIndex (箱の番号): 13 / 10 = 1  → 1番目の箱（ベンチ）
    // subIndex(席の番号)   : 13 % 10 = 3  → 3番目の席
    int blockIndex = id / BulletBlueprint::ID_PER_BLOCK;
    int subIndex   = id % BulletBlueprint::ID_PER_BLOCK;

    // 2. ターゲットの配列を取得
    // 指定のフェーズの配列を取ってくる（例: Travelフェーズなら _blueprint.travelCounts）
    uint64_t *targetArray = GetCountsArrayPtr(phase); 
    if (!targetArray || blockIndex >= BulletBlueprint::BLOCK_COUNT) return *this;

    // 3. その箱の中のデータを書き換える &でポインタを取る
    uint64_t *targetBlock = &targetArray[blockIndex]; // 対象の箱

    // シフト量（右から何ビット目に入れるか）を計算
    int      shift        = subIndex * BulletBlueprint::BITS_PER_EFFECT;

    // 4. 現在の値を取り出して、加算して、最大値を超えないようにする
    // 現在の値を取り出す: まずシフトしてからマスクをかける
    uint64_t currentCount = (*targetBlock >> shift) & BulletBlueprint::EFFECT_MASK;
    // 加算して最大値を超えないようにする
    uint64_t newCount     = std::min(currentCount + count, BulletBlueprint::EFFECT_MASK);

    // 古い値に新しい値をセットするとビットが壊れるので、
    // マスクで古い値を消してから新しい値をセットする
    // EffectMaskを~で反転して（...111111 000000 111111... のように、目的の場所だけが0のフィルターを作る）
    // それを&=　で現在のやつに重ねて0の目的のデータのところだけ0でクリアになる
    *targetBlock &= ~(BulletBlueprint::EFFECT_MASK << shift);
    // 先ほどのクリアした場所に新しい値をシフトして|=で重ねる
    // （...000000 010000 000000... のように、目的の場所だけが新しい値のビットになる）
    *targetBlock |= (newCount << shift);

    return *this;
}

// ヘルパー関数: 配列の先頭アドレスを返すように変更
uint64_t *BulletBlueprintBuilder::GetCountsArrayPtr(BulletPhase phase)
{
    switch (phase) {
    case BulletPhase::Spawn: return _blueprint.spawnCounts;
    case BulletPhase::Travel: return _blueprint.travelCounts;
    case BulletPhase::OnHit: return _blueprint.onHitCounts;
    case BulletPhase::End: return _blueprint.endCounts;
    default: return nullptr;
    }
}

BulletBlueprintBuilder &BulletBlueprintBuilder::SetHomingParams(float strength, float range)
{
    _blueprint.params.homingStrength = strength;
    _blueprint.params.homingRange    = range;
    return *this;
}

BulletBlueprintBuilder &BulletBlueprintBuilder::SetExplosionParams(float radius, float damage)
{
    _blueprint.params.explosionRadius = radius;
    _blueprint.params.explosionDamage = damage;
    return *this;
}

BulletBlueprintBuilder &BulletBlueprintBuilder::SetSplitParams(int baseCount)
{
    _blueprint.params.splitBaseCount = baseCount;
    return *this;
}

// ■ 最終ビルド & シナジー検出
BulletBlueprint BulletBlueprintBuilder::Build()
{
    // 最後にシナジーを検出して焼き込む
    DetectSynergies();
    return _blueprint;
}



// ■ シナジー判定ロジック
// 実行時(Update)に if文 を書くのではなく、ここで判定してフラグを1つ立てるだけにする
void BulletBlueprintBuilder::DetectSynergies()
{
    _blueprint.synergyFlags = 0; // リセット

    // ヘルパーラムダ: 指定フェーズにエフェクトがあるか確認
    auto Has = [&](BulletPhase p, EffectID e) { return _blueprint.GetEffectCount(p, e) > 0; };

    // ----------------------------------------------------------
    // シナジー組み合わせ定義
    // ----------------------------------------------------------

    // 例1: 【反射】+【カオス】(Travel中)
    // 効果: 反射時にランダムな方向に跳ね返る
    if (Has(BulletPhase::Travel, EffectID::Reflection) &&
        Has(BulletPhase::Travel, EffectID::Chaos)) {
        // BulletEnums.h で定義予定のビットフラグ
        _blueprint.synergyFlags |= static_cast<uint32_t>(SynergyBit::ReflectionChaos);

        // ※とりあえず今はコメントアウトしておきます（SynergyBit定義待ち）
    }

    // 例2: 【貫通】(OnHit) + 【爆発】(End)
    // 効果: 貫通するたびにその場で小爆発を起こす
    if (Has(BulletPhase::OnHit, EffectID::Pierce) && Has(BulletPhase::End, EffectID::Explosion)) {
         //_blueprint.synergyFlags |= SynergyBit::PierceExplosion;
    }

    // 例3: 【誘導】(Travel) + 【分裂】(End)
    // 効果: 分裂した子弾も親のターゲットを引き継いで誘導する
    if (Has(BulletPhase::Travel, EffectID::Homing) && Has(BulletPhase::End, EffectID::Split)) {
        // _blueprint.synergyFlags |= SynergyBit::HomingSplit;
    }
}