
struct MyUniforms{
    color: vec4f,
    time: f32,
}

@group(0) @binding(0) var<uniform> uMyUniforms: MyUniforms;

struct VertexInput {
    @location(0) position: vec2f,
    @location(1) color: vec3f,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) color: vec3f,
};

@vertex
fn vertexMain(in: VertexInput) -> VertexOutput {
    let ratio = 1600.0 / 900.0;
    var offset = vec2f(-0.5, -0.5);
    offset += 0.3 * vec2f(cos(uMyUniforms.time), sin(uMyUniforms.time));
    var out: VertexOutput;
    out.position = vec4f(in.position.x+offset.x, (in.position.y + offset.y) * ratio, 0.0, 1.0);
    out.color = in.color * uMyUniforms.color.rgb;
    return out;
    }
