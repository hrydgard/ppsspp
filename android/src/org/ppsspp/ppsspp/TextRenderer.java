package org.ppsspp.ppsspp;

import android.content.Context;
import android.graphics.*;
import android.provider.Settings;
import android.util.Log;

import androidx.annotation.Keep;

import java.util.HashMap;

public class TextRenderer {
	private static class Font {
		public Paint paint;
		public Typeface typeFace;
	}

	private static Paint textPaint = null;

	private static final Paint bg;
	private static final String TAG = "TextRenderer";

	private static int idGen = 1;

	private static HashMap<java.lang.Integer, Typeface> fontMap = new HashMap<>();

	private static boolean highContrastFontsEnabled = false;

	static {
		bg = new Paint();
		bg.setColor(0);
	}

	@Keep
	public static int allocFont(Context ctx, String ttfFile) {
		try {
			Typeface typeFace = Typeface.createFromAsset(ctx.getAssets(), ttfFile);
			if (typeFace != null) {
				Log.i(TAG, "Successfully loaded typeface from " + ttfFile);
			} else {
				Log.e(TAG, "Failed to load asset file " + ttfFile);
			}
			int id = idGen++;
			fontMap.put(id, typeFace);
			return id;
		} catch (Exception e) {
			Log.e(TAG, "Exception when loading typeface. shouldn't happen but is reported. We just fall back." + e);
			return -1337;
		}
	}

	@Keep
	public static void freeAllFonts() {
		fontMap.clear();
	}

	public static void init(Context ctx) {
		Log.i(TAG, "initializing TextDrawerAndroid java side");
		textPaint = new Paint(Paint.SUBPIXEL_TEXT_FLAG | Paint.ANTI_ALIAS_FLAG);
		textPaint.setColor(Color.WHITE);
		highContrastFontsEnabled = Settings.Secure.getInt(ctx.getContentResolver(), "high_text_contrast_enabled", 0) == 1;
	}

	private static Point measureLine(String string, int font, double textSize) {
		textPaint.setTextSize((float)textSize);
		int w = (int) textPaint.measureText(string);
		// Round width up to even already here to avoid annoyances from odd-width 16-bit textures
		// which OpenGL does not like - each line must be 4-byte aligned
		w = (w + 1) & ~1;
		w += 2;
		int h = (int) (textPaint.descent() - textPaint.ascent() + 2.0f);
		Point p = new Point();
		p.x = w;
		p.y = h;
		return p;
	}

	private static Point measure(String string, int font, double textSize) {
		String [] lines = string.replaceAll("\r", "").split("\n");
		Point total = new Point();
		total.x = 0;
		for (String line : lines) {
			Point sz = measureLine(line, font, textSize);
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

	@Keep
	public static int measureText(String string, int font, double textSize) {
		textPaint.setTypeface(fontMap.get(font));
		textPaint.setTextSize((float) textSize);
		Point s = measure(string, font, textSize);
		return (s.x << 16) | s.y;
	}
	public static int[] renderText(String string, int font, double textSize, int w, int h) {
		textPaint.setTypeface(fontMap.get(font));
		textPaint.setTextSize((float) textSize);

		Bitmap bmp = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
		Canvas canvas = new Canvas(bmp);
		canvas.drawRect(0.0f, 0.0f, w, h, bg);

		String [] lines = string.replaceAll("\\r", "").split("\n");
		float y = 1.0f;

		Path path = null;
		for (String line : lines) {
			if (!line.isEmpty()) {
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
