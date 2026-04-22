/*
 * ==============================================================================
 * ФАЙЛ: nesting.cpp (та nesting.h)
 * ОПИС: Ядро алгоритму розкрою (Nesting Engine / Bottom-Left Algorithm).
 *       Тут відбувається вся магія розміщення.
 *       Функція RunNesting використовує багатопоточність (std::thread) для
 *       паралельного сканування сітки аркуша.
 *       Застосовує оптимізації:
 *       - Early Intersection (сортування перешкод по осі X).
 *       - Smart X-Jump (стрибок через перешкоду на ширину фактичного перетину
 *         полігонів Clipper2, щоб не сканувати кожен міліметр всередині деталей).
 *       Шукає позицію з найменшим Y (глобальний мінімум) та найменшим X.
 * ==============================================================================
 */
#pragma once
#include "types.h"

void RunNesting(
    std::vector<Part>& queue,
    std::vector<PlacedSheet>& allSheets,
    const std::vector<SheetTemplate>& available_sheets,
    int64_t marg,
    int64_t step,
    double& total_placed_area_mm2,
    size_t& totalPlaced
);