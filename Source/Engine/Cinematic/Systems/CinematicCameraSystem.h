#pragma once
// ===========================================================================
// 概要: ムービーの再生を進め、ムービー用カメラの姿勢と優先度を VirtualCamera へ書く System
//
// 責務:
//   - 起動時に台本フォルダの JSON をすべて読み、エディタが保存したら読み直す
//   - 再生要求で指された台本を流し、長さに達するか長押しでスキップされたら終えて知らせる
//   - 開始待ちと再生中は、姿勢と画角を書いて優先度を上げ、GameFlowState::cinematic を立てる
//   - 再生していない間は、エディタが指した台本と時刻の絵を見せる（プレビュー）
//   - ショットの被写界深度を、DepthOfFieldConfigComponent の上書きスロットへ書く
//
// 備考: どのカメラを映すかとブレンドは CameraBrainSystem（R01b）が決める。ここは優先度を上げ下げするだけ
//       開始待ちと再生位置は実時間で進める。開始待ち・再生中に届いた要求は捨てる
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Camera/Components/VirtualCameraComponents.h"
#include "Engine/Cinematic/Components/CinematicCameraComponent.h"
#include "Engine/ECS/Common/CCL_TypeInfo.h"
#include "Engine/Core/Components/NameComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Graphics/Components/PostProcess/DepthOfFieldConfigComponent.h"

#include <SimpleMath.h>
#include <map>
#include <mutex>
#include <string>

class CinematicCameraSystem : public CCL::ECS::IfSystem<
    CinematicCameraSystem,
    CCL::ECS::Write<VirtualCamera>,
    CCL::ECS::Write<CinematicCameraComponent>>
{
public:
    CinematicCameraSystem() : IfSystem("CinematicCameraSystem") {}

    // 台本をすべて読み、再生要求の購読を登録する
    void Initialize() override;

    void Update(float dt) override;

    // 再生の状態を表示し、台本を選んで再生要求を出す・台本を読み直すテスト用ボタンを置く
    void OnGui() override;

    // ショットの基準にする対象の名前と位置を GetComponent / ViewEach で読むので申告する
    // 申告しないと、それらを書く System と並列に走ってしまう
    std::vector<CCL::ECS::TypeID> GetReadTypes() const override
    {
        auto types = IfSystem::GetReadTypes();
        types.push_back(CCL::ECS::TypeInfo<NameComponent>::ID());
        types.push_back(CCL::ECS::TypeInfo<TransformComponent>::ID());
        return types;
    }

    // 被写界深度の上書きスロットを GetSingletonComponent で書く。ForEach のフィルタには載らないので、ここで申告する
    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override
    {
        auto types = IfSystem::GetWriteTypes();
        types.push_back(CCL::ECS::TypeInfo<DepthOfFieldConfigComponent>::ID());
        return types;
    }

private:
    // スキップボタンを今押しているか。エディタの操作中は false
    bool ReadSkipHeld() const;

    // 今の再生位置の絵（姿勢と画角）をカメラへ書く。ショットが替わった瞬間なら、補間元と基準もここで決める
    // ショットがない・基準が見つからないときは false を返し、カメラは書き換えない
    bool WriteShotPose(VirtualCamera &vcam, CinematicCameraComponent &cine) const;

    // ショットに入った瞬間に、基準にする対象とその向き（shotSubject / shotYaw）を決める
    void EnterShot(CinematicCameraComponent &cine, const CameraShot &shot) const;

    // 今のショットの基準の位置（ワールド、m）を求める。subject が空ならワールドの原点
    // 基準の対象が見つからない・消えたときは false
    bool ResolveSubjectPosition(const CinematicCameraComponent &cine, const CameraShot &shot,
                                DirectX::SimpleMath::Vector3 &outPos) const;

    // エディタが指した台本と時刻の絵を、カメラへ書く（プレビュー）。再生の状態は変えず、被写界深度だけ書く
    // ショットがない・基準が見つからないときは false を返し、カメラは書き換えない
    bool WritePreviewPose(VirtualCamera &vcam, CinematicCameraComponent &cine) const;

    // 被写界深度の効きを target（0 か 1）へ fadeTime 秒で寄せ、上書きスロットへ書く。効きが 0 になったら手放す
    void UpdateDof(CinematicCameraComponent &cine, float target, float fadeTime, float dt) const;

    // 台本のショットに、ぼかすものが1つでもあるか。なければ被写界深度に触らず、シーンの設定のままにする
    static bool UsesDof(const CinematicScript &script);

    // 台本フォルダの JSON をすべて読み直す。読めなかった台本はログを出して外す
    void LoadAllScripts();

    std::map<std::string, CinematicScript> m_scripts; // 読み込んだ台本（キーはパス）

    // 届いた再生要求。購読で入れ、Update で取り出して空にする
    std::mutex  m_requestMutex;           // m_pendingPath・m_pendingStartTime・m_hasRequest・m_reloadRequested を守る
    std::string m_pendingPath;              // 流す台本のパス
    float       m_pendingStartTime = 0.0f;  // 再生を始める位置（秒）
    bool        m_hasRequest       = false; // 取り出していない再生要求があるか
    bool        m_reloadRequested  = false; // 台本のファイルが書き換わったので、読み直すか

    std::string m_guiScriptPath; // デバッグ UI で選んでいる台本のパス
};
