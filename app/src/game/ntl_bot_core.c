#include "ntl_bot_core.h"

#include <math.h>
#include <float.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    float x;
    float y;
    float radius;
} Circle;

typedef struct {
    float x;
    float y;
    float distance;
    float edge_distance;
    float angle;
    float radius;
    int snake_index;
    int is_head;
    int bin_index;
} Threat;

typedef struct {
    float x;
    float y;
    float angle;
    float distance;
    float radius;
    int snake_index;
    int bin_index;
    int is_head;
} BinThreat;

typedef struct {
    NtlVec2 point;
    float len;
} PathPoint;

typedef struct {
    PathPoint points[NTL_BOT_MAX_PATH_POINTS];
    size_t count;
    float total_length;
} BodyPath;

typedef struct {
    Circle head_circle;
    float radius;
    float diameter;
    float speed_mult;
    float length_score;
    float scan_min_x;
    float scan_min_y;
    float scan_max_x;
    float scan_max_y;
    float turn_side;
    NtlVec2 follow_target;
    BodyPath path;
    BinThreat bins[NTL_BOT_MAX_BINS];
    unsigned char bin_used[NTL_BOT_MAX_BINS];
    Threat threats[NTL_BOT_MAX_THREATS];
    size_t threat_count;
    int bin_count;
} Runtime;

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static float sqr_distance(float ax, float ay, float bx, float by) {
    float dx = ax - bx;
    float dy = ay - by;
    return dx * dx + dy * dy;
}

static float angle_to(float ax, float ay, float bx, float by) {
    return atan2f(by - ay, bx - ax);
}

static float wrap_angle(float a) {
    float two_pi = (float)(M_PI * 2.0);
    float r = fmodf(a, two_pi);
    if (r < 0.0f) r += two_pi;
    return r;
}

static float shortest_angle(float from, float to) {
    float a = fmodf(to - from, (float)(M_PI * 2.0));
    if (a < -(float)M_PI) a += (float)(M_PI * 2.0);
    if (a >  (float)M_PI) a -= (float)(M_PI * 2.0);
    return a;
}

static NtlVec2 normalize_vec(NtlVec2 v) {
    float len = sqrtf(v.x * v.x + v.y * v.y);
    NtlVec2 out = {0.0f, 0.0f};
    if (len > 0.0f) {
        out.x = v.x / len;
        out.y = v.y / len;
    }
    return out;
}

static float snake_radius(const NtlSnakeView *snake) {
    return 14.6f * (snake->mass > 0.0f ? snake->mass : 1.0f);
}

static float snake_length_score(const NtlSnakeView *snake) {
    if (snake->length_score > 0.0f) return snake->length_score;
    return (float)snake->segment_count * snake_radius(snake) * 0.5f;
}

static int quantize_angle(float angle, float step, int bin_count) {
    int idx = (int)lroundf(wrap_angle(angle) / step);
    if (idx == bin_count) idx = 0;
    if (idx < 0) idx = 0;
    if (idx >= bin_count) idx = bin_count - 1;
    return idx;
}

static int point_in_scan_rect(const Runtime *rt, float x, float y) {
    return x >= rt->scan_min_x && x <= rt->scan_max_x && y >= rt->scan_min_y && y <= rt->scan_max_y;
}

static void build_body_path(const NtlSnakeView *self, Runtime *rt) {
    float head_x = self->x + self->fx;
    float head_y = self->y + self->fy;
    float total = 0.0f;
    float prev_x = head_x;
    float prev_y = head_y;

    rt->path.count = 0;
    rt->path.total_length = 0.0f;

    rt->path.points[rt->path.count++] = (PathPoint){{head_x, head_y}, 0.0f};

    for (size_t idx = self->segment_count; idx > 0; idx--) {
        const NtlSegment *seg = &self->segments[idx - 1];
        if (seg->dying) continue;
        if (rt->path.count >= NTL_BOT_MAX_PATH_POINTS) break;
        {
            float x = seg->x + seg->fx;
            float y = seg->y + seg->fy;
            total += sqrtf(sqr_distance(prev_x, prev_y, x, y));
            rt->path.points[rt->path.count++] = (PathPoint){{x, y}, total};
            prev_x = x;
            prev_y = y;
        }
    }

    rt->path.total_length = total;
}

static NtlVec2 sample_path_distance(const Runtime *rt, float distance) {
    NtlVec2 out = {0.0f, 0.0f};
    if (rt->path.count == 0) return out;
    if (distance <= 0.0f) return rt->path.points[0].point;
    if (distance >= rt->path.total_length) return rt->path.points[rt->path.count - 1].point;

    size_t lo = 0;
    size_t hi = rt->path.count - 1;
    while (hi - lo > 1) {
        size_t mid = (lo + hi) / 2;
        if (distance > rt->path.points[mid].len) lo = mid;
        else hi = mid;
    }

    {
        PathPoint left = rt->path.points[lo];
        PathPoint right = rt->path.points[hi];
        float span = right.len - left.len;
        float t = span <= 0.0f ? 1.0f : (distance - left.len) / span;
        out.x = left.point.x + (right.point.x - left.point.x) * t;
        out.y = left.point.y + (right.point.y - left.point.y) * t;
    }

    return out;
}

static float nearest_path_distance(const Runtime *rt, NtlVec2 point) {
    float best_d2 = FLT_MAX;
    float best_len = 0.0f;
    if (rt->path.count < 2) return 0.0f;

    for (size_t i = 1; i < rt->path.count; i++) {
        NtlVec2 a = rt->path.points[i - 1].point;
        NtlVec2 b = rt->path.points[i].point;
        float abx = b.x - a.x;
        float aby = b.y - a.y;
        float apx = point.x - a.x;
        float apy = point.y - a.y;
        float denom = abx * abx + aby * aby;
        float t = denom <= 0.0f ? 0.0f : clampf((apx * abx + apy * aby) / denom, 0.0f, 1.0f);
        float x = a.x + abx * t;
        float y = a.y + aby * t;
        float d2 = sqr_distance(point.x, point.y, x, y);
        if (d2 < best_d2) {
          best_d2 = d2;
          best_len = rt->path.points[i - 1].len + sqrtf(sqr_distance(a.x, a.y, x, y));
        }
    }
    return best_len;
}

static void register_threat(const NtlSnakeView *self,
                            const NtlBotConfig *cfg,
                            Runtime *rt,
                            float x,
                            float y,
                            float radius,
                            int snake_index,
                            int is_head) {
    if (rt->threat_count >= NTL_BOT_MAX_THREATS) return;

    {
        float dist2 = sqr_distance(self->x, self->y, x, y);
        float angle = angle_to(self->x, self->y, x, y);
        float edge = sqrtf(dist2) - radius;
        float edge_distance = edge > 0.0f ? edge * edge : 0.0f;
        int bin_index = quantize_angle(angle, cfg->angle_step, rt->bin_count);
        Threat *th = &rt->threats[rt->threat_count++];
        *th = (Threat){x, y, dist2, edge_distance, angle, radius, snake_index, is_head, bin_index};

        if (!rt->bin_used[bin_index] || rt->bins[bin_index].distance > edge_distance) {
            rt->bin_used[bin_index] = 1;
            rt->bins[bin_index] = (BinThreat){x, y, angle, edge_distance, radius, snake_index, bin_index, is_head};
        }
    }
}

static void init_runtime(const NtlWorldView *world,
                         const NtlSnakeView *self,
                         const NtlBotConfig *cfg,
                         Runtime *rt) {
    memset(rt, 0, sizeof(*rt));
    rt->radius = snake_radius(self);
    rt->diameter = rt->radius * 2.0f;
    rt->speed_mult = (self->speed > 0.0f ? self->speed : cfg->base_speed) / cfg->base_speed;
    rt->length_score = snake_length_score(self);
    rt->bin_count = (int)lroundf((float)(M_PI * 2.0) / cfg->angle_step);
    if (rt->bin_count > NTL_BOT_MAX_BINS) rt->bin_count = NTL_BOT_MAX_BINS;
    if (rt->bin_count < 1) rt->bin_count = 1;
    rt->scan_min_x = self->x - cfg->scan_box_size * 0.5f;
    rt->scan_min_y = self->y - cfg->scan_box_size * 0.5f;
    rt->scan_max_x = self->x + cfg->scan_box_size * 0.5f;
    rt->scan_max_y = self->y + cfg->scan_box_size * 0.5f;
    rt->turn_side = 1.0f;
    rt->head_circle = (Circle){
        self->x + cosf(self->heading) * fminf(1.0f, rt->speed_mult - 1.0f) * cfg->danger_radius_multiplier * rt->radius * 0.5f,
        self->y + sinf(self->heading) * fminf(1.0f, rt->speed_mult - 1.0f) * cfg->danger_radius_multiplier * rt->radius * 0.5f,
        cfg->danger_radius_multiplier * rt->radius * 0.5f
    };
    rt->follow_target = world->center;
    if (world->radius > 0.0f) {
        rt->follow_target.x = world->center.x + cosf(world->center_heading) * world->radius * 0.98f;
        rt->follow_target.y = world->center.y + sinf(world->center_heading) * world->radius * 0.98f;
    }
    build_body_path(self, rt);
}

static void scan_threats(const NtlWorldView *world,
                         const NtlSnakeView *self,
                         const NtlBotConfig *cfg,
                         Runtime *rt) {
    for (size_t i = 0; i < world->enemy_count; i++) {
        const NtlSnakeView *enemy = &world->enemy_snakes[i];
        if (!enemy->alive || enemy->id == self->id) continue;

        {
            float er = snake_radius(enemy);
            float lead = fminf(1.0f, (enemy->speed / cfg->base_speed) - 1.0f);
            register_threat(self, cfg, rt,
                            enemy->x + cosf(enemy->heading) * er * lead * cfg->danger_radius_multiplier * 0.5f,
                            enemy->y + sinf(enemy->heading) * er * lead * cfg->danger_radius_multiplier * 0.5f,
                            rt->head_circle.radius,
                            (int)i,
                            1);

            for (size_t s = 0; s < enemy->segment_count; s++) {
                const NtlSegment *seg = &enemy->segments[s];
                if (seg->dying) continue;
                if (!point_in_scan_rect(rt, seg->x, seg->y)) continue;
                register_threat(self, cfg, rt, seg->x, seg->y, er, (int)i, 0);
            }
        }
    }

    if (cfg->use_boundary_threats && world->radius > 0.0f) {
        float center_dist = sqrtf(sqr_distance(self->x, self->y, world->center.x, world->center.y));
        if (center_dist > world->radius - 3500.0f) {
            for (int offset = -1000; offset <= 1000; offset += 8) {
                float angle = world->center_heading + (float)offset / fmaxf(1.0f, world->radius);
                register_threat(self, cfg, rt,
                                world->center.x + cosf(angle) * world->radius * 0.98f,
                                world->center.y + sinf(angle) * world->radius * 0.98f,
                                8.0f,
                                -1,
                                0);
            }
        }
    }
}

static const Threat *find_immediate_collision(const NtlSnakeView *self,
                                              const NtlBotConfig *cfg,
                                              const Runtime *rt) {
    for (size_t i = 0; i < rt->threat_count; i++) {
        const Threat *th = &rt->threats[i];
        float combined = rt->head_circle.radius + th->radius;
        if (sqr_distance(rt->head_circle.x, rt->head_circle.y, th->x, th->y) < combined * combined) {
            if (fabsf(shortest_angle(self->heading, th->angle)) < cfg->front_cone) {
                return th;
            }
        }
    }
    return NULL;
}

static float choose_escape_heading(const Runtime *rt, const NtlBotConfig *cfg) {
    int open_start = -1;
    int best_start = -1;
    int best_end = -1;
    int best_span = -1;
    float farthest_angle = 0.0f;
    float farthest_distance = FLT_MAX;

    for (int i = 0; i < rt->bin_count; i++) {
        if (!rt->bin_used[i]) {
            if (open_start < 0) open_start = i;
        } else {
            if (rt->bins[i].distance < farthest_distance && rt->bins[i].distance != 0.0f) {
                farthest_distance = rt->bins[i].distance;
                farthest_angle = (float)i * cfg->angle_step;
            }
            if (open_start >= 0) {
                int span = i - 1 - open_start;
                if (span > best_span) {
                    best_span = span;
                    best_start = open_start;
                    best_end = i - 1;
                }
                open_start = -1;
            }
        }
    }

    if (open_start >= 0) {
        int span = rt->bin_count - 1 - open_start;
        if (span > best_span) {
            best_span = span;
            best_start = open_start;
            best_end = rt->bin_count - 1;
        }
    }

    if (best_start >= 0) {
        return wrap_angle(((float)best_start + (float)best_end) * 0.5f * cfg->angle_step);
    }

    return farthest_angle;
}

static void compute_occupancy(const Runtime *rt,
                              const NtlBotConfig *cfg,
                              float *occupied_ratio,
                              float *close_ratio,
                              int *dominant_snake,
                              int *dominant_count) {
    int per_snake[256];
    int occupied = 0;
    int close_bins = 0;
    memset(per_snake, 0, sizeof(per_snake));
    *dominant_snake = -1;
    *dominant_count = 0;

    for (int i = 0; i < rt->bin_count; i++) {
        if (!rt->bin_used[i]) continue;
        occupied++;
        if (rt->bins[i].snake_index >= 0 && rt->bins[i].snake_index < 256) {
            per_snake[rt->bins[i].snake_index]++;
            if (per_snake[rt->bins[i].snake_index] > *dominant_count) {
                *dominant_count = per_snake[rt->bins[i].snake_index];
                *dominant_snake = rt->bins[i].snake_index;
            }
        }
        if (rt->bins[i].distance < (rt->radius * cfg->close_threat_radius_multiplier) *
                                   (rt->radius * cfg->close_threat_radius_multiplier)) {
            close_bins++;
        }
    }

    *occupied_ratio = rt->bin_count > 0 ? (float)occupied / (float)rt->bin_count : 0.0f;
    *close_ratio = rt->bin_count > 0 ? (float)close_bins / (float)rt->bin_count : 0.0f;
}

static int select_food_target(const NtlWorldView *world,
                              const NtlSnakeView *self,
                              const NtlBotConfig *cfg,
                              const Runtime *rt,
                              NtlVec2 *target,
                              float *angle_out) {
    float best_score = -1.0f;
    int found = 0;

    for (size_t i = 0; i < world->food_count; i++) {
        const NtlFoodView *food = &world->foods[i];
        if (food->eaten) continue;

        {
            float angle = angle_to(self->x, self->y, food->x, food->y);
            float delta = fabsf(shortest_angle(self->heading, angle));
            float d2 = sqr_distance(self->x, self->y, food->x, food->y);
            int bin = quantize_angle(angle, cfg->angle_step, rt->bin_count);

            if (delta > cfg->food_turn_tolerance) continue;
            if (rt->bin_used[bin] && rt->bins[bin].distance < d2 + rt->radius * cfg->danger_radius_multiplier) continue;

            {
                float score = (food->mass * food->mass) / fmaxf(1.0f, d2);
                if (score > best_score) {
                    best_score = score;
                    target->x = food->x;
                    target->y = food->y;
                    *angle_out = angle;
                    found = 1;
                }
            }
        }
    }

    return found;
}

static int body_follow_target(const NtlWorldView *world,
                              const NtlSnakeView *self,
                              const NtlBotConfig *cfg,
                              const Runtime *rt,
                              NtlVec2 *target,
                              float *angle_out) {
    if (rt->path.count < 3 || rt->path.total_length < 9.0f * rt->diameter) return 0;

    {
        NtlVec2 self_point = {self->x + self->fx, self->y + self->fy};
        float nearest = nearest_path_distance(rt, self_point);
        float look_ahead = nearest + cfg->body_follow_look_ahead * rt->diameter;
        float behind = fmaxf(0.0f, nearest - rt->diameter);
        NtlVec2 a = sample_path_distance(rt, look_ahead);
        NtlVec2 b = sample_path_distance(rt, behind);
        NtlVec2 tangent = normalize_vec((NtlVec2){a.x - b.x, a.y - b.y});
        NtlVec2 normal = {-tangent.y * rt->turn_side, tangent.x * rt->turn_side};
        float offset = rt->diameter * cfg->body_follow_offset;
        NtlVec2 center_pull = normalize_vec((NtlVec2){rt->follow_target.x - self->x, rt->follow_target.y - self->y});
        NtlVec2 raw_target = {a.x + normal.x * offset, a.y + normal.y * offset};

        target->x = raw_target.x * 0.75f + (self->x + center_pull.x * offset * 2.0f) * 0.25f;
        target->y = raw_target.y * 0.75f + (self->y + center_pull.y * offset * 2.0f) * 0.25f;
        *angle_out = angle_to(self->x, self->y, target->x, target->y);
    }

    return 1;
}

void ntl_bot_default_config(NtlBotConfig *cfg) {
    if (!cfg) return;
    cfg->angle_step = (float)M_PI / 8.0f;
    cfg->danger_radius_multiplier = 12.0f;
    cfg->food_min_mass = 200.0f;
    cfg->avoid_bias = (float)M_PI / 3.0f;
    cfg->close_threat_radius_multiplier = 30.0f;
    cfg->high_occupancy_ratio = 0.5f;
    cfg->close_occupancy_ratio = 0.5f;
    cfg->base_speed = 5.78f;
    cfg->front_cone = (float)M_PI / 2.0f;
    cfg->food_turn_tolerance = (float)M_PI / 2.0f;
    cfg->circle_length_threshold = 180.0f;
    cfg->body_follow_look_ahead = 5.0f;
    cfg->body_follow_spacing = 3.0f;
    cfg->body_follow_offset = 1.25f;
    cfg->scan_box_size = 2400.0f;
    cfg->use_boundary_threats = 1;
    cfg->smart_body_follow = 1;
    cfg->immediate_boost_enemy_length = 10;
}

void ntl_bot_update(const NtlWorldView *world,
                    const NtlSnakeView *self,
                    const NtlBotConfig *cfg_in,
                    NtlBotDecision *out_decision) {
    NtlBotConfig cfg_local;
    const NtlBotConfig *cfg = cfg_in;
    Runtime rt;
    const Threat *immediate;
    float occupied_ratio = 0.0f;
    float close_ratio = 0.0f;
    int dominant_snake = -1;
    int dominant_count = 0;
    NtlVec2 target = {0.0f, 0.0f};
    float angle = 0.0f;

    if (!world || !self || !out_decision) return;
    if (!cfg) {
        ntl_bot_default_config(&cfg_local);
        cfg = &cfg_local;
    }

    init_runtime(world, self, cfg, &rt);
    scan_threats(world, self, cfg, &rt);

    immediate = find_immediate_collision(self, cfg, &rt);
    if (immediate) {
        angle = choose_escape_heading(&rt, cfg);
        out_decision->mode = NTL_BOT_MODE_EVADE;
        out_decision->target.x = self->x + cosf(angle) * rt.head_circle.radius;
        out_decision->target.y = self->y + sinf(angle) * rt.head_circle.radius;
        out_decision->aim_angle = angle;
        out_decision->boost = immediate->is_head ? 1 : 0;
        return;
    }

    compute_occupancy(&rt, cfg, &occupied_ratio, &close_ratio, &dominant_snake, &dominant_count);
    if (occupied_ratio > cfg->high_occupancy_ratio || close_ratio > cfg->close_occupancy_ratio) {
        angle = choose_escape_heading(&rt, cfg);
        out_decision->mode = NTL_BOT_MODE_EVADE;
        out_decision->target.x = self->x + cosf(angle) * rt.head_circle.radius;
        out_decision->target.y = self->y + sinf(angle) * rt.head_circle.radius;
        out_decision->aim_angle = angle;
        out_decision->boost = dominant_snake >= 0 ? 1 : 0;
        return;
    }

    if (cfg->smart_body_follow && rt.length_score >= cfg->circle_length_threshold &&
        body_follow_target(world, self, cfg, &rt, &target, &angle)) {
        out_decision->mode = NTL_BOT_MODE_BODY_FOLLOW;
        out_decision->target = target;
        out_decision->aim_angle = angle;
        out_decision->boost = 0;
        return;
    }

    if (select_food_target(world, self, cfg, &rt, &target, &angle)) {
        out_decision->mode = NTL_BOT_MODE_FOOD;
        out_decision->target = target;
        out_decision->aim_angle = angle;
        out_decision->boost = 0;
        return;
    }

    out_decision->mode = NTL_BOT_MODE_RECENTER;
    out_decision->target = rt.follow_target;
    out_decision->aim_angle = angle_to(self->x, self->y, rt.follow_target.x, rt.follow_target.y);
    out_decision->boost = 0;
}
