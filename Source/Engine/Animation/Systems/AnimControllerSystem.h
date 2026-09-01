#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Animation/Core/AnimStateMachine.h"
#include "Engine/Animation/Components/AnimParametersComponent.h"
#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Time/TimeState.h"

// ===================================================================================
// ファイル: AnimControllerSystem.h
// 概要: アニメーションの状態遷移を決定する「評価器（舞台監督）」システム
// 
// [ 役割 ]
// 毎フレーム、AnimParametersComponent（掲示板）の値を読み取り、
// AnimStateMachine（ルールブック）の遷移条件を満たしているかをチェックする。
// 条件を満たした場合、現在の状態を書き換え、
// AnimatorComponent に対して新しいアニメーションの再生を命令する。
// ※物理計算や描画は一切行わない純粋なステートマシン評価器。
// ===================================================================================

class AnimControllerSystem : public CCL::ECS::IfSystem<AnimControllerSystem,
    CCL::ECS::Write<AnimStateMachineComponent>,
    CCL::ECS::Write<AnimatorComponent>,
    CCL::ECS::Write<AnimParametersComponent>,
    CCL::ECS::Read<TimeStateComponent>>
{
public:
    // 基底クラスのコンストラクタ呼び出しもフルパスで指定
    AnimControllerSystem() : IfSystem("AnimControllerSystem") 
    {
    }
    virtual ~AnimControllerSystem() = default;


    // 実行順序の宣言（TaskScheduler が同一ステージ内の順序決定に使う）。
    // AnimSetup が不足コンポーネントを付与してから状態遷移を評価する。
    std::vector<const char *> GetRunAfter() const override { return {"AnimSetupSystem"}; }

    void Update(float dt) override;

    // エディタ用のシステムインスペクタ描画
    void OnGui() override;

  private:
    // 状態遷移ログを出力するかどうかのフラグ（デフォルトOFFでコンソール汚染を防ぐ）
    bool m_enableTransitionLog = false;
};