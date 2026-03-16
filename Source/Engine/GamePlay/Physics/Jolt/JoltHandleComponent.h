#pragma once
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

// Joltの世界における「マイナンバー」を保持するコンポーネント
// ※ポインタではないため、わずか4バイト。キャッシュ効率が極めて高い。
struct JoltHandleComponent {
    // ※SetupSystemが自動で付与するため、ユーザーが手動で触ることはない。
    JPH::BodyID bodyID;
};