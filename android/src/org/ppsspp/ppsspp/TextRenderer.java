package org.ppsspp.ppsspp;

import android.content.Context;
import android.graphics.*;
import android.provider.Settings;
import android.util.Log;
import android.view.accessibility.AccessibilityManager;

public class TextRenderer {
	private static Paint textPaint;
	private static Paint bg;
	private static Typeface robotoCondensed;
	private static final String TAG = "TextRenderer";

	private static boolean highContrastFontsEnabled = false;

	static {
		textPaint = new Paint(Paint.SUBPIXEL_TEXT_FLAG | Paint.ANTI_ALIAS_FLAG);
		textPaint.setColor(Color.WHITE);
		bg = new Paint();
		bg.setColor(0);
	}

	public static void init(Context ctx) {
		robotoCondensed = Typeface.createFromAsset(ctx.getAssets(), "Roboto-Condensed.ttf");
		if (robotoCondensed != null) {
			Log.i(TAG, "Successfully loaded Roboto Condensed");
			textPaint.setTypeface(robotoCondensed);
		} else {
			Log.e(TAG, "Failed to load Roboto Condensed");
		}
		highContrastFontsEnabled = Settings.Secure.getInt(ctx.getContentResolver(), "high_text_contrast_enabled", 0) == 1;
	}

	private static Point measureLine(String string, double textSize) {
		int w;
		if (string.length() > 0) {
			textPaint.setTextSize((float) textSize);
			w = (int) textPaint.measureText(string);
			// Round width up to even already here to avoid annoyances from odd-width 16-bit textures
			// which OpenGL does not like - each line must be 4-byte aligned
			w = (w + 5) & ~1;
		} else {
			w = 1;
		}
		int h = (int) (textPaint.descent() - textPaint.ascent() + 2.0f);
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
		total.y = (int) (textPaint.descent() - textPaint.ascent()) * lines.length + 2;
		// Returning a 0 size can create problems when the caller
		// uses the measurement to create a texture.
		// Also, clamp to a reasonable maximum size.
		if (total.x < 1)
			total.x = 1;
		if (total.y < 1)
			total.y = 1;
		if (total.x > 4096)
			total.x = 4096;
		if (total.y > 4096)
			total.y = 4096;
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

		Bitmap bmp = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
		Canvas canvas = new Canvas(bmp);
		canvas.drawRect(0.0f, 0.0f, w, h, bg);

		String lines[] = string.replaceAll("\\r", "").split("\n");
		float y = 1.0f;

		Path path = new Path();

		for (String line : lines) {
			if (line.length() > 0) {
				if (highContrastFontsEnabled) {
					// This is a workaround for avoiding "High Contrast Fonts" screwing up our
					// single-channel font rendering.
					// Unfortunately, emoji do not render on this path.
					if (path == null) {
						path = new Path();
					}
					textPaint.getTextPath(line, 0, line.length(), 1, -textPaint.ascent() + y, path);
					canvas.drawPath(path, textPaint);
				} else {
					// Standard text rendering, including emoji. Fine if high contrast fonts are not enabled.
					canvas.drawText(line, 1, -textPaint.ascent() + y, textPaint);
				}
			}
			y += textPaint.descent() - textPaint.ascent();
		}

		int[] pixels = new int[w * h];
		bmp.getPixels(pixels, 0, w, 0, 0, w, h);
		bmp.recycle();
		return pixels;
	}
}
