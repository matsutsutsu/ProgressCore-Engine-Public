#pragma once

/// @brief プロトタイプ検証用のグローバルパラメータ
struct PrototypeConfig
{
    // --- ドローン関連 ---
    int   targetDroneCount   = 200;   ///< 目標とするドローンの数
    float droneApproachSpeed = 5.0f;  ///< プレイヤーへの接近速度
    float droneDashSpeed     = 25.0f; ///< 突撃時の速度
    float droneDashRange     = 15.0f; ///< 突撃を開始する距離
    bool  enableDroneSpawn   = true;  ///< スポーンのON/OFF

    // --- ジャスト回避・戦闘関連 ---
    int   killCountPerEvade = 10;    ///< 1回のジャスト回避で消し飛ばすドローンの数
    float killRadius        = 20.0f; ///< 撃破判定の半径

    // --- ダッシュボード用統計データ（読み取り専用） ---
    int currentDroneCount = 0;
    int totalKills        = 0;
    int currentCombo      = 0;
    int maxCombo          = 0;
    int hitCount          = 0;
};