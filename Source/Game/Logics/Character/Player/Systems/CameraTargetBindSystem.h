#pragma once
/// @file CameraTargetBindSystem.h
/// @brief カメラの追従/注視ターゲットを「役割」で解決して繋ぎ直すシステム。
///        追従カメラ→PlayerComponent、導入演出カメラ→BossPhaseComponent。
///
/// なぜゲーム側にあるか:
///   「どのエンティティがプレイヤーか」はゲームの知識であり、Engine のカメラが
///   PlayerComponent を知っていると Engine→Game 依存になる。そこで解決役を
///   ゲーム側に置き、Engine のカメラコンポーネント(target)へ push する。
///
/// なぜ必要か:
///   ハードコードした EntityID ではなく役割で繋ぐため、プレハブ/レベルで
///   プレイヤーとカメラを別々に配置しても、save/load やリスポーンで ID が
///   変わっても、実行時に自動でカメラ target をプレイヤーへ繋ぎ直せる。
///
/// 実行ステージ:
///   R00b_CameraTargetBind(75)。カメラ本体(R01_Camera=100)より確実に前へ置く。
///   移管前も同じ「カメラ更新の直前」に解決していたため、タイミングは不変。
///   ※ Logic ステージに置くと FixedUpdate が走らないフレームで解決が遅れるため、
///     あえて Render ステージのカメラ直前に置いている。

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Camera/Components/VirtualCameraComponents.h"
#include "Engine/Serialization/Core/EntityLink.h"

class CameraTargetBindSystem
    : public CCL::ECS::IfSystem<CameraTargetBindSystem,
          CCL::ECS::Read<VirtualCamera>>
{
  public:
    CameraTargetBindSystem()
        : IfSystem("CameraTargetBindSystem")
    {
    }
    virtual ~CameraTargetBindSystem() = default;

    void Update(float dt) override;

  private:
    /// PlayerComponent を持つ最初のエンティティ(=プレイヤー)を役割で探す。
    CCL::ECS::EntityID FindPlayer();

    /// BossPhaseComponent を持つ最初のエンティティ(=ボス)を役割で探す。
    /// 導入カメラ演出がボスを注視対象にするために使う。ボスが居ないシーンでは
    /// InvalidEntityID が返り、演出システム側が安全に演出を打ち切る。
    CCL::ECS::EntityID FindBoss();

    /// target が無効(未接続 / リロードでID変化 / リスポーン)なら player へ繋ぎ直す。
    /// 有効な相手を指している場合(手動設定など)は尊重して上書きしない。
    void BindIfInvalid(CCL::Serialization::EntityLink &target, CCL::ECS::EntityID player);
};
