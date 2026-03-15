#include "Shader/Common/Common.hlsli"


// --- リソース定義 ---
cbuffer CbMesh : register(b0)
{
    float4 materialColor;
};

cbuffer CbToon : register(b1)
{
    float4 gRimColor; // リムライトの色
    float gRimPower; // リムの鋭さ
    float gRimIntensity; // リムの強さ
    float2 gToonPadding; // パディング
};

Texture2D DiffuseMap : register(t0);
SamplerState LinearSampler : register(s0);
Texture2D NormalMap : register(t1);

// トゥーン専用のリソースは、そのシェーダーで定義する
Texture2D ToonRampMap : register(t2);

// ディゾルブで使うノイズテクスチャ
Texture2D DissolveNoiseMap : register(REG_SRV_GLOBAL_NOISE);

// --- メイン関数 ---
float4 main(VS_OUT pin) : SV_TARGET
{
    
    // ------------------------------------------------------------
    // 0. ディゾルブ (消滅) 処理
    // ------------------------------------------------------------
    float dissolveThreshold = pin.customParams.x; // C++から送られてきた進行度 (0.0 ～ 1.0)
    
    if (dissolveThreshold > 0.0f)
    {
        // ノイズテクスチャから白黒の値を読み取る (UVを少し掛けて細かさを調整)
        // 1.0が白、0.0が黒。
        float noiseValue = DissolveNoiseMap.Sample(LinearSampler, pin.texcoord * 3.0f).r;
        
        // 【魔法の関数 clip】
        // 括弧の中が「0未満マイナス」になった瞬間、このピクセルは画面に描画されず強制終了(破棄)される。
        // つまり noiseValue が閾値を下回った黒い部分から順番に穴が空いていく。
        clip(noiseValue - dissolveThreshold);
    }
    
    // ------------------------------------------------------------
    // 1. 表面情報 (SurfaceInfo) の構築
    // ------------------------------------------------------------
    // ばらばらに届いた頂点データ(pin)を元に、ライティング計算に必要な情報をSurfaceInfo構造体にまとめる
    SurfaceInfo surf;
    
    // 基本色と透明度の取得
    // DiffuseMap.Sample() でテクスチャから色をサンプリングし、materialColor と乗算
    float4 sampledColor = DiffuseMap.Sample(LinearSampler, pin.texcoord) * materialColor;
    surf.color = sampledColor.rgb;
    surf.alpha = sampledColor.a;

    // ワールド座標の格納
    surf.pos = pin.position.xyz;

    // 視線ベクトルの計算 (カメラ座標 - ピクセル座標)
    // 光がどこで反射して目に届くかを計算するために必要
    surf.viewDir = normalize(cameraPosition.xyz - surf.pos);

    // 法線計算を関数に委譲
    // 法線マップを考慮した最終的な法線ベクトルを取得
    surf.normal = CalculateWorldNormal(
        pin.normal,
        pin.tangent,
        pin.texcoord,
        NormalMap,
        LinearSampler
    );
    
    float shadowWeight = CalculateShadowWeight(pin.shadowPos);

    // ------------------------------------------------------------
    // 2. ライティング計算 (初期化が重要！)
    // ------------------------------------------------------------
    LightResult lightRes = (LightResult) 0; // ここで 0 初期化。これでエラーが消えます

    // 平行光源の計算
    {
        float3 L = normalize(gDirLight.direction);
        float3 N = surf.normal;

        // 【ハーフランバート】
        // 0.5倍して0.5足すことで、-1～1 の範囲を 0～1 に押し込める
        float halfLambert = dot(N, -L) * 0.5f + 0.5f;

        // 【ランプサンプリング】
        // 1Dテクスチャとして扱うため、V座標は 0.5 固定
        float3 ramp = ToonRampMap.Sample(LinearSampler, float2(halfLambert, 0.5f)).rgb;

        lightRes.diffuse += gDirLight.color * gDirLight.intensity * ramp * shadowWeight;
    }

    // ------------------------------------------------------------
    // 3. 【リムライティング】
    // ------------------------------------------------------------
    // 視線と法線の角度を見て、縁を光らせる
    float rimFactor = 1.0f - saturate(dot(surf.viewDir, surf.normal));
    // ImGuiから届く gRimPower で鋭さを調整
    rimFactor = pow(rimFactor, gRimPower);
    
    // ImGuiから届く色と強さを適用
    // 影の影響も受けるように shadowWeight を掛ける　
    float3 rimLight = gRimColor.rgb * rimFactor * gRimIntensity * shadowWeight; 
    
    
    // ------------------------------------------------------------
    // 4. 環境光と最終合成
    // ------------------------------------------------------------
    float hemiWeight = surf.normal.y * 0.5f + 0.5f;
    float3 ambient = lerp(gDirLight.gGroundColor.rgb * gDirLight.gGroundColor.a, gDirLight.gSkyColor.rgb * gDirLight.gSkyColor.a, hemiWeight);

    // 物体色を光に乗せ、最後にリム（後光）を足す
    float3 finalColor = surf.color * (lightRes.diffuse + ambient) + rimLight;

    // 影の色補正
    finalColor = ApplyShadowColor(finalColor, shadowWeight);

    // フォグ適用
    float dist = length(surf.pos - cameraPosition.xyz);
    float fogFactor = saturate((dist - gFogParams.x) / (gFogParams.y - gFogParams.x));
    finalColor = lerp(finalColor, gFogColor.rgb, fogFactor * gFogColor.a);

    
    // ==========================================================
    // ディゾルブのエッジ発光（燃え尽きる表現）
    // ==========================================================
    if (dissolveThreshold > 0.0f)
    {
        // 冒頭と同じノイズの値を再度取得
        float noiseValue = DissolveNoiseMap.Sample(LinearSampler, pin.texcoord * 3.0f).r;
        
        // 境界線の幅（0.05 など、小さくすると線が細くなる）
        float edgeWidth = 0.05f;
        
        // ノイズ値と閾値の「差」が境界幅の中にいるピクセルなら、エッジとして判定
        if (noiseValue - dissolveThreshold < edgeWidth)
        {
            // 強烈なオレンジ色を設定（RGBの値を1.0より大きくすることで、Bloom等のポストプロセスで光り輝く）
            float3 edgeColor = float3(3.0f, 1.0f, 0.2f);
            
            // 境界線の部分だけ色を上書きする
            finalColor = edgeColor;
        }
    }
    
    return float4(finalColor, surf.alpha);

}
