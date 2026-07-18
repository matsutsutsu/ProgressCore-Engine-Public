#pragma once

#include "Scene.h"
#include <string>

#include "Engine/ECS/Jobs/CCL_JobSystem.h" // シーン非同期ロード用の汎用スレッドプール

// World クラスが存在することをコンパイラに教える
namespace CCL::ECS::Core { class World; }

// シーンマネージャー
class SceneManager {
private:
    SceneManager() {}
    ~SceneManager() {}

public:
    // 唯一のインスタンス取得
    static SceneManager& Instance() {
        static SceneManager instance;
        return instance;
    }

    // 初期化
    void Initialize();

    // 終了化
    //void Finalize();

    // --- 追加：固定更新 ---
    void FixedUpdate(float fixedTime);

    // 更新処理
    void Update(float elapsedTime);

    // 描画処理
    void Render();

    // GUI描画
    void DrawGUI();

    // シーンクリア
    void Clear();

    // シーン切り替え
    void ChangeScene(const std::string& name);

    // インスタンスを直接渡すオーバーロード
    void ChangeScene(Scene *scene);

    // 非同期シーン切り替え：ローディング画面を挟み、対象シーンの重い
    // Initialize() を裏（ワーカースレッド）で走らせてから遷移する。
    void ChangeSceneAsync(const std::string& name);

    // シーンロード専用の汎用スレッドプール（各Worldの並列プールとは独立）。
    // SceneLoading がここへ「対象シーンの Initialize」ジョブを投げる。
    CCL::ECS::Core::JobSystem& GetLoaderJobs() { return _loaderJobs; }

    // 現在のシーン名を取得・設定
    const std::string& GetCurrentSceneName() const { return _currentSceneName; }
    void SetCurrentSceneName(const std::string& name) { _currentSceneName = name; }

    // --- ECSデータ駆動のシーン保存・読み込み ---
    void SaveScene(CCL::ECS::Core::World* world, const std::string& filepath);
    void LoadScene(CCL::ECS::Core::World* world, const std::string& filepath);

	Scene* GetCurrentScene() const { return dynamic_cast<Scene*>(currentScene); }


private:


    Scene* currentScene = nullptr; // 現在のシーン
    Scene* nextScene = nullptr;    // 次に遷移するシーン

    // 現在のシーン名を保持する変数
    std::string _currentSceneName = "";

    // シーン非同期ロード専用のスレッドプール（毎フレーム並列とは別インスタンス）。
    // ※ 各Worldの JobSystem はそのWorld自身をロードできない（鶏卵問題）ため、
    //    ロードはWorldに依存しないここのプールで回す。
    CCL::ECS::Core::JobSystem _loaderJobs;
};