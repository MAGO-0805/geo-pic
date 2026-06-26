# PA1 光线追踪实现记录

## 参考来源

原始框架基于 PA1 路径追踪的蒙特卡洛估计与俄罗斯轮盘赌策略参考课程第二讲习题课。

## Whitted-Style 光线追踪

核心思想：光线碰到什么材质就做什么事——漫反射的停、镜面的接着反射、透明的接着折射，每条光线在场景中弹跳直到碰到漫反射或飞出场景。

**材质系统**（`material.hpp`）。原有 `Material` 类仅支持 Phong 参数，现新增 `MaterialType` 枚举区分 PHONG、REFLECTIVE、REFRACTIVE 三种类型。反射材质存储 `attenuationColor`（每次递归乘一次衰减），折射材质额外存储 `refractiveIndex`。反射/折射方向的计算采用标准向量公式，折射需额外检查全反射条件。`PHONG` 构造器签名不变，保证旧场景文件直接兼容。

**递归追踪**（`main.cpp`）。`traceRay()` 函数替代了原有的像素循环内联逻辑。PHONG 分支遍历所有光源累加 Phong 着色并返回；REFLECTIVE 分支计算反射方向后递归调用自身，返回值乘以衰减色；REFRACTIVE 分支根据折射率确定光线方向后递归。关键细节：各几何类的 `hit.getNormal()` 始终指向光线来源侧，因此无法用 `dot(I,N)` 的符号判断进入/离开介质，改用 `currentIOR` 参数在递归栈中传递当前介质折射率——当 `currentIOR == materialIOR` 时判定为离开，反之为进入。

**阴影**（`light.hpp` + `main.cpp`）。为 `Light` 基类新增虚函数 `getMaxShadowDistance(p)`：方向光返回无穷大，点光源返回交点至光源的欧氏距离。PHONG 分支中，计算每个光源贡献前先从交点向光源方向发射 Shadow Ray，若在到达光源前击中任何几何体则跳过该光源。

**场景文件**（`scene_parser.cpp`）。新增 `ReflectiveMaterial` 和 `RefractiveMaterial` 两种 token 的解析。

## 路径追踪

核心思想：不让光线在漫反射面上停下来，而是让它随机继续弹跳，用大量随机样本去"猜"每个像素到底该多亮——猜得越多越接近真实。

**BRDF 采样接口**（`material.hpp`）。在 `Material` 类上新增三个方法，为后续 glossy BRDF 和 NEE 预留扩展点：
- `sampleBRDF(wo, N, r1, r2) → (wi, pdf)`：重要性采样。PHONG 类型使用余弦加权半球采样，反射/折射属于 delta 分布直接返回 false。
- `evalBRDF(wo, wi, N)`：评估给定方向对的 BRDF 值。PHONG 返回 `diffuseColor/π`。
- `pdfBRDF(wo, wi, N)`：采样概率密度。PHONG 返回 `cosθ/π`。

对于 Lambert 漫反射，蒙特卡洛估计量恰好化简：`(diffuseColor/π) × cosθ / (cosθ/π) = diffuseColor`，throughput 直接乘漫反射颜色即可。

**面光源**（`material.hpp` + `scene_parser.cpp`）。新增 `EMISSIVE` 材质类型，解析 token `EmissiveMaterial { emissionColor }`。路径追踪中击中发光体即终止并累加 `throughput × emissionColor`。

**路径追踪循环**（`main.cpp`）。用 `throughput` 变量替代递归，循环体内：击中发光体→累加终止；击中 delta 材质→更新方向和 throughput 后 continue；击中漫反射→采样出射方向、按 `throughput × brdf × cosθ / pdf` 更新后继续。俄罗斯轮盘赌从第 4 次弹射起生效，存活概率取 throughput 三通道最大值，存活后 throughput 除以该概率以保证无偏。

**双模式切换**。`main.cpp` 同时保留 Whitted 和 Path 两套逻辑，通过命令行 `--path` 标志切换。同一场景文件可同时包含点光源（供 Whitted 用）和发光材质（供 Path 用），两者互不干扰。

## 涉及文件

| 文件 | 改动性质 |
|------|----------|
| `include/brdf.hpp` | BRDF 类层次：Lambertian / SpecularReflection / SpecularTransmission / GlossyBRDF |
| `include/material.hpp` | 新增 REFLECTIVE / REFRACTIVE / EMISSIVE / GLOSSY 类型、衰减参数、BRDF 采样接口 |
| `include/light.hpp` | 新增 `getMaxShadowDistance()` |
| `include/scene_parser.hpp` | 新增三个材质解析函数声明 |
| `src/scene_parser.cpp` | 新增 Reflective / Refractive / Emissive 材质解析 |
| `src/main.cpp` | Whitted 递归追踪 + Shadow Ray + 路径追踪采样循环 + 双模式 |
| `testcases/scene_cornell_box.txt` | Whitted/Path 共用对比场景 |
| `run_all.sh` | 自动遍历 testcases/ 下所有 .txt |

## BRDF 解耦重构

核心思想：把材质"能做什么"从"是什么"里拆出来——Material 只管存数据，BRDF 只管算光怎么弹，换一种 BRDF 像换皮肤，渲染循环不动。

**`include/brdf.hpp`** — BRDF 基类声明两套虚接口：非 delta 的 `sample()/eval()/pdf()`，delta 的 `sampleDelta()/deltaThroughput()`。派生类只需覆写对应接口，渲染循环零分支判断：

- `LambertianBRDF`：余弦加权半球采样，`eval = kd·cosθ/π`，`pdf = cosθ/π`
- `SpecularReflectionBRDF`：反射方向由入射方向与法线公式确定，throughput 乘衰减系数
- `SpecularTransmissionBRDF`：根据 currentIOR 判断进入/离开，折射或全反射，throughput 乘衰减系数

路径追踪 delta 分支简化为 `wi = brdf->sampleDelta(...); throughput *= brdf->deltaThroughput()`，非 delta 分支简化为 `brdf->sample(...); throughput *= brdf->eval(...)/pdf`。新增 BRDF 类型只需派生类和 Material 构造器，路径循环不变。

## Glossy BRDF

核心思想：真实表面既不是纯镜子也不是纯哑光，粗糙度控制镜面反射的模糊程度——想象拉丝金属和抛光金属的区别，同一颜色，一个倒影清晰一个倒影散开。

**`include/brdf.hpp`** — `GlossyBRDF` 继承 `BRDF`，`isDelta()=false`。实现 Cook-Torrance 模型：

- **D (法线分布)**：GGX，参数 α = roughness²
- **G (几何遮蔽)**：Smith height-correlated，`G1(v) = 2/(1+√(1+α²tan²θv))`
- **F (菲涅尔)**：Schlick 近似，`F0 + (1-F0)(1-cosθh)⁵`

重要性采样：按 kd 和 F0 的亮度比例随机选 diffuse 或 specular 路径。diffuse 用余弦半球采样，specular 用 GGX 半向量采样（采样 h，计算 `wi = reflect(-wo, h)`）。pdf 为两路混合：`pDiff·pdfDiff + (1-pDiff)·pdfSpec`。支持任意 roughness 的材质表现——roughness=0 近似镜面，roughness=1 近似漫反射。

**测例用法**：
```
GlossyMaterial {
    diffuseColor 0.15 0.15 0.6    ← 固有色
    specularColor 0.9 0.85 0.7    ← 菲涅尔反射率 (F0)，金属感越强越接近 1
    roughness 0.15                 ← 粗糙度，0=镜面 1=漫反射
}
```

## 折射菲涅尔

核心思想：透明物体不只是折射，正面看几乎全透、侧面看几乎全反——车窗从正面看透明，从极斜的角度看变成镜子，菲涅尔公式就是计算这个反射比。

**`include/brdf.hpp`** — `SpecularTransmissionBRDF` 新增 `setFresnel(bool)`。关闭时保持原有纯折射行为。开启后，非全反射时用 Schlick 反射率 `Fr = R0 + (1-R0)(1-cosθ)⁵`（其中 `R0 = ((ior-1)/(ior+1))²`）做俄罗斯轮盘赌：`randf() < Fr` 则走反射，否则走折射。

**`main.cpp`** — 路径追踪 delta 分支后增加检查：若材质 `hasFresnel()` 且本次为成功折射（非全反射），计算 Fr 并随机决定是否改为反射。throughput 不变（反射和折射损耗一致，概率自动抵消）。

**测例用法**：
```
RefractiveMaterial {
    refractiveIndex 1.5
    attenuationColor 1 1 1
    fresnel on          ← 开启菲涅尔，不写则不开启
}
```

**验证实验**：
1. 同一场景渲染两张，`fresnel on` vs 不写，对比玻璃球反射强弱
2. 玻璃球 (IOR=1.5) 与水球 (IOR=1.33) 并排，全反射临界角不同：`θc = arcsin(1/ior)`，IOR 越大越容易全反射，球体内部可见明亮反射区域
3. 掠射角观察：球形边缘入射角接近 90°，`cosθ→0`，`Fr→1`，球体边缘呈现镜面反射

## NEE (Next Event Estimation)

核心思想：路径追踪靠 BRDF 随机弹射"碰运气"击中光源来获得亮度——光源越小，碰中的概率越低，画面就一直噪。NEE 的思路是：每次光线弹到任意一个表面上时，不急着继续随机弹，而是先主动对这场景里每一个发光体采样一个点，从这个表面朝那个点连一条线，检查中间有没有东西挡住——没挡住就直接把光算进去。这样每一跳都有保障地获得直接光照，不再依赖"运气弹中"。

**表面采样接口**（`object3d.hpp` + 各几何类）。为 `Object3D` 新增虚函数 `sampleSurface(r1, r2) → (point, normal, pdf_area)`：
- `Sphere`：球面均匀采样，`pdf = 1/(4πr²)`
- `Triangle`：重心坐标均匀采样，`pdf = 1/面积`
- `Mesh`：按面积 CDF 选三角形后 delegate，`pdf = 1/总面积`
- `Transform`：采样子对象，将局部点/法线变换到世界空间

**固体角转换**。发光体采样得到的是面积 PDF，需转为固体角 PDF 才与 BRDF 采样 PDF 单位一致：`pdf_ω = pdf_A × dist² / cosθ_light`。

**我不理解为什么MIS是一个加分项？NEE只能处理直接光照，必然需要BRDF采样，那就必然需要融合二者呀？**
**MIS 权重**。核心思想：当你能用多种不同方法采样同一个被积函数时，与其选其中一种，不如把多种方法的估计量加权混合，总能比最差的那路好，且不会比最好的那路差太多。具体：NEE 侧和 BRDF 弹射侧使用相同的幂启发式权重函数，对称组合：
- NEE 侧：`w_light = pdf_light² / (pdf_light² + pdf_brdf²)`，累加 `emission × brdf × w_light / pdf_light`
- BRDF 侧：当前非 delta 顶点的 BRDF 弹射若下一跳击中发光体，计算 `w_brdf = pdf_brdf² / (pdf_light² + pdf_brdf²)`，累加 `emission × brdf × w_brdf / pdf_brdf`

实现中保存上一非 delta 顶点的 `hitPoint`、`wo`、`N`、`BRDF*` 和 `throughput`（BRDF 采样前），在下一跳命中发光体时回推 `pdf_light`（通过 `emissive->getArea()` 获取面积 PDF 并固体角转换）。delta 顶点跳过 MIS（其 BRDF 为 Dirac delta，pdf_brdf 对任意有限方向为 0）。

**可见性**。NEE 采样出光源点后发射 Shadow Ray，若在到达光源前击中其他物体则跳过。与 Whitted 的 Shadow Ray 逻辑相同，只是方向由采样决定而非固定指向光源中心。

**路径循环改动**（`main.cpp`）。非 delta 顶点在 BRDF 采样前遍历发光体执行 NEE，MIS 加权的 radiance 直接累加；BRDF 弹射的次级光线击中发光体时不再简单丢弃，改为计算 BRDF 侧 MIS 后累加。delta 顶点将 `prevBRDF` 置空以避免错误 MIS。

**发射体收集**（`scene_parser.cpp`）。解析完成后递归遍历 Group/Transform 树，将 `isEmissive()` 返回 true 的对象指针收集到 `emissives` 向量供渲染循环使用。

**涉及文件**：
| 文件 | 改动 |
|------|------|
| `include/object3d.hpp` | 新增虚函数 `sampleSurface()`, `getArea()` |
| `include/sphere.hpp` | 球面均匀采样实现 |
| `include/triangle.hpp` | 三角形重心坐标采样实现 |
| `include/mesh.hpp` | 面积 CDF + 三角 delegate, const operator[] |
| `include/transform.hpp` | 空间变换 delegate, `getChild()` |
| `include/group.hpp` | `getChild(i)` |
| `include/scene_parser.hpp/cpp` | 发射体收集 + `getEmissives()` |
| `src/main.cpp` | NEE 循环 + MIS + 可见性 + 次级发光休止 |

## OpenMP 并行加速

核心思想：每个像素的路径追踪互相独立——计算像素 A 不需要等像素 B 的结果。让多个 CPU 核心同时各算各的像素，最后拼成一张图。只要保证随机数不打架，结果和单线程一模一样。

**线程安全 RNG**（`main.cpp`）。原 `randf()` 使用全局 `mt19937`，多线程同时调用会导致数据竞争。改为 `thread_local` 惰性初始化：每个线程首次调用时用原子计数器分配独立种子创建自己的 `mt19937` 实例，之后各线程完全独立。

**并行调度**（`main.cpp`）。像素循环外层加 `#pragma omp parallel for schedule(dynamic) if(cfg.use_omp)`。`schedule(dynamic)` 让快线程多干活，避免因路径深度不均导致的负载倾斜。Path 和 Whitted 两路都覆盖。

**编译**（`CMakeLists.txt`）。`find_package(OpenMP)` 自动检测编译器和链接标志。若机器不支持 OpenMP，pragma 被静默忽略，回退单线程。

**开关**（`config/settings.conf`）。`use_omp = true` 开启，`false` 关闭。关闭时与修改前行为完全一致。

## 法向插值（Smooth Shading）

**核心思想**：三角网格本来就是用平面去近似曲面——兔子 200 个三角拼出来，每个三角只有一个法向量，面与面之间有明显的棱，像低面数的游戏模型。法向插值不是增加三角，而是"骗"着色器：在三角内部让法向量渐变，把硬棱抹平。每个顶点存一个"平均法向量"（周围面法向量的平均值），三角内部任意一点的着色法向量 = 三个顶点法向量按重心坐标混合。

**目的和预期**：让 bunny 等网格模型看起来光滑而非马赛克。对独立三角（非 Mesh）无影响，因为三个顶点法向量都是同一个面法向量，插值出来还是面法向量。对平面墙也无影响，原因相同。

**算法思路**：

1. **CPU 端算顶点法向量**：`Mesh::computeNormal()` 先算每个面的法向量 `n[i]`（叉积 + 归一化），再对每个顶点累加所有相邻面的 `n[i]`，最后归一化。结果存进 `vn` 数组——第 k 个顶点的法向量 = 所有包含该顶点的三角面的法向量平均值。

2. **展平时顺带传下去**：`flattenScene` 把 `GPUTriangle` 加 9 个 float（三个顶点的法向量 `n0, n1, n2`）。对独立三角，三个都填面法向量；对 Mesh，从 `vn` 取 + 乘法线变换矩阵（`accInv.transposed()`）变换到世界空间；对 Plane，三个都填平面法向量。**额外修复**：Plane 的三角顶点顺序在不同轴向上导出的几何面法向量方向不一致（有的同向有的反向），逐三角检查 `dot(cross(edge1,edge2), plane_normal)`，反向则交换两顶点，确保几何面法向量始终等于平面法向量。

3. **GPU 求交时插值**：`gpu_intersect_triangle` 命中三角后，用已有的重心坐标 `β, γ` 线性混合：`N = (1-β-γ)·N₀ + β·N₁ + γ·N₂`，归一化即得着色素法向量。

4. **翻转判断的关键细节**：法向量必须朝向光线来源侧才能正确计算余弦项。用**几何面法向量** `fn = cross(e1, e2)` 做翻转判断（而非插值法向量），因为 fn 对三角全局一致，不会因插值在轮廓处偏斜而导致错误翻转。`fn` 的顶点顺序在第 2 步已修正为与数据法向量同向。

**低面数局限**：200 面 bunny 的耳朵尖等部位，相邻面夹角可达 60-90°，顶点法向量取平均后偏了近 45°，既不贴合左面也不贴合右面。插值能在三角内部渐变，但跨边时梯度不连续——笔直色差边界属于 Phong 插值固有局限，不是 bug。提高面数可缓解。

**开关**：`config/settings.conf` 中 `use_smooth_shading = true/false`。`false` 时 kernel 走原几何面法向量路径。

**涉及文件**：

| 文件 | 改动 |
|------|------|
| `include/mesh.hpp` | 新增 `vn` 数组（逐顶点法向量） |
| `src/mesh.cpp` | `computeNormal()` 追加顶点法向量计算（面法向量累加 + 归一化） |
| `include/gpu_render.h` | `GPUTriangle` 新增 9 个 float：`n0x/y/z, n1x/y/z, n2x/y/z`，`gpuRender` 新增 `smoothShading` 参数 |
| `src/gpu_render.cu` | `flattenScene`：三路填充顶点法向量（独立三角=面法向量、Mesh=vn+法线矩阵变换、Plane=平面法向量+顶点顺序修正）。`gpu_intersect_triangle`：新增 9 参数，命中后重心插值。`gpu_scene_intersect`/`path_trace_kernel`：透传 `useSmooth` 标志。翻转判断用几何面法向量 |
| `src/main.cpp` | `Config` 新增 `use_smooth_shading`，`loadConfig` 解析，传递到 `gpuRender` |
| `config/settings.conf` | 新增 `use_smooth_shading = true` |

## 伽马校正（Gamma Correction）

**核心思想**：显示器不是线性的——给它 0.5 的电压，人眼看到的不是"一半亮"，而是更暗（约 0.22 感知亮度）。这是 CRT 时代留下的物理特性，现代显示器也沿用，标准 gamma 值约 2.2。路径追踪算出的是线性辐射度值，直接写进图片会被显示器压暗：暗部死黑、中间调发灰。伽马校正就是写文件前先做逆运算 `pow(x, 1/2.2)`，显示器再压一次 `pow(x, 2.2)`，两者抵消，人眼看到正确的线性亮度。

**预期**：暗部细节不再被压死，中间调更亮，整体画面观感"正常"。

**算法**：逐像素逐通道 `pow(value, 1.0/2.2)`。纯后处理，不影响渲染核心。

**实现**：`main.cpp` 新增 `gammaCorrect(Vector3f)` 内联函数，在 GPU 路径追踪、CPU 路径追踪、Whitted 三条路径的 `SetPixel` 前各套一层，受 `use_gamma_correction` 开关控制。关闭时直接透传原值。

**涉及文件**：

| 文件 | 改动 |
|------|------|
| `src/main.cpp` | `Config` 新增 `use_gamma_correction`，`loadConfig` 解析，新增 `gammaCorrect()` 辅助函数，三条渲染路径 `SetPixel` 前条件应用 |
| `config/settings.conf` | 新增 `use_gamma_correction = true` |
