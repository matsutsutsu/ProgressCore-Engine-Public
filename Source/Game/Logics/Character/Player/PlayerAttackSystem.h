//#pragma once
//// ===========================================================================
//// @file   PlayerAttackSystem.h
//// @brief  プレイヤーの「入力の意思 (CharacterMovementInputComponent)」を、
////         「杖の発射状態 (WandComponent)」へ橋渡しするシステム。
////
//// @note   責務の分離:
////         PlayerInputSystem   → ハードウェアを読んで isAttackRequested / lookTargetPos を書く
////         PlayerAttackSystem  → それを読んで WandComponent.fireRequested / aimDirection を書く (本システム)
////         WandFireSystem      → WandComponent を読んで弾エンティティを生成する
////
////         本システムはデバイスにも World 構造変更にも触れない。
////         純粋な「データ変換器」として機能するため ForEachParallel に昇格可能だが、
////         プレイヤー数は実質 1 体なので現状はシングルスレッドで十分。
//// ===========================================================================
//
//#include "Engine/ECS/System/CCL_IfSystem.h"
//#include "Engine/Transform/Components/TransformComponent.h"           // @note 実パスに調整
//#include "Engine/Physics/Components/Character/CharacterMovementInputComponent.h" // @note 実パスに調整
//#include "Game/Logics/Character/Player/PlayerComponent.h"
//#include "Game/Logics/Wand/RF_WandComponents.h"                       // @note Wand/ フォルダのパスに調整
//
//
///**
// * @brief 入力の意思を杖コンポーネントへ転送するシステム。
// *
// * @note  テンプレート引数の順序 = ForEach ラムダ引数の順序（厳守）。
// *        フィルタは「WandComponent + CharacterMovementInputComponent
// *                   + TransformComponent + PlayerComponent を全て持つ」エンティティ。
// *        つまりプレイヤーエンティティにこの 4 種が揃っていることが前提。
// */
//class PlayerAttackSystem
//    : public CCL::ECS::IfSystem<
//    PlayerAttackSystem,
//    CCL::ECS::Write<RuneForge::WandComponent>,
//    CCL::ECS::Read<CharacterMovementInputComponent>,
//    CCL::ECS::Read<TransformComponent>,
//    CCL::ECS::Read<PlayerComponent>>
//{
//public:
//    /**
//     * @brief コンストラクタ。DeadTag を持つエンティティを除外フィルタに登録する。
//     * @note  PlayerMoveSystem の ForEachWithID + HasComponent 方式より
//     *        SetExclude の方が Archetype レベルで弾くため効率的。
//     */
//    PlayerAttackSystem();
//    virtual ~PlayerAttackSystem() = default;
//
//    /**
//     * @brief 入力の意思を杖へ転送する。
//     * @param dt 未使用（時間依存の計算なし）。rawDt / localDt どちらも不要。
//     */
//    void Update(float dt) override;
//};