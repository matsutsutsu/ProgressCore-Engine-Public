#pragma once
#include "Engine/Scene/BaseScene.h"

#include "Engine/ECS/Jobs/CCL_JobSystem.h" // JobCounter（ロードジョブの寿命管理）

#include <atomic>
#include <memory>

class TextRenderer; // 前方宣言（"NOW LOADING" 表示用。unique_ptr なので dtor は .cpp）
class Sprite;       // 前方宣言（プログレスバーの単色矩形用）

// ============================================================================
// SceneLoading
//   非同期ロード用の「つなぎ」シーン。
//   - 自身の Initialize は軽い（共通初期化のみ）。
//   - 対象シーン(m_nextScene)の重い Initialize() を SceneManager のロード専用
//     JobSystem へ投げ、ワーカースレッドで裏読みする。
//   - 毎フレーム版の並列（Execute→WaitForCounter で待つ）と違い、ここは
//     「投げっぱなし＋毎フレーム覗く（fire-and-poll）」でメインを止めない。
//   - 完了したら SceneManager::ChangeScene(m_nextScene) で本線へ。
//     対象は SetReady() 済みなので再 Initialize されない。
// ============================================================================
class SceneLoading : public BaseScene {
  private:
    // 次に遷移するシーン（裏読み対象）
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

    // 最低待機時間（秒）。画面は黒で保持されるため“見せる”ためではなく、
    // ロードが一瞬で終わった場合に黒を一拍だけ置くための最小間。
    static constexpr float kMinLoadTime = 0.15f;

  public:
    // コンストラクタで次のシーンを受け取る
    SceneLoading(Scene *nextScene);
    virtual ~SceneLoading() override;

    void Initialize() override;
    void Finalize() override;
    void Update(float elapsedTime) override;

    // 描画は BaseScene の Render -> OnRenderUI の流れを使う
    void OnRenderUI() override;
    void OnDrawImGui() override;

    CCL::ECS::World* GetWorld() override { return m_worldPtr; }
    CCL::ECS::SystemManager* GetSystemManager() override { return m_ecsContext->GetSystemManager(); }

    float time = 0.0f;
};
