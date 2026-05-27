
/*
 * ==============================================================================
 * ФАЙЛ: geometry.cpp (та geometry.h)
 * ОПИС: Модуль математики та обробки 2D-геометрії.
 *       Містить функції для створення базових фігур (CreateRectangle, CreateCircle),
 *       обертання полігонів (RotatePath) та швидкої перевірки перетинів габаритів
 *       (BoundsOverlap).
 *       Найважливіша функція: GenerateSmartAngles — аналізує грані багатокутника
 *       і математично вираховує ідеальні кути повороту для максимально щільного
 *       пакування складних фігур
 * ==============================================================================
 */
#pragma once
#include "types.h"

Path64 RotatePath(const Path64& path, double angle_deg);
Path64 CreateRectangle(double w_mm, double h_mm);
Path64 CreateCircle(double r_mm);
inline bool BoundsOverlap(const Rect64& a, const Rect64& b) {
    return (a.left < b.right && a.right > b.left && a.top < b.bottom && a.bottom > b.top);
}
std::vector<int> GenerateSmartAngles(const Path64& path, const std::string& type, double w = 0, double h = 0);
double CalculatePerimeter(const Path64& path);
std::string DetectPolygonType(const Path64& path);
// Додайте це до інших оголошень
double GetPathsPerimeter(const Paths64& paths);
double GetBoundaryEdgesLength(const Paths64& paths, int64_t sheetW_scaled, int64_t sheetH_scaled);
double CalculateOptimizedCutLength(const Paths64& placedParts, double sheetWidthMm, double sheetHeightMm);