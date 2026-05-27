#include "io_utils.h"
#include "geometry.h"
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <cmath>

// Переклад типів на українську
std::string TranslateType(const std::string& type) {
    if (type == "rectangle")     return (const char*)u8"Прямокутник";
    if (type == "circle")        return (const char*)u8"Коло";
    if (type == "square")        return (const char*)u8"Квадрат";
    if (type == "triangle")      return (const char*)u8"Трикутник";
    if (type == "pentagon")      return (const char*)u8"П'ятикутник";
    if (type == "hexagon")       return (const char*)u8"Шестикутник";
    if (type == "trapezoid")     return (const char*)u8"Трапеція";
    if (type == "rhombus")       return (const char*)u8"Ромб";
    if (type == "parallelogram") return (const char*)u8"Паралелограм";
    if (type == "ellipse")       return (const char*)u8"Еліпс";
    if (type == "polygon")       return (const char*)u8"Полігон";
    return (const char*)u8"Довільна фігура";
}

bool ParseAndCreatePart(const json& item, std::vector<Part>& queue) {
    Path64 base_path;
    std::string type = item.value("type", "unknown");
    double w = item.value("w", 0.0), h = item.value("h", 0.0);

    if (type == "circle") {
        double r = item.contains("r") ? (double)item["r"] : (item.value("d", 0.0) / 2.0);
        if (r <= 0) return false;
        base_path = CreateCircle(r);
    }
    else if (type == "rectangle" || type == "square") {
        if (w <= 0 || h <= 0) return false;
        base_path = CreateRectangle(w, h);
    }
    else if (item.contains("points")) {
        for (const auto& pt : item["points"]) {
            base_path.push_back(Point64(std::round((double)pt["x"] * SCALE), std::round((double)pt["y"] * SCALE)));
        }
    }
    else return false;

    if (base_path.empty()) return false;

    int q = item.value("q", 1);
    std::vector<int> angles = GenerateSmartAngles(base_path, type, w, h);

    // Розраховуємо периметр
    double p_mm = CalculatePerimeter(base_path);
    double a_scaled = std::abs(Area(base_path));

    // Додаємо периметр до метаданих для подальшого використання у JSON
    json meta_copy = item;
    meta_copy["perimeter_mm"] = p_mm;

    for (int i = 0; i < q; ++i) {
        Part p;
        p.meta = meta_copy;
        p.path = base_path;
        p.type = type;
        p.angles = angles;
        p.area = a_scaled;
        p.perimeter = p_mm;
        p.q_index = queue.size();
        p.is_placed = false;
        queue.push_back(p);
    }
    return true;
}

void SaveJSON(const std::string& filename, const std::vector<PlacedSheet>& allSheets, double machine_speed, double total_perimeter_mm, int error_code) {
    nlohmann::ordered_json root;

    // Записуємо глобальні поля в корінь
    if (error_code != ST_OK) {
        root["status"] = GetStatusString(error_code);
    }
    else {
        root["status"] = "ST_OK";
    }

    root["total_perimeter_mm"] = std::round(total_perimeter_mm * 100.0) / 100.0;

    double machine_time_mins = (machine_speed > 0) ? (total_perimeter_mm / machine_speed) : 0.0;
    root["machine_time_mins"] = machine_time_mins;

    // 1. Рахуємо сумарний "сирий" периметр всіх розміщених деталей
    double total_raw_perim = 0.0;
    for (const auto& sheet : allSheets) {
        for (const auto& p : sheet.parts) {
            // УВАГА: Ділити на SCALE тут не треба, функція вже повертає міліметри!
            total_raw_perim += CalculatePerimeter(p.path);
        }
    }

    // 2. Рахуємо коефіцієнт оптимізації (якщо економія 50%, то фактор = 0.5)
    double optimization_factor = (total_raw_perim > 0) ? (total_perimeter_mm / total_raw_perim) : 1.0;

    json parts_arr = json::array(); // Масив для деталей

    for (size_t s_idx = 0; s_idx < allSheets.size(); ++s_idx) {
        const auto& sheet = allSheets[s_idx];

        double sheetArea = sheet.sheet_def.w * sheet.sheet_def.h;
        double usedArea = 0;
        for (const auto& p : sheet.parts) {
            usedArea += std::abs(Area(p.path)) / (SCALE * SCALE);
        }
        double leftPercent = (sheetArea > 0) ? ((sheetArea - usedArea) / sheetArea) * 100.0 : 0.0;

        for (const auto& p : sheet.parts) {
            json jp;
            jp["id"] = p.meta.value("id", 0);
            jp["sheet_id"] = s_idx + 1;
            jp["sheet_leftover_percent"] = std::round(std::max(0.0, leftPercent) * 100.0) / 100.0;

            // 3. Рахуємо чесний час для кожної деталі пропорційно економії
            double p_perim_raw = CalculatePerimeter(p.path); // Без ділення на SCALE!
            double p_perim_optimized = p_perim_raw * optimization_factor;

            double p_time = (machine_speed > 0) ? (p_perim_optimized / machine_speed) : 0.0;
            jp["time_mins"] = std::round(p_time * 10000.0) / 10000.0;

            jp["type"] = p.meta.value("type", "unknown");

            parts_arr.push_back(jp);
        }
    }

    root["parts"] = parts_arr;

    std::ofstream f(filename);
    f << root.dump(4);
}

// --- ДОПОМІЖНІ СТРУКТУРИ ДЛЯ ГРУПУВАННЯ АРКУШІВ ---
bool AreSheetsIdentical(const PlacedSheet& a, const PlacedSheet& b) {
    if (std::abs(a.sheet_def.w - b.sheet_def.w) > 0.1 || std::abs(a.sheet_def.h - b.sheet_def.h) > 0.1) return false;
    if (a.parts.size() != b.parts.size()) return false;
    for (size_t i = 0; i < a.parts.size(); ++i) {
        if (a.parts[i].meta.value("id", 0) != b.parts[i].meta.value("id", 0)) return false;
        if (a.parts[i].angle != b.parts[i].angle) return false;
        if (!a.parts[i].path.empty() && !b.parts[i].path.empty()) {
            if (a.parts[i].path[0].x != b.parts[i].path[0].x || a.parts[i].path[0].y != b.parts[i].path[0].y) return false;
        }
    }
    return true;
}

struct SvgSheetGroup {
    size_t first_index;
    int count;
    const PlacedSheet* sheet;
};
// --------------------------------------------------

void SaveMultiSheetSVG(const std::string& filename, const std::vector<PlacedSheet>& allSheets, const json& inputParams, double time_sec, size_t totalPlacedCount, double totalPlacedAreaMm2, double total_cut_length_mm) {
    std::ofstream svg(filename);
    if (!svg.is_open() || allSheets.empty()) return;

    // 1. ГРУПУЄМО ОДНАКОВІ АРКУШІ
    std::vector<SvgSheetGroup> grouped;
    grouped.push_back({ 0, 1, &allSheets[0] });
    for (size_t i = 1; i < allSheets.size(); ++i) {
        if (AreSheetsIdentical(*grouped.back().sheet, allSheets[i])) {
            grouped.back().count++;
        }
        else {
            grouped.push_back({ i, 1, &allSheets[i] });
        }
    }

    double visualScale = 2.0;
    double maxSheetW = 0;
    for (const auto& g : grouped) if (g.sheet->sheet_def.w > maxSheetW) maxSheetW = g.sheet->sheet_def.w;

    double dynUnit = std::max(maxSheetW * visualScale / 100.0, 1.0);
    double padding = dynUnit * 20.0;
    double tableW = dynUnit * 130.0;

    double gapX = 0.0;
    double gapY = 0.0;
    double innerPadX = dynUnit * 20.0;
    double innerPadY = dynUnit * 20.0;

    int numCols = grouped.size() < 5 ? std::max<int>(1, static_cast<int>(grouped.size())) : 5;

    double cellW = std::max(maxSheetW * visualScale, tableW) + innerPadX * 2.0;
    double colW = cellW + gapX;

    struct PartGroup {
        std::string displayName;
        std::string dimensions;
        int count;
    };
    struct LayoutData {
        double blockH;
        std::vector<PartGroup> groups;
    };

    std::vector<LayoutData> layouts(grouped.size());
    std::vector<double> rowHeights;
    double currentRowMax = 0.0;
    double totalH = padding * 2;

    for (size_t i = 0; i < grouped.size(); ++i) {
        const auto& sheet = *(grouped[i].sheet);

        std::map<std::string, size_t> groupIndexMap;
        for (const auto& p : sheet.parts) {
            std::string p_type = DetectPolygonType(p.path);
            Rect64 b = GetBounds(p.path);
            int dimW = (int)std::round(b.Width() / SCALE);
            int dimH = (int)std::round(b.Height() / SCALE);

            // --- ОПТИМІЗАЦІЯ: Сортування розмірів ---
            // Завжди ставимо більший габарит першим, щоб ігнорувати обертання фігури
            int maxDim = std::max(dimW, dimH);
            int minDim = std::min(dimW, dimH);

            std::string displayName = p.meta.contains("name") ? p.meta["name"].get<std::string>() : TranslateType(p_type);

            // Формуємо рядок з відсортованих розмірів
            std::string d = std::to_string(maxDim) + "x" + std::to_string(minDim);
            if (p_type == "circle") d = "R" + std::to_string(maxDim / 2);

            std::string key = displayName + "|" + d;
            if (groupIndexMap.find(key) == groupIndexMap.end()) {
                groupIndexMap[key] = layouts[i].groups.size();
                layouts[i].groups.push_back({ displayName, d, 1 });
            }
            else {
                layouts[i].groups[groupIndexMap[key]].count++;
            }
        }

        double sh = sheet.sheet_def.h * visualScale;
        double rowH = dynUnit * 7.5;
        double tableH = (layouts[i].groups.size() + 4) * rowH + dynUnit * 2.0;

        double titleAreaH = dynUnit * 30.0;
        double sheetBlockH = sh + dynUnit * 15.0;

        layouts[i].blockH = titleAreaH + sheetBlockH + tableH;

        if (layouts[i].blockH > currentRowMax) {
            currentRowMax = layouts[i].blockH;
        }

        if ((i + 1) % numCols == 0 || i == grouped.size() - 1) {
            double finalCellH = currentRowMax + innerPadY * 2.0;
            rowHeights.push_back(finalCellH);
            totalH += finalCellH + gapY;
            currentRowMax = 0.0;
        }
    }

    if (!rowHeights.empty()) totalH -= gapY;
    double totalW = padding * 2 + numCols * colW;

    svg << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    svg << "<svg width=\"150%\" height=\"150%\" viewBox=\"0 0 " << totalW << " " << totalH << "\" preserveAspectRatio=\"xMinYMin meet\" xmlns=\"http://www.w3.org/2000/svg\">\n";
    svg << "<defs><marker id=\"arrow\" viewBox=\"0 0 10 10\" refX=\"5\" refY=\"5\" markerWidth=\"3\" markerHeight=\"3\" orient=\"auto-start-reverse\"><path d=\"M 0 0 L 10 5 L 0 10 z\" fill=\"#333\" /></marker></defs>\n";

    svg << "<style>\n"
        << "  .sheet { fill: #f0f8ff; stroke: #111; stroke-width: " << dynUnit * 0.5 << "; }\n"
        << "  .part { fill: rgba(0, 120, 255, 0.15); stroke: #0055ff; stroke-width: " << dynUnit * 0.2 << "; }\n"
        << "  .line { stroke: #333; stroke-width: " << dynUnit * 0.3 << "; marker-start: url(#arrow); marker-end: url(#arrow); }\n"
        << "  .txt-label { fill: #000; font-family: sans-serif; font-size: " << dynUnit * 3.0 << "px; text-anchor: middle; }\n"
        << "  .t-head { fill: #1a252f; }\n"
        << "  .t-foot { fill: #ecf0f1; }\n"
        << "  .t-text { font-family: sans-serif; font-size: " << dynUnit * 3.5 << "px; }\n"
        << "  .t-cell { font-family: monospace; font-size: " << dynUnit * 3.0 << "px; fill: #2c3e50; }\n"
        << "  .t-cell-bold { font-family: sans-serif; font-size: " << dynUnit * 3.2 << "px; font-weight: bold; fill: #111; }\n"
        << "  .t-res-label { font-family: sans-serif; font-size: " << dynUnit * 3.2 << "px; font-weight: bold; fill: #555; }\n"
        << "  .t-res-val { font-family: monospace; font-size: " << dynUnit * 3.4 << "px; font-weight: bold; fill: #000; }\n"
        << "</style>\n";

    svg << "  <rect width=\"100%\" height=\"100%\" fill=\"#ffffff\"/>\n";

    double currentX = padding;
    double currentY = padding;
    int rowIndex = 0;

    for (size_t i = 0; i < grouped.size(); ++i) {
        const auto& gp = grouped[i];
        const auto& sheet = *(gp.sheet);
        double sw_mm = sheet.sheet_def.w;
        double sh_mm = sheet.sheet_def.h;
        double sw = sw_mm * visualScale;
        double sh = sh_mm * visualScale;

        double cellH = rowHeights[rowIndex];

        svg << "  <rect x=\"" << currentX << "\" y=\"" << currentY << "\" width=\"" << cellW << "\" height=\"" << cellH << "\" fill=\"#ffffff\" stroke=\"#000000\" stroke-width=\"" << dynUnit * 1.5 << "\"/>\n";

        double ox = currentX + (cellW - sw) / 2.0;
        double tx = currentX + (cellW - tableW) / 2.0;
        double contentStartY = currentY + (cellH - layouts[i].blockH) / 2.0;

        std::string title = (const char*)u8"АРКУШ №" + std::to_string(gp.first_index + 1);
        if (gp.count > 1) {
            title += "-" + std::to_string(gp.first_index + gp.count);
            title += " (" + std::to_string(gp.count) + " " + (const char*)u8"шт)";
        }

        svg << "  <text style=\"font-family: sans-serif; font-weight:bold; font-size: " << dynUnit * 4.5 << "px; text-anchor: middle;\" "
            << "x=\"" << (currentX + cellW / 2.0) << "\" y=\"" << (contentStartY + dynUnit * 12.0) << "\">"
            << title << "</text>\n";

        double sheetY = contentStartY + dynUnit * 30.0;

        double arrowY = sheetY - (dynUnit * 5.0);
        svg << "  <line class=\"line\" x1=\"" << ox << "\" y1=\"" << arrowY << "\" x2=\"" << ox + sw << "\" y2=\"" << arrowY << "\"/>\n";
        svg << "  <text class=\"txt-label\" x=\"" << ox + sw / 2 << "\" y=\"" << arrowY - (dynUnit * 2.0) << "\">" << (int)sw_mm << " mm</text>\n";
        double arrowX = ox - (dynUnit * 6.0);
        svg << "  <line class=\"line\" x1=\"" << arrowX << "\" y1=\"" << sheetY << "\" x2=\"" << arrowX << "\" y2=\"" << sheetY + sh << "\"/>\n";
        svg << "  <text class=\"txt-label\" x=\"" << arrowX - (dynUnit * 2.0) << "\" y=\"" << sheetY + sh / 2 << "\" transform=\"rotate(-90," << arrowX - (dynUnit * 2.0) << "," << sheetY + sh / 2 << ")\">" << (int)sh_mm << " mm</text>\n";

        svg << "  <rect class=\"sheet\" x=\"" << ox << "\" y=\"" << sheetY << "\" width=\"" << sw << "\" height=\"" << sh << "\"/>\n";

        double usedAreaMm2 = 0;
        for (const auto& p : sheet.parts) {
            usedAreaMm2 += std::abs(Area(p.path)) / ((double)SCALE * (double)SCALE);
            svg << "  <polygon class=\"part\" points=\"";
            for (const auto& pt : p.path) svg << ox + (pt.x / SCALE) * visualScale << "," << sheetY + (pt.y / SCALE) * visualScale << " ";
            svg << "\"/>\n";
        }

        double ty = sheetY + sh + dynUnit * 15.0;
        double rowH = dynUnit * 7.5;
        double tableStartY = ty;

        svg << "  <rect class=\"t-head\" x=\"" << tx << "\" y=\"" << ty << "\" width=\"" << tableW << "\" height=\"" << rowH << "\" stroke=\"#111\" stroke-width=\"" << dynUnit * 0.2 << "\"/>\n";
        svg << "  <text class=\"t-text\" fill=\"white\" style=\"font-weight:bold;\" x=\"" << tx + dynUnit * 2 << "\" y=\"" << ty + dynUnit * 5.0 << "\">ФІГУРА</text>\n";
        svg << "  <text class=\"t-text\" fill=\"white\" style=\"font-weight:bold;\" x=\"" << tx + dynUnit * 48 << "\" y=\"" << ty + dynUnit * 5.0 << "\">РОЗМІРИ</text>\n";
        svg << "  <text class=\"t-text\" fill=\"white\" style=\"font-weight:bold;\" x=\"" << tx + dynUnit * 88 << "\" y=\"" << ty + dynUnit * 5.0 << "\">КІЛЬКІСТЬ</text>\n";
        ty += rowH;

        for (size_t k = 0; k < layouts[i].groups.size(); ++k) {
            const auto& g_part = layouts[i].groups[k];
            std::string bg_color = (k % 2 == 0) ? "#f8f9fa" : "#ffffff";
            svg << "  <rect fill=\"" << bg_color << "\" x=\"" << tx << "\" y=\"" << ty << "\" width=\"" << tableW << "\" height=\"" << rowH << "\" stroke=\"#111\" stroke-width=\"" << dynUnit * 0.2 << "\"/>\n";
            svg << "  <text class=\"t-cell-bold\" x=\"" << tx + dynUnit * 2 << "\" y=\"" << ty + dynUnit * 5.0 << "\">" << g_part.displayName << "</text>\n";
            svg << "  <text class=\"t-cell\" x=\"" << tx + dynUnit * 48 << "\" y=\"" << ty + dynUnit * 5.0 << "\">" << g_part.dimensions << "</text>\n";
            svg << "  <text class=\"t-cell\" x=\"" << tx + dynUnit * 88 << "\" y=\"" << ty + dynUnit * 5.0 << "\">" << g_part.count << " шт</text>\n";
            ty += rowH;
        }

        svg << "  <line x1=\"" << tx + dynUnit * 46 << "\" y1=\"" << tableStartY << "\" x2=\"" << tx + dynUnit * 46 << "\" y2=\"" << ty << "\" stroke=\"#111\" stroke-width=\"" << dynUnit * 0.2 << "\"/>\n";
        svg << "  <line x1=\"" << tx + dynUnit * 86 << "\" y1=\"" << tableStartY << "\" x2=\"" << tx + dynUnit * 86 << "\" y2=\"" << ty << "\" stroke=\"#111\" stroke-width=\"" << dynUnit * 0.2 << "\"/>\n";

        double totalSheetAreaM2 = (sw_mm * sh_mm) / 1000000.0;
        double usedM2 = usedAreaMm2 / 1000000.0;
        double leftM2 = totalSheetAreaM2 - usedM2;
        double leftPercent = (totalSheetAreaM2 > 0) ? (leftM2 / totalSheetAreaM2) * 100.0 : 0.0;

        ty += (dynUnit * 2.0);
        double resStartY = ty;

        svg << "  <rect class=\"t-foot\" x=\"" << tx << "\" y=\"" << ty << "\" width=\"" << tableW << "\" height=\"" << rowH * 3 << "\" stroke=\"#111\" stroke-width=\"" << dynUnit * 0.2 << "\"/>\n";
        svg << "  <line x1=\"" << tx << "\" y1=\"" << ty + rowH << "\" x2=\"" << tx + tableW << "\" y2=\"" << ty + rowH << "\" stroke=\"#111\" stroke-width=\"" << dynUnit * 0.2 << "\"/>\n";
        svg << "  <line x1=\"" << tx << "\" y1=\"" << ty + rowH * 2 << "\" x2=\"" << tx + tableW << "\" y2=\"" << ty + rowH * 2 << "\" stroke=\"#111\" stroke-width=\"" << dynUnit * 0.2 << "\"/>\n";
        svg << "  <line x1=\"" << tx + dynUnit * 86 << "\" y1=\"" << ty << "\" x2=\"" << tx + dynUnit * 86 << "\" y2=\"" << ty + rowH * 3 << "\" stroke=\"#111\" stroke-width=\"" << dynUnit * 0.2 << "\"/>\n";

        auto drawRes = [&](int rowIdx, std::string lbl, std::string val) {
            double ry = resStartY + (rowIdx * rowH) + dynUnit * 5.0;
            svg << "  <text class=\"t-res-label\" x=\"" << tx + dynUnit * 5 << "\" y=\"" << ry << "\">" << lbl << "</text>\n";
            svg << "  <text class=\"t-res-val\" x=\"" << tx + dynUnit * 88 << "\" y=\"" << ry << "\">" << val << "</text>\n";
            };

        std::stringstream sTotal, sLeft, sPerc;
        sTotal << std::fixed << std::setprecision(4) << totalSheetAreaM2 << " m2";
        sLeft << std::fixed << std::setprecision(4) << std::max(0.0, leftM2) << " m2";
        sPerc << std::fixed << std::setprecision(2) << std::max(0.0, leftPercent) << " %";

        drawRes(0, (const char*)u8"Загальна площа аркуша:", sTotal.str());
        drawRes(1, (const char*)u8"Залишилося вільної площі:", sLeft.str());
        drawRes(2, (const char*)u8"Відсоток залишку:", sPerc.str());

        if ((i + 1) % numCols == 0 || i == grouped.size() - 1) {
            if (i == grouped.size() - 1 && (grouped.size() % numCols) != 0) {
                size_t leftover = grouped.size() % numCols;
                size_t emptyCells = numCols - leftover;
                for (size_t e = 0; e < emptyCells; ++e) {
                    currentX += colW;
                    svg << "  <rect x=\"" << currentX << "\" y=\"" << currentY << "\" width=\"" << cellW << "\" height=\"" << cellH << "\" fill=\"#ffffff\" stroke=\"#000000\" stroke-width=\"" << dynUnit * 1.5 << "\"/>\n";
                }
            }
            currentX = padding;
            currentY += rowHeights[rowIndex];
            rowIndex++;
        }
        else {
            currentX += colW;
        }
    }
    svg << "</svg>";
}