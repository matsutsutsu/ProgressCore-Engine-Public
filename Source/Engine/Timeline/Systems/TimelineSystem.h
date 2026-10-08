#pragma once
// ===========================================================================
// 概要: タイムライン（ムービー演出）の再生位置を進め、再生の始まりと終わりを管理する System。
//
// 責務:
//   - 資産の遅延読み込み
//   - 自動再生・再生要求（TimelinePlayRequestEvent）・スキップ入力による再生制御
//   - 役割の結び付けを待ち、来なければ打ち切ってゲームを進める
//   - 点イベント（Sound / Effect / Signal）を、再生位置が越えた瞬間に1回だけ発火する
//   - Animation クリップの区間だけ役割の AnimatorComponent を握り、終わったらステートマシンへ返す
//   - 再生中は GameFlowState::cinematic を立て、プレイヤー操作とボスの BT を止めさせる
//   - 終了時（スキップ・中止を含む）に TimelineFinishedEvent を発行する
//
// 備考:
//   ロジックステージ（固定ステップ）で再生位置を進める。今後足すアニメ・SE・エフェクトの
//   トラックは固定ステップで発火させたいので、時計をこちらに置いている。
//   ポーズ中はロジックが止まるので、タイムラインも自動で止まる。
//   カメラの評価は描画ステージの TimelineCameraSystem が、この再生位置を補間して行う。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/ECS/Common/CCL_TypeInfo.h"
#include "Engine/Timeline/Components/TimelinePlayerComponent.h"
#include "Engine/Timeline/TimelineEvents.h"
#include "Engine/Animation/Components/AnimatorComponent.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

class TimelineSystem : public CCL::ECS::IfSystem<
                           TimelineSystem,
                           CCL::ECS::Write<TimelinePlayerComponent>>
{
public:
    TimelineSystem()
        : IfSystem("TimelineSystem")
    {
    }
    virtual ~TimelineSystem() = default;

    void Initialize() override;
    void Update(float dt) override;

    /**
     * @brief 書き込む Component 型をスケジューラへ申告する。
     *
     * Animation トラックは役割の AnimatorComponent を GetComponent で書き換える。
     * ForEach のフィルタ（TimelinePlayerComponent）には載らないので、ここで足しておかないと
     * 同じステージの System と並列に走ってしまう。
     */
    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override
    {
        auto types = IfSystem::GetWriteTypes();
        types.push_back(CCL::ECS::TypeInfo<AnimatorComponent>::ID());
        return types;
    }

private:
    /**
     * @brief 1つの TimelinePlayer の再生を1ステップ進める（読み込み・開始待ち・再生・点イベント）。
     * @param id            進める TimelinePlayer の Entity。
     * @param player        進める再生状態。
     * @param dt            経過時間（秒）。
     * @param skipRequested このステップにスキップの入力があったか。
     * @param fires         点イベントの発火の予約（ForEach を抜けてから実行する）。
     * @param finished      終了通知の予約（同上）。
     * @return このステップで開始待ちか再生中なら true（カットシーン中フラグを立てる）。
     */
    bool StepPlayback(CCL::ECS::EntityID id, TimelinePlayerComponent &player, float dt, bool skipRequested,
                      std::vector<std::function<void()>> &fires,
                      std::vector<TimelineFinishedEvent>  &finished) const;

    /**
     * @brief Animation クリップの区間に合わせて、役割の AnimatorComponent を握る・離す。
     * @note 再生中でなければ全部離す。終了・中止・最初から再生し直しのどれでも、
     *       握ったままの実体が残らないようにするため、状態に関わらず毎ステップ呼ぶ。
     */
    void SyncAnimationClips(TimelinePlayerComponent &player) const;

    // entity の Animator を握ってクリップを再生する。Animator が無ければ false
    bool StartAnimation(CCL::ECS::EntityID entity, const AnimationClip &clip) const;

    // entity の Animator を離し、ステートマシンへ今のステートのクリップへ戻させる
    void ReleaseAnimation(CCL::ECS::EntityID entity, float blendOut) const;

    // 資産をまだ読んでいなければ読む。失敗したら hasLoadError を立てて再試行しない
    void LoadAssetIfNeeded(TimelinePlayerComponent &player) const;

    // 資産が参照する役割が、すべて生きている実体へ結び付いているか
    bool AreRolesBound(const TimelinePlayerComponent &player) const;

    // 役割名から生きている実体を引く。結び付いていない・消えているなら InvalidEntityID
    CCL::ECS::EntityID ResolveRole(const TimelinePlayerComponent &player, const std::string &role) const;

    /**
     * @brief 再生位置までに来た点イベント（Sound / Effect / Signal）を、発火の予約に積む。
     * @param id     発行元の TimelinePlayer（シグナルに載せる）。
     * @param player 発火カーソルを進める再生状態。
     * @param out    発火の予約。ForEach を抜けてから順に実行する。
     * @note スキップしたステップでは、fireOnSkip のクリップ以外は鳴らさずに読み飛ばす。
     *       終端へ飛んだ分を全部鳴らすと、スキップした瞬間に残りの SE とエフェクトが一斉に出る。
     */
    void CollectClipFires(CCL::ECS::EntityID id, TimelinePlayerComponent &player,
                          std::vector<std::function<void()>> &out) const;

    // 1つの Sound クリップの発火を予約する
    void QueueSound(const TimelinePlayerComponent &player, const SoundClip &clip,
                    std::vector<std::function<void()>> &out) const;

    // 1つの Effect クリップの発火を予約する
    void QueueEffect(const TimelinePlayerComponent &player, const EffectClip &clip,
                     std::vector<std::function<void()>> &out) const;

    /**
     * @brief 再生状態を1ステップ進める。
     * @param id            進める TimelinePlayer の Entity（ログ用）。
     * @param player        進める再生状態。
     * @param dt            経過時間（秒）。
     * @param skipRequested このステップにスキップの入力があったか。
     * @return このステップに終わったら true。
     * @note 終了イベントの発行は呼び出し側が行う（ForEach 内から発行しないため）。
     */
    bool Advance(CCL::ECS::EntityID id, TimelinePlayerComponent &player, float dt, bool skipRequested) const;

    // 打ち切って終了状態にする（資産の読み込み失敗・役割の未解決の保険）
    void Abort(TimelinePlayerComponent &player) const;

    // スキップ入力（攻撃/ジャンプ）が押されているか
    bool ReadSkipInput() const;
};
