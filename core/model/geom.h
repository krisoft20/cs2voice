#pragma once

struct vec3_t {
    double x = 0, y = 0, z = 0;
};

namespace geom {
    vec3_t forward(double pitch, double yaw);
    vec3_t sub(const vec3_t& a, const vec3_t& b);
    double dot(const vec3_t& a, const vec3_t& b);
    double len(const vec3_t& a);
    double angle_between(const vec3_t& a, const vec3_t& b);
    double wrap_deg(double a);
    void to_angles(const vec3_t& dir, double& pitch, double& yaw);
}
