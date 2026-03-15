//#include "PhysXManager.h"
//
//using namespace physx;
//
//PhysXManager::PhysXManager() {}
//
//PhysXManager::~PhysXManager() {
//    Cleanup();
//}
//
//bool PhysXManager::Init(CCL::ECS::Core::World* world) {
//    // 1. Foundationの作成
//    _foundation = PxCreateFoundation(PX_PHYSICS_VERSION, _allocator, _errorCallback);
//    if (!_foundation) return false;
//
//    // 2. Physicsオブジェクトの作成
//    _physics = PxCreatePhysics(PX_PHYSICS_VERSION, *_foundation, PxTolerancesScale(), true, nullptr);
//    if (!_physics) return false;
//
//    // 3. シーンの設定
//    PxSceneDesc sceneDesc(_physics->getTolerancesScale());
//    sceneDesc.gravity = PxVec3(0.0f, -9.81f, 0.0f); // 重力の設定
//
//    // CPUスレッド（Dispatcher）の作成
//    _dispatcher = PxDefaultCpuDispatcherCreate(2); // 2スレッド使用
//    sceneDesc.cpuDispatcher = _dispatcher;
//    sceneDesc.filterShader = PxDefaultSimulationFilterShader; // デフォルトの衝突判定ロジック
//
//	// 衝突コールバックの設定
//    _collisionCallback = std::make_unique<CollisionCallback>(world);
//    sceneDesc.simulationEventCallback = _collisionCallback.get();
//
//    // 4. シーンの作成
//    _scene = _physics->createScene(sceneDesc);
//    if (!_scene) return false;
//
//    return true;
//}
//
//void PhysXManager::Cleanup() {
//    // 解放は作成の逆順で行う
//    if (_scene) { _scene->release(); _scene = nullptr; }
//    if (_dispatcher) { _dispatcher->release(); _dispatcher = nullptr; }
//    if (_physics) { _physics->release(); _physics = nullptr; }
//    if (_foundation) { _foundation->release(); _foundation = nullptr; }
//}