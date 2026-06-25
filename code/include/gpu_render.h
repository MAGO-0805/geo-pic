#ifndef GPU_RENDER_H
#define GPU_RENDER_H

#include <vector>
#include "vecmath.h"

// === GPU 展平几何体 ===
struct GPUSphere {
    float cx, cy, cz, r;
    int mat_id;
};

struct GPUTriangle {
    float v0x, v0y, v0z;
    float v1x, v1y, v1z;
    float v2x, v2y, v2z;
    int mat_id;
};

// === GPU 展平材质 ===
enum GPUMatType { GPU_DIFFUSE = 0, GPU_REFLECTIVE, GPU_REFRACTIVE, GPU_EMISSIVE, GPU_GLOSSY };

struct GPUMaterial {
    float kd_r, kd_g, kd_b;     // diffuse / albedo
    float F0_r, F0_g, F0_b;     // specular F0 (glossy) / emission (emissive)
    float atten_r, atten_g, atten_b; // attenuation (reflective/refractive)
    float ior, roughness;
    int type;
    int hasFresnel;
};

// === 相机 ===
struct GPUCamera {
    float cx, cy, cz;    // position
    float dx, dy, dz;    // direction
    float ux, uy, uz;    // up
    float hx, hy, hz;    // horizontal
    float fx, fy;        // focal lengths
    int w, h;            // resolution
};

// === 场景展平 ===
struct GPUScene {
    std::vector<GPUSphere> spheres;
    std::vector<GPUTriangle> triangles;
    std::vector<GPUMaterial> materials;
    GPUCamera cam;
    float bg_r, bg_g, bg_b;
};

// 从 SceneParser 展平为 GPU 可用的扁平数据
GPUScene flattenScene(class SceneParser &parser, class Group *group);

// GPU 渲染入口，mode = "mis" / "brdf" / "nee"
void gpuRender(const GPUScene &scene, float *output, int samples, const char *mode);

#endif
