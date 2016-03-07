#version 150

in vec3 normal;
in vec3 pos_cs;
in vec2 texture_coords;

uniform vec4 sundir;
uniform vec4 diffuse_colour;
uniform int have_shading_normals;
uniform int have_texture;
uniform sampler2D diffuse_tex;
uniform mat4 texture_matrix;
uniform float exponent;
uniform float fresnel_scale;

out vec4 colour_out;


float square(float x) { return x*x; }
float pow5(float x) { return x*x*x*x*x; }

float alpha2ForExponent(float exponent)
{
	return 2.0 / (exponent + 2.0);
}

float trowbridgeReitzPDF(float cos_theta, float alpha2)
{
	return cos_theta * alpha2 / (3.1415926535897932384626433832795 * square(square(cos_theta) * (alpha2 - 1.0) + 1.0));
}

// https://en.wikipedia.org/wiki/Schlick%27s_approximation
float fresnellApprox(float cos_theta, float ior)
{
	float r_0 = square((1.0 - ior) / (1.0 + ior));
	return r_0 + (1.0 - r_0)*pow5(1.0 - cos_theta);
}

void main()
{
	vec3 use_normal;
	if(have_shading_normals != 0)
	{
		use_normal = normal;
	}
	else
	{
		vec3 dp_dx = dFdx(pos_cs);    
		vec3 dp_dy = dFdy(pos_cs);  
		vec3 N_g = normalize(cross(dp_dx, dp_dy)); 
		use_normal = N_g;
	}
    vec3 unit_normal = normalize(use_normal);

	float light_cos_theta = max(dot(unit_normal, sundir.xyz), 0.0);

	vec3 frag_to_cam = normalize(pos_cs * -1.0);

	vec3 h = normalize(frag_to_cam + sundir.xyz);

	float h_cos_theta = max(0.0, dot(h, unit_normal));
	float specular = trowbridgeReitzPDF(h_cos_theta, alpha2ForExponent(exponent)) * 
		fresnellApprox(h_cos_theta, 1.5) * fresnel_scale;
 
	vec4 col;
	if(have_texture != 0)
		col = texture(diffuse_tex, (texture_matrix * vec4(texture_coords.x, texture_coords.y, 0.0, 1.0)).xy);
	else
		col = diffuse_colour;

	colour_out = col * (0.5 + light_cos_theta * 0.5) + vec4(specular);
}