#pragma once

struct JoltSphereColliderComponent {
    float radius = 0.5f;
    bool  isDirty    = false; // サイズを変更した時にtrueにする
};