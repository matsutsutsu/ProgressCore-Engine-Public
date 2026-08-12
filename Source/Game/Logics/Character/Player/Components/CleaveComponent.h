#pragma once
#include <SimpleMath.h>
#include <cstdint>

/// @brief 切り払い(CLEAVE)のパラメータと状態。
///        右クリックで「準備(Ready)」に入り、群れ(Enemy)だけがスローになる。準備中に
///        左クリックでカメラ方向へ突進(Lunge)し、前方の扇に触れたドローンを「破壊せず」
///        進行方向へ押しのけて道を切り開く。ドローンに当てたら再び準備へ戻り(スロー)、
///        左クリックで連鎖(チェイン)する。外す/時間切れで解除しクールダウンへ。
///        ※プロトタイプ版：ゲージ／ストック／BREAK は未実装（後で積む）。
struct CleaveComponent {
    /// 状態遷移。Idle=待機/クールダウン, Ready=準備(スロー・照準), Lunging=突進中。
    enum class Phase : uint8_t { Idle = 0, Ready = 1, Lunging = 2 };

    // --- Config（プランナー調整用） ---
    float cleaveSpeed    = 60.0f; ///< 突進速度（ダッシュより速い「切り込み」）
    float cleaveDuration = 0.18f; ///< 突進時間(秒)。速度×時間 ≒ 突進距離
    float cleaveCooldown = 0.4f;  ///< 連鎖が途切れた後の再突入待ち(秒)
    float readyDuration  = 1.5f;  ///< 準備(スロー)を左クリックせず放置した時の自動解除(秒・実時間)
    float fanRange       = 6.0f;  ///< 前方扇の半径(m)
    float fanHalfAngle   = 45.0f; ///< 扇の半開き角(度)。±この角度が判定内（±45=計90°）
    float knockbackSpeed = 28.0f; ///< ドローンを進行方向へ吹き飛ばす初速
    float knockbackTime  = 2.0f;  ///< 吹き飛ばし〜隊列復帰までの無効時間(秒)

    // --- スロー演出（準備中、cinematicSlow=globalScale経由で自機含む全体を遅くする） ---
    float slowScale    = 0.1f;  ///< 準備中の全体タイムスケール（0.1=10%速度でよりゆっくり）。1.0で無効
    float slowDuration = 0.35f; ///< スローのイーズ時間(秒)。解除後この時間で等速へ戻る

    // --- State（実行時） ---
    Phase phase      = Phase::Idle;
    float phaseTimer = 0.0f; ///< Ready:残り猶予 / Lunging:残り突進時間 / Idle:クールダウン残
    int   hitCount   = 0;    ///< 直近の突進で当てたドローン数（0なら連鎖終了）
    int   chainCount = 0;    ///< 現在の連鎖段数（当てるたびに+1、解除で0）

    // 入力エッジ検出（GetAction はレベル値のため押した瞬間を自前で拾う）
    bool wasCleavePressed  = false; ///< 右クリック（準備）用
    bool wasReleasePressed = false; ///< 左クリック（突進）用

    DirectX::SimpleMath::Vector3 direction = {0.0f, 0.0f, 1.0f}; ///< 突進方向（カメラ前方・水平）
};
