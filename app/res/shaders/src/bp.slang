import modules.common;

struct vs_input {
  [[vk::location(0)]] float2 pos;
  [[vk::location(1)]] float4 circ;
  [[vk::location(2)]] float4 uv;
  [[vk::location(3)]] float4 color;
  [[vk::location(4)]] float2 shape;
};

struct vs_output {
  float4 clipspace : SV_Position;
  [[vk::location(0)]] float2 uv;
  [[vk::location(1)]] float2 uv2;
  [[vk::location(2)]] float4 color;
  [[vk::location(3)]] float2 dimensions;
  [[vk::location(4)]] float2 shape;
};

[[vk::binding(0, 0)]] ConstantBuffer<Global> global;
[[vk::binding(1, 0)]] Sampler2D tex_atlas;

vs_output vs_main(vs_input input) {
  vs_output output;

  // shape.y == 4: continuous "ribbon" segment used for transparent snake
  // bodies. circ = (A.xy, B.xy) in screen pixels, uv = (offA.xy, offB.xy) are
  // the (miter-joined) half-width offset vectors at each end and shape.x is
  // the half width in pixels. Neighbouring segments share their end vertices
  // exactly, so the body is one gap-free, overlap-free surface and a
  // semi-transparent colour is blended exactly once per pixel (same result as
  // NTL's single stroked canvas path).
  if (input.shape.y > 3.5 && input.shape.y < 4.5) {
    float t = input.pos.x;
    float side = input.pos.y * 2.0 - 1.0;
    float2 a = input.circ.xy;
    float2 b = input.circ.zw;
    float2 offa = input.uv.xy;
    float2 offb = input.uv.zw;
    float2 wp = lerp(a, b, t) + side * lerp(offa, offb, t);
    float2 hv = global.viewport * 0.5;
    float2 o = (wp - hv) / hv;
    output.clipspace = float4(o, 1);
    output.uv = float2(t, side);
    output.uv2 = float2(0, 0);
    output.color = input.color;
    output.dimensions = float2(0, 0);
    output.shape = input.shape;
    return output;
  }

  // shape.y == 5 (half-disc end cap) uses the normal circle-sprite layout.
  bool is_capsule = input.shape.y > 0.5 && input.shape.y < 4.5;
  float2 dimensions = is_capsule
                          ? float2(input.circ.z, input.shape.x)
                          : float2(input.circ.z, input.circ.z);
  float2 p = (input.pos - 0.5) * (dimensions + EDGE_SOFTNESS);

  float s = sin(input.circ.w);
  float c = cos(input.circ.w);

  float2 r;
  if (is_capsule) {
    r = float2(p.x * c - p.y * s, p.x * s + p.y * c) +
        0.5 * dimensions;
  } else {
    // Preserve the original circle-sprite positioning exactly.
    r = float2(p.x * c - p.y * s, p.x * s + p.y * c) +
        0.5 * (input.circ.z + EDGE_SOFTNESS * 0.5);
  }

  float2 hv = global.viewport * 0.5;
  float2 wp = r + input.circ.xy;
  float2 o = (wp - hv) / hv;

  output.clipspace = float4(o, 1);
  output.uv = input.pos;
  output.uv2 = input.uv.xy + input.pos * input.uv.zw;
  output.color = input.color;
  output.dimensions = dimensions;
  output.shape = input.shape;

  return output;
}

float4 fs_main(vs_output input) {
  if (input.shape.y > 3.5 && input.shape.y < 4.5) {
    // Ribbon: distance (pixels) from this fragment to the side edge.
    float edge_px = (1.0 - abs(input.uv.y)) * input.shape.x;
    float alpha = saturate(edge_px + 0.5);
    return float4(input.color.rgb, input.color.a * alpha);
  }

  if (input.shape.y > 4.5) {
    // Half disc (round end cap of a ribbon): keep only the +x half of the
    // sprite in its rotated frame.
    float d = length(2 * input.uv - 1);
    float p = length(float2(ddx(d), ddy(d)));
    float a = smoothstep(0.0, EDGE_SOFTNESS, (1 - d) / p);
    float keep = step(0.5, input.uv.x);
    return float4(input.color.rgb, input.color.a * a * keep);
  }

  if (input.shape.y > 1.5) {
    // Rect shape (shape.y == 2): square / rectangle with hard corners, sized
    // by input.dimensions (same quad layout as the capsule path below).
    float2 p = (input.uv - 0.5) * (input.dimensions + EDGE_SOFTNESS);
    float2 q = abs(p) - input.dimensions * 0.5;
    float distance_to_edge = max(q.x, q.y);
    float aa = max(length(float2(ddx(distance_to_edge), ddy(distance_to_edge))), 0.75);
    float alpha = 1.0 - smoothstep(-aa, aa, distance_to_edge);
    return float4(input.color.rgb, input.color.a * alpha);
  }

  if (input.shape.y > 0.5) {
    float2 p = (input.uv - 0.5) * (input.dimensions + EDGE_SOFTNESS);
    float radius = input.shape.x * 0.5;
    float segment_half =
        max(0.0, (input.dimensions.x - input.shape.x) * 0.5);
    float2 q = float2(max(abs(p.x) - segment_half, 0.0), p.y);
    float distance_to_edge = length(q) - radius;
    float aa = max(length(float2(ddx(distance_to_edge), ddy(distance_to_edge))), 0.75);
    float alpha = 1.0 - smoothstep(-aa, aa, distance_to_edge);
    return float4(input.color.rgb, input.color.a * alpha);
  }

  float4 c = tex_atlas.Sample(input.uv2).rgba;
  float d = length(2 * input.uv - 1);
  float p = length(float2(ddx(d), ddy(d)));
  float a = smoothstep(0.0, EDGE_SOFTNESS, (1 - d) / p);
  return input.color * float4(c.rgb, a * c.a);
}
