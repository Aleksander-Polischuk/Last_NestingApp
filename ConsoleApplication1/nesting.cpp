/*
 * ==============================================================================
 * ФАЙЛ: nesting.cpp
 * ОПИС: Ядро алгоритму розкрою (Nesting Engine).
 *       Використовує надійний багатопотоковий Grid Scan з оптимізацією
 *       X-Jump та Early Intersection. Формула оцінки (score) оптимізована
 *       для стягування деталей у щільний блок (Мангеттенська гравітація).
 * ==============================================================================
 */
#include "nesting.h"
#include "geometry.h"
#include <iostream>
#include <thread>
#include <atomic>
#include <algorithm>

namespace {
    struct ThreadLocalBest {
        int64_t y = INT64_MAX;
        int64_t x = INT64_MAX;
        int64_t score = INT64_MAX; // Оцінка компактності
        Path64 finalInflated;
        Rect64 bounds;
        int angle = -1;
    };
}

void RunNesting(std::vector<Part>& queue, std::vector<PlacedSheet>& allSheets, const std::vector<SheetTemplate>& available_sheets, int64_t marg, int64_t step, double& total_placed_area_mm2, size_t& totalPlaced) {
    size_t totalItems = queue.size();
    unsigned int num_cores = std::thread::hardware_concurrency();
    if (num_cores == 0) num_cores = 4;

    const int64_t COARSE_MULT = 4;
    const int64_t coarse_step = step * COARSE_MULT;

    while (totalPlaced < totalItems) {
        int best_sheet_idx = -1;
        double best_roi = -1.0;

        std::vector<PlacedPart> best_sheet_parts;
        std::vector<Obstacle> best_sheet_obstacles;
        std::vector<size_t> best_placed_indices;
        double best_area_for_sheet = 0;

        int sheetNum = (int)allSheets.size() + 1;
        std::cout << "Обробка аркуша №" << sheetNum << "... (Залишилось деталей: " << (totalItems - totalPlaced) << " / " << totalItems << ")\n";

        for (size_t s_idx = 0; s_idx < available_sheets.size(); ++s_idx) {
            const auto& sheet_tpl = available_sheets[s_idx];
            int64_t sW = static_cast<int64_t>(sheet_tpl.w * SCALE);
            int64_t sH = static_cast<int64_t>(sheet_tpl.h * SCALE);

            std::vector<PlacedPart> test_parts;
            std::vector<Obstacle> test_obs;
            std::vector<size_t> test_placed_indices;
            double test_area = 0;

            std::vector<bool> sim_placed(queue.size());
            for (size_t i = 0; i < queue.size(); ++i) sim_placed[i] = queue[i].is_placed;

            for (auto& part : queue) {
                if (sim_placed[part.q_index]) continue;

                std::atomic<int64_t> global_best_score{ INT64_MAX };
                std::vector<ThreadLocalBest> local_bests(num_cores);
                std::vector<std::thread> workers;

                int64_t min_shape_h = INT64_MAX;
                int64_t max_shape_h = 0;
                for (const auto& shape : part.prepared_shapes) {
                    if (shape.bounds.Height() < min_shape_h) min_shape_h = shape.bounds.Height();
                    if (shape.bounds.Height() > max_shape_h) max_shape_h = shape.bounds.Height();
                }

                int64_t absolute_end_y = sH - marg - min_shape_h;
                int64_t current_bottom_y = marg;
                for (const auto& obs : test_obs) {
                    if (obs.bounds.bottom > current_bottom_y) current_bottom_y = obs.bounds.bottom;
                }

                int64_t dynamic_end_y = current_bottom_y + max_shape_h + static_cast<int64_t>(50.0 * SCALE);
                if (dynamic_end_y > absolute_end_y) dynamic_end_y = absolute_end_y;

                int64_t total_coarse_steps = (dynamic_end_y - marg) / coarse_step + 1;
                if (total_coarse_steps <= 0) continue;

                for (unsigned int t = 0; t < num_cores; ++t) {
                    workers.emplace_back([&, t, total_coarse_steps, dynamic_end_y, sW, sH]() {
                        ThreadLocalBest local;
                        int64_t steps_per_thread = total_coarse_steps / num_cores;
                        int64_t remainder = total_coarse_steps % num_cores;
                        int64_t my_start_step = t * steps_per_thread + std::min((int64_t)t, remainder);
                        int64_t my_end_step = my_start_step + steps_per_thread + (t < remainder ? 1 : 0);

                        Clipper64 clipper;
                        Path64 translated_shape;
                        Paths64 subj_paths(1);
                        Paths64 clip_paths(1);
                        Paths64 overlap_result;

                        auto ScanRow = [&](int64_t y, ThreadLocalBest& row_best) -> bool {
                            bool row_found = false;
                            for (const auto& shape : part.prepared_shapes) {
                                int64_t cand_bottom = y + shape.bounds.Height();
                                if (cand_bottom > sH - marg) continue;

                                std::vector<const Obstacle*> local_obstacles;
                                for (const auto& obs : test_obs) {
                                    if (obs.bounds.bottom >= y && obs.bounds.top <= cand_bottom) {
                                        local_obstacles.push_back(&obs);
                                    }
                                }

                                std::sort(local_obstacles.begin(), local_obstacles.end(), [](const Obstacle* a, const Obstacle* b) {
                                    return a->bounds.left < b->bounds.left;
                                    });

                                for (int64_t x = marg; x <= sW - marg - shape.bounds.Width(); ) {
                                    Rect64 candBounds = shape.bounds;
                                    candBounds.left += x; candBounds.right += x;
                                    candBounds.top += y; candBounds.bottom += y;

                                    bool collision = false;
                                    int64_t next_x = x + step;

                                    for (const auto* obs_ptr : local_obstacles) {
                                        const auto& obs = *obs_ptr;
                                        if (obs.bounds.left > candBounds.right) break;

                                        if (BoundsOverlap(candBounds, obs.bounds)) {
                                            translated_shape.resize(shape.inflated.size());
                                            for (size_t k = 0; k < shape.inflated.size(); ++k) {
                                                translated_shape[k].x = shape.inflated[k].x + x;
                                                translated_shape[k].y = shape.inflated[k].y + y;
                                            }

                                            subj_paths[0] = obs.path;
                                            clip_paths[0] = translated_shape;

                                            clipper.Clear();
                                            clipper.AddSubject(subj_paths);
                                            clipper.AddClip(clip_paths);
                                            clipper.Execute(ClipType::Intersection, FillRule::NonZero, overlap_result);

                                            if (!overlap_result.empty()) {
                                                collision = true;
                                                Rect64 overlapBounds = GetBounds(overlap_result);
                                                int64_t shift = overlapBounds.right - overlapBounds.left;
                                                if (shift < step) shift = step;

                                                int64_t jump = x + shift;
                                                int64_t rem = (jump - marg) % step;
                                                if (rem != 0) jump += (step - rem);

                                                next_x = std::max(next_x, jump);
                                                break;
                                            }
                                        }
                                    }

                                    if (!collision) {
                                        // ОЦІНКА: Збалансована гравітація. 
                                        // Стягуємо фігури до кута. y * 2 гарантує, що аркуш
                                        // заповнюватиметься знизу вгору щільними рядами.
                                        int64_t score = y * sW + x;

                                        if (score < row_best.score) {
                                            row_best.score = score;
                                            row_best.y = y;
                                            row_best.x = x;
                                            row_best.angle = shape.angle;
                                            row_best.finalInflated = TranslatePath(shape.inflated, x, y);
                                            row_best.bounds = candBounds;
                                        }
                                        row_found = true;
                                        break; // Переходимо до наступного кута (shape)
                                    }
                                    else {
                                        x = next_x;
                                    }
                                }
                            }
                            return row_found;
                            };

                        for (int64_t i = my_start_step; i < my_end_step; ++i) {
                            int64_t y_coarse = marg + i * coarse_step;

                            // Ранній вихід: якщо мінімально можливий score для цього Y 
                            // вже гірший за глобально знайдений найкращий
                            int64_t min_possible_score = y_coarse * sW + marg;
                            if (min_possible_score > global_best_score.load(std::memory_order_relaxed)) break;

                            ThreadLocalBest coarse_best;
                            bool found_coarse = ScanRow(y_coarse, coarse_best);

                            if (found_coarse) {
                                int64_t y_fine_start = std::max(marg, y_coarse - coarse_step + step);
                                bool found_fine = false;
                                ThreadLocalBest fine_best;

                                for (int64_t y_fine = y_fine_start; y_fine < y_coarse; y_fine += step) {
                                    if (ScanRow(y_fine, fine_best)) {
                                        found_fine = true;
                                        break;
                                    }
                                }

                                local = found_fine ? fine_best : coarse_best;

                                int64_t current_best_score = global_best_score.load(std::memory_order_relaxed);
                                while (local.score < current_best_score) {
                                    if (global_best_score.compare_exchange_weak(current_best_score, local.score, std::memory_order_relaxed)) {
                                        break;
                                    }
                                }
                                break;
                            }
                        }

                        if (local.angle != -1) {
                            local_bests[t] = local;
                        }
                        });
                }

                for (auto& w : workers) w.join();

                ThreadLocalBest absolute_best;
                for (const auto& lb : local_bests) {
                    if (lb.angle != -1) {
                        if (lb.score < absolute_best.score) {
                            absolute_best = lb;
                        }
                    }
                }

                if (absolute_best.angle != -1) {
                    test_obs.push_back({ absolute_best.finalInflated, absolute_best.bounds });
                    test_parts.push_back({ part.meta, TranslatePath(RotatePath(part.path, absolute_best.angle), absolute_best.x, absolute_best.y), absolute_best.angle });
                    sim_placed[part.q_index] = true;
                    test_placed_indices.push_back(part.q_index);
                    test_area += part.area;
                }
            }

            double current_roi = 0;
            if (sheet_tpl.cost > 0.001) {
                current_roi = test_area / sheet_tpl.cost;
            }
            else {
                current_roi = test_area;
            }

            if (current_roi > best_roi && !test_parts.empty()) {
                best_roi = current_roi;
                best_sheet_idx = static_cast<int>(s_idx);
                best_sheet_parts = std::move(test_parts);
                best_sheet_obstacles = std::move(test_obs);
                best_placed_indices = std::move(test_placed_indices);
                best_area_for_sheet = test_area;
            }
        }

        if (best_sheet_idx == -1) {
            std::cerr << "Помилка: Деталі, що залишилися, не влазять у жоден з доступних форматів листів!\n";
            break;
        }

        for (size_t q_idx : best_placed_indices) {
            queue[q_idx].is_placed = true;
            totalPlaced++;
        }
        total_placed_area_mm2 += best_area_for_sheet / (SCALE * SCALE);

        allSheets.push_back({ best_sheet_parts, available_sheets[best_sheet_idx] });

        // --- ДОДАНО: ОПТИМІЗАЦІЯ ДУБЛЮВАННЯ АРКУШІВ ---
        // 1. Рахуємо, скільки яких деталей (за ID) лежить на щойно запакованому аркуші
        std::map<int, int> placed_ids_count;
        for (const auto& p : best_sheet_parts) {
            int pid = p.meta.value("id", 0);
            placed_ids_count[pid]++;
        }

        // 2. Спробуємо дублювати цей аркуш, поки вистачає нерозміщених деталей
        bool can_duplicate = true;
        while (can_duplicate && totalPlaced < totalItems) {
            // Рахуємо залишок деталей у черзі
            std::map<int, int> remaining_ids_count;
            for (const auto& qp : queue) {
                if (!qp.is_placed) {
                    int pid = qp.meta.value("id", 0);
                    remaining_ids_count[pid]++;
                }
            }

            // Перевіряємо, чи вистачає залишку для повної копії аркуша
            for (const auto& kv : placed_ids_count) {
                if (remaining_ids_count[kv.first] < kv.second) {
                    can_duplicate = false;
                    break;
                }
            }

            // Якщо вистачає - миттєво створюємо копію!
            if (can_duplicate) {
                std::map<int, int> to_mark = placed_ids_count;
                for (auto& qp : queue) {
                    if (!qp.is_placed) {
                        int pid = qp.meta.value("id", 0);
                        if (to_mark[pid] > 0) {
                            qp.is_placed = true;
                            to_mark[pid]--;
                            totalPlaced++;
                        }
                    }
                }

                // Додаємо точну копію аркуша в загальний масив
                allSheets.push_back({ best_sheet_parts, available_sheets[best_sheet_idx] });
                total_placed_area_mm2 += best_area_for_sheet / (SCALE * SCALE);

                std::cout << "-> Миттєво здубльовано аркуш! (Залишилось деталей: " << (totalItems - totalPlaced) << ")\n";
            }
        }
        // --- КІНЕЦЬ ОПТИМІЗАЦІЇ ---
    }


}