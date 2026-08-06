#include"SceneManager.h"
#include"SceneFactory.h"

// 具体シーンは各 .cpp の REGISTER_SCENE で SceneFactory へ自己登録される。
// ここで include すると Engine→Game 依存になるため、名前(文字列)だけで扱う。
#include "Engine/Scene/SceneLoading.h" // 非同期ロード用のローディングシーン


#include "Engine/Serialization/Serializer/SceneSerializer.h"
#include "Engine/Serialization/Registry/ComponentRegistry.h"

#include "Engine/ECS/Core/CCL_World.h"

// フェード描画用（黒の全画面矩形）。
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Graphics/Renderer/Sprite.h"

// ロード前検証（破壊前のファイル存在確認）に使用
#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Platform/Core/Logger.h"


// タグとシステム
#include "Engine/Core/Components/PersistentTag.h"
#include "Engine/Transform/Systems/TransformUpdateSystem.h"
#include "Engine/Transform/Systems/HierarchyCleanupSystem.h"
#include "Engine/Physics/Systems/Jolt/JoltCleanupSystem.h"
#include "Engine/Audio/Systems/AudioCleanupSystem.h"

 
#include "tracy/Tracy.hpp"


using namespace CCL::ECS;

// コンストラクタ／デストラクタは、m_fadeQuad(unique_ptr<Sprite>) の破棄に
// Sprite の完全型が必要なため、Sprite を include したこの .cpp 側で定義する。
SceneManager::SceneManager() = default;
SceneManager::~SceneManager() = default;

//初期化
void SceneManager::Initialize()
{
	ZoneScopedN("SceneManager::Initialize");

	// シーン登録は各シーンの .cpp 末尾にある REGISTER_SCENE マクロが
	// 静的初期化時に済ませている（"Editor" / "Particle" / "Title" / "Game"）。
	// ここでは名前で参照するだけなので、具体シーンのヘッダは不要。

	// 最初に表示するシーン（生成予約のみ。実体の重い初期化は初回 Update で走る）。
	// 展示会用：タイトルは見せず、起動直後に GameScene(=BossBattleStage) の戦闘へ直行する。
	// （Shipping で発生していたタイトルの緑点滅もタイトルを経由しないことで回避される。）
	// 従来の編集用 EditorScene / TitleScene はメニューやコードから任意に開ける。
	ChangeScene("Game");
	//ChangeScene("Particle");

	// 起動直後は黒から表示へフェードイン（初回シーンのポップインを隠す）。
	FadeInFromBlack();
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

	// フェードを先に進める。黒に達した瞬間に「予約された切替」を発火し、
	// nextScene をセットするので、直後の切替ブロックが同じフレームで処理する。
	// （黒で完全に隠れている間に重い切替・初期化が走る＝ポップインが見えない）
	AdvanceFade(elapsedTime);

	if (nextScene != nullptr)
	{
		// シーン切替フレーム（起動直後もここ）。重い初期化が集中するので個別に計測する。
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

	// 遷移フェードの黒矩形は最前面。シーン描画（UI含む）の後に全ての上へ被せる。
	// BaseScene 側に描画を書かず、遷移の所有者であるここで一元的に描く。
	DrawFade();
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

	// SceneManager 自身は Meyer's Singleton（static ローカル変数）のため、
	// このインスタンスの実体の破棄は wWinMain リターン後の静的破棄タイミングまで
	// 起きない＝Graphics::Shutdown()（デバイス破棄・リークレポート）より後になる。
	// m_fadeQuad はここで明示的に解放しておかないと、GraphicsDevice::Shutdown()
	// の ReportLiveDeviceObjects で「真のリーク」として誤検出されてしまう。
	m_fadeQuad.reset();
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
    // 0. 事前検証 (破壊する前にロード可能か確かめる)
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

    // 破棄されるエンティティを即時適用
    // DestroyTagを即時に適用することで下のシステムのCleanupも作動するため
    world->ScrutinyAndApply();

    // ① Joltの剛体を即座に消去する
    JoltCleanupSystem::ExecuteCleanup(*world);

    // ①-b 破棄される AudioEmitter の再生中サウンドを止める（BGM等が遷移後も鳴り続けるのを防ぐ）
    AudioCleanupSystem::ExecuteCleanup(*world);

    // ② ECSの階層を切り離し、RequestDestroy を発行する
    HierarchyCleanupSystem::ExecuteCleanup(*world);

    // ③ 世界に溜まった RequestDestroy を「今すぐ確定」させる
    world->ScrutinyAndApply();




    // ==========================================================
    // 3. シーン構築
    //   旧版にあった「手動 ID リマップ」ブロックを削除。
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
    m_currentSceneName = name;

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
    m_currentSceneName = name;

    // ローディング画面で包む。target の重い Initialize() は
    // SceneLoading がワーカースレッドで裏読みし、完了後に本線へ切り替える。
    ChangeScene(new SceneLoading(target));
}


void SceneManager::ChangeScene(Scene *scene)
{
    // ※ここでは m_currentSceneName は自動更新されない
    // 呼び出し側（GateSystem）で SetCurrentSceneName を呼んでからこれを使う
    if (nextScene != nullptr) {
        delete nextScene;
    }

    // nextSceneに直接セット
    nextScene = scene;
}


// =========================================================================
//  画面遷移フェード（SceneManager が統括）
// =========================================================================

bool SceneManager::RequestTransition(const std::string& name, bool async)
{
    if (IsTransitioning()) return false; // 遷移中の多重要求は無視

    // 「名前指定の切替」を黒の瞬間に実行するよう予約し、フェードアウト開始。
    m_pendingValid = true;
    m_pendingName  = name;
    m_pendingAsync = async;
    m_pendingScene = nullptr;
    m_fade         = FadeState::Out; // 現在の m_fadeAlpha（通常0）からアウト
    return true;
}

bool SceneManager::RequestTransition(Scene* readyScene)
{
    if (!readyScene) return false;

    // 非同期ロードの黒保持中に SceneLoading から呼ばれるケース。
    //   すでに画面は黒（alpha=1）で、ローディングも黒の裏で完了している。
    //   ここで“新たに暗転し直す”とチラつくので、暗転はせず本命へ差し替えて
    //   そのままフェードイン（黒→ゲーム）する。
    if (m_asyncHold)
    {
        ChangeScene(readyScene); // nextScene に本命をセット（次フレームで切替）
        m_asyncHold = false;
        m_fade      = FadeState::In; // alpha は 1 のまま → ゲームへ1回だけ明転
        return true;
    }

    if (IsTransitioning()) return false;

    // 通常の実体指定遷移（黒保持なし）：暗転から始める。
    m_pendingValid = true;
    m_pendingName.clear();
    m_pendingAsync = false;
    m_pendingScene = readyScene;
    m_fade         = FadeState::Out;
    return true;
}

void SceneManager::FadeInFromBlack(float dur)
{
    m_fadeInDur = (dur > 0.0001f) ? dur : 0.0001f;
    m_fadeAlpha = 1.0f;        // 黒から
    m_fade      = FadeState::In;
}

void SceneManager::AdvanceFade(float dt)
{
    switch (m_fade)
    {
    case FadeState::Out:
        m_fadeAlpha += dt / m_fadeOutDur;
        if (m_fadeAlpha >= 1.0f)
        {
            m_fadeAlpha = 1.0f;
            // 完全な黒の瞬間に、予約された切替を実行（nextScene がセットされる）。
            BeginPendingChange();
            m_fadeHoldTimer = 0.0f;
            m_fade = FadeState::Hold; // 黒を少し保持してから明転へ
        }
        break;

    case FadeState::Hold:
        // 非同期ロード中は黒のまま待機（明転しない）。ロード完了時に
        // SceneLoading が RequestTransition(Scene*) を呼び、そこで In へ移る。
        if (m_asyncHold) break;

        // 通常（同期切替）は黒を少しタメてから明転。この裏で新シーンの
        // 初回フレームが描かれ切る。
        m_fadeHoldTimer += dt;
        if (m_fadeHoldTimer >= m_fadeHoldDur)
            m_fade = FadeState::In;
        break;

    case FadeState::In:
        m_fadeAlpha -= dt / m_fadeInDur;
        if (m_fadeAlpha <= 0.0f)
        {
            m_fadeAlpha = 0.0f;
            m_fade      = FadeState::None;
        }
        break;

    case FadeState::None:
    default:
        break;
    }
}

void SceneManager::BeginPendingChange()
{
    if (!m_pendingValid) return;
    m_pendingValid = false;

    if (m_pendingScene)
    {
        // 実体指定：SceneLoading が裏読み完了した本命シーンへ切替。
        ChangeScene(m_pendingScene);
        m_pendingScene = nullptr;
    }
    else if (m_pendingAsync)
    {
        // 名前＋非同期：黒のまま裏読みする（ローディング画面は見せない）。
        // 完了まで黒を保持し、本命が用意できたら“1回だけ”フェードインする。
        ChangeSceneAsync(m_pendingName);
        m_asyncHold = true;
    }
    else
    {
        // 名前＋同期：その場で切替（重い Initialize は黒の裏で走る）。
        ChangeScene(m_pendingName);
    }
}

void SceneManager::DrawFade()
{
    if (m_fadeAlpha <= 0.001f) return; // 透明なら描かない

    auto& gfx = Graphics::Instance();
    auto* dc  = gfx.GetDeviceContext();
    if (!dc) return;

    // 半透明合成・深度無効を明示的に設定（他パスの状態に依存しない）。
    dc->OMSetBlendState(
        gfx.GetRenderState()->GetBlendState(BlendState::Transparency), nullptr, 0xffffffff);
    dc->OMSetDepthStencilState(
        gfx.GetRenderState()->GetDepthStencilState(DepthState::NoTestNoWrite), 0);

    // リニアだと明暗が一定速度で変わり「機械的」で違和感が出やすい。
    // smoothstep（両端が緩やかな S 字）で加減速をつけ、ヌルっと見せる。
    const float a = m_fadeAlpha;
    const float eased = a * a * (3.0f - 2.0f * a);

    // 黒の全画面矩形（白ダミーテクスチャに黒＋alpha を掛ける）。遅延生成。
    if (!m_fadeQuad) m_fadeQuad = std::make_unique<Sprite>(gfx.GetDevice());
    m_fadeQuad->Render(dc, 0.0f, 0.0f, 0.0f,
        gfx.GetScreenWidth(), gfx.GetScreenHeight(), 0.0f,
        0.0f, 0.0f, 0.0f, eased);
}