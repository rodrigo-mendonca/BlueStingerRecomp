#version 450
// N7b: vertice da IR (native/render/render.h::Vertex), ja em tela como o TA entrega. Os atributos vao SEM
// perspectiva do hardware, multiplicados por 1/w (z do PVR): o fragmento divide pelo 1/w interpolado, a mesma
// correcao de perspectiva do rasterizador de referencia (runtime/pvr/pvr_render.cpp, Raster::draw).
// A profundidade e o proprio 1/w vezes uma potencia de 2 do quadro (exata), comparada como no PVR (maior = perto).
layout(location = 0) in vec2  inPos;     // pixels
layout(location = 1) in float inZ;       // 1/w
layout(location = 2) in vec2  inUv;
layout(location = 3) in uint  inColor;   // ARGB
layout(location = 4) in uint  inOffset;  // ARGB
layout(location = 5) in uint  inSeq;     // ordem de submissao do triangulo (desempate do autosort)
layout(location = 6) in uint  inTri;     // indice do triangulo (cobertura exata)

layout(push_constant) uniform Batch {
    vec2  size;        // largura e altura do alvo
    float depthScale;  // potencia de 2: maior 1/w do quadro <= 1
    uint  flags;
    ivec4 clip;        // recorte "fora": x0, y0, x1, y1 (inclusivo)
    uvec2 texSize;
    uint  ptRef;
    uint  blend;       // fonte (bits 5-3) e destino (bits 2-0) do TSP
    uint  clampMin, clampMax;
    float depthBias;   // subtraido de 1/w antes da escala (quadros com 1/w negativo)
    float invScale;    // 1 / escala da resolucao interna: pixel do alvo -> coordenadas do jogo (1 no modo fiel)
} pc;

layout(location = 0) noperspective out float vZ;
layout(location = 1) noperspective out vec2  vUvZ;
layout(location = 2) noperspective out vec4  vColZ;
layout(location = 3) noperspective out vec3  vOffZ;
layout(location = 4) flat out vec4 fCol;
layout(location = 5) flat out vec3 fOff;
layout(location = 6) flat out uint fSeq;
layout(location = 7) flat out uint fTri;

vec4 argb(uint c) {  // c / 255, como o decodificador do TA
    return vec4(float((c >> 16) & 0xFFu), float((c >> 8) & 0xFFu), float(c & 0xFFu), float(c >> 24)) / 255.0;
}

void main() {
    gl_Position = vec4(inPos / pc.size * 2.0 - 1.0, (inZ - pc.depthBias) * pc.depthScale, 1.0);
    const vec4 c = argb(inColor), o = argb(inOffset);
    vZ    = inZ;
    vUvZ  = inUv * inZ;
    vColZ = c * inZ;
    vOffZ = o.rgb * inZ;
    fCol  = c;  // sombreamento plano: a CPU ja pos a cor do ultimo vertice nos tres
    fOff  = o.rgb;
    fSeq  = inSeq;
    fTri  = inTri;
}
