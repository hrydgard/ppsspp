package org.ppsspp.ppsspp;

import android.content.Context;
import android.graphics.*;
import android.util.Log;

public class TextRenderer {
	private static Paint p;
	private static Paint bg;
	private static Typeface robotoCondensed;
	private static final String TAG = "TextRenderer";

	static {
		p = new Paint(Paint.SUBPIXEL_TEXT_FLAG | Paint.ANTI_ALIAS_FLAG);
		p.setColor(Color.WHITE);
		bg = new Paint();
		bg.setColor(Color.BLACK);
	}

	public static void init(Context ctx) {
		robotoCondensed = Typeface.createFromAsset(ctx.getAssets(), "Roboto-Condensed.ttf");
		if (robotoCondensed != null) {
			Log.i(TAG, "Successfully loaded Roboto Condensed");
			p.setTypeface(robotoCondensed);
		} else {
			Log.e(TAG, "Failed to load Roboto Condensed");
		}
	}

	private static Point measureLine(String string, double textSize) {
		int w;
		if (string.length() > 0) {
			p.setTextSize((float) textSize);
			w = (int) p.measureText(string);
			// Round width up to even already here to avoid annoyances from odd-width 16-bit textures
			// which OpenGL does not like - each line must be 4-byte aligned
			w = (w + 5) & ~1;
		} else {
			w = 1;
		}
		int h = (int) (p.descent() - p.ascent() + 2.0f);
		Point p = new Point();
		p.x = w;
		p.y = h;
		return p;
	}

	private static Point measure(String string, double textSize) {
		String lines[] = string.replaceAll("\\r", "").split("\n");
		Point total = new Point();
		total.x = 0;
		for (String line : lines) {
			Point sz = measureLine(line, textSize);
			total.x = Math.max(sz.x, total.x);
		}
		total.y = (int) (p.descent() - p.ascent()) * lines.length + 2;
		return total;
	}

	public static int measureText(String string, double textSize) {
		Point s = measure(string, textSize);
		return (s.x << 16) | s.y;
	}

	public static int[] renderText(String string, double textSize) {
		Point s = measure(string, textSize);

		int w = s.x;
		int h = s.y;
		if (w == 0)
			w = 1;
		if (h == 0)
			h = 1;

		Bitmap bmp = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
		Canvas canvas = new Canvas(bmp);
		canvas.drawRect(0.0f, 0.0f, w, h, bg);

		String lines[] = string.replaceAll("\\r", "").split("\n");
		float y = 1.0f;
		for (String line : lines) {
			if (line.length() > 0)
				canvas.drawText(line, 1, -p.ascent() + y, p);
			y += p.descent() - p.ascent();
		}

		int[] pixels = new int[w * h];
		bmp.getPixels(pixels, 0, w, 0, 0, w, h);
		bmp.recycle();
		return pixels;
	}
}
