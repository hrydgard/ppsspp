package org.ppsspp.ppsspp;
import android.content.Context;
import android.graphics.*;
import android.util.Log;

import java.nio.ByteBuffer;

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
	private static Point measure(String string, double textSize) {
		Rect bound = new Rect();
		p.setTextSize((float)textSize);
		p.getTextBounds(string, 0, string.length(), bound);
		int w = bound.width();
		int h = (int)(p.descent() - p.ascent() + 2.0f);
		// Round width up to even already here to avoid annoyances from odd-width 16-bit textures which
		// OpenGL does not like - each line must be 4-byte aligned
		w = (w + 5) & ~1;
		Point p = new Point();
		p.x = w;
		p.y = h;
		return p;
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
		p.setColor(Color.WHITE);
		canvas.drawText(string, 1, -p.ascent() + 1, p);

		int [] pixels = new int[w * h];
		bmp.getPixels(pixels, 0, w, 0, 0, w, h);
		bmp.recycle();
		return pixels;
	}
}
