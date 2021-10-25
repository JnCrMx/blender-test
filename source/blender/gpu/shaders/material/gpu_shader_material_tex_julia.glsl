#define csquare(a) vec2(a.x*a.x-a.y*a.y, a.x*a.y+a.y*a.x)

float julia(vec2 p, vec2 c, int maxiter, float breakout, bool smoth)
{
    vec2 x = p;
    float breakoutsquared = breakout*breakout;

    for(int i=0; i<maxiter; i++)
    {
        x = csquare(x) + c;
        if(dot(x, x) > breakoutsquared)
        {
            return float(i) + (smoth?(1 - log(log2(length(x)))):0);
        }
    }

    return float(maxiter);
}

void node_tex_julia(
    vec3 co, vec3 rule, float iterations, float breakout, float smoth, out vec4 color)
{
  float it = julia(co.xy, rule.xy, int(iterations), breakout, smoth==1.0f);
  hsv_to_rgb(vec4(it/iterations, 1.0, it < iterations ? 1.0 : 0.0, 1.0), color);
}
