/*
 * ==============================================================================
 * ФАЙЛ: types.h
 * ОПИС: Базові типи даних, глобальні константи та структури проекту.
 * ==============================================================================
 */
#pragma once
#include <string>
#include <vector>
#include "json.hpp"
#include "clipper2/clipper.h"

using namespace Clipper2Lib;
using json = nlohmann::json;

const double MY_PI = 3.14159265358979323846;
const double SCALE = 100.0;
const int CIRCLE_SEGMENTS = 64;

struct PreparedShape {
    int angle;
    Path64 inflated;
    Rect64 bounds;
};

// ДОДАНО: Структура формату аркуша зі складу
struct SheetTemplate {
    double w = 2000.0;
    double h = 1000.0;
    double cost = 0.0;
    std::string name = "Default";
};

struct Part {
    json meta;
    Path64 path;
    std::string type;
    std::vector<int> angles;
    std::vector<PreparedShape> prepared_shapes;
    double area;
    double perimeter; // Довжина різу
    size_t q_index;   // Оригінальний індекс деталі
    bool is_placed;
};

struct PlacedPart {
    json meta;
    Path64 path;
    int angle;
};

// ЗМІНЕНО: Тепер аркуш знає свої фізичні розміри та вартість
struct PlacedSheet {
    std::vector<PlacedPart> parts;
    SheetTemplate sheet_def;
};

struct Obstacle {
    Path64 path;
    Rect64 bounds;
};