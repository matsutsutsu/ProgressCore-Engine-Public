#pragma once
#include "Game/Logics/Combat/Weapon/Core/BulletEnums.h"
#include <string>
#include <unordered_map>

/* ==============================================================================
 * [パラメータ・マッピング チートシート]
 * 汎用変数 (Value1, Value2等) が各アタッチメントでどのパラメータに対応するかを示す。
 * * EffectID    | spawnValue1 (Scale) | spawnValue2 (Scale) | baseValue1 (Scale) | baseValue2(Scale)
 * ------------------------------------------------------------------------------
 * Homing      | 初速乗数            | -                   | 誘導力(TurnSpeed)  | -
 * Accelerate  | 初速乗数            | -                   | 毎秒の加速率       | -
 * Slow        | 初速減衰率          | -                   | 毎秒の減速率       | -
 * Pierce      | 初期貫通回数        | -                   | (命中時追加貫通)   | -
 * Knockback   | -                   | -                   | ノックバック倍率   | -
 * Explosion   | 初速乗数            | -                   | 爆発半径           | 爆発ダメージ倍率
 * Split       | 発射増加数          | 発射時拡散角        | 飛行中分裂数       | 飛行中分裂拡散角
 * Chaos       | 発射時角度ランダム  | 発射時拡散角ランダム| 発射時速度ランダム | 飛行中速度変動幅
 * Reflection  | -                   | -                   | 反射回数           | -
 * ============================================================================== */


// アタッチメント1種類あたりの汎用パラメータを保持する構造体
// ※モッドごとに使い方は異なります（例: Explosionなら value1=Radius, value2=DamageMult）
struct AttachmentParam {
    // --- Spawnフェーズ用 ---
    float spawnValue1 = 0.0f;
    float spawnScale1 = 0.0f;
    float spawnValue2 = 0.0f;
    float spawnScale2 = 0.0f;

    // --- Travel / OnHit フェーズ用 ---
    float baseValue1  = 0.0f;
    float levelScale1 = 0.0f;
    float baseValue2  = 0.0f;
    float levelScale2 = 0.0f;

    float interval = 0.0f;
};

class AttachmentMasterData {
  public:
    // ゲーム起動時（またはシーンロード時）に1回だけ呼ぶ
    static void LoadFromJson(const std::string &filepath);

    // 毎フレーム弾を撃つ時に呼ばれる超高速な取得関数
    static const AttachmentParam &GetParam(EffectID id);

  private:
    static std::unordered_map<EffectID, AttachmentParam> _data;
    static AttachmentParam                               _defaultParam;

    // 文字列からEffectIDへの変換ヘルパー
    static EffectID StringToEffectID(const std::string &name);
};