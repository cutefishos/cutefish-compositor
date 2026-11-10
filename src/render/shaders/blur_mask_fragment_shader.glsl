varying vec2 vTexCoord;
uniform sampler2D uTexture;
uniform vec4 uSampleRect;
uniform vec2 uSize;
uniform float uRadius;
uniform float uOpacity;
// Lowest and highest texture coordinates sampled, as in blur_fragment_shader.
uniform vec4 uClamp;
// A client shape rasterized over the quad replaces the rounded rectangle.
uniform sampler2D uShape;
uniform float uHasShape;
// A premultiplied tint over the blurred backdrop, then a straight colour
// blended over that by uFillMode (BackdropBlend; 0 for none).
uniform vec4 uFillTint;
uniform float uFillMode;
uniform vec4 uFillColor;

float lum(vec3 c) {
    return dot(c, vec3(0.3, 0.59, 0.11));
}

vec3 clipColor(vec3 c) {
    float l = lum(c);
    float n = min(min(c.r, c.g), c.b);
    float x = max(max(c.r, c.g), c.b);
    if (n < 0.0)
        c = l + (c - l) * l / max(l - n, 1e-5);
    if (x > 1.0)
        c = l + (c - l) * (1.0 - l) / max(x - l, 1e-5);
    return c;
}

vec3 setLum(vec3 c, float l) {
    return clipColor(c + (l - lum(c)));
}

float sat(vec3 c) {
    return max(max(c.r, c.g), c.b) - min(min(c.r, c.g), c.b);
}

vec3 setSat(vec3 c, float s) {
    float n = min(min(c.r, c.g), c.b);
    float x = max(max(c.r, c.g), c.b);
    return x > n ? (c - n) * s / (x - n) : vec3(0.0);
}

float softLight(float b, float s) {
    float d = b <= 0.25 ? ((16.0 * b - 12.0) * b + 4.0) * b : sqrt(b);
    return s <= 0.5 ? b - (1.0 - 2.0 * s) * b * (1.0 - b) : b + (2.0 * s - 1.0) * (d - b);
}

float hardLight(float b, float s) {
    return s <= 0.5 ? b * 2.0 * s : 1.0 - (1.0 - b) * (1.0 - (2.0 * s - 1.0));
}

// B(backdrop, source) of the separable and non-separable modes.
vec3 blend(vec3 b, vec3 s, float mode) {
    if (mode < 1.5)
        return s;
    if (mode < 2.5)
        return b * s;
    if (mode < 3.5)
        return b + s - b * s;
    if (mode < 4.5)
        return vec3(hardLight(s.r, b.r), hardLight(s.g, b.g), hardLight(s.b, b.b));
    if (mode < 5.5)
        return min(b, s);
    if (mode < 6.5)
        return max(b, s);
    if (mode < 7.5)
        return vec3(b.r == 0.0 ? 0.0 : (s.r >= 1.0 ? 1.0 : min(1.0, b.r / (1.0 - s.r))),
                    b.g == 0.0 ? 0.0 : (s.g >= 1.0 ? 1.0 : min(1.0, b.g / (1.0 - s.g))),
                    b.b == 0.0 ? 0.0 : (s.b >= 1.0 ? 1.0 : min(1.0, b.b / (1.0 - s.b))));
    if (mode < 8.5)
        return vec3(b.r >= 1.0 ? 1.0 : (s.r <= 0.0 ? 0.0 : 1.0 - min(1.0, (1.0 - b.r) / s.r)),
                    b.g >= 1.0 ? 1.0 : (s.g <= 0.0 ? 0.0 : 1.0 - min(1.0, (1.0 - b.g) / s.g)),
                    b.b >= 1.0 ? 1.0 : (s.b <= 0.0 ? 0.0 : 1.0 - min(1.0, (1.0 - b.b) / s.b)));
    if (mode < 9.5)
        return vec3(hardLight(b.r, s.r), hardLight(b.g, s.g), hardLight(b.b, s.b));
    if (mode < 10.5)
        return vec3(softLight(b.r, s.r), softLight(b.g, s.g), softLight(b.b, s.b));
    if (mode < 11.5)
        return abs(b - s);
    if (mode < 12.5)
        return b + s - 2.0 * b * s;
    if (mode < 13.5)
        return setLum(setSat(s, sat(b)), lum(b));
    if (mode < 14.5)
        return setLum(setSat(b, sat(s)), lum(b));
    if (mode < 15.5)
        return setLum(s, lum(b));
    return setLum(b, lum(s));
}

void shade() {
    vec2 point = vTexCoord * uSize;
    vec2 delta = abs(point - uSize * 0.5) - (uSize * 0.5 - vec2(uRadius));
    float distance = length(max(delta, vec2(0.0)))
                   + min(max(delta.x, delta.y), 0.0) - uRadius;
    float mask = mix(clamp(0.5 - distance, 0.0, 1.0), texture2D(uShape, vTexCoord).a, uHasShape);
    vec2 uv = uSampleRect.xy + vTexCoord * uSampleRect.zw;
    vec4 color = texture2D(uTexture, clamp(vec2(uv.x, 1.0 - uv.y), uClamp.xy, uClamp.zw));
    if (uFillMode > 0.5) {
        color = uFillTint + color * (1.0 - uFillTint.a);
        vec3 backdrop = color.a > 0.0 ? color.rgb / color.a : vec3(0.0);
        vec3 mixed = (1.0 - color.a) * uFillColor.rgb + color.a * blend(backdrop, uFillColor.rgb, uFillMode);
        color = vec4(uFillColor.a * mixed + (1.0 - uFillColor.a) * color.rgb,
                     uFillColor.a + color.a * (1.0 - uFillColor.a));
    }
    gl_FragColor = color * mask * uOpacity;
}
