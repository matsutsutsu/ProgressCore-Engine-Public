#pragma once
// ===========================================================================
// 概要: 回避ダッシュ中の接触からジャスト回避の成立を判定し、成立時の処理を行う System。
//
// 責務:
//   - JoltCollisionEvent を溜め、Update でジャスト回避の成立を判定する
//   - 成立したら JustEvadeSuccessEvent と、カメラの画角を広げる要求を発行する
//   - 成立時に世界スローとラッシュ（踏み込み）の開始、専用モーション、ビネット演出を始める
//   - 成立フラグと、同じ相手に続けて成立させないための記録を片付ける
//
// 備考: 成立するのは、突撃中のドローンか、判定が開いている Hitbox に受付時間内に触れたときだけ。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Character/Player/Components/JustEvadeComponent.h"
#include "Game/Logics/Character/Player/Components/DashComponent.h"
#include "Engine/Time/Components/TimeState.h"
#include "Engine/Physics/Components/Collision/JoltCollisionEvent.h"

#include <SimpleMath.h>
#include <vector>

struct DroneComponent; // ポインタ参照のみのため前方宣言で足りる

// TimeStateComponent は resetTimer を localDt で進めるのに読むだけなので Read。
// 世界スローは Time::SustainWorldSlow（ワールド側）が担い、ここでは per-entity の時間を書かない。
class JustEvadeSystem : public CCL::ECS::IfSystem<JustEvadeSystem,
                                                  CCL::ECS::Write<JustEvadeComponent>,
                                                  CCL::ECS::Write<DashComponent>,
                                                  CCL::ECS::Read<TimeStateComponent>>
{
public:
    JustEvadeSystem()
        : IfSystem("JustEvadeSystem")
    {
    }

    void Initialize() override;
    void Update(float dt) override;

    /**
     * @brief 成立時のイベントハンドラが書く型を、テンプレート引数の分に足して申告する。
     *
     * テンプレート引数に足すと ForEach の Archetype フィルタまで狭まり、ループの対象から外れてしまう。
     * フィルタと申告は別なのでここで足す。
     */
    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override;

    // 成立時に Dodge の向きを決めるため、プレイヤーの向き/ロックオン先と相手の位置を ForEach 外で読む。
    // その分の Read 型をテンプレート引数に足して申告する。
    std::vector<CCL::ECS::TypeID> GetReadTypes() const override;

    // 接触の配送が終わってから走らせる（同じ L05_Collision のため順序が不定になる）。
    std::vector<const char *> GetRunAfter() const override
    {
        return {"JoltCollisionEventSystem"};
    }

private:
    /**
     * @brief 接触した2体について、ジャスト回避が成立するかを調べ、成立したら記録する。
     *
     * 接触イベントはどちらがプレイヤーか決まっていないので、呼び出し側が向きを入れ替えて2回呼ぶ。
     *
     * @param playerCandidate プレイヤーかもしれない側。JustEvadeComponent と DashComponent が無ければ何もしない。
     * @param otherCandidate  ぶつかった相手。突撃中のドローンか、判定が開いているボスの剣のときだけ成立する。
     */
    void CheckJustEvade(CCL::ECS::EntityID playerCandidate, CCL::ECS::EntityID otherCandidate);

    // このフレームに届いた接触。コールバックが溜め、Update が処理して空にする。
    //
    // EventBus は同期配送なので、コールバック内で判定すると処理が発行元
    // (JoltCollisionEventSystem)のタスク上で走り、この System の申告が
    // スケジューラから見えなくなる。溜めるだけにして、判定は Update で行う。
    std::vector<JoltCollisionEvent> m_pendingContacts;

    /**
     * @brief そのドローンが、今ぶつかったらジャスト回避が成立する相手かを返す。
     *
     * 隊列で待機しているだけのドローンにぶつかっても成立しないようにする。
     *
     * @param drone 調べるドローン。
     * @return 突撃中で、吹き飛ばされていなければ true。
     */
    bool IsDroneHostile(const DroneComponent *drone) const;

    /**
     * @brief 回避方向から、再生する Dodge アニメの向き（前/後/左/右）を求める。
     *
     * 基準の前方はハイブリッド：ロックオン中は対象への向き、非ロックオン時はキャラの向き(currentYaw)。
     * その前方に対して回避方向が前後左右どちらに近いかで 0=前/1=後/2=左/3=右 を返す。
     * 返り値は AnimParameters の float パラメータ "DodgeDir" にそのまま入れる（遷移はアニメグラフが分岐）。
     *
     * @param playerID 回避したプレイヤー。
     * @param evadeDir 回避（ダッシュ）方向。正規化済み・XZ平面で見る。
     * @return 0=前 / 1=後 / 2=左 / 3=右。
     */
    float ComputeDodgeDir(CCL::ECS::EntityID playerID, const DirectX::SimpleMath::Vector3 &evadeDir) const;
};