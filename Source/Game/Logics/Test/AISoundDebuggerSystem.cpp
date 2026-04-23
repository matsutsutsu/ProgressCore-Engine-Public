#include "AISoundDebuggerSystem.h"
#include "ECS/Core/CCL_World.h"
#include "Engine/Audio/IAudioAPI.h"
#include "Game/Logics/AI/NavAI/Events/AISoundEvent.h"
#include "SimpleMath.h"

#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

#include <imgui.h>

using namespace DirectX::SimpleMath;
using namespace CCL::ECS;

void AISoundDebuggerSystem::Update(float dt) {
    if (!_world) return;

    ImGui::Begin("AI Sound Debugger");

    // テスト用の音源座標
    static Vector3 soundPos = { 0.0f, 1.0f, 0.0f };
    ImGui::DragFloat3("Sound Source Pos", &soundPos.x, 0.5f);

    // テスト用の音量（AIが聞こえる半径）
    static float volumeRadius = 30.0f;
    ImGui::DragFloat("Volume Radius (m)", &volumeRadius, 1.0f, 0.0f, 100.0f);

    ImGui::Spacing();

    // ボタンを押すと音が発生する
    if (ImGui::Button("FIRE SOUND EVENT!", ImVec2(-1, 40))) {

        // 1. プレイヤー（自分）の耳に音を鳴らす (FMOD)
        // ※まだBankがロードされていない場合はエラーログが出るだけで無視されます。
        //if (auto audioAPI = _world->GetResource<std::shared_ptr<IAudioAPI>>()) {
        //    audioAPI->PlayEvent3D("event:/SFX/Shoot", soundPos);
        //}

        // 2. AIの脳に「音が鳴った」ことを伝達する (ECS Event)
        AISoundEvent soundEvent;
        soundEvent.position = soundPos;
        soundEvent.volumeRadius = volumeRadius;

        // エンジンのイベントシステムに波紋を投下する
        _world->GetEventBus().Publish(soundEvent);
    }

    ImGui::End();
}

// デバッグ用なのでRenderStageのUIフェーズに登録
REGISTER_RENDER_SYSTEM(AISoundDebuggerSystem, Priority::RenderStage::R10_UI)