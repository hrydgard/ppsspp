package org.ppsspp.ppsspp;
import android.graphics.*;
import android.graphics.drawable.*;

import java.nio.ByteBuffer;

public class TextRenderer {
	static int measureText(String string, float textSize) {
		Paint p;
		p = new Paint(Paint.ANTI_ALIAS_FLAG);
		Rect bound = new Rect();
		p.setTextSize(textSize);
		p.getTextBounds(string, 0, string.length(), bound);
		int packedBounds = (bound.width() << 16) | bound.height();
		return packedBounds;
	}
	static short[] renderText(String string, float textSize) {
		Paint p;
		p = new Paint(Paint.ANTI_ALIAS_FLAG);
		Rect bound = new Rect();
		p.setTextSize(textSize);
		p.getTextBounds(string, 0, string.length(), bound);
		Bitmap bmp = Bitmap.createBitmap(bound.width(), bound.height(), Bitmap.Config.ARGB_4444);
		Canvas canvas = new Canvas(bmp);
		p.setColor(Color.WHITE);
		canvas.drawText(string, 0, 0, p);

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
				val = (val << 12) | 0xFFF;
				output[dstOffset + x] = (short)val;
			}
		}
		return output;
	}
}
