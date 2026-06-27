# 真实感渲染大作业报告

姓名：孙晨翔
班级：计41
学号：2024010679
___

## 一、功能列表
- Path Guiding
- 复杂网格模型及其求交加速（包围盒和层次结构）
- 色散
- 基于 CUDA 的 GPU 并行加速
- MIS
- 法向插值
- 伽马校正
- 基于 OpenMP 的 CPU 并行加速

## 二、未验收功能
### 1、Path Guiding

正常的路径追踪，从镜头发出的寻找光源的线路在表面上都是随机取向的，如果通往光源的路径很少或者藏在角落里，大部分方向都——碰不到光源、贡献为零。Path Guiding 让渲染器在渲染过程中自己学会"往哪走容易找到光"，学习出一个空间中光源的大致分布。然后在后续采样时，通过一个线性提升的比例来控制以逐渐升高的概率向光源潜在区域探索。

具体而言，对于场景中每个位置，维护一个方向分布，记录"从历史上看，往这个方向弹射带来了多少 radiance"。

分布用一个空间-方向直方图来表示：把场景划分为均匀的 3D 空间网格，每个格子里存一个半球方向直方图（θ, φ 分 bin）。每轮迭代渲染时，路径追踪的结果被记录到这些直方图里。

#### 代码实现

**数据结构**。`DirectionalHistogram` 管理单个空间格子的方向分布：`_sum[θ][φ]` 累积入射 radiance 的亮度权重，`normalize()` 后构建边缘 CDF（θ）和条件 CDF（φ），`sample()` 用二分查找按概率采样方向，`pdf()` 返回给定方向的概率密度不必多说。

`GuidingDistribution` 管理整个场景的空间-方向分布。构造时根据场景包围盒划出均匀空间网格，每个格子里放一个 `DirectionalHistogram`。核心方法就三个：`record(pos, N, wi, weight)` 记录一条光线样本，`sample(pos, N, r1, r2, wi, pdf)` 按已学习的分布采样方向，`finishIteration()` 把一轮所有线程攒下来的数据归一化。

**渲染流程**。开启 path guiding 后，整个渲染从之前一遍跑完变成了多轮迭代。主循环如下：

1. **初始化**：算场景包围盒，建空间网格（6³=216 格，这个设置为参数可以在config中调节），每个格子 4×4=16 个方向 bin（同样可调节）。
2. **每轮迭代**：由于学习到的分布的可信度是逐渐提高的，一个naive的想法是维护一个`guideProb`——前 20% 轮纯粹学习（guideProb=0），20%~60% 从 0 线性爬升到 0.9，后 40% 保持 0.9。然后渲染所有像素。（论文中的实现是从0爬升到0.5，但是由于难于构造到非常合适的测例，为了展现算法的有效性，我极大的提高了这个爬升的速度，并且构造了专门的测例以突出效果，见后面的效果展示环节）
3. **路径追踪**（`tracePath`）：delta 材质方向确定，不参与 guide。对于非 delta：

   - 先做 NEE 直接光照采样，算完这部分 radiance 后记录下当前累计值 `radianceAfterNEE`；
   - 然后采样 continuation 方向：如果 guide 已训练且 `guideProb > 0`，用 MIS 融合 BRDF 采样和 guide 采样；否则纯 BRDF 采样；
   - **延迟记录**：把 `(位置, 法向, 采样的 wi)` 和 `radianceAfterNEE` 暂存到数组里；
   - 路径结束后回填, 对每个暂存的顶点，计算 `weight = max(总radiance) - max(该顶点NEE后radiance)`，这个差值恰好等于**该顶点通过方向 wi 对图像的真正贡献**,包含了 BSDF × 后续入射 radiance ÷ MIS pdf，正是 guide 应该学习的量。


4. **`finishIteration()`**：这轮的路径全部跑完后，把所有直方图归一化。下一轮开始，guide 就可以用来采样了。

#### 测试效果
<div style="display: grid; grid-template-columns: 1fr 1fr; gap: 16px; width: 100%;">
  <div style="aspect-ratio: 1 / 1; overflow: hidden;">
    <img src="report_pic/pg_off_500.bmp" style="width: 100%; height: 100%; object-fit: cover;">
  </div>
  <div style="aspect-ratio: 1 / 1; overflow: hidden;">
    <img src="report_pic/pg_on_500.bmp" style="width: 100%; height: 100%; object-fit: cover;">
  </div>
</div>
唯一的光照从一条缝隙中传来，在同样的迭代轮数下（200SPP），未开启 PG (左图) 的结果明显比开启了PG的结果多出非常多的黑色噪点

另外，为了debug，还写了某一点学到的光照空间分布的可视化图，正好可以用来验证学习的有效性，测例如下：
![alt text](report_pic/tiny_on.bmp)
环境非常黑，只有一个极小的发光球体作为光源，从发光球体出发，向镜头走0.04，向左走0.04，向下走0.05，来到可视化数据的采样点，此时，光源在采样点的“右 前 上”方
<div style="display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 16px; width: 100%;">
  <div style="aspect-ratio: 1 / 1; overflow: hidden;">
    <img src="report_pic/iter_-1_dir_top1.png" style="width: 100%; height: 100%; object-fit: cover;">
  </div>
  <div style="aspect-ratio: 1 / 1; overflow: hidden;">
    <img src="report_pic/iter_251_dir_top1.png" style="width: 100%; height: 100%; object-fit: cover;">
  </div>
  <div style="aspect-ratio: 1 / 1; overflow: hidden;">
    <img src="report_pic/iter_495_dir_top1.png" style="width: 100%; height: 100%; object-fit: cover;">
  </div>
</div>
从左到右分别是初始化均匀分布，第251次迭代、第495次迭代后（共500次迭代，每迭代20spp）的采样点的光源空间分布图，圆上的不同角度即为相对于采样点的极坐标角度，圆心为采样点，越远离圆心的地方，也代表实际空间中远离采样点的地方。光源越有可能分布的地方，颜色越偏亮黄色。图像上方的weight代表这一点累计收到的贡献总数，越高说明采到了光源的样本越多，越可信。

可以清晰看出，光照分布有着显著的变化，并且正确的学习到了位于“右前上方”的光源，而左测几乎完全没有光照。

### 2、包围盒和层次结构

#### 算法思想

大作业文档中提供的网址中，模型动辄上万三角面，每根光线暴力遍历全部三角面的 O(N) 复杂度完全不可接受。BVH是最经典的加速方案：把场景中的几何体用轴对齐包围盒（AABB）一层层包起来，形成一棵二叉树层次。光线求交时先测包围盒——没命中就直接跳过整棵子树，命中了才递归进行具体的求交。显然，对于 N 个三角面，遍历代价从 O(N) 降到约 O(log N)。

#### 代码实现

这个功能是最后实现的，为了避免写崩整个项目，整个 BVH 实现在一个独立的模块中（好在这是可能的），夹在解析scene文件之后，开始路径追踪之前。

核心维护一个 S 形数组存储的二叉树：每个节点存包围盒的最小/最大顶点，以及两个整数区分内部节点（存左右子节点索引）和叶节点（存首个 primitive 的起始位置和数量）。

构建用 Surface Area Heuristic 策略：递归地把 primitive 按最长轴排序，在所有可能的分割点中选使 SAH 代价函数最小的那个。代价函数即 = 遍历开销 + 左右包围盒表面积加权 × 各自的 primitive 数。当 primitive 数量 ≤ 4 或 SAH 认为不分割更优时停止递归。

为了防止递归爆炸，求交则用显式栈迭代遍历替代递归：

```
stack = [root]
while stack:
    node = stack.pop()
    if 光线与node的AABB不相交: continue
    if 叶节点:
        逐一求交该范围内的所有三角面
    else:
        stack.push(左子)  // 先推远的，后推近的
        stack.push(右子)
```

#### 测试效果
用 cornell 盒 + 三个 head.OBJ（共约 1.5 万三角）测试，BVH 版本 10spp 即可得到正确渲染结果（无全黑全白、像素值均匀分布），CPU 端渲染时间大幅缩短。进一步的速度和正确性对比留给后续系统测试。
