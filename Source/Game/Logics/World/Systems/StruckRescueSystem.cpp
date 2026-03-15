#include "StruckRescueSystem.h"
#include <SimpleMath.h>
#include <vector>

// システムの実行順序の定義ヘッダー
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

using namespace DirectX::SimpleMath;

void StruckRescueSystem::Update(float dt)
{
    if (!_world->HasResource<StageMap>()) return;
    const auto &map = _world->GetResource<StageMap>();

    // 索敵半径（何マス先まで床を探すか）
    // 1だと隣しか見ませんが、3くらいにすると壁3枚分くらい埋まっていても助かります
    const int SEARCH_RADIUS = 3;

    // Transform と RigidBody の両方を持つエンティティのみ回ってくる
    ForEach([&](TransformComponent &trans, RigidBodyComponent &rb) {
        // 静的(Static)な剛体なら救済しない
        if (rb.isStatic) return;

        int gx, gz;
        map.WorldToGrid(trans.position, gx, gz);

        // 壁の中にいるか判定
        if (map.IsWall(gx, gz)) {

            // 3. 救済策: 周囲8マスを調べて、一番近い「床」を探す
            int   bestX     = -1;
            int   bestZ     = -1;
            float minDistSq = FLT_MAX;

            // 周囲1マス（斜め含む）を探索
            for (int z = -SEARCH_RADIUS; z <= SEARCH_RADIUS; ++z) {
                for (int x = -SEARCH_RADIUS; x <= SEARCH_RADIUS; ++x) {
                    if (x == 0 && z == 0) continue;

                    int tx = gx + x;
                    int tz = gz + z;

                    // 壁じゃない場所(床)が見つかったら候補にする
                    // 一番近い床を記録する
                    if (!map.IsWall(tx, tz)) {
                        Vector3 floorPos = map.GridToWorld(tx, tz);
                        float   distSq   = Vector3::DistanceSquared(trans.position, floorPos);
                        if (distSq < minDistSq) {
                            minDistSq = distSq;
                            bestX     = tx;
                            bestZ     = tz;
                        }
                    }
                }
            }

            // 救出実行
            if (bestX != -1) {
                Vector3 safePos = map.GridToWorld(bestX, bestZ);
                safePos.y       = trans.position.y;
                trans.position  = safePos;

                // 物理演算の速度もリセットして、吹っ飛びを防ぐ
                rb.velocity     = Vector3::Zero;                
            }
        }
    });
}


REGISTER_LOGIC_SYSTEM(StruckRescueSystem, Priority::LogicStage::Resolution);