#pragma once
// ===========================================================================
// 概要: IAudioAPI::Update() を 1 描画フレームにつき 1 回だけ呼ぶ System（R11_AudioTick）。
//
// 責務:
//   - オーディオエンジンの 1 フレーム更新（3D 音響の再計算、再生済みインスタンスの回収、
//     スロー度の追従、XAudio2 のデバイスロスト復帰）
//
// 備考:
//   - Logic 側（AudioSystem の末尾など）で呼んではいけない。Logic は FixedUpdate なので
//     1 描画フレームに最大 4 回（Framework の MAX_STEPS）走り、
//       ・DXTK の AudioEngine::Update は毎フレーム 1 回を前提にした API
//       ・再生中の全ボイスへピッチを掛け直すループが回数分重複する
//     という問題が出る。
//   - 発音の指示（AudioSystem）とリスナー位置（CameraListenerSystem）が揃った後に
//     反映したいので、Render グループの最後（R11_AudioTick）に置く。
//   - Entity を走査しないので、CameraListenerSystem と同じく SystemBase を直接継承する。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"

class AudioEngineTickSystem : public CCL::ECS::SystemBase
{
public:
    AudioEngineTickSystem();
    virtual ~AudioEngineTickSystem() = default;

    std::vector<CCL::ECS::TypeID> GetReadTypes() const override { return {}; }
    void                          Update(float dt) override;
};
