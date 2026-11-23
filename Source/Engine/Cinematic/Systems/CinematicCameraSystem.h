#pragma once
// ===========================================================================
// 概要: ムービーの再生を進め、ムービー用カメラの姿勢と優先度を VirtualCamera へ書く System
//
// 責務:
//   - 再生要求・停止要求を受け、再生位置を実時間で進める。長押しでスキップする
//   - 再生中は優先度を上げて画面を握り、ショットの絵と被写界深度を書く。GameFlowState::cinematic を立てる
//   - 再生していない間は、エディタが指した時刻の絵を出す（プレビュー）
//
// 備考: どのカメラを映すかと、カメラ同士の切り替えのブレンドは CameraBrainSystem（R01b）が決める
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Camera/Components/VirtualCameraComponents.h"
#include "Engine/Cinematic/Components/CinematicCameraComponent.h"
#include "Engine/ECS/Common/CCL_TypeInfo.h"
#include "Engine/Core/Components/NameComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Graphics/Components/PostProcess/DepthOfFieldConfigComponent.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class CinematicCameraSystem : public CCL::ECS::IfSystem<CinematicCameraSystem,
                                                        CCL::ECS::Write<VirtualCamera>,
                                                        CCL::ECS::Write<CinematicCameraComponent>>
{
public:
    CinematicCameraSystem() : IfSystem("CinematicCameraSystem") {}

    void Initialize() override;

    void Update(float dt) override;

    std::vector<CCL::ECS::TypeID> GetReadTypes() const override
    {
        auto types = IfSystem::GetReadTypes();
        types.push_back(CCL::ECS::TypeInfo<NameComponent>::ID());      // ショットの基準を名前で探す
        types.push_back(CCL::ECS::TypeInfo<TransformComponent>::ID()); // 基準の位置と向きを読む
        return types;
    }

    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override
    {
        auto types = IfSystem::GetWriteTypes();
        types.push_back(CCL::ECS::TypeInfo<DepthOfFieldConfigComponent>::ID()); // 被写界深度の上書きスロット
        return types;
    }

private:
    // 届いた要求。購読で書き、Update で取り出す
    struct Request
    {
        bool                                   play      = false; // 再生要求があるか
        bool                                   stop      = false; // 停止要求があるか
        bool                                   reload    = false; // 台本を読み直すか
        std::string                            path;              // 流す台本のパス
        float                                  startTime = 0.0f;  // 再生を始める位置（秒）
        std::shared_ptr<const CinematicScript> script;            // 直接渡された台本。なければ path で探す
    };

    // 届いた要求を取り出して空にする
    Request TakeRequest();

    // 再生要求を受け付け、台本を写して流し始める。再生中か、台本がなければ捨てる
    void AcceptRequest(CinematicCameraComponent &cine, const Request &request) const;

    // 再生していない間の処理。プレビューを出すか、優先度を下げて被写界深度を消していく
    void UpdateIdle(VirtualCamera &vcam, CinematicCameraComponent &cine, float dt) const;

    // 今の再生位置の絵をカメラへ書く。基準が見つからなければ false
    bool WriteShotPose(VirtualCamera &vcam, CinematicCameraComponent &cine) const;

    // エディタが指した台本と時刻の絵をカメラへ書く。基準が見つからなければ false
    bool WritePreviewPose(VirtualCamera &vcam, CinematicCameraComponent &cine) const;

    // ショットに入った瞬間に、基準の Entity と向きを決める。向きはショットの間変えない
    void EnterShot(CinematicCameraComponent &cine, const CameraShot &shot) const;

    // 被写界深度の効きを target へ fadeTime 秒で寄せ、上書きスロットへ書く。効きが 0 になったら手放す
    void UpdateDof(CinematicCameraComponent &cine, float target, float fadeTime, float dt) const;

    // ぼかすショットがあるか。なければ被写界深度に触らず、シーンの設定のままにする
    static bool UsesDof(const CinematicScript &script);

    // スキップボタンを押しているか
    bool ReadSkipHeld() const;

    // 台本フォルダの JSON をすべて読み直す
    void LoadAllScripts();

    std::map<std::string, CinematicScript> m_scripts; // 読み込んだ台本（キーはパス）

    std::mutex m_requestMutex; // m_pending を守る。要求は並列に走る System からも届く
    Request    m_pending;      // まだ取り出していない要求
};
