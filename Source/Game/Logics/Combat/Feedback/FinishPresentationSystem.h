#pragma once
// ===========================================================================
// 概要: とどめで HP の線を越えた瞬間の演出（フィニッシュ演出）を進める System
//
// 責務:
//   - FinishBlowEvent を受けて、世界をスローに保ち、フィニッシュのカメラを流す
//   - スローが明けたら、ボスの形態に合わせて移行ムービーか撃破ムービーへ移る
//
// 備考: 形態を変えるのは BossPhaseSystem で、ここは見せ方だけを持つ
//       ムービーを選ぶ頃には、BossPhaseSystem が Transition か Dead を決め終えている
//       スローを保つ長さは、フィニッシュのカメラの台本（Finisher.json）の長さで決まる
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Character/Boss/Components/BossPhaseComponent.h"

struct CameraShot;

class FinishPresentationSystem : public CCL::ECS::IfSystem<
    FinishPresentationSystem,
    CCL::ECS::Read<BossPhaseComponent>>
{
public:
    FinishPresentationSystem() : IfSystem("FinishPresentationSystem") {}

    void Initialize() override;
    void Update(float dt) override;

private:
    // 演出を始め、フィニッシュのカメラを頼む。演出中に届いたとどめと、ボスでない相手へのとどめは捨てる
    void Begin(CCL::ECS::EntityID targetID);

    // フィニッシュのカメラを流し、スローを保つ長さ（実時間の秒）を返す
    // 長さは台本で決まる。最初のショットは、今の画面のカメラから回り込み始める
    float PlayFinisherCamera();

    // Orbit のショットの始まりを、今の画面のカメラの位置・向き・画角に合わせる
    // Orbit でないときと、基準や画面のカメラが見つからないときは台本のまま
    void MatchShotStartToScreen(CameraShot &shot) const;

    // ボスの状態から流すムービーの台本を選ぶ
    const char *SelectMovie(const BossPhaseComponent &phase) const;

    CCL::ECS::EntityID m_pendingTarget = CCL::ECS::InvalidEntityID; // 届いて、まだ始めていないとどめの受けた側
    CCL::ECS::EntityID m_target        = CCL::ECS::InvalidEntityID; // 演出中のとどめの受けた側
    float              m_holdTimer     = 0.0f;                      // スローを保つ残り（実時間の秒）
};
