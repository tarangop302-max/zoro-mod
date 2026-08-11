#ifndef NTL_BOT_CORE_H
#define NTL_BOT_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#define NTL_BOT_MAX_BINS 64
#define NTL_BOT_MAX_PATH_POINTS 4096
#define NTL_BOT_MAX_THREATS 8192

typedef struct {
    float x;
    float y;
} NtlVec2;

typedef struct {
    float x;
    float y;
    float fx;
    float fy;
    int dying;
} NtlSegment;

typedef struct {
    int id;
    float x;
    float y;
    float fx;
    float fy;
    float heading;
    float speed;
    float mass;
    float length_score;
    const NtlSegment *segments;
    size_t segment_count;
    int alive;
} NtlSnakeView;

typedef struct {
    float x;
    float y;
    float mass;
    int eaten;
} NtlFoodView;

typedef struct {
    NtlVec2 center;
    float center_heading;
    float radius;
    const NtlSnakeView *enemy_snakes;
    size_t enemy_count;
    const NtlFoodView *foods;
    size_t food_count;
} NtlWorldView;

typedef struct {
    float angle_step;
    float danger_radius_multiplier;
    float food_min_mass;
    float avoid_bias;
    float close_threat_radius_multiplier;
    float high_occupancy_ratio;
    float close_occupancy_ratio;
    float base_speed;
    float front_cone;
    float food_turn_tolerance;
    float circle_length_threshold;
    float body_follow_look_ahead;
    float body_follow_spacing;
    float body_follow_offset;
    float scan_box_size;
    int use_boundary_threats;
    int smart_body_follow;
    int immediate_boost_enemy_length;
} NtlBotConfig;

typedef enum {
    NTL_BOT_MODE_FOOD = 0,
    NTL_BOT_MODE_EVADE = 1,
    NTL_BOT_MODE_BODY_FOLLOW = 2,
    NTL_BOT_MODE_RECENTER = 3
} NtlBotMode;

typedef struct {
    NtlBotMode mode;
    NtlVec2 target;
    float aim_angle;
    int boost;
} NtlBotDecision;

void ntl_bot_default_config(NtlBotConfig *cfg);
void ntl_bot_update(const NtlWorldView *world,
                    const NtlSnakeView *self,
                    const NtlBotConfig *cfg,
                    NtlBotDecision *out_decision);

#ifdef __cplusplus
}
#endif

#endif
