#ifndef BRDF_H
#define BRDF_H

#include <vecmath.h>
#include <cmath>
#include <cassert>

// === BRDF 抽象基类 ===
// 路径追踪循环只依赖 isDelta() 区分处理方式，
// 不关心具体 BRDF 类型，便于后续新增 Glossy 等。

class BRDF {
public:
    virtual ~BRDF() = default;
    virtual bool isDelta() const = 0;

    // 非 delta 接口
    virtual bool sample(const Vector3f &wo, const Vector3f &N, float r1, float r2,
                        Vector3f &wi, float &pdf) const { return false; }
    virtual Vector3f eval(const Vector3f &wo, const Vector3f &wi, const Vector3f &N) const { return Vector3f::ZERO; }
    virtual float pdf(const Vector3f &wo, const Vector3f &wi, const Vector3f &N) const { return 0.0f; }

    // delta 接口
    virtual Vector3f sampleDelta(const Vector3f &wo, const Vector3f &N,
                                 float currentIOR, float &nextIOR) const {
        nextIOR = currentIOR; return Vector3f::ZERO;
    }
    virtual Vector3f deltaThroughput() const { return Vector3f::ZERO; }
};

// === Lambert 漫反射 ===
class LambertianBRDF : public BRDF {
public:
    explicit LambertianBRDF(const Vector3f &kd) : kd(kd) {}

    bool isDelta() const override { return false; }

    // 余弦加权半球采样，返回 wi 和 pdf
    bool sample(const Vector3f &wo, const Vector3f &N, float r1, float r2,
                Vector3f &wi, float &pdf) const override {
        float phi = 2.0f * M_PI * r1;
        float cosTheta = sqrt(r2);
        float sinTheta = sqrt(1.0f - r2);

        Vector3f local(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);

        Vector3f tangent, bitangent;
        if (fabs(N.x()) > 0.9f)
            tangent = Vector3f::cross(Vector3f(0, 1, 0), N).normalized();
        else
            tangent = Vector3f::cross(Vector3f(1, 0, 0), N).normalized();
        bitangent = Vector3f::cross(N, tangent);

        wi = (tangent * local.x() + bitangent * local.y() + N * local.z()).normalized();
        pdf = cosTheta / M_PI;
        return true;
    }

    // 评估 f_r * cosθ
    Vector3f eval(const Vector3f &wo, const Vector3f &wi, const Vector3f &N) const override {
        float cosT = std::max(0.0f, Vector3f::dot(N, wi));
        return kd * cosT / M_PI;
    }

    // 采样 pdf
    float pdf(const Vector3f &wo, const Vector3f &wi, const Vector3f &N) const override {
        float cosT = std::max(0.0f, Vector3f::dot(N, wi));
        return cosT / M_PI;
    }

    Vector3f kd;
};

// === 完美镜面反射 ===
class SpecularReflectionBRDF : public BRDF {
public:
    SpecularReflectionBRDF(const Vector3f &atten) : atten(atten) {}

    bool isDelta() const override { return true; }

    // 反射方向，nextIOR 不变
    Vector3f sampleDelta(const Vector3f &wo, const Vector3f &N,
                         float currentIOR, float &nextIOR) const override {
        Vector3f I = -wo; // 入射方向 (指向表面)
        nextIOR = currentIOR;
        return (I - 2.0f * Vector3f::dot(N, I) * N).normalized();
    }

    Vector3f deltaThroughput() const override { return atten; }

    Vector3f atten;
};

// === 完美折射 ===
class SpecularTransmissionBRDF : public BRDF {
public:
    SpecularTransmissionBRDF(float ior, const Vector3f &atten) : ior(ior), atten(atten) {}

    bool isDelta() const override { return true; }

    // 折射方向 (或全反射)，更新 nextIOR
    Vector3f sampleDelta(const Vector3f &wo, const Vector3f &N,
                         float currentIOR, float &nextIOR) const override {
        Vector3f I = -wo;
        float eta;

        if (currentIOR == ior) {
            // 离开介质
            eta = currentIOR / 1.0f;
            nextIOR = 1.0f;
        } else {
            // 进入介质
            eta = currentIOR / ior;
            nextIOR = ior;
        }

        float cosI = -Vector3f::dot(I, N);
        float sinT2 = eta * eta * (1.0f - cosI * cosI);

        if (sinT2 > 1.0f) {
            // 全反射
            nextIOR = currentIOR;
            return (I - 2.0f * Vector3f::dot(N, I) * N).normalized();
        }

        float cosT = sqrt(1.0f - sinT2);
        return (eta * I + (eta * cosI - cosT) * N).normalized();
    }

    Vector3f deltaThroughput() const override { return atten; }

    float ior;
    Vector3f atten;
};

#endif // BRDF_H
