#ifndef PLANE_H
#define PLANE_H

#include "object3d.hpp"
#include <vecmath.h>
#include <cmath>

// TODO: Implement Plane representing an infinite plane
// function: ax+by+cz=d
// choose your representation , add more fields and fill in the functions

class Plane : public Object3D {
public:
    Plane() {
        this->normal = Vector3f(0, 1, 0);
        this->d = 0;
        // 默认为y=0的水平面
    }

    Plane(const Vector3f &normal, float d, Material *m) : Object3D(m) {
        this->normal = normal;
        this->normal.normalize(); // 确保法向量是单位向量
        this->d = -d;
    }

    ~Plane() override = default;

    bool intersect(const Ray &r, Hit &h, float tmin) override {
        // 计算光源到交点距离t
        if (fabs(Vector3f::dot(normal, r.getDirection())) < 1e-6f) {
            return false; // 光线平行于平面，无交点
        }
        float t = -(Vector3f::dot(normal, r.getOrigin()) + d) / Vector3f::dot(normal, r.getDirection());

        // 根据光线打到哪个面，计算交点处的法向量
        Vector3f hit_normal = normal;
        if (Vector3f::dot(normal, r.getDirection()) < 0) {
            hit_normal = normal; // 打到正面，法向量不变
        } else {
            hit_normal = -normal; // 打到反面，法向量取反
        }

        // 判断t是否在合理范围内, 设置交点
        if (t > tmin && t < h.getT()) {
            h.set(t, material, hit_normal);
            return true;
        }
        // 否则没有交点
        return false;
    }

    float getArea() const override { return 0; } // 无限平面

    Vector3f getNormal() const { return normal; }
    float getD() const { return d; }

protected:
    Vector3f normal;
    float d;
};

#endif //PLANE_H
		

