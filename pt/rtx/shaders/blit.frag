// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Ferguson
#version 450

layout(set = 0, binding = 0) uniform sampler2D overlay;

layout(push_constant) uniform Push
{
	vec4	view;		// x, y, width, height in pixels
	vec2	screen;		// width, height in pixels
	float	has_view;
} pc;

layout(location = 0) out vec4 color;

void main()
{
	vec2 p = gl_FragCoord.xy;
	vec3 bg = vec3(0.0);

	// placeholder until there is a traced image: a dark green gradient
	if (pc.has_view > 0.5 && p.x >= pc.view.x && p.y >= pc.view.y &&
		p.x < pc.view.x + pc.view.z && p.y < pc.view.y + pc.view.w)
	{
		float t = (p.y - pc.view.y) / max(pc.view.w, 1.0);
		bg = mix(vec3(0.03, 0.14, 0.07), vec3(0.01, 0.04, 0.02), t);
	}

	vec4 ov = texture(overlay, p / pc.screen);	// premultiplied
	color = vec4(ov.rgb + bg * (1.0 - ov.a), 1.0);
}
