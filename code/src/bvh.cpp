// 原创性：课程内容(BVH原理) + 参考PBRT(SAH实现)

#include "bvh.hpp"
#include <algorithm>
#include <cstring>

// ============================================================================
// SAH 参数
// ============================================================================
static const int LEAF_THRESHOLD = 4;        // 叶节点最大 primitive 数
static const float TRAVERSAL_COST = 0.5f;   // 遍历一个节点的相对开销
static const float INTERSECT_COST = 1.0f;   // 求交一个 primitive 的相对开销

void BVH::build(const std::vector<Vector3f> &centers,
                const std::vector<Vector3f> &mins,
                const std::vector<Vector3f> &maxs) {
    int n = (int)centers.size();
    if (n == 0) return;

    std::vector<BuildInfo> prims(n);
    for (int i = 0; i < n; i++) {
        prims[i].center = centers[i];
        prims[i].bmin   = mins[i];
        prims[i].bmax   = maxs[i];
        prims[i].origIdx = i;
    }

    _nodes.clear();
    _root = buildRecursive(prims, 0, n);
}

static void growBBox(const Vector3f &pmin, const Vector3f &pmax,
                      Vector3f &bmin, Vector3f &bmax) {
    bmin = Vector3f(std::min(bmin.x(), pmin.x()),
                    std::min(bmin.y(), pmin.y()),
                    std::min(bmin.z(), pmin.z()));
    bmax = Vector3f(std::max(bmax.x(), pmax.x()),
                    std::max(bmax.y(), pmax.y()),
                    std::max(bmax.z(), pmax.z()));
}

int BVH::buildRecursive(std::vector<BuildInfo> &prims, int begin, int end) {
    int count = end - begin;
    if (count <= 0) return -1;

    // 计算包围盒
    Vector3f bmin = prims[begin].bmin;
    Vector3f bmax = prims[begin].bmax;
    Vector3f cmin = prims[begin].center;
    Vector3f cmax = prims[begin].center;
    for (int i = begin + 1; i < end; i++) {
        growBBox(prims[i].bmin, prims[i].bmax, bmin, bmax);
        cmin = Vector3f(std::min(cmin.x(), prims[i].center.x()),
                        std::min(cmin.y(), prims[i].center.y()),
                        std::min(cmin.z(), prims[i].center.z()));
        cmax = Vector3f(std::max(cmax.x(), prims[i].center.x()),
                        std::max(cmax.y(), prims[i].center.y()),
                        std::max(cmax.z(), prims[i].center.z()));
    }

    // 叶节点
    if (count <= LEAF_THRESHOLD) {
        BVHNode leaf;
        leaf.bbox_min = bmin;
        leaf.bbox_max = bmax;
        leaf.left  = (int)_primIndices.size();  // _primIndices 中的起始位置
        leaf.right = -count;
        for (int i = begin; i < end; i++)
            _primIndices.push_back(prims[i].origIdx);
        int idx = (int)_nodes.size();
        _nodes.push_back(leaf);
        return idx;
    }

    // 选择最长轴
    Vector3f extent = cmax - cmin;
    int axis = 0;
    if (extent.y() > extent.x()) axis = 1;
    if (extent.z() > extent[axis]) axis = 2;

    // 按选定轴排序
    std::sort(prims.begin() + begin, prims.begin() + end,
              [axis](const BuildInfo &a, const BuildInfo &b) {
                  return a.center[axis] < b.center[axis];
              });

    // SAH 分 bin：在 begin+1 到 end-1 之间找最优分割点
    float bestCost = 1e30f;
    int bestSplit = begin + 1;

    Vector3f diag = bmax - bmin;
    float invArea = 1.0f;
    float sa = diag.x() * diag.y() + diag.y() * diag.z() + diag.z() * diag.x();
    if (sa > 1e-10f) invArea = 0.5f / sa;

    // 从左到右扫描，维护左侧包围盒
    Vector3f lbmin = prims[begin].bmin, lbmax = prims[begin].bmax;
    for (int mid = begin + 1; mid < end; mid++) {
        // 右侧包围盒：从 mid 到 end（在循环内实时计算开销大，用预计算优化）
        Vector3f rbmin = prims[mid].bmin, rbmax = prims[mid].bmax;
        for (int i = mid + 1; i < end; i++)
            growBBox(prims[i].bmin, prims[i].bmax, rbmin, rbmax);

        int leftCount = mid - begin;
        int rightCount = end - mid;

        diag = lbmax - lbmin;
        float leftSA = diag.x() * diag.y() + diag.y() * diag.z() + diag.z() * diag.x();
        diag = rbmax - rbmin;
        float rightSA = diag.x() * diag.y() + diag.y() * diag.z() + diag.z() * diag.x();

        float cost = TRAVERSAL_COST +
                     INTERSECT_COST * (leftCount * leftSA + rightCount * rightSA) * invArea;

        if (cost < bestCost) {
            bestCost = cost;
            bestSplit = mid;
        }

        // 扩展左侧包围盒
        growBBox(prims[mid].bmin, prims[mid].bmax, lbmin, lbmax);
    }

    // 与不分割比较
    float noSplitCost = INTERSECT_COST * count;
    if (bestCost >= noSplitCost) {
        BVHNode leaf;
        leaf.bbox_min = bmin;
        leaf.bbox_max = bmax;
        leaf.left  = (int)_primIndices.size();
        leaf.right = -count;
        for (int i = begin; i < end; i++)
            _primIndices.push_back(prims[i].origIdx);
        int idx = (int)_nodes.size();
        _nodes.push_back(leaf);
        return idx;
    }

    // 内部节点
    int leftIdx  = buildRecursive(prims, begin, bestSplit);
    int rightIdx = buildRecursive(prims, bestSplit, end);

    BVHNode internal;
    internal.bbox_min = bmin;
    internal.bbox_max = bmax;
    internal.left  = leftIdx;
    internal.right = rightIdx;
    int idx = (int)_nodes.size();
    _nodes.push_back(internal);
    return idx;
}
