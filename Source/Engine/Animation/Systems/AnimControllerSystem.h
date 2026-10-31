#pragma once
// ===========================================================================
// 概要: アニメーションのステートマシンを評価して遷移を決める System（L02_Update）。
//
// 責務:
//   - グラフファイルを読み込み（初回とインスペクタの再読込）、クリップ・カーブを束縛して entry を再生する
//   - AnimParametersComponent の値で遷移条件を判定し、遷移したら AnimatorComponent へ再生を指示する
//   - トリガー（先行入力）の寿命を進める
//
// 備考:
//   - 読込と束縛（ResourceManager・ファイル I/O）は直列、評価は並列の 2 段に分けている。
//   - 階層ステートマシンは外側（親）の遷移を内側（子）より優先して評価する。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Animation/Core/AnimStateMachine.h"
#include "Engine/Animation/Components/AnimParametersComponent.h"
#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Time/Components/TimeState.h"

class AnimControllerSystem : public CCL::ECS::IfSystem<AnimControllerSystem,
                                                       CCL::ECS::Write<AnimStateMachineComponent>,
                                                       CCL::ECS::Write<AnimatorComponent>,
                                                       CCL::ECS::Write<AnimParametersComponent>,
                                                       CCL::ECS::Read<TimeStateComponent>>
{
public:
    AnimControllerSystem() : IfSystem("AnimControllerSystem") {}
    virtual ~AnimControllerSystem() = default;

    // AnimSetupSystem が足りないコンポーネントを付けてから評価する
    std::vector<const char *> GetRunAfter() const override { return {"AnimSetupSystem"}; }

    void Update(float dt) override;

    void OnGui() override;

    /**
     * @brief fsm.graphPath のグラフを読み込み、クリップを束縛して entry から再生し直す。
     *
     * 初回の読込とインスペクタの再読込の両方がこれを通る。
     * パラメータの初期値は、まだ値を持っていないパラメータにだけ入れる。
     *
     * @param fsm      読み込む先。失敗したときは何も変えない。
     * @param animator entry を再生する先。nullptr なら再生しない。所有しない。
     * @param params   初期値を入れる先。nullptr なら入れない。所有しない。
     * @return 読み込めたか。
     * @note ファイル I/O と ResourceManager を使うので、メインスレッドから呼ぶこと。
     * @note animator が握られている間（isEditorOverride / isExternalOverride）は、ステートだけ entry に戻して再生しない。
     */
    static bool LoadGraph(AnimStateMachineComponent &fsm, AnimatorComponent *animator,
                          AnimParametersComponent *params);

    /**
     * @brief 指定のステートへ入り直し、そのクリップを頭から再生する（エディタからのジャンプ用）。
     * @param stateHash 入るステート。どの階層でもよく、サブマシンなら entry の葉まで降りる。
     * @return ステートが見つかったか。見つからなければ何も変えない。
     * @note animator が握られている間は、ステートだけ変えて再生しない。
     */
    static bool JumpToState(AnimStateMachineComponent &fsm, AnimatorComponent &animator, uint32_t stateHash);

private:
    bool m_enableTransitionLog = false; // 遷移のたびにログを出すか（遷移が多くログが埋まるので既定は OFF）
};