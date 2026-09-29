#pragma once
// ===========================================================================
// 概要: 攻撃が当たった瞬間の画面演出（放射ブラーと色収差）の設定と残り時間を持つ
//       シングルトンコンポーネント。シーンに1つ置く。
//
// 責務:
//   - 演出の調整値（長さ・強さ・カメラへ要求する衝撃量）を持つ
//   - Trigger で立てる残り時間 timer と強さの倍率 power を持つ
//
// 備考: DamageResolutionSystem が命中時に Trigger し、ScreenImpactSystem（R02）が timer を減らして
//       ポストプロセスへ反映する。timer が 0 の間は何も書かない。
//       ヒットストップは Time::HitStopEntity、画面の揺れは CameraShakeRequestEvent で別に行う。
//       ここでカメラを直接動かすと、カメラが決めた結果にさらに足すことになり、
//       他の揺れ（着地・被弾）と合わせ方が食い違う。
// ===========================================================================

struct ScreenImpactComponent
{
    // --- 調整値（保存する） ---
    float duration          = 0.22f;  // 1回の演出の長さ（秒）
    float radialStrength    = 0.35f;  // 放射ブラーの最大の強さ
    float chromaticStrength = 0.012f; // 色収差の最大の強さ（画面に対する割合）

    // 命中1回ぶんの衝撃量（0〜1）。カメラへ揺れを要求するときの強さ。
    // 揺れの大きさや長さはカメラの CameraShake 側が持つ。
    // ドローンを続けて斬ると命中も続くので、1回ぶんは小さめにしておく。
    float shakeTrauma = 0.18f;

    // --- 実行時の状態（保存しない） ---
    float timer = 0.0f; // 残り時間（秒）。0 より大きい間だけ演出を出す
    float power = 1.0f; // この命中の強さの倍率（0.3〜1.5 くらい）。Trigger で決める

    /**
     * @brief 命中時に呼ぶ。timer を duration に戻し、強さを最新の命中のものにする。
     *
     * 続けて当たると timer が戻り続けるので、演出が途切れずに続く。
     *
     * @param p 強さの倍率
     */
    void Trigger(float p)
    {
        timer = duration;
        power = p;
    }
};
