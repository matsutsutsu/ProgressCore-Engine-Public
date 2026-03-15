#pragma once
#include "BulletBlueprint.h"
#include <vector>

/* ==============================================================================
 * [CORE ARCHITECTURE: GunBuilder 発射パイプライン]
 * 役割: カスタマイズ構成から弾丸Entityを量産する「発射機構」。
 *
 * [データフローと役割分担]
 * (1) 非戦闘時 (ResolveBlueprint) - 構成が変わった時のみ1回実行
 * [プレイヤーの構成] -> (Builder) -> 【BulletBlueprint (圧縮設計図)】
 * |
 * [AttachmentRegistry] -----------------------+---> 【_cachedArchetype (金型)】
 * (全アタッチメントのマニュアル)                +---> 【_activeBindings (手順書の付箋)】
 *
 * (2) 戦闘時 (Fire) - 毎フレーム・1万発実行 (if文・分岐予測ミスの完全排除)
 * 【金型(_cachedArchetype)】を使って、プールからEntityを最速で実体化。
 * 【付箋(_activeBindings)】をループで回し、確定しているモッドのデータだけを
 * AddComponent(上書き)で流し込む。型の検索やHasMod判定は一切行わない。
 * ============================================================================== */


class BulletBlueprintBuilder {
  public:
    BulletBlueprintBuilder();

    // -----------------------------------------------------------------------
    // ■ 基本パラメータ設定
    // -----------------------------------------------------------------------
    BulletBlueprintBuilder &SetBaseStats(float damage, float lifeTime);

    // クリティカル設定 (確率:0.0~1.0, 倍率:1.0~)
    BulletBlueprintBuilder &SetCritical(float chance, float multiplier);

    // ノックバック設定 (力)
    BulletBlueprintBuilder &SetKnockback(float power);

    // -----------------------------------------------------------------------
    // ■ エフェクト追加
    // -----------------------------------------------------------------------
    // 指定したフェーズにエフェクトを追加します。
    // count: 重複個数 (例: Split x2 なら 2 を渡す)
    BulletBlueprintBuilder &AddEffect(BulletPhase phase, EffectID effect, int count = 1);

    // -----------------------------------------------------------------------
    // ■ 個別パラメータ設定 (必要な場合のみ呼び出す)
    // -----------------------------------------------------------------------
    BulletBlueprintBuilder &SetHomingParams(float strength, float range);
    BulletBlueprintBuilder &SetExplosionParams(float radius, float damage);
    BulletBlueprintBuilder &SetSplitParams(int baseCount);
    // 必要に応じて他も追加...

    // -----------------------------------------------------------------------
    // ■ ビルド実行
    // -----------------------------------------------------------------------
    // 最終的なBlueprintを作成して返します。
    // ここで自動的にシナジー判定が行われます。
    BulletBlueprint Build();

  private:
    // 作成中のブループリント
    BulletBlueprint _blueprint;

    // 内部ヘルパー: フェーズごとのカウント変へのポインタを取得
    uint64_t *GetCountsArrayPtr(BulletPhase phase);

    // 内部ヘルパー: シナジーを検出してフラグを立てる
    void DetectSynergies();
};