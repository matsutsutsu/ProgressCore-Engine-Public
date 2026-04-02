#include "ModelRenderer.h"
#include "Engine/Core/Common/Misc.h"
#include "Engine/Graphics/Core/GpuResourceUtils.h"
#include "Engine/Graphics/Core/Graphics.h"
#include "Engine/Graphics/Renderer/RenderQueue.h"
#include <algorithm>


#include "Engine/Graphics/Shader/ShaderResources.h"
#include "Engine/Graphics/Shader/ShaderRegistry.h"



ModelRenderer::ModelRenderer(ID3D11Device *device)
{
   
    // GPUインスタンシングのバッファ作成
    _instanceBuffer.Create(device, 8192); // 8192個まで対応

    // グローバル定数バッファの初期化
    InitializeGlobalConstantBuffers(device);


    // ① 不透明用：デフォルトそのまま
    _psoOpaque = PipelineState(PipelineStateDesc::DefaultOpaque());

    // ② 半透明用：デフォルトそのまま
    _psoTransparent = PipelineState(PipelineStateDesc::DefaultTransparent());

    // ③ 影用：不透明をベースにするが、念のため明示的にもう一度上書きしておく
    PipelineStateDesc shadowDesc = PipelineStateDesc::DefaultOpaque();
    _psoShadow = PipelineState(shadowDesc);


    OutputDebugStringA("ModelRenderer: Initialized with shader reflection\n");
}

void ModelRenderer::InitializeGlobalConstantBuffers(ID3D11Device *device)
{
    // CbScene は Graphics::Initialize で既に作られているため、ここでは CbSkeleton のみ作成します。
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(CbSkeleton);
    // 16バイトアライメント
    if (desc.ByteWidth % 16 != 0) desc.ByteWidth = ((desc.ByteWidth + 15) / 16) * 16;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    // ※ ヘッダーの _cbSkeletonBuffer を ComPtr にしていない場合は生ポインタとして代入されます
    device->CreateBuffer(&desc, nullptr, &_cbSkeletonBuffer);
}


void ModelRenderer::Render(const RenderContext &rc)
{
    int                  drawCallCount = 0;
    ID3D11DeviceContext *dc            = rc.deviceContext;


    // === 2. サンプラステート設定 ===
    ID3D11SamplerState *samplerStates[] = {
        rc.renderState->GetSamplerState(SamplerState::LinearWrap)};
    dc->PSSetSamplers(SLOT_SMP_LINEAR, _countof(samplerStates), samplerStates);

    // === 3. レンダーステート設定 ===
	// 不透明オブジェクトの描画なので、デフォルトの不透明用ステートを適用
    _psoOpaque.Apply(dc, rc.renderState);


    // === 4. 視錐台カリングの準備 ===
    DirectX::XMMATRIX projection = DirectX::XMLoadFloat4x4(&rc.camera->GetProjection());
    DirectX::BoundingFrustum::CreateFromMatrix(m_frustum, projection);

    DirectX::XMMATRIX view    = DirectX::XMLoadFloat4x4(&rc.camera->GetView());
    DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(nullptr, view);
    m_frustum.Transform(m_frustum, invView);

    // カメラ情報（透明度ソート用）
    DirectX::XMVECTOR CameraPosition = DirectX::XMLoadFloat3(&rc.camera->GetEye());
    DirectX::XMVECTOR CameraFront    = DirectX::XMLoadFloat3(&rc.camera->GetFront());


    // 描画の直前に、カウンターの伝票をソートさせる
    auto& queue = RenderQueue::Instance();
    queue.SortAndBuildBatches();

    // カウンターから綺麗に並んだデータを受け取る
    const auto& sortKeys = queue.GetSortedKeys();
    const auto& opaqueCommands = queue.GetOpaqueCommands();

    // 不透明バッチの中に混ざっていた「半透明メッシュ」を一時退避するカウンター
    std::vector<TransparencyCommand> deferredTransparents;

    // -------------------------------------------------------------------
    // 5. 並べ替えた伝票（sortKeys）の順番に従って描画していく
    // -------------------------------------------------------------------
    size_t i = 0;
    while (i < sortKeys.size()) {
        
        // 伝票に書かれている「元の番号」を見る
        uint32_t currentIndex = sortKeys[i].originalIndex;
        
        // その番号を使って、初めて重いデータ(DrawInfo)の中身を覗き見る
        const auto &current = opaqueCommands[currentIndex];

        // シェーダーの準備
        Shader* shader = ShaderRegistry::Instance().GetShader(current.shaderHash, Graphics::Instance().GetDevice());
        if (!shader) {
            // 万が一見つからなかった場合（登録忘れなど）はスキップ
            i++;
            continue;
        }

        shader->Begin(rc);

        // -------------------------------------------------------------------
        // 6. 究極に最適化されたバッチ判定（同じモデルが何個続くか数える）
        // -------------------------------------------------------------------
        size_t batchStart = i;
        size_t batchCount = 0;

        // 今見ている「基準となる伝票の64ビットの数字」をメモしておく
        uint64_t currentBatchKey = sortKeys[batchStart].value;

        // ★ポイント: 昔はここで「モデルは同じか？マテリアルのforループを回して...」とやっていました。
        // 今は「64ビットの数字が完全に同じか？」を比較するだけで終わります！
        while (i < sortKeys.size()) {
            if (sortKeys[i].value != currentBatchKey) {
                break; // 数字が1ビットでも違えば、別のモデルかマテリアルなので、ここで数えるのをやめる
            }
            batchCount++; // 同じなら「一気に描画する数」を+1する
            i++;
        }

        // -------------------------------------------------------------------
        // 7. GPUに送るための一括データ（インスタンス配列）を作る
        // -------------------------------------------------------------------
        static std::vector<InstanceData> instanceDataCache; 
        instanceDataCache.clear();
        instanceDataCache.reserve(batchCount);

        for (size_t k = 0; k < batchCount; ++k) {
            // さっき数えた「同じモデルの集まり(batchCount)」の分だけ、
            // 伝票の順番通りに元の重いデータを取りに行く
            uint32_t idx = sortKeys[batchStart + k].originalIndex;
            const auto & cmd = opaqueCommands[idx];
            
            // 行列などの本当に必要なデータだけをコピーしてGPU転送用の配列に詰める
            InstanceData data;
            data.world = cmd.worldMatrix;
            data.customParams = cmd.customParams;
            instanceDataCache.push_back(data);
        }
        // GPUバッファへ転送
        _instanceBuffer.Write(dc, instanceDataCache);
        // VSの t11 スロットにバインド
        _instanceBuffer.Bind(dc, SLOT_SRV_INSTANCE);

        // === モデル内の全メッシュを描画 ===
        // 同じモデルなので、メッシュ構造は共通。
        // メッシュごとに「インスタンス数」を指定して描画する。
        const auto &meshes = current.model->GetMeshes();
        for (size_t meshIdx = 0; meshIdx < meshes.size(); ++meshIdx) {
            const Model::Mesh &mesh = meshes[meshIdx];

            // --- マテリアル差し替えトリック ---
            Model::Mesh     &mutableMesh    = const_cast<Model::Mesh &>(mesh);
            Model::Material *originalMatPtr = mutableMesh.material;

            // オーバーライド判定
            // 「リストがあり」かつ「今のメッシュ番号に対応するデータがあり」かつ「nullptrじゃない」場合
            MaterialData *overrideData = nullptr;

            if (meshIdx < current.overrideCount) {
                // ハンドルを使って実体を取りに行く
                MaterialHandle handle = current.overrideMaterials[meshIdx];
                if (handle.IsValid()) {
                    overrideData = ResourceManager::Instance().GetMaterial(handle);
                }
            }

            Model::Material tempMatWrapper;
            bool            applyOverride = (overrideData != nullptr);

            if (applyOverride) {
                // overrideData は ResourceManager が管理しているポインタなので、
                // 勝手に delete されないように「空のデリータ」付き shared_ptr に包んで渡す
                tempMatWrapper.data =
                    std::shared_ptr<MaterialData>(overrideData, [](MaterialData *) {});
                tempMatWrapper.name  = "InstanceOverride";
                mutableMesh.material = &tempMatWrapper;
            }

            // マテリアル情報の取得 (差し替えた後のものを見る)
            auto &matData = *mutableMesh.material->data;

            // 半透明判定
            float alpha = 1.0f;
            if (matData.colors.count("materialColor")) alpha = matData.colors.at("materialColor").w;
            bool isTransparent =
                (matData.alphaMode == AlphaMode::Blend || (alpha > 0.01f && alpha < 0.99f));

            if (isTransparent) // 半透明の場合
            {
                // 半透明の場合：インスタンシングできないので、個別にリストに追加して後回し
                // バッチ内の全インスタンスについてループ
                for (size_t k = 0; k < batchCount; ++k) {
                    const auto &info = opaqueCommands[batchStart + k];

                    // TransparencyDrawInfo から TransparencyCommand へ変更
                    // opaqueCommands.emplace_back() ではなく、通常の変数として作成する
                    TransparencyCommand tInfo;
                    tInfo.shaderHash = current.shaderHash;
                    tInfo.mesh = &mesh;
                    tInfo.worldMatrix = info.worldMatrix;

                    if (meshIdx < info.overrideCount) {
                        tInfo.overrideMaterial = info.overrideMaterials[meshIdx];
                    }
                    else {
                        tInfo.overrideMaterial = MaterialHandle{};
                    }
                    tInfo.customParams = info.customParams;

                    // 距離計算（ソート用）
                    DirectX::XMVECTOR WorldPos = DirectX::XMLoadFloat4x4(&info.worldMatrix).r[3];
                    tInfo.distance = DirectX::XMVectorGetX(DirectX::XMVector3Length(
                        DirectX::XMVectorSubtract(WorldPos, CameraPosition)));

                    // 一時退避リストに放り込む
                    deferredTransparents.push_back(tInfo);
                }
            }
            else // 不透明の場合
            {

                // ボーンがある場合はインスタンシングできない（今回はスキップして単体描画にフォールバック）
                // ※本格対応するにはボーン行列をテクスチャに焼くなどの高度な実装が必要
                bool isSkinned = !mesh.bones.empty();

                if (isSkinned) {
                    // --- スキンメッシュ：単体描画ループ ---
                    // バッチ内の全インスタンスについてループ
                    for (size_t k = 0; k < batchCount; ++k) {
                        // スケルトン更新（この関数内で cbSkeleton を更新している）
                        UpdateSkeletonConstants(dc, mesh);

                        // マテリアル更新
                        shader->Update(rc, mesh);

                        // 描画
                        UINT stride = sizeof(ModelResource::Vertex);
                        UINT offset = 0;
                        dc->IASetVertexBuffers(
                            0, 1, mesh.data->vertexBuffer.GetAddressOf(), &stride, &offset);
                        dc->IASetIndexBuffer(mesh.data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
                        dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                        dc->DrawIndexed(static_cast<UINT>(mesh.data->indices.size()), 0, 0);
                        drawCallCount++;
                    }
                }
                else {
                    // --- 静的メッシュ：インスタンシング描画 ---

                    // 1. マテリアル更新（バッチ内で共通）
                    shader->Update(rc, mesh);

                    // 2. 頂点バッファセット
                    UINT stride = sizeof(ModelResource::Vertex);
                    UINT offset = 0;
                    dc->IASetVertexBuffers(
                        0, 1, mesh.data->vertexBuffer.GetAddressOf(), &stride, &offset);
                    dc->IASetIndexBuffer(mesh.data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
                    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

                    // 3. インスタンシング描画発行！
                    // 第2引数に「インスタンス数（batchCount）」を渡す
                    dc->DrawIndexedInstanced(static_cast<UINT>(mesh.data->indices.size()),
                        static_cast<UINT>(batchCount), // ここがポイント！
                        0,
                        0,
                        0);

                    drawCallCount++;
                }
            }

            // 元に戻す（重要）
            if (applyOverride) {
                mutableMesh.material = originalMatPtr;
            }
        }

        // バインド解除
        _instanceBuffer.Unbind(dc, SLOT_SRV_INSTANCE);
        shader->End(rc);
    }

    // === 7. 半透明オブジェクトの描画 (変更なし) ===
    // 半透明はソート順が命なので、インスタンシング（モデル順ソート）とは相性が悪い。
    // そのまま単体描画を行うのが一般的

	// 半透明オブジェクトの描画なので、デフォルトの半透明用ステートを適用
    _psoTransparent.Apply(dc, rc.renderState);


    // 退避していた半透明メッシュと、ECSから直接Submitされた半透明メッシュを合流させる
    auto allTrans = deferredTransparents;
    for (const auto& t : queue.GetTransparentCommands()) {
        allTrans.push_back(t);
    }

    // transparencyDrawInfos を allTrans に、TransparencyDrawInfo を TransparencyCommand に変更
    std::sort(allTrans.begin(),
        allTrans.end(),
        [](const TransparencyCommand& lhs, const TransparencyCommand& rhs) {
            return lhs.distance > rhs.distance; // 遠い順（Zオーダー）
        });


    for (const TransparencyCommand& info : allTrans) {
        Shader* shader = ShaderRegistry::Instance().GetShader(info.shaderHash, Graphics::Instance().GetDevice());
        if (!shader) continue;

        shader->Begin(rc);

        // マテリアル差し替え
        const Model::Mesh &mesh           = *info.mesh;
        Model::Mesh       &mutableMesh    = const_cast<Model::Mesh &>(mesh);
        Model::Material   *originalMatPtr = mutableMesh.material;
        Model::Material    tempMatWrapper;
        // 1. ハンドルが有効かどうかチェック
        bool applyOverride = info.overrideMaterial.IsValid();

        if (applyOverride) {
            // 2. ハンドルから実体（生ポインタ）を取得
            MaterialData *ptr = ResourceManager::Instance().GetMaterial(info.overrideMaterial);

            if (ptr) {
                // 3. 空のデリータ付き shared_ptr に包んで渡す
                tempMatWrapper.data  = std::shared_ptr<MaterialData>(ptr, [](MaterialData *) {});
                tempMatWrapper.name  = "TranspOverride";
                mutableMesh.material = &tempMatWrapper;
            }
            else {
                applyOverride = false; // ポインタが取れなければ無効化
            }
        }

        // 1. データ作成
        InstanceData singleData;
        singleData.world = info.worldMatrix;

        // 汎用パラメータ初期化
        singleData.customParams = info.customParams;

        // 2. バッファの0番目に書き込む（vectorのサイズ1で渡せばOK）
        // 毎回 Map/Unmap が発生しますが、半透明の数は少ないので許容範囲です
        std::vector<InstanceData> temp = {singleData};
        _instanceBuffer.Write(dc, temp);

        // 3. バインド (t11にセット)
        _instanceBuffer.Bind(dc, SLOT_SRV_INSTANCE);

        // 4. マテリアル更新
        // （定数バッファの更新などは drawMesh と同じ）
        UpdateSkeletonConstants(dc, mesh); // スキンメッシュ対応
        shader->Update(rc, mesh);

        // 5. 描画
        UINT stride = sizeof(ModelResource::Vertex);
        UINT offset = 0;
        dc->IASetVertexBuffers(0, 1, mesh.data->vertexBuffer.GetAddressOf(), &stride, &offset);
        dc->IASetIndexBuffer(mesh.data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
        dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // DrawIndexed を呼ぶと instanceID=0 になるので、
        // さっき書き込んだ 0番目のデータ が使われる！
        dc->DrawIndexed(static_cast<UINT>(mesh.data->indices.size()), 0, 0);

        // 復元
        if (applyOverride) mutableMesh.material = originalMatPtr;

        // 6. 後始末
        _instanceBuffer.Unbind(dc, SLOT_SRV_INSTANCE);
        shader->End(rc);
    }

    // === 8. リソース解除 ===
    UnbindAllResources(dc);

    // デバッグ出力（必要に応じて）
    // char buf[128];
    // sprintf_s(buf, "Total Draw Calls: %d\n", drawCallCount);
    // OutputDebugStringA(buf);
}





// === 新しいメソッド：完全なリソース解除 ===
void ModelRenderer::UnbindAllResources(ID3D11DeviceContext *dc)
{
    ID3D11Buffer             *nullCB      = nullptr;
    ID3D11SamplerState       *nullSampler = nullptr;
    ID3D11ShaderResourceView *nullSRV     = nullptr;

    // 1. グローバル定数バッファの解除
    //for (auto &[name, buffer] : globalConstantBuffers) {
    //    UINT slot = buffer.GetBindPoint();
    //    dc->VSSetConstantBuffers(slot, 1, &nullCB);
    //    dc->PSSetConstantBuffers(slot, 1, &nullCB);
    //}

    // 1. グローバル定数バッファの解除 (マクロで直接指定)
    dc->VSSetConstantBuffers(SLOT_CB_SCENE, 1, &nullCB);
    dc->PSSetConstantBuffers(SLOT_CB_SCENE, 1, &nullCB);

    dc->VSSetConstantBuffers(SLOT_CB_SKELETON, 1, &nullCB);
    dc->PSSetConstantBuffers(SLOT_CB_SKELETON, 1, &nullCB);

    // 2. サンプラーステートの解除
    dc->PSSetSamplers(0, 1, &nullSampler);

    // ライト・シャドウ関連の解除
    // CbLight (b9)
    dc->VSSetConstantBuffers(SLOT_CB_LIGHT, 1, &nullCB);
    dc->PSSetConstantBuffers(SLOT_CB_LIGHT, 1, &nullCB);

    // CbShadow (b10)
    dc->VSSetConstantBuffers(SLOT_CB_SHADOW, 1, &nullCB);
    dc->PSSetConstantBuffers(SLOT_CB_SHADOW, 1, &nullCB);

    // SrvShadow (t10)
    dc->PSSetShaderResources(SLOT_SRV_SHADOW, 1, &nullSRV);

    // SmpShadow (s10)
    dc->PSSetSamplers(SLOT_SMP_SHADOW, 1, &nullSampler);

    // ※ シェーダー固有のリソースは各シェーダーの End() で解除済み
}


void ModelRenderer::UpdateSkeletonConstants(ID3D11DeviceContext *dc, const Model::Mesh &mesh)
{
    // 文字列検索を削除！ポインタがあれば即実行
    if (!_cbSkeletonBuffer) return;

    // ボーンがある場合
    if (!mesh.bones.empty()) {
        size_t count = mesh.bones.size();
        if (count > MAX_BONES) count = MAX_BONES; // 安全対策

        for (size_t i = 0; i < count; ++i) {
            const Model::Bone &bone = mesh.bones[i];

            // 計算：Offset(初期姿勢の逆行列) * World(現在の姿勢)
            // これで「モデル空間の頂点」を直接「ワールド空間」へ持っていく行列になります
            DirectX::XMMATRIX matOffset = DirectX::XMLoadFloat4x4(&bone.data->offsetTransform);
            DirectX::XMMATRIX matWorld  = DirectX::XMLoadFloat4x4(&bone.node->worldTransform);
            DirectX::XMMATRIX matFinal  = matOffset * matWorld;

            DirectX::XMStoreFloat4x4(&m_cbSkeletonCache.boneTransforms[i], matFinal);
        }
    }
    else {
        // ボーンがない場合はメッシュ自体のワールド行列を0番に入れる（または何もしない）
        // ※シェーダー側で「ボーンなし」の分岐があるなら不要ですが、念のため
        m_cbSkeletonCache.boneTransforms[0] = mesh.node->worldTransform;
    }

    // GPU更新
    dc->UpdateSubresource(_cbSkeletonBuffer, 0, nullptr, &m_cbSkeletonCache, 0, 0);

    // 更新したバッファを b12 (CbSkeleton) に紐づける
    ID3D11Buffer *buffer = _cbSkeletonBuffer;
    dc->VSSetConstantBuffers(SLOT_CB_SKELETON, 1, &buffer);
}

void ModelRenderer::UpdateAllGlobalConstantBuffers(ID3D11DeviceContext *dc)
{
    // (何もしない。スケルトンバッファは UpdateSkeletonConstants で個別更新されるため)

    //for (auto &[name, buffer] : globalConstantBuffers) {
    //    // CbSkeleton は UpdateSkeletonConstants で個別更新するのでスキップ
    //    if (name == "CbSkeleton") continue;

    //    buffer.UpdateBuffer(dc);
    //}
}

// 修正後：Graphicsクラスのシーンバッファと、自身のスケルトンバッファをバインド
void ModelRenderer::BindAllGlobalConstantBuffers(ID3D11DeviceContext* dc)
{
    // 1. CbScene (スロット b8)
    ID3D11Buffer* sceneBuf = Graphics::Instance().GetSceneConstantBuffer();
    if (sceneBuf) {
        dc->VSSetConstantBuffers(SLOT_CB_SCENE, 1, &sceneBuf);
        dc->PSSetConstantBuffers(SLOT_CB_SCENE, 1, &sceneBuf);
    }

    // 2. CbSkeleton (スロット b12)
    if (_cbSkeletonBuffer) {
        dc->VSSetConstantBuffers(SLOT_CB_SKELETON, 1, &_cbSkeletonBuffer);
    }
}


void ModelRenderer::PrintGlobalReflectionInfo() const
{
    OutputDebugStringA("\n=== ModelRenderer Global Constant Buffers ===\n");

    OutputDebugStringA("=============================================\n\n");
}

void ModelRenderer::PrintAllShaderReflectionInfo() const
{
    OutputDebugStringA("\n=== All Shader Reflection Info ===\n");


    OutputDebugStringA("=====================================\n\n");
}

void ModelRenderer::DrawShadow(
    const Model *model, const Model::Mesh *mesh, const DirectX::XMFLOAT4X4 &worldMatrix)
{
    shadowDrawInfos.push_back({model, mesh, worldMatrix});
}

void ModelRenderer::RenderShadowPass(const RenderContext &rc)
{
    // 何もなければ帰る
    if (shadowDrawInfos.empty()) return;

    ID3D11DeviceContext *dc        = rc.deviceContext;
    auto                &shadowMap = rc.shadowMap;

    // 1. シャドウマップへの書き込みを開始
    shadowMap->Activate(dc);

    // =========================================================================
    // 前フレームのUI描画（NoWrite）などの状態を引き継がないよう、
    // ここでシャドウマップ用の「正しい描画ステート」に強制リセットする！
    // =========================================================================
    _psoShadow.Apply(dc, rc.renderState);

    // 定数バッファの更新とバインド
    {
        // コンポーネントから受け取った値をShadowMapに適用
        shadowMap->params.shadowBias  = _shadowBias;
        shadowMap->params.shadowColor = _shadowColor;
        // orthoSizeはViewProj行列計算に使ったのでここでは不要（行列に含まれている）

        // (1) CbShadowのデータを生成 (受け取っておいた _shadowLightVP を使う)
        auto cbData = shadowMap->CreateCbData(_shadowLightVP);

        // (2) GPU上の定数バッファを更新
        shadowMap->UpdateConstantBuffer(dc, cbData);

        // (3) 頂点シェーダー(VS)にバインド (Slot 10)
        //     ※影生成パスではテクスチャ(SRV)やサンプラーは不要なので -1 を指定
        shadowMap->Bind(dc, -1, -1, SLOT_CB_SHADOW);
    }

    // 2. シャドウ用シェーダーを有効化

    Shader* shader = ShaderRegistry::Instance().GetShader("ShadowMap"_hash, Graphics::Instance().GetDevice());
    if (!shader) return; // 安全対策
    shader->Begin(rc);

    // =========================================================================
    // パーティクル(点)やデバッグ(線)のステートを引き継がないよう、
    // ここで明示的に「三角形(ポリゴン)」にリセットする！
    // =========================================================================
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // 3. ソート：メッシュ（マテリアル）単位で並べ替えてバッチ効率を上げる
    std::sort(shadowDrawInfos.begin(),
        shadowDrawInfos.end(),
        [](const ShadowDrawInfo &a, const ShadowDrawInfo &b) {
            // メッシュのアドレスで比較（同じメッシュが連続するように）
            return a.mesh < b.mesh;
        });

    // インスタンシング用の一時バッファ
    // （静的メッシュをここに溜め込んで一気に描く）
    struct InstanceBatch {
        const Model::Mesh        *mesh = nullptr;
        std::vector<InstanceData> data = {};
    };
    std::vector<InstanceBatch> batches;
    // メモリ確保の効率化（毎回再確保しないように）
    batches.reserve(16);

    // --- ループ処理：仕分け ---
    for (const auto &info : shadowDrawInfos) {
        // 安全対策
        if (!info.mesh || !info.mesh->data) continue;

        // ボーンがあるかチェック（スキンメッシュ判定）
        bool isSkinned = !info.mesh->bones.empty();

        if (isSkinned) {
            // === A. スキンメッシュ（キャラクター等）：個別描画 ===
            // ※現状のShadowMapShaderはスキン非対応（Tポーズになる）可能性がありますが、
            //   構造としてはここで「スキン行列を更新してDraw」を行います。

            // 将来的にはここで info.model->UpdateBoneConstants(dc) などを呼ぶ
            UpdateSkeletonConstants(dc, *info.mesh);

            // 頂点バッファセット
            UINT stride = sizeof(ModelResource::Vertex);
            UINT offset = 0;
            dc->IASetVertexBuffers(
                0, 1, info.mesh->data->vertexBuffer.GetAddressOf(), &stride, &offset);
            dc->IASetIndexBuffer(info.mesh->data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

            // ワールド行列を更新（スキンメッシュ用シェーダーがない場合、とりあえずワールド行列で描く）
            // ※本来は CB_Skeleton を更新する必要があります
            shader->Update(rc, *info.mesh); // ここでWorld行列を送る実装になっているか確認が必要

            // 個別描画
            dc->DrawIndexed((UINT)info.mesh->data->indices.size(), 0, 0);
        }
        else {
            // === B. 静的メッシュ（背景・プロップ）：インスタンシング ===
            // 直前のバッチと同じメッシュなら追加、違えば新規作成
            if (!batches.empty() && batches.back().mesh == info.mesh) {
                batches.back().data.push_back({info.worldMatrix});
            }
            else {
                InstanceBatch newBatch;
                newBatch.mesh = info.mesh;
                newBatch.data.push_back({info.worldMatrix});
                batches.push_back(newBatch);
            }
        }
    }

    // --- インスタンシング描画実行 (静的メッシュ) ---
    for (const auto &batch : batches) {

        // =========================================================================
        // 静的メッシュでも必ずシェーダーのUpdateを呼び、定数バッファをGPUに送る
        // =========================================================================
        shader->Update(rc, *batch.mesh);

        size_t totalCount = batch.data.size();
        size_t offset     = 0;

        // インスタンスバッファの上限ごとに分割して描画
        while (offset < totalCount) {
            // 今回描画する数 (残り or 上限)
            size_t drawCount = (std::min)(totalCount - offset, InstanceBuffer::MAX_INSTANCES);

            // 必要な分だけデータをコピーするための部分配列を作成
            // (vectorのイテレータを使って範囲指定コピー)
            std::vector<InstanceData> chunk(
                batch.data.begin() + offset, batch.data.begin() + offset + drawCount);

            // GPUバッファに転送 (Map/Unmap)
            _instanceBuffer.Write(dc, chunk);

            // VSの指定スロット(t11等)にバインド
            _instanceBuffer.Bind(dc, SLOT_SRV_INSTANCE);

            // 頂点バッファセット
            UINT stride = sizeof(ModelResource::Vertex);
            UINT zero   = 0;

            dc->IASetVertexBuffers(
                0, 1, batch.mesh->data->vertexBuffer.GetAddressOf(), &stride, &zero);
            dc->IASetIndexBuffer(batch.mesh->data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

            // インスタンシング描画発行
            // 第1引数: インデックス数
            // 第2引数: インスタンス数
            dc->DrawIndexedInstanced(
                (UINT)batch.mesh->data->indices.size(), (UINT)drawCount, 0, 0, 0);

            offset += drawCount;
        }
    }

    // 4. 終了処理
    // シェーダー終了
    shader->End(rc);

    // インスタンスバッファのバインド解除（重要）
    _instanceBuffer.Unbind(dc, SLOT_SRV_INSTANCE);

    // シャドウマップへの書き込み終了
    shadowMap->Deactivate(dc);
}