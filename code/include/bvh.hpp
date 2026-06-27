// 原创性：课程内容 + 参考PBRT(SAH实现)
#ifndef BVH_HPP
#define BVH_HPP

#include <vector>
#include <vecmath.h>
#include "ray.hpp"
#include "hit.hpp"

// ============================================================================
// BVHNode — 扁平数组中的单个 BVH 节点
// ============================================================================
// 内部节点: left = 左子 index, right = 右子 index (right > left)
// 叶节点:   first = 首个 primitive index, count = primitive 数量
//           通过 left >= 0 判断（内部节点的 right > left >= 0）
struct BVHNode {
    Vector3f bbox_min, bbox_max;
    int left;     // 内部: 左子节点 index; 叶: 首个 primitive
    int right;    // 内部: 右子节点 index; 叶: primitive 数量
};

// ============================================================================
// 辅助: AABB 与光线求交
// ============================================================================
inline bool bbox_intersect(const Vector3f &bmin, const Vector3f &bmax,
                           const Ray &r, float tmin, float tmax) {
    const Vector3f &o = r.getOrigin();
    const Vector3f &d = r.getDirection();
    float t0 = tmin, t1 = tmax;

    for (int i = 0; i < 3; i++) {
        float inv = 1.0f / d[i];
        float tNear = (bmin[i] - o[i]) * inv;
        float tFar  = (bmax[i] - o[i]) * inv;
        if (tNear > tFar) std::swap(tNear, tFar);
        t0 = tNear > t0 ? tNear : t0;
        t1 = tFar  < t1 ? tFar  : t1;
        if (t0 > t1) return false;
    }
    return true;
}

// ============================================================================
// BVH — 二叉树加速结构
// ============================================================================
class BVH {
public:
    BVH() {}

    // 从 primitive 的 AABB 构建
    // mins/maxs: 每个 primitive 的包围盒
    void build(const std::vector<Vector3f> &centers,
               const std::vector<Vector3f> &mins,
               const std::vector<Vector3f> &maxs);

    // 与光线求交：遍历 BVH，对叶节点中的 primitive 调用回调
    // callback(primitive_index) → 返回 true 表示找到更近的交点（可选继续）
    template<typename F>
    bool intersect(const Ray &ray, float tmin, float tmax, F &&callback) const;

    const std::vector<BVHNode> &getNodes() const { return _nodes; }
    const std::vector<int> &getPrimIndices() const { return _primIndices; }
    int rootIndex() const { return _root; }

private:
    std::vector<BVHNode> _nodes;
    std::vector<int> _primIndices;  // BVH 顺序下的 primitive 原始索引
    int _root = -1;

    struct BuildInfo {
        Vector3f center, bmin, bmax;
        int origIdx;
    };

    int buildRecursive(std::vector<BuildInfo> &prims, int begin, int end);
};

// ============================================================================
// intersect 模板实现
// ============================================================================
template<typename F>
bool BVH::intersect(const Ray &ray, float tmin, float tmax, F &&callback) const {
    if (_root < 0) return false;
    bool hit = false;

    int stack[64];
    int sp = 0;
    stack[sp++] = _root;

    while (sp > 0) {
        int ni = stack[--sp];
        const BVHNode &node = _nodes[ni];

        if (!bbox_intersect(node.bbox_min, node.bbox_max, ray, tmin, tmax))
            continue;

        if (node.right <= 0) {
            // 叶节点
            int first = node.left;
            int count = -node.right;
            for (int i = 0; i < count; i++) {
                if (callback(_primIndices[first + i])) hit = true;
            }
        } else {
            // 内部节点: 先推远子节点（后出栈），再推近子节点
            int left = node.left, right = node.right;
            float tl = 0, tr = 0;
            // 简化: 不排序远近，直接推（对性能影响小）
            if (sp + 2 <= 64) {
                stack[sp++] = right;
                stack[sp++] = left;
            }
        }
    }
    return hit;
}

#endif
