#pragma once
#include <SimpleMath.h>
#include <cstdint> // uint8_t

// 切り払いの状態遷移。Idle=待機/クールダウン, Ready=準備(スロー・照準), Lunging=突進中。
//
// このフェーズは「攻撃ボタン(左クリック)が何を意味するか」の唯一の決定者でもある。
// 同じ PlayerInputComponent.attackPressed を2つのシステムが読むため、
// どちらが処理するかはここで排他的に決まる。
//
//   フェーズ  | 攻撃ボタンの意味   | 処理するシステム
//   ----------|--------------------|---------------------------------
//   Idle      | 通常/ダッシュ/空中 | PlayerAttackSystem (L01_Input)
//   Ready     | 切り払いの突進     | PlayerCleaveSystem (L02_PostUpdate)
//   Lunging   | 無視               | （どちらも処理しない）
//
// 追加でこのボタンを読むシステムを作るときは、必ずこの表に従うこと。
// フラグを増やして意味を分けるのは禁止（システムごとに押下フラグを持つと、
// 同じボタンの解釈がシステムごとにずれる）。
enum class CleavePhase : uint8_t
{
    Idle    = 0,
    Ready   = 1,
    Lunging = 2
};

/**
 * @brief プレイヤーの特殊アクション切り払いの状態とパラメータを保持するコンポーネント
 * @details 右クリックで準備(Ready)に入り全体スロー＋モノクロ。左クリックで向いた3D方向へ突進し、
 *          前方の扇でドローンを破壊せず押しのける。当てて残ストックがあれば再び準備へ戻り連鎖する。
 *          切り払いはストック制（ジャスト回避等で溜め、最大3、1突進で1消費＝チェイン上限3）。
 *          段が進むほど準備のスローと猶予は「段ごとの固定値」で締まっていく。
 */
struct CleaveComponent
{
    // パラメータ
    float cleaveSpeed    = 60.0f; // 突進速度（ダッシュより速い「切り込み」）
    float cleaveDuration = 0.18f; // 突進時間(秒)。速度×時間 ≒ 突進距離
    float cleaveCooldown = 0.4f;  // 連鎖が途切れた後の再突入待ち(秒)
    float fanRange       = 6.0f;  // 前方扇の半径(m)
    float fanHalfAngle   = 45.0f; // 扇の半開き角(度)。±この角度が判定内
    float knockbackSpeed = 28.0f; // ドローンを進行方向へ吹き飛ばす初速
    float knockbackTime  = 2.0f;  // 吹き飛ばし〜隊列復帰までの無効時間(秒)

    // スロー演出（準備中、cinematicSlow=globalScale経由で自機含む全体を遅くする）
    float slowDuration = 0.35f; // スローのイーズ時間(秒)。解除後この時間で等速へ戻る

    // チェイン段ごとの「スロー倍率」と「準備猶予」を段1/2/3で直接管理する。
    //   チェインはストックで最大3段に固定されるので、式ではなく3点を直接置く。
    //   段が進むほど倍率↑（危険が迫る）・猶予↓（迷える時間が減る）。最終段(段3)でも
    //   人が判断できる最低限の秒数を必ず確保する（＝readyDuration3 が設計上の下限）。
    float slowScale1 = 0.10f, slowScale2 = 0.25f, slowScale3 = 0.40f;           // 準備中の全体タイムスケール
    float readyDuration1 = 1.5f, readyDuration2 = 0.95f, readyDuration3 = 0.6f; // 準備の自動解除(秒・実時間)
    bool  enableStageDecay = true;                                              // false=段階減衰オフ（常に段1の値）

    // ゲージ／ストック（ジャスト回避で溜め、最大3、1切り払いで1消費）
    float maxGauge         = 100.0f; // ストック1つ分のゲージ量
    int   maxStock         = 3;      // ストック上限（＝チェイン上限）
    float gaugeOnJustEvade = 50.0f;  // ジャスト回避成立で加算（主供給）
    float gaugeOnDash      = 8.0f;   // 通常回避(ダッシュ)開始で加算（救済）

    // State（実行時）
    CleavePhase phase      = CleavePhase::Idle;
    float       phaseTimer = 0.0f;  // Ready:残り猶予 / Lunging:残り突進時間 / Idle:クールダウン残
    int         hitCount   = 0;     // 直近の突進で当てたドローン数（0なら連鎖終了）
    int         chainCount = 0;     // 現在の連鎖段数（当てるたびに+1、解除で0）
    float       gauge      = 0.0f;  // 現在のゲージ量（次のストックまで）
    int         stock      = 3;     // 現在のストック数（0なら切り払い不可）。テスト用に満タン開始
    bool        wasDashing = false; // ダッシュ状態の立ち上がり検出（救済ゲージ用。※入力エッジではない）

    DirectX::SimpleMath::Vector3 direction = {0.0f, 0.0f, 1.0f}; // 突進方向（カメラ前方・3D）
};
