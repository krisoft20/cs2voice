#include "pch.h"

#include "model/geom.h"

static const double deg = 3.14159265358979323846 / 180.0;

namespace geom {

vec3_t forward(double pitch, double yaw) {
    double cp = cos(pitch * deg), sp = sin(pitch * deg);
    double cy = cos(yaw * deg), sy = sin(yaw * deg);
    return {cp * cy, cp * sy, -sp};
}

vec3_t sub(const vec3_t& a, const vec3_t& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

double dot(const vec3_t& a, const vec3_t& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

double len(const vec3_t& a) {
    return sqrt(dot(a, a));
}

double angle_between(const vec3_t& a, const vec3_t& b) {
    if (len(a) <= 0 || len(b) <= 0)
        return 180.0;
    vec3_t c = {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    return atan2(len(c), dot(a, b)) / deg;
}

double wrap_deg(double a) {
    while (a > 180.0)
        a -= 360.0;
    while (a <= -180.0)
        a += 360.0;
    return a;
}

void to_angles(const vec3_t& dir, double& pitch, double& yaw) {
    double h = sqrt(dir.x * dir.x + dir.y * dir.y);
    yaw = atan2(dir.y, dir.x) / deg;
    pitch = -atan2(dir.z, h) / deg;
}

}
