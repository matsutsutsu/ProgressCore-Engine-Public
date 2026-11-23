#pragma once
// プレイヤーを映している TPS カメラを探す
// カメラへ要求を書く System（ロックオン・ダッシュ・ラッシュ・チュートリアル）が同じ探し方をするので、1か所にまとめる
#include "Engine/ECS/Common/CCL_Types.h"

namespace CCL::ECS
{
class World;
}

// playerID を追っている（target が playerID の）TPS カメラを返す。なければ InvalidEntityID
// 複数あるときは最初に見つかった1台
CCL::ECS::EntityID FindPlayerCamera(CCL::ECS::World &world, CCL::ECS::EntityID playerID);
