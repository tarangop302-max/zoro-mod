import modules.common;

// Food shape shader. Used for every food type except plain Solid/Rings with
// Food glow off (those keep the original fd/fdr shaders untouched).
//
// shape: 0 Solid, 1 Rings, 2 Hollow square, 3 Star outline, 4 Star solid,
//        5 Hollow triangle, 6 Asterisk, 7 Sparkle
// glow:  0 = no halo at all, 1 = soft halo around the shape

struct vs_input {
  [[vk::location(0)]] float2 pos;
  [[vk::location(1)]] float3 circ;
  [[vk::location(2)]] float4 color;
  [[vk::location(3)]] float flicker;
  [[vk::location(4)]] float shape;
  [[vk::location(5)]] float glow;
  [[vk::location(6)]] float angle;
};

struct vs_output {
  float4 clipspace : SV_Position;
  [[vk::location(0)]] float2 uv;
  [[vk::location(1)]] float4 color;
  [[vk::location(3)]] float flicker;
  [[vk::location(4)]] float shape;
  [[vk::location(5)]] float glow;
  [[vk::location(6)]] float angle;
  [[vk::location(7)]] float px;
};

[[vk::binding(0, 0)]] ConstantBuffer<Global> global;

vs_output vs_main(vs_input input) {
  vs_output output;

  float2 p = (input.pos - 0.5) * (input.circ.z * 2 + EDGE_SOFTNESS);
  float2 r = p + 0.5 * (input.circ.z + EDGE_SOFTNESS * 0.5);

  float2 hv = global.viewport * 0.5;
  float2 wp = r + input.circ.xy;
  float2 o = (wp - hv) / hv;

  output.clipspace = float4(o, 1);
  output.uv = input.pos;
  output.color = input.color;
  output.flicker = input.flicker;
  output.shape = input.shape;
  output.glow = input.glow;

  // The angle comes from a food id that can be in the millions. GPUs return
  // garbage for sin/cos of huge values, so wrap it into 0..2*PI here.
  float tau = 6.2831853;
  output.angle = input.angle - tau * floor(input.angle / tau);

  // Size of one screen pixel in shape units (food radius = 1, quad edge = 2).
  output.px = 2.0 / (input.circ.z + EDGE_SOFTNESS * 0.5);

  return output;
}

float sd_box(float2 p, float b) {
  float2 d = abs(p) - float2(b, b);
  return length(max(d, float2(0.0, 0.0))) + min(max(d.x, d.y), 0.0);
}

float sd_seg(float2 p, float2 d, float len) {
  float t = clamp(dot(p, d), -len, len);
  return length(p - d * t);
}

float sd_tri(float2 p, float r) {
  float k = sqrt(3.0);
  p.x = abs(p.x) - r;
  p.y = p.y + r / k;
  if (p.x + k * p.y > 0.0) {
    p = float2(p.x - k * p.y, -k * p.x - p.y) / 2.0;
  }
  p.x = p.x - clamp(p.x, -2.0 * r, 0.0);
  return -length(p) * sign(p.y);
}

float sd_star(float2 p, float r, float n, float m) {
  float an = PI / n;
  float en = PI / m;
  float2 acs = float2(cos(an), sin(an));
  float2 ecs = float2(cos(en), sin(en));
  float a = atan2(p.x, p.y);
  float bn = a - 2.0 * an * floor(a / (2.0 * an)) - an;
  p = length(p) * float2(cos(bn), abs(sin(bn)));
  p = p - r * acs;
  p = p + ecs * clamp(-dot(p, ecs), 0.0, r * acs.y / ecs.y);
  return length(p) * sign(p.x);
}

float4 fs_main(vs_output input) {
  float2 u = (input.uv * 2.0 - 1.0) * 2.0;
  float px = input.px;

  float cs = cos(input.angle);
  float sn = sin(input.angle);
  float2 p = float2(cs * u.x + sn * u.y, -sn * u.x + cs * u.y);

  // Bold outlines that never get thinner than about 2 screen pixels, so the
  // hollow shapes stay readable when Food scale is small.
  float hw = max(0.2, 0.9 * px);
  float lw = max(0.17, 0.75 * px);

  int s = int(input.shape + 0.5);
  float sd = 0.0;

  if (s == 0) {
    sd = length(p) - 0.95;
  } else if (s == 1) {
    sd = abs(length(p) - 0.86) - max(0.14, 0.7 * px);
  } else if (s == 2) {
    sd = abs(sd_box(p, 0.8)) - hw;
  } else if (s == 3) {
    sd = abs(sd_star(p, 1.0, 5.0, 2.8)) - hw * 0.8;
  } else if (s == 4) {
    sd = sd_star(p, 1.1, 5.0, 2.8);
  } else if (s == 5) {
    sd = abs(sd_tri(p, 0.85)) - hw;
  } else if (s == 6) {
    float m0 = sd_seg(p, float2(1.0, 0.0), 1.05);
    float m1 = sd_seg(p, float2(0.5, 0.8660254), 1.05);
    float m2 = sd_seg(p, float2(-0.5, 0.8660254), 1.05);
    sd = min(min(m0, m1), m2) - lw;
  } else {
    sd = sd_star(p, 1.35, 4.0, 3.0);
  }

  // One-pixel anti-aliased edge.
  float shape_a = clamp(0.5 - sd / px, 0.0, 1.0) * 0.9;

  // Glow halo: only when the Food glow setting is on.
  float halo = 0.0;
  if (input.glow > 0.5) {
    float g = 1.0 - smoothstep(0.0, 0.9, max(sd, 0.0));
    float edge = 1.0 - smoothstep(1.3, 1.95, length(u));
    halo = g * g * edge * (0.5 + 0.2 * input.flicker);
  }

  float a = max(shape_a, halo);
  float3 col = lerp(input.color.rgb, float3(1.0, 1.0, 1.0), input.flicker * 0.25 * shape_a);

  return float4(col, input.color.a * a);
}
