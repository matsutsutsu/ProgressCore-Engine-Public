#pragma once

#include "Camera.h"
#include "RenderState.h"
#include "Light.h"

//	シャドウマップ用情報
class ShadowMap;

struct RenderContext
{
	// 全てのポインタを nullptr で初期化
	ID3D11DeviceContext* deviceContext = nullptr;
	const RenderState* renderState = nullptr;
	const Camera* camera = nullptr;
	LightManager* lightManager = nullptr;
	ShadowMap* shadowMap = nullptr;
};
