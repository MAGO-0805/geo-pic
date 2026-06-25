#ifndef MESH_H
#define MESH_H

#include <vector>
#include "object3d.hpp"
#include "triangle.hpp"
#include "Vector2f.h"
#include "Vector3f.h"


class Mesh : public Object3D {

public:
    Mesh(const char *filename, Material *m);

    struct TriangleIndex {
        TriangleIndex() {
            x[0] = 0; x[1] = 0; x[2] = 0;
        }
        int &operator[](const int i) { return x[i]; }
        int operator[](const int i) const { return x[i]; }
        // By Computer Graphics convention, counterclockwise winding is front face
        int x[3]{};
    };

    std::vector<Vector3f> v;
    std::vector<TriangleIndex> t;
    std::vector<Vector3f> n;
    bool intersect(const Ray &r, Hit &h, float tmin) override;

    float sampleSurface(float r1, float r2, Vector3f &point, Vector3f &normal) const override {
        // 按面积 CDF 选三角形，再在三角形上采样
        int triCount = (int)t.size();
        if (triCount == 0) return 0;
        std::vector<float> areas(triCount);
        float totalArea = 0;
        for (int i = 0; i < triCount; i++) {
            Vector3f e1 = v[t[i][1]] - v[t[i][0]];
            Vector3f e2 = v[t[i][2]] - v[t[i][0]];
            areas[i] = Vector3f::cross(e1, e2).length() * 0.5f;
            totalArea += areas[i];
        }
        float target = r1 * totalArea;
        float accum = 0;
        int idx = triCount - 1;
        for (int i = 0; i < triCount; i++) {
            accum += areas[i];
            if (accum >= target) { idx = i; break; }
        }
        Triangle tri(v[t[idx][0]], v[t[idx][1]], v[t[idx][2]], material);
        return tri.sampleSurface(r1, r2, point, normal) * areas[idx] / totalArea;
    }

    float getArea() const override {
        float total = 0;
        for (auto &ti : t) {
            Vector3f e1 = v[ti[1]] - v[ti[0]];
            Vector3f e2 = v[ti[2]] - v[ti[0]];
            total += Vector3f::cross(e1, e2).length() * 0.5f;
        }
        return total;
    }

private:

    // Normal can be used for light estimation
    void computeNormal();
};

#endif
