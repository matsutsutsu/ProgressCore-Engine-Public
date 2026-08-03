#pragma once
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
    Model(ID3D11Device *device, const char *filename, float sampleRate = 60);

    struct Node
    {
        // Nodeを識別するための名前
        std::string name;
        // Nodeの実際のposition?
        DirectX::XMFLOAT3 position = {0, 0, 0};
        DirectX::XMFLOAT4 rotation = {0, 0, 0, 1}; // クォータニオン
        DirectX::XMFLOAT3 scale    = {1, 1, 1};

        // 親ノードに対する相対的な座標
        DirectX::XMFLOAT4X4 localTransform = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        // モデル全体でのノードの相対座標（原点を基準にどれぐらい座標が離れているか）
        DirectX::XMFLOAT4X4 globalTransform = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        // シーン空間での最終位置
        DirectX::XMFLOAT4X4 worldTransform = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

        // 親ノードへの参照（子ノードに必要）
        Node *parent = nullptr;
        // 子ノード
        std::vector<Node *> children;

        // ★ 位置リターゲット対象フラグ（ロード時に一度だけ確定する静的な性質）。
        //   true  = Root / Pelvis / Hips 系。アニメーションの位置移動をそのまま使う。
        //   false = それ以外。位置はバインドポーズ(骨の長さ)で上書きしプロポーション崩壊を防ぐ。
        //   骨名で決まり実行中に変化しないため、毎フレーム文字列判定するのは無駄。
        //   ここへ焼き込み、ComputeAnimation ではこのフラグを見るだけにする。
        bool isRootOrPelvis = false;
    };

    struct Material
    {
        std::string name;
        // 純粋なデータの所有権を持つ
        std::shared_ptr<MaterialData> data;
    };

    struct Bone
    {
        // Resouceのボーン構造を参照しつつ自身のNodeを割り当てるため
        const ModelResource::Bone *data = nullptr; // 静的情報（offsetTransformなど）
        Node                      *node = nullptr; // 実際のノード（現在の姿勢を参照）
    };

    struct Mesh
    {
        const ModelResource::Mesh *data; // 元データへの参照

        //メッシュにも個別に別のマテリアルを適応することができるため持つ
        //例：キャラAでは赤色、キャラBでは青色にするなど
        Material *material;
        //メッシュが付く場所はNodeを参照して決めるから必要
        Node *node;
        //メッシュがどのボーンに影響されるかを知るために必要
        //ボーンなどで形を変えた時Meshもそれに合わせて変形するから
        std::vector<Bone> bones;
    };

    // モデルの「現在の姿勢（ポーズ）」を保持するためのデータ
    // ノード一つ分の現在の姿勢（位置・回転・スケール）
    struct NodePose
    {
        DirectX::XMFLOAT3 position = {0, 0, 0};
        DirectX::XMFLOAT4 rotation = {0, 0, 0, 1};
        DirectX::XMFLOAT3 scale    = {1, 1, 1};
    };

    // マテリアルデータ取得
    //const std::vector<Material>& GetMaterials() const { return materials; }

    // メッシュデータ取得
    const std::vector<Mesh> &GetMeshes() const
    {
        return meshes;
    }

    // アニメーションインデックス取得
    int GetAnimationIndex(const char *name) const;

    // ノードデータ取得
    const std::vector<Node> &GetNodes() const
    {
        return nodes;
    }
    std::vector<Node> &GetNodes()
    {
        return nodes;
    }

    // ルートノード取得
    Node *GetRootNode()
    {
        return nodes.data();
    }

    // ノードインデックス取得
    int GetNodeIndex(const char *name) const;

    // トランスフォーム更新処理
    void UpdateTransform(const DirectX::XMFLOAT4X4 &worldTransform);

    // アニメーション計算
    void ComputeAnimation(int animationIndex, int nodeIndex, float time, NodePose &nodePose) const;
    void ComputeAnimation(int animationIndex, float time, std::vector<NodePose> &nodePoses) const;

  private:
    // 単一ノードのポーズ評価の実体。既に取得済みの Animation を受け取ることで、
    // vector 版がノード数ぶん ResourceManager を引き直す無駄を無くす（呼び出し側で1回だけ取得する）。
    void ComputeNodePose(const ModelResource::Animation &animation, int nodeIndex, float time, NodePose &nodePose) const;

  public:

    // ノードポーズ設定
    void SetNodePoses(const std::vector<NodePose> &nodePoses);

    // ノードポーズ取得
    void GetNodePoses(std::vector<NodePose> &nodePoses) const;

    // ファイルパス取得
    const std::string &GetPath() const
    {
        return filePath;
    }

    const ModelResource *GetResource() const;
    ModelResource       *GetResourceMutable();

    // アニメーション補間処理
    static void BlendAnimations(
        const std::vector<NodePose> &animation0,
        const std::vector<NodePose> &animation1,
        float                        blendRate,
        std::vector<NodePose>       &result);

    // ゲッターもComponentではなくDataを返すようにする（const推奨）
    const std::vector<Material> &GetMaterials() const
    {
        return materials;
    }

    // 頂点・インデックス配列からモデルを生成する静的関数
    static std::shared_ptr<Model> CreateFromData(ID3D11Device *device,
        const std::vector<ModelResource::Vertex>              &vertices,
        const std::vector<uint32_t>                           &indices,
        std::shared_ptr<MaterialData>                          baseMaterial);

    // ルートモーションの抽出と固定の対象となる、真のルートボーンを探して返す
    int GetRootMotionNodeIndex() const;

    // アニメーションデータの中で、実際に大きく移動している真のルートボーンを自動特定する
    int FindRealRootMotionNodeIndex(int animIndex) const;

    // 指定クリップの「0秒時点」における指定ノードのポーズを返す（キャッシュ付き）。
    //   ルートモーションのピン留め基準（前進量を殺すための0秒姿勢）は毎フレーム不変なので、
    //   初回だけ ComputeAnimation で計算し、以降は再利用してホットパスの再計算を消す。
    const NodePose &GetPinnedPoseAtZero(int animIndex, int nodeIndex) const;

  private:
    // shared_ptrで持つことで、外部に配ってもデータは消えない
    std::vector<Material> materials;

    const ModelResource::Animation *currentAnimation = nullptr;
    float                           animationTime    = 0.0f;
    bool                            loop             = true;

  private:
    std::string filePath; // 読み込み元ファイルを保存

    // ModelHandle ではなく元の型を直接指定する
    CCL::Handle<ModelResource> resource;

    //std::vector<Material>	materials;
    std::vector<Mesh> meshes;
    std::vector<Node> nodes;

    // ---------------------------------------------------------------
    // ★ FindRealRootMotionNodeIndex の結果キャッシュ。
    //   「このクリップの真のルートボーンはどれか」はアニメーションデータ由来の
    //   静的な性質で、時間や再生状態に関係なく一度特定すれば二度と変わらない。
    //   しかし毎フレーム呼ばれる箇所（AnimationSystem/RootMotionSystem）から
    //   使われるため、キャッシュ無しだと ComputeAnimation を全ノード×2回、
    //   フレームごとに再実行することになり大きな無駄になる。
    //   要素 -1 は「まだ計算していない」を表す番兵（有効なノード番号は常に0以上）。
    //   const 関数から書き込むため mutable。
    // ---------------------------------------------------------------
    mutable std::vector<int> m_rootMotionNodeCache;

    // ---------------------------------------------------------------
    // ★ アニメ名 → インデックスのキャッシュ。
    //   GetAnimationIndex は毎フレーム（ブレンド時は2回）呼ばれるが、旧実装は
    //   全アニメを std::string 比較で線形走査していた。名前とインデックスの対応は
    //   ロード後は不変なので、FNV-1a ハッシュ(HashString)をキーに一度だけ表を作り O(1) 化する。
    //   初回 GetAnimationIndex 呼び出しで遅延構築する（m_animIndexBuilt が番兵）。
    // ---------------------------------------------------------------
    mutable std::unordered_map<uint32_t, int> m_animIndexCache;
    mutable bool                              m_animIndexBuilt = false;

    // ---------------------------------------------------------------
    // ★ ルートモーションのピン留め用「0秒ポーズ」キャッシュ。
    //   キーは (animIndex, nodeIndex) を 1つの 64bit に詰めたもの。
    //   0秒時点の姿勢はクリップ不変なので、初回計算後は再利用する。
    // ---------------------------------------------------------------
    mutable std::unordered_map<uint64_t, NodePose> m_pinnedPoseAtZeroCache;
};