// 原创性：课程内容
#ifndef BRDF_H
#define BRDF_H

#include <vecmath.h>
#include <cmath>
#include <cassert>
#include <algorithm>

class BRDF {
public:
    virtual ~BRDF() = default;
    virtual bool isDelta() const = 0;

    virtual bool sample(const Vector3f &wo, const Vector3f &N, float r1, float r2,
                        Vector3f &wi, float &pdf) const { return false; }
    virtual Vector3f eval(const Vector3f &wo, const Vector3f &wi, const Vector3f &N) const { return Vector3f::ZERO; }
    virtual float pdf(const Vector3f &wo, const Vector3f &wi, const Vector3f &N) const { return 0.0f; }

    virtual Vector3f sampleDelta(const Vector3f &wo, const Vector3f &N,
                                 float currentIOR, float &nextIOR) const {
        nextIOR = currentIOR; return Vector3f::ZERO;
    }
    virtual Vector3f deltaThroughput() const { return Vector3f::ZERO; }
};

// === 工具函数 ===
inline void buildTangentFrame(const Vector3f &N, Vector3f &T, Vector3f &B) {
    if (fabs(N.x()) > 0.9f)
        T = Vector3f::cross(Vector3f(0, 1, 0), N).normalized();
    else
        T = Vector3f::cross(Vector3f(1, 0, 0), N).normalized();
    B = Vector3f::cross(N, T);
}

inline Vector3f toWorld(const Vector3f &local, const Vector3f &T, const Vector3f &B, const Vector3f &N) {
    return (T * local.x() + B * local.y() + N * local.z()).normalized();
}

// === Lambert 漫反射 ===
class LambertianBRDF : public BRDF {
public:
    explicit LambertianBRDF(const Vector3f &kd) : kd(kd) {}
    bool isDelta() const override { return false; }

    bool sample(const Vector3f &wo, const Vector3f &N, float r1, float r2,
                Vector3f &wi, float &pdf) const override {
        float phi = 2.0f * M_PI * r1;
        float cosT = sqrt(r2), sinT = sqrt(1.0f - r2);
        Vector3f local(sinT * cos(phi), sinT * sin(phi), cosT);
        Vector3f T, B; buildTangentFrame(N, T, B);
        wi = toWorld(local, T, B, N);
        pdf = cosT / M_PI;
        return true;
    }

    Vector3f eval(const Vector3f &wo, const Vector3f &wi, const Vector3f &N) const override {
        return kd * std::max(0.0f, Vector3f::dot(N, wi)) / M_PI;
    }

    float pdf(const Vector3f &wo, const Vector3f &wi, const Vector3f &N) const override {
        return std::max(0.0f, Vector3f::dot(N, wi)) / M_PI;
    }

    Vector3f kd;
};

// === 完美镜面反射 ===
class SpecularReflectionBRDF : public BRDF {
public:
    SpecularReflectionBRDF(const Vector3f &atten) : atten(atten) {}
    bool isDelta() const override { return true; }

    Vector3f sampleDelta(const Vector3f &wo, const Vector3f &N,
                         float currentIOR, float &nextIOR) const override {
        Vector3f I = -wo;
        nextIOR = currentIOR;
        return (I - 2.0f * Vector3f::dot(N, I) * N).normalized();
    }
    Vector3f deltaThroughput() const override { return atten; }
    Vector3f atten;
};

// === 完美折射 (支持菲涅尔) ===
class SpecularTransmissionBRDF : public BRDF {
public:
    SpecularTransmissionBRDF(float ior, const Vector3f &atten) : ior(ior), atten(atten), useFresnel(false) {}
    bool isDelta() const override { return true; }

    void setFresnel(bool v) { useFresnel = v; }
    bool hasFresnel() const { return useFresnel; }

    Vector3f sampleDelta(const Vector3f &wo, const Vector3f &N,
                         float currentIOR, float &nextIOR) const override {
        // 该方法总是返回确定方向；菲涅尔反射在 path 循环中通过 resolveFresnel 处理
        Vector3f I = -wo;
        float eta;
        if (currentIOR == ior) { eta = currentIOR / 1.0f; nextIOR = 1.0f; }
        else                    { eta = currentIOR / ior;  nextIOR = ior; }

        float cosI = -Vector3f::dot(I, N);
        float sinT2 = eta * eta * (1.0f - cosI * cosI);
        if (sinT2 > 1.0f) { nextIOR = currentIOR; return (I - 2.0f * Vector3f::dot(N, I) * N).normalized(); }
        float cosT = sqrt(1.0f - sinT2);
        return (eta * I + (eta * cosI - cosT) * N).normalized();
    }

    // 菲涅尔反射率 Schlick 近似
    float fresnelReflectance(float cosI) const {
        float R0 = (ior - 1.0f) * (ior - 1.0f) / ((ior + 1.0f) * (ior + 1.0f));
        return R0 + (1.0f - R0) * pow(1.0f - cosI, 5.0f);
    }

    Vector3f deltaThroughput() const override { return atten; }

    float ior;
    Vector3f atten;
    bool useFresnel;
};

// === Glossy BRDF (Cook-Torrance, GGX) ===
class GlossyBRDF : public BRDF {
public:
    GlossyBRDF(const Vector3f &kd, const Vector3f &F0, float roughness)
            : kd(kd), F0(F0), alpha(std::max(0.001f, roughness * roughness)) {}

    bool isDelta() const override { return false; }

    bool sample(const Vector3f &wo, const Vector3f &N, float r1, float r2,
                Vector3f &wi, float &pdf) const override;

    Vector3f eval(const Vector3f &wo, const Vector3f &wi, const Vector3f &N) const override;

    float pdf(const Vector3f &wo, const Vector3f &wi, const Vector3f &N) const override;

    float pdfSpecular(const Vector3f &wo, const Vector3f &wi, const Vector3f &N) const;

private:
    float D_GGX(float cosH) const {
        float c2 = cosH * cosH, t2 = (1.0f - c2) / c2;
        float a2 = alpha * alpha;
        return a2 / (M_PI * c2 * c2 * (a2 + t2) * (a2 + t2));
    }

    float G1(float cosV) const {
        float t2 = (1.0f - cosV * cosV) / (cosV * cosV);
        return 2.0f / (1.0f + sqrt(1.0f + alpha * alpha * t2));
    }

    float G(const Vector3f &wo, const Vector3f &wi, const Vector3f &N) const {
        float nowo = std::max(0.0f, Vector3f::dot(N, wo));
        float nowi = std::max(0.0f, Vector3f::dot(N, wi));
        return G1(nowo) * G1(nowi);
    }

    Vector3f schlickFresnel(float cosH) const {
        return F0 + (Vector3f(1,1,1) - F0) * pow(1.0f - cosH, 5.0f);
    }

    Vector3f kd, F0;
    float alpha;
};

// 余弦加权采样
inline Vector3f sampleCosineHemisphere(float r1, float r2, const Vector3f &N) {
    float phi = 2.0f * M_PI * r1;
    float cosT = sqrt(r2), sinT = sqrt(1.0f - r2);
    Vector3f T, B; buildTangentFrame(N, T, B);
    return toWorld(Vector3f(sinT * cos(phi), sinT * sin(phi), cosT), T, B, N);
}

// GGX 半向量采样
inline Vector3f sampleGGXHalfVector(float r1, float r2, float alpha, const Vector3f &N) {
    float phi = 2.0f * M_PI * r1;
    float cosT = sqrt((1.0f - r2) / (1.0f + (alpha * alpha - 1.0f) * r2));
    float sinT = sqrt(std::max(0.0f, 1.0f - cosT * cosT));
    Vector3f T, B; buildTangentFrame(N, T, B);
    return toWorld(Vector3f(sinT * cos(phi), sinT * sin(phi), cosT), T, B, N);
}

// === GlossyBRDF 实现 ===
inline bool GlossyBRDF::sample(const Vector3f &wo, const Vector3f &N,
                                float r1, float r2, Vector3f &wi, float &pdf) const {
    float pDiff = (kd.x() + kd.y() + kd.z()) /
                  (kd.x() + kd.y() + kd.z() + F0.x() + F0.y() + F0.z() + 1e-6f);

    if (r1 < pDiff) {
        // 漫反射采样
        float r1b = r1 / pDiff; // 重新映射到 [0,1)
        wi = sampleCosineHemisphere(r1b, r2, N);
        float cosWi = std::max(0.0f, Vector3f::dot(N, wi));
        float pdfD = cosWi / M_PI;
        float pdfS = pdfSpecular(wo, wi, N);
        pdf = pDiff * pdfD + (1.0f - pDiff) * pdfS;
    } else {
        // 镜面反射采样
        float r1b = (r1 - pDiff) / (1.0f - pDiff);
        Vector3f h = sampleGGXHalfVector(r1b, r2, alpha, N);
        float dwh = Vector3f::dot(wo, h);
        if (dwh < 0) h = -h;
        dwh = std::max(0.0f, Vector3f::dot(wo, h));
        wi = (2.0f * dwh * h - wo).normalized();
        float cosWi = std::max(0.0f, Vector3f::dot(N, wi));
        if (cosWi <= 0) { pdf = 0; return false; }
        float cosH = std::max(0.0f, Vector3f::dot(N, h));
        float pdfS = D_GGX(cosH) * cosH / std::max(1e-6f, 4.0f * dwh);
        float pdfD = cosWi / M_PI;
        pdf = pDiff * pdfD + (1.0f - pDiff) * pdfS;
    }
    return pdf > 1e-8f;
}

inline float GlossyBRDF::pdf(const Vector3f &wo, const Vector3f &wi, const Vector3f &N) const {
    float cosWi = std::max(0.0f, Vector3f::dot(N, wi));
    if (cosWi <= 0) return 0;
    float pDiff = (kd.x() + kd.y() + kd.z()) /
                  (kd.x() + kd.y() + kd.z() + F0.x() + F0.y() + F0.z() + 1e-6f);
    float pdfD = cosWi / M_PI;

    Vector3f h = (wo + wi).normalized();
    float cosH = std::max(0.0f, Vector3f::dot(N, h));
    float dwh = std::max(1e-6f, Vector3f::dot(wo, h));
    float pdfS = D_GGX(cosH) * cosH / (4.0f * dwh);

    return pDiff * pdfD + (1.0f - pDiff) * pdfS;
}

inline Vector3f GlossyBRDF::eval(const Vector3f &wo, const Vector3f &wi, const Vector3f &N) const {
    float cosWo = std::max(0.0f, Vector3f::dot(N, wo));
    float cosWi = std::max(0.0f, Vector3f::dot(N, wi));
    if (cosWo <= 0 || cosWi <= 0) return Vector3f::ZERO;

    Vector3f diffuse = kd * cosWi / M_PI;

    Vector3f h = (wo + wi).normalized();
    float cosH = std::max(0.0f, Vector3f::dot(N, h));
    float D = D_GGX(cosH);
    float Gv = G(wo, wi, N);
    Vector3f F = schlickFresnel(std::max(0.0f, Vector3f::dot(wo, h)));
    Vector3f specular = D * Gv * F / std::max(1e-6f, 4.0f * cosWo);

    return diffuse + specular;
}

// pdf specular helper (not virtual, called from sample)
inline float GlossyBRDF::pdfSpecular(const Vector3f &wo, const Vector3f &wi, const Vector3f &N) const {
    Vector3f h = (wo + wi).normalized();
    float cosH = std::max(0.0f, Vector3f::dot(N, h));
    float dwh = std::max(1e-6f, Vector3f::dot(wo, h));
    return D_GGX(cosH) * cosH / (4.0f * dwh);
}

#endif // BRDF_H
