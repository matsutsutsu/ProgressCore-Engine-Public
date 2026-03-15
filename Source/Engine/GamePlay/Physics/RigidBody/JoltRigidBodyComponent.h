#pragma once
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

// Joltの世界における「マイナンバー」を保持するコンポーネント
// ※ポインタではないため、わずか4バイト。キャッシュ効率が極めて高い。
struct JoltRigidBodyComponent {
    JPH::BodyID bodyID;
};