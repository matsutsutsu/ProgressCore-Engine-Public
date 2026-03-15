//#pragma once
//#include <PxPhysicsAPI.h>
//#include "CollisionCallback.h"
//#include <iostream>
//
//class PhysXManager {
//public:
//    PhysXManager();
//    ~PhysXManager();
//
//    // 初期化と終了処理
//    bool Init(CCL::ECS::Core::World* world);
//    void Cleanup();
//
//    // ゲッター
//    physx::PxPhysics* GetPhysics() { return _physics; }
//    physx::PxScene* GetScene() { return _scene; }
//    physx::PxCpuDispatcher* GetDispatcher() { return _dispatcher; }
//
//private:
//    physx::PxFoundation* _foundation = nullptr;
//    physx::PxPhysics* _physics = nullptr;
//    physx::PxDefaultCpuDispatcher* _dispatcher = nullptr;
//    physx::PxScene* _scene = nullptr;
//    physx::PxPvd* _pvd = nullptr;
//    physx::PxDefaultAllocator     _allocator;
//    physx::PxDefaultErrorCallback _errorCallback;
//
//    std::unique_ptr<CollisionCallback> _collisionCallback; // 寿命を管理
//};