#version 150

uniform sampler2D tex;

in vec2 texture_coords;

out vec4 colour_out;

void main()
{
	ivec2 tex_size = textureSize(tex, 0);
	float delta_x = 1.0 / float(tex_size.x);
	float delta_y = 1.0 / float(tex_size.y);
	
	float a = texture(tex, (vec4(texture_coords.x - delta_x, texture_coords.y - delta_y, 0.0, 1.0)).xy).x;
	float b = texture(tex, (vec4(texture_coords.x          , texture_coords.y - delta_y, 0.0, 1.0)).xy).x;
	float c = texture(tex, (vec4(texture_coords.x + delta_x, texture_coords.y - delta_y, 0.0, 1.0)).xy).x;
	float d = texture(tex, (vec4(texture_coords.x - delta_x, texture_coords.y          , 0.0, 1.0)).xy).x;
	//        texture(tex, (vec4(texture_coords.x          , texture_coords.y          , 0.0, 1.0)).xy).x;
	float f = texture(tex, (vec4(texture_coords.x + delta_x, texture_coords.y          , 0.0, 1.0)).xy).x;
	float g = texture(tex, (vec4(texture_coords.x - delta_x, texture_coords.y + delta_y, 0.0, 1.0)).xy).x;
	float h = texture(tex, (vec4(texture_coords.x          , texture_coords.y + delta_y, 0.0, 1.0)).xy).x;
	float i = texture(tex, (vec4(texture_coords.x + delta_x, texture_coords.y + delta_y, 0.0, 1.0)).xy).x;

	float G_x = -a + c - 2*d + 2*f - g + i;
	float G_y =  a + 2*b + c - g - 2*h - i;
	float G = sqrt(G_x*G_x + G_y*G_y) * 0.25;

	colour_out = vec4(112.0 / 255.0, 184.0 / 255.0, 247.0 / 255.0, G);
}