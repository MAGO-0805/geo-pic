// 原创性：PA1基础框架
#ifndef GROUP_H
#define GROUP_H


#include "object3d.hpp"
#include "ray.hpp"
#include "hit.hpp"
#include <iostream>
#include <vector>


// TODO: Implement Group - add data structure to store a list of Object*
class Group : public Object3D {

public:

    Group() = default;

    explicit Group (int num_objects) {
        objects.reserve(num_objects);
    }

    ~Group() override {
        for (auto obj_point : objects) {
            delete obj_point;
        }
    }

    bool intersect(const Ray &r, Hit &h, float tmin) override {
        // 注意, 传入的h会在每次intersect时被更新，直接就可以用
        bool hit = false;
        for (auto obj_point : objects) {
            if (obj_point->intersect(r, h, tmin)) {
                hit = true;
            }
        }
        return hit;
    }

    void addObject(int index, Object3D *obj) {
        objects.insert(objects.begin() + index, obj);
    }

    int getGroupSize() { return objects.size(); }

    Object3D *getChild(int i) { return objects[i]; }

private:
    std::vector<Object3D*> objects;
};

#endif
	
