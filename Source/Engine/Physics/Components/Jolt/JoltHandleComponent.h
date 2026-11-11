#pragma once
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

// Jolt に生成した剛体の BodyID。手で付けず、JoltBodySetupSystem と JoltCharacterSetupSystem が付ける
// JoltPushSystem / JoltPullSystem はこれを持つ Entity だけを同期し、破棄時は JoltCleanupSystem が剛体を消す
struct JoltHandleComponent
{
    JPH::BodyID bodyID; // 既定は無効な ID
};
