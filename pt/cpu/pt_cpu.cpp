// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Ferguson
//
// CPU backend. For now it only owns the framebuffer and gets it on screen
// through GDI; the tracer itself comes later.

#include "../include/pt.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <vector>

namespace {

struct CpuBackend
{
	pt_backend_t	base{};
	HWND			hwnd = nullptr;
	HDC				memdc = nullptr;
	HBITMAP			dib = nullptr;
	HGDIOBJ			olddib = nullptr;
	uint32_t		*dibbits = nullptr;		// 0x00RRGGBB, top row first
	int				width = 0, height = 0;
	std::vector<uint32_t> scene;			// 0x00RRGGBB
	bool			has_view = false;
	pt_view_t		view{};
};

CpuBackend *Self(pt_backend_t *b) { return reinterpret_cast<CpuBackend *>(b); }

void Destroy(pt_backend_t *b)
{
	CpuBackend *s = Self(b);
	if (s->memdc)
	{
		if (s->olddib)
			SelectObject(s->memdc, s->olddib);
		DeleteDC(s->memdc);
	}
	if (s->dib)
		DeleteObject(s->dib);
	delete s;
}

void ClipView(const CpuBackend *s, int &x0, int &y0, int &x1, int &y1)
{
	x0 = s->view.x < 0 ? 0 : s->view.x;
	y0 = s->view.y < 0 ? 0 : s->view.y;
	x1 = s->view.x + s->view.width;
	y1 = s->view.y + s->view.height;
	if (x1 > s->width) x1 = s->width;
	if (y1 > s->height) y1 = s->height;
}

// placeholder until there is a scene to trace: a dark blue gradient
void RenderView(pt_backend_t *b, const pt_view_t *view)
{
	CpuBackend *s = Self(b);
	s->view = *view;
	s->has_view = true;

	int x0, y0, x1, y1;
	ClipView(s, x0, y0, x1, y1);
	for (int y = y0; y < y1; y++)
	{
		const int t = view->height > 1 ? (y - view->y) * 255 / (view->height - 1) : 0;
		const uint32_t r = 10 - t * 6 / 255;
		const uint32_t g = 16 - t * 10 / 255;
		const uint32_t bl = 40 - t * 26 / 255;
		const uint32_t c = (r << 16) | (g << 8) | bl;
		uint32_t *row = &s->scene[(size_t)y * s->width];
		for (int x = x0; x < x1; x++)
			row[x] = c;
	}
}

void Present(pt_backend_t *b, const uint32_t *overlay)
{
	CpuBackend *s = Self(b);

	int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
	if (s->has_view)
		ClipView(s, x0, y0, x1, y1);

	for (int y = 0; y < s->height; y++)
	{
		const uint32_t *ov = &overlay[(size_t)y * s->width];
		const uint32_t *sc = &s->scene[(size_t)y * s->width];
		uint32_t *out = &s->dibbits[(size_t)y * s->width];
		const bool rowin = s->has_view && y >= y0 && y < y1;

		for (int x = 0; x < s->width; x++)
		{
			const uint32_t o = ov[x];
			const uint32_t a = o >> 24;
			const uint32_t bg = (rowin && x >= x0 && x < x1) ? sc[x] : 0;
			// overlay is R,G,B,A bytes; the DIB wants 0x00RRGGBB
			const uint32_t orgb = ((o & 0xff) << 16) | (o & 0xff00) | ((o >> 16) & 0xff);

			if (a == 255)
				out[x] = orgb;
			else if (a == 0)
				out[x] = bg;
			else
			{
				const uint32_t ia = 255 - a;
				const uint32_t rb = (((bg & 0xff00ff) * ia + 0x800080) >> 8) & 0xff00ff;
				const uint32_t g = (((bg & 0x00ff00) * ia + 0x008000) >> 8) & 0x00ff00;
				out[x] = orgb + (rb | g);
			}
		}
	}
	s->has_view = false;

	HDC dc = GetDC(s->hwnd);
	if (dc)
	{
		BitBlt(dc, 0, 0, s->width, s->height, s->memdc, 0, 0, SRCCOPY);
		ReleaseDC(s->hwnd, dc);
	}
}

} // namespace

extern "C" pt_backend_t *pt_cpu_create(const pt_create_t *ci, char *err, int errlen)
{
	CpuBackend *s = new CpuBackend;
	s->base.name = "CPU path tracer";
	s->base.destroy = Destroy;
	s->base.render_view = RenderView;
	s->base.present = Present;
	s->hwnd = (HWND)ci->hwnd;
	s->width = ci->width;
	s->height = ci->height;
	s->scene.assign((size_t)ci->width * ci->height, 0);

	BITMAPINFO bmi{};
	bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
	bmi.bmiHeader.biWidth = ci->width;
	bmi.bmiHeader.biHeight = -ci->height;	// top-down
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	void *bits = nullptr;
	s->memdc = CreateCompatibleDC(nullptr);
	if (s->memdc)
		s->dib = CreateDIBSection(s->memdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
	if (!s->memdc || !s->dib || !bits)
	{
		snprintf(err, errlen, "could not create a %dx%d framebuffer", ci->width, ci->height);
		Destroy(&s->base);
		return nullptr;
	}
	s->dibbits = (uint32_t *)bits;
	s->olddib = SelectObject(s->memdc, s->dib);

	if (ci->log)
		ci->log("CPU path tracer: GDI presentation\n");
	return &s->base;
}
