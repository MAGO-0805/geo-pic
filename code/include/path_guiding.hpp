#ifndef PATH_GUIDING_HPP
#define PATH_GUIDING_HPP

#include <vector>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vecmath.h>

class Group;

// ============================================================================
// DirectionalHistogram — 单个空间格子的半球方向分布
// ============================================================================
// 方向在切线空间中用 (θ, φ) 参数化:
//   θ ∈ [0, π/2]:  法线与方向的夹角 (cosθ = dot(N, dir))
//   φ ∈ [0, 2π):    切线平面内的方位角
//
// 每个 bin 累积入射 radiance 的 luminance 和样本数，归一化后构建 CDF
// ============================================================================

class DirectionalHistogram {
public:
    DirectionalHistogram(int thetaRes = 8, int phiRes = 16);

    // 记录一个样本：direction=(θ, φ), weight=luminance of incident radiance
    void record(float theta, float phi, float weight);

    // 归一化：将累积的 radiance 转换为概率分布，构建 CDF
    void normalize();

    // 从分布中采样方向，返回 (θ, φ, pdf)
    bool sample(float r1, float r2, float &theta, float &phi, float &pdf) const;

    // 查询给定方向的 pdf（基于当前归一化后的分布）
    float pdf(float theta, float phi) const;

    bool isNormalized() const { return _normalized; }

    // 访问器，供 GuidingDistribution::dumpStats 使用
    int thetaRes() const { return _thetaBins; }
    int phiRes()   const { return _phiBins; }
    float totalSum() const { return _totalSum; }
    const std::vector<std::vector<float>>& bins() const { return _sum; }

private:
    int _thetaBins, _phiBins;
    std::vector<std::vector<float>> _sum;    // 累积 luminance
    std::vector<std::vector<int>>   _count;  // 样本数
    float _totalSum;
    int   _totalCount;
    bool  _normalized;

    // CDF 结构（normalize 后构建）
    std::vector<float>              _marginalCDF;   // θ 边缘 CDF
    std::vector<std::vector<float>> _conditionalCDF; // 每个 θ bin 的条件 φ CDF

    float binSolidAngle(int ti) const;   // 第 ti 个 θ bin 的累计立体角
    float binPDF(int ti, int pi) const;  // 单个 bin 的 pdf per solid angle
    void binFromThetaPhi(float theta, float phi, int &ti, int &pi) const;
};

// ============================================================================
// GuidingDistribution — 空间-方向分布（SD-tree 的简化版：均匀空间网格）
// ============================================================================

class GuidingDistribution {
public:
    GuidingDistribution(const Vector3f &bboxMin, const Vector3f &bboxMax,
                        int resX, int resY, int resZ,
                        int thetaBins, int phiBins);

    // 训练：在位置 pos 处，从方向 wi 入射了 radiance（权重 = luminance）
    void record(const Vector3f &pos, const Vector3f &N,
                const Vector3f &wi, float weight);

    // 引导采样：在位置 pos 处，按已学习的分布采样一个出射方向
    bool sample(const Vector3f &pos, const Vector3f &N,
                float r1, float r2, Vector3f &wi, float &pdf) const;

    // 查询 pdf：给定位置和方向，返回 guide 的采样概率密度
    float pdf(const Vector3f &pos, const Vector3f &N,
              const Vector3f &wi) const;

    // 一轮迭代结束后调用：合并线程局部数据 → 归一化所有格子
    void finishIteration();

    // 导出当前分布到文件，供可视化
    void dumpStats(const char *filename) const;

    bool isTrained() const { return _trained; }

    // 从场景树计算包围盒
    static Vector3f computeSceneBounds(Group *scene, Vector3f &outMin, Vector3f &outMax);

private:
    Vector3f _bboxMin, _bboxMax;
    int _resX, _resY, _resZ;
    Vector3f _cellSize;
    int _thetaBins, _phiBins;
    bool _trained;

    std::vector<DirectionalHistogram> _cells;

    int cellIndex(const Vector3f &pos) const;
    void worldToLocal(const Vector3f &N, const Vector3f &wi, float &theta, float &phi) const;
    Vector3f localToWorld(const Vector3f &N, float theta, float phi) const;
};

// ============================================================================
// 场景包围盒计算（递归遍历 Group / Transform / Sphere / Triangle / Mesh）
// ============================================================================
void computeBoundsRecursive(class Object3D *obj, const class Matrix4f &accInv,
                            Vector3f &bmin, Vector3f &bmax, bool &init);

#endif // PATH_GUIDING_HPP
