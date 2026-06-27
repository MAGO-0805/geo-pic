// 原创性：独立实现，参考论文 Path guiding in production

#include "path_guiding.hpp"
#include "object3d.hpp"
#include "group.hpp"
#include "transform.hpp"
#include "sphere.hpp"
#include "triangle.hpp"
#include "mesh.hpp"
#include "plane.hpp"

#include <cmath>
#include <algorithm>
#include <cstdio>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// ============================================================================
// DirectionalHistogram
// ============================================================================

DirectionalHistogram::DirectionalHistogram(int thetaRes, int phiRes)
    : _thetaBins(thetaRes), _phiBins(phiRes),
      _totalSum(0), _totalCount(0), _normalized(false)
{
    _sum.resize(_thetaBins, std::vector<float>(_phiBins, 0.0f));
    _count.resize(_thetaBins, std::vector<int>(_phiBins, 0));
}

void DirectionalHistogram::record(float theta, float phi, float weight) {
    if (weight <= 0) return;
    int ti, pi;
    binFromThetaPhi(theta, phi, ti, pi);
    _sum[ti][pi] += weight;
    _count[ti][pi] += 1;
}

void DirectionalHistogram::normalize() {
    _totalSum = 0;
    _totalCount = 0;
    for (int ti = 0; ti < _thetaBins; ti++) {
        for (int pi = 0; pi < _phiBins; pi++) {
            _totalSum += _sum[ti][pi];
            _totalCount += _count[ti][pi];
        }
    }

    // 构建 θ 边缘 CDF 和条件 φ CDF
    _marginalCDF.resize(_thetaBins);
    _conditionalCDF.resize(_thetaBins, std::vector<float>(_phiBins));

    if (_totalSum <= 0) {
        // 无样本：全部设为均匀分布
        for (int ti = 0; ti < _thetaBins; ti++) {
            _marginalCDF[ti] = (float)(ti + 1) / _thetaBins;
            for (int pi = 0; pi < _phiBins; pi++) {
                _conditionalCDF[ti][pi] = (float)(pi + 1) / _phiBins;
            }
        }
        _normalized = true;
        return;
    }

    float invTotal = 1.0f / _totalSum;

    // 计算每个 θ bin 的 row sum
    std::vector<float> rowSum(_thetaBins, 0);
    for (int ti = 0; ti < _thetaBins; ti++) {
        for (int pi = 0; pi < _phiBins; pi++) {
            rowSum[ti] += _sum[ti][pi];
        }
    }

    // 构建 θ 边缘 CDF
    float accum = 0;
    for (int ti = 0; ti < _thetaBins; ti++) {
        accum += rowSum[ti] * invTotal;
        _marginalCDF[ti] = accum;
    }
    _marginalCDF[_thetaBins - 1] = 1.0f;

    // 构建条件 φ CDF（每个 θ bin 内）
    for (int ti = 0; ti < _thetaBins; ti++) {
        if (rowSum[ti] <= 0) {
            for (int pi = 0; pi < _phiBins; pi++)
                _conditionalCDF[ti][pi] = (float)(pi + 1) / _phiBins;
        } else {
            float invRow = 1.0f / rowSum[ti];
            float accumPhi = 0;
            for (int pi = 0; pi < _phiBins; pi++) {
                accumPhi += _sum[ti][pi] * invRow;
                _conditionalCDF[ti][pi] = accumPhi;
            }
            _conditionalCDF[ti][_phiBins - 1] = 1.0f;
        }
    }

    _normalized = true;
}

bool DirectionalHistogram::sample(float r1, float r2, float &theta, float &phi, float &pdf) const {
    if (!_normalized) return false;

    // 用二分查找 θ bin
    auto it = std::lower_bound(_marginalCDF.begin(), _marginalCDF.end(), r1);
    int ti = (int)(it - _marginalCDF.begin());
    if (ti >= _thetaBins) ti = _thetaBins - 1;

    // 在选中的 θ bin 中查找 φ bin
    auto it2 = std::lower_bound(_conditionalCDF[ti].begin(), _conditionalCDF[ti].end(), r2);
    int pi = (int)(it2 - _conditionalCDF[ti].begin());
    if (pi >= _phiBins) pi = _phiBins - 1;

    // bin 内部均匀采样
    float dTheta = (M_PI * 0.5f) / _thetaBins;
    float dPhi = (2.0f * M_PI) / _phiBins;

    // 在 bin 内重新映射 r1, r2（需要获取 bin 的 CDF 范围）
    // 简化：bin 内均匀采样（对高分辨率网格足够好）
    float thetaMin = ti * dTheta;
    float phiMin = pi * dPhi;

    // 在 bin 内均匀 jitter
    float j1 = r1 - (ti > 0 ? _marginalCDF[ti - 1] : 0);
    float binCDF = _marginalCDF[ti] - (ti > 0 ? _marginalCDF[ti - 1] : 0);
    float fracT = (binCDF > 1e-10f) ? j1 / binCDF : 0.5f;
    theta = thetaMin + fracT * dTheta;

    float j2 = r2 - (pi > 0 ? _conditionalCDF[ti][pi - 1] : 0);
    float binPhiCDF = _conditionalCDF[ti][pi] - (pi > 0 ? _conditionalCDF[ti][pi - 1] : 0);
    float fracP = (binPhiCDF > 1e-10f) ? j2 / binPhiCDF : 0.5f;
    phi = phiMin + fracP * dPhi;

    if (phi > 2.0f * M_PI) phi = 2.0f * M_PI;
    if (theta > M_PI * 0.5f) theta = M_PI * 0.5f;

    pdf = this->pdf(theta, phi);
    if (pdf < 1e-10f) pdf = 1e-10f;
    return true;
}

float DirectionalHistogram::pdf(float theta, float phi) const {
    int ti, pi;
    binFromThetaPhi(theta, phi, ti, pi);
    return binPDF(ti, pi);
}

float DirectionalHistogram::binSolidAngle(int ti) const {
    float dTheta = (M_PI * 0.5f) / _thetaBins;
    float dPhi = (2.0f * M_PI) / _phiBins;
    float thetaMin = ti * dTheta;
    float thetaMax = (ti + 1) * dTheta;
    return (cosf(thetaMin) - cosf(thetaMax)) * dPhi;
}

float DirectionalHistogram::binPDF(int ti, int pi) const {
    float sa = binSolidAngle(ti);
    if (sa <= 0) return 1e-10f;

    // 如果未归一化或无数据，返回均匀 pdf
    if (_totalSum <= 0 || !_normalized) {
        return 1.0f / (2.0f * M_PI);  // 半球均匀分布: 1/(2π)
    }

    float mass = _sum[ti][pi] / _totalSum;
    return mass / sa;
}

void DirectionalHistogram::binFromThetaPhi(float theta, float phi, int &ti, int &pi) const {
    float dTheta = (M_PI * 0.5f) / _thetaBins;
    float dPhi = (2.0f * M_PI) / _phiBins;

    ti = (int)(theta / dTheta);
    if (ti < 0) ti = 0;
    if (ti >= _thetaBins) ti = _thetaBins - 1;

    pi = (int)(phi / dPhi);
    if (pi < 0) pi = 0;
    if (pi >= _phiBins) pi = _phiBins - 1;
}

// ============================================================================
// GuidingDistribution
// ============================================================================

GuidingDistribution::GuidingDistribution(const Vector3f &bboxMin, const Vector3f &bboxMax,
                                         int resX, int resY, int resZ,
                                         int thetaBins, int phiBins)
    : _bboxMin(bboxMin), _bboxMax(bboxMax),
      _resX(resX), _resY(resY), _resZ(resZ),
      _thetaBins(thetaBins), _phiBins(phiBins),
      _trained(false)
{
    // 扩展一点避免边界上的点落在格子外
    Vector3f pad = (bboxMax - bboxMin) * 0.01f;
    _bboxMin -= pad;
    _bboxMax += pad;

    _cellSize = Vector3f(
        (_bboxMax.x() - _bboxMin.x()) / _resX,
        (_bboxMax.y() - _bboxMin.y()) / _resY,
        (_bboxMax.z() - _bboxMin.z()) / _resZ);

    int totalCells = _resX * _resY * _resZ;
    _cells.reserve(totalCells);
    for (int i = 0; i < totalCells; i++)
        _cells.emplace_back(_thetaBins, _phiBins);
}

int GuidingDistribution::cellIndex(const Vector3f &pos) const {
    int ix = (int)((pos.x() - _bboxMin.x()) / _cellSize.x());
    int iy = (int)((pos.y() - _bboxMin.y()) / _cellSize.y());
    int iz = (int)((pos.z() - _bboxMin.z()) / _cellSize.z());

    ix = std::max(0, std::min(ix, _resX - 1));
    iy = std::max(0, std::min(iy, _resY - 1));
    iz = std::max(0, std::min(iz, _resZ - 1));

    return (iz * _resY + iy) * _resX + ix;
}

void GuidingDistribution::worldToLocal(const Vector3f &N, const Vector3f &wi,
                                       float &theta, float &phi) const {
    // 构建与 brdf.hpp 一致的切线坐标系
    Vector3f T, B;
    if (fabs(N.x()) > 0.9f)
        T = Vector3f::cross(Vector3f(0, 1, 0), N).normalized();
    else
        T = Vector3f::cross(Vector3f(1, 0, 0), N).normalized();
    B = Vector3f::cross(N, T);

    float lx = Vector3f::dot(wi, T);
    float ly = Vector3f::dot(wi, B);
    float lz = Vector3f::dot(wi, N);

    // clamp 防止 acos 越界
    if (lz < 0) lz = 0;
    if (lz > 1) lz = 1;

    theta = acosf(lz);
    phi = atan2f(ly, lx);
    if (phi < 0) phi += 2.0f * M_PI;
}

Vector3f GuidingDistribution::localToWorld(const Vector3f &N, float theta, float phi) const {
    Vector3f T, B;
    if (fabs(N.x()) > 0.9f)
        T = Vector3f::cross(Vector3f(0, 1, 0), N).normalized();
    else
        T = Vector3f::cross(Vector3f(1, 0, 0), N).normalized();
    B = Vector3f::cross(N, T);

    float st = sinf(theta);
    float ct = cosf(theta);
    Vector3f local(st * cosf(phi), st * sinf(phi), ct);
    return (T * local.x() + B * local.y() + N * local.z()).normalized();
}

void GuidingDistribution::record(const Vector3f &pos, const Vector3f &N,
                                  const Vector3f &wi, float weight) {
    if (weight <= 0) return;
    int idx = cellIndex(pos);
    float theta, phi;
    worldToLocal(N, wi, theta, phi);
#pragma omp critical(path_guiding_record)
    {
        _cells[idx].record(theta, phi, weight);
    }
}

bool GuidingDistribution::sample(const Vector3f &pos, const Vector3f &N,
                                  float r1, float r2, Vector3f &wi, float &pdf) const {
    int idx = cellIndex(pos);
    float theta, phi;
    if (!_cells[idx].sample(r1, r2, theta, phi, pdf))
        return false;
    wi = localToWorld(N, theta, phi);
    return true;
}

float GuidingDistribution::pdf(const Vector3f &pos, const Vector3f &N,
                                const Vector3f &wi) const {
    int idx = cellIndex(pos);
    float theta, phi;
    worldToLocal(N, wi, theta, phi);
    return _cells[idx].pdf(theta, phi);
}

void GuidingDistribution::finishIteration() {
    for (auto &cell : _cells) {
        cell.normalize();
    }
    _trained = true;
}

void GuidingDistribution::dumpStats(const char *filename) const {
    FILE *fp = fopen(filename, "w");
    if (!fp) return;

    // 元数据
    fprintf(fp, "# grid: %d %d %d\n", _resX, _resY, _resZ);
    fprintf(fp, "# bbox_min: %.4f %.4f %.4f\n", _bboxMin.x(), _bboxMin.y(), _bboxMin.z());
    fprintf(fp, "# bbox_max: %.4f %.4f %.4f\n", _bboxMax.x(), _bboxMax.y(), _bboxMax.z());
    fprintf(fp, "# theta_bins: %d  phi_bins: %d\n", _thetaBins, _phiBins);

    // 每格总权重（空间分布用）
    fprintf(fp, "SPATIAL\n");
    int totalCells = _resX * _resY * _resZ;
    for (int c = 0; c < totalCells; c++) {
        int iz = c / (_resY * _resX);
        int iy = (c % (_resY * _resX)) / _resX;
        int ix = c % _resX;
        fprintf(fp, "%d %d %d %.6f\n", ix, iy, iz, _cells[c].totalSum());
    }

    // 找权重最大的 3 个格子，导出方向直方图
    int topIdx[3] = {-1, -1, -1};
    float topW[3] = {0, 0, 0};
    for (int c = 0; c < totalCells; c++) {
        float w = _cells[c].totalSum();
        if (w > topW[0]) { topW[2]=topW[1]; topIdx[2]=topIdx[1];
                           topW[1]=topW[0]; topIdx[1]=topIdx[0];
                           topW[0]=w; topIdx[0]=c; }
        else if (w > topW[1]) { topW[2]=topW[1]; topIdx[2]=topIdx[1];
                                topW[1]=w; topIdx[1]=c; }
        else if (w > topW[2]) { topW[2]=w; topIdx[2]=c; }
    }

    for (int rank = 0; rank < 3 && topIdx[rank] >= 0; rank++) {
        int c = topIdx[rank];
        int iz = c / (_resY * _resX);
        int iy = (c % (_resY * _resX)) / _resX;
        int ix = c % _resX;
        const auto &bins = _cells[c].bins();
        fprintf(fp, "HISTO %d %d %d %d %d %.6f\n",
                ix, iy, iz, _cells[c].thetaRes(), _cells[c].phiRes(), topW[rank]);
        for (int ti = 0; ti < _cells[c].thetaRes(); ti++) {
            for (int pi = 0; pi < _cells[c].phiRes(); pi++) {
                fprintf(fp, "%.6f%c", bins[ti][pi],
                        (pi + 1 < _cells[c].phiRes()) ? ' ' : '\n');
            }
        }
    }

    fclose(fp);
}

// ============================================================================
// 场景包围盒计算
// ============================================================================

void computeBoundsRecursive(Object3D *obj, const Matrix4f &accInv,
                            Vector3f &bmin, Vector3f &bmax, bool &init) {
    if (!obj) return;

    if (auto *g = dynamic_cast<Group *>(obj)) {
        for (int i = 0; i < g->getGroupSize(); i++)
            computeBoundsRecursive(g->getChild(i), accInv, bmin, bmax, init);
    } else if (auto *t = dynamic_cast<Transform *>(obj)) {
        computeBoundsRecursive(t->getChild(), t->getTransformInv() * accInv, bmin, bmax, init);
    } else if (auto *s = dynamic_cast<Sphere *>(obj)) {
        Matrix4f M = accInv.inverse();
        Vector3f c = (M * Vector4f(s->getCenter(), 1)).xyz();
        // 近似：取最大缩放轴的半径
        float sx = M.getCol(0).xyz().length();
        float sy = M.getCol(1).xyz().length();
        float sz = M.getCol(2).xyz().length();
        float r = s->getRadius() * std::max(std::max(sx, sy), sz);
        auto expand = [&](const Vector3f &p) {
            if (!init) {
                bmin = p; bmax = p; init = true;
            } else {
                bmin = Vector3f(std::min(bmin.x(), p.x()), std::min(bmin.y(), p.y()), std::min(bmin.z(), p.z()));
                bmax = Vector3f(std::max(bmax.x(), p.x()), std::max(bmax.y(), p.y()), std::max(bmax.z(), p.z()));
            }
        };
        expand(c + Vector3f(r, r, r));
        expand(c - Vector3f(r, r, r));
    } else if (auto *tri = dynamic_cast<Triangle *>(obj)) {
        Matrix4f M = accInv.inverse();
        for (int vi = 0; vi < 3; vi++) {
            Vector3f v = (M * Vector4f(tri->getVertex(vi), 1)).xyz();
            if (!init) { bmin = v; bmax = v; init = true; }
            else {
                bmin = Vector3f(std::min(bmin.x(), v.x()), std::min(bmin.y(), v.y()), std::min(bmin.z(), v.z()));
                bmax = Vector3f(std::max(bmax.x(), v.x()), std::max(bmax.y(), v.y()), std::max(bmax.z(), v.z()));
            }
        }
    } else if (auto *mesh = dynamic_cast<Mesh *>(obj)) {
        Matrix4f M = accInv.inverse();
        for (auto &vert : mesh->v) {
            Vector3f v = (M * Vector4f(vert, 1)).xyz();
            if (!init) { bmin = v; bmax = v; init = true; }
            else {
                bmin = Vector3f(std::min(bmin.x(), v.x()), std::min(bmin.y(), v.y()), std::min(bmin.z(), v.z()));
                bmax = Vector3f(std::max(bmax.x(), v.x()), std::max(bmax.y(), v.y()), std::max(bmax.z(), v.z()));
            }
        }
    }
    // Plane 无限大，忽略
}

Vector3f GuidingDistribution::computeSceneBounds(Group *scene, Vector3f &outMin, Vector3f &outMax) {
    bool init = false;
    Vector3f bmin, bmax;
    computeBoundsRecursive(scene, Matrix4f::identity(), bmin, bmax, init);

    if (!init) {
        // fallback
        bmin = Vector3f(-10, -10, -10);
        bmax = Vector3f(10, 10, 10);
    }

    // 添加 10% 边距
    Vector3f diag = bmax - bmin;
    bmin -= diag * 0.1f;
    bmax += diag * 0.1f;

    outMin = bmin;
    outMax = bmax;
    return (bmin + bmax) * 0.5f;
}
