#pragma once
// ===========================================================================
// 概要: 1体分のモデルのインスタンス。共有の ModelResource を参照し、
//       ノードの姿勢とマテリアルを個体ごとに持つ。
//
// 責務:
//   - ModelResource のノード階層を複製し、親子のポインタを張り直す
//   - ModelResource のマテリアルから MaterialData を作る
//   - アニメーションのキーフレームから各ノードの姿勢を求める・ブレンドする
//   - ノードの姿勢から、ローカル・グローバル・ワールド行列を求める
//   - ルートモーション用のルートボーンの特定と、0秒の姿勢をキャッシュする
//
// 備考: 頂点やアニメーションなどの重いデータは ModelResource（ResourceManager が共有）が持つ。
//       Mesh・Bone・Node はそのデータと、このインスタンスの nodes を指すポインタでつなぐ
//       （どれも所有しない）。nodes の要素を指すので、nodes の大きさを後から変えてはいけない。
// ===========================================================================
#include "ModelResource.h"
#include <DirectXMath.h>
#include <d3d11.h>
#include <wrl.h>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <unordered_map>

#include "MaterialData.h"
#include "ResourcePool.h"

class Model
{
public:
    // filename の ModelResource を ResourceManager から取り（未読込なら読み込み）、インスタンスを作る
    Model(ID3D11Device *device, const char *filename, float sampleRate = 60);

    struct Node
    {
        std::string name;

        // 親ノードに対する姿勢
        DirectX::XMFLOAT3 position = {0, 0, 0};
        DirectX::XMFLOAT4 rotation = {0, 0, 0, 1}; // クォータニオン
        DirectX::XMFLOAT3 scale    = {1, 1, 1};

        DirectX::XMFLOAT4X4 localTransform  = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; // 親ノードに対する行列
        DirectX::XMFLOAT4X4 globalTransform = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; // モデルの原点に対する行列
        DirectX::XMFLOAT4X4 worldTransform  = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; // ワールド空間の行列

        Node               *parent = nullptr; // 親ノード（所有しない。ルートは nullptr）
        std::vector<Node *> children;         // 子ノード（所有しない）

        // アニメーションの位置の移動をそのまま使うボーンか（ルート・Pelvis・Hips 系）。
        //   false のボーンは位置をバインドポーズ（骨の長さ）で上書きし、体型の崩れを防ぐ。
        //   骨の名前で決まり実行中は変わらないので、ロード時に一度だけ判定してここに持つ
        //   （毎フレーム文字列で判定すると重い）。
        bool isRootOrPelvis = false;
    };

    struct Material
    {
        std::string                   name;
        std::shared_ptr<MaterialData> data; // このインスタンスが持つマテリアルの値
    };

    struct Bone
    {
        const ModelResource::Bone *data = nullptr; // オフセット行列などの共有データ（所有しない）
        Node                      *node = nullptr; // このボーンが指すノード（今の姿勢を読む。所有しない）
    };

    struct Mesh
    {
        const ModelResource::Mesh *data;     // 頂点などの共有データ（所有しない）
        Material                  *material; // このメッシュに使うマテリアル（所有しない）
        Node                      *node;     // メッシュが付いているノード（所有しない）
        std::vector<Bone>          bones;    // スキニングに使うボーン。空ならスキンメッシュではない
    };

    // ノード1つ分の姿勢（位置・回転・スケール）
    struct NodePose
    {
        DirectX::XMFLOAT3 position = {0, 0, 0};
        DirectX::XMFLOAT4 rotation = {0, 0, 0, 1};
        DirectX::XMFLOAT3 scale    = {1, 1, 1};
    };

    const std::vector<Mesh> &GetMeshes() const
    {
        return meshes;
    }

    // 名前からアニメーションの番号を引く。無ければ -1
    int GetAnimationIndex(const char *name) const;

    const std::vector<Node> &GetNodes() const
    {
        return nodes;
    }
    std::vector<Node> &GetNodes()
    {
        return nodes;
    }

    // 名前からノードの番号を引く。無ければ -1
    int GetNodeIndex(const char *name) const;

    // 各ノードの姿勢から、ローカル・グローバル・ワールド行列を求める
    void UpdateTransform(const DirectX::XMFLOAT4X4 &worldTransform);

    // time 秒の姿勢を求める（1ノード版と全ノード版）
    void ComputeAnimation(int animationIndex, int nodeIndex, float time, NodePose &nodePose) const;
    void ComputeAnimation(int animationIndex, float time, std::vector<NodePose> &nodePoses) const;

private:
    // 1ノード分の姿勢を求める本体。取得済みの Animation を受け取るので、全ノード版が
    // ノードの数だけ ResourceManager を引き直さずに済む（呼び出し側で1回だけ取る）。
    void ComputeNodePose(const ModelResource::Animation &animation, int nodeIndex, float time, NodePose &nodePose) const;

public:
    // 全ノードの姿勢を上書きする（行列は UpdateTransform で求める）
    void SetNodePoses(const std::vector<NodePose> &nodePoses);

    // 全ノードの今の姿勢を nodePoses に写す（ノード数に合わせて大きさを変える）
    void GetNodePoses(std::vector<NodePose> &nodePoses) const;

    // 読み込み元のファイルパス
    const std::string &GetPath() const { return filePath; }

    const ModelResource *GetResource() const;
    ModelResource       *GetResourceMutable();

    // 2つの姿勢の列を blendRate（0〜1）で補間する。位置・スケールは線形、回転は球面線形
    static void BlendAnimations(
        const std::vector<NodePose> &animation0,
        const std::vector<NodePose> &animation1,
        float                        blendRate,
        std::vector<NodePose>       &result);

    const std::vector<Material> &GetMaterials() const
    {
        return materials;
    }

    // アニメーションの中で実際に水平に動いている、一番親に近いボーンを探す（結果はキャッシュする）
    int FindRealRootMotionNodeIndex(int animIndex) const;

    // 指定クリップの0秒時点での、指定ノードの姿勢を返す（キャッシュ付き）。
    //   ルートモーションで前進量を打ち消す基準になる姿勢で、毎フレーム同じなので、
    //   初回だけ ComputeAnimation で求めて以後は使い回す。
    const NodePose &GetPinnedPoseAtZero(int animIndex, int nodeIndex) const;

private:
    std::string filePath; // 読み込み元のファイルパス

    std::vector<Material> materials; // マテリアル（Mesh::material がこの要素を指す）

    CCL::Handle<ModelResource> resource; // 共有データ（ResourceManager が持つ）

    std::vector<Mesh> meshes;
    std::vector<Node> nodes; // Mesh・Bone・Node::parent がこの要素を指す

    // UpdateTransform でノードを計算する順番（nodes の番号。親が必ず子より先）
    std::vector<int> m_nodeUpdateOrder;

    // FindRealRootMotionNodeIndex の結果のキャッシュ（添字はアニメーションの番号）。
    //   どのボーンがルートかはアニメーションのデータだけで決まり、後から変わらない。
    //   一方で毎フレーム呼ばれる（AnimationSystem・RootMotionSystem）ので、キャッシュが無いと
    //   全ノードの ComputeAnimation を毎フレーム2回ずつ走らせることになる。
    //   -1 は「まだ求めていない」（有効なノード番号は 0 以上）。const 関数から書くので mutable。
    mutable std::vector<int> m_rootMotionNodeCache;

    // 位置のリターゲットをするか（ModelResource から写した値）。
    //   false なら isRootOrPelvis に関係なく、アニメーションの移動をそのまま使う。
    //   詳しくは ModelResource::GetTranslationRetargeting() を参照。
    bool m_translationRetargeting = true;

    // アニメーション名のハッシュ → 番号の表。
    //   GetAnimationIndex は毎フレーム（ブレンド中は2回）呼ばれる。名前と番号の対応は
    //   ロード後に変わらないので、HashString をキーに一度だけ表を作って O(1) で引く
    //   （std::string を比べる線形探索にしないこと）。
    //   最初の GetAnimationIndex で作る（m_animIndexBuilt が作ったかどうか）。
    mutable std::unordered_map<uint32_t, int> m_animIndexCache;
    mutable bool                              m_animIndexBuilt = false;

    // GetPinnedPoseAtZero のキャッシュ。キーは (animIndex, nodeIndex) を 64bit に詰めたもの
    mutable std::unordered_map<uint64_t, NodePose> m_pinnedPoseAtZeroCache;
};
