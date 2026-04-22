#include "io_utils.h"
#include "geometry.h"
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <cmath>

// Переклад типів на українську
std::string TranslateType(const std::string& type) {
    if (type == "rectangle") return (const char*)u8"Прямокутник";
    if (type == "circle") return (const char*)u8"Коло";
    if (type == "square") return (const char*)u8"Квадрат";
    if (type == "polygon") return (const char*)u8"Полігон";
    return type;
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

    int q = item.value("q", 1);
    std::vector<int> angles = GenerateSmartAngles(base_path, type, w, h);

    for (int i = 0; i < q; ++i) {
        Part p; p.meta = item; p.path = base_path; p.type = type;
        p.angles = angles; p.area = std::abs(Area(base_path));
        p.q_index = queue.size(); p.is_placed = false;
        queue.push_back(p);
    }
    return true;
}

void SaveJSON(const std::string& filename, const std::vector<PlacedSheet>& allSheets) {
    json out = json::array();

    for (size_t s = 0; s < allSheets.size(); ++s) {
        const auto& sheet = allSheets[s];

        // Розрахунок відсотка залишку
        double sheet_area_mm2 = sheet.sheet_def.w * sheet.sheet_def.h;
        double used_area_mm2 = 0;
        for (const auto& p : sheet.parts) {
            used_area_mm2 += std::abs(Area(p.path)) / ((double)SCALE * (double)SCALE);
        }

        double left_percent = (sheet_area_mm2 > 0) ? ((sheet_area_mm2 - used_area_mm2) / sheet_area_mm2) * 100.0 : 0.0;
        double rounded_percent = std::round(std::max(0.0, left_percent) * 100.0) / 100.0;

        for (const auto& p : sheet.parts) {
            json jp;
            jp["sheet_id"] = s + 1;
            jp["id"] = p.meta.value("id", 0);

            // Координати
            //jp["x"] = GetBounds(p.path).left / (double)SCALE;
            //jp["y"] = GetBounds(p.path).top / (double)SCALE;

            // Тип деталі
            std::string type = p.meta.value("type", "unknown");
            jp["type"] = type;

            // розміри для прямокутників та квадратів
            if (type == "rectangle" || type == "square") {
                jp["width"] = p.meta.value("w", 0.0);
                jp["height"] = p.meta.value("h", 0.0);
            }
            else if (type == "circle") {
                // Для кола логічно вивести радіус або діаметр
                jp["radius"] = p.meta.value("r", 0.0);
            }

            // Відсоток залишку аркуша
            jp["sheet_leftover_percent"] = rounded_percent;

            out.push_back(jp);
        }
    }

    std::ofstream o(filename);
    if (o.is_open()) {
        o << std::setw(4) << out;
    }
}

void SaveMultiSheetSVG(const std::string& filename, const std::vector<PlacedSheet>& allSheets, const json& inputParams, double time_sec, size_t totalPlacedCount, double totalPlacedAreaMm2, double total_cut_length_mm) {
    std::ofstream svg(filename);
    if (!svg.is_open() || allSheets.empty()) return;

    double visualScale = 2.0;
    double maxSheetW = 0;
    for (const auto& s : allSheets) if (s.sheet_def.w > maxSheetW) maxSheetW = s.sheet_def.w;

    double dynUnit = std::max(maxSheetW * visualScale / 100.0, 1.0);
    double padding = dynUnit * 20.0;
    double tableW = dynUnit * 220.0;
    double gapX = dynUnit * 15.0;

    double totalH = padding;
    for (const auto& s : allSheets) {
        double sheetH = s.sheet_def.h * visualScale + (dynUnit * 15.0);
        double rowH = dynUnit * 7.5;
        double tableH = (s.parts.size() + 5) * rowH;
        totalH += std::max(sheetH, tableH) + (dynUnit * 50.0);
    }

    double totalW = (maxSheetW * visualScale) + tableW + (padding * 2) + gapX + (dynUnit * 15.0);

    svg << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    svg << "<svg width=\"100%\" height=\"100%\" viewBox=\"0 0 " << totalW << " " << totalH << "\" preserveAspectRatio=\"xMinYMin meet\" xmlns=\"http://www.w3.org/2000/svg\">\n";
    svg << "<defs><marker id=\"arrow\" viewBox=\"0 0 10 10\" refX=\"5\" refY=\"5\" markerWidth=\"3\" markerHeight=\"3\" orient=\"auto-start-reverse\"><path d=\"M 0 0 L 10 5 L 0 10 z\" fill=\"#333\" /></marker></defs>\n";

    svg << "<style>\n"
        << "  .sheet { fill: white; stroke: #111; stroke-width: " << dynUnit * 0.5 << "; }\n"
        << "  .part { fill: rgba(0, 120, 255, 0.1); stroke: #0055ff; stroke-width: " << dynUnit * 0.15 << "; }\n"
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

    double currentY = padding;
    for (size_t i = 0; i < allSheets.size(); ++i) {
        const auto& sheet = allSheets[i];
        double sw_mm = sheet.sheet_def.w;
        double sh_mm = sheet.sheet_def.h;
        double sw = sw_mm * visualScale;
        double sh = sh_mm * visualScale;
        double ox = padding + (dynUnit * 15.0);

        // 1. Назва аркушаs
        svg << "  <text style=\"font-family: sans-serif; font-weight:bold; font-size: " << dynUnit * 4.5 << "px; text-anchor: middle;\" "
            << "x=\"" << ox + (sw / 2.0) << "\" y=\"" << currentY << "\">"
            << (const char*)u8"АРКУШ №" << (i + 1) << "</text>\n";

        double sheetY = currentY + (dynUnit * 12.0);

        // Стрілки розмірів
        double arrowY = sheetY - (dynUnit * 5.0);
        svg << "  <line class=\"line\" x1=\"" << ox << "\" y1=\"" << arrowY << "\" x2=\"" << ox + sw << "\" y2=\"" << arrowY << "\"/>\n";
        svg << "  <text class=\"txt-label\" x=\"" << ox + sw / 2 << "\" y=\"" << arrowY - (dynUnit * 2.0) << "\">" << (int)sw_mm << " mm</text>\n";
        double arrowX = ox - (dynUnit * 6.0);
        svg << "  <line class=\"line\" x1=\"" << arrowX << "\" y1=\"" << sheetY << "\" x2=\"" << arrowX << "\" y2=\"" << sheetY + sh << "\"/>\n";
        svg << "  <text class=\"txt-label\" x=\"" << arrowX - (dynUnit * 2.0) << "\" y=\"" << sheetY + sh / 2 << "\" transform=\"rotate(-90," << arrowX - (dynUnit * 2.0) << "," << sheetY + sh / 2 << ")\">" << (int)sh_mm << " mm</text>\n";

        // Креслення
        svg << "  <rect class=\"sheet\" x=\"" << ox << "\" y=\"" << sheetY << "\" width=\"" << sw << "\" height=\"" << sh << "\"/>\n";

        double usedAreaMm2 = 0;
        for (const auto& p : sheet.parts) {
            usedAreaMm2 += std::abs(Area(p.path)) / ((double)SCALE * (double)SCALE);

            svg << "  <polygon class=\"part\" points=\"";
            for (const auto& pt : p.path) svg << ox + (pt.x / SCALE) * visualScale << "," << sheetY + (pt.y / SCALE) * visualScale << " ";
            svg << "\"/>\n";
        }

        // --- ТАБЛИЦЯ СПЕЦИФІКАЦІЇ ---
        double tx = ox + sw + gapX;
        double ty = currentY;
        double rowH = dynUnit * 7.5;

        // Шапка
        svg << "  <rect class=\"t-head\" x=\"" << tx << "\" y=\"" << ty << "\" width=\"" << tableW << "\" height=\"" << rowH << "\" rx=\"2\"/>\n";
        svg << "  <text class=\"t-text\" fill=\"white\" style=\"font-weight:bold;\" x=\"" << tx + dynUnit * 2 << "\" y=\"" << ty + dynUnit * 5.0 << "\">ФІГУРА</text>\n";
        svg << "  <text class=\"t-text\" fill=\"white\" style=\"font-weight:bold;\" x=\"" << tx + dynUnit * 50 << "\" y=\"" << ty + dynUnit * 5.0 << "\">РОЗМІРИ</text>\n";
        svg << "  <text class=\"t-text\" fill=\"white\" style=\"font-weight:bold;\" x=\"" << tx + dynUnit * 90 << "\" y=\"" << ty + dynUnit * 5.0 << "\">КООРДИНАТИ 4-Х КУТІВ (мм)</text>\n";
        ty += rowH;

        // Деталі
        for (size_t k = 0; k < sheet.parts.size(); ++k) {
            const auto& p = sheet.parts[k];
            std::string p_type = p.meta.value("type", "unknown");
            std::string displayName = p.meta.contains("name") ? p.meta["name"].get<std::string>() : TranslateType(p_type);
            std::string d = std::to_string((int)p.meta.value("w", 0.0)) + "x" + std::to_string((int)p.meta.value("h", 0.0));
            if (p_type == "circle") d = "R" + std::to_string((int)p.meta.value("r", 0.0));

            std::stringstream ss;
            for (size_t pt_idx = 0; pt_idx < std::min((size_t)4, p.path.size()); ++pt_idx) {
                ss << "(" << (int)(p.path[pt_idx].x / SCALE) << "," << (int)(p.path[pt_idx].y / SCALE) << ")";
                if (pt_idx < 3) ss << "; ";
            }
            std::string bg_color = (k % 2 == 0) ? "#f8f9fa" : "#ffffff";
            svg << "  <rect fill=\"" << bg_color << "\" x=\"" << tx << "\" y=\"" << ty << "\" width=\"" << tableW << "\" height=\"" << rowH << "\"/>\n";
            svg << "  <text class=\"t-cell-bold\" x=\"" << tx + dynUnit * 2 << "\" y=\"" << ty + dynUnit * 5.0 << "\">" << displayName << "</text>\n";
            svg << "  <text class=\"t-cell\" x=\"" << tx + dynUnit * 50 << "\" y=\"" << ty + dynUnit * 5.0 << "\">" << d << "</text>\n";
            svg << "  <text class=\"t-cell\" x=\"" << tx + dynUnit * 90 << "\" y=\"" << ty + dynUnit * 5.0 << "\">" << ss.str() << "</text>\n";
            ty += rowH;
        }

        // --- ПІДСУМОК ПЛОЩІ (FOOTER ТАБЛИЦІ) ---
        double totalSheetAreaM2 = (sw_mm * sh_mm) / 1000000.0;
        double usedM2 = usedAreaMm2 / 1000000.0;
        double leftM2 = totalSheetAreaM2 - usedM2;
        double leftPercent = (totalSheetAreaM2 > 0) ? (leftM2 / totalSheetAreaM2) * 100.0 : 0.0;

        ty += (dynUnit * 2.0);
        svg << "  <rect class=\"t-foot\" x=\"" << tx << "\" y=\"" << ty << "\" width=\"" << tableW << "\" height=\"" << rowH * 3.2 << "\" rx=\"2\"/>\n";

        auto drawRes = [&](int rowIdx, std::string lbl, std::string val) {
            double ry = ty + (rowIdx * rowH) + dynUnit * 5.5;
            svg << "  <text class=\"t-res-label\" x=\"" << tx + dynUnit * 5 << "\" y=\"" << ry << "\">" << lbl << "</text>\n";
            svg << "  <text class=\"t-res-val\" x=\"" << tx + dynUnit * 100 << "\" y=\"" << ry << "\">" << val << "</text>\n";
            };

        std::stringstream sTotal, sLeft, sPerc;
        sTotal << std::fixed << std::setprecision(4) << totalSheetAreaM2 << " m2";
        sLeft << std::fixed << std::setprecision(4) << std::max(0.0, leftM2) << " m2";
        sPerc << std::fixed << std::setprecision(2) << std::max(0.0, leftPercent) << " %";

        drawRes(0, (const char*)u8"Загальна площа аркуша:", sTotal.str());
        drawRes(1, (const char*)u8"Залишилося вільної площі:", sLeft.str());
        drawRes(2, (const char*)u8"Відсоток залишку:", sPerc.str());

        ty += rowH * 4;
        double maxBlockH = std::max(sh + (dynUnit * 15.0), ty - currentY);
        currentY += maxBlockH + (dynUnit * 40.0);
    }
    svg << "</svg>";
}