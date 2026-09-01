#pragma once
#include "Engine/ECS/Common/CCL_IDEmitter.h"

// JUST回避の成功状態＋ジャスト回避チェインを管理するコンポーネント
//
// チェイン:
//   ジャスト回避を連続で成立させるほど、判定は厳しく／撃破範囲は広く／移動速度は上昇する
//   （DMCのランク制度に近い、リスクとリターンが共に上がる方式）。上限を設けて青天井にしない。
//   被弾・長時間の静止でチェインは初期化される。
struct JustEvadeComponent {
    bool isJustSuccess = false;

    // 多段発動・無限ループ防止 (Traffic Control) 用データ
    CCL::ECS::EntityID lastEvadedTarget = CCL::ECS::InvalidEntityID; // 最後にジャスト回避した相手の顔（ID）
    float              resetTimer       = 0.0f;                      // ブラックリストの記憶を消すまでの時間

    // =========================================================
    // チェイン Config（プランナー調整用）
    // =========================================================
    int   maxChain            = 5;     // チェイン上限（目安5回）。速度・範囲・厳しさの上限もここで頭打ち。
    float chainSpeedPerLevel  = 0.08f; // 1段ごとの移動速度上昇率（+8%/段）
    float chainSpeedMax       = 0.40f; // 移動速度上昇の上限（+40%）
    float windowScalePerLevel = 0.10f; // 1段ごとに受付窓を縮める率（厳しくなる）
    float windowScaleMin      = 0.50f; // 受付窓倍率の下限（これ以上は厳しくしない）
    float killRadiusBase      = 3.0f;  // チェイン1段目の撃破半径（この球内の攻撃中ドローンを撃破）
    float killRadiusPerLevel  = 2.0f;  // 1段ごとの撃破半径の拡大量
    float idleBreakTime       = 4.0f;  // この秒数だけ静止し続けるとチェイン切断（0以下で無効）。※攻撃予兆待機の区別は未実装(暫定)

    // =========================================================
    // チェイン State（実行時）
    // =========================================================
    int   chainCount = 0;   // 現在のチェイン段数（0〜maxChain）
    float idleTimer  = 0.0f; // 静止し続けている経過時間
};