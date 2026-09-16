#pragma once
#include "Engine/Physics/JoltIntegration/PhysicsLayers.h" // objectLayer (JPH::ObjectLayer) 用

// ===================================================================================
// ファイル名: JoltCharacterConfigComponent.h
// 役割: 仮想キャラクター（CharacterVirtual）の運動性能を定義する「設計図」。
//
// 【アーキテクチャ仕様】
// -
// 段差の乗り越え高さや、登れる坂の角度など、アクションゲーム特有のパラメータを保持。
// - 物理レイヤー(objectLayer)もここで持つ。剛体(JoltRigidbodyComponent.objectLayer)と
//   同じ ObjectLayer の仕組みに乗せることで、キャラのレイヤー分けを一元管理する。
//
// 【使い方・ルール】
// - プレイヤーや敵AIにアタッチする。JoltCharacterSetupSystem
// がこれを見てキャラクター本体(CharacterVirtual)と
//   presence用のKinematicプロキシ剛体を生成する。
// - 生成後も walkSpeed などをゲームロジック（PlayerMoveSystem等）から参照して使用する。
// ===================================================================================

// キャラクターの運動性能（プランナーが調整する値）
struct JoltCharacterConfigComponent
{
    // --- 物理レイヤー ---
    // presence用KinematicプロキシのObjectLayer。ExtendedUpdateのフィルタにも使う。
    // これにより「キャラが世界を見る層」と「他者がキャラを見る層」が一致する。
    JPH::ObjectLayer objectLayer = PhysicsLayers::PLAYER;

    float maxSlopeAngle = 45.0f; // 登れる最大角度（度）
    float maxStepHeight = 0.5f;  // 乗り越えられる段差の最大高
    float walkSpeed     = 6.0f;  // 歩行速度
    float jumpSpeed     = 10.0f; // ジャンプの初速
    float characterMass = 70.0f; // 体重（押し出し計算などに影響）

    // --- キビキビ着地のための追加パラメータ ---
    float gravityScale     = 1.0f; // 通常重力（上昇用）
    float fallGravityScale = 2.5f; // 落下時は2.5倍強く引っ張る！

    // --- 加減速の手触り（水平移動の速度追従レート） ---
    // 目標速度へ寄せる Lerp レート。加速と減速で別々に持たせ、ハイスピードの
    // 「キビキビ加速して、狙ったぶんだけ滑って止まる」を作れるようにする。
    float acceleration = 12.0f; // 加速の速さ（高いほど即最高速へ乗る）
    float deceleration = 8.0f;  // 減速の速さ（低いほどダッシュ後によく滑る）

    // --- 空中のタメ（Apex Hang Time）パラメータ ---
    float apexGravityScale      = 0.3f; // 頂点付近での重力倍率（軽いほどフワッとする）
    float apexVelocityThreshold = 1.5f; // 頂点と判定するY軸速度の閾値
};