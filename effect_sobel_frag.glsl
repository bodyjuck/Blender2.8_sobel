/*
 * Copyright 2018, Blender Foundation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Contributor(s): Kinouti Takahiro
 *
 */
uniform sampler2D tex_depth;
uniform sampler2D tex_color;
uniform sampler2D tex_normal;
uniform vec2 offset;
uniform float z_near;
uniform float z_far;
uniform float normal_threshold;	
uniform float normal_strength;
uniform float normal_depth_decay;
uniform float depth_threshold;
uniform float depth_strength;
uniform vec3 line_color;
uniform float line_thickness;

in vec4 uvcoordsvar;
out vec4 FragColor;

float FetchDepth(sampler2D tex, vec2 uv){
	float z_raw = 2*texture(tex, uv).r - 1;
	return 2 * z_near * (z_far / (z_far + z_near - z_raw * (z_far - z_near)));
}
float DetectEdgeDepth(sampler2D tex, float threshold, float strength, vec2 uv){
	float samples[8];
	vec2 ofs = offset * line_thickness;
	samples[0] = FetchDepth(tex, vec2(uv.x-ofs.x, uv.y-ofs.y));
	samples[1] = FetchDepth(tex, vec2(uv.x-ofs.x, uv.y));
	samples[2] = FetchDepth(tex, vec2(uv.x-ofs.x, uv.y+ofs.y));
	samples[3] = FetchDepth(tex, vec2(uv.x,		  uv.y-ofs.y));
	samples[4] = FetchDepth(tex, vec2(uv.x,		  uv.y+ofs.y));
	samples[5] = FetchDepth(tex, vec2(uv.x+ofs.x, uv.y-ofs.y));
	samples[6] = FetchDepth(tex, vec2(uv.x+ofs.x, uv.y));
	samples[7] = FetchDepth(tex, vec2(uv.x+ofs.x, uv.y+ofs.y));

	// Convolution
	const vec4 coeff_conv = vec4(-2.0, -1.0, 1.0, 2.0);
	vec4 sample_x = vec4(samples[3], samples[5], samples[2], samples[4]);
	vec4 sample_y = vec4(samples[1], samples[2], samples[5], samples[6]);
	vec2 sample_terminal = vec2(samples[7] - samples[0]);
	vec2 s = vec2(dot(sample_x, coeff_conv), dot(sample_y, coeff_conv)) + sample_terminal;
	float val = dot(s, s);

	return (val > threshold*threshold) ? sqrt(val)*strength : 0;
}

vec4 FetchNormal(sampler2D tex, vec2 uv){
	vec2 rg = texture(tex, uv.xy).rg;
	return vec4(rg.r, rg.g, sqrt(1 - dot(rg, rg)), 0);
}

float DetectEdgeNormal(sampler2D tex, float threshold, float strength, vec2 uv, float depth_decay){
	vec4 samples[8];
	vec2 ofs = offset * line_thickness;
	samples[0] = FetchNormal(tex, vec2(uv.x-ofs.x, uv.y-ofs.y));
	samples[1] = FetchNormal(tex, vec2(uv.x-ofs.x, uv.y));
	samples[2] = FetchNormal(tex, vec2(uv.x-ofs.x, uv.y+ofs.y));
	samples[3] = FetchNormal(tex, vec2(uv.x,	   uv.y-ofs.y));
	samples[4] = FetchNormal(tex, vec2(uv.x,	   uv.y+ofs.y));
	samples[5] = FetchNormal(tex, vec2(uv.x+ofs.x, uv.y-ofs.y));
	samples[6] = FetchNormal(tex, vec2(uv.x+ofs.x, uv.y));
	samples[7] = FetchNormal(tex, vec2(uv.x+ofs.x, uv.y+ofs.y));

	// Convolution
	vec3 g = vec3(0);
	const vec4 coeff_conv = vec4(-2.0, -1.0, 1.0, 2.0);
	for(int i = 0; i < 3/*xyz*/; ++i){
		vec4 sample_x = vec4(samples[3][i], samples[5][i], samples[2][i], samples[4][i]);
		vec4 sample_y = vec4(samples[1][i], samples[2][i], samples[5][i], samples[6][i]);
		vec2 sample_terminal = vec2(samples[7][i] - samples[0][i]);
		vec2 s = vec2(dot(sample_x, coeff_conv), dot(sample_y, coeff_conv)) + sample_terminal;
		g[i] = dot(s, s);
	}

	float val = max(max(g.r, g.g), g.b) * depth_decay;
	return (val > threshold*threshold) ? sqrt(val)*strength : 0;
}

void main()
{
	vec4 col = texture(tex_color, uvcoordsvar.xy);
	float factor = (depth_strength != 0) ? DetectEdgeDepth(tex_depth, depth_threshold, depth_strength, uvcoordsvar.xy) : 0;
	float raw_depth = texture(tex_depth, uvcoordsvar.xy).r;
	if(raw_depth < 1){
		// It seems that normal buffer is not cleared. Thus skip normal pass on background pixel.
		factor += (normal_strength != 0) ? DetectEdgeNormal(tex_normal, normal_threshold, normal_strength, uvcoordsvar.xy, (1 - pow(raw_depth, 1/normal_depth_decay))) : 0;
	}

	factor = clamp(factor, 0, 1);
	gl_FragColor = vec4(vec3((1-factor)*col.rgb + factor*line_color), 1);
}