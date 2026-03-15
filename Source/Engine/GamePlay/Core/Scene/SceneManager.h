#pragma once

#include "Scene.h"

#include <string>
#include "Game/Logics/Combat/Weapon/Core/GunBuilder.h" // GunBuilderを使うため

// プレイヤーの引き継ぎデータ
struct PlayerPersistentData {
    int currentHp = 100;
    int maxHp     = 100;
    int money     = 180; // 追加：所持金

    // 現在のステージレベル (初期値1)
    int stageLevel = 1;

    GunBuilder gunConfig; // 銃のカスタマイズ状態

    bool hasData = false; // データが保存されているかフラグ

    // データをリセットする関数（ゲームオーバー時などに呼ぶ）
    // これするとプレイヤーの初期設定にならなくね？使うのか？
    void Reset()
    {
        hasData   = false;
        currentHp = 100;
        money      = 180;           
        stageLevel = 1;            // リセット時は1に戻す
        gunConfig = GunBuilder(); // 初期状態に戻す
    }
};

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

    // これを経由して money や HP を書き換える
    PlayerPersistentData &GetPlayerData() { return playerData; }

// ★追加: 現在のシーン名を取得・設定
    const std::string& GetCurrentSceneName() const { return _currentSceneName; }
    void SetCurrentSceneName(const std::string& name) { _currentSceneName = name; }

private:
    // どこからでもアクセスできるプレイヤーデータ
    PlayerPersistentData playerData;

    Scene* currentScene = nullptr; // 現在のシーン
    Scene* nextScene = nullptr;    // 次に遷移するシーン

    // 現在のシーン名を保持する変数
    std::string _currentSceneName = "";
};