# Qwen Code Renderer 仓库速览

你是一个 CPU+GPU 混合路径追踪渲染器，C++14 + CUDA 实现。下面是理解整个仓库所需的关键信息。

## 项目结构

```
PA1/code/
├── include/          # 头文件（CPU 端类定义）
│   ├── camera.hpp   # Camera / PerspectiveCamera
│   ├── object3d.hpp # 几何体基类（虚函数 intersect + sampleSurface + getArea）
│   ├── group.hpp    # 场景树容器，getChild(i) 迭代子节点
│   ├── sphere.hpp   # 球体（含 sampleSurface 球面均匀采样）
│   ├── plane.hpp    # 无限平面（getNormal, getD）
│   ├── triangle.hpp # 三角形（含 sampleSurface 重心坐标采样）
│   ├── mesh.hpp     # 三角网格（含 sampleSurface 按面积 CDF 采样）
│   ├── transform.hpp# 变换包装（存储逆矩阵，getChild, getTransformInv）
│   ├── brdf.hpp     # BRDF 抽象层：Lambertian / SpecularReflection / SpecularTransmission / GlossyBRDF
│   ├── material.hpp # 材质（持有 BRDF*，枚举 PHONG/REFLECTIVE/REFRACTIVE/EMISSIVE/GLOSSY）
│   ├── hit.hpp      # 交点记录（t, material*, normal）
│   ├── ray.hpp      # 射线（origin, direction）
│   ├── light.hpp    # 点光源/方向光（Whitted 用）
│   ├── image.hpp    # BMP/TGA/PPM 读写（SaveBMP 被 main 调用）
│   ├── scene_parser.hpp # 场景文件解析器（递归下降解析自定义 DSL）
│   └── gpu_render.h # GPU 渲染入口 + 展平数据结构
├── src/
│   ├── main.cpp     # 入口（双模式：Whitted / Path Tracing，config 文件驱动）
│   ├── scene_parser.cpp # 解析器实现
│   ├── image.cpp    # 图像 I/O
│   ├── mesh.cpp     # .obj 加载
│   └── gpu_render.cu# GPU 核函数 + 场景展平（nvcc 编译）
├── config/
│   └── settings.conf # 运行时常量开关
├── testcases/       # .txt 场景文件（自定义 DSL）
├── mesh/            # .obj 三角网格
├── deps/vecmath/    # 自研数学库
└── CMakeLists.txt   # 构建系统
```

## 代码准则

1. **CPU 代码已冻结**，只修 Bug 不加功能。新功能全在 GPU 侧实现。
2. **GPUScene 展平**是关键桥接层——遍历 Group/Transform/Sphere/Triangle/Mesh/Plane，转为无指针的扁平数组（`vector<GPUSphere>`, `vector<GPUTriangle>`, `vector<GPUMaterial>`），上传 GPU 显存。
3. **Transform 类存储的是逆矩阵**，`getTransformInv()` 返回逆矩阵，`transformPoint(transform.inverse(), pt)` 将局部点变到世界空间。
4. **Plane 展平**：无限平面转两块大三角（20x20），`getD()` 返回的是 `this->d`（构造函数做了 `this->d = -offset`，即方程 `dot(n,P) + d = 0`）。
5. GPU 核函数 `path_trace_kernel` 用 for 循环模拟递归（非真实递归），`fromDelta` 标记追踪 delta 路径。
6. **不要改已有测例文件**。

## 已实现功能

| 功能 | CPU | GPU |
|------|:--:|:--:|
| Whitted 光线追踪（Phong+递归反射折射+Shadow Ray） | ✅ | ❌ |
| 路径追踪（BRDF+俄罗斯轮盘赌） | ✅ | ✅ |
| Lambert diffuse / Specular reflection / Specular refraction / Glossy GGX | ✅ | ✅ |
| 折射菲涅尔 opt-in（场景文件 `fresnel on`） | ✅ | ✅ |
| NEE 直接光照 + MIS 多重重要性采样 | ✅ | ✅ |
| `direct_lighting` 三模式: `mis` / `brdf` / `nee` | ✅ | ✅ |
| NEE 面光源采样（球体） | ✅ | ✅ |
| NEE 面光源采样（三角形/矩形） | ✅ | ❌ |
| OpenMP CPU 并行 | ✅ | ❌ |
| CUDA GPU 加速 | ❌ | ✅ |

## Config 开关机制

`config/settings.conf` 的 Key-Value 对在 `main.cpp` 的 `loadConfig()` 解析到 `Config` 结构体：

```
use_path_tracing = true         # true=路径追踪, false=Whitted
direct_lighting = mis           # mis / brdf / nee
use_omp = true                  # CPU 多线程
use_cuda = true                 # GPU 加速
```

调用方式：`./build/PA1 scene.txt output.bmp`

## CUDA 开发要点

1. **GPU 渲染入口**：`gpuRender(const GPUScene&, float* output, int samples, const char* mode)`
   - 将展平的场景数据 cudaMemcpy 到设备
   - 启动 `path_trace_kernel<<<grid, block>>>`
   - 核函数内部用 `float3` + 自定义运算符（`operator+-*/` 已定义）
   - mode 转换：`"brdf"→0, "mis"→1, "nee"→2`

2. **GPU RNG**：`gpu_randf(seed)`，PCG 哈希，每像素独立种子

3. **GPU BRDF**：所有 BRDF 逻辑内联在核函数中（无虚函数），通过 `mat.type` 分发：
   - `GPU_DIFFUSE` → 余弦加权采样，throughput *= kd
   - `GPU_REFLECTIVE` → 反射公式，throughput *= attenuation
   - `GPU_REFRACTIVE` → 折射+TIR+菲涅尔(optonal)，throughput *= attenuation
   - `GPU_GLOSSY` → GGX Cook-Torrance，混合采样
   - `GPU_EMISSIVE` → 返回 emission

4. **GPU NEE**：在非 delta 顶点遍历发光体编号数组，采样球面点，固体角转换，可见性检测，MIS 权重

5. **材质映射**：`flattenScene` 中 `mat->getType()` 映射到 `GPUMatType`，`hasFresnel` 从 `mat->hasFresnel()` 读取

6. **Shadow Ray 可见性**：GPU 核函数中只跳过 `GPU_EMISSIVE` 材质（非发光体一律遮挡），与 CPU 版本 `!isEmissive()` 行为一致
