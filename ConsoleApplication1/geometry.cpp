#include "geometry.h"
#include <cmath>
#include <set>

Path64 RotatePath(const Path64& path, double angle_deg) {
    if (std::abs(angle_deg) < 0.001) return path;
    double rad = angle_deg * MY_PI / 180.0;
    double s = std::sin(rad); double c = std::cos(rad);
    Path64 result;
    for (const auto& pt : path) {
        Point64 p;
        p.x = static_cast<int64_t>(std::round(pt.x * c - pt.y * s));
        p.y = static_cast<int64_t>(std::round(pt.x * s + pt.y * c));
        result.push_back(p);
    }
    Rect64 b = GetBounds(result);
    return TranslatePath(result, -b.left, -b.top);
}

Path64 CreateRectangle(double w_mm, double h_mm) {
    Path64 p;
    int64_t w = static_cast<int64_t>(std::round(w_mm * SCALE));
    int64_t h = static_cast<int64_t>(std::round(h_mm * SCALE));
    Point64 p1; p1.x = 0; p1.y = 0; p.push_back(p1);
    Point64 p2; p2.x = w; p2.y = 0; p.push_back(p2);
    Point64 p3; p3.x = w; p3.y = h; p.push_back(p3);
    Point64 p4; p4.x = 0; p4.y = h; p.push_back(p4);
    return p;
}

Path64 CreateCircle(double r_mm) {
    Path64 path;
    int64_t r = static_cast<int64_t>(std::round(r_mm * SCALE));
    for (int i = 0; i < CIRCLE_SEGMENTS; ++i) {
        double angle = 2.0 * MY_PI * i / CIRCLE_SEGMENTS;
        path.push_back(Point64(
            static_cast<int64_t>(std::round(r * std::cos(angle))),
            static_cast<int64_t>(std::round(r * std::sin(angle)))
        ));
    }
    return TranslatePath(path, r, r);
}

std::vector<int> GenerateSmartAngles(const Path64& path, const std::string& type, double w, double h) {
    if (type == "circle") return { 0 };
    if (type == "rectangle" || type == "square") {
        if (std::abs(w - h) < 0.001) return { 0 };
        return { 0, 90 };
    }

    std::set<int> unique_angles = { 0, 90, 180, 270 };
    size_t n = path.size();
    if (n < 3) return { 0 };

    for (size_t i = 0; i < n; ++i) {
        Point64 p1 = path[i];
        Point64 p2 = path[(i + 1) % n];

        double dx = static_cast<double>(p2.x - p1.x);
        double dy = static_cast<double>(p2.y - p1.y);

        double len = std::sqrt(dx * dx + dy * dy);
        if (len < 5.0 * SCALE) continue;

        double angle_rad = std::atan2(dy, dx);
        double angle_deg = angle_rad * 180.0 / MY_PI;

        int align_x = static_cast<int>(std::round(360.0 - angle_deg)) % 360;
        if (align_x < 0) align_x += 360;

        unique_angles.insert(align_x);
        unique_angles.insert((align_x + 90) % 360);
        unique_angles.insert((align_x + 180) % 360);
        unique_angles.insert((align_x + 270) % 360);
    }
    return std::vector<int>(unique_angles.begin(), unique_angles.end());
}

// ДОДАНО: Математика довжини контуру
double CalculatePerimeter(const Path64& path) {
    if (path.size() < 2) return 0.0;
    double len = 0;
    for (size_t i = 0; i < path.size(); ++i) {
        Point64 p1 = path[i];
        Point64 p2 = path[(i + 1) % path.size()];
        double dx = static_cast<double>(p2.x - p1.x) / SCALE;
        double dy = static_cast<double>(p2.y - p1.y) / SCALE;
        len += std::sqrt(dx * dx + dy * dy);
    }
    return len;
}