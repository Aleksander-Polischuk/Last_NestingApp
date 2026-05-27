/*
 * ==============================================================================
 * ФАЙЛ: main.cpp
 * ОПИС: Головний файл програми.
 * Зв'язує всі модулі (Geometry, IO, Nesting).
 * Реалізовано гібридне сортування для максимальної щільності NFP.
 * ДОДАНО: Стратегія двох проходів для вибору кращої орієнтації аркуша.
 * ==============================================================================
 */

#define NOMINMAX
#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <filesystem>
#include <string>
#include <map>
#include <vector>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

#include "types.h"
#include "geometry.h"
#include "io_utils.h"
#include "nesting.h"

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    // =========================================================================
    // БЛОК 1: ПАРСИНГ АРГУМЕНТІВ КОМАНДНОГО РЯДКА
    // =========================================================================

    fs::path exePath = fs::path(argv[0]);
    fs::path exeDir = exePath.parent_path();
    if (exeDir.empty()) {
        exeDir = fs::current_path();
    }

    fs::path inputPath = (argc > 1) ? fs::path(argv[1]) : (exeDir / "input.json");
    std::string outputJsonName = (argc > 2) ? argv[2] : "output.json";
    fs::path outDir = exeDir;

    if (argc > 3) {
        fs::path targetDir = fs::path(argv[3]);
        if (fs::exists(targetDir) && fs::is_directory(targetDir)) {
            fs::path testFile = targetDir / "test_write_permission.tmp";
            std::ofstream ofs(testFile);
            if (ofs.is_open()) {
                ofs.close();
                fs::remove(testFile);
                outDir = targetDir;
            }
            else {
                std::cerr << "Увага: Немає прав на запис у " << targetDir.string() << ". Збережемо поруч із програмою.\n";
            }
        }
    }

    fs::path finalOutputJson = outDir / outputJsonName;
    std::string svgName = fs::path(outputJsonName).stem().string() + ".svg";
    fs::path finalOutputSvg = outDir / svgName;

    std::cout << "--- НАЛАШТУВАННЯ ШЛЯХІВ ---\n";
    std::cout << "Вхідний JSON:  " << inputPath.string() << "\n";
    std::cout << "Вихідний JSON: " << finalOutputJson.string() << "\n";
    std::cout << "Вихідний SVG:  " << finalOutputSvg.string() << "\n";
    std::cout << "---------------------------\n";

    if (!fs::exists(inputPath)) {
        std::cerr << "КРИТИЧНА ПОМИЛКА: Вхідний файл не знайдено за шляхом: " << inputPath.string() << "\n";
        return 1;
    }

    // =========================================================================
    // БЛОК 2: ОСНОВНА ЛОГІКА ПРОГРАМИ
    // =========================================================================

    try {
        std::ifstream f(inputPath.string());
        json input = json::parse(f, nullptr, true, true);
        auto start_t = std::chrono::high_resolution_clock::now();

        // --- ЧИТАННЯ ПАРАМЕТРІВ ---
        double sW_mm = input.value("sheet_w", 2000.0);
        double sH_mm = input.value("sheet_h", 1000.0);
        double marg_mm = input.value("margin", 0.0);
        double spacing = input.value("spacing", 0.0);
        double kerf = input.value("kerf", 0.0);
        bool is_roll = input.value("is_roll", false);
        double step_mm = input.value("calc_step", 2.0);
        double machine_speed = input.value("machine_speed", 2000.0);

        double inflate_offset = (spacing + kerf) / 2.0 * SCALE;
        int64_t marg = static_cast<int64_t>(marg_mm * SCALE);
        int64_t step = static_cast<int64_t>(step_mm * SCALE);

        // --- ЕКОНОМІЧНІ НАЛАШТУВАННЯ ---
        double cost_per_pierce = 0.0, cost_per_meter = 0.0, labor_cost = 0.0;
        if (input.contains("costs")) {
            cost_per_pierce = input["costs"].value("per_pierce", 0.0);
            cost_per_meter = input["costs"].value("per_meter_cut", 0.0);
            labor_cost = input["costs"].value("labor_cost", 0.0);
        }

        std::vector<Part> queue;
        for (auto& item : input["items"]) {
            if (!ParseAndCreatePart(item, queue)) {
                std::cerr << "Увага: Пропущено некоректну деталь: " << item.dump() << "\n";
            }
        }

        // 2. АНАЛІЗ ГЕОМЕТРІЇ ТА ГЕНЕРАЦІЯ КУТІВ
        for (auto& part : queue) {
            part.type = DetectPolygonType(part.path);
            part.angles = GenerateSmartAngles(part.path, part.type);
        }

        // 3. ОПТИМАЛЬНЕ СОРТУВАННЯ (Гібридне)
        std::sort(queue.begin(), queue.end(), [](const Part& a, const Part& b) {
            if (std::abs(a.area - b.area) > (1.0 * SCALE * SCALE)) {
                return a.area > b.area;
            }
            auto getShapeRank = [](const std::string& t) {
                if (t == "polygon" || t == "triangle") return 1;
                if (t == "trapezoid" || t == "parallelogram") return 2;
                return 3;
                };
            int rankA = getShapeRank(a.type);
            int rankB = getShapeRank(b.type);
            if (rankA != rankB) return rankA < rankB;
            return a.perimeter > b.perimeter;
            });

        // --- КЕШУВАННЯ ГЕОМЕТРІЇ ---
        std::map<std::string, std::vector<PreparedShape>> shape_cache;
        for (auto& part : queue) {
            int part_id = part.meta.value("id", 0);
            std::string cache_key = part.type + "_ID" + std::to_string(part_id);

            if (shape_cache.find(cache_key) == shape_cache.end()) {
                std::vector<PreparedShape> preps;
                for (int ang : part.angles) {
                    Path64 rotated = RotatePath(part.path, ang);
                    Path64 final_shape;
                    if (inflate_offset <= 0.001) {
                        final_shape = rotated;
                    }
                    else {
                        Paths64 inflated = InflatePaths({ rotated }, inflate_offset, JoinType::Miter, EndType::Polygon);
                        if (!inflated.empty()) final_shape = inflated[0];
                    }
                    if (!final_shape.empty()) preps.push_back({ ang, final_shape, GetBounds(final_shape) });
                }
                shape_cache[cache_key] = preps;
            }
            part.prepared_shapes = shape_cache[cache_key];
        }

        // =========================================================================
        // РЕАЛІЗАЦІЯ ДВОХ ПРОХОДІВ
        // =========================================================================
        std::vector<PlacedSheet> bestAllSheets;
        size_t bestTotalPlaced = 0;
        double bestTotalPlacedArea = 0.0;
        double best_sW = sW_mm, best_sH = sH_mm;

        for (int pass = 1; pass <= 2; ++pass) {
            // Якщо це рулон, другий прохід (поворот аркуша) не має сенсу
            if (pass == 2 && is_roll) break;

            double currentW = (pass == 1) ? sW_mm : sH_mm;
            double currentH = (pass == 1) ? sH_mm : sW_mm;

            std::cout << "\n--- ПРОХІД " << pass << " (" << currentW << "x" << currentH << ") ---\n";

            // Скидаємо статус деталей перед кожним проходом
            for (auto& p : queue) p.is_placed = false;

            std::vector<SheetTemplate> current_sheets;
            if (is_roll) {
                current_sheets.push_back({ currentW, 10000000.0, input.value("cost", 0.0), "Безперервний Рулон" });
            }
            else {
                current_sheets.push_back({ currentW, currentH, input.value("cost", 0.0), "Базовий Аркуш" });
            }

            std::vector<PlacedSheet> tempSheets;
            size_t tempPlaced = 0;
            double tempPlacedArea = 0.0;

            // Запуск розрахунку для поточної орієнтації
            RunNesting(queue, tempSheets, current_sheets, marg, step, tempPlacedArea, tempPlaced);

            // Критерій вибору: менша кількість аркушів або більше розміщених деталей
            bool isBetter = false;
            if (bestAllSheets.empty()) {
                isBetter = true;
            }
            else {
                if (tempSheets.size() < bestAllSheets.size()) {
                    isBetter = true;
                }
                else if (tempSheets.size() == bestAllSheets.size() && tempPlaced > bestTotalPlaced) {
                    isBetter = true;
                }
            }

            if (isBetter) {
                bestAllSheets = std::move(tempSheets);
                bestTotalPlaced = tempPlaced;
                bestTotalPlacedArea = tempPlacedArea;
                best_sW = currentW;
                best_sH = currentH;
                std::cout << ">> Ця орієнтація КРАЩА\n";
            }
            else {
                std::cout << ">> Ця орієнтація ГІРША, ігноруємо\n";
            }
        }

        // Призначаємо найкращий результат для подальшої обробки
        std::vector<PlacedSheet> allSheets = std::move(bestAllSheets);
        size_t totalPlaced = bestTotalPlaced;
        double total_placed_area_mm2 = bestTotalPlacedArea;

        // --- ЕКОНОМІЧНИЙ ПІДСУМОК ---
        auto end_t = std::chrono::high_resolution_clock::now();
        double total_time = std::chrono::duration<double>(end_t - start_t).count();
        double total_cut_length_mm = 0, total_material_cost = 0;
        size_t total_pierces = 0;

        for (const auto& sheet : allSheets) {
            total_material_cost += sheet.sheet_def.cost;
            Paths64 sheetPaths;
            for (const auto& p : sheet.parts) {
                sheetPaths.push_back(p.path);
            }
            total_cut_length_mm += CalculateOptimizedCutLength(sheetPaths, sheet.sheet_def.w, sheet.sheet_def.h);
            Paths64 unionResult = Union(sheetPaths, FillRule::NonZero);
            total_pierces += unionResult.size();
        }

        double cutting_cost = (total_cut_length_mm / 1000.0) * cost_per_meter;
        double piercing_cost = total_pierces * cost_per_pierce;
        double total_project_cost = total_material_cost + cutting_cost + piercing_cost;

        std::cout << "\n=== ЕКОНОМІЧНИЙ ЗВІТ ===\n";
        std::cout << "ОБРАНА ОРІЄНТАЦІЯ: " << best_sW << " x " << best_sH << "\n";
        std::cout << "ЗАГАЛЬНА СОБІВАРТІСТЬ: $" << std::fixed << std::setprecision(2) << total_project_cost << "\n";

        // =========================================================================
        // БЛОК 3: ЗБЕРЕЖЕННЯ
        // =========================================================================
        SaveMultiSheetSVG(finalOutputSvg.string(), allSheets, input, total_time, totalPlaced, total_placed_area_mm2, total_cut_length_mm);

        int error_code = (totalPlaced < queue.size()) ? ST_ERR_BIG_PARTS : ST_OK;
        SaveJSON(finalOutputJson.string(), allSheets, machine_speed, total_cut_length_mm, error_code);

        std::cout << "\nРозрахунок ЗАВЕРШЕНО!\nЗагальний час: " << total_time << " сек.\n";
    }
    catch (const std::exception& e) {
        std::cerr << "Критична помилка: " << e.what() << "\n";
        return 1;
    }
    return 0;
}