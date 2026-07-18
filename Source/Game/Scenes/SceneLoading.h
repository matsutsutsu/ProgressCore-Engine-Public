#pragma once
#include "Game/Scenes/BaseScene.h"

#include "Engine/ECS/Jobs/CCL_JobSystem.h" // JobCounter（ロードジョブの寿命管理）

#include <atomic>
#include <memory>

class TextRenderer; // 前方宣言（"NOW LOADING" 表示用。unique_ptr なので dtor は .cpp）
class Sprite;       // 前方宣言（プログレスバーの単色矩形用）

// ============================================================================
// SceneLoading
//   非同期ロード用の「つなぎ」シーン。
//   - 自身の Initialize は軽い（共通初期化のみ）。
//   - 対象シーン(_nextScene)の重い Initialize() を SceneManager のロード専用
//     JobSystem へ投げ、ワーカースレッドで裏読みする。
//   - 毎フレーム版の並列（Execute→WaitForCounter で待つ）と違い、ここは
//     「投げっぱなし＋毎フレーム覗く（fire-and-poll）」でメインを止めない。
//   - 完了したら SceneManager::ChangeScene(_nextScene) で本線へ。
//     対象は SetReady() 済みなので再 Initialize されない。
// ============================================================================
class SceneLoading : public BaseScene {
  private:
    // 次に遷移するシーン（裏読み対象）
    Scene *_nextScene = nullptr;

    // 裏読み完了フラグ（ワーカーが書き、メインが Update で覗く）
    std::atomic<bool> _isFinished{false};

    // ロードジョブの寿命管理カウンタ。Finalize/dtor で WaitForCounter して
    // 「ジョブ完了前に this が壊れる」use-after-free を防ぐ（join 相当）。
    CCL::ECS::Core::JobCounter _loadCounter;

    // ローディング中の "NOW LOADING" 表示（メインスレッド専用・遅延生成）
    std::unique_ptr<TextRenderer> _text;

    // プログレスバー描画用の白スプライト（色を掛けて単色矩形を描く・遅延生成）
    std::unique_ptr<Sprite> _white;

    // 表示用の進捗 0..1（実ロードは進捗を取れないのでスムージングで見せる）
    float _progress = 0.0f;

    // 最低表示時間（秒）。ロードが一瞬で終わっても、この時間は必ずローディング画面を出す。
    static constexpr float kMinLoadTime = 1.5f;

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

    CCL::ECS::Core::World* GetWorld() override { return _worldPtr; }
    CCL::ECS::SystemManager* GetSystemManager() override { return _ecsContext->GetSystemManager(); }

    float time = 0.0f;
};
