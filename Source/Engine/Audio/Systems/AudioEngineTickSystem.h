#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"

// ===================================================================================
// 【 オーディオエンジンの1フレーム更新 : AudioEngineTickSystem 】
//
// [ 役割 ]
// IAudioAPI::Update() を1描画フレームにつき正確に1回だけ叩く。
// 3D音響の再計算、再生し終えたインスタンスの回収、スロー度の追従、
// XAudio2 のデバイスロスト復帰がここで走る。
//
// [ なぜ Logic ではなく Render なのか ]
// 以前は AudioSystem（Logic / L06_Resolution）の末尾で呼んでいた。
// しかし Logic は FixedUpdate 側で1描画フレームに最大4回まわる（Framework::MAX_STEPS）。
//   ・DXTK の AudioEngine::Update は「毎フレーム1回」を前提にしたAPI
//   ・再生中の全ボイスへピッチを掛け直すループも回数ぶん重複する
//   ・スロー度の追従が dt ではなく「呼ばれた回数」で進むため、
//     処理落ちするほどスロー演出のピッチ変化が速くなっていた
// エンティティを走査しないリソース専用システムなので、CameraListenerSystem と同じく
// SystemBase を直接継承する。
//
// 発音の指示（AudioSystem）・リスナー位置（CameraListenerSystem）が出揃った後に
// 反映したいので、Render グループの最後段（R11_AudioTick）に置く。
// ===================================================================================
class AudioEngineTickSystem : public CCL::ECS::SystemBase
{
public:
    AudioEngineTickSystem();
    virtual ~AudioEngineTickSystem() = default;

    std::vector<CCL::ECS::TypeID> GetReadTypes() const override { return {}; }
    void                          Update(float dt) override;
};
