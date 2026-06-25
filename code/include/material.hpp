#ifndef MATERIAL_H
#define MATERIAL_H

#include <cassert>
#include <vecmath.h>

#include "ray.hpp"
#include "hit.hpp"
#include <iostream>

// TODO: Implement Shade function that computes Phong introduced in class.
class Material {
public:

    explicit Material(const Vector3f &d_color, const Vector3f &s_color = Vector3f::ZERO, float s = 0) :
            diffuseColor(d_color), specularColor(s_color), shininess(s) {

    }

    virtual ~Material() = default;

    virtual Vector3f getDiffuseColor() const {
        return diffuseColor;
    }


    Vector3f Shade(const Ray &ray, const Hit &hit,
                   const Vector3f &dirToLight, const Vector3f &lightColor) {
        Vector3f shaded = Vector3f::ZERO; // 返回的是颜色坐标，这里只用处理一条光的作用，累加由main实现
        // 计算必要的单位向量
        Vector3f L = dirToLight.normalized(); // 光线从交点指向光源的单位向量
        Vector3f N = hit.getNormal().normalized(); // 交点处的单位法向量
        Vector3f V = -ray.getDirection(); // 交点处指向相机的单位向量
        Vector3f R = (2 * Vector3f::dot(N, L) * N - L).normalized(); // 交点处的反射光线单位向量
        
        // 计算漫反射分量
        Vector3f diffuse = diffuseColor * std::max(0.0f, Vector3f::dot(N, L));

        // 计算镜面反射分量
        Vector3f specular = specularColor * std::pow(std::max(0.0f, Vector3f::dot(R, V)), shininess);
        
        // 叠加漫反射和镜面反射分量得到最终颜色
        shaded = lightColor * (diffuse + specular); // 注意Vector3f * Vector3f是分量乘，即每个分量分别相乘
        return shaded;
    }

protected:
    Vector3f diffuseColor;
    Vector3f specularColor;
    float shininess;
};


#endif // MATERIAL_H
