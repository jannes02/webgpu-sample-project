

struct FragmentInput {
    //position: vec4f,
    @location(0) color: vec3f,
};

@fragment
fn fragmentMain(in: FragmentInput) -> @location(0) vec4f {
        return vec4f(in.color, 1.0);
    }