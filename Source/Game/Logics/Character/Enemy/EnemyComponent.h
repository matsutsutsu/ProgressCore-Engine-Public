#pragma once
// ===========================================================================
// EnemyComponent.h
// ----------------
// 役割:
//   「このエンティティは敵である」ことを示す識別コンポーネント。
//   SystemのForEachフィルタとして機能し、AIが将来参照するパラメータの置き場にもなる。
//
// 責務:
//   - 敵の識別タグ（BulletTag と同様の役割）。
//   - AIが参照する基礎パラメータ（移動速度・攻撃力等）の保持。
//
// アーキテクチャ上の位置付け:
//   HPは HealthComponent に一元化。ダメージパイプライン（HitboxCollisionSystem →
//   DamageResolutionSystem → HealthSystem）は HealthComponent だけを見て動く。
//   本コンポーネントはAISystemが参照する「行動パラメータ」の置き場。
//
// 設計方針:
//   - POD。AIが使うパラメータはここに追加していく（移動System・AISystemが読む）。
//   - HPを持たない。HP二重管理はバグの温床になるため HealthComponent に一元化する。
// ===========================================================================

/// @brief 敵の識別＋AIパラメータコンポーネント。
struct EnemyComponent
{
    float moveSpeed = 5.0f;  ///< 移動速度(units/秒)。AISystemが参照する。
    float attackRange = 2.0f;  ///< 攻撃射程。AIが「攻撃可能か」の判定に使う。
    float attackDamage = 10.0f; ///< 敵の攻撃ダメージ。将来の敵攻撃Systemが参照する。
};