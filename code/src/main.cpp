#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <iostream>
#include <random>
#include <vector>
#include <string>
#include <fstream>
#include <atomic>

#include "scene_parser.hpp"
#include "image.hpp"
#include "camera.hpp"
#include "group.hpp"
#include "light.hpp"
#include "material.hpp"
#include "path_guiding.hpp"

#ifdef USE_CUDA
#include "gpu_render.h"
#endif

using namespace std;

// === 配置 ===
struct Config {
    bool use_path_tracing = true;
    std::string direct_lighting = "mis";
    bool use_omp = true;
    bool use_cuda = false;
    bool use_smooth_shading = true;
    bool use_gamma_correction = true;
    float gamma = 2.2f;
    bool use_fresnel = true;
    bool use_path_guiding = false;
    int  path_guiding_iterations = 4;
    int  path_guiding_grid_res = 8;
    int  path_guiding_theta_bins = 8;
    int  path_guiding_phi_bins = 16;
};

Config loadConfig(const char *path) {
    Config cfg;
    ifstream f(path);
    if (!f.is_open()) return cfg;
    string line;
    while (getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == string::npos) continue;
        string key = line.substr(0, eq);
        string val = line.substr(eq + 1);
        // 去掉行内注释
        size_t comment = val.find('#');
        if (comment != string::npos) val = val.substr(0, comment);
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        val.erase(0, val.find_first_not_of(" \t"));
        val.erase(val.find_last_not_of(" \t") + 1);
        if (key == "direct_lighting") cfg.direct_lighting = val;
        if (key == "use_path_tracing") cfg.use_path_tracing = (val == "true");
        if (key == "use_omp") cfg.use_omp = (val == "true");
        if (key == "use_cuda") cfg.use_cuda = (val == "true");
        if (key == "use_smooth_shading") cfg.use_smooth_shading = (val == "true");
        if (key == "use_gamma_correction") cfg.use_gamma_correction = (val == "true");
        if (key == "gamma") cfg.gamma = (float)atof(val.c_str());
        if (key == "use_fresnel") cfg.use_fresnel = (val == "true");
        if (key == "use_path_guiding") cfg.use_path_guiding = (val == "true");
        if (key == "path_guiding_iterations") cfg.path_guiding_iterations = atoi(val.c_str());
        if (key == "path_guiding_grid_res") cfg.path_guiding_grid_res = atoi(val.c_str());
        if (key == "path_guiding_theta_bins") cfg.path_guiding_theta_bins = atoi(val.c_str());
        if (key == "path_guiding_phi_bins") cfg.path_guiding_phi_bins = atoi(val.c_str());
    }
    return cfg;
}

// === 路径追踪参数 ===
const int SAMPLES = 5000;
const int MAX_DEPTH = 10;
const int RR_DEPTH = 3;
const float EPSILON = 0.001f;
const float AIR_IOR = 1.0f;

inline Vector3f gammaCorrect(const Vector3f &c, float gamma) {
    float inv = 1.0f / gamma;
    return Vector3f(powf(c.x(), inv), powf(c.y(), inv), powf(c.z(), inv));
}

// === 随机数（每线程独立种子） ===
inline float randf() {
    static thread_local mt19937 *rng = nullptr;
    static thread_local uniform_real_distribution<float> *d = nullptr;
    if (!rng) {
        static atomic<int> seed{42};
        rng = new mt19937(seed.fetch_add(100));
        d = new uniform_real_distribution<float>(0.0f, 1.0f);
    }
    return (*d)(*rng);
}

// === Whitted-Style 递归追踪 ===
Vector3f traceWhitted(const Ray &ray, float tmin, Group *scene, SceneParser &parser,
                      int depth, float currentIOR) {
    if (depth > 5) return Vector3f::ZERO;

    Hit hit;
    if (!scene->intersect(ray, hit, tmin)) return parser.getBackgroundColor();

    Material *mat = hit.getMaterial();
    Vector3f hitPoint = ray.pointAtParameter(hit.getT());

    if (mat->getType() == PHONG) {
        Vector3f color(0, 0, 0);
        for (int li = 0; li < parser.getNumLights(); li++) {
            Light *light = parser.getLight(li);
            Vector3f L, lc;
            light->getIllumination(hitPoint, L, lc);
            Ray sr(hitPoint, L);
            Hit sh;
            float md = light->getMaxShadowDistance(hitPoint);
            if (scene->intersect(sr, sh, EPSILON) && sh.getT() < md) continue;
            color += mat->Shade(ray, hit, L, lc);
        }
        return color;
    }

    if (mat->getType() == REFLECTIVE) {
        Vector3f I = ray.getDirection().normalized();
        Vector3f N = hit.getNormal().normalized();
        Vector3f R = Material::reflectDirection(I, N);
        return mat->getAttenuationColor() *
               traceWhitted(Ray(hitPoint, R), EPSILON, scene, parser, depth + 1, currentIOR);
    }

    if (mat->getType() == REFRACTIVE) {
        Vector3f I = ray.getDirection().normalized();
        Vector3f N = hit.getNormal().normalized();
        float ior = mat->getRefractiveIndex();
        float eta = (currentIOR == ior) ? (currentIOR / AIR_IOR) : (currentIOR / ior);
        float nextIOR = (currentIOR == ior) ? AIR_IOR : ior;

        Vector3f T;
        if (Material::refractDirection(I, N, eta, T))
            return mat->getAttenuationColor() *
                   traceWhitted(Ray(hitPoint, T), EPSILON, scene, parser, depth + 1, nextIOR);
        else {
            Vector3f R = Material::reflectDirection(I, N);
            return mat->getAttenuationColor() *
                   traceWhitted(Ray(hitPoint, R), EPSILON, scene, parser, depth + 1, currentIOR);
        }
    }

    return Vector3f::ZERO;
}

// === 路径追踪 ===
Vector3f tracePath(const Ray &firstRay, Group *scene, const Vector3f &bgColor,
                   const vector<Object3D *> &emissives, const Config &cfg,
                   GuidingDistribution *guiding, float guideProb) {
    Vector3f radiance(0, 0, 0);
    Vector3f throughput(1, 1, 1);
    Ray ray = firstRay;
    float currentIOR = AIR_IOR;

    // 上一非 delta 顶点信息，用于 BRDF 击中光源时的 MIS
    Vector3f prevHitPoint, prevWo, prevN;
    BRDF *prevBRDF = nullptr;
    Vector3f prevThroughput(0, 0, 0);

    // Path guiding 延迟记录：收集路径上的非 delta 顶点，路径结束后用完整贡献记录
    const int MAX_RECORDS = MAX_DEPTH;
    struct PathVertex { Vector3f pos, N, wi; };
    PathVertex pendingRecords[MAX_RECORDS];
    Vector3f  radianceAfterNEE[MAX_RECORDS];  // 该顶点 NEE 完成后的 radiance
    int       pendingCount = 0;

    for (int depth = 0; depth < MAX_DEPTH; depth++) {
        if (depth > RR_DEPTH) {
            float p = min(1.0f, max(throughput.x(), max(throughput.y(), throughput.z())));
            if (randf() > p) break;
            throughput = throughput / p;
        }

        Hit hit;
        if (!scene->intersect(ray, hit, EPSILON)) {
            radiance += throughput * bgColor;
            break;
        }

        Material *mat = hit.getMaterial();
        Vector3f hitPoint = ray.pointAtParameter(hit.getT());
        Vector3f N = hit.getNormal().normalized();
        Vector3f wo = -ray.getDirection().normalized();

        // 击中发光体
        if (mat->isEmissive()) {
            if (depth == 0 || cfg.direct_lighting == "brdf") {
                radiance += throughput * mat->getEmission();
            } else if (cfg.direct_lighting != "nee" && prevBRDF) {
                // BRDF 弹射侧 MIS
                Vector3f wi_local = ray.getDirection().normalized();
                float pdf_brdf = prevBRDF->pdf(prevWo, wi_local, prevN);
                Vector3f brdf_val = prevBRDF->eval(prevWo, wi_local, prevN);

                float pdf_light = 0;
                for (Object3D *em : emissives) {
                    if (em->getMaterial() == mat) {
                        float area = em->getArea();
                        if (area > 0) {
                            float dist2 = (hitPoint - prevHitPoint).squaredLength();
                            float cosLight = max(0.0f, Vector3f::dot(N, -wi_local));
                            if (cosLight > 0)
                                pdf_light = (1.0f / area) * dist2 / cosLight;
                        }
                        break;
                    }
                }

                float mis_w = (pdf_brdf * pdf_brdf) / max(1e-6f, pdf_brdf * pdf_brdf + pdf_light * pdf_light);
                if (pdf_brdf > 1e-8f)
                    radiance += prevThroughput * mat->getEmission() * brdf_val * mis_w / pdf_brdf;
            } else {
                // delta 弹射命中光源，方向确定无需 MIS
                radiance += throughput * mat->getEmission();
            }
            break;
        }

        BRDF *brdf = mat->getBRDF();
        Vector3f wi;
        float pdf, nextIOR;

        if (brdf->isDelta()) {
            prevBRDF = nullptr; // delta 顶点不参与 MIS
            float disp = mat->getDispersion();
            if (disp > 0.0f && mat->getType() == REFRACTIVE && currentIOR == AIR_IOR) {
                // 色散: 随机选 R/G/B, 各 1/3 概率, 波长全程保持
                float r = randf();
                int ch;
                float wl_ior;
                if (r < 1.0f/3.0f)      { ch=0; wl_ior = mat->getRefractiveIndex() - disp*0.5f; }
                else if (r < 2.0f/3.0f) { ch=1; wl_ior = mat->getRefractiveIndex(); }
                else                    { ch=2; wl_ior = mat->getRefractiveIndex() + disp*0.5f; }
                float eta = (currentIOR == wl_ior) ? (currentIOR/AIR_IOR) : (currentIOR/wl_ior);
                nextIOR = mat->getRefractiveIndex();
                Vector3f I = ray.getDirection().normalized();
                Vector3f T;
                if (Material::refractDirection(I, N, eta, T))
                    wi = T;
                else
                    wi = Material::reflectDirection(I, N);
                Vector3f atten3 = mat->getAttenuationColor() * 3.0f;
                if      (ch == 0) throughput = throughput * Vector3f(atten3.x(), 0, 0);
                else if (ch == 1) throughput = throughput * Vector3f(0, atten3.y(), 0);
                else              throughput = throughput * Vector3f(0, 0, atten3.z());
            } else {
                wi = brdf->sampleDelta(wo, N, currentIOR, nextIOR);
                throughput = throughput * brdf->deltaThroughput();
            }

            if (!disp && cfg.use_fresnel && mat->getType() == REFRACTIVE && nextIOR != currentIOR) {
                float cosI = fabs(Vector3f::dot(wo, N));
                float R0 = (mat->getRefractiveIndex() - 1.0f) * (mat->getRefractiveIndex() - 1.0f) /
                           ((mat->getRefractiveIndex() + 1.0f) * (mat->getRefractiveIndex() + 1.0f));
                float Fr = R0 + (1.0f - R0) * pow(1.0f - cosI, 5.0f);
                if (randf() < Fr) {
                    Vector3f I = -wo;
                    wi = (I - 2.0f * Vector3f::dot(N, I) * N).normalized();
                    nextIOR = currentIOR;
                }
            }
        } else {
            // 保存上一顶点信息（NEE 之前）
            prevHitPoint = hitPoint;
            prevWo = wo;
            prevN = N;
            prevBRDF = brdf;
            prevThroughput = throughput;

            if (cfg.direct_lighting != "brdf") {
                // NEE: 对每个发光体采样
                for (Object3D *emissive : emissives) {
                Vector3f lp, ln;
                float pdf_area = emissive->sampleSurface(randf(), randf(), lp, ln);
                if (pdf_area <= 0) continue;

                Vector3f lwi = (lp - hitPoint).normalized();
                float dist2 = (lp - hitPoint).squaredLength();
                float cosLight = fabs(Vector3f::dot(ln, -lwi));
                if (cosLight < 1e-6f) continue;

                float pdf_w = pdf_area * dist2 / cosLight;
                float cosRay = max(0.0f, Vector3f::dot(N, lwi));
                if (cosRay <= 0) continue;

                Ray sray(hitPoint, lwi);
                Hit sh;
                float maxT = sqrt(dist2);
                if (scene->intersect(sray, sh, EPSILON)) {
                    if (!sh.getMaterial()->isEmissive())
                        continue;                        // 非发光体遮挡
                }

                Vector3f brdf_val = brdf->eval(wo, lwi, N);
                float pdf_brdf = brdf->pdf(wo, lwi, N);
                float mis_w = (pdf_w * pdf_w) / (pdf_w * pdf_w + pdf_brdf * pdf_brdf + 1e-6f);
                radiance += throughput * emissive->getMaterial()->getEmission() * brdf_val * mis_w / pdf_w;
            }
            } // NEE

            if (cfg.direct_lighting == "nee") break; // 纯 NEE 模式：不弹射

            // 记录 NEE 完成后的 radiance（用于后续计算该顶点方向的真实贡献）
            if (guiding && pendingCount < MAX_RECORDS) {
                radianceAfterNEE[pendingCount] = radiance;
            }

            if (guiding && guiding->isTrained() && guideProb > 0) {
                // MIS between BSDF sampling and path guiding
                float pdf_bsdf, pdf_guide;
                if (randf() < guideProb) {
                    if (!guiding->sample(hitPoint, N, randf(), randf(), wi, pdf_guide)) {
                        brdf->sample(wo, N, randf(), randf(), wi, pdf_bsdf);
                        pdf_guide = guiding->pdf(hitPoint, N, wi);
                    } else {
                        pdf_bsdf = brdf->pdf(wo, wi, N);
                    }
                } else {
                    brdf->sample(wo, N, randf(), randf(), wi, pdf_bsdf);
                    pdf_guide = guiding->pdf(hitPoint, N, wi);
                }
                pdf = (1.0f - guideProb) * pdf_bsdf + guideProb * pdf_guide;
                if (pdf < 1e-6f) break;
                throughput = throughput * brdf->eval(wo, wi, N) / pdf;
            } else {
                brdf->sample(wo, N, randf(), randf(), wi, pdf);
                if (pdf < 1e-6f) break;
                throughput = throughput * brdf->eval(wo, wi, N) / pdf;
            }

            // 延迟记录：暂存顶点信息，等路径结束后用完整 radiance 贡献来记录
            if (guiding && pendingCount < MAX_RECORDS) {
                pendingRecords[pendingCount].pos = hitPoint;
                pendingRecords[pendingCount].N   = N;
                pendingRecords[pendingCount].wi  = wi;
                pendingCount++;
            }

            nextIOR = AIR_IOR;
        }
        ray = Ray(hitPoint, wi);
        currentIOR = nextIOR;
    }

    // 路径结束后，用完整 radiance 贡献记录所有非 delta 顶点
    if (guiding) {
        for (int i = 0; i < pendingCount; i++) {
            // 该顶点 NEE 后的 radiance 为 radianceAfterNEE[i]
            // 路径结束总 radiance - NEE 后 radiance = 该顶点 wi 方向的真实贡献
            float prev = std::max(radianceAfterNEE[i].x(),
                         std::max(radianceAfterNEE[i].y(), radianceAfterNEE[i].z()));
            float cur  = std::max(radiance.x(), std::max(radiance.y(), radiance.z()));
            float w = cur - prev;
            if (w > 0) {
                guiding->record(pendingRecords[i].pos, pendingRecords[i].N,
                                pendingRecords[i].wi, w);
            }
        }
    }
    return radiance;
}

int main(int argc, char *argv[]) {
    string inputFile, outputFile;

    Config cfg = loadConfig("config/settings.conf");

    for (int i = 1; i < argc; i++) {
        if (inputFile.empty()) inputFile = argv[i];
        else outputFile = argv[i];
    }

    if (inputFile.empty() || outputFile.empty()) {
        cout << "Usage: ./PA1 <scene.txt> <output.bmp>" << endl;
        return 1;
    }

    SceneParser parser(inputFile.c_str());
    Camera *camera = parser.getCamera();
    Group *scene = parser.getGroup();
    int w = camera->getWidth(), h = camera->getHeight();
    Image outputImage(w, h);

    if (cfg.use_path_tracing) {
        Vector3f bg = parser.getBackgroundColor();

#ifdef USE_CUDA
        if (cfg.use_cuda) {
            cout << "GPU Path Tracing " << w << "x" << h << " with " << SAMPLES << " spp..." << endl;
            GPUScene gpuScene = flattenScene(parser, scene);
            float *pixels = new float[w * h * 3];
            gpuRender(gpuScene, pixels, SAMPLES, cfg.direct_lighting.c_str(), cfg.use_smooth_shading, cfg.use_fresnel);
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    int i = (y * w + x) * 3;
                    Vector3f c(pixels[i], pixels[i+1], pixels[i+2]);
                    outputImage.SetPixel(x, y, cfg.use_gamma_correction ? gammaCorrect(c, cfg.gamma) : c);
                }
            delete[] pixels;
            cout << " Done!" << endl;
            outputImage.SaveBMP(outputFile.c_str());
            return 0;
        }
#endif

        GuidingDistribution *guiding = nullptr;
        if (cfg.use_path_guiding) {
            // ── 初始化 ──
            Vector3f bmin, bmax;
            GuidingDistribution::computeSceneBounds(scene, bmin, bmax);
            guiding = new GuidingDistribution(bmin, bmax,
                                              cfg.path_guiding_grid_res,
                                              cfg.path_guiding_grid_res,
                                              cfg.path_guiding_grid_res,
                                              cfg.path_guiding_theta_bins,
                                              cfg.path_guiding_phi_bins);

            int totalIters = cfg.path_guiding_iterations;
            int sppPerIter = SAMPLES / totalIters;
            int remSPP     = SAMPLES % totalIters;

            int Iw      = (int)(totalIters * 0.2f);  // 前 20% 纯预热
            int rampEnd = (int)(totalIters * 0.6f);  // 20%~60% 线性爬升到 pMax
            float pMax  = 0.9f;                       // guideProb 最大值 90%

            cout << "direct_lighting=" << cfg.direct_lighting << endl;
            cout << "Path Guiding: " << totalIters << " iters, warmup=" << Iw
                 << ", grid " << cfg.path_guiding_grid_res << "^3, dir "
                 << cfg.path_guiding_theta_bins << "x" << cfg.path_guiding_phi_bins << endl;

            // ── 迭代循环 ──
            for (int iter = 0; iter < totalIters; iter++) {
                int spp = sppPerIter + (iter < remSPP ? 1 : 0);

                // 引导概率：预热期=0, 20%~60%线性爬升到pMax, 后40%保持pMax
                float guideProb = 0;
                if (iter + 1 > Iw) {
                    if (iter + 1 <= rampEnd)
                        guideProb = pMax * (float)(iter + 1 - Iw)
                                          / (float)(rampEnd - Iw);
                    else
                        guideProb = pMax;
                }

                cout << "  iter " << (iter + 1) << "/" << totalIters
                     << "  spp=" << spp
                     << "  guideProb=" << guideProb << " ..." << endl;

                #pragma omp parallel for schedule(dynamic) if(cfg.use_omp)
                for (int y = 0; y < h; y++) {
                    for (int x = 0; x < w; x++) {
                        Vector3f color(0, 0, 0);
                        for (int s = 0; s < spp; s++) {
                            float jx = randf() - 0.5f, jy = randf() - 0.5f;
                            color += tracePath(
                                camera->generateRay(Vector2f(x + jx, y + jy)),
                                scene, bg, parser.getEmissives(), cfg,
                                guiding, guideProb);
                        }
                        // 所有轮都累加原始颜色
                        Vector3f prev = outputImage.GetPixel(x, y);
                        outputImage.SetPixel(x, y, prev + color);
                    }
                    if (y % (h / 10) == 0) cout << "." << flush;
                }
                cout << " Done!" << endl;
                guiding->finishIteration();

                // 导出当前 iter 的分布数据
                {
                    char statsFile[256];
                    snprintf(statsFile, sizeof(statsFile),
                             "output/pg_stats/pg_iter_%d.txt", iter);
                    guiding->dumpStats(statsFile);
                }
            }

            // ── 归一化 + gamma ──
            float invTotal = 1.0f / SAMPLES;
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    Vector3f c = outputImage.GetPixel(x, y) * invTotal;
                    outputImage.SetPixel(x, y,
                        cfg.use_gamma_correction ? gammaCorrect(c, cfg.gamma) : c);
                }
            }
        } else {
            // === 无 path guiding：原始单轮渲染 ===
            cout << "direct_lighting=" << cfg.direct_lighting << endl;
            cout << "Path Tracing " << w << "x" << h << " with " << SAMPLES << " spp...";
            #pragma omp parallel for schedule(dynamic) if(cfg.use_omp)
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    Vector3f color(0, 0, 0);
                    for (int s = 0; s < SAMPLES; s++) {
                        float jx = randf() - 0.5f, jy = randf() - 0.5f;
                        color += tracePath(camera->generateRay(Vector2f(x + jx, y + jy)),
                                           scene, bg, parser.getEmissives(), cfg,
                                           nullptr, 0);
                    }
                    Vector3f c = color / SAMPLES;
                    outputImage.SetPixel(x, y, cfg.use_gamma_correction ? gammaCorrect(c, cfg.gamma) : c);
                }
                if (y % (h / 10) == 0) cout << "." << flush;
            }
            cout << " Done!" << endl;
        }

        delete guiding;
    } else {
        cout << "Whitted-Style " << w << "x" << h << "...";
        #pragma omp parallel for schedule(dynamic) if(cfg.use_omp)
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                Ray r = camera->generateRay(Vector2f(x, y));
                Vector3f c = traceWhitted(r, 0, scene, parser, 0, AIR_IOR);
                outputImage.SetPixel(x, y, cfg.use_gamma_correction ? gammaCorrect(c, cfg.gamma) : c);
            }
            if (y % (h / 10) == 0) cout << "." << flush;
        }
        cout << " Done!" << endl;
    }

    outputImage.SaveBMP(outputFile.c_str());
    return 0;
}
