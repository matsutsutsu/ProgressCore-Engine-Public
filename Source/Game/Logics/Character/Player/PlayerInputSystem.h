
// inputAPIを使用していためコメントアウト

//#pragma once
//#include "Engine/ECS/System/CCL_System.h"                  // IfSystemの継承元
//
//// 前方宣言 (インクルード削減のため)
//class Camera;
//struct PlayerComponent;
//
//class PlayerInputSystem
//    : public CCL::ECS::IfSystem<PlayerInputSystem, CCL::ECS::Write<PlayerComponent>> {
//  public:
//    // コンストラクタ
//    PlayerInputSystem();
//    virtual ~PlayerInputSystem() = default;
//
//    // 更新処理
//    void Update(float dt) override;
//};