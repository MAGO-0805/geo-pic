# PA1 光线追踪实现记录

## 参考来源

原始框架基于 Peter Shirley & Keith Morley 的光线追踪教学代码（`ray.hpp` 中注释注明），Phong 模型来自课程第四讲课件，Whitted 模型参考 Turner Whitted 1980 年论文，路径追踪的蒙特卡洛估计与俄罗斯轮盘赌策略参考课程第二讲习题课伪代码。

## Whitted-Style 光线追踪

核心思路：在 PA1 光线投射基础上，将像素着色逻辑从「单次求交 + Phong 本地光照」扩展为「递归追踪 + 全局光照」。不同材质类型的表面在击中后采取不同行为——漫反射面终止递归并计算 Phong 着色，镜面/透明面则计算反射/折射方向后继续追踪，直至击中漫反射面或离开场景。

**材质系统**（`material.hpp`）。原有 `Material` 类仅支持 Phong 参数，现新增 `MaterialType` 枚举区分 PHONG、REFLECTIVE、REFRACTIVE 三种类型。反射材质存储 `attenuationColor`（每次递归乘一次衰减），折射材质额外存储 `refractiveIndex`。反射/折射方向的计算采用标准向量公式，折射需额外检查全反射条件。`PHONG` 构造器签名不变，保证旧场景文件直接兼容。

**递归追踪**（`main.cpp`）。`traceRay()` 函数替代了原有的像素循环内联逻辑。PHONG 分支遍历所有光源累加 Phong 着色并返回；REFLECTIVE 分支计算反射方向后递归调用自身，返回值乘以衰减色；REFRACTIVE 分支根据折射率确定光线方向后递归。关键细节：各几何类的 `hit.getNormal()` 始终指向光线来源侧，因此无法用 `dot(I,N)` 的符号判断进入/离开介质，改用 `currentIOR` 参数在递归栈中传递当前介质折射率——当 `currentIOR == materialIOR` 时判定为离开，反之为进入。

**阴影**（`light.hpp` + `main.cpp`）。为 `Light` 基类新增虚函数 `getMaxShadowDistance(p)`：方向光返回无穷大，点光源返回交点至光源的欧氏距离。PHONG 分支中，计算每个光源贡献前先从交点向光源方向发射 Shadow Ray，若在到达光源前击中任何几何体则跳过该光源。

**场景文件**（`scene_parser.cpp`）。新增 `ReflectiveMaterial` 和 `RefractiveMaterial` 两种 token 的解析。

## 路径追踪

核心思路：Whitted 的 Phong 着色对漫反射面做了「只弹一次就停」的简化，路径追踪则让漫反射面也继续弹射，用蒙特卡洛法估计渲染方程的半球积分。光线只在击中发光体时获得 radiance，途中每次弹射按 BRDF/PDF 更新 throughput。无限递归用俄罗斯轮盘赌截断。

**BRDF 采样接口**（`material.hpp`）。在 `Material` 类上新增三个方法，为后续 glossy BRDF 和 NEE 预留扩展点：
- `sampleBRDF(wo, N, r1, r2) → (wi, pdf)`：重要性采样。PHONG 类型使用余弦加权半球采样，反射/折射属于 delta 分布直接返回 false。
- `evalBRDF(wo, wi, N)`：评估给定方向对的 BRDF 值。PHONG 返回 `diffuseColor/π`。
- `pdfBRDF(wo, wi, N)`：采样概率密度。PHONG 返回 `cosθ/π`。

对于 Lambert 漫反射，蒙特卡洛估计量恰好化简：`(diffuseColor/π) × cosθ / (cosθ/π) = diffuseColor`，throughput 直接乘漫反射颜色即可。

**面光源**（`material.hpp` + `scene_parser.cpp`）。新增 `EMISSIVE` 材质类型，解析 token `EmissiveMaterial { emissionColor }`。路径追踪中击中发光体即终止并累加 `throughput × emissionColor`。Cornell 盒天花板上放置了一个发光小球作为面光源（点光源无法被有限概率击中）。

**路径追踪循环**（`main.cpp`）。用 `throughput` 变量替代递归，循环体内：击中发光体→累加终止；击中 delta 材质→更新方向和 throughput 后 continue；击中漫反射→采样出射方向、按 `throughput × brdf × cosθ / pdf` 更新后继续。俄罗斯轮盘赌从第 4 次弹射起生效，存活概率取 throughput 三通道最大值，存活后 throughput 除以该概率以保证无偏。

**双模式切换**。`main.cpp` 同时保留 Whitted 和 Path 两套逻辑，通过命令行 `--path` 标志切换。同一场景文件可同时包含点光源（供 Whitted 用）和发光材质（供 Path 用），两者互不干扰。

## 涉及文件

| 文件 | 改动性质 |
|------|----------|
| `include/material.hpp` | 新增 REFLECTIVE / REFRACTIVE / EMISSIVE 类型、衰减参数、BRDF 采样接口 |
| `include/light.hpp` | 新增 `getMaxShadowDistance()` |
| `include/scene_parser.hpp` | 新增三个材质解析函数声明 |
| `src/scene_parser.cpp` | 新增 Reflective / Refractive / Emissive 材质解析 |
| `src/main.cpp` | Whitted 递归追踪 + Shadow Ray + 路径追踪采样循环 + 双模式 |
| `testcases/scene_cornell_box.txt` | Whitted/Path 共用对比场景 |
| `run_all.sh` | 自动遍历 testcases/ 下所有 .txt |
