/*
 * ==============================================================================
 * ФАЙЛ: main.cpp
 * ОПИС: Головний файл програми
 * Зв'язує всі модулі воєдино
 * Xитання параметрів,
 * Hозрахунок собівартості
 * ==============================================================================
 */
#define NOMINMAX
#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <filesystem>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

#include "types.h"
#include "geometry.h"
#include "io_utils.h"
#include "nesting.h"

#ifdef _WIN32
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")
#endif

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    // =========================================================================
    // БЛОК 1: ПАРСИНГ АРГУМЕНТІВ КОМАНДНОГО РЯДКА
    // =========================================================================

    // 1. ПЕРШИЙ ПАРАМЕТР: Шлях до самого екзешника
    fs::path exePath = fs::path(argv[0]);
    fs::path exeDir = exePath.parent_path();
    if (exeDir.empty()) {
        exeDir = fs::current_path();
    }

    // 2. ДРУГИЙ ПАРАМЕТР: Вхідний файл
    fs::path inputPath = (argc > 1) ? fs::path(argv[1]) : (exeDir / "input.json");

    // 3. ТРЕТІЙ ПАРАМЕТР: Назва вихідного JSON
    std::string outputJsonName = (argc > 2) ? argv[2] : "output.json";

    // 4. ЧЕТВЕРТИЙ ПАРАМЕТР: Директорія виходу
    fs::path outDir = exeDir; // За замовчуванням кладемо поруч із екзешником

    if (argc > 3) {
        fs::path targetDir = fs::path(argv[3]);

        if (fs::exists(targetDir) && fs::is_directory(targetDir)) {
            // Перевіряємо права на запис
            fs::path testFile = targetDir / "test_write_permission.tmp";
            std::ofstream ofs(testFile);
            if (ofs.is_open()) {
                ofs.close();
                fs::remove(testFile); // Права є
                outDir = targetDir;   // Застосовуємо шлях
            }
            else {
                std::cerr << "Увага: Немає прав на запис у " << targetDir.string() << ". Збережемо поруч із програмою.\n";
            }
        }
        else {
            std::cerr << "Увага: Папки " << targetDir.string() << " не існує. Збережемо поруч із програмою.\n";
        }
    }

    // Формуємо фінальні абсолютні шляхи
    fs::path finalOutputJson = outDir / outputJsonName;

    // Якщо користувач вказав json як data.json, зробимо картинку data.svg
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

        // Захист від некоректних даних
        if (sW_mm <= 0) sW_mm = 2000.0;
        if (sH_mm <= 0) sH_mm = 1000.0;
        if (marg_mm < 0) marg_mm = 0;
        if (spacing < 0) spacing = 0;
        if (kerf < 0) kerf = 0;
        if (step_mm <= 0.1) step_mm = 2.0;

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

        std::vector<SheetTemplate> available_sheets;

        if (is_roll) {
            // ЯКЩО УВІМКНЕНО РУЛОН
            double cost = input.value("cost", 0.0);
            available_sheets.push_back({ sW_mm, 10000000.0, cost, "Безперервний Рулон" });
        }
        else {
            // ЯКЩО АРКУШІ
            if (input.contains("sheets") && input["sheets"].is_array()) {
                for (const auto& sh : input["sheets"]) {
                    SheetTemplate tpl;
                    tpl.w = sh.value("w", 2000.0);
                    tpl.h = sh.value("h", 1000.0);
                    tpl.cost = sh.value("cost", 0.0);
                    tpl.name = sh.value("name", "Аркуш " + std::to_string((int)tpl.w) + "x" + std::to_string((int)tpl.h));
                    if (tpl.w > 0 && tpl.h > 0) available_sheets.push_back(tpl);
                }
            }
            if (available_sheets.empty()) {
                double cost = input.value("cost", 0.0);
                available_sheets.push_back({ sW_mm, sH_mm, cost, "Базовий Аркуш" });
            }
        }

        std::vector<Part> queue;
        for (auto& item : input["items"]) {
            if (!ParseAndCreatePart(item, queue)) {
                std::cerr << "Увага: Пропущено некоректну деталь: " << item.dump() << "\n";
            }
        }

        // Сортуємо від найбільших до найменших
        std::sort(queue.begin(), queue.end(), [](const Part& a, const Part& b) {
            return a.area > b.area;
            });

        // --- КЕШУВАННЯ ГЕОМЕТРІЇ ---
        std::map<std::string, std::vector<PreparedShape>> shape_cache;
        for (auto& part : queue) {
            std::string cache_key = part.type + "_" + std::to_string(static_cast<int64_t>(part.area)) + "_" + std::to_string(part.path.size());

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

        std::vector<PlacedSheet> allSheets;
        size_t totalPlaced = 0;
        double total_placed_area_mm2 = 0.0;

        std::cout << "--- ПОЧАТОК РОЗРАХУНКУ ---\n";
        std::cout << "Крок розрахунку (сітка): " << step_mm << " мм.\n";

        RunNesting(queue, allSheets, available_sheets, marg, step, total_placed_area_mm2, totalPlaced);

        if (is_roll && !allSheets.empty()) {
            int64_t max_bottom = 0;
            for (const auto& part : allSheets[0].parts) {
                Rect64 b = GetBounds(part.path);
                if (b.bottom > max_bottom) max_bottom = b.bottom;
            }

            double used_length_mm = (double)(max_bottom + marg) / SCALE;
            allSheets[0].sheet_def.h = used_length_mm;

            std::cout << "\n[!] РЕЖИМ РУЛОНУ: Фактична використана довжина: " << std::fixed << std::setprecision(1) << used_length_mm << " мм\n";
        }

        auto end_t = std::chrono::high_resolution_clock::now();
        double total_time = std::chrono::duration<double>(end_t - start_t).count();

        // --- ЕКОНОМІЧНИЙ ПІДСУМОК ---
        double total_cut_length_mm = 0;
        double total_material_cost = 0;
        int total_pierces = 0;

        for (const auto& sheet : allSheets) {
            total_material_cost += sheet.sheet_def.cost;
        }

        for (const auto& part : queue) {
            if (part.is_placed) {
                total_cut_length_mm += part.perimeter;
                total_pierces++;
            }
        }

        double cutting_cost = (total_cut_length_mm / 1000.0) * cost_per_meter;
        double piercing_cost = total_pierces * cost_per_pierce;
        double total_project_cost = total_material_cost + cutting_cost + piercing_cost;

        std::cout << "\n=== ЕКОНОМІЧНИЙ ЗВІТ ===\n";
        std::cout << "Витрати на матеріал: $" << std::fixed << std::setprecision(2) << total_material_cost << "\n";
        std::cout << "Вартість машинного часу: $" << (cutting_cost + piercing_cost) << " (Різ + " << total_pierces << " врізань)\n";
        std::cout << "ЗАГАЛЬНА СОБІВАРТІСТЬ: $" << total_project_cost << "\n";
        std::cout << "Довжина різу: " << total_cut_length_mm / 1000.0 << " метрів\n";

        // =========================================================================
        // БЛОК 3: ЗБЕРЕЖЕННЯ
        // =========================================================================

        SaveMultiSheetSVG(finalOutputSvg.string(), allSheets, input, total_time, totalPlaced, total_placed_area_mm2, total_cut_length_mm);

        SaveJSON(finalOutputJson.string(), allSheets);

        std::cout << "\n------------------------------------------\n";
        std::cout << "Розрахунок ЗАВЕРШЕНО!\nЗагальний час: " << std::fixed << std::setprecision(2) << total_time << " сек.\n";
        std::cout << "Використано аркушів: " << allSheets.size() << "\nДеталей розміщено: " << totalPlaced << " / " << queue.size() << "\n";
        std::cout << "------------------------------------------\n";

    }
    catch (const std::exception& e) {
        std::cerr << "Критична помилка: " << e.what() << "\n";
        return 1;
    }
    return 0;
}