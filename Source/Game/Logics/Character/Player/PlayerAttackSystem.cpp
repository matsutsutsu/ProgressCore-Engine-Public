//// ===========================================================================
//// @file   PlayerAttackSystem.cpp
//// @brief  PlayerAttackSystem の実装。
////
//// @note   Why: L01_Input 内で PlayerInputSystem (Write<CMIC>) の後に実行する。
////         タスクスケジューラが「Write<CMIC> → Read<CMIC>」の型競合を検出し、
////         PlayerInputSystem より後のバッチに自動配置する。依存関係は明示的な
////         型宣言だけで解決できるため、手動での順序指定は不要。
////
////         本システムは構造変更（Spawn/Destroy）を一切行わないため、
////         PendingOps を触らず _world の参照すら必要としない。
////         「データを読んで別のコンポーネントに書くだけ」という
////         最も軽量なシステムの実装例でもある。
//// ===========================================================================
//
//#include "PlayerAttackSystem.h"
//#include "Game/Logics/Combat/DeadTag.h"                    // @note SetExclude<Tag::DeadTag>() 用
//#include "Engine/ECS/System/CCL_SystemRegistry.h"          // @note REGISTER_LOGIC_SYSTEM
//#include "Engine/Core/Systems/SystemPriority.h"
//
//using namespace CCL::ECS;
//using namespace DirectX::SimpleMath;
//using namespace RuneForge;
//
//
//// ---------------------------------------------------------------------------
//// Constructor
//// ---------------------------------------------------------------------------
//PlayerAttackSystem::PlayerAttackSystem()
//    : IfSystem("PlayerAttackSystem")
//{
//    SetExclude<Tag::DeadTag>();
//}
//
//
//// ---------------------------------------------------------------------------
//// Update
//// ---------------------------------------------------------------------------
//void PlayerAttackSystem::Update(float /*dt*/)
//{
//    // ※ 構造変更なし・Resource アクセスなし → ForEach（ラムダ内で _world 不要）
//    ForEach([](WandComponent& wand,
//        const CharacterMovementInputComponent& input,
//        const TransformComponent& xform,
//        const PlayerComponent&                 /*player*/)
//        {
//            // ------------------------------------------------------------------
//            // 1. 発射トリガーの転送
//            //    InputSystem が毎フレーム input.isAttackRequested を上書きするため、
//            //    こちらも毎フレーム同期する（フレーム遅れなし）。
//            // ------------------------------------------------------------------
//            wand.fireRequested = input.isAttackRequested;
//
//            // ------------------------------------------------------------------
//            // 2. 照準方向の計算
//            //    プレイヤー位置 → マウスのワールド座標（Y = 0 平面）方向ベクトル。
//            //    PlayerMoveSystem の振り向きロジックと同じ XZ 平面射影を使う。
//            // ------------------------------------------------------------------
//            Vector3 toTarget = input.lookTargetPos - xform.position;
//            toTarget.y = 0.0f; // トップビュー: 高さ方向の差は無視
//
//            if (toTarget.LengthSquared() > 1e-6f)
//            {
//                toTarget.Normalize();
//                wand.aimDirection = toTarget;
//                // aimDirection が安定していれば最後に向いていた方向を保持するため
//                // else ブランチは書かない（Vector3::Zero を上書きしない）。
//            }
//
//            // ------------------------------------------------------------------
//            // 3. 銃口オフセットは本システムでは触らない。
//            //    muzzleOffset はエンティティ生成時に一度だけ設定するコンフィグ値。
//            //    （プレイヤー設定: wand.muzzleOffset = Vector3{0,0,0} が既定）
//            //    @todo キャラクターの銃口ボーン位置が確定したら専用の
//            //          MuzzleTrackingSystem で毎フレーム更新する。
//            // ------------------------------------------------------------------
//        });
//}
//
//
//// ---------------------------------------------------------------------------
//// システム登録
//// ---------------------------------------------------------------------------
//// L01_Input に配置する理由:
////   PlayerInputSystem (L01, Write<CMIC>) の後、
////   WandFireSystem    (L02, Read<WandComponent>) の前、に実行したい。
////   本システムは Read<CMIC> を持つため、スケジューラが自動で
////   PlayerInputSystem の後ろのバッチに配置する。
//REGISTER_LOGIC_SYSTEM(PlayerAttackSystem, Priority::LogicStage::L01_Input);