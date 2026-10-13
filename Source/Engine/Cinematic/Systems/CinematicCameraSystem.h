#pragma once
// ===========================================================================
// 概要: ムービーの再生を進め、ムービー用カメラの姿勢と優先度を VirtualCamera へ書く System。
//
// 責務:
//   - 台本フォルダの JSON を起動時にすべて読んでおく
//   - 再生要求（CinematicPlayRequestEvent）で指された台本を流す。開始待ち・再生中に届いたものは捨てる
//   - 開始待ちと再生位置を実時間で進め、長さに達したか長押しでスキップされたら終えて知らせる
//   - 開始待ち・再生中は姿勢と画角を書き、優先度を上げてカメラを掌握する。それ以外は優先度を下げて譲る
//   - ショットの基準にする対象を名前で探し、その位置と向きからカメラの位置を決める
//   - ムービーの開始待ち・再生中の間、GameFlowState::cinematic を立てる
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
     * @brief 名前（NameComponent）から Entity を探す。
     * @param name 探す名前。
     * @return 最初に見つかった Entity。見つからなければ InvalidEntityID。
     */
    CCL::ECS::EntityID FindEntityByName(const std::string &name) const;

    /**
     * @brief 今のショットの基準の位置（ワールド、m）を求める。
     * @param cine    今のショットの基準（shotSubject）を持つ再生状態。
     * @param shot    今のショット。
     * @param outPos  基準の位置。subject が空ならワールドの原点。
     * @return 基準の対象が見つからない・消えたときは false。
     */
    bool ResolveSubjectPosition(const CinematicCameraComponent &cine, const CameraShot &shot,
                                DirectX::SimpleMath::Vector3 &outPos) const;

    // 台本フォルダの JSON をすべて読み直す。読めなかった台本はログを出して外す
    void LoadAllScripts();

    std::map<std::string, CinematicScript> m_scripts; // 読み込んだ台本（キーはパス）

    // 届いた再生要求。購読で入れ、Update で取り出して空にする
    std::mutex  m_requestMutex;       // m_pendingPath と m_hasRequest を守る
    std::string m_pendingPath;        // 流す台本のパス
    bool        m_hasRequest = false; // 取り出していない再生要求があるか

    std::string m_guiScriptPath; // デバッグ UI で選んでいる台本のパス
};
