#pragma once
#include "Game/Logics/Combat/Weapon/Core/GunBuilder.h"

// ===================================================================================
// クラス名: GunComponent
// 役割: エンティティ（プレイヤーや敵）に「銃」としての
//       機能を持たせるデータコンポーネント
//
// 機能:
//   - GunBuilder（弾の生成レシピ）を保持する
//   - 発射間隔（クールダウン）の状態管理を行う
// ===================================================================================

struct GunComponent {
    // 銃の機能そのものを保持
    GunBuilder gun;

    // 連射制御用
    float coolDownTimer = 0.0f;
    float fireInterval = 0.2f; // 0.2秒に1発
};