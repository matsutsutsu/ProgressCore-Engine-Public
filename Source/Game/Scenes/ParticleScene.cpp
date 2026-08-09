#include "ParticleScene.h"
#include "Engine/Scene/SceneFactory.h" // REGISTER_SCENE（ファイル末尾で自己登録）
#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Serialization/Serializer/LevelSerializer.h" // レベル読み込み

#include "Engine/ECS/System/CCL_SystemRegistry.h"


void ParticleScene::Initialize()
{
    BaseScene::Initialize();

        // --- 戦場レベルを読み込む ---
    // LoadLevel は内部で LoadScene（既存ワールドをPersistentTag以外全消去）→ ステージ復元
    // → プレハブ実体(ライト/カメラ/プレイヤー/ボス/ステージ)を再生成する。BaseScene::Initialize は
    // ゲーム実体を生成しないので、ここで全消去されて困るものは無い（リソース/システムは残る）。
    {
        // 起動時に読み込む戦場レベル。ライト/カメラ/プレイヤー/ボス/ステージを全て含む。
        std::string particleLevel = "Assets/Scene/ParticleScene.level.json";


        ZoneScopedN("ParticleScene: LoadLevel ParticleScene");
        if (!LevelSerializer::LoadLevel(m_worldPtr, particleLevel))
            CCL_LOG_ERROR(LogCategory::Core, "ParticleScene: パーティクルシーンレベルの読み込みに失敗: %s", particleLevel.c_str());
    }
  
}

void ParticleScene::Finalize()
{
    BaseScene::Finalize(); 
}

void ParticleScene::RegisterSystems()
{
    // EditorSceneと全く同じ (SystemGroupsのおかげで短縮)
    ID3D11Device *device = Graphics::Instance().GetDevice();

    // SystemManagerへのアクセスを m_ecsContext 経由に変更
    // (SystemGroupsの関数が参照を受け取る仕様であると推測されるため、ポインタをデリファレンスしています)
    auto &sysManager = *(m_ecsContext->GetSystemManager());
    CCL::ECS::SystemRegistry::Instance().RegisterAll(sysManager);
}

// SceneFactory への自動登録（SceneManager が本クラスを include せずに済むようにする）
REGISTER_SCENE(ParticleScene, "Particle")
