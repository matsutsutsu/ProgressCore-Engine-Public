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

class ResourceManager;

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
    // animator が握られている間（IsOverridden）は、ステートだけ entry に戻して再生しない
    static bool LoadGraph(AnimStateMachineComponent &fsm, AnimatorComponent *animator,
                          AnimParametersComponent *params);

    // 指定のステートへ入り直し、クリップを頭から再生する（エディタからのジャンプ用）。サブマシンなら entry の葉まで降りる
    // 見つからなければ false で何も変えない。animator が握られている間は、ステートだけ変えて再生しない
    static bool JumpToState(AnimStateMachineComponent &fsm, AnimatorComponent &animator, uint32_t stateHash);

private:
    // --- 階層の探索（現在地は fsm.activePath [ルート側..., 葉]）---

    // このグラフの直下のステートを hash で引く（子グラフはたどらない）
    static const AnimState *FindDirectState(const AnimStateGraph &graph, uint32_t hash);

    // st がサブマシンなら entry をたどって葉まで path を伸ばし、葉を返す。葉ならそのまま返す
    static const AnimState *DescendToLeaf(const AnimState *st, std::vector<uint32_t> &path);

    // graph 直下の targetHash に入り、サブマシンなら葉まで path を伸ばして葉を返す
    // targetHash がこの階層になければ nullptr で、path は変えない
    static const AnimState *EnterResolveLeaf(const AnimStateGraph &graph, uint32_t targetHash,
                                             std::vector<uint32_t> &path);

    // hash のステートを階層全体から探し、ルートからのパスを out に積む。なければ nullptr
    // activePath を作り直すとき（食い違いの修復・エディタからのジャンプ）に使う
    static const AnimState *BuildPathTo(const AnimStateGraph &graph, uint32_t hash, std::vector<uint32_t> &out);

    // --- 条件の判定 ---

    // 条件 1 つの判定。AND / OR の結合は呼び出し側が行う
    static bool EvaluateCondition(const AnimatorComponent &animator, const AnimParametersComponent &params,
                                  const AnimCondition &cond);

    // 遷移 1 本（受付区間 + 条件）が満たされるか。自己遷移の除外とグループの判定は呼び出し側で行う
    static bool ConditionsMet(const AnimatorComponent &animator, const AnimParametersComponent &params,
                              const AnimTransition &t);

    // --- 遷移の実行と階層の評価 ---

    // ステートが再生するクリップ。ブレンドスペースは時間の基準（点 [0]）を返す。なければ nullptr
    static const AnimSequence *StateSequence(const AnimState &state);

    // 葉ステートのクリップを、そのステートの再生設定（ループ・速度）で再生する
    static void PlayState(AnimatorComponent &animator, const AnimState &leaf, float blendTime, bool restart);

    // ブレンドスペースの入力を目標へ寄せ、点ごとの重みを求める。単体のクリップなら何もしない
    static void UpdateBlendWeights(AnimatorComponent &animator, const AnimParametersComponent &params, float dt);

    // 遷移を実行する。graph は遷移が発火した階層（target はその直下のステート）
    // 実際に遷移したら true。false のときは fsm / params を一切変えていない
    static bool FireTransition(const AnimStateGraph &graph, int level, const AnimTransition &t,
                               AnimStateMachineComponent &fsm, AnimatorComponent &animator,
                               AnimParametersComponent &params, bool enableLog);

    // level 階層を評価する。遷移したら true（このステップの評価はそこで終わる）
    static bool EvaluateLevel(const AnimStateGraph &graph, int level, AnimStateMachineComponent &fsm,
                              AnimatorComponent &animator, AnimParametersComponent &params, bool enableLog);

    // --- リソースの束縛 ---

    // グラフ内の全ステートの sequence / ブレンドスペースの点を束縛する（サブマシンの子も再帰的に）
    static void BindGraphResources(AnimStateGraph &graph, ResourceManager &rm);

    bool m_enableTransitionLog = false; // 遷移のたびにログを出すか（遷移が多くログが埋まるので既定は OFF）
};