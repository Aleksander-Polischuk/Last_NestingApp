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

//std::vector<int> GenerateSmartAngles(const Path64& path, const std::string& type, double w, double h) {
//    if (type == "circle") return { 0 };
//    if (type == "rectangle" || type == "square") {
//        if (std::abs(w - h) < 0.001) return { 0 };
//        return { 0, 90 };
//    }
//
//    std::set<int> unique_angles = { 0, 90, 180, 270 };
//    size_t n = path.size();
//    if (n < 3) return { 0 };
//
//    for (size_t i = 0; i < n; ++i) {
//        Point64 p1 = path[i];
//        Point64 p2 = path[(i + 1) % n];
//
//        double dx = static_cast<double>(p2.x - p1.x);
//        double dy = static_cast<double>(p2.y - p1.y);
//
//        double len = std::sqrt(dx * dx + dy * dy);
//        if (len < 5.0 * SCALE) continue;
//
//        double angle_rad = std::atan2(dy, dx);
//        double angle_deg = angle_rad * 180.0 / MY_PI;
//
//        int align_x = static_cast<int>(std::round(360.0 - angle_deg)) % 360;
//        if (align_x < 0) align_x += 360;
//
//        unique_angles.insert(align_x);
//        unique_angles.insert((align_x + 90) % 360);
//        unique_angles.insert((align_x + 180) % 360);
//        unique_angles.insert((align_x + 270) % 360);
//    }
//    return std::vector<int>(unique_angles.begin(), unique_angles.end());
//}

std::vector<int> GenerateSmartAngles(const Path64& path, const std::string& type, double w, double h) {
    std::string realType = DetectPolygonType(path);

    // 1. Для квадратів та кіл — тільки 0 градусів (симетрія)
    if (realType == "circle" || realType == "square") return { 0 };

    // 2. Прямокутники та ромби — 0 та 90
    if (realType == "rectangle" || realType == "rhombus" || realType == "parallelogram") {
        return { 0, 90 };
    }

    // 3. Для трикутників та інших складних полігонів — крок 15 градусів
    std::vector<int> angles;
    for (int a = 0; a < 360; a += 15) {
        angles.push_back(a);
    }
    return angles;
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

// ФАЙЛ: geometry.cpp

std::string DetectPolygonType(const Path64& path) {
    size_t n = path.size();
    if (n < 3) return "unknown";

    // 1. КОЛО ТА ЕЛІПС (якщо багато точок)
    if (n > 20) {
        Rect64 bounds = GetBounds(path);
        double w = static_cast<double>(bounds.Width());
        double h = static_cast<double>(bounds.Height());
        double aspect = (w > h) ? (w / h) : (h / w);

        // Якщо сторони майже рівні — це коло, інакше — еліпс
        if (aspect < 1.05) return "circle";
        return "ellipse";
    }

    // 2. ТРИКУТНИК
    if (n == 3) return "triangle";

    // 3. ЧОТИРИКУТНИКИ (складна перевірка)
    if (n == 4) {
        auto distSq = [](Point64 a, Point64 b) {
            return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y);
            };

        int64_t eps = static_cast<int64_t>(2.0 * SCALE); // Похибка 2 мм
        int64_t s1 = distSq(path[0], path[1]);
        int64_t s2 = distSq(path[1], path[2]);
        int64_t s3 = distSq(path[2], path[3]);
        int64_t s4 = distSq(path[3], path[0]);

        auto isParallel = [](Point64 a, Point64 b, Point64 c, Point64 d) {
            int64_t dx1 = b.x - a.x; int64_t dy1 = b.y - a.y;
            int64_t dx2 = d.x - c.x; int64_t dy2 = d.y - c.y;
            // Векторний добуток для перевірки паралельності
            return std::abs(dx1 * dy2 - dy1 * dx2) < (500 * SCALE);
            };

        bool p1 = isParallel(path[0], path[1], path[2], path[3]);
        bool p2 = isParallel(path[1], path[2], path[3], path[0]);
        bool allSidesEqual = (std::abs(s1 - s2) < eps && std::abs(s2 - s3) < eps && std::abs(s3 - s4) < eps);
        bool oppositeSidesEqual = (std::abs(s1 - s3) < eps && std::abs(s2 - s4) < eps);

        if (p1 && p2) {
            if (allSidesEqual) {
                // Квадрат має бути перпендикулярним (скалярний добуток = 0)
                int64_t dot = (path[1].x - path[0].x) * (path[2].x - path[1].x) + (path[1].y - path[0].y) * (path[2].y - path[1].y);
                return (std::abs(dot) < 500 * SCALE) ? "square" : "rhombus";
            }
            if (oppositeSidesEqual) {
                int64_t dot = (path[1].x - path[0].x) * (path[2].x - path[1].x) + (path[1].y - path[0].y) * (path[2].y - path[1].y);
                return (std::abs(dot) < 500 * SCALE) ? "rectangle" : "parallelogram";
            }
        }
        if (p1 || p2) return "trapezoid";
    }

    // 4. ІНШІ БАГАТОКУТНИКИ
    if (n == 5) return "pentagon";
    if (n == 6) return "hexagon";

    return "polygon";
}

// ==============================================================================
// ОПТИМІЗАЦІЯ ДОВЖИНИ РІЗУ (Common-line cutting + межі аркуша)
// ==============================================================================

double GetPathsPerimeter(const Paths64& paths) {
    double total = 0.0;
    for (const auto& path : paths) {
        total += CalculatePerimeter(path); // Використовуємо вашу вже існуючу функцію
    }
    return total;
}

double GetBoundaryEdgesLength(const Paths64& paths, int64_t sheetW_scaled, int64_t sheetH_scaled) {
    double boundaryLength = 0.0;
    for (const auto& path : paths) {
        if (path.size() < 2) continue;
        for (size_t i = 0; i < path.size(); ++i) {
            Point64 p1 = path[i];
            Point64 p2 = path[(i + 1) % path.size()];

            // Вертикальні лінії на лівому (x=0) або правому (x=W) краю
            if (p1.x == p2.x && (p1.x == 0 || p1.x == sheetW_scaled)) {
                boundaryLength += std::abs(p2.y - p1.y);
            }
            // Горизонтальні лінії на верхньому (y=0) або нижньому (y=H) краю
            else if (p1.y == p2.y && (p1.y == 0 || p1.y == sheetH_scaled)) {
                boundaryLength += std::abs(p2.x - p1.x);
            }
        }
    }
    return boundaryLength;
}

double CalculateOptimizedCutLength(const Paths64& placedParts, double sheetWidthMm, double sheetHeightMm) {
    if (placedParts.empty()) return 0.0;

    // 1. Сума всіх окремих периметрів (де спільні лінії пораховані двічі)
    double totalIndividualPerimeters = GetPathsPerimeter(placedParts) * SCALE; // множимо назад на SCALE для точності перед діленням

    // 2. Об'єднуємо всі деталі в моноліт
    Paths64 unionResult = Union(placedParts, FillRule::NonZero);

    // 3. Периметр моноліту
    double unionPerimeter = GetPathsPerimeter(unionResult) * SCALE;

    // 4. Довжина ліній зі спільним різом (1 прохід)
    double rawCutLength = (totalIndividualPerimeters + unionPerimeter) / 2.0;

    // 5. Віднімаємо краї аркуша
    int64_t wScaled = static_cast<int64_t>(std::round(sheetWidthMm * SCALE));
    int64_t hScaled = static_cast<int64_t>(std::round(sheetHeightMm * SCALE));

    double boundaryLength = GetBoundaryEdgesLength(unionResult, wScaled, hScaled);

    double finalCutLength = rawCutLength - boundaryLength;

    return finalCutLength / SCALE;
}