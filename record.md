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

核心思路：原 Material 类通过 type 枚举 + switch 分支实现不同材质的采样和求值，新增材质需同时改 Material、parser、main 三处。重构后将 BRDF 抽象为独立类层次，Material 只持有 BRDF 指针和 Whitted 所需参数，路径追踪循环仅依赖 BRDF 虚接口。

**`include/brdf.hpp`** — BRDF 基类声明两套虚接口：非 delta 的 `sample()/eval()/pdf()`，delta 的 `sampleDelta()/deltaThroughput()`。派生类只需覆写对应接口，渲染循环零分支判断：

- `LambertianBRDF`：余弦加权半球采样，`eval = kd·cosθ/π`，`pdf = cosθ/π`
- `SpecularReflectionBRDF`：反射方向由入射方向与法线公式确定，throughput 乘衰减系数
- `SpecularTransmissionBRDF`：根据 currentIOR 判断进入/离开，折射或全反射，throughput 乘衰减系数

路径追踪 delta 分支简化为 `wi = brdf->sampleDelta(...); throughput *= brdf->deltaThroughput()`，非 delta 分支简化为 `brdf->sample(...); throughput *= brdf->eval(...)/pdf`。新增 BRDF 类型只需派生类和 Material 构造器，路径循环不变。

## Glossy BRDF

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

核心思路：路径追踪靠 BRDF 随机采样「碰运气」击中光源，收敛极慢。NEE 在每个散射路径顶点**主动对所有发光体采样一点**，直接计算光源对该点的贡献，不再依赖概率命中。BRDF 弹射仅负责间接光照，击中发光体时不再重复累加。

**表面采样接口**（`object3d.hpp` + 各几何类）。为 `Object3D` 新增虚函数 `sampleSurface(r1, r2) → (point, normal, pdf_area)`：
- `Sphere`：球面均匀采样，`pdf = 1/(4πr²)`
- `Triangle`：重心坐标均匀采样，`pdf = 1/面积`
- `Mesh`：按面积 CDF 选三角形后 delegate，`pdf = 1/总面积`
- `Transform`：采样子对象，将局部点/法线变换到世界空间

**固体角转换**。发光体采样得到的是面积 PDF，需转为固体角 PDF 才与 BRDF 采样 PDF 单位一致：`pdf_ω = pdf_A × dist² / cosθ_light`。

**MIS 权重**。NEE 侧和 BRDF 弹射侧使用相同的幂启发式权重函数，对称组合：
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
