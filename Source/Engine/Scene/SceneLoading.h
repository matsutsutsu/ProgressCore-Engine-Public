#pragma once
// ===========================================================================
// 概要: 非同期ロード用の「つなぎ」シーン SceneLoading。
//
// 責務:
//   - 対象シーンの重い Initialize() を、SceneManager のロード専用 JobSystem で裏読みする
//   - 完了を毎フレーム覗き、終わったら SceneManager::RequestTransition で本命へ切り替える
//   - ロード中の表示（NOW LOADING とプログレスバー）
//
// 備考: 自身の Initialize は軽い（共通初期化のみで System は0個）。
//       毎フレームの並列（Execute → WaitForCounter で待つ）と違い、ここは
//       「投げっぱなし + 毎フレーム覗く（fire-and-poll）」でメインスレッドを止めない。
//       対象シーンは SetReady() 済みで渡るので、切替時に再 Initialize されない。
// ===========================================================================
#include "Engine/Scene/BaseScene.h"

#include "Engine/ECS/Jobs/CCL_JobSystem.h" // JobCounter（ロードジョブの寿命管理）

#include <atomic>
#include <memory>

class TextRenderer; // "NOW LOADING" 表示用（unique_ptr なのでデストラクタは .cpp に置く）
class Sprite;       // プログレスバーの単色矩形用
class SceneLoading : public BaseScene
{
private:
    // 裏読みする本命シーン。切替を頼んだ後は SceneManager が所有する
    Scene *m_nextScene = nullptr;

    // 裏読み完了フラグ（ワーカーが書き、メインが Update で覗く）
    std::atomic<bool> m_isFinished{false};

    // ロードジョブの寿命管理カウンタ。Finalize/dtor で WaitForCounter して
    // 「ジョブ完了前に this が壊れる」use-after-free を防ぐ（join 相当）。
    CCL::ECS::JobCounter m_loadCounter;

    // ローディング中の "NOW LOADING" 表示（メインスレッド専用・遅延生成）
    std::unique_ptr<TextRenderer> m_text;

    // プログレスバー描画用の白スプライト（色を掛けて単色矩形を描く・遅延生成）
    std::unique_ptr<Sprite> m_white;

    // 表示用の進捗 0..1（実ロードは進捗を取れないのでスムージングで見せる）
    float m_progress = 0.0f;

    // 最低待機時間（秒）。画面は黒で保持されているので「見せる」ためではなく、
    // ロードが一瞬で終わった場合に黒を一拍だけ置くための最小の間
    static constexpr float kMinLoadTime = 0.15f;

public:
    SceneLoading(Scene *nextScene);
    virtual ~SceneLoading() override;

    void Initialize() override;
    void Finalize() override;
    void Update(float elapsedTime) override;

    // 描画は BaseScene の Render → OnRenderUI の流れを使う
    void OnRenderUI() override;
    void OnDrawImGui() override;

    CCL::ECS::World         *GetWorld() override { return m_worldPtr; }
    CCL::ECS::SystemManager *GetSystemManager() override { return m_ecsContext->GetSystemManager(); }

    float time = 0.0f; // このシーンに入ってからの経過時間（秒）
};
