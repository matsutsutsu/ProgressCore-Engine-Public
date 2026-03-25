// 1体のデータ (16バイト境界)
struct BoidData
{
    float3 position;
    float scale;
    float3 velocity;
    float padding;
};

// 群れ全体のパラメータ
cbuffer CbBoidsParams : register(b10)
{
    float3 TargetPosition;
    float TargetWeight;
    
    float SeparationWeight;
    float AlignmentWeight;
    float CohesionWeight;
    float PerceptionRadius;
    
    float MaxSpeed;
    float MaxForce;
    int BoidsCount;
    float DeltaTime;
};