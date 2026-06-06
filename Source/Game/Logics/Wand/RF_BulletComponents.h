#pragma once
// ===========================================================================
// @file   RF_BulletComponents.h
// @brief  飛翔する「弾」エンティティが持つコンポーネント群を定義する。
//
// @note   設計の核心: 弾は "ほぼ均一の Archetype" に保つ。
//           基本構成 = [TransformComponent] [BulletState] [BulletProgramRef]
//         ビルドの違いは BulletProgramRef.programId（データ）が吸収するため、
//         どんな多彩なビルドでも弾の Archetype は基本1種類に収束する。
//         → Chunk が密に詰まり、ForEachParallel のバッチ処理が最大限効く。
//
//         例外として「毎フレーム重い専用処理」が要る挙動だけ、任意コンポーネント
//         （HomingState / PeriodicBlastTimer）を Spawn 時に付与する。これにより
//         専用システムが "その挙動を持つ弾だけ" をフィルタして処理できる。
//         任意コンポーネントは少数(5〜8)に限定し、Archetype 爆発を防ぐ。
// ===========================================================================

#include <cstdint>
#include "SimpleMath.h" // @note 実環境のパスに合わせて調整。

#include "Engine/ECS/Common/CCL_Types.h" // @note EntityID。実環境のパスに合わせて調整。
#include "RF_PhaseTypes.h"

namespace RuneForge
{
    using Vector3 = DirectX::SimpleMath::Vector3;

    // ----------------------------------------------------------------------
    // BulletState（弾の可変ランタイム状態）
    // ----------------------------------------------------------------------

    /**
     * @brief 弾の毎フレーム変化する状態。Travel/OnHit システムが書き換える。
     *
     * @note velocity は「方向×基準速度」を保持し、加減速は speedMul で表す。
     *       実移動量は Travel システムが velocity * speedMul * localDt で積分する。
     *       こうすることで Accelerate（speedMul を増やす）/ Slow（減らす）を
     *       velocity を破壊せずに合成できる。
     * @warning フレームを跨いで生ポインタ保持しないこと（Chunk再配置で無効化する）。
     */
    struct BulletState
    {
        Vector3      velocity     = { 0, 0, 0 }; ///< 方向×基準速度
        float        lifeRemaining= 0.0f;        ///< 残り寿命（秒）。0以下で継承判定へ。
        float        speedMul     = 1.0f;        ///< 速度倍率（加減速の累積）
        std::uint8_t pierceLeft   = 0;           ///< 残り貫通回数（0で衝突時消滅）
        std::uint8_t bounceLeft   = 0;           ///< 残り反射回数
    };


    // ----------------------------------------------------------------------
    // BulletProgramRef（弾が参照する不変ビルド）
    // ----------------------------------------------------------------------

    /**
     * @brief 弾がどのコンパイル済みプログラムに従うか、と陣営を保持する。
     *
     * 発射後は不変（Read専用）。発射処理は programId をコピーするだけで済むため、
     * Spawn 時に Archetype 検索や構造変更の連鎖が起きない。
     *
     * @note faction は将来の敵弾（第4層以降の魔法使い敵）と同じ仕組みを共有する
     *       ための識別子。プレイヤー=0 / 敵=1 を想定。
     */
    struct BulletProgramRef
    {
        ProgramID    programId = InvalidProgramID; ///< 参照するプログラム
        std::uint8_t faction   = 0;                ///< 0=Player, 1=Enemy
    };


    // ----------------------------------------------------------------------
    // 任意挙動コンポーネント（optionalMask に応じて Spawn 時に付与）
    // ----------------------------------------------------------------------

    /**
     * @brief 追尾（Homing）の状態。HomingSystem が "これを持つ弾だけ" を処理する。
     * @note  最近敵探索という重い処理を全弾に課さないための分離。プロトでは未使用。
     */
    struct HomingState
    {
        CCL::ECS::EntityID target   = CCL::ECS::InvalidEntityID; ///< 現在の追尾対象
        float              turnRate = 5.0f;                      ///< 旋回速度（rad/sec）
    };

    /**
     * @brief 飛行中の周期爆発（Explosion@Travel）用タイマー。
     * @note  プロトでは未使用。Explosion 実装時に PeriodicBlastSystem が消費する。
     */
    struct PeriodicBlastTimer
    {
        float interval = 0.30f; ///< 爆発間隔（秒）
        float accum    = 0.0f;  ///< 経過累積
    };

} // namespace RuneForge
