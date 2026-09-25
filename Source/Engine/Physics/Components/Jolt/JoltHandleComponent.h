#pragma once
// ===========================================================================
// 概要: Jolt 空間に生成した剛体の BodyID を保持するコンポーネント。
//
// 責務:
//   - Entity と Jolt の剛体を対応づける（BodyID は4バイトの値で、ポインタは持たない）
//
// 備考: 手で付けない。JoltBodySetupSystem（剛体）と JoltCharacterSetupSystem
//       （キャラクターの presence プロキシ）が生成時に付ける。
//       JoltPushSystem / JoltPullSystem はこれを持つ Entity だけを同期する。
//       破棄時は JoltCleanupSystem が剛体を消し、bodyID を無効値に戻す。
// ===========================================================================
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

struct JoltHandleComponent
{
    JPH::BodyID bodyID; // 既定は無効な ID
};
