package org.ppsspp.ppsspp;
import android.graphics.*;

import java.nio.ByteBuffer;

public class TextRenderer {
	public static int measureText(String string, float textSize) {
		Paint p;
		p = new Paint(Paint.ANTI_ALIAS_FLAG);
		Rect bound = new Rect();
		p.setTextSize(textSize);
		p.getTextBounds(string, 0, string.length(), bound);
		return (bound.width() << 16) | bound.height();
	}
	public static short[] renderText(String string, float textSize) {
		Paint p;
		p = new Paint(Paint.ANTI_ALIAS_FLAG);
		Rect bound = new Rect();
		p.setTextSize(textSize);
		p.getTextBounds(string, 0, string.length(), bound);
		float baseline = -p.ascent();
		Bitmap bmp = Bitmap.createBitmap(bound.width(), bound.height(), Bitmap.Config.ARGB_4444);
		Canvas canvas = new Canvas(bmp);
		p.setColor(Color.WHITE);
		canvas.drawText(string, 0, baseline, p);

		int bufSize = bmp.getRowBytes() * bmp.getHeight() * 2;  // 2 = sizeof(ARGB_4444)
		ByteBuffer buf = ByteBuffer.allocate(bufSize);
		bmp.copyPixelsFromBuffer(buf);
		byte[] bytes = buf.array();

		// Output array size must match return value of measureText
		short[] output = new short[bound.width() * bound.height()];

		// 16-bit pixels but stored as bytes.
		for (int y = 0; y < bound.height(); y++) {
			int srcOffset = y * bmp.getRowBytes();
			int dstOffset = y * bound.width();
			for (int x = 0; x < bound.width(); x++) {
				int val = bytes[srcOffset + x * 2];
				output[dstOffset + x] = (short)val;
			}
		}
		return output;
	}
}
