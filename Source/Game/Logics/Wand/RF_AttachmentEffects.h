#pragma once
// ===========================================================================
// @file   RF_AttachmentEffects.h
// @brief  アタッチメント効果の「フェーズ別ディスパッチ」と、フェーズ仕様を
//         一元化した弾生成ヘルパ(SpawnBulletsFromProgram)を宣言する。
//
// @note   ここがアタッチメント挙動の単一の権威。新アタッチメントの追加は、
//         原則この対応表（.cpp の switch / 将来はレジストリ）への追記だけで済む。
//         「フェーズ＝フック点」という設計のため、効果は『状態(BulletState)を
//         書き換える純粋関数』として表現し、移動の積分など実処理は各システム側に
//         任せる（効果ロジックとシステム実行を分離＝テスト容易・再利用可）。
// ===========================================================================

#include <vector>
#include "SimpleMath.h" // @note 実環境のパスに合わせて調整。
#include "Engine/ECS/Common/CCL_Types.h" // @note EntityID。実環境のパスに合わせて調整。

#include "RF_PhaseTypes.h"
#include "RF_WandProgram.h"
#include "RF_BulletComponents.h"
#include "RF_BulletEvents.h"

// World は宣言ヘッダでは前方宣言で十分（.cpp で完全定義を取り込む）。
namespace CCL::ECS::Core { class World; }

namespace RuneForge
{
    using Vector3 = DirectX::SimpleMath::Vector3;

    // ----------------------------------------------------------------------
    // フェーズ別の効果適用
    // ----------------------------------------------------------------------

    /**
     * @brief Spawn フェーズ: 発射時の「弾1発以上の初期化情報リスト」を構築する。
     *
     * 基準として aimDir 方向の1発を置き、Spawn の各 EffectOp で加工する
     * （Accelerate→初速倍率増、Split→要素追加、Focus→拡散除去 …）。
     *
     * @param prog   参照プログラム
     * @param aimDir 照準方向（正規化済み想定）
     * @param out    [出力] 生成すべき弾の初期化情報。呼び出し前に空にしておくこと。
     */
    void ApplySpawnPhase(const CompiledWandProgram& prog,
                         const Vector3&             aimDir,
                         std::vector<BulletInit>&   out);

    /**
     * @brief Travel フェーズ: 飛行中、毎フレームの状態更新（加減速・揺らぎ等）。
     * @param prog    参照プログラム
     * @param st      [入出力] 弾の状態
     * @param localDt この弾に適用する個別時間（TimeStateComponent.localDt）
     * @warning rawDt ではなく必ず localDt を渡すこと（ヒットストップ等に追従）。
     */
    void ApplyTravelPhase(const CompiledWandProgram& prog,
                          BulletState&               st,
                          float                      localDt);

    /**
     * @brief OnHit フェーズ: 衝突した瞬間の状態更新（貫通加速・反射等）。
     * @param prog 参照プログラム
     * @param st   [入出力] 弾の状態
     * @param hit  衝突イベント情報
     */
    void ApplyHitPhase(const CompiledWandProgram& prog,
                       BulletState&               st,
                       const BulletHitEvent&      hit);


    // ----------------------------------------------------------------------
    // 弾生成（発射と継承の共通経路）
    // ----------------------------------------------------------------------

    /**
     * @brief プログラムに従って弾エンティティを生成する。発射と継承の唯一の生成経路。
     *
     * 内部で ApplySpawnPhase を呼んで弾数・初速を決め、各弾を World へ Spawn する。
     * optionalMask に応じて HomingState 等の任意コンポーネントを付与する。
     *
     * @param world     対象 World
     * @param reg        プログラムレジストリ
     * @param programId  生成元プログラム（InvalidProgramID なら何もしない）
     * @param origin     生成位置（ワールド座標）
     * @param aimDir     基準方向（正規化済み想定）
     * @param faction    0=Player / 1=Enemy
     * @param baseSpeed  基準速度
     * @param baseLife   基準寿命（秒）
     *
     * @warning 構造変更を伴うため、必ず "シングルスレッドのシステム" から呼ぶこと
     *          （ForEachParallel 内からは呼ばない）。Spawn 自体は PendingOps 経由で
     *          遅延適用されるため、呼び出し中のイテレーションは無効化しない。
     */
    void SpawnBulletsFromProgram(CCL::ECS::Core::World& world,
                                 WandProgramRegistry&   reg,
                                 ProgramID              programId,
                                 const Vector3&         origin,
                                 const Vector3&         aimDir,
                                 std::uint8_t           faction,
                                 float                  baseSpeed,
                                 float                  baseLife);

} // namespace RuneForge
