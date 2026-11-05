#pragma once
// ===========================================================================
// 概要: アニメーションのステートマシンを評価して遷移を決める System（L02_Update）
//
// 責務:
//   - グラフファイルを読み込み（初回とインスペクタの再読込）、クリップ・カーブを束縛して entry を再生する
//   - AnimParametersComponent の値で遷移条件を判定し、遷移したら AnimatorComponent へ再生を指示する
//   - トリガー（先行入力）の寿命を進める
//
// 備考:
//   - 読込と束縛（ResourceManager・ファイル I/O）は直列、評価は並列の 2 段に分けている
//   - 階層ステートマシンは外側（親）の遷移を内側（子）より優先して評価する
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Animation/Core/AnimStateMachine.h"
#include "Engine/Animation/Components/AnimParametersComponent.h"
#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Time/Components/TimeState.h"

class AnimControllerSystem : public CCL::ECS::IfSystem<
    AnimControllerSystem,
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

    // fsm.graphPath のグラフを読み、クリップを結び付けて entry から再生し直す。失敗したら何も変えない
    // パラメータの初期値は、まだ値を持っていないものにだけ入れる。ファイルを読むので、メインスレッドから呼ぶ
    // animator が握られている間（isEditorOverride / isExternalOverride）は、ステートだけ entry に戻して再生しない
    static bool LoadGraph(AnimStateMachineComponent &fsm, AnimatorComponent *animator,
                          AnimParametersComponent *params);

    // 指定のステートへ入り直し、クリップを頭から再生する（エディタからのジャンプ用）。サブマシンなら entry の葉まで降りる
    // 見つからなければ false で何も変えない。animator が握られている間は、ステートだけ変えて再生しない
    static bool JumpToState(AnimStateMachineComponent &fsm, AnimatorComponent &animator, uint32_t stateHash);

private:
    bool m_enableTransitionLog = false; // 遷移のたびにログを出すか（遷移が多くログが埋まるので既定は OFF）
};