#pragma once
// ===========================================================================
// 概要: ムービーの再生を進め、ムービー用カメラの姿勢と優先度を VirtualCamera へ書く System。
//
// 責務:
//   - 台本フォルダの JSON を起動時にすべて読んでおく
//   - 再生要求（CinematicPlayRequestEvent）で指された台本を、指された位置（既定は頭）から流す。開始待ち・再生中に届いたものは捨てる
//   - 開始待ちと再生位置を実時間で進め、長さに達したか長押しでスキップされたら終えて知らせる
//   - 開始待ち・再生中は姿勢と画角を書き、優先度を上げてカメラを掌握する。それ以外は優先度を下げて譲る
//   - ショットの基準にする対象を名前で探し、その位置と向きからカメラの位置を決める
//   - ムービーの開始待ち・再生中の間、GameFlowState::cinematic を立てる
//   - 再生していない間、エディタが指した台本と時刻の絵を見せる（プレビュー）
//   - エディタが台本を保存したら、台本を読み直す
//   - ショットの被写界深度を、DepthOfFieldConfigComponent の上書きスロットへ書く
//
// 備考:
//   どのカメラを映すかを決め、切り替えをブレンドするのは CameraBrainSystem（R01b）。
//   この System は優先度を上げ下げするだけで、Brain に選ばれる側に回る。
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

class CinematicCameraSystem : public CCL::ECS::IfSystem<CinematicCameraSystem,
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

    /**
     * @brief 読み取る Component 型をスケジューラへ申告する。
     *
     * ショットの基準にする対象（プレイヤーやボス）の名前と位置を GetComponent / ViewEach で読む。
     * ForEach のフィルタには載らないので、ここで足しておかないと、
     * それらを書く System と並列に走ってしまう。
     */
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

    /**
     * @brief 今の再生位置の絵（姿勢と画角）をカメラへ書く。
     * @param vcam 書き込む先のカメラ。
     * @param cine 再生状態。ショットが替わった瞬間なら、補間元と基準もここで決める。
     * @return 書けたら true。ショットが無い・基準が見つからないときは false で、カメラは書き換えない。
     */
    bool WriteShotPose(VirtualCamera &vcam, CinematicCameraComponent &cine) const;

    /**
     * @brief ショットに入った瞬間に、基準にする対象とその向きを決める。
     * @param cine 書き込む先（shotSubject / shotYaw）。
     * @param shot 入ったショット。
     */
    void EnterShot(CinematicCameraComponent &cine, const CameraShot &shot) const;

    /**
     * @brief 今のショットの基準の位置（ワールド、m）を求める。
     * @param cine    今のショットの基準（shotSubject）を持つ再生状態。
     * @param shot    今のショット。
     * @param outPos  基準の位置。subject が空ならワールドの原点。
     * @return 基準の対象が見つからない・消えたときは false。
     */
    bool ResolveSubjectPosition(const CinematicCameraComponent &cine, const CameraShot &shot,
                                DirectX::SimpleMath::Vector3 &outPos) const;

    /**
     * @brief エディタが指した台本と時刻の絵を、カメラへ書く（プレビュー）。
     * @param vcam 書き込む先のカメラ。
     * @param cine 見せる台本と時刻（previewScript / previewTime）を持つ再生状態。再生の状態は書き換えず、被写界深度（dof）だけ書く。
     * @return 書けたら true。ショットが無い・基準が見つからないときは false で、カメラは書き換えない。
     */
    bool WritePreviewPose(VirtualCamera &vcam, CinematicCameraComponent &cine) const;

    /**
     * @brief 被写界深度の効きを目標へ寄せ、上書きスロットへ書く。効きが 0 になったらスロットを手放す。
     * @param cine     書く被写界深度（dof）と効き（dofWeight）を持つ再生状態。
     * @param target   効きの目標（0 か 1）。
     * @param fadeTime 目標まで寄せる時間（秒）。0 ならすぐ。
     * @param dt       経過時間（秒、実時間）。
     */
    void UpdateDof(CinematicCameraComponent &cine, float target, float fadeTime, float dt) const;

    // 台本のショットに、ぼかすものが1つでもあるか。無ければ被写界深度に触らず、シーンの設定のままにする
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
