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
    double perimeter; // Довжина різу в мм
    size_t q_index;
    bool is_placed;
};

struct PlacedPart {
    json meta;
    Path64 path;
    int angle;
};

struct PlacedSheet {
    std::vector<PlacedPart> parts;
    SheetTemplate sheet_def;
};

struct Obstacle {
    Path64 path;
    Rect64 bounds;
};

enum StatusCode {
    ST_OK = 0,
    ST_ERR_INPUT_FILE = 1,
    ST_ERR_EMPTY_DATA = 2,
    ST_ERR_GEOMETRY = 3,
    ST_ERR_BIG_PARTS = 4,
    ST_ERR_CRITICAL = 5
};

inline std::string GetStatusString(int code) {
    switch (code) {
    case ST_OK:             return "ST_OK";
    case ST_ERR_INPUT_FILE: return "ST_ERR_INPUT_FILE";
    case ST_ERR_EMPTY_DATA: return "ST_ERR_EMPTY_DATA";
    case ST_ERR_GEOMETRY:   return "ST_ERR_GEOMETRY";
    case ST_ERR_BIG_PARTS:  return "ST_ERR_BIG_PARTS";
    case ST_ERR_CRITICAL:   return "ST_ERR_CRITICAL";
    default:                return "ST_UNKNOWN";
    }
}