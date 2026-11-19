#ifndef SSAO_HLSLI
#define SSAO_HLSLI

#include "Engine/Graphics/Shader/ShaderResources.h"

// SSAO の1段目の設定。PostProcessShared.h の CbSSAOData と同じ並びにする（変えるときは両方を直す）
// PostProcessShared.h を include すると、同じ番号に置く他のエフェクトの cbuffer も一緒に入るので、SSAO のシェーダーはこちらを使う
cbuffer CbSSAO : register(REG_CB(SLOT_CB_SSAO))
{
    float ssaoRadius;      // 周りを調べる半径（m）
    float ssaoIntensity;   // 暗くする強さ（0〜1）
    float ssaoBias;        // これより浅い差は遮られていないとみなす（面の自己遮蔽を防ぐ）
    float ssaoMaxDistance; // これより遠いピクセルは計算しない（m）
};

#endif
