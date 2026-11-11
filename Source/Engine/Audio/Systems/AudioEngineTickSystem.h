#pragma once
// ===========================================================================
// 概要: IAudioAPI::Update を1描画フレームに1回だけ呼ぶ System（R11_AudioTick）
//
// 備考: ロジックの段で呼ばない。ロジックは1描画フレームに最大4回走るが、
//       DXTK の AudioEngine::Update は毎フレーム1回を前提にしている
//       発音の指示とリスナーの位置がそろってから当てるので、描画の段の最後に置く
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"

class AudioEngineTickSystem : public CCL::ECS::SystemBase
{
public:
    AudioEngineTickSystem() : SystemBase("AudioEngineTickSystem") {}
    virtual ~AudioEngineTickSystem() = default;

    std::vector<CCL::ECS::TypeID> GetReadTypes() const override { return {}; }
    void                          Update(float dt) override;
};
