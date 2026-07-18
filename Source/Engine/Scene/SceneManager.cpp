#include"SceneManager.h"
#include"SceneFactory.h"

// シーン登録ヘッダー
#include"Game/Scenes/EditorScene.h"
#include "Game/Scenes/ParticleScene.h"
#include "Game/Scenes/TitleScene.h"   // ゲームループのタイトル画面
#include "Game/Scenes/SceneLoading.h" // 非同期ロード用のローディングシーン


#include "Engine/Serialization/Serializer/SceneSerializer.h"
#include "Engine/Serialization/Registry/ComponentRegistry.h"

#include "Engine/ECS/Core/CCL_World.h"

// ★ ロード前検証（破壊前のファイル存在確認）に使用
#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Platform/Core/Logger.h"


// タグとシステム
#include "Engine/Core/Components/PersistentTag.h"
#include "Engine/Transform/Systems/TransformUpdateSystem.h"
#include "Engine/Transform/Systems/HierarchyCleanupSystem.h"
#include "Engine/Physics/Systems/Jolt/JoltCleanupSystem.h"

 
#include "tracy/Tracy.hpp"


using namespace CCL::ECS;

//初期化
void SceneManager::Initialize()
{
	ZoneScopedN("SceneManager::Initialize");

	//シーン登録
	//SceneFactory::Instance().Register("ModelViewer", []() { return new ModelViewerScene(); });
	SceneFactory::Instance().Register("Editor", []() { return new EditorScene(); });

	SceneFactory::Instance().Register("Particle", []() { return new ParticleScene(); });

	// ゲームループ用シーン
	SceneFactory::Instance().Register("Title", []() { return new TitleScene(); });
	// "Game" = 実プレイ。SpawnGameTest の重複を避けるため既存のゲーム世界を再利用する。
	SceneFactory::Instance().Register("Game", []() { return new EditorScene(); });


	// 最初に表示するシーン（生成予約のみ。実体の重い初期化は初回 Update で走る）
	// エディタ開発時は従来通りゲーム世界を直接開く。製品ビルドはタイトルから開始する。
#ifdef ENABLE_EDITOR
	ChangeScene("Editor");
#else
	ChangeScene("Title");
#endif
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
	ZoneScopedN("SceneManager::Update");

	if (nextScene != nullptr)
	{
		// ★ シーン切替フレーム（起動直後もここ）。重い初期化が集中するので個別に計測する。
		ZoneScopedN("SceneManager: Scene Switch");

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
            {
                ZoneScopedN("Scene::Initialize (heavy load)");
                currentScene->Initialize();
            }

			// 終わった瞬間にマネージャーが責任を持ってフラグを立てる
            currentScene->SetReady();
        }

		// 【2段階目】: GPUへのセットアップ (Start)
        // これは絶対にメインスレッドのこのタイミングで1回だけ呼ばれる。
        // dc->Map などGPUリソースのバインドはここで行う。
        {
            ZoneScopedN("Scene::Start (GPU setup)");
            currentScene->Start();
        }
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


// =========================================================================
// ECSシーンの保存 (Save)
// =========================================================================
void SceneManager::SaveScene(CCL::ECS::World* world, const std::string& filepath)
{
	// 1. 写真撮影前の「世界の整理」
	// 今溜まっている RequestDestroy などのペンディング操作を完全に終わらせ、
	// 中途半端な半死半生のエンティティがセーブデータに混ざるのを防ぐ
	world->ScrutinyAndApply();

	// 2. 翻訳家（シリアライザ）に現在の状態をJSONに書き出させる
	SceneSerializer::Serialize(world, filepath);
}

void SceneManager::LoadScene(CCL::ECS::World* world, const std::string& filepath)
{
    // ==========================================================
    // 0. 事前検証 (★破壊する前にロード可能か確かめる)
    //   本関数は「全破壊 → デシリアライズ」の順で動く。
    //   ファイルが欠損/空だと破壊だけ実行され、ワールドが丸ごと消滅する。
    //   （Play-In-Editor で Play 前に Stop を押し、バックアップ未作成のまま
    //     復元しようとして全エンティティが消える事故の直接原因。）
    //   ここで読めないと判明したら、現在の世界には一切手を付けずに中止する。
    // ==========================================================
    const std::string jsonStr = FileSystem::ReadAllText(filepath);
    if (jsonStr.empty()) {
        CCL_LOG_ERROR(LogCategory::Core,
            "[SceneManager] LoadScene 中止: ファイルが存在しないか空です (%s)。"
            "現在のワールドは破壊しません。", filepath.c_str());
        return;
    }

    // ==========================================================
    // 1. 破壊の宣告 (Marking)
    // ==========================================================
    auto& chunks = world->GetChunkManager().GetChunks();
    for (auto& chunkPtr : chunks) {
        if (!chunkPtr) continue;
        auto* chunk = chunkPtr.get();

        // PersistentTag（シーンを跨ぐシステム等）は絶対に破壊しない
        if (chunk->HasComponent<Tag::PersistentTag>()) continue;

        for (size_t i = 0; i < chunk->GetEntityCount(); ++i) {
            CCL::ECS::EntityID entity = chunk->GetEntityByIndex(i);
            if (entity != CCL::ECS::InvalidEntityID) {
                // 階層ごと安全に削除予約（DestroyTag を付与）
                TransformUpdateSystem::MarkForDestruction(*world, entity, true);
            }
        }
    }

    // ==========================================================
    // 2. 同期的な解体作業 (Synchronous Cleanup)
    // ここでシステムを直接呼ぶことで、1フレーム待たずに「今すぐ」物理空間とメモリを消し去る
    // ==========================================================

    // ① Joltの剛体を即座に消去する
    JoltCleanupSystem::ExecuteCleanup(*world);

    // ② ECSの階層を切り離し、RequestDestroy を発行する
    HierarchyCleanupSystem::ExecuteCleanup(*world);

    // ③ 世界に溜まった RequestDestroy を「今すぐ確定」させる
    world->ScrutinyAndApply();




    // ==========================================================
    // 3. シーン構築
    //   ★ 旧版にあった「手動 ID リマップ」ブロックを削除。
    //
    //   削除した理由:
    //     - SerializationContext::GetIdMap() はそもそも実装されていなかった仮コード
    //     - 新版 SceneSerializer::Deserialize は内部で LoadContext を使い、
    //       EntityLink フィールドを Pass 2 時点で自動解決する
    //     - 外から remap を呼ぶ必要がなくなった
    //
    // ==========================================================
    SceneSerializer::Deserialize(world, filepath);

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


// 非同期シーン切り替え
void SceneManager::ChangeSceneAsync(const std::string& name)
{
    // 対象シーンを生成（この時点では Initialize は走らせない＝軽い）
    Scene* target = SceneFactory::Instance().Create(name);
    if (!target) return;

    // 表示上のシーン名は「最終的に遷移する先」を指しておく
    _currentSceneName = name;

    // ローディング画面で包む。target の重い Initialize() は
    // SceneLoading がワーカースレッドで裏読みし、完了後に本線へ切り替える。
    ChangeScene(new SceneLoading(target));
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