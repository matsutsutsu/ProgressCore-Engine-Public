#pragma once

// ===================================================================================
// ファイル名: JoltCharacterConfigComponent.h
// 役割: 仮想キャラクター（CharacterVirtual）の運動性能を定義する「設計図」。
//
// 【アーキテクチャ仕様】
// -
// 段差の乗り越え高さや、登れる坂の角度など、アクションゲーム特有のパラメータを保持。
//
// 【使い方・ルール】
// - プレイヤーや敵AIにアタッチする。JoltCharacterSetupSystem
// がこれを見てキャラクターを生成する。
// - 生成後も walkSpeed
// などをゲームロジック（PlayerMoveSystem等）から参照して使用する。
// ===================================================================================

// キャラクターの運動性能（プランナーが調整する値）
struct JoltCharacterConfigComponent {
    float maxSlopeAngle = 45.0f; // 登れる最大角度（度）
    float maxStepHeight = 0.5f;  // 乗り越えられる段差の最大高
    float walkSpeed     = 6.0f;  // 歩行速度
    float jumpSpeed     = 10.0f; // ジャンプの初速
    float characterMass = 70.0f; // 体重（押し出し計算などに影響）

    // --- ★キビキビ着地のための追加パラメータ ---
    float gravityScale     = 1.0f; // 通常重力（上昇用）
    float fallGravityScale = 2.5f; // 落下時は2.5倍強く引っ張る！

    // --- ★ダッシュ後の慣性・減速パラメータ ---
    float deceleration = 8.0f; ///< 減衰力（低いほどダッシュ後によく滑る）
};