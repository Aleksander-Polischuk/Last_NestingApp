/*
 * ==============================================================================
 * ФАЙЛ: io_utils.cpp (та io_utils.h)
 * ОПИС: Модуль вводу та виводу даних (Input/Output).
 *       - ParseAndCreatePart: читає вхідний JSON, розпізнає типи фігур,
 *         створює їх контури (Path64) та додає в чергу на розміщення.
 *       - SaveJSON: генерує фінальний JSON із координатами розміщених деталей.
 *       - SaveMultiSheetSVG: малює масштабоване векторне креслення
 *         усіх аркушів розкрою з розміткою, статистикою та КВМ.
 * ==============================================================================
 */
#pragma once
#include "types.h"

bool ParseAndCreatePart(const json& item, std::vector<Part>& queue);
//void SaveJSON(const std::string& filename, const std::vector<PlacedSheet>& allSheets, double total_perimeter_mm, double machine_time_min, int error_code = ST_OK);
void SaveJSON(const std::string& filename, const std::vector<PlacedSheet>& allSheets, double machine_speed, double total_perimeter_mm, int error_code = ST_OK);
void SaveMultiSheetSVG(const std::string& filename, const std::vector<PlacedSheet>& allSheets, const json& inputParams, double time_sec, size_t totalPlacedCount, double totalPlacedAreaMm2, double total_cut_length_mm);