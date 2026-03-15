#include"SceneManager.h"
#include"SceneFactory.h"

// シーン登録ヘッダー
#include"Game/Scenes/EditorScene.h"
#include "Game/Scenes/ParticleScene.h"
#include "Game/Scenes/GameScene.h"
#include "Game/Scenes/ShopScene.h"

//初期化
void SceneManager::Initialize()
{
	//シーン登録
	//SceneFactory::Instance().Register("ModelViewer", []() { return new ModelViewerScene(); });
	SceneFactory::Instance().Register("Editor", []() { return new EditorScene(); });
	SceneFactory::Instance().Register("Game", []() { return new GameScene(); });

	SceneFactory::Instance().Register("Particle", []() { return new ParticleScene(); });

	SceneFactory::Instance().Register("Shop", []() { return new ShopScene(); });

	// 最初に表示するシーン
	//ChangeScene("Editor");
	ChangeScene("Shop");
	//ChangeScene("Game");
	//ChangeScene("Particle");
}

// --- 追加：固定更新の実装 ---
void SceneManager::FixedUpdate(float fixedTime)
{
	// シーン遷移待ちの状態では物理演算を行わないのが安全
	if (nextScene == nullptr && currentScene != nullptr)
	{
		currentScene->FixedUpdate(fixedTime);
	}
}

//更新処理
void SceneManager::Update(float elapsedTime)
{
	if (nextScene != nullptr)
	{
		//古いシーンを終了処理
		Clear();

		//新しいシーンを設定
		currentScene = nextScene;
		nextScene = nullptr;

		//シーン初期化処理
        // 【1段階目】: 重いデータロード (Initialize)
        // もし SceneLoading を経由して既に行われていれば、IsReady() が true
        // なのでスキップされる
        if (!currentScene->IsReady()) {
            currentScene->Initialize();

			// 終わった瞬間にマネージャーが責任を持ってフラグを立てる
            currentScene->SetReady();
        }

		// 【2段階目】: GPUへのセットアップ (Start) 
        // これは絶対にメインスレッドのこのタイミングで1回だけ呼ばれる。
        // dc->Map や UIManager の dc へのバインド等はここで行う。
        currentScene->Start();
	}

	if (currentScene != nullptr)
	{
		currentScene->Update(elapsedTime);
	}

}

//描画処理
void SceneManager::Render()
{
	if (currentScene != nullptr)
	{
		currentScene->Render();
	}
}

//GUI描画
void SceneManager::DrawGUI()
{
	if (currentScene != nullptr)
	{
		currentScene->DrawGUI();
	}
}

//シーンクリア
void SceneManager::Clear()
{
	if (currentScene != nullptr)
	{
		currentScene->Finalize();
		delete currentScene;
		currentScene = nullptr;
	}
}

//シーン切り替え
void SceneManager::ChangeScene(const std::string& name)
{
    // 名前を記録しておく
    _currentSceneName = name;

	Scene* newScene = SceneFactory::Instance().Create(name);
	if (newScene) {
		nextScene = newScene;
	}
}


void SceneManager::ChangeScene(Scene *scene)
{
    // ※ここでは _currentSceneName は自動更新されない
    // 呼び出し側（GateSystem）で SetCurrentSceneName を呼んでからこれを使う
    if (nextScene != nullptr) {
        delete nextScene;
    }

    // nextSceneに直接セット
    nextScene = scene;
}