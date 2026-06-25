#ifndef SPHERE_H
#define SPHERE_H

#include "object3d.hpp"
#include <vecmath.h>
#include <cmath>
// 这是球体类

// TODO: Implement functions and add more fields as necessary

class Sphere : public Object3D {
public:
    Sphere() {
        // unit ball at the center
        this->center = Vector3f(0,0,0);
        this->radius = 1.0;
        // material = nullptr; // 父类实现
    }

    Sphere(const Vector3f &center, float radius, Material *material) : Object3D(material) {
        // @DONE
        this->center = center;
        this->radius = radius;
    }


    ~Sphere() override = default;

    bool intersect(const Ray &r, Hit &h, float tmin) override {
        // 使用二次方程求解通用的射线-球体相交，支持方向向量未归一的情况
        Vector3f o_c = r.getOrigin() - center; 
        Vector3f d = r.getDirection();

        // 计算二次项系数
        float A = Vector3f::dot(d, d);
        // 如果方向向量为零，则无交点（防御性检查）
        if (A == 0.0f) return false;
        float B = 2.0f * Vector3f::dot(d, o_c);
        float C = Vector3f::dot(o_c, o_c) - radius * radius;

        // 判别式
        float disc = B * B - 4.0f * A * C;
        if (disc < 0.0f) return false; // 无实根，无交点

        float sqrt_disc = sqrtf(disc);
        // 两个根（参数 t），选择较小的正根
        float t1 = (-B - sqrt_disc) / (2.0f * A);
        float t2 = (-B + sqrt_disc) / (2.0f * A);

        float t = t1;
        if (t < tmin || t > h.getT()) {
            t = t2; // 如果第一个根不合要求，尝试第二个
            if (t < tmin || t > h.getT()) return false; // 两个根都无效
        }

        // 计算交点与法线，并更新 hit
        Vector3f hit_point = r.pointAtParameter(t);
        Vector3f normal = (hit_point - center).normalized();
        // 如果射线从球内出发，法向量方向应取反
        if (Vector3f::dot(d, normal) > 0.0f) {
            normal = -normal;
        }
        h.set(t, material, normal);
        return true;
    }

protected:
    Vector3f center;
    float radius;
    // 父类Object3D还有material这个成员变量，已继承
};


#endif
