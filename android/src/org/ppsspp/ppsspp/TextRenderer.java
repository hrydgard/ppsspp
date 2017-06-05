package org.ppsspp.ppsspp;
import android.graphics.*;

import java.nio.ByteBuffer;

public class TextRenderer {
	private static Paint p;
	private static Paint bg;
	static {
		p = new Paint(Paint.SUBPIXEL_TEXT_FLAG | Paint.ANTI_ALIAS_FLAG);
		p.setColor(Color.WHITE);
		bg = new Paint();
		bg.setColor(Color.BLACK);
	}
	public static int measureText(String string, double textSize) {
		Rect bound = new Rect();
		p.setTextSize((float)textSize);
		p.getTextBounds(string, 0, string.length(), bound);
		int w = bound.width();
		int h = bound.height();
		// Round width up to even already here to avoid annoyances from odd-width 16-bit textures which
		// OpenGL does not like - each line must be 4-byte aligned
		w = (w + 3) & ~1;
		h += 2;
		return (w << 16) | h;
	}
	public static int[] renderText(String string, double textSize) {
		Rect bound = new Rect();
		p.setTextSize((float)textSize);
		p.getTextBounds(string, 0, string.length(), bound);
		int w = bound.width();
		int h = bound.height();
		// Round width up to even already here to avoid annoyances from odd-width 16-bit textures which
		// OpenGL does not like - each line must be 4-byte aligned
		w = (w + 3) & ~1;
		h += 2;

		float baseline = -p.ascent();
		Bitmap bmp = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
		Canvas canvas = new Canvas(bmp);
		canvas.drawRect(0.0f, 0.0f, w, h, bg);
		p.setColor(Color.WHITE);
		canvas.drawText(string, 1, -bound.top + 1, p);

		int [] pixels = new int[w * h];
		bmp.getPixels(pixels, 0, w, 0, 0, w, h);
		return pixels;
	}
}
