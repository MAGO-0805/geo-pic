#include "mesh.hpp"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cstdlib>
#include <utility>
#include <sstream>

bool Mesh::intersect(const Ray &r, Hit &h, float tmin) {
    bool result = false;
    _bvh.intersect(r, tmin, h.getT(), [&](int triId) {
        if (triId < 0 || triId >= (int)t.size()) return false;
        Triangle triangle(v[t[triId][0]], v[t[triId][1]], v[t[triId][2]], material);
        triangle.normal = n[triId];
        if (triangle.intersect(r, h, tmin)) { result = true; return true; }
        return false;
    });
    return result;
}

Mesh::Mesh(const char *filename, Material *material) : Object3D(material) {

    // Optional: Use tiny obj loader to replace this simple one.
    std::ifstream f;
    f.open(filename);
    if (!f.is_open()) {
        std::cout << "Cannot open " << filename << "\n";
        return;
    }
    std::string line;
    std::string vTok("v");
    std::string fTok("f");
    std::string texTok("vt");
    char bslash = '/', space = ' ';
    std::string tok;
    int texID;
    while (true) {
        std::getline(f, line);
        if (f.eof()) {
            break;
        }
        if (line.size() < 3) {
            continue;
        }
        if (line.at(0) == '#') {
            continue;
        }
        std::stringstream ss(line);
        ss >> tok;
        if (tok == vTok) {
            Vector3f vec;
            ss >> vec[0] >> vec[1] >> vec[2];
            v.push_back(vec);
        } else if (tok == fTok) {
            if (line.find(bslash) != std::string::npos) {
                std::replace(line.begin(), line.end(), bslash, space);
                std::stringstream facess(line);
                facess >> tok;
                std::vector<int> vids;
                int vid, tid;
                while (facess >> vid >> tid) {
                    vids.push_back(vid - 1);
                }
                for (int ii = 1; ii + 1 < (int)vids.size(); ii++) {
                    TriangleIndex trig;
                    trig[0] = vids[0];
                    trig[1] = vids[ii];
                    trig[2] = vids[ii + 1];
                    t.push_back(trig);
                }
            } else {
                std::vector<int> vids;
                int vid;
                while (ss >> vid) {
                    vids.push_back(vid - 1);
                }
                for (int ii = 1; ii + 1 < (int)vids.size(); ii++) {
                    TriangleIndex trig;
                    trig[0] = vids[0];
                    trig[1] = vids[ii];
                    trig[2] = vids[ii + 1];
                    t.push_back(trig);
                }
            }
        } else if (tok == texTok) {
            Vector2f texcoord;
            ss >> texcoord[0];
            ss >> texcoord[1];
        }
    }
    computeNormal();
    buildBVH();

    f.close();
}

void Mesh::computeNormal() {
    n.resize(t.size());
    for (int triId = 0; triId < (int) t.size(); ++triId) {
        TriangleIndex& triIndex = t[triId];
        Vector3f a = v[triIndex[1]] - v[triIndex[0]];
        Vector3f b = v[triIndex[2]] - v[triIndex[0]];
        b = Vector3f::cross(a, b);
        n[triId] = b / b.length();
    }
    // compute per-vertex normals by averaging adjacent face normals
    vn.resize(v.size(), Vector3f::ZERO);
    for (int triId = 0; triId < (int) t.size(); ++triId) {
        TriangleIndex& triIndex = t[triId];
        for (int j = 0; j < 3; ++j) {
            vn[triIndex[j]] += n[triId];
        }
    }
    for (auto &vn_i : vn) {
        float len = vn_i.length();
        if (len > 1e-6f) vn_i = vn_i / len;
    }
}

void Mesh::buildBVH() {
    int triCount = (int)t.size();
    if (triCount == 0) return;

    _triCenters.resize(triCount);
    _triMins.resize(triCount);
    _triMaxs.resize(triCount);

    for (int i = 0; i < triCount; i++) {
        Vector3f v0 = v[t[i][0]];
        Vector3f v1 = v[t[i][1]];
        Vector3f v2 = v[t[i][2]];

        Vector3f bmin = Vector3f(std::min(std::min(v0.x(), v1.x()), v2.x()),
                                  std::min(std::min(v0.y(), v1.y()), v2.y()),
                                  std::min(std::min(v0.z(), v1.z()), v2.z()));
        Vector3f bmax = Vector3f(std::max(std::max(v0.x(), v1.x()), v2.x()),
                                  std::max(std::max(v0.y(), v1.y()), v2.y()),
                                  std::max(std::max(v0.z(), v1.z()), v2.z()));
        _triCenters[i] = (bmin + bmax) * 0.5f;
        _triMins[i] = bmin;
        _triMaxs[i] = bmax;
    }

    _bvh.build(_triCenters, _triMins, _triMaxs);
}
